# TH8 Portability Guide

Compiler, OS, and library prerequisites for building TH8.

On a fresh **Debian/Ubuntu** machine the POSIX `Makefile` installs every
system package needed for a standard build in one step:

    make apt-deps            # shared/dynamic build
    make apt-deps-static     # the above, plus the extras a fully-static link needs

Both run `apt-get` via `sudo` (pass `SUDO=` to run as root directly, e.g.
inside a container).  The vendored libraries below marked "Compiled into
TH8 (no external library)" need no packages — the `make vendoring` target
regenerates them from `externals/*/vendor/`.

The build's `clang-format` style audit (`make audit-format`, part of `make
audit`) is pinned to one canonical clang-format major version
(`CLANG_FORMAT_VERSION` in the Makefile, currently **19**), because
clang-format's output is not stable across major versions.  `make apt-deps`
installs `clang-format-19`; the audit **warns and skips** under any other
version (or none), so a build is never blocked by the clang-format a host
happens to ship — only the canonical version enforces.

The gate resolves the binary by the exact name **`clang-format-19`** (via
`command -v clang-format-19`, falling back to an unversioned `clang-format`),
and enforces only when that binary's major version is 19.  Formatting is stable
within a major version, so any `19.x` patch release is fine; only the major (19)
must match.

### Installing `clang-format-19` on macOS (or any host without an apt package)

macOS has no `apt`, so install the pinned version into an isolated virtualenv
and expose it under the name the gate looks for:

    python3 -m venv ~/.clang-format-19-venv
    ~/.clang-format-19-venv/bin/pip install 'clang-format==19.1.7'
    ln -sf ~/.clang-format-19-venv/bin/clang-format \
        "$(brew --prefix)/bin/clang-format-19"   # or any dir already on $PATH

Verify with `clang-format-19 --version` (expect `19.x`); `make audit-format`
should then **enforce** (`format_code: summary: ok=... changed=0`) instead of
printing a `SKIP` line.  To remove it later: `rm "$(brew --prefix)/bin/clang-format-19"`
and `rm -rf ~/.clang-format-19-venv`.

The Homebrew LLVM 19 toolchain is a heavier (~1.5 GB) official alternative:

    brew install llvm@19
    ln -sf "$(brew --prefix llvm@19)/bin/clang-format" \
        "$(brew --prefix)/bin/clang-format-19"

---

## 1. C Language Standard

**Required: C99** (`-std=c99`)

TH8 uses the following C99 features:
- `long long` / `unsigned long long` for 64-bit integers
- `//` comments (though most code uses `/* */`)
- Mixed declarations and statements (avoided by convention;
  `-Wdeclaration-after-statement` is enabled)
- `<stdbool.h>` (used by libtommath; not used directly by TH8 core)
- `<stdint.h>` types (used by libtommath and ConvertUTF_v2)

TH8 does NOT require C11 or later.

### Compiler-Specific Extensions Used

| Feature | GCC/Clang | MSVC | Fallback |
|---------|-----------|------|----------|
| TLS | `__thread` | `__declspec(thread)` | None (mutex required) |
| Visibility | `__attribute__((visibility("default")))` | `__declspec(dllexport/dllimport)` | Empty |
| 64-bit int | `long long` | `__int64` | `long long` |
| Atomic CAS | `__sync_val_compare_and_swap` | `InterlockedCompareExchange` | N/A |

---

## 2. Supported Compilers

| Compiler | Minimum Version | Notes |
|----------|----------------|-------|
| GCC | 4.9+ | Full C99, `__attribute__((visibility))`; routinely tested on GCC 11-14 |
| Clang | 3.5+ | Full C99 + attribute support; routinely tested on Clang 14-19 |
| Apple Clang | Xcode 10+ | macOS 10.14+ (Apple LLVM 10); routinely tested on Apple LLVM 17 |
| MSVC | VS 2015 (v14) + | Requires C99-compatible `<inttypes.h>` and `_Static_assert`; MSVC 2013 is not supported |

### Recommended Compiler Flags

```
-std=c99 -pedantic -Wall -Wextra
-Wdeclaration-after-statement -Wstrict-prototypes
-Wmissing-prototypes -Wold-style-definition
-Wno-long-long -Wno-unused-parameter
-ffp-contract=off
```

---

## 3. POSIX Platform Requirements

### Minimum POSIX Version

**POSIX.1-2008** (`_POSIX_C_SOURCE 200809L`)

The following POSIX.1-2008 features are used:

