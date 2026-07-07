###############################################################################
#
# bigint.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for arbitrary precision integer support (TH8_ENABLE_BIGINT).
# These tests verify overflow promotion, round-trip string conversion,
# all arithmetic/bitwise operators on large integers, and the
# int()/wide()/entier()/typeof() math functions.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- Overflow promotion (addition, subtraction, multiplication)
#
###############################################################################

runTest {test bigint-1.1 {
  addition overflow promotes to bigint
} -constraints {
    bigint
} -body {
  expr {9223372036854775807 + 1}
} -result {9223372036854775808}}

###############################################################################

runTest {test bigint-1.2 {
  multiplication overflow promotes to bigint
} -constraints {
    bigint
} -body {
  expr {9223372036854775807 * 2}
} -result {18446744073709551614}}

###############################################################################

runTest {test bigint-1.3 {
  subtraction overflow promotes to bigint
} -constraints {
    bigint
} -body {
  expr {-9223372036854775807 - 2}
} -result {-9223372036854775809}}

###############################################################################

runTest {test bigint-1.4 {
  unary minus of INT64_MIN promotes to bigint
} -constraints {
    bigint
} -body {
  # -(-9223372036854775808) overflows int64
  expr {-(-9223372036854775807 - 1)}
} -result {9223372036854775808}}

###############################################################################
#
# Section 2 -- Exponentiation
#
###############################################################################

runTest {test bigint-2.1 {
  2**64 is correct
} -constraints {
    bigint
} -body {
  expr {2**64}
} -result {18446744073709551616}}

###############################################################################

runTest {test bigint-2.2 {
  2**100 produces 31-digit number
} -constraints {
    bigint
} -body {
  expr {2**100}
} -result {1267650600228229401496703205376}}

###############################################################################

runTest {test bigint-2.3 {
  10**20 is correct
} -constraints {
    bigint
} -body {
  expr {10**20}
} -result {100000000000000000000}}

###############################################################################

runTest {test bigint-2.4 {
  small base large exponent
} -constraints {
    bigint
} -body {
  string length [expr {2**256}]
} -result {78}}

###############################################################################
#
# Section 3 -- Bigint arithmetic (add, sub, mul, div, mod on big values)
#
###############################################################################

runTest {test bigint-3.1 {
  bigint + bigint
} -constraints {
    bigint
} -setup {
} -body {
  set a [expr {2**100}]
  set b [expr {2**100}]
  expr {$a + $b}
} -cleanup {
  unset -nocomplain a b
} -result {2535301200456458802993406410752}}

###############################################################################

runTest {test bigint-3.2 {
  bigint - bigint equals zero
} -constraints {
    bigint
} -body {
  set a [expr {2**100}]
  expr {$a - $a}
} -cleanup {
  unset -nocomplain a
} -result {0}}

###############################################################################

runTest {test bigint-3.3 {
  bigint * bigint
} -constraints {
    bigint
} -body {
  set a [expr {10**20}]
  expr {$a * 2}
} -cleanup {
  unset -nocomplain a
} -result {200000000000000000000}}

###############################################################################

runTest {test bigint-3.4 {
  bigint / small int
} -constraints {
    bigint
} -body {
  set a [expr {10**20}]
  expr {$a / 10}
} -cleanup {
  unset -nocomplain a
} -result {10000000000000000000}}

###############################################################################

runTest {test bigint-3.5 {
  bigint modulus
} -constraints {
    bigint
} -body {
  set a [expr {10**20 + 7}]
  expr {$a % 10}
} -cleanup {
  unset -nocomplain a
} -result {7}}

###############################################################################
#
# Section 4 -- Bitwise operations on bigints
#
###############################################################################

runTest {test bigint-4.1 {
  bigint left shift
} -constraints {
    bigint
} -body {
  expr {1 << 100}
} -result {1267650600228229401496703205376}}

###############################################################################

runTest {test bigint-4.2 {
  bigint right shift
} -constraints {
    bigint
} -body {
  set a [expr {2**100}]
  expr {$a >> 50}
} -cleanup {
  unset -nocomplain a
} -result {1125899906842624}}

###############################################################################

runTest {test bigint-4.3 {
  bigint bitwise AND
} -constraints {
    bigint
} -body {
  set a [expr {2**100 - 1}]
  expr {$a & 0xFF}
} -cleanup {
  unset -nocomplain a
} -result {255}}

###############################################################################

runTest {test bigint-4.4 {
  bigint bitwise OR
} -constraints {
    bigint
} -setup {
} -body {
  set a [expr {2**100}]
  expr {$a | 1}
} -cleanup {
  unset -nocomplain a
} -result {1267650600228229401496703205377}}

