#pragma once

/* ── Useful wrapper for memmem() calls ───────────────────────────────── */
typedef struct {
  uint8_t *address;
  size_t   size;
} MemoryAddress;

/* ── GVAS file header ────────────────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
  uint32_t signature;
  uint32_t version;
  uint32_t package;
  struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint32_t changelist;
  } engine;
} GvasHeader;

/*
 * Pascal-style strings:
 *   positive length → ANSI
 *   negative length → UCS2
 *   0               → NULL
 *   1               → empty FString
 */
typedef struct {
  uint32_t  length;
  void     *data;
} FString;

typedef struct {
  FString  name;
  FString  type;
  uint64_t length;
  void    *data;
} UProperty;

typedef struct {
  FString  type;
  uint8_t  unknown;
  uint32_t size;
  void    *value;
} UArrayProperty;
#pragma pack(pop)

/* ── Constants ───────────────────────────────────────────────────────── */
#define GVAS_HEADER_SIGNATURE         0x53415647u
#define GVAS_HEADER_VERSION           2

#define COMMAND_DECOMPRESS            "-d"
#define COMMAND_COMPRESS              "-c"
#define VERBOSITY_FLAG                "-v"

#define APPLICATION_IMAGE_NAME        "hlsaves"

#define RDI_UPROPERTY_NAME            "RawDatabaseImage"
#define RDI_UPROPERTY_NAME_LEN        17
#define RDI_UPROPERTY_TYPE            "ArrayProperty"
#define RDI_UPROPERTY_TYPE_LEN        14
#define RDI_UPROPERTY_VALUE_TYPE      "ByteProperty"
#define RDI_UPROPERTY_VALUE_TYPE_LEN  13

#define RDI_UPROPERTY_DATA_OFFSET     65
#define UARRAYPROPERTY_ADDED_LENGTH   4
#define OODLE_COMPRESSION_FACTOR_MAGIC 15
#define OODLE_MAX_BLOCK_SIZE          131072u
#define OODLE_COMPRESSED_BLOCK_SIGNATURE 0x9E2A83C1u

#define SQLITE_HEADER_SIGNATURE       "SQLite format 3"
#define SQLITE_HEADER_SIGNATURE_LEN   16
#define SQLITE_UPK_HEADER_ADDED_LENGTH 4

/* ── Helper macros ───────────────────────────────────────────────────── */

/** Load a function pointer from a shared library or abort.
 *  Uses memcpy to sidestep the ISO C prohibition on casting object→function ptr.
 */
#define GET_PROC(handle, fp, type, name) do { \
  void *_sym = lib_sym(handle, name);         \
  if (!_sym) {                                \
    printf_error("Failed to obtain procedure address: %s", name); \
    exit(EXIT_FAILURE);                       \
  }                                           \
  memcpy(&(fp), &_sym, sizeof(_sym));         \
} while (0)

/** Allocate zeroed memory or abort. */
#define SAFE_ALLOC(type, ptr)           SAFE_ALLOC_SIZE(type, ptr, sizeof(type))
#define SAFE_ALLOC_SIZE(type, ptr, sz) \
  type *ptr = NULL; \
  do { \
    ptr = (type *)malloc(sz); \
    if (ptr == NULL) { \
      printf_error("malloc(%zu) failed: %s", (size_t)(sz), strerror(errno)); \
      exit(EXIT_FAILURE); \
    } \
    memset(ptr, 0, sz); \
  } while (0)

/** Open a file or abort. */
#define OPEN_FILE(filename, mode, fp) do { \
  (fp) = fopen(filename, mode);           \
  if ((fp) == NULL) {                     \
    printf_error("fopen(\"%s\", \"%s\") failed: %s", filename, mode, strerror(errno)); \
    exit(EXIT_FAILURE);                   \
  }                                       \
} while (0)

/** fwrite a chunk or return EXIT_FAILURE. */
#define WRITE_FILE(fp, data, size, count) do { \
  if (fwrite(data, size, count, fp) != (size_t)(count) || ferror(fp)) { \
    printf_error("fwrite failed");             \
    return EXIT_FAILURE;                       \
  }                                            \
} while (0)

/** Find last occurrence of needle in memory region. */
#define SEARCH_MEMORY(result_ptr, mem_start, mem_sz, needle_data, needle_len) do { \
  uint8_t *_s = (uint8_t *)(mem_start);                                            \
  size_t   _n = (mem_sz);                                                           \
  (result_ptr) = NULL;                                                              \
  while (1) {                                                                       \
    uint8_t *_found = memmem(_s, _n, needle_data, needle_len);                     \
    if (!_found) break;                                                             \
    (result_ptr) = _found;                                                          \
    _n -= (size_t)((_found + (needle_len)) - _s);                                  \
    _s  = _found + (needle_len);                                                    \
  }                                                                                 \
} while (0)

/** memcpy and advance source pointer. */
#define COPY_MEMORY(src, dst, sz) do { \
  memcpy(dst, src, sz);               \
  (src) += (sz);                      \
} while (0)

/** Read a Pascal FString from a byte buffer. */
#define READ_FSTRING(str, mem) do { \
  COPY_MEMORY(mem, &(str).length, sizeof((str).length)); \
  if ((str).length > 0) { \
    size_t _ds = sizeof(uint8_t) * (str).length; \
    SAFE_ALLOC_SIZE(uint8_t, _d, _ds); \
    COPY_MEMORY(mem, _d, _ds); \
    (str).data = _d; \
  } \
} while (0)

/** Print FString for debugging. */
#define PRINT_FSTRING(str) do { \
  if ((str).length > 0 && (str).data != NULL) { \
    printf_verbose(verbose, "FString len=%u val=\"%s\"", (str).length, (char *)(str).data); \
  } \
} while (0)

/** Serialize FString to file. */
#define SERIALIZE_FSTRING(str, fp) do { \
  fwrite(&(str).length, sizeof((str).length), 1, fp); \
  fwrite((str).data, (str).length, 1, fp); \
} while (0)

/** Serialize a full ArrayProperty to file. */
#define SERIALIZE_ARRAY_PROPERTY(prop, fp) do { \
  SERIALIZE_FSTRING((prop)->name, fp); \
  SERIALIZE_FSTRING((prop)->type, fp); \
  fwrite(&(prop)->length, sizeof((prop)->length), 1, fp); \
  UArrayProperty *_ap = (UArrayProperty *)(prop)->data; \
  SERIALIZE_FSTRING(_ap->type, fp); \
  fwrite(&_ap->unknown, sizeof(_ap->unknown), 1, fp); \
  fwrite(&_ap->size,    sizeof(_ap->size),    1, fp); \
  fwrite(_ap->value,    _ap->size,            1, fp); \
} while (0)

/* ── Printf wrappers (defined in hlsaves.c) ───────────────────────────── */
int printf_error(const char *fmt, ...);
int printf_verbose(bool enabled, const char *fmt, ...);
