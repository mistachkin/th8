###############################################################################
#
# coverage_bigint_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for bigint parsing decisions in
# src/th8_bigint.c, focusing on missing vectors:
#
#   :339  if (p[1] == 'x' || p[1] == 'X')
#                 (hex prefix, missing C2-pair: p[1] == 'X'
#                  -- uppercase variant)
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

runTest {test bigint-1.1 {
  Bigint hex literal with uppercase 'X' prefix drives the
  C2-pair at th8_bigint.c:339 (the 'x' check fails so the
  '|| p[1] == X' branch fires).  Combined with existing
  lowercase 'x' coverage, this closes the prefix-detect
  compound.
} -constraints {
    th8
} -body {
  list \
      [expr {0X1F * 2}] \
      [expr {0XAB + 0}] \
      [expr {0XFFFFFFFFFFFFFFFF + 1}] \
      [expr {0x123456789ABCDEF0 * 0X10}]
} -result {62 171 18446744073709551616 20988295479420645120}}

###############################################################################

runTest {test bigmsc-2.1 {
  expr max() / min() with one bigint and one regular
  int operand drives the C2/C3 pairs at th8MathMax
  (line 289-291) and th8MathMin (line 338-340) -- the
  bigint detection compound (Th8_IsBigintEnabled &&
  (th8IsBigint(z1) || th8IsBigint(z2))) needs each side
  tested independently.
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {max(99999999999999999999, 1)}] \
      [expr {max(1, 99999999999999999999)}] \
      [expr {min(99999999999999999999, 1)}] \
      [expr {min(1, 99999999999999999999)}]
} -result {99999999999999999999 99999999999999999999 1 1}}

###############################################################################

runTest {test bigmsc-3.1 {
  expr UNARY operations on bigints drive the C2=T vector
  at th8_expr.c:1128 (zLeft == 0 in the bigint-arith
  dispatch compound).  Unary minus, bitwise-NOT, etc.
  on a bigint operand have NULL left-side and the right
  side is bigint.
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {-99999999999999999999}] \
      [expr {~99999999999999999999}] \
      [expr {+99999999999999999999}]
} -result {-99999999999999999999 -100000000000000000000 99999999999999999999}}

###############################################################################

runTest {test bigmsc-3.2 {
  expr BINARY operations with one bigint and one int
  drive the C5/C7 pairs at th8_expr.c:1128 (right
  operand fits in WideInt).  Left bigint + right int
  drives C5=F, C6=F, C7=T (right ToWideInt OK).
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {99999999999999999999 + 1}] \
      [expr {1 + 99999999999999999999}] \
      [expr {99999999999999999999 - 1}] \
      [expr {99999999999999999999 * 2}]
} -result {100000000000000000000 100000000000000000000 99999999999999999998 199999999999999999998}}

###############################################################################

runTest {test bigmsc-3.3 {
  expr INTEGER bitwise operations with bigint operands
  drive the C5=T vector at th8_expr.c:1260-1263 (the
  bigint-ToWideInt-skip compound for INTEGER path).  Left
  int + right bigint exercises (T,T,F,T,T) -- the only
  vector that has C5=T.
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {1 & 99999999999999999999}] \
      [expr {99999999999999999999 & 1}] \
      [expr {1 | 99999999999999999999}] \
      [expr {99999999999999999999 ^ 1}]
} -result {1 1 99999999999999999999 99999999999999999998}}

###############################################################################

runTest {test bigmsc-3.4 {
  expr SHIFT and DIV/MOD operations on bigints exercise
  additional bigint paths through th8BigintBinaryOp /
  th8BigintShift dispatch and the related compounds in
  th8_expr.c.
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {99999999999999999999 << 1}] \
      [expr {99999999999999999999 >> 4}] \
      [expr {99999999999999999999 % 7}] \
      [expr {99999999999999999999 / 3}]
} -result {199999999999999999998 6249999999999999999 1 33333333333333333333}}

###############################################################################

