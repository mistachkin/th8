###############################################################################
#
# coverage_scan_dual_e.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the C3=F (bSawExp=T) vector at the scan %f exponent-
# state-machine guard `(c == 'e' || c == 'E') && !bSawExp`
# in th8_formatting.c:1139.  Existing scan_cov tests stop at
# the first 'e' (bSawExp transitions 0->1 but never tests
# the post-set guard); inputs with two exponent markers
# ("1e2e3", "1E5e1") force the loop to re-encounter 'e'/'E'
# after bSawExp is set, exercising the guard's false path.
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

runTest {test scandualE-1.1 {
  scan %f against an input containing TWO 'e' markers
  drives the (T,-,F) vector at th8_formatting.c:1139 --
  on the second 'e' bSawExp is already set, so the loop
  breaks rather than re-consuming.  scan accepts the
  "1e2" prefix as 100.0 and stops at the second 'e'.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {1e2e3 1E5e1 9.9e10E2 2.5e-3e+4 1E+2E-1} {
      set rc [scan $inp "%f" v]
      lappend rcs $rc
      unset -nocomplain v
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs rc inp v
} -result {1 1 1 1 1}}

###############################################################################

source tests/epilogue.tcl
