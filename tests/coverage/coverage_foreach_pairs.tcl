###############################################################################
#
# coverage_foreach_pairs.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [foreach] multi-pair iteration
# state checks in src/plugins/th8_looping.c:
#
#   :727  if (pp->nVar > 0 && pp->iIndex < pp->nValue)
#   :747  if (pp->azVar && pp->azValue
#                 && pp->iIndex + jj < pp->nValue)
#
# Existing foreach tests cover the common single-pair case
# (T,T) but leave the (T,F) "this pair exhausted but another
# is not" and (F,-) "empty varlist" vectors uncovered.  This
# file drives those by passing an empty varlist and by
# mismatched-length pairs in a multi-pair foreach.
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

runTest {test feloop-1.1 {
  Mismatched-length multi-pair foreach drives the (T,F)
  vector at th8_looping.c:727 -- one pair exhausts before
  the other; nVar > 0 (T) but iIndex >= nValue (F) for the
  shorter pair while the longer continues.  Also drives
  the (T,T,F) and (T,T,T) vectors at line 747 inside the
  per-iteration variable assignment loop.
} -constraints {
    th8
} -body {
  set r {}
  foreach {a b} {1 2 3 4} {c} {x y z} {
      lappend r [list $a $b $c]
  }
  set r
} -cleanup {
  unset -nocomplain r a b c
} -result {{1 2 x} {3 4 y} {{} {} z}}}

###############################################################################

runTest {test feloop-1.2 {
  Empty varlist drives the (F,-) vector at line 727
  (nVar == 0 short-circuits).  TH8's foreach treats this
  as a no-op: the body never runs and the result is empty.
} -constraints {
    th8
} -body {
  set bodyRan 0
  foreach {} {} {set bodyRan 1}
  set bodyRan
} -cleanup {
  unset -nocomplain bodyRan
} -result {0}}

###############################################################################

runTest {test feloop-1.3 {
  Multi-pair foreach where the LONGER pair ends first --
  drives a different ordering of (T,F) at 727 and the
  multi-variable assignment at 747 with iIndex+jj past
  end-of-values for one var but not another in the same
  pair (3 vars, 4 values: third iteration fills v1=4,
  v2="", v3="").
} -constraints {
    th8
} -body {
  set r {}
  foreach {v1 v2 v3} {1 2 3 4} {tag} {A B} {
      lappend r [list $v1 $v2 $v3 $tag]
  }
  set r
} -cleanup {
  unset -nocomplain r v1 v2 v3 tag
} -result {{1 2 3 A} {4 {} {} B}}}

###############################################################################

source tests/epilogue.tcl
