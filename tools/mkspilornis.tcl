#!/usr/bin/env tclsh
###############################################################################
#
# mkspilornis.tcl --
#
#     Rename Windows-conflicting type names in Spilornis source files
#     to use a "se_" prefix (short for Spilornis/Eagle).
#
#     The Spilornis library (Eagle's Tcl list parser) defines types
#     like WCHAR, DWORD, BOOL, etc. with different base types than
#     the Windows SDK (e.g., WCHAR = unsigned char for UTF-8 mode,
#     not wchar_t).  These collide when compiled in the same
#     translation unit as Windows code (amalgamation build).
#
#     This tool performs a source-level rename of all conflicting
#     symbols to se_WCHAR, se_DWORD, se_BOOL, etc.  The transformed
#     files compile cleanly alongside <windows.h>.
#
# Usage:
#     tclsh tools/mkspilornis.tcl INPUT ?OUTPUT?
#
#     If OUTPUT is omitted, the output is written to bin/<basename>.
#
# Examples:
#     tclsh tools/mkspilornis.tcl externals/spilornis/Spilornis.c
#     tclsh tools/mkspilornis.tcl externals/spilornis/Spilornis.h bin/Spilornis.h
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

if {[llength $argv] < 1} then {
  puts stderr "Usage: tclsh tools/mkspilornis.tcl INPUT ?OUTPUT?"
  exit 1
}

set input [lindex $argv 0]
set output [lindex $argv 1]

if {$output eq ""} then {
  set output [file join bin [file tail $input]]
}

if {![file exists $input]} then {
  puts stderr "Input file not found: $input"
  exit 1
}

file mkdir [file dirname $output]

#
# Types that conflict with <windows.h>.
# Each is renamed from NAME to se_NAME.
#
set typeNames {
  WCHAR LPWSTR LPCWSTR
  CHAR LPSTR LPCSTR
  BYTE LPBYTE
  LPVOID LPCVOID
  USHORT LPUSHORT
  UCSCHAR LPUCSCHAR
  DWORD
  BOOL LPBOOL
  FLAGS RETURNCODE
  SIZE_T LPSIZE_T LPCSIZE_T
  INT UINT
  HANDLE
}

#
# CRT function macros that conflict with <windows.h> / CRT headers.
# Each is renamed from NAME to se_NAME.
#
set crtNames {
  iswspace iswdigit iswxdigit iswbdigit iswodigit
  calloc free memcpy memset memcmp
  strlen strncmp strncpy snprintf vsnprintf
  wcslen wcsncmp wmemcpy wmemset
  swprintf vswprintf wcsncpy
}

#
# Type guard macros (e.g., _WCHAR_DEFINED).
# These are renamed to prevent blocking <windows.h> typedefs.
#
set guardNames {
  _WCHAR_DEFINED _LPWSTR_DEFINED _LPCWSTR_DEFINED
  _CHAR_DEFINED _LPSTR_DEFINED _LPCSTR_DEFINED
  _BYTE_DEFINED _LPBYTE_DEFINED
  _LPVOID_DEFINED _LPCVOID_DEFINED
  _USHORT_DEFINED _LPUSHORT_DEFINED
  _UCSCHAR_DEFINED _LPUCSCHAR_DEFINED
  _DWORD_DEFINED
  _BOOL_DEFINED _LPBOOL_DEFINED
  _INT_DEFINED _UINT_DEFINED
  _BSTR_DEFINED
  _FLAGS_DEFINED _RETURNCODE_DEFINED
  _CONST_DEFINED _VOID_DEFINED
  __SIZE_T_DEFINED _LPSIZE_T_DEFINED _LPCSIZE_T_DEFINED
}

#
# Read the input file.
#
set fd [open $input r]
set data [read $fd]
close $fd

#
# Apply renames.  We use word-boundary matching to avoid
# corrupting substrings (e.g., LIBRARY_FREED_MEMORY contains
# no conflict, but DWORD inside a comment or typedef does).
#
# Process type names first (longer names before shorter to
# avoid partial matches, e.g., LPCWSTR before WCHAR).
#

#
# Sort by length descending so longer names are matched first.
#
proc byLengthDesc {a b} {
  return [expr {[string length $b] - [string length $a]}]
}

set allNames [concat $typeNames $crtNames $guardNames]
set allNames [lsort -command byLengthDesc $allNames]

foreach name $allNames {
  #
  # Match whole words only.  The pattern matches NAME when it
  # is preceded by a non-alphanumeric/underscore character (or
  # start of line) and followed by a non-alphanumeric/underscore
  # character (or end of line).
  #
  # We use regsub with a lookbehind/lookahead approximation:
  # match the boundary characters and preserve them.
  #
  set pattern "(?:^|(\[^a-zA-Z0-9_\]))${name}(?=\[^a-zA-Z0-9_\]|$)"
  set replacement "\\1se_${name}"
  set data [regsub -all -line $pattern $data $replacement]
}

#
# Write the output file.
#
set fd [open $output w]
fconfigure $fd -translation binary
puts -nonewline $fd $data
close $fd

set nLines [llength [split $data \n]]
puts "mkspilornis: $input -> $output"
puts "  Lines: $nLines"
puts "  Types renamed: [llength $typeNames]"
puts "  Guards renamed: [llength $guardNames]"
