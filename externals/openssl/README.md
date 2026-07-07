# OpenSSL for TH8 (Win32 only)

This directory is the **Win32-only** OpenSSL drop used by
`Makefile.msc` and the Harpy crypto plugins.  On POSIX (macOS,
Linux) the system / Homebrew / package-manager OpenSSL is used and
this directory is not needed.

The `include/` and `lib/` subdirectories are excluded from source
control via `.gitignore`.  They are populated by running the build
script described below.

## Directory layout (after populating)

    externals/openssl/
    +-- README.md         (this file; checked in)
    +-- include/openssl/  (public headers; gitignored)
    +-- lib/x64/          (static .lib files; gitignored)
    +-- lib/x86/          (static .lib files; gitignored)
    +-- src/              (upstream source clone; gitignored)

## How to populate

Use `tools/bootstrap_win32_deps.sh` (or the documented flow inside
that script) to:

  1. Clone the pinned OpenSSL release into `src/`.
  2. Build it with `nmake -f makefile` (or VS Developer Prompt).
  3. Copy `out32dll/*.lib` to `lib/x64/` (or `lib/x86/` for 32-bit).
  4. Copy `inc32/openssl/*.h` to `include/openssl/`.

`Makefile.msc` then picks them up automatically.

The pinned upstream version, the build flags, and the verification
checksum live in `tools/bootstrap_win32_deps.sh`.

## License

OpenSSL is distributed under the Apache License 2.0 (3.0.x and
later).  Built artifacts placed under `lib/` and headers placed
under `include/` are subject to that license, not TH8's.  If you
redistribute a TH8 build with OpenSSL statically linked, comply
with OpenSSL's license terms (including its NOTICE file
requirements).
