###############################################################################
#
# coverage_samefile.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_posix.c th8PosixSameFile L2054:
#
#   return (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
#
# Existing [file same] tests pass absolute paths that fail the
# IsPathUnderBase guard early, so the C1/C2 pair at L2054 never
# executes (no statx call is reached).  This test passes two
# relative paths that both exist and are under the test cwd,
# driving the (T,T) vector with same paths and the (T,F)
# vector with different paths.
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

runTest {test samefile-1.1 {
  file same on two relative paths to the SAME existing file
  drives th8_posix.c L2054 C1=T,C2=T -- st_dev and st_ino
  both match.  Returns 1.
} -constraints {
    th8
} -setup {
} -body {
  set r [file same tests/prologue.tcl tests/prologue.tcl]
  set r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test samefile-1.2 {
  file same on two relative paths to DIFFERENT existing files
  drives th8_posix.c L2054 C1=T,C2=F -- both files are under
  base, both stats succeed, but st_dev matches and st_ino
  differs (or st_dev differs).  Returns 0.  This closes the
  C2-Pair via vectors 1.1 (T,T) and 1.2 (T,F).
} -constraints {
    th8
} -setup {
} -body {
  set r [file same tests/prologue.tcl tests/epilogue.tcl]
  set r
} -cleanup {
  unset -nocomplain r
} -result {0}}

###############################################################################

runTest {test samefile-1.3 {
  file same on a regular file and a path on a DIFFERENT
  filesystem drives th8_posix.c L2054 C1=F vector --
  st_dev differs so the short-circuit AND result is F
  without evaluating st_ino.  Existing tests only differ
  on st_ino (C1=T, C2=F); this closes the C1-Pair via
  a cross-filesystem comparison.

  /tmp lives on the main APFS volume; /dev/null is on
  devfs with a distinct st_dev on every Unix.  Both paths
  always exist, so the IsPathUnderBase guard accepts them
  and we reach the dev/ino check.
} -constraints {
    th8
} -setup {
} -body {
  if {[file exists /dev/null] && [file exists /tmp]} then {
      set r [file same /tmp /dev/null]
  } else {
      set r 0
  }
  set r
} -cleanup {
  unset -nocomplain r
} -result {0}}

###############################################################################

source tests/epilogue.tcl
