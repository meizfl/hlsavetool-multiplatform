#include "oodle.h"

/* Needle bytes for "RawDatabaseImage" FString */
static const uint8_t needle[] = {
  0x11, 0x00, 0x00, 0x00,                          /* length = 17 */
  0x52, 0x61, 0x77, 0x44, 0x61, 0x74, 0x61, 0x62, /* RawDatab */
  0x61, 0x73, 0x65, 0x49, 0x6D, 0x61, 0x67, 0x65, /* aseImage */
  0x00                                             /* NUL      */
};
static const size_t needle_len = sizeof(needle);

/* ── Portable memmem (needed on Windows and macOS) ───────────────────── */
#if defined(_WIN32) || defined(__APPLE__)
void *memmem(const void *haystack, size_t hlen,
             const void *needle_,  size_t nlen)
{
  if (!haystack || !needle_ || hlen == 0 || nlen == 0 || hlen < nlen)
    return NULL;
  for (const char *p = haystack; hlen >= nlen; ++p, --hlen)
    if (memcmp(p, needle_, nlen) == 0)
      return (void *)p;
  return NULL;
}
#endif

/* ── Printf helpers ──────────────────────────────────────────────────── */
int printf_error(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fputs("\x1B[31m[Error] ", stdout);
  int r = vprintf(fmt, ap);
  fputs(".\x1B[0m\n", stdout);
  va_end(ap);
  return r;
}

int printf_verbose(bool enabled, const char *fmt, ...)
{
  if (!enabled) return 0;
  va_list ap;
  va_start(ap, fmt);
  fputs("\x1B[34m[Info] ", stdout);
  int r = vprintf(fmt, ap);
  fputs("\x1B[0m\n", stdout);
  va_end(ap);
  return r;
}

/* ── Usage ────────────────────────────────────────────────────────────── */
static void usage(const char *argv0)
{
#if defined(_WIN32)
  const char *sep = "\\";
#else
  const char *sep = "/";
#endif
  const char *base = strrchr(argv0, sep[0]);
  base = base ? base + 1 : argv0;
  printf(
    "Usage: %s [OPTION] input output [%s]\n"
    " [OPTION]\n"
    "  -d  decompress: convert new format → old (SQLite)\n"
    "  -c  compress:   convert old (SQLite) → new format\n"
    " [%s]\n"
    "  -v  print additional info (optional)\n",
    base, VERBOSITY_FLAG, VERBOSITY_FLAG
  );
}