runTest {test bigmsc-3.5 {
  expr left-shift with shift count > 63 (overflow path)
  drives the C2=T vector at th8_expr.c:1459-1460
  -- Th8_IsBigintEnabled T, iRight > 63 T -- routing the
  result through bigint.  Combined with negative-shift
  catch to exercise the iRight < 0 path of the same
  compound.
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  lappend rcs [expr {1 << 100}]
  lappend rcs [expr {1 << 64}]
  lappend rcs [expr {1 >> 100}]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {1267650600228229401496703205376 18446744073709551616 0}}

###############################################################################

runTest {test bigmsc-3.6 {
  expr ** (power) with operands that overflow int64 or
  exercise the 14-condition pow-overflow detection at
  th8_expr.c:1535.  Tests cover small-base/large-exp,
  bigint-base, negative-base, and zero edge cases that
  exercise the four overflow-direction subcompounds.
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {2 ** 100}] \
      [expr {99999999999999999999 ** 2}] \
      [expr {(-2) ** 64}] \
      [expr {2 ** 0}] \
      [expr {0 ** 0}]
} -result {1267650600228229401496703205376 9999999999999999999800000000000000000001 18446744073709551616 1 1}}

###############################################################################

runTest {test bigmsc-3.7 {
  expr ** (power) with int-base values that overflow
  during the multiply loop drive specific quadrants of
  the 14-condition pow overflow check at L1535:
    - 1000000 ** 5 -- positive base, positive growing
      (result>0 && base>0 && result>MAX/base) C3-pair
    - (-1000000) ** 5 -- positive*negative quadrant
    - (-1000000) ** 4 -- negative*negative quadrant
    - 2 ** 63 -- exact int64 boundary
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {1000000 ** 5}] \
      [expr {(-1000000) ** 5}] \
      [expr {(-1000000) ** 4}] \
      [expr {2 ** 63}]
} -result {1000000000000000000000000000000 -1000000000000000000000000000000 1000000000000000000000000 9223372036854775808}}

###############################################################################

