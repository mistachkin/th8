###############################################################################
#
# exproverflow.tcl --
#
# Regression tests for integer-overflow error behavior in [expr]
# arithmetic with bigint DISABLED: multiply, add, and
# exponentiation overflow must raise "integer overflow" rather
# than silently wrapping.  These distinct operations complement
# the divide/modulus overflow checks covered elsewhere.
#
# (History: authored as a coverage sweep for th8_expr.c's
# overflow arms; those arms turned out already-covered, but the
# checks are unique and retained here as regression -- moved out
# of tests/coverage/.)
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test exproverflow-1.1 {
  Integer multiply, add, and exponentiation overflow with bigint
  disabled each raise "integer overflow" rather than wrapping or
  promoting -- the multiply/add checks in th8ExprEval and the
  exponentiation-overflow check.  Complements the divide/modulus
  overflow regression in coverage_expr_int_min_no_bigint.tcl.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set max 9223372036854775807
  set r [list]
  lappend r [catch {expr {$max * 2}} m] [string match *overflow* $m]
  lappend r [catch {expr {$max + 1}} m] [string match *overflow* $m]
  lappend r [catch {expr {2 ** 100}} m] [string match *overflow* $m]
  set r
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint max r m
} -result {1 1 1 1 1 1}}

###############################################################################
#
# Section 2 -- [expr] recursion-depth bound (TH8K-019).
#
# TH8 evaluates expressions with a recursive-descent tree builder and
# evaluator, so it bounds nesting explicitly at TH8_MX_EXPR_DEPTH (1000)
# and reports "expression nested too deeply" instead of overflowing the
# native C stack.  These are th8-constrained: the reference Tcl uses a
# bytecode/iterative evaluator with no such limit, so it would evaluate
# these deep expressions rather than reject them.
#
###############################################################################

runTest {test exproverflow-2.1 {
  A deeply nested parenthesised expression is rejected cleanly with
  "expression nested too deeply" (bounds th8ExprMakeTree recursion)
  rather than crashing.
} -constraints {
    th8
} -setup {
  set deep "[string repeat ( 5000]1[string repeat ) 5000]"
} -body {
  list [catch {expr $deep} m] $m
} -cleanup {
  unset -nocomplain deep m
} -result {1 {expression nested too deeply}}}

###############################################################################

runTest {test exproverflow-2.2 {
  A long operator chain (deep evaluation/free tree) is rejected cleanly
  with "expression nested too deeply" (bounds th8ExprEval recursion)
  rather than overflowing the native stack.
} -constraints {
    th8
} -setup {
  set chain "1[string repeat +1 5000]"
} -body {
  list [catch {expr $chain} m] $m
} -cleanup {
  unset -nocomplain chain m
} -result {1 {expression nested too deeply}}}

###############################################################################

runTest {test exproverflow-2.3 {
  A very long operator chain (well beyond the limit) is still rejected
  cleanly and its oversized parse tree is torn down without a stack
  overflow (iterative th8ExprFree).
} -constraints {
    th8
} -setup {
  set chain "1[string repeat +1 100000]"
} -body {
  # The catch returning 1 (not a crash) demonstrates the iterative free
  # survived a 100000-deep tree the evaluator refused to evaluate.
  catch {expr $chain}
} -cleanup {
  unset -nocomplain chain
} -result {1}}

###############################################################################

runTest {test exproverflow-2.4 {
  Expressions nested just under the limit still evaluate normally, and a
  genuine syntax error is still reported as such (the depth bound does
  not mask ordinary parse errors).
} -constraints {
    th8
} -body {
  list [expr "[string repeat ( 900]1[string repeat ) 900]"] \
      [expr "1[string repeat +1 900]"] \
      [catch {expr {1 + }} m]
} -cleanup {
  unset -nocomplain m
} -result {1 901 1}}

###############################################################################

source tests/epilogue.tcl
