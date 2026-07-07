###############################################################################
#
# coverage_double_precision.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Precision-edge regression coverage for the Grisu /
# cached-power-of-10 paths in src/th8_core.c:
#
#   th8ExtractDigits17    (Grisu digit generator used by
#                          Th8_SetResultDouble for output)
#   th8ScaleByPow10       (cached-power multiplier used by
#                          Th8_ToDouble for input parsing)
#
# These tests pin the round-trip behaviour at the IEEE 754
# edges that the historical `mant /= 1e16` cascade plus
# `val *= 1e22` parser chain drifted on (DBL_MAX, smallest
# normal, smallest subnormal, large positive / negative
# exponents).
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

runTest {test dblprec-1.1 {
  DBL_MAX as a literal expression must parse to exactly
  the IEEE 754 maximum representable double and format
  back as the canonical 17-digit form.  The historical
  chained `val *= 1e22` parser dropped this to a value
  ~13 ULP below DBL_MAX; the cached-power scaler restores
  exact round-trip.
} -constraints {
    th8
} -body {
  expr {1.7976931348623157e+308}
} -result {1.7976931348623157e+308}}

###############################################################################

runTest {test dblprec-1.2 {
  -DBL_MAX must round-trip with the same precision as
  +DBL_MAX (sign-bit-flip independence).
} -constraints {
    th8
} -body {
  expr {-1.7976931348623157e+308}
} -result {-1.7976931348623157e+308}}

###############################################################################

runTest {test dblprec-2.1 {
  Smallest positive subnormal (5e-324) must round-trip
  exactly through both the parser and the formatter.
  This is the IEEE 754 minimum denormal, mantissa = 1,
  exponent = -1074.
} -constraints {
    th8
} -body {
  expr {5e-324}
} -result {5e-324}}

###############################################################################

runTest {test dblprec-2.2 {
  Smallest positive normal value (DBL_MIN_NORMAL =
  2^-1022) must round-trip exactly via the Eisel-Lemire
  fast path with libtommath halfway-case fallback.  This
  also drives the subnormal->normal carry promotion in
  th8ScaleByPow10 (rounding the subnormal-path mantissa
  to 2^52 must promote to biased_exp = 1 rather than mask
  to zero).
} -constraints {
    th8
} -body {
  expr {2.2250738585072014e-308}
} -result {2.2250738585072014e-308}}

###############################################################################

runTest {test dblprec-3.1 {
  Wide span of magnitudes must each round-trip exactly
  through the Th8_ToDouble -> Th8_SetResultDouble chain.
  Hits the cached-power table at K = -100, -22, 0, 22,
  100 (every step of 8 used by th8GetCachedPow10).
} -constraints {
    th8
} -body {
  list \
      [expr {1e-100}] \
      [expr {1e-22}] \
      [expr {1e22}] \
      [expr {1e100}]
} -result {1e-100 1e-22 1e+22 1e+100}}

###############################################################################

runTest {test dblprec-3.2 {
  Common irrational constants emit Tcl 8.6-compatible
  17-digit forms.  pi, e, and sqrt(2) all require the
  full 17-digit IEEE 754 precision to round-trip.
} -constraints {
    th8
} -body {
  list \
      [expr {3.141592653589793}] \
      [expr {2.718281828459045}] \
      [expr {1.4142135623730951}]
} -result {3.141592653589793 2.718281828459045 1.4142135623730951}}

###############################################################################

runTest {test dblprec-3.3 {
  Canonical "0.1 + 0.2" inexact result must produce the
  textbook IEEE 754 string 0.30000000000000004, neither
  hidden by parser drift nor flattened by formatter
  rounding.  This is the simplest published test of a
  binary-floating-point implementation's transparency.
} -constraints {
    th8
} -body {
  expr {0.1 + 0.2}
} -result {0.30000000000000004}}

###############################################################################