/* ── main ─────────────────────────────────────────────────────────────── */
int main(int argc, const char *argv[])
{
  printf(
    "Hogwarts Legacy save file tool – decompress/compress RawDatabaseImage SQLite.\n"
    "Open-source tool by @katt and @ifonlythatweretrue\n"
    "Multiplatform port. Report issues at "
    "https://github.com/topche-katt/hlsavetool/issues\n\n"
  );

  if (argc < 4) { usage(argv[0]); return EXIT_FAILURE; }

  const char *command = argv[1];
  bool do_decompress = (strcmp(command, COMMAND_DECOMPRESS) == 0);
  bool do_compress   = (strcmp(command, COMMAND_COMPRESS)   == 0);
  if (!do_decompress && !do_compress) {
    printf_error("Unknown command \"%s\"", command);
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  const char *input_filename  = argv[2];
  const char *output_filename = argv[3];
  bool verbose = (argc >= 5 && argv[4] && strcmp(argv[4], VERBOSITY_FLAG) == 0);

  /* ── Open files ───────────────────────────────────────────────────── */
  FILE *fpin  = NULL;
  FILE *fpout = NULL;
  OPEN_FILE(input_filename,  "rb", fpin);
  OPEN_FILE(output_filename, "wb", fpout);

  printf("Trying to %s save file \"%s\"\n",
         do_compress ? "compress" : "decompress", input_filename);

  printf_verbose(verbose, "Input  file: %s", input_filename);
  printf_verbose(verbose, "Output file: %s", output_filename);

  /* ── Read whole input into memory ─────────────────────────────────── */
  fseek(fpin, 0, SEEK_END);
  size_t file_size = (size_t)ftell(fpin);
  fseek(fpin, 0, SEEK_SET);
  printf_verbose(verbose, "Input file size: %zu bytes", file_size);

  SAFE_ALLOC_SIZE(uint8_t, buffer, file_size);
  if (fread(buffer, 1, file_size, fpin) != file_size) {
    printf_error("Failed to read input file");
    return EXIT_FAILURE;
  }
  fclose(fpin);

  /* ── Validate GVAS header ─────────────────────────────────────────── */
  GvasHeader header;
  memcpy(&header, buffer, sizeof(header));

  if (header.signature != GVAS_HEADER_SIGNATURE) {
    printf_error("Invalid GVAS signature: expected 0x%08X got 0x%08X",
                 bswap32(GVAS_HEADER_SIGNATURE), bswap32(header.signature));
    return EXIT_FAILURE;
  }
  if (header.version != GVAS_HEADER_VERSION) {
    printf_error("Invalid GVAS version: expected %u got %u",
                 GVAS_HEADER_VERSION, header.version);
    return EXIT_FAILURE;
  }

  printf_verbose(verbose, "GVAS header:");
  printf_verbose(verbose, "  Signature: 0x%08X", bswap32(header.signature));
  printf_verbose(verbose, "  Version:   %u",     header.version);
  printf_verbose(verbose, "  Package:   %u",     header.package);
  printf_verbose(verbose, "  Engine:    %u.%u.%u (%u)",
                 header.engine.major, header.engine.minor,
                 header.engine.patch, header.engine.changelist);

  /* ── Locate RawDatabaseImage property ────────────────────────────── */
  uint8_t *address = NULL;
  {
    uint8_t *mem   = buffer;
    size_t   memsz = file_size;
    SEARCH_MEMORY(address, mem, memsz, needle, needle_len);
  }
  if (!address) {
    printf_error("Could not locate \"RawDatabaseImage\" UProperty");
    return EXIT_FAILURE;
  }

  printf_verbose(verbose, "Buffer base:          %p", (void *)buffer);
  printf_verbose(verbose, "RawDatabaseImage addr:%p", (void *)address);

  /* head = everything before RawDatabaseImage */
  MemoryAddress head = { buffer, (size_t)(address - buffer) };
  printf_verbose(verbose, "Head size: %zu bytes", head.size);

  /* ── Parse the UProperty ──────────────────────────────────────────── */
  SAFE_ALLOC(UProperty, property);

  READ_FSTRING(property->name, address);
  PRINT_FSTRING(property->name);
  if (memcmp(property->name.data, RDI_UPROPERTY_NAME, RDI_UPROPERTY_NAME_LEN) != 0) {
    printf_error("Expected UProperty name \"%s\", got \"%s\"",
                 RDI_UPROPERTY_NAME, (char *)property->name.data);
    return EXIT_FAILURE;
  }

  READ_FSTRING(property->type, address);
  PRINT_FSTRING(property->type);
  if (memcmp(property->type.data, RDI_UPROPERTY_TYPE, RDI_UPROPERTY_TYPE_LEN) != 0) {
    printf_error("Expected UProperty type \"%s\", got \"%s\"",
                 RDI_UPROPERTY_TYPE, (char *)property->type.data);
    return EXIT_FAILURE;
  }

  COPY_MEMORY(address, &property->length, sizeof(property->length));
  printf_verbose(verbose, "ArrayProperty length: %"PRIu64" bytes", property->length);

  SAFE_ALLOC(UArrayProperty, value);

  READ_FSTRING(value->type, address);
  PRINT_FSTRING(value->type);
  if (memcmp(value->type.data, RDI_UPROPERTY_VALUE_TYPE, RDI_UPROPERTY_VALUE_TYPE_LEN) != 0) {
    printf_error("Expected UProperty value type \"%s\", got \"%s\"",
                 RDI_UPROPERTY_VALUE_TYPE, (char *)value->type.data);
    return EXIT_FAILURE;
  }

  COPY_MEMORY(address, &value->unknown, sizeof(value->unknown));
  COPY_MEMORY(address, &value->size,    sizeof(value->size));
  printf_verbose(verbose, "ByteProperty size: %u bytes", value->size);

  size_t data_sz = (size_t)value->size;
  SAFE_ALLOC_SIZE(uint8_t, data, data_sz);
  COPY_MEMORY(address, data, data_sz);
  value->value   = data;
  property->data = value;

  /* tail = everything after the property */
  MemoryAddress tail = {
    buffer + head.size + property->length + RDI_UPROPERTY_DATA_OFFSET,
    file_size - head.size - (size_t)property->length - RDI_UPROPERTY_DATA_OFFSET
  };
  printf_verbose(verbose, "Tail address: %p, size: %zu bytes",
                 (void *)tail.address, tail.size);

  /* ── Compress / decompress ────────────────────────────────────────── */
  if (do_decompress) decompress(property, verbose);
  if (do_compress)   compress  (property, verbose);

  /* ── Write output ─────────────────────────────────────────────────── */
  printf_verbose(verbose, "Writing output file: %s", output_filename);
  WRITE_FILE(fpout, head.address, head.size, 1);
  SERIALIZE_ARRAY_PROPERTY(property, fpout);
  WRITE_FILE(fpout, tail.address, tail.size, 1);
  fclose(fpout);
  printf_verbose(verbose, "Done writing: %s", output_filename);

  /* ── Cleanup ──────────────────────────────────────────────────────── */
  free(buffer);
  free(value->value);
  free(value);
  free(property);

  printf("Successfully %s to \"%s\"\n",
         do_compress ? "compressed" : "decompressed", output_filename);
  return EXIT_SUCCESS;
}
