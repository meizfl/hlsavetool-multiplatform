#include "oodle.h"

/* Compressed block signature bytes (little-endian on disk) */
static const uint8_t block_signature[] = { 0xC1, 0x83, 0x2A, 0x9E };

/* ── Lazily-loaded Oodle function pointers ───────────────────────────── */
static lib_handle_t                           Oodle_Handle       = NULL;
static OodleLZ_Compress_FP                   *OodleLZ_Compress   = NULL;
static OodleLZ_Decompress_FP                 *OodleLZ_Decompress = NULL;
static OodleLZ_GetCompressedBufferSizeNeeded_FP *OodleLZ_SizeNeeded  = NULL;
static OodleLZ_CompressOptions_GetDefault_FP *OodleLZ_Opts       = NULL;

/*
 * Build an absolute path "<exe_dir>/<libname>" so dlopen finds the library
 * placed next to the executable regardless of the working directory or
 * LD_LIBRARY_PATH settings.
 */
static void build_lib_path(char *out, size_t out_sz, const char *libname)
{
#if defined(_WIN32)
  /* GetModuleFileName gives the full path of the .exe */
  char exe[4096] = {0};
  GetModuleFileNameA(NULL, exe, sizeof(exe) - 1);
  char *last = strrchr(exe, '\\');
  if (last) { *(last + 1) = '\0'; snprintf(out, out_sz, "%s%s", exe, libname); }
  else       { snprintf(out, out_sz, "%s", libname); }
#elif defined(__linux__)
  char exe[4096] = {0};
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n > 0) {
    exe[n] = '\0';
    char *last = strrchr(exe, '/');
    if (last) { *(last + 1) = '\0'; snprintf(out, out_sz, "%s%s", exe, libname); return; }
  }
  snprintf(out, out_sz, "%s", libname);
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
  char exe[4096] = {0};
  uint32_t sz = sizeof(exe);
  if (_NSGetExecutablePath(exe, &sz) == 0) {
    char *last = strrchr(exe, '/');
    if (last) { *(last + 1) = '\0'; snprintf(out, out_sz, "%s%s", exe, libname); return; }
  }
  snprintf(out, out_sz, "%s", libname);
#else
  snprintf(out, out_sz, "%s", libname);
#endif
}

/* ── Library init / teardown ─────────────────────────────────────────── */
static void InitOodleLibrary(void)
{
  char lib_path[4096];
  build_lib_path(lib_path, sizeof(lib_path), OODLE_LIB_NAME);

  /* Try path relative to exe first, then fall back to system search */
  Oodle_Handle = lib_open(lib_path);
  if (!Oodle_Handle)
    Oodle_Handle = lib_open(OODLE_LIB_NAME);

  if (!Oodle_Handle) {
    printf_error("Cannot load \"%s\": %s", OODLE_LIB_NAME, lib_error());
    printf_error("Put the Oodle library next to the executable.\n"
                 "  Windows : oo2core_9_win64.dll\n"
                 "  Linux   : liboo2core.so\n"
                 "  macOS   : liboo2core.dylib\n"
                 "Download from: "
                 "https://github.com/new-world-tools/go-oodle/releases/tag/v0.2.3-files");
    exit(EXIT_FAILURE);
  }

  GET_PROC(Oodle_Handle, OodleLZ_Compress,   OodleLZ_Compress_FP,   "OodleLZ_Compress");
  GET_PROC(Oodle_Handle, OodleLZ_Decompress, OodleLZ_Decompress_FP, "OodleLZ_Decompress");
  GET_PROC(Oodle_Handle, OodleLZ_SizeNeeded, OodleLZ_GetCompressedBufferSizeNeeded_FP,
                         "OodleLZ_GetCompressedBufferSizeNeeded");
  GET_PROC(Oodle_Handle, OodleLZ_Opts, OodleLZ_CompressOptions_GetDefault_FP,
                         "OodleLZ_CompressOptions_GetDefault");
}

static void ReleaseOodleLibrary(void)
{
  if (Oodle_Handle) {
    lib_close(Oodle_Handle);
    Oodle_Handle = NULL;
  }
  OodleLZ_Compress   = NULL;
  OodleLZ_Decompress = NULL;
  OodleLZ_SizeNeeded  = NULL;
  OodleLZ_Opts       = NULL;
}