| Function | Header | Purpose |
|----------|--------|---------|
| `open` (O_CLOEXEC) | `<fcntl.h>` | Close-on-exec file open |
| `open` (O_NOFOLLOW) | `<fcntl.h>` | Reject symlinks |
| `read`, `close` | `<unistd.h>` | File I/O |
| `fstat`, `S_ISREG` | `<sys/stat.h>` | File metadata |
| `realpath` | `<stdlib.h>` | Canonical path resolution |
| `getpid` | `<unistd.h>` | Process identification |
| `getuid`, `getpwuid` | `<pwd.h>` | User name resolution |
| `gettimeofday` | `<sys/time.h>` | Wall clock time |
| `dlopen`, `dlsym`, `dlclose` | `<dlfcn.h>` | Dynamic library loading |
| `pthread_mutex_*` | `<pthread.h>` | Mutex for global state |
| `pthread_self` | `<pthread.h>` | Thread identification |
| `/dev/urandom` | (device) | Cryptographic entropy |

### GNU/Linux-Specific Extensions

Enabled by `_GNU_SOURCE` (defined in `th8.h` on non-Apple,
non-Windows systems):

| Function | Purpose |
|----------|---------|
| `pthread_getattr_np` | Get thread attributes (for stack bounds) |
| `pthread_attr_getstack` | Get stack base and size |
| `dladdr`, `Dl_info` | Reverse symbol lookup (for base path) |
| `malloc_usable_size` | Actual allocation size tracking |

### Minimum Linux Version

- **Kernel**: 2.6.23+ (for `O_CLOEXEC`)
- **glibc**: 2.12+ (for `pthread_getattr_np`, `O_CLOEXEC`)
- **musl**: Any version (all POSIX extensions are always visible)

### Link Libraries (Linux)

```
-lm -ldl -lpthread
```

Plus optionally: `-lcurl` (with `TH8_ENABLE_LIBCURL`)

---

## 4. macOS Platform Requirements

### Minimum macOS Version

- **macOS**: 10.12+ (Sierra) for full API support
- **Xcode**: 8.0+

### macOS-Specific APIs

| Function | Header | Purpose |
|----------|--------|---------|
| `malloc_create_zone` | `<malloc/malloc.h>` | Private memory zone |
| `malloc_zone_calloc` | `<malloc/malloc.h>` | Zone-specific allocation |
| `malloc_zone_realloc` | `<malloc/malloc.h>` | Zone-specific reallocation |
| `malloc_zone_free` | `<malloc/malloc.h>` | Zone-specific deallocation |
| `malloc_size` | `<malloc/malloc.h>` | Allocation size query |
| `pthread_get_stackaddr_np` | `<pthread.h>` | Thread stack address |
| `pthread_get_stacksize_np` | `<pthread.h>` | Thread stack size |

### Feature Test Macros

**None defined.** macOS headers expose all APIs by default.
Defining `_POSIX_C_SOURCE` or `_XOPEN_SOURCE` would actually
restrict the visible API surface.

### Link Libraries (macOS)

```
-lm -lcurl
```

(pthreads are part of the system library on macOS.)

---

## 5. Windows Platform Requirements

### Minimum Windows Version

- **Windows**: Vista+ (Windows 6.0+, `_WIN32_WINNT >= 0x0600`)
- Required for `GetFinalPathNameByHandleA`; falls back to
  `GetFullPathNameA` on older versions.

### Win32 APIs Used

| Function | DLL | Purpose |
|----------|-----|---------|
| `CreateFileA` | kernel32 | File open |
| `ReadFile`, `WriteFile` | kernel32 | File/pipe I/O |
| `ReadConsoleW`, `WriteConsoleW` | kernel32 | Unicode console I/O |
| `GetFileAttributesA` | kernel32 | File existence check |
| `GetFinalPathNameByHandleA` | kernel32 | Canonical path (Vista+) |
| `GetCurrentProcessId` | kernel32 | Process identification |
| `GetSystemTimeAsFileTime` | kernel32 | Wall clock time |
| `VirtualQuery` | kernel32 | Stack bounds detection |
| `LoadLibraryA`, `GetProcAddress` | kernel32 | Dynamic loading |
| `ExitProcess` | kernel32 | Process termination |
| `RtlGenRandom` (SystemFunction036) | advapi32 | Cryptographic entropy |

### MSVC Build

Use `Makefile.msc` with `nmake`:
```
nmake /f Makefile.msc
```

---

## 6. C Standard Library Requirements

### Headers Required by Core

| Header | Used For |
|--------|----------|
| `<stddef.h>` | `size_t`, `NULL` |
| `<stdarg.h>` | `va_list` (platform callback) |
| `<assert.h>` | `assert()` (debug builds only) |
| `<string.h>` | `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`, `strchr` |
| `<stdlib.h>` | `malloc`, `calloc`, `realloc`, `free`, `atoi`, `qsort` |
| `<stdio.h>` | `fgets`, `fwrite`, `vsnprintf` |
| `<math.h>` | 21 transcendental functions (sin, cos, exp, log, pow, sqrt, etc.) |
| `<limits.h>` | `INT_MAX`, `LLONG_MAX` |
| `<stdbool.h>` | `bool`, `true`, `false` (used by libtommath) |

