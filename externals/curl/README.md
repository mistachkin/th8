# libcurl for TH8

Static libcurl build for the TH8 `th8_curl.c` platform module.

## Directory Layout

    externals/curl/
    ├── README.md               This file
    ├── build_win64.bat         Windows x64 build script
    ├── lib/x64/
    │   └── libcurl_a.lib       Built static library (after build)
    └── src/                    Curl source (cloned separately)
        ├── winbuild/
        ├── include/curl/
        └── ...

## Setup

Clone the curl source into the `src/` subdirectory:

    cd externals/curl
    git clone --depth 1 https://github.com/curl/curl.git src

## Building (Windows x64)

Requires **CMake** on PATH and a **x64 Native Tools Command Prompt
for VS**.

    externals\curl\build_win64.bat            Release build
    externals\curl\build_win64.bat debug      Debug build

The script uses CMake to build a static libcurl with:

  - **Schannel** for TLS (Windows built-in, no OpenSSL needed)
  - **IPv6** enabled
  - No zlib, brotli, zstd, SSH, HTTP/2, or other dependencies

The built library is automatically copied to `lib/x64/`.

## Integrating with TH8

Build TH8 with libcurl support:

    nmake /f Makefile.msc ENABLE_LIBCURL=1

This adds `th8_curl.obj` to the DLL, links against `libcurl_a.lib`
and the required system libraries (`ws2_32.lib`, `crypt32.lib`,
`normaliz.lib`).

## POSIX

On POSIX, libcurl is expected to be installed system-wide.  The
POSIX Makefile links with `-lcurl` when `ENABLE_LIBCURL=1` is set.
No manual build step is needed.
