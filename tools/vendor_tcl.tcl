#!/usr/bin/env tclsh
###############################################################################
#
# vendor_tcl.tcl --
#
#     Vendor customization tool for externals/mmm/tcl.c.
#
#     Copies the vendor source to src/th8_tcl_stubs.c and applies
#     TH8-specific patches:
#
#       1. Environment variable: MMM3_TCL_PATH -> TH8_TCL_PATH
#       2. MSVC-only APIs to portable equivalents:
#            _strdup  -> strdup  (on non-Windows)
#            _snprintf -> snprintf (on non-Windows)
#            _stat / struct _stat -> stat / struct stat (on non-Windows)
#       2b. _POSIX_C_SOURCE for strdup on Linux with -std=c99
#       3. Remove stale TH1/Fossil references from comments
#       4. Add TH8 header comment
#
# Usage:
#     tclsh tools/vendor_tcl.tcl bin/
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set vendor "externals/mmm/tcl.c"
set output [file join [lindex $argv 0] th8_tcl_stubs.c]

if {![file exists $vendor]} then {
  puts stderr "ERROR: vendor source not found: $vendor"
  exit 1
}

set fd [open $vendor r]
set data [read $fd]
close $fd

#
# 1. Environment variable name.
#
set data [string map {
  "MMM3_TCL_PATH" "TH8_TCL_PATH"
} $data]

#
# 2. MSVC-only API portability.
#
#    Add a portability block after the existing includes that maps
#    the MSVC names to POSIX names on non-Windows platforms.
#
set portBlock {
  /*
  ** Portability: map MSVC-specific names to POSIX equivalents.
  */
  #if !defined(_WIN32) && !defined(WIN32)
  #  ifndef _strdup
  #    define _strdup strdup
  #  endif
  #  ifndef _snprintf
  #    define _snprintf snprintf
  #  endif
  #  ifndef _stat
  #    define _stat stat
  #  endif
  #endif /* !_WIN32 && !WIN32 */
}

#
# Insert the portability block after the #include "tcl.h" line.
#
set marker "#include \"tcl.h\"       /* NOTE: For public Tcl API. */"
set idx [string first $marker $data]
if {$idx >= 0} then {
  set insertAt [expr {$idx + [string length $marker]}]
  set data [string range $data 0 [expr {$insertAt - 1}]]\n$portBlock[string range $data $insertAt end]
}

#
# Insert _POSIX_C_SOURCE before the first #include so that strdup
# is declared by <string.h> on Linux with -std=c99.
#
set firstInclude "#include <stdio.h>"
set idx [string first $firstInclude $data]
if {$idx >= 0} then {
  set posixGuard "#if !defined(_WIN32) && !defined(WIN32)\n"
  append posixGuard "#  ifndef _POSIX_C_SOURCE\n"
  append posixGuard "#    define _POSIX_C_SOURCE 200809L\n"
  append posixGuard "#  endif\n"
  append posixGuard "#endif\n"
  set data [string range $data 0 [expr {$idx - 1}]]$posixGuard[string range $data $idx end]
}

#
# 3. Remove stale TH1/Fossil references from comments.
#
set data [string map {
  "tclInvoke TH1 command" "tclEval command"
} $data]

#
# 4. Add a TH8-specific file header.
#
set header {/*
  ** th8_tcl_stubs.c -- Reverse Tcl stubs loader for TH8.
  **
  ** This file is auto-generated from externals/mmm/tcl.c by
  ** tools/vendor_tcl.tcl.  DO NOT EDIT DIRECTLY.
  **
  ** Provides the "reverse stubs" technique: dynamically loads the Tcl
  ** shared library at runtime, bootstraps the Tcl stubs table, and
  ** exposes functions to create/evaluate/destroy a Tcl interpreter
  ** without linking against libtcl at build time.
  **
  ** See the file "license.terms" for information on usage and redistribution of
  ** this file, and for a DISCLAIMER OF ALL WARRANTIES.
  **
  ** Original copyright and license below.
  */
}
set data "$header$data"

#
# Write the output.
#
file mkdir [file dirname $output]
set fd [open $output w]
fconfigure $fd -translation binary
puts -nonewline $fd $data
close $fd

puts "Vendored: $vendor -> $output"
puts "  Applied TH8 customizations."