/* ── compress ────────────────────────────────────────────────────────── */
void compress(UProperty *property, bool verbose)
{
  UArrayProperty *arr = (UArrayProperty *)property->data;
  printf_verbose(verbose, "Compressing %u bytes...", arr->size);
  InitOodleLibrary();

  uint8_t *raw = (uint8_t *)arr->value;

  /* Validate SQLite magic */
  SqliteHeader sqlite_hdr;
  uint8_t *tmp = raw;
  COPY_MEMORY(tmp, &sqlite_hdr, sizeof(sqlite_hdr));
  if (memcmp(sqlite_hdr.magic, SQLITE_HEADER_SIGNATURE, SQLITE_HEADER_SIGNATURE_LEN) != 0) {
    printf_error("Expected SQLite signature, got \"%.*s\"",
                 (int)SQLITE_HEADER_SIGNATURE_LEN, sqlite_hdr.magic);
    exit(EXIT_FAILURE);
  }

  uint16_t page_size = bswap16(sqlite_hdr.page_size);
  uint32_t db_pages  = bswap32(sqlite_hdr.database_size);
  uint32_t sqlite_sz = page_size * db_pages;
  printf_verbose(verbose, " SQLite page_size=%u db_pages=%u calc_size=%u",
                 page_size, db_pages, sqlite_sz);

  if (sqlite_sz != arr->size) {
    printf_error("SQLite size mismatch: expected %u, actual %u", arr->size, sqlite_sz);
    exit(EXIT_FAILURE);
  }

  /* Prepend UpkOodleSqliteSize header */
  UpkOodleSqliteSize upk_sz = {
    sqlite_sz + SQLITE_UPK_HEADER_ADDED_LENGTH,
    sqlite_sz
  };
  size_t new_input_sz = arr->size + sizeof(upk_sz);
  uint8_t *new_input  = (uint8_t *)realloc(arr->value, new_input_sz);
  if (!new_input) {
    printf_error("realloc(%zu) failed", new_input_sz);
    exit(EXIT_FAILURE);
  }
  memmove(new_input + sizeof(upk_sz), new_input, arr->size);
  memcpy(new_input, &upk_sz, sizeof(upk_sz));
  arr->value = new_input;
  arr->size  = (uint32_t)new_input_sz;

  /* Compress in chunks of OODLE_MAX_BLOCK_SIZE */
  size_t result_cap = arr->size;           /* compressed output <= input size */
  SAFE_ALLOC_SIZE(uint8_t, result, result_cap);
  size_t result_sz = 0;

  UpkOodle upk;
  memset(&upk, 0, sizeof(upk));
  upk.signature     = OODLE_COMPRESSED_BLOCK_SIGNATURE;
  upk.max_block_size = OODLE_MAX_BLOCK_SIZE;

  OodleLZ_CompressOptions *opts =
      OodleLZ_Opts(OodleLZ_Compressor_Kraken, OodleLZ_CompressionLevel_Fast);

  uint8_t *src       = (uint8_t *)arr->value;
  size_t   remaining = arr->size;
  uint32_t chunk_idx = 1;

  while (remaining > 0) {
    size_t chunk = remaining < OODLE_MAX_BLOCK_SIZE ? remaining : OODLE_MAX_BLOCK_SIZE;
    printf_verbose(verbose, "Block #%u: uncompressed=%zu", chunk_idx, chunk);

    intptr_t need = OodleLZ_SizeNeeded(OodleLZ_Compressor_Kraken, (uintptr_t)chunk);
    SAFE_ALLOC_SIZE(uint8_t, out_buf, (size_t)need);

    int comp_bytes = OodleLZ_Compress(
        OodleLZ_Compressor_Kraken,
        src, chunk,
        out_buf,
        OodleLZ_CompressionLevel_Fast, opts,
        NULL, NULL, NULL, 0
    );
    if (comp_bytes <= 0) {
      printf_error("OodleLZ_Compress returned %d for block #%u", comp_bytes, chunk_idx);
      exit(EXIT_FAILURE);
    }
    printf_verbose(verbose, "Block #%u: compressed=%d", chunk_idx, comp_bytes);

    upk.blocks[0].compressed_size   = (uint64_t)comp_bytes;
    upk.blocks[1].compressed_size   = (uint64_t)comp_bytes;
    upk.blocks[0].uncompressed_size  = (uint64_t)chunk;
    upk.blocks[1].uncompressed_size  = (uint64_t)chunk;

    /* Grow result buffer if needed */
    size_t needed = result_sz + sizeof(upk) + (size_t)comp_bytes;
    if (needed > result_cap) {
      result_cap = needed * 2;
      uint8_t *tmp2 = (uint8_t *)realloc(result, result_cap);
      if (!tmp2) { printf_error("realloc failed"); exit(EXIT_FAILURE); }
      result = tmp2;
    }
    memcpy(result + result_sz, &upk, sizeof(upk));
    result_sz += sizeof(upk);
    memcpy(result + result_sz, out_buf, (size_t)comp_bytes);
    result_sz += (size_t)comp_bytes;

    free(out_buf);
    src       += chunk;
    remaining -= chunk;
    chunk_idx++;
  }

  free(arr->value);
  arr->value      = result;
  arr->size       = (uint32_t)result_sz;
  property->length = (uint64_t)result_sz + UARRAYPROPERTY_ADDED_LENGTH;

  ReleaseOodleLibrary();
}

