###############################################################################
#
# coverage_math_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for math-function decisions in
# src/th8_libc.c that aren't fully exercised by the standard
# mathfunc.tcl test file.  Existing tests cover one ordering of
# arguments; the missing vectors are the symmetrical case.
#
#   :545  isnan(a) || isnan(b)   (TH8_MATH_ISUNORDERED)
#                                 missing C2=T (a not NaN, b is NaN)
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

runTest {test mathmisc-1.1 {
  isunordered with second-argument NaN drives the C2=T
  vector at th8_libc.c:545 -- isnan(a) is F (a is finite),
  short-circuit OR evaluates isnan(b) which is T, result T.
  Existing mathfunc-9.9 covers (T,-) [first arg NaN] and
  (F,F) [neither NaN]; this closes the C2-pair.
} -constraints {
    c99math th8
} -body {
  list \
      [expr {isunordered(1.0, NaN)}] \
      [expr {isunordered(2.5, NaN)}] \
      [expr {isunordered(0.0, NaN)}]
} -result {1 1 1}}

###############################################################################

runTest {test mathmisc-2.1 {
  abs() of an integer-too-large-for-int that does NOT match
  the bigint format drives the C1-pair at th8_math.c:57 (
  the th8MathAbs bigint fast-path is gated on
  Th8_IsBigintEnabled).  We disable bigint via the testlib
  toggle, then call abs() on an integer that overflows int
  but is still a valid double -- routing through the double
  fall-through branch (L68-71).  With bigint disabled, C1=F
  short-circuits the IsBigint check.
} -constraints {
    th8 bigint
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set rcs {}
  lappend rcs [catch {expr {abs(9999999999999999999.5)}} m]
  lappend rcs [catch {expr {abs(-1e20)}} m]
  lappend rcs [catch {expr {abs(3.14)}} m]
  set rcs
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint rcs m
} -result {0 0 0}}

###############################################################################

