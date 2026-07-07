###############################################################################
#
# coverage_flags_show_multi.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L688
# inside the th8AfFormat function (flags show formatter):
#
#   if (bSpace && nOut > 0) {
#       Th8_StringAppend(interp, &zOut, &nOut, " ", 1);
#   }
#
# Closes the (T,T) vector -- bSpace=1 enabled AND nOut > 0
# (i.e., we are past the first iteration so a leading separator
# space is needed).  Existing fl_show-1.1 uses single-key input
# ("ABEKLNSTY_") so the loop runs once, never reaches nOut > 0,
# and L688's (T,T) vector is uncovered.
#
# This test passes a TWO-key complex flag dict with -space so
# the second iteration emits a leading space, driving (T,T).
# Result format: keys are space-separated.
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

runTest {test fl_show_multi-1.1 {
  [flags change -complex -space] on a two-key dict drives
  th8_attrflags.c L688 (T,T) -- bSpace=1 and nOut>0 on the
  second-and-later iterations.  Note: `flags show` calls
  Th8_AttrFlagsFormat per-key with bSpace=0 (one entry per
  call), so it never drives L688's (T,T); `flags change`
  emits the whole result map in a single Format call with
  bSpace forwarded, so multi-key input triggers the
  separator-emit branch.
} -constraints {
    th8
} -setup {
} -body {
  set r [flags change -complex -space "{1:abc} {2:def}" +x]
  expr {[string length $r] > 0 && [string match "*1*2*x*" $r]}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
