###############################################################################
#
# coverage_double_inf_prefix.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c L15627 Th8_ToDouble Inf-
# prefix detector C4-Pair and C5-Pair:
#
#   if (i + 3 <= n && (z[i] == 'I' || z[i] == 'i')
#           && (z[i+1] == 'n' || z[i+1] == 'N')
#           && (z[i+2] == 'f' || z[i+2] == 'F'))
#
# Existing tests cover the "Inf"/"Infinity"/"inf" successful
# parse vectors plus a handful of failure vectors with z[i+2]
# not matching f/F.  The missing C4-Pair / C5-Pair vector is
# (T,F,T,F,F,-,-) -- string starts with 'i' lowercase but the
# second character is neither 'n' nor 'N', so the OR at L15628
# short-circuits to F and the entire AND chain is F at C5.
#
# Driver: [expr {double("iqf")}] -- a 3-char string starting
# with 'i', second char 'q' (not n/N), third char 'f'.
# Forces Th8_ToDouble through the inf-prefix check where C4=F
# and C5=F simultaneously, producing the missing pair vector.
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

runTest {test dblinf-1.1 {
  expr double("iqf") drives th8_core.c L15627 Inf-prefix
  C4-Pair / C5-Pair vector (T,F,T,F,F,-,-) F -- z[i]='i'
  matches the i/I OR (C3=T), but z[i+1]='q' is neither 'n'
  nor 'N' so C4=F and C5=F, short-circuiting the AND chain
  before C6/C7 are evaluated.  Th8_ToDouble returns ERROR,
  which propagates as a Tcl error.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {double("iqf")}} m]
  lappend rcs [catch {expr {double("ixf")}} m]
  lappend rcs [catch {expr {double("iaF")}} m]
  lappend rcs [catch {expr {double("iZf")}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1 1}}

###############################################################################

runTest {test dblinf-1.2 {
  Variant of dblinf-1.1 using uppercase first 'I' to also
  drive the C2-side of the (I|i) OR with neither-n-nor-N
  second char.  Result strings are 3-char "IqF", "IxF" etc;
  same L15627 path with C2=T (z[i]='I'), C3 masked, C4=F,
  C5=F.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {double("Iqf")}} m]
  lappend rcs [catch {expr {double("IxF")}} m]
  lappend rcs [catch {expr {double("IaF")}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

source tests/epilogue.tcl
