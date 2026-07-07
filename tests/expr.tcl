###############################################################################
#
# expr.tcl --
#
# Tcl Language Standard
# Conformance Test File
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
# Section 1 -- expr: Arithmetic operators (+, -, *, /, %)
#
###############################################################################

runTest {test expr-1.1 {
  R-46538-24607: addition
} -body {
  expr {3 + 4}
} -result {7}}

###############################################################################

runTest {test expr-1.2 {
  R-46538-24607: subtraction
} -body {
  expr {10 - 3}
} -result {7}}

###############################################################################

runTest {test expr-1.3 {
  R-46538-24607: multiplication
} -body {
  expr {6 * 7}
} -result {42}}

###############################################################################

runTest {test expr-1.4 {
  R-11223-03145: integer division
} -body {
  expr {17 / 5}
} -result {3}}

###############################################################################

runTest {test expr-1.5 {
  R-11223-03145: modulo
} -body {
  expr {17 % 5}
} -result {2}}

###############################################################################

runTest {test expr-1.6 {
  R-11223-03145: negative arithmetic
} -body {
  expr {-3 + -4}
} -result {-7}}

###############################################################################

runTest {test expr-1.7 {
  R-23909-62226: floating point division
} -body {
  expr {7.0 / 2.0}
} -match regexp -result {^3\.50*$}}

###############################################################################

runTest {test expr-1.7a {
  R-23909-62226: IEEE 754 boundary doubles emit their
  canonical shortest-round-trip decimal form; DBL_MAX uses
  17 significant digits.
} -body {
  expr {1.7976931348623157e+308}
} -result {1.7976931348623157e+308}}

###############################################################################

runTest {test expr-1.7b {
  R-23909-62226: the smallest positive normal double
  (DBL_MIN_NORMAL = 2^-1022) round-trips through its
  17-digit canonical form.
} -body {
  expr {2.2250738585072014e-308}
} -result {2.2250738585072014e-308}}

###############################################################################

runTest {test expr-1.7c {
  R-23909-62226: the smallest positive subnormal (2^-1074)
  is emitted as the shortest decimal that uniquely
  identifies it.
} -body {
  expr {5e-324}
} -result {5e-324}}

###############################################################################

runTest {test expr-1.8 {
  R-11223-03145: unary minus
} -body {
  expr {-(5)}
} -result {-5}}

###############################################################################
#
# Section 2 -- expr: Comparison operators
#
###############################################################################

runTest {test expr-2.1 {
  R-57807-21393: less than true
} -body {
  expr {3 < 5}
} -result {1}}

###############################################################################

runTest {test expr-2.2 {
  R-57807-21393: less than false
} -body {
  expr {5 < 3}
} -result {0}}

###############################################################################

runTest {test expr-2.3 {
  R-57807-21393: greater than
} -body {
  expr {5 > 3}
} -result {1}}

###############################################################################

runTest {test expr-2.4 {
  R-57807-21393: less than or equal
} -body {
  expr {3 <= 3}
} -result {1}}

###############################################################################

runTest {test expr-2.5 {
  R-57807-21393: greater than or equal
} -body {
  expr {3 >= 4}
} -result {0}}

###############################################################################

runTest {test expr-2.6 {
  R-57807-21393: numeric equality
} -body {
  expr {42 == 42}
} -result {1}}

###############################################################################

runTest {test expr-2.7 {
  R-57807-21393: numeric inequality
} -body {
  expr {42 != 43}
} -result {1}}

###############################################################################
#
# Section 3 -- expr: Logical operators
#
###############################################################################

runTest {test expr-3.1 {
  R-56068-58014: logical and true
} -body {
  expr {1 && 1}
} -result {1}}

###############################################################################

runTest {test expr-3.2 {
  R-56068-58014: logical and false
} -body {
  expr {1 && 0}
} -result {0}}

###############################################################################

runTest {test expr-3.3 {
  R-53262-03561: logical or true
} -body {
  expr {0 || 1}
} -result {1}}

###############################################################################

runTest {test expr-3.4 {
  R-53262-03561: logical or false
} -body {
  expr {0 || 0}
} -result {0}}

###############################################################################

runTest {test expr-3.5 {
  R-46538-24607: logical not true
} -body {
  expr {!0}
} -result {1}}

###############################################################################

runTest {test expr-3.6 {
  R-46538-24607: logical not false
} -body {
  expr {!1}
} -result {0}}

###############################################################################
#
# Section 4 -- expr: Ternary operator
#
###############################################################################

runTest {test expr-4.1 {
  R-57807-21393: ternary true branch
} -body {
  expr {1 ? "yes" : "no"}
} -result {yes}}

###############################################################################