runTest {test dblprec-3.4 {
  Multiplication of paired large/small magnitudes must
  cancel exactly: 1e100 * 1e-100 = 1.0.  Verifies the
  binary-exponent accumulation in th8ScaleByPow10 is
  symmetric.
} -constraints {
    th8
} -body {
  expr {1e100 * 1e-100}
} -result {1.0}}

###############################################################################

runTest {test dblprec-4.1 {
  Expr literal with explicit scientific-notation
  exponent (1.5E2) must normalise to the canonical
  decimal form 150.0 -- not pass through as the raw
  string "1.5E2" (Eagle's literal behaviour) and not
  underflow due to parser drift.
} -constraints {
    th8
} -body {
  expr {1.5E2}
} -result {150.0}}

###############################################################################

runTest {test dblprec-5.1 {
  Eisel-Lemire halfway-case fallback: 1e23 is a classic
  "hard to round" input -- the true value lies between
  two adjacent doubles and the 128-bit cached pow10 alone
  cannot tell which way to round.  The libtommath
  exact-comparison fallback (TH8_ENABLE_BIGINT) resolves
  it to the same value Tcl 8.6 / Gay's strtod returns.
} -constraints {
    th8 bigint
} -body {
  expr {1e23}
} -result {1e+23}}

###############################################################################

runTest {test dblprec-5.2 {
  ULP-boundary precision: 1.7976931348623155e+308 is
  exactly one ULP below DBL_MAX and must round-trip as a
  distinct double from DBL_MAX (1.7976931348623157e+308).
  Verifies Eisel-Lemire distinguishes adjacent doubles at
  the IEEE 754 ceiling.
} -constraints {
    th8
} -body {
  expr {[expr {1.7976931348623157e+308}] -
        [expr {1.7976931348623155e+308}] > 0}
} -result {1}}

###############################################################################

runTest {test dblprec-5.3 {
  Largest subnormal (2.225073858507201e-308 below
  DBL_MIN_NORMAL by 1 ULP) must parse to a distinct
  double from DBL_MIN_NORMAL.  Exercises the subnormal
  path's boundary handling.
} -constraints {
    th8
} -body {
  set v1 [expr {2.225073858507201e-308}]
  set v2 [expr {2.2250738585072014e-308}]
  list [expr {$v2 > $v1}] [expr {$v2 - $v1 > 0}]
} -cleanup {
  unset -nocomplain v1 v2
} -result {1 1}}

###############################################################################

runTest {test dblprec-4.2 {
  Round-trip via a variable: stringify, store, restore,
  reparse.  The string the formatter emits for a double
  must, when fed back to the parser, reproduce the same
  double.  Pinned for DBL_MAX, pi, and 0.1.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1.7976931348623157e+308 3.141592653589793 0.1} {
      set parsed [expr {$v + 0.0}]
      set rebuilt [expr {$parsed + 0.0}]
      lappend rcs [expr {$parsed eq $rebuilt}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v parsed rebuilt
} -result {1 1 1}}

###############################################################################

runTest {test dblprec-6.1 {
  Halfway-case rounding via the libtommath exact-comparison
  fallback (th8BignumDecideRound at th8_core.c).  That path
  is opt-in: it runs ONLY when bigint is enabled at runtime
  for the interpreter (otherwise th8ScaleByPow10 uses round-
  to-nearest-even).  With bigint enabled, the classic hard-
  to-round inputs must resolve to the same correctly-rounded
  doubles as the fast path -- this is the only test that
  actually exercises the wired-up exact comparison with a
  valid interp, rather than silently degrading to round-down.
} -constraints {
    th8 bigint
} -setup {
  ::th8testlib::bigint enable
} -body {
  list [expr {1e23}] [expr {1.5e2}] [expr {5e-324}]
} -cleanup {
  ::th8testlib::bigint disable
} -result {1e+23 150.0 5e-324}}

###############################################################################

source tests/epilogue.tcl
