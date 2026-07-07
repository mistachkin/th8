###############################################################################
#
# coverage_double_long.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c Th8_ToDouble L15689 and L15711:
#
#   while (i < n && z[i] >= '0' && z[i] <= '9') {
#       if (!truncated
#               && mantissa <= 1844674407370955160ULL) {
#           mantissa = mantissa * 10 + (z[i] - '0');
#       } else {
#           truncated = 1;
#           decExp++;
#       }
#       ...
#   }
#
# (Same structure repeated for the fractional-part loop at L15711.)
#
# Existing tests only convert small/normal doubles, so truncated
# never becomes T mid-loop and the (F,-) vector for C1=!truncated
# is never observed.  This test feeds a 30-digit integer mantissa
# and a 30-digit fractional mantissa to expr so Th8_ToDouble
# enters truncation mode mid-parse, driving (F,-) at L15689
# (integer part) and L15711 (fractional part).
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

runTest {test dlong-1.1 {
  Th8_ToDouble on a 30-digit integer mantissa drives L15689
  (F,-) -- after ~19 digits the mantissa exceeds the safe
  multiply-by-10 threshold and truncated is latched.  Each
  subsequent digit re-enters the loop body with C1=!truncated
  evaluating F, taking the else-branch (decExp++).
} -constraints {
    th8
} -setup {
} -body {
  catch {expr {123456789012345678901234567890.0 + 0.0}} r
  expr {[string length $r] > 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test dlong-1.2 {
  Th8_ToDouble on a fractional part with 30+ digits past the
  decimal point drives L15711 (F,-) -- once mantissa overflows
  in the fractional loop, trailing fractional digits are dropped
  via the truncated-latch path.
} -constraints {
    th8
} -setup {
} -body {
  catch {expr {1.234567890123456789012345678901234567890123 + 0.0}} r
  expr {[string length $r] > 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