runTest {test expr-4.2 {
  R-57807-21393: ternary false branch
} -body {
  expr {0 ? "yes" : "no"}
} -result {no}}

###############################################################################

runTest {test expr-4.3 {
  R-57807-21393: ternary with expression condition
} -setup {
} -body {
  set x 5
  expr {$x > 3 ? "big" : "small"}
} -cleanup {
  unset -nocomplain x
} -result {big}}

###############################################################################
#
# Section 5 -- expr: String eq/ne operators
#
###############################################################################

runTest {test expr-5.1 {
  R-57807-21393: string eq true
} -body {
  expr {"hello" eq "hello"}
} -result {1}}

###############################################################################

runTest {test expr-5.2 {
  R-57807-21393: string eq false
} -body {
  expr {"hello" eq "world"}
} -result {0}}

###############################################################################

runTest {test expr-5.3 {
  R-57807-21393: string ne true
} -body {
  expr {"hello" ne "world"}
} -result {1}}

###############################################################################

runTest {test expr-5.4 {
  R-57807-21393: string ne false
} -body {
  expr {"hello" ne "hello"}
} -result {0}}

###############################################################################
#
# Section 6 -- expr: Math functions
#
###############################################################################

runTest {test expr-6.1 {
  R-63118-53739: abs of negative
} -body {
  expr {abs(-5)}
} -result {5}}

###############################################################################

runTest {test expr-6.2 {
  R-63118-53739: abs of positive
} -body {
  expr {abs(5)}
} -result {5}}

###############################################################################

runTest {test expr-6.3 {
  R-63118-53739: int truncation
} -body {
  expr {int(3.7)}
} -result {3}}

###############################################################################

runTest {test expr-6.4 {
  R-63118-53739: double conversion
} -body {
  expr {double(3)}
} -match regexp -result {^3\.0+$}}

###############################################################################

runTest {test expr-6.5 {
  R-63118-53739: round function
} -body {
  expr {round(3.6)}
} -result {4}}

###############################################################################

runTest {test expr-6.6 {
  R-63118-53739: round down
} -body {
  expr {round(3.4)}
} -result {3}}

###############################################################################

runTest {test expr-6.7 {
  R-63118-53739: sqrt
} -body {
  expr {sqrt(16.0)}
} -match regexp -result {^4\.0+$}}

###############################################################################

runTest {test expr-6.8 {
  R-63118-53739: max function
} -body {
  expr {max(3, 7)}
} -result {7}}

###############################################################################

runTest {test expr-6.9 {
  R-63118-53739: min function
} -body {
  expr {min(3, 7)}
} -result {3}}

###############################################################################

runTest {test expr-6.10 {
  R-63118-53739: wide function
} -body {
  expr {wide(42)}
} -result {42}}

###############################################################################

runTest {test expr-6.11 {
  R-63118-53739: sin of zero
} -body {
  expr {sin(0.0)}
} -match regexp -result {^0\.0+$}}

###############################################################################

runTest {test expr-6.12 {
  R-63118-53739: cos of zero
} -body {
  expr {cos(0.0)}
} -match regexp -result {^1\.0+$}}

###############################################################################
#
# Section 7 -- expr: Operator precedence
#
###############################################################################

runTest {test expr-7.1 {
  R-46538-24607: multiplication before addition
} -body {
  expr {2 + 3 * 4}
} -result {14}}

###############################################################################

runTest {test expr-7.2 {
  R-26525-02176: parentheses override precedence
} -body {
  expr {(2 + 3) * 4}
} -result {20}}

###############################################################################

runTest {test expr-7.3 {
  R-46538-24607: comparison lower than arithmetic
} -body {
  expr {2 + 3 > 4}
} -result {1}}

###############################################################################

runTest {test expr-7.4 {
  R-56068-58014: logical and lower than comparison
} -body {
  expr {3 > 2 && 4 > 3}
} -result {1}}

###############################################################################
#
# Section 8 -- expr: Integer overflow detection
#
###############################################################################

runTest {test expr-8.1 {
  R-58455-48961: large integer multiplication
} -body {
  expr {1000000 * 1000000}
} -result {1000000000000}}

###############################################################################

runTest {test expr-8.2 {
  R-58455-48961: wide integer arithmetic
} -body {
  expr {wide(2147483647) + 1}
} -result {2147483648}}

###############################################################################
#
# Section 9 -- expr: Error cases
#
###############################################################################

