###############################################################################
#
# coverage_expr_bigint_type.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_expr.c L1171-1179 (bigint
# type-coercion fallback in th8ExprEval).  The 7-condition
# decision:
#
#   else if (Th8_IsBigintEnabled(interp)
#       && (zLeft  == 0 || th8IsBigint(...) || Th8_ToWideInt(...))
#       && (zRight == 0 || th8IsBigint(...) || Th8_ToWideInt(...)))
#
# Previously covered vectors all had the right-side OR
# evaluating to T (either zRight==0 for unary, or right is
# bigint, or right is a wide int).  The C5/C6/C7 pairs need a
# vector where the entire right OR fails (zRight non-NULL,
# right NOT a bigint, right NOT a parseable wide int) so the
# overall AND is F and control falls through to the double
# branch at L1188.
#
# Driver: an arithmetic operator with a bigint left operand
# and a non-numeric right operand.  The expression itself
# errors (the double fallback also rejects the non-numeric
# string), but the type-coercion decision at L1171 is still
# evaluated, giving the (T,F,T,-,F,F,F)=F vector that closes
# all three pairs at once.
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

runTest {test exprbigtype-1.1 {
  Binary arithmetic with a bigint left operand and a
  non-numeric right operand drives the right-side OR-chain
  to F at L1171 (zRight non-NULL && not bigint && not
  parseable as wide int).  The expression evaluation
  ultimately errors because the double fallback also
  rejects the non-numeric token; we just assert that the
  catch sees a non-zero rc.
} -constraints {
    th8 bigint
} -setup {
} -body {
  set s "abc"
  set rc [catch {expr {12345678901234567890123 + $s}} msg]
  expr {$rc != 0}
} -cleanup {
  unset -nocomplain rc msg s
} -result {1}}

###############################################################################

runTest {test exprbigtype-1.2 {
  Binary subtraction with a non-numeric left operand and a
  bigint right operand also exercises the bigint-fallback
  decision.  The right OR succeeds (C6 or C7 true), but the
  left OR fails (zLeft non-NULL, not bigint, not wide int),
  so the whole AND is F.  Same fall-through to the double
  branch and same error result; included as a second
  reachable F-vector to make the test resilient to vector
  re-ordering.
} -constraints {
    th8 bigint
} -setup {
} -body {
  set s "xyz"
  set rc [catch {expr {$s - 99999999999999999999999}} msg]
  expr {$rc != 0}
} -cleanup {
  unset -nocomplain rc msg s
} -result {1}}

###############################################################################

source tests/epilogue.tcl
