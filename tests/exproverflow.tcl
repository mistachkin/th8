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
  ::th8testlib::bigint disable
} -body {
  set max 9223372036854775807
  set r [list]
  lappend r [catch {expr {$max * 2}} m] [string match *overflow* $m]
  lappend r [catch {expr {$max + 1}} m] [string match *overflow* $m]
  lappend r [catch {expr {2 ** 100}} m] [string match *overflow* $m]
  set r
} -cleanup {
  ::th8testlib::bigint enable
  unset -nocomplain max r m
} -result {1 1 1 1 1 1}}

###############################################################################

source tests/epilogue.tcl