runTest {test expr-9.1 {
  R-51583-32353: division by zero
} -setup {
} -body {
  list [catch {expr {1 / 0}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *divide by zero*}}

###############################################################################

runTest {test expr-9.2 {
  R-34938-53648: modulo by zero
} -setup {
} -body {
  list [catch {expr {1 % 0}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *divide by zero*}}

###############################################################################

runTest {test expr-9.3 {
  R-46538-24607: expr with no args
} -setup {
} -body {
  list [catch {expr} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test expr-9.4 {
  R-46538-24607: expr with invalid syntax
} -setup {
} -body {
  list [catch {expr {1 +}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 10 -- expr: Bitwise operators
#
###############################################################################

runTest {test expr-10.1 {
  R-46538-24607: bitwise AND
} -body {
  expr {0xFF & 0x0F}
} -result {15}}

###############################################################################

runTest {test expr-10.2 {
  R-46538-24607: bitwise OR
} -body {
  expr {0xF0 | 0x0F}
} -result {255}}

###############################################################################

runTest {test expr-10.3 {
  R-46538-24607: bitwise XOR
} -body {
  expr {0xFF ^ 0x0F}
} -result {240}}

###############################################################################

runTest {test expr-10.4 {
  R-46538-24607: bitwise NOT
} -body {
  expr {~0}
} -result {-1}}

###############################################################################

runTest {test expr-10.5 {
  R-46538-24607: left shift
} -body {
  expr {1 << 4}
} -result {16}}

###############################################################################

runTest {test expr-10.6 {
  R-46538-24607: right shift
} -body {
  expr {256 >> 4}
} -result {16}}

###############################################################################
#
# Section 11 -- expr: Integer division truncation
#
###############################################################################

runTest {test expr-11.1 {
  R-11223-03145: integer division truncates toward zero positive
} -body {
  expr {7 / 2}
} -result {3}}

###############################################################################

runTest {test expr-11.2 {
  R-11223-03145: integer division floors toward negative infinity
} -body {
  expr {-7 / 2}
} -result {-4}}

###############################################################################

runTest {test expr-11.3 {
  R-11223-03145: integer division negative divisor floors
} -body {
  expr {7 / -2}
} -result {-4}}

###############################################################################
#
# Section 12 -- expr: Modulo operator
#
###############################################################################

runTest {test expr-12.1 {
  R-11223-03145: modulo basic
} -body {
  expr {7 % 3}
} -result {1}}

###############################################################################

runTest {test expr-12.2 {
  R-11223-03145: modulo with negative dividend
} -body {
  expr {-7 % 3}
} -result {2}}

###############################################################################

runTest {test expr-12.3 {
  R-11223-03145: modulo with exact division
} -body {
  expr {9 % 3}
} -result {0}}

###############################################################################
#
# Section 13 -- expr: Short-circuit evaluation
#
###############################################################################

runTest {test expr-13.1 {
  R-56068-58014: && short-circuits when left is false
} -setup {
} -body {
  list [catch {expr {0 && [error "should not evaluate"]}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {0 0}}

###############################################################################

runTest {test expr-13.2 {
  R-53262-03561: || short-circuits when left is true
} -setup {
} -body {
  list [catch {expr {1 || [error "should not evaluate"]}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {0 1}}

###############################################################################

runTest {test expr-13.3 {
  R-56068-58014: && evaluates right side when left is true
} -setup {
} -body {
  list [catch {expr {1 && [error "evaluated"]}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 evaluated}}

###############################################################################

runTest {test expr-13.4 {
  R-53262-03561: || evaluates right side when left is false
} -setup {
} -body {
  list [catch {expr {0 || [error "evaluated"]}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 evaluated}}

###############################################################################
#
# Section 14 -- expr: Ternary operator additional tests
#
###############################################################################

runTest {test expr-14.1 {
  R-57807-21393: ternary with string result true
} -body {
  expr {1 ? "a" : "b"}
} -result {a}}

###############################################################################

runTest {test expr-14.2 {
  R-57807-21393: ternary with string result false
} -body {
  expr {0 ? "a" : "b"}
} -result {b}}

###############################################################################

runTest {test expr-14.3 {
  R-57807-21393: nested ternary
} -body {
  expr {1 ? (0 ? "a" : "b") : "c"}
} -result {b}}

###############################################################################

runTest {test expr-14.4 {
  R-57807-21393: ternary short-circuit true branch
} -setup {
} -body {
  list [catch {expr {1 ? "yes" : [error "should not evaluate"]}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {0 yes}}

###############################################################################

runTest {test expr-14.5 {
  R-57807-21393: ternary short-circuit false branch
} -setup {
} -body {
  list [catch {expr {0 ? [error "should not evaluate"] : "no"}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {0 no}}

###############################################################################
#
# Section 15 -- expr: Nested function calls
#
###############################################################################

runTest {test expr-15.1 {
  R-63118-53739: nested abs and int
} -body {
  expr {abs(int(-3.7))}
} -result {3}}

###############################################################################

runTest {test expr-15.2 {
  R-63118-53739: nested double and round
} -body {
  expr {round(double(7) / 2.0)}
} -result {4}}

###############################################################################
#
# Section 16 -- expr: String comparison operators eq/ne
#
###############################################################################

runTest {test expr-16.1 {
  R-57807-21393: eq with identical numeric strings
} -body {
  expr {"42" eq "42"}
} -result {1}}

###############################################################################

runTest {test expr-16.2 {
  R-57807-21393: ne with different strings
} -body {
  expr {"foo" ne "bar"}
} -result {1}}

###############################################################################

runTest {test expr-16.3 {
  R-57807-21393: eq with empty strings
} -body {
  expr {"" eq ""}
} -result {1}}

###############################################################################

runTest {test expr-16.4 {
  R-57807-21393: ne with empty vs non-empty
} -body {
  expr {"" ne "x"}
} -result {1}}

###############################################################################
#
# Section 17 -- expr: Exponentiation
#
###############################################################################

runTest {test expr-17.1 {
  R-46538-24607: exponentiation 2 ** 10
} -body {
  expr {2 ** 10}
} -result {1024}}

###############################################################################

runTest {test expr-17.2 {
  R-46538-24607: exponentiation with zero exponent
} -body {
  expr {5 ** 0}
} -result {1}}

###############################################################################

runTest {test expr-17.3 {
  R-46538-24607: exponentiation with exponent 1
} -body {
  expr {42 ** 1}
} -result {42}}

###############################################################################
#
# Section 18 -- expr: Backslash-newline continuation in expressions
#
###############################################################################

runTest {test expr-18.1 {
  R-46538-24607: expr with backslash-newline inside braces
} -body {
  expr {1 + \
      2 + \
      3}
} -result {6}}

###############################################################################

runTest {test expr-18.2 {
  R-46538-24607: multi-line expression with continuation evaluates correctly
} -body {
  expr {10 * \
      5 + \
      3}
} -result {53}}

###############################################################################

runTest {test expr-18.3 {
  R-46538-24607: backslash-newline in comparison expression
} -body {
  expr {100 > \
      50}
} -result {1}}

###############################################################################

runTest {test expr-18.4 {
  R-46538-24607: backslash-newline in ternary expression
} -body {
  expr {1 ? \
      "yes" : \
      "no"}
} -result {yes}}

###############################################################################

runTest {test expr-18.5 {
  R-46538-24607: backslash-newline in logical expression
} -body {
  expr {1 && \
      1 && \
      1}
} -result {1}}

###############################################################################

###############################################################################
#
# Section 19 -- expr: ternary operator
#
###############################################################################

runTest {test expr-19.1 {
  R-19076-33990: ternary true branch
} -body {
  expr {1 ? 42 : 0}
} -result {42}}

###############################################################################

runTest {test expr-19.2 {
  R-19076-33990: ternary false branch
} -body {
  expr {0 ? 42 : 99}
} -result {99}}

###############################################################################
#
# Section 20 -- expr: bitwise operators
#
###############################################################################

runTest {test expr-20.1 {
  R-14522-60989: bitwise AND
} -body {
  expr {0xFF & 0x0F}
} -result {15}}

###############################################################################

runTest {test expr-20.2 {
  R-14522-60989: bitwise OR
} -body {
  expr {0x0F | 0xF0}
} -result {255}}

###############################################################################

runTest {test expr-20.3 {
  R-14522-60989: bitwise XOR
} -body {
  expr {0xFF ^ 0x0F}
} -result {240}}

###############################################################################

runTest {test expr-20.4 {
  R-14522-60989: left shift
} -body {
  expr {1 << 8}
} -result {256}}

###############################################################################

runTest {test expr-20.5 {
  R-14522-60989: right shift
} -body {
  expr {256 >> 4}
} -result {16}}

###############################################################################

runTest {test expr-20.6 {
  R-14522-60989: bitwise complement
} -body {
  expr {~0}
} -result {-1}}

###############################################################################
#
# Section 21 -- expr: binary and octal literals
#
###############################################################################

runTest {test expr-21.1 {
  R-39874-12230: 0b binary literal
} -body {
  expr {0b1010}
} -result {10}}

###############################################################################

runTest {test expr-21.2 {
  R-39874-12230: 0o octal literal
} -body {
  expr {0o17}
} -result {15}}

###############################################################################

runTest {test expr-21.3 {
  R-39874-12230: 0b with leading zeros
} -body {
  expr {0b00001111}
} -result {15}}

###############################################################################

source tests/epilogue.tcl