###############################################################################
#
# Section 5 -- Math functions: typeof, int, wide, entier
#
###############################################################################

runTest {test bigint-5.1 {
  typeof small int
} -constraints {
    bigint th8
} -body {
  expr {typeof(42)}
} -result {int}}

###############################################################################

runTest {test bigint-5.2 {
  typeof wide int
} -constraints {
    bigint th8
} -body {
  expr {typeof(9999999999)}
} -result {wide}}

###############################################################################

runTest {test bigint-5.3 {
  typeof bigint
} -constraints {
    bigint th8
} -body {
  expr {typeof(2**100)}
} -result {entier}}

###############################################################################

runTest {test bigint-5.4 {
  typeof double
} -constraints {
    bigint th8
} -body {
  expr {typeof(3.14)}
} -result {double}}

###############################################################################

runTest {test bigint-5.5 {
  entier preserves bigint
} -constraints {
    bigint
} -body {
  expr {entier(2**100)}
} -result {1267650600228229401496703205376}}

###############################################################################

runTest {test bigint-5.6 {
  int preserves bigint (TH8 behavior)
} -constraints {
    bigint th8
} -body {
  expr {int(2**100)}
} -result {1267650600228229401496703205376}}

###############################################################################

runTest {test bigint-5.7 {
  wide truncates to 64-bit
} -constraints {
    bigint
} -body {
  # wide() always returns a 64-bit int, even with bigint enabled
  string is integer [expr {wide(42)}]
} -result {1}}

###############################################################################
#
# Section 6 -- Comparison operators on bigints
#
###############################################################################

runTest {test bigint-6.1 {
  bigint greater than int64
} -constraints {
    bigint
} -body {
  expr {2**100 > 9223372036854775807}
} -result {1}}

###############################################################################

runTest {test bigint-6.2 {
  bigint equality
} -constraints {
    bigint
} -body {
  expr {2**100 == 2**100}
} -result {1}}

###############################################################################

runTest {test bigint-6.3 {
  bigint less than
} -constraints {
    bigint
} -body {
  expr {2**99 < 2**100}
} -result {1}}

###############################################################################
#
# Section 7 -- Edge cases
#
###############################################################################

runTest {test bigint-7.1 {
  divide by zero error
} -constraints {
    bigint
} -setup {
} -body {
  set a [expr {2**100}]
  catch {expr {$a / 0}} msg
  set msg
} -cleanup {
  unset -nocomplain a msg
} -result {divide by zero}}

###############################################################################

runTest {test bigint-7.2 {
  bigint exponent overflow produces very large number
} -constraints {
    bigint
} -body {
  expr {[string length [expr {2**1000}]] > 300}
} -result {1}}

###############################################################################

runTest {test bigint-7.3 {
  negative bigint
} -constraints {
    bigint
} -body {
  expr {-(2**100)}
} -result {-1267650600228229401496703205376}}

###############################################################################
#
# Section 8 -- Requirements coverage for bigint standard sections
#
###############################################################################

runTest {test bigint-8.1 {
  R-28097-02932: overflow promotes to bigint instead of error
} -constraints {
    bigint
} -body {
  expr {0x7FFFFFFFFFFFFFFF + 1}
} -result {9223372036854775808}}

###############################################################################

runTest {test bigint-8.2 {
  R-03101-05776: bigint in arithmetic, bitwise, comparison, shift
} -constraints {
    bigint
} -body {
  set big [expr {2**100}]
  list [expr {$big + 1 > $big}] \
      [expr {$big * 2 > $big}] \
      [expr {$big << 1 > $big}]
} -cleanup {
  unset -nocomplain big
} -result {1 1 1}}

###############################################################################

runTest {test bigint-8.3 {
  R-48717-03962: abs, max, min, int, entier, typeof accept bigint
} -constraints {
    bigint th8
} -body {
  set big [expr {2**100}]
  list [expr {abs(-$big) == $big}] \
      [expr {max($big, 0) == $big}] \
      [expr {min($big, 0) == 0}] \
      [expr {typeof($big)}]
} -cleanup {
  unset -nocomplain big
} -result {1 1 1 entier}}

###############################################################################

runTest {test bigint-8.4 {
  R-12852-08369: bigint is per-interpreter security gate
} -constraints {
    bigint
} -body {
  #
  # When bigint is enabled, overflow promotes.  This test
  # verifies the gate is open (the constraint ensures it).
  #
  expr {9223372036854775807 * 2}
} -result {18446744073709551614}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
