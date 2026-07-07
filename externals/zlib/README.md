# zlib for TH8 (Win32 only)

This directory holds the **Win32-only** zlib drop linked by the
TH8 `th8_curl.c` platform module (libcurl depends on zlib for
content-encoding).  On POSIX (macOS, Linux), the system zlib is
used.

The `include/` and `lib/` subdirectories are excluded from source
control via `.gitignore`.  They are populated by the bootstrap
script.

## Directory layout (after populating)

    externals/zlib/
    +-- README.md         (this file; checked in)
    +-- include/          (zlib.h, zconf.h; gitignored)
    +-- lib/x64/          (zlib.lib; gitignored)
    +-- lib/x86/          (zlib.lib; gitignored)
    +-- src/              (upstream source extracted; gitignored)

## How to populate

Use `tools/bootstrap_win32_deps.sh`, or manually:

  1. Download the zlib source release from https://zlib.net/
     (the bootstrap script pins zlib 1.3.2).  Older releases live
     under https://zlib.net/fossils/.
  2. Build the static library with `nmake -f win32/Makefile.msc`
     (or via the Visual Studio Developer Prompt).
  3. Copy `zlib.h` and `zconf.h` to `include/`.
  4. Copy the resulting `zlib.lib` (or rename `zlibstat.lib`) to
     `lib/x64/`.

The pinned upstream version lives in
`tools/bootstrap_win32_deps.sh`.

## License

zlib is distributed under the zlib license (a permissive
BSD-style license).  Built artefacts placed under `lib/` and
headers placed under `include/` are subject to that license.  The
acknowledgement requirement is minimal but it does exist; see
`zlib.h` in the upstream source.