runTest {test mathmisc-2.2 {
  expr abs() of a POSITIVE bigint drives the C2=F vector
  at th8_math.c:60 (the leading-'-' check in the bigint
  fast-path).  Existing tests use a negative bigint
  (C2=T); this closes the C2-pair via a positive bigint
  value where z1[0] is a digit, not '-'.
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  lappend rcs [expr {abs(99999999999999999999)}]
  lappend rcs [expr {abs(12345678901234567890)}]
  lappend rcs [expr {abs(-99999999999999999999)}]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {99999999999999999999 12345678901234567890 99999999999999999999}}

###############################################################################

runTest {test mathmisc-3.1 {
  expr min() and max() with first argument as a double drive
  the C1=F vector at th8_math.c:300 (th8MathMax double
  fallback) and L349 (th8MathMin double fallback) -- the
  preceding int fast-path Th8_ToInt fails on the float
  argument, falls through to the double branch where C1
  (Th8_ToDouble(z1)==TH8_OK) is T but the entry was reached
  because the int path returned without setting result.
  Existing tests use all-int args; this closes the
  double-fallback C1 vector.
} -constraints {
    th8
} -body {
  set rcs {}
  # First arg double, second int: int fast-path's Th8_ToInt
  # fails on first arg, falls through to double fallback.
  lappend rcs [expr {min(1.5, 2)}]
  lappend rcs [expr {max(1.5, 2)}]
  lappend rcs [expr {min(3.14, 4)}]
  lappend rcs [expr {max(3.14, 4)}]
  # Both args double.
  lappend rcs [expr {min(1.5, 2.5)}]
  lappend rcs [expr {max(1.5, 2.5)}]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {1.5 2.0 3.14 4.0 1.5 2.5}}

###############################################################################

runTest {test mathmisc-4.1 {
  expr math functions (fpclassify, sin, cos, etc.) with a
  NON-NUMERIC string argument drive the C2=T vector at
  th8_math.c:L496 (th8MathClassify) and L553
  (th8MathTranscendental) -- z1 is non-NULL (C1=T) and
  Th8_ToDouble returns non-OK (C2=T), so the early-return
  TH8_ERROR fires.  Existing tests pass numeric arguments
  (C2=F); this closes the C2-pair for both functions.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {fpclassify("abc")}} m]
  lappend rcs [catch {expr {sin("xyz")}} m]
  lappend rcs [catch {expr {cos("notanumber")}} m]
  lappend rcs [catch {expr {tan("nope")}} m]
  lappend rcs [catch {expr {log("foobar")}} m]
  lappend rcs [catch {expr {exp("baz")}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test mathmisc-4.2 {
  expr acos/asin with an IN-RANGE argument (|a| <= 1.0)
  drives the (F,F) vector at th8_libc.c:394/398 (the
  domain-error bounds check).  Existing tests pass only
  out-of-range values (e.g. acos(2.0), asin(-1.5)), which
  always trigger the error path (C1=T or C2=T); this
  closes both C1 and C2 pairs by computing a valid result.
} -constraints {
    th8 c99math
} -body {
  set rcs {}
  lappend rcs [expr {acos(0.0) > 1.5 && acos(0.0) < 1.6}]
  lappend rcs [expr {asin(0.0) == 0.0}]
  lappend rcs [expr {acos(1.0) == 0.0}]
  lappend rcs [expr {acos(-1.0) > 3.14}]
  lappend rcs [expr {asin(1.0) > 1.5 && asin(1.0) < 1.6}]
  lappend rcs [expr {asin(-1.0) < -1.5 && asin(-1.0) > -1.6}]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test mathmisc-max-min-string-1.1 {
  [expr max("abc", 1.0)] and [expr min("abc", 1.0)] both
  reach the double-fallback at src/th8_math.c L300 / L349
  with z1 NOT parseable as a double, driving the C1=F
  vector that no normal numeric usage triggers.  The
  expressions error out but the C-level decisions are
  exercised before the error propagates.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {max("abc", 1.0)}} m]
  lappend rcs [catch {expr {min("abc", 1.0)}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

runTest {test mathmisc-max-min-string-1.2 {
  Complement to mathmisc-max-min-string-1.1: passes a
  parseable first arg and an unparseable second to drive
  the C2=F vector at L300 / L349.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {max(1.0, "abc")}} m]
  lappend rcs [catch {expr {min(1.0, "abc")}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

runTest {test mathmisc-max-min-nobigint-1.1 {
  [max] and [min] with double args while bigint is disabled
  drive C1=F at th8_math.c L289 (th8MathMax) and L338
  (th8MathMin) -- the `Th8_IsBigintEnabled(interp)` check
  short-circuits the bigint block.  Existing tests run with
  bigint ON (C1=T), and double-arg tests fall through; this
  closes the C1-pair on the bigint guard via the explicit
  bigint-off scenario.
} -constraints {
    th8 bigint
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set rcs {}
  lappend rcs [expr {max(1.5, 2.5)}]
  lappend rcs [expr {min(1.5, 2.5)}]
  set rcs
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint rcs
} -result {2.5 1.5}}

###############################################################################

runTest {test mathmisc-isqrt-1.1 {
  expr isqrt() drives th8_math.c L246 th8MathIsqrt -- the
  function is registered as a math command but no prior test
  exercised it.  Covers: positive integer (success path),
  zero (root=0 boundary), negative (error path), non-numeric
  (Th8_ToInt failure).
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [expr {isqrt(0)}]
  lappend rcs [expr {isqrt(1)}]
  lappend rcs [expr {isqrt(4)}]
  lappend rcs [expr {isqrt(15)}]
  lappend rcs [expr {isqrt(16)}]
  lappend rcs [expr {isqrt(100)}]
  lappend rcs [catch {expr {isqrt(-1)}} m]
  lappend rcs [catch {expr {isqrt("abc")}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 1 2 3 4 10 1 1}}

###############################################################################

source tests/epilogue.tcl