runTest {test bigmsc-3.8 {
  expr left-shift with NEGATIVE shift count drives the
  C3=T vector at th8_expr.c:1459-1460 -- bigint
  enabled, iRight > 63 F, iRight < 0 T, routing to
  the bigint shift which errors with "negative shift
  count".  The catch wrapper accepts the error.
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  foreach inp {{99999999999999999999 << -1} {1 << -2} {1 << -100}} {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1}}

###############################################################################

runTest {test bigmsc-3.9 {
  expr int ADD/SUBTRACT operations whose result would
  overflow int64 drive the bigint-fallback compound at
  th8_expr.c:1416-1420 (ADD) and 1437-1441 (SUBTRACT).
  Both sign quadrants of the overflow check.
} -constraints {
    th8 bigint
} -body {
  set min [expr {-9223372036854775807-1}]
  set max 9223372036854775807
  list \
      [expr {$max + 1}] \
      [expr {$min + (-1)}] \
      [expr {$max - (-1)}] \
      [expr {$min - 1}]
} -cleanup {
  unset -nocomplain min max
} -result {9223372036854775808 -9223372036854775809 9223372036854775808 -9223372036854775809}}

###############################################################################

runTest {test bigmsc-3.10 {
  expr int MULTIPLY operations whose result would
  overflow int64 drive the bigint-fallback compound for
  TH8_OP_MULTIPLY (similar to ADD/SUB).  Tests both
  sign quadrants and edge cases at MIN*-1 / MAX*2.
} -constraints {
    th8 bigint
} -body {
  set max 9223372036854775807
  list \
      [expr {$max * 2}] \
      [expr {$max * -2}] \
      [expr {1000000000 * 1000000000 * 1000}]
} -cleanup {
  unset -nocomplain max
} -result {18446744073709551614 -18446744073709551614 1000000000000000000000}}

###############################################################################

runTest {test bigmsc-3.11 {
  expr COMPARISON operations with one bigint and one
  int operand drive bigint-comparison dispatch.  Tests
  <, >, ==, != with mixed bigint/int operands.
} -constraints {
    th8 bigint
} -body {
  set big 99999999999999999999
  list \
      [expr {$big < 1}] \
      [expr {1 < $big}] \
      [expr {$big > 1}] \
      [expr {$big == $big}] \
      [expr {$big != 1}] \
      [expr {$big >= $big}] \
      [expr {$big <= 99999999999999999999}]
} -cleanup {
  unset -nocomplain big
} -result {0 1 1 1 1 1 1}}

###############################################################################

runTest {test bigmsc-3.12 {
  expr min/max with mixed double + bigint operands
  exercise the int-path falls-through to double-path in
  th8_expr.c (the bigint-detect compound returns F for
  doubles, and min/max routes via Th8_ToDouble).
} -constraints {
    th8 bigint
} -body {
  list \
      [expr {min(1.5, 2)}] \
      [expr {max(1, 2.5)}] \
      [expr {min(99999999999999999999, 1.5)}]
} -result {1.5 2.5 1.5}}

###############################################################################

runTest {test bigmsc-3.13 {
  expr min/max with NON-NUMERIC operand drives the C1=F
  vector at th8_math.c:300 (Th8_ToDouble fails on left
  side, short-circuits the && check).  Both error paths
  produce "expected number" diagnostics.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {{max("abc", 1)} {min(1, "xyz")} {max("abc", "def")}} {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1}}

###############################################################################

runTest {test bigmsc-5.1 {
  expr LEFT-SHIFT (and RIGHT-SHIFT) of a bigint by an
  OUT-OF-RANGE right operand drives the C1=T vector at
  th8_bigint.c:487-488 (and the matching right-shift
  check) -- the right operand parses as a bigint OK but
  Th8_ToWideInt fails because the value exceeds int64
  range.  This short-circuits the shift<0 check and sets
  the "negative shift count" / "shift count too large"
  error (whichever the code uses).
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  # Right operand is a huge bigint -- bigint parse succeeds
  # but Th8_ToWideInt fails (overflows int64).
  lappend rcs [catch {expr {99999999999999999999 << 99999999999999999999}} m]
  lappend rcs [catch {expr {99999999999999999999 >> 99999999999999999999}} m]
  lappend rcs [catch {expr {1000000000000000000000 << 1000000000000000000000}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test bigmsc-5.2 {
  expr bigint shift by a NEGATIVE in-range integer drives
  the C2=T vector at th8_bigint.c:487-488 (left shift) and
  th8_bigint.c:500-501 (right shift) -- Th8_ToWideInt
  succeeds (C1=F) so the shift<0 sub-condition decides the
  outcome.  Existing tests cover the positive-shift happy
  path and the out-of-range-bigint failure (C1=T).
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  lappend rcs [catch {expr {99999999999999999999 << -1}} m]
  lappend rcs [catch {expr {99999999999999999999 >> -1}} m]
  lappend rcs [catch {expr {99999999999999999999 << -7}} m]
  lappend rcs [catch {expr {99999999999999999999 >> -7}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1 1}}

###############################################################################

runTest {test bigmsc-5.3 {
  expr LOGICAL-NOT on a bigint operand drives the C4-pair at
  th8_expr.c:1314-1317 -- with bigint enabled and the
  operand exceeding int64 range, the unary-op
  classification check evaluates C1..C3=F (not -, +, ~)
  and C4=T (LOGICAL_NOT).  Existing tests cover the other
  three unary ops on bigints; this closes the LOGICAL_NOT
  vector via "!".
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  lappend rcs [expr {! 99999999999999999999}]
  lappend rcs [expr {!99999999999999999999}]
  lappend rcs [expr {! 1000000000000000000000}]
  lappend rcs [expr {! -99999999999999999999}]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {0 0 0 0}}

###############################################################################

runTest {test bigmsc-5.4 {
  expr bigint EXPONENTIATION with a NEGATIVE or
  out-of-range integer exponent drives the C1=T / C2=T
  vectors at th8_bigint.c:527-528 (TH8_OP_EXPONENT
  arm).  Th8_ToWideInt failing on huge right operand
  drives C1=T; in-range negative exponent drives C2=T.
  Existing tests cover the happy positive-exponent
  path (C1=F,C2=F); this closes both pairs.
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  # C2=T: negative exponent (in-range).
  lappend rcs [catch {expr {99999999999999999999 ** -1}} m]
  lappend rcs [catch {expr {99999999999999999999 ** -5}} m]
  # C1=T: out-of-range exponent (huge bigint as exponent).
  lappend rcs [catch {expr {99999999999999999999 ** 99999999999999999999}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

source tests/epilogue.tcl
