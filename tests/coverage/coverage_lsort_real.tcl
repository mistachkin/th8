###############################################################################
#
# coverage_lsort_real.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for lsort -real bad-operand parse
# compounds at src/plugins/th8_lists.c:
#
#   :1264-1265  if (Th8_ToDouble(interp, zA, nA, &ra) != TH8_OK ||
#                   Th8_ToDouble(interp, zB, nB, &rb) != TH8_OK) {
#
# Existing lsort -real tests use all-numeric inputs, hitting
# only the (F,F) and (F,T) vectors of the parse-failure short-
# circuit.  The C1=T vector (left operand fails parsing) needs
# a list where the bad-double element compares as the LEFT
# operand at least once.  Sort comparators in TH8 use a stable
# merge so the bad element will appear as left in at least one
# comparison regardless of position; placing it first
# guarantees this on the first comparison.
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

runTest {test lsort_real-1.1 {
  lsort -real with the unparseable element FIRST drives the
  L1264 C1=T vector (left operand's Th8_ToDouble fails).
  The sort comparator gets called with (NOT_A_NUMBER, 1.0)
  as its first compare, the left ToDouble call fails, the
  function returns TH8_ERROR -- short-circuiting before the
  right operand is evaluated.  Asserts that the [lsort]
  call errors out cleanly.
} -constraints {
    th8
} -setup {
} -body {
  set rc [catch {lsort -real {NOT_A_NUMBER 1.0 2.0}} m]
  expr {$rc == 1}
} -cleanup {
  unset -nocomplain rc m
} -result {1}}

###############################################################################

source tests/epilogue.tcl
