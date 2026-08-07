###############################################################################
#
# coverage_expr_overflow_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Completes MC/DC for the integer-overflow-check decision family in
# src/th8_expr.c that the pre-existing arithmetic tests left partial.
# Each decision is a short-circuit OR-of-ANDs over the operand SIGNS
# and a magnitude test, so covering every condition's independence
# pair needs one drive per sign combination, both overflowing and
# not, plus a decision-disabled (overflow_check off) drive for the
# leading `interp->bOverflowCheck` condition.
#
# Targeted decisions (condition indices per llvm-cov -show-mcdc):
#
#   :1411 MULTIPLY overflow -- missing C9/C10 (iLeft>0 && iRight<0
#         pos*neg arm) and C12/C13 (iLeft<0 && iRight>0 neg*pos arm).
#   :1441 DIVIDE INT64_MIN/-1 -- missing C1 (overflow_check off) and
#         C3 (iRight != -1, i.e. INT64_MIN by something other than -1).
#   :1624 EXPONENT per-step multiply overflow -- missing the neg-base
#         and mixed-sign `result`/`base` arms.
#   :1638 EXPONENT base-squaring overflow -- missing C8/C9 (base<0
#         squaring-overflow arm).
#
# WHY bigint is disabled: with bigint enabled the early dispatch in
# th8_expr.c routes any expression whose operand STRING exceeds the
# int64 range to the bigint path, and an integer-overflow check that
# fires promotes to bigint instead of erroring.  The int64 decision
# still evaluates, but asserting a clean "integer overflow" error is
# only possible with bigint off; disabling it also makes the results
# deterministic.  (The pre-existing :1441 divide test in
# coverage_expr_arith.tcl runs with bigint ENABLED, so its intended
# (T,T,F)/(F,-,-) vectors never materialise -- its operands promote
# to bigint before reaching the int64 decision.  These tests build
# INT64_MIN as a genuine int64 via `-9223372036854775807 - 1`.)
#
# SAFETY: INT64_MIN / -1 with overflow checking OFF is C signed-
# overflow UB (SIGFPE on some targets), so the C1-off (overflow
# disabled) vector uses a benign division -- the leading condition
# short-circuits the decision regardless of the operands.
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

runTest {test exprovf-1.1 {
  MULTIPLY overflow, pos*neg arm (iLeft > 0 && iRight < 0):
  drives th8_expr.c:1411 C9=T, C10=T, C11=T -- a large
  positive times a negative that overflows INT64_MIN.  With
  bigint disabled the check errors instead of promoting.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  catch {expr {4611686018427387904 * -3}} m
  string match {*overflow*} $m
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint m
} -result {1}}

###############################################################################

runTest {test exprovf-1.2 {
  MULTIPLY no-overflow, pos*neg arm: iLeft > 0 && iRight < 0
  but the product fits, so th8_expr.c:1411 C9=T, C10=T but
  C11=F -- the pos*neg arm evaluates to F (no overflow) and
  ordinary multiplication proceeds.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  expr {1000000 * -3}
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint
} -result {-3000000}}

###############################################################################

runTest {test exprovf-1.3 {
  MULTIPLY overflow, neg*pos arm (iLeft < 0 && iRight > 0):
  drives th8_expr.c:1411 C12=T, C13=T, C14=T -- a large
  negative times a positive that overflows INT64_MIN.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  catch {expr {-4611686018427387904 * 3}} m
  string match {*overflow*} $m
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint m
} -result {1}}

###############################################################################

runTest {test exprovf-1.4 {
  MULTIPLY no-overflow, neg*pos arm: iLeft < 0 && iRight > 0
  with a product that fits -- th8_expr.c:1411 C12=T, C13=T,
  C14=F, decision F, ordinary multiplication.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  expr {-1000000 * 3}
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint
} -result {-3000000}}

###############################################################################