### Math Library Functions (from `<math.h>`)

All math functions are used via the platform's `xMathFunc`
callback. The `th8_libc.c` platform provides them via `-lm`:

**Core (C89/C99):**
```
sin cos tan asin acos atan atan2
sinh cosh tanh
exp log log10 pow sqrt
floor ceil fabs fmod hypot
```

**TIP #745 (C99):**
```
acosh asinh atanh cbrt copysign
erf erfc exp2 expm1 fdim
tgamma lgamma ldexp
log1p log2 logb
nextafter remainder trunc
```

**TIP #521 (C99 classification macros, wrapped in platform):**
```
isfinite isinf isnan isnormal fpclassify signbit
```

---

## 7. Optional External Dependencies

### libcurl (HTTP/HTTPS support)

- **Gate**: `TH8_ENABLE_LIBCURL` / `ENABLE_LIBCURL=1`
- **Minimum version**: 7.28.0+ (for `CURLOPT_ACCEPT_ENCODING`)
- **Link**: `curl-config --libs` (includes transitive deps:
  OpenSSL, zlib, etc.)
- **Used for**: `[source https://...]` URI loading

### libtommath (Arbitrary precision integers)

- **Gate**: `TH8_ENABLE_BIGINT` / `ENABLE_BIGINT=1`
- **Version**: Latest trunk (mp_*.c naming, C99 bool)
- **Link**: Compiled into TH8 (no external library)
- **Memory**: Routed through TH8 platform allocator via
  `MP_MALLOC`/`MP_FREE` bridge
- **Used for**: Integer overflow promotion in `[expr]`

### Spencer Regex Engine (Regular expressions)

- **Gate**: `TH8_ENABLE_REGEXP` / `ENABLE_REGEXP=1`
- **Origin**: PostgreSQL's vendored Spencer regex
- **Link**: Compiled into TH8 (no external library)
- **Memory**: Routed through TH8 platform allocator via
  `MALLOC`/`FREE` bridge
- **Used for**: `[regexp]` and `[regsub]` commands

### Spilornis (Eagle List Parser)

- **Always linked** (core list operations)
- **Origin**: Eagle project
- **Memory**: Routed through TH8 bridge functions via
  `th8_spilornis.h` macro overrides

### ConvertUTF_v2 (UTF encoding conversion)

- **Always linked** (core UTF-8/UTF-16/UTF-32 operations)
- **Origin**: Unicode, Inc. (modified for C89 compat)
- **Used for**: Win32 Unicode console I/O, regex engine

### bestline (Line editing)

- **Shell only** (not in library)
- **Origin**: jart/bestline
- **Requires**: POSIX terminal I/O (`tcgetattr`, `tcsetattr`)
- **Gate**: `TH8_USE_BESTLINE`

---

## 8. Amalgamation Build

The amalgamation (`bin/th8.c`) includes all TH8 core code plus
vendored dependencies in a single file.  Build with:

```
cc -DTH8_ENABLE_REGEXP -Isrc/ -Iexternals/tommath \
   -Isrc/regexp -Iexternals/regex/build \
   -Iexternals/spilornis -Iexternals/utf \
   bin/th8.c -lm -o libth8.a
```

The public header `th8.h` must be on the include path.  All
platform-specific code is gated by `TH8_PLATFORM_*` macros
(auto-detected from compiler defines).

---

## 9. Platform Macro Summary

| Macro | Auto-detected | Controls |
|-------|---------------|----------|
| `TH8_PLATFORM_POSIX` | `!_WIN32 && !WIN32` | th8_posix.c |
| `TH8_PLATFORM_WIN32` | `_WIN32 \|\| WIN32` | th8_win32.c |
| `TH8_PLATFORM_MACOS` | `__APPLE__ && POSIX` | th8_macos.c |
| `TH8_PLATFORM_LIBC` | Always (default on) | th8_libc.c |
| `TH8_PLATFORM_NULLIO` | Always (default on) | th8_nullio.c |
| `TH8_PLATFORM_CURL` | `TH8_ENABLE_LIBCURL` | th8_curl.c |
| `TH8_ENABLE_REGEXP` | Compile flag | th8_regex.c + Spencer |
| `TH8_ENABLE_BIGINT` | Compile flag | th8_bigint.c + libtommath |
| `TH8_ENABLE_LIBCURL` | Compile flag | th8_curl.c |
| (always compiled) | — | th8_cache.c (internal-rep cache) |
| (always compiled) | — | th8_glob.c (glob matching) |
| (always compiled) | — | th8_math.c (math functions) |

All can be overridden by predefining before including `th8.h`.
