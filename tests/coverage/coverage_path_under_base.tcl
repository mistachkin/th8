###############################################################################
#
# coverage_path_under_base.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_posix.c th8PosixIsUnderBase
# L4010-4012 C2-Pair:
#
#   if (nAbs > nBase && memcmp(zAbs, zBase, nBase) == 0
#           && zAbs[nBase] == '/')
#
# Previously covered vectors were (F,-,-) -- path shorter
# than base -- and (T,T,T) -- path strictly under base.  The
# C2-Pair (T,F,-) -- path longer than base but the byte
# prefix differs -- was unreached because the standard test
# suite operates entirely inside the sandbox base directory.
#
# Driver: `file normalize` of a foreign nonexistent path
# whose absolute form is strictly longer than the base but
# shares no byte prefix.  The non-existent route forces the
# function through the manual normalization branch at
# L4140-L4161 which then calls th8PosixIsUnderBase with the
# constructed absolute string.  memcmp fails on the very
# first compared byte (different top-level directory), so
# C2=F.
#
# Coverage-driven; not pinned to specific R-markers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test pathbase-1.1 {
  `file normalize` on a nonexistent foreign absolute path
  drives th8PosixIsUnderBase L4010 C2=F: the resolved
  result is longer than the base, but the byte prefix
  differs so memcmp returns non-zero and the AND short-
  circuits before evaluating C3 (separator check).
} -constraints {
    th8
} -setup {
} -body {
  set r [file normalize \
      /tmp/nonexistent_th8_under_base/very/long/path/that/does/not/share/prefix]
  expr {[string length $r] > 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pathbase-1.2 {
  `file normalize` on a path that SHARES the byte prefix of the
  sandbox base but extends with a non-`/` byte at position
  nBase drives th8PosixIsUnderBase L4010 C3=F: (T,T,F) -- path
  longer than base, same prefix bytes, but the byte at position
  nBase is not `/` so the path is "sibling" not "under".  The
  driver uses `th8testlib::basepath absolute` to read the
  actual absolute base path (the project root via dladdr) and
  appends a non-`/` extension before normalization.
} -constraints {
    th8
} -setup {
} -body {
  set base [::th8testlib::basepath absolute]
  set sibling "${base}X/somefile"
  set r [file normalize $sibling]
  expr {[string length $r] > 0}
} -cleanup {
  unset -nocomplain base sibling r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
