#pragma once

/* memmem requires _GNU_SOURCE on glibc */
#if defined(__linux__)
#  define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

/* ── Platform-specific shared library loading ─────────────────────────── */
#if defined(_WIN32)
  #include <windows.h>
  typedef HMODULE lib_handle_t;
  /* GetModuleFileNameA is in windows.h – no extra include needed */
  #define lib_open(name)        LoadLibraryExA(name, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)
  #define lib_sym(h, sym)       GetProcAddress(h, sym)
  #define lib_close(h)          FreeLibrary(h)
  #define lib_error()           "(LoadLibrary failed)"
  #define OODLE_LIB_NAME        "oo2core_9_win64.dll"
  #define CALLING_CONV          __stdcall
  /* MSVC byte-swap intrinsics */
  #include <stdlib.h>
  #define bswap32(x)            _byteswap_ulong(x)
  #define bswap16(x)            _byteswap_ushort(x)
#else
  #include <dlfcn.h>
  #include <unistd.h>
  typedef void *lib_handle_t;
  #define lib_open(name)        dlopen(name, RTLD_LAZY)
  #define lib_sym(h, sym)       dlsym(h, sym)
  #define lib_close(h)          dlclose(h)
  #define lib_error()           dlerror()
  #if defined(__APPLE__)
    #define OODLE_LIB_NAME      "liboo2core.dylib"
  #else
    #define OODLE_LIB_NAME      "liboo2core.so"
  #endif
  #define CALLING_CONV          /* empty – System V ABI */
  /* GCC/Clang built-in byte swap */
  #define bswap32(x)            __builtin_bswap32(x)
  #define bswap16(x)            __builtin_bswap16(x)
#endif

/* ── Portable memmem (missing on Windows / some non-GNU libc) ─────────── */
#if defined(_WIN32) || defined(__APPLE__)
void *memmem(const void *haystack, size_t haystack_len,
             const void *needle,   size_t needle_len);
#endif

/* ── Debug print helper ───────────────────────────────────────────────── */
#ifdef NDEBUG
  #define dprintf(...)  ((void)0)
#else
  #define dprintf(...)  printf(__VA_ARGS__)
#endif

#include "defines.h"