runTest {test exprovf-1.5 {
  MULTIPLY neg*neg no-overflow reaches th8_expr.c:1411 with
  the pos*neg (C9) and neg*pos-sign conditions evaluated F,
  giving those conditions their F-side companion vectors: a
  small neg*neg product does not overflow (decision F).
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  expr {-1000 * -1000}
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint
} -result {1000000}}

###############################################################################

runTest {test exprovf-2.1 {
  DIVIDE INT64_MIN by a divisor other than -1 drives
  th8_expr.c:1441 C3-Pair (T,T,F): iLeft == INT64_MIN,
  iRight != -1, so the special overflow case is skipped and
  ordinary floor division proceeds.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set min [expr {-9223372036854775807 - 1}]
  list [expr {$min / 2}] [expr {$min / 4}]
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint min
} -result {-4611686018427387904 -2305843009213693952}}

###############################################################################

runTest {test exprovf-2.2 {
  DIVIDE with overflow checking DISABLED drives th8_expr.c:1441
  C1-Pair (F,-,-): the leading interp->bOverflowCheck condition
  is F, short-circuiting the decision regardless of operands.
  A benign division is used deliberately -- INT64_MIN/-1 with
  checking off is signed-overflow UB.
} -constraints {
    th8
} -setup {
  set savedOverflow [::th8testlib::overflow_check query]
  ::th8testlib::overflow_check disable
} -body {
  expr {12 / -4}
} -cleanup {
  if {$savedOverflow} then { ::th8testlib::overflow_check enable } else { ::th8testlib::overflow_check disable }
  unset -nocomplain savedOverflow
} -result {-3}}

###############################################################################

runTest {test exprovf-3.1 {
  EXPONENT with overflow checking DISABLED drives the leading
  interp->bOverflowCheck condition F-side at th8_expr.c:1624 /
  :1638 -- with checking off the power wraps rather than being
  detected.  A modest exponent keeps the wrap well-defined.
} -constraints {
    th8
} -setup {
  set savedOverflow [::th8testlib::overflow_check query]
  ::th8testlib::overflow_check disable
} -body {
  expr {2 ** 10}
} -cleanup {
  if {$savedOverflow} then { ::th8testlib::overflow_check enable } else { ::th8testlib::overflow_check disable }
  unset -nocomplain savedOverflow
} -result {1024}}

###############################################################################

runTest {test exprovf-3.2 {
  EXPONENT positive-base overflow drives the pos*pos step-
  multiply arm at th8_expr.c:1624 and the positive-base
  squaring arm at :1638.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  list \
      [catch {expr {7 ** 23}} a] \
      [catch {expr {123 ** 15}} b] \
      [string match {*overflow*} $a] \
      [string match {*overflow*} $b]
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint a b
} -result {1 1 1 1}}

###############################################################################

runTest {test exprovf-3.3 {
  EXPONENT negative-base overflow drives the neg-base step-
  multiply arms at th8_expr.c:1624 (result/base sign flips
  across binary-exponentiation steps) and the negative-base
  squaring arm C8/C9 at :1638.  Odd exponent -> negative
  result; even exponent -> positive result; both overflow.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  list \
      [catch {expr {(-7) ** 23}} a] \
      [catch {expr {(-3) ** 40}} b] \
      [catch {expr {(-123) ** 15}} c] \
      [string match {*overflow*} $a] \
      [string match {*overflow*} $b] \
      [string match {*overflow*} $c]
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint a b c
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test exprovf-3.4 {
  EXPONENT base-squaring overflow with a base whose SQUARE
  overflows on the first doubling step (exp > 1) drives
  th8_expr.c:1638 -- 3037000500^2 > INT64_MAX.  Positive and
  negative base variants exercise the sign arms.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  list \
      [catch {expr {3037000500 ** 3}} a] \
      [catch {expr {(-3037000500) ** 3}} b] \
      [string match {*overflow*} $a] \
      [string match {*overflow*} $b]
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint a b
} -result {1 1 1 1}}

###############################################################################

source tests/epilogue.tcl