/* ── decompress ──────────────────────────────────────────────────────── */
void decompress(UProperty *property, bool verbose)
{
  UArrayProperty *arr = (UArrayProperty *)property->data;
  printf_verbose(verbose, "Decompressing %u bytes...", arr->size);
  InitOodleLibrary();

  /* Allocate generous output buffer */
  size_t out_cap = (size_t)property->length * OODLE_COMPRESSION_FACTOR_MAGIC;
  SAFE_ALLOC_SIZE(uint8_t, result, out_cap);
  size_t result_sz = 0;

  uint8_t *src = (uint8_t *)arr->value;
  size_t   pos = 0;

  while (pos < arr->size) {
    UpkOodle upk;
    memset(&upk, 0, sizeof(upk));
    memcpy(&upk, src + pos, sizeof(upk));

    if (memcmp(&upk.signature, block_signature, sizeof(block_signature)) != 0) {
      printf_error("Bad block signature at pos %zu: got 0x%08X expected 0x%08X",
                   pos, bswap32((uint32_t)upk.signature),
                   bswap32(OODLE_COMPRESSED_BLOCK_SIGNATURE));
      exit(EXIT_FAILURE);
    }

    uint64_t comp_sz   = upk.blocks[0].compressed_size;
    uint64_t decomp_sz = upk.blocks[0].uncompressed_size;
    printf_verbose(verbose, "Block @ %zu: comp=%"PRIu64" decomp=%"PRIu64,
                   pos, comp_sz, decomp_sz);
    pos += sizeof(upk);

    SAFE_ALLOC_SIZE(uint8_t, in_buf,  (size_t)comp_sz);
    SAFE_ALLOC_SIZE(uint8_t, out_buf, (size_t)decomp_sz);
    memcpy(in_buf, src + pos, (size_t)comp_sz);
    pos += (size_t)comp_sz;

    int got = OodleLZ_Decompress(
        in_buf,   (size_t)comp_sz,
        out_buf,  (size_t)decomp_sz,
        OodleLZ_FuzzSafe_No, OodleLZ_CheckCRC_No, OodleLZ_Verbosity_None,
        NULL, 0, NULL, NULL, NULL, 0,
        OodleLZ_Decode_ThreadPhaseAll
    );
    if ((uint64_t)got != decomp_sz) {
      printf_error("Partial decompression: expected %"PRIu64" got %d", decomp_sz, got);
      exit(EXIT_FAILURE);
    }

    /* Grow result buffer if needed */
    if (result_sz + (size_t)decomp_sz > out_cap) {
      out_cap = (result_sz + (size_t)decomp_sz) * 2;
      uint8_t *tmp2 = (uint8_t *)realloc(result, out_cap);
      if (!tmp2) { printf_error("realloc failed"); exit(EXIT_FAILURE); }
      result = tmp2;
    }
    memcpy(result + result_sz, out_buf, (size_t)decomp_sz);
    result_sz += (size_t)decomp_sz;

    free(in_buf);
    free(out_buf);
  }

  /* Strip the UpkOodleSqliteSize header to get raw SQLite */
  UpkOodleSqliteSize upk_sz;
  uint8_t *rp = result;
  COPY_MEMORY(rp, &upk_sz, sizeof(upk_sz));
  printf_verbose(verbose, "UPK container_size=%u sqlite_size=%u",
                 upk_sz.container_size, upk_sz.sqlite_size);

  /* Validate SQLite header */
  SqliteHeader sqlite_hdr;
  memcpy(&sqlite_hdr, rp, sizeof(sqlite_hdr));
  uint16_t page_size = bswap16(sqlite_hdr.page_size);
  uint32_t db_pages  = bswap32(sqlite_hdr.database_size);
  uint32_t sqlite_sz = page_size * db_pages;
  printf_verbose(verbose, " SQLite page_size=%u db_pages=%u calc_size=%u",
                 page_size, db_pages, sqlite_sz);

  if (sqlite_sz != upk_sz.sqlite_size) {
    printf_error("SQLite size mismatch: header says %u, calculated %u",
                 upk_sz.sqlite_size, sqlite_sz);
    exit(EXIT_FAILURE);
  }

  SAFE_ALLOC_SIZE(uint8_t, sqlite_data, sqlite_sz);
  memcpy(sqlite_data, result + sizeof(upk_sz), sqlite_sz);

  free(result);
  free(arr->value);
  arr->value       = sqlite_data;
  arr->size        = sqlite_sz;
  property->length = (uint64_t)sqlite_sz + UARRAYPROPERTY_ADDED_LENGTH;

  ReleaseOodleLibrary();
}
