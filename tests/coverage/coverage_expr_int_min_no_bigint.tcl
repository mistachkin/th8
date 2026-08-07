###############################################################################
#
# coverage_expr_int_min_no_bigint.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the int64-arithmetic overflow branches in th8_expr.c
# that are normally routed around when bigint is enabled.
# Specifically:
#
#   th8_expr.c:1394 (DIVIDE INT64_MIN/-1)  -- (T,T,T) vector
#   th8_expr.c:1413 (MODULUS INT64_MIN/-1) -- (T,T) vector
#   th8_expr.c:1512 (UNARY_MINUS INT64_MIN) -- (T,T) vector
#
# With bigint enabled, the L1338-1352 early dispatch routes
# any expression containing a bigint-formatted operand to
# the bigint path, so the int64 overflow checks are dead.
# Disabling bigint forces the path through to the integer-
# overflow error arm, exercising both the (T,T,T) entry and
# the (F=disabled) bigint-fallback miss inside.
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

runTest {test exprmin-1.1 {
  INT64_MIN / -1 with bigint disabled drives th8_expr.c:1394
  C2-Pair (T,T,T) -- the divide-overflow check fires with
  iLeft == TH8_INT64_MIN and iRight == -1, and the bigint-
  fallback at L1397 is F (disabled), so the path falls
  through to the "integer overflow" error arm.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set min [expr {-9223372036854775807 - 1}]
  catch {expr {$min / -1}} m
  string match {*overflow*} $m
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint min m
} -result {1}}

###############################################################################

runTest {test exprmin-2.1 {
  INT64_MIN % -1 with bigint disabled drives th8_expr.c:1413
  C1-Pair / C2-Pair (T,T) -- the modulus-overflow check
  branches to iRes = 0 (the mathematically-correct result
  for INT64_MIN % -1, no overflow possible).
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set min [expr {-9223372036854775807 - 1}]
  expr {$min % -1}
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint min
} -result {0}}

###############################################################################

runTest {test exprmin-2.2 {
  INT64_MIN % N (with N != -1) with bigint disabled drives
  the (T, F) C2-Pair at th8_expr.c:1424 -- iLeft == INT64_MIN
  but iRight is not -1, so the special-case branch is
  skipped and the path falls through to the ordinary floor
  modulus arithmetic.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set min [expr {-9223372036854775807 - 1}]
  list \
      [expr {$min % 3}] \
      [expr {$min % 7}] \
      [expr {$min % -3}]
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint min
} -match glob -result {*}}

###############################################################################

runTest {test exprmin-3.1 {
  -INT64_MIN with bigint disabled drives th8_expr.c:1512
  C2-Pair (T,T) -- unary minus on TH8_INT64_MIN overflows
  int64, bigint-fallback is F (disabled), falls through to
  "integer overflow" error arm.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set min [expr {-9223372036854775807 - 1}]
  catch {expr {-$min}} m
  string match {*overflow*} $m
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint min m
} -result {1}}

###############################################################################

source tests/epilogue.tcl
