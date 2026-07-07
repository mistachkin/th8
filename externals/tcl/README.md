# Tcl 8.6 build-time headers for TH8 (Win32 only)

This directory holds the **Win32-only** Tcl 8.6 headers and stub
library needed by `Makefile.msc` to build the `tclsh`-driven audit
and codegen scripts (the same scripts that are invoked from
`make audit`, `tools/mkreq.tcl`, `tools/format_code.tcl`, etc.).

On POSIX (macOS, Linux), the system Tcl installation supplies these
via `pkg-config tcl-8.6` and this directory is not used.

The `include/` and `lib/` subdirectories are excluded from source
control via `.gitignore`.  They are populated by the bootstrap
script.

## Directory layout (after populating)

    externals/tcl/
    +-- README.md           (this file; checked in)
    +-- include/            (Tcl 8.6 public headers; gitignored)
    +-- lib/x64/            (tclstub86.lib for x64; gitignored)
    +-- lib/x86/            (tclstub86.lib for x86; gitignored)

## How to populate

Use `tools/bootstrap_win32_deps.sh`, or manually:

  1. Download the Tcl 8.6.x source release from
     https://www.tcl-lang.org/software/tcltk/download.html
  2. Build per `win/README` in the Tcl source.
  3. Copy the public headers from the Tcl source's `generic/` and
     `win/` directories into `include/`.
  4. Copy the resulting `tclstub86.lib` into `lib/x64/` (or
     `lib/x86/`).

The pinned upstream version lives in
`tools/bootstrap_win32_deps.sh`.

## License

Tcl is distributed under a BSD-style license (the "Tcl/Tk License,"
identical in substance to TH8's `license.terms`).  Built artefacts
placed under `lib/` and headers placed under `include/` are subject
to that license.
