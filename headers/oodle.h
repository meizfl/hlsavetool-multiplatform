#pragma once

#include "common.h"

/* ── UPK/Oodle on-disk structures ─────────────────────────────────────── */
#pragma pack(push, 1)

/*
 * Compressed block header layout:
 *   [sig 8B][max_block 8B][compressed_size 8B][uncompressed_size 8B]
 *                         [compressed_size 8B][uncompressed_size 8B]  ← repeated
 */
typedef struct {
  uint64_t compressed_size;
  uint64_t uncompressed_size;
} UpkBlock;

typedef struct {
  uint64_t signature;
  uint64_t max_block_size;
  UpkBlock blocks[2];          /* duplicated pair, as in the original */
} UpkOodle;

/*
 * Header prepended to the SQLite data inside the UPK container:
 *   [container_size 4B][sqlite_size 4B]
 */
typedef struct {
  uint32_t container_size;
  uint32_t sqlite_size;
} UpkOodleSqliteSize;

/* Minimal SQLite header – only fields we need */
typedef struct {
  char     magic[16];
  uint16_t page_size;
  uint8_t  unused[10];
  uint32_t database_size;
} SqliteHeader;

#pragma pack(pop)

/* ── Oodle enumerations (portable, no WINAPI) ─────────────────────────── */
typedef enum {
  OodleLZ_Compressor_Invalid = -1,
  OodleLZ_Compressor_Kraken  =  8
} OodleLZ_Compressor;

typedef enum {
  OodleLZ_CompressionLevel_Fast = 3
} OodleLZ_CompressionLevel;

typedef enum {
  OodleLZ_FuzzSafe_No  = 0,
  OodleLZ_FuzzSafe_Yes = 1
} OodleLZ_FuzzSafe;

typedef enum {
  OodleLZ_CheckCRC_No     = 0,
  OodleLZ_CheckCRC_Yes    = 1,
  OodleLZ_CheckCRC_Force32 = 0x40000000
} OodleLZ_CheckCRC;

typedef enum {
  OodleLZ_Verbosity_None = 0
} OodleLZ_Verbosity;

typedef enum {
  OodleLZ_Decode_ThreadPhaseAll = 3
} OodleLZ_Decode_ThreadPhase;

typedef enum {
  OodleLZ_Profile_Main = 0
} OodleLZ_Profile;

typedef enum {
  OodleLZ_Jobify_Default    = 0,
  OodleLZ_Jobify_Disable    = 1,
  OodleLZ_Jobify_Normal     = 2,
  OodleLZ_Jobify_Aggressive = 3,
  OodleLZ_Jobify_Count      = 4,
  OodleLZ_Jobify_Force32    = 0x40000000
} OodleLZ_Jobify;

typedef struct {
  uint32_t         unused_was_verbosity;
  int32_t          minMatchLen;
  int32_t          seekChunkReset;
  int32_t          seekChunkLen;
  OodleLZ_Profile  profile;
  int32_t          dictionarySize;
  int32_t          spaceSpeedTradeoffBytes;
  int32_t          unused_was_maxHuffmansPerChunk;
  int32_t          sendQuantumCRCs;
  int32_t          maxLocalDictionarySize;
  int32_t          makeLongRangeMatcher;
  int32_t          matchTableSizeLog2;
  OodleLZ_Jobify   jobify;
  void            *jobifyUserPtr;
  int32_t          farMatchMinLen;
  int32_t          farMatchOffsetLog2;
  uint32_t         reserved[4];
} OodleLZ_CompressOptions;

/* ── Function-pointer typedefs (CALLING_CONV resolves per platform) ────── */
typedef OodleLZ_CompressOptions *(CALLING_CONV OodleLZ_CompressOptions_GetDefault_FP)(
    OodleLZ_Compressor compressor, OodleLZ_CompressionLevel lzLevel);

typedef intptr_t (CALLING_CONV OodleLZ_GetCompressedBufferSizeNeeded_FP)(
    OodleLZ_Compressor compressor, uintptr_t rawSize);

typedef int (CALLING_CONV OodleLZ_Compress_FP)(
    OodleLZ_Compressor compressor,
    uint8_t *data_buf, size_t data_len,
    uint8_t *dst_buf,
    OodleLZ_CompressionLevel compression,
    OodleLZ_CompressOptions *cmps_opts,
    const void *dictionary, const void *lrmv,
    void *scratch, size_t scratch_size);

typedef int (CALLING_CONV OodleLZ_Decompress_FP)(
    uint8_t *src_buf, size_t src_len,
    uint8_t *dst_buf, size_t dst_size,
    int fuzz, int crc, int verbose,
    uint8_t *dst_base, size_t e,
    void *cb, void *cb_ctx,
    void *scratch, size_t scratch_size,
    int threadPhase);

/* ── Public API ───────────────────────────────────────────────────────── */
void compress(UProperty *property, bool verbose);
void decompress(UProperty *property, bool verbose);
