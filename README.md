# hlsavetool – multiplatform

Decompress / compress the SQLite database embedded in a Hogwarts Legacy GVAS save file.

Multiplatform rewrite of [topche-katt/hlsavetool](https://github.com/topche-katt/hlsavetool).  
All `windows.h` / WinAPI / MSVC-only code has been replaced with portable C11.

---

## Supported platforms

| Platform | Library file         |
|----------|----------------------|
| Windows  | `oo2core_9_win64.dll`|
| Linux    | `liboo2core.so`      |
| macOS    | `liboo2core.dylib`   |

---

## Getting the Oodle library

The Oodle runtime cannot be redistributed, but pre-extracted native builds are available here:

**<https://github.com/new-world-tools/go-oodle/releases/tag/v0.2.3-files>**

Download the file matching your platform and place it in the **same directory** as the
`hlsaves` executable (or anywhere in your `PATH` / `LD_LIBRARY_PATH`).

---

## Building

### Linux / macOS

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# binary: build/hlsaves
```

### Windows (MSVC)

```cmd
cmake -B build
cmake --build build --config Release
# binary: build\Release\hlsaves.exe
```

### Windows (MinGW)

```sh
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Usage

```
hlsaves [OPTION] input output [-v]

  -d   decompress  →  extracts the raw SQLite database
  -c   compress    →  re-packs a SQLite database into the save format
  -v   verbose output (optional)
```

### Examples

```sh
# Extract SQLite from a save file
./hlsaves -d MySave.sav MySave.db

# Re-pack an edited SQLite database back into the save
./hlsaves -c MySave.db MySave_patched.sav

# Same with verbose output
./hlsaves -d MySave.sav MySave.db -v
```

---

## What changed from the original

| Original (Windows-only)            | This port                                  |
|------------------------------------|--------------------------------------------|
| `#include <windows.h>`             | Removed entirely                           |
| `LoadLibraryEx` / `FreeLibrary`    | `dlopen` / `dlclose` (POSIX); `LoadLibraryExA` only on Windows via `#ifdef` |
| `GetProcAddress`                   | `dlsym` / `GetProcAddress` via macro       |
| `HMODULE`                          | `void *` (POSIX) / `HMODULE` (Windows)     |
| `WINAPI` calling convention        | `__stdcall` only on Windows via `CALLING_CONV` macro |
| `_byteswap_ulong` / `_byteswap_ushort` | `__builtin_bswap32/16` (GCC/Clang) / `_byteswap_*` (MSVC) |
| `memmem` (not in MSVC CRT)         | Custom implementation compiled on Windows and macOS |
| `.exe` hard-coded in usage text    | Platform-neutral basename detection        |

---

## License

MIT – same as the original project.
