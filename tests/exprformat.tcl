###############################################################################
#
# exprformat.tcl --
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
# Section 1 -- expr: Octal integer literals (R-55151-16878)
#
###############################################################################

runTest {test exprformat-1.1 {
  R-55151-16878: octal literal 010 is 8
} -constraints {
    leadingOctal
} -body {
  expr {010}
} -result {8}}

###############################################################################

runTest {test exprformat-1.2 {
  R-55151-16878: octal literal 077 is 63
} -constraints {
    leadingOctal
} -body {
  expr {077}
} -result {63}}

###############################################################################

runTest {test exprformat-1.3 {
  R-55151-16878: octal literal 00 is 0
} -constraints {
    leadingOctal
} -body {
  expr {00}
} -result {0}}

###############################################################################
#
# Section 2 -- expr: Hexadecimal integer literals (R-10236-64979)
#
###############################################################################

runTest {test exprformat-2.1 {
  R-10236-64979: hex literal 0x10 is 16
} -body {
  expr {0x10}
} -result {16}}

###############################################################################

runTest {test exprformat-2.2 {
  R-10236-64979: hex literal 0xFF is 255
} -body {
  expr {0xFF}
} -result {255}}

###############################################################################

runTest {test exprformat-2.3 {
  R-10236-64979: hex literal 0x0 is 0
} -body {
  expr {0x0}
} -result {0}}

###############################################################################
#
# Section 3 -- expr: Floor division (R-02187-29178)
#
###############################################################################

runTest {test exprformat-3.1 {
  R-02187-29178: floor division 7 / 2 is 3
} -body {
  expr {7 / 2}
} -result {3}}

###############################################################################

runTest {test exprformat-3.2 {
  R-02187-29178: floor division -7 / 2 floors toward negative infinity
} -body {
  expr {-7 / 2}
} -result {-4}}

###############################################################################

runTest {test exprformat-3.3 {
  R-02187-29178: floor division 7 / -2 floors toward negative infinity
} -body {
  expr {7 / -2}
} -result {-4}}

###############################################################################

runTest {test exprformat-3.4 {
  R-02187-29178: floor division -7 / -2 is 3
} -body {
  expr {-7 / -2}
} -result {3}}

###############################################################################
#
# Section 4 -- expr: Modulo same sign as divisor (R-49019-45026)
#
###############################################################################

runTest {test exprformat-4.1 {
  R-49019-45026: modulo -7 % 3 has same sign as divisor
} -body {
  expr {-7 % 3}
} -result {2}}

###############################################################################

runTest {test exprformat-4.2 {
  R-49019-45026: modulo 7 % -3 has same sign as divisor
} -body {
  expr {7 % -3}
} -result {-2}}

###############################################################################

runTest {test exprformat-4.3 {
  R-49019-45026: modulo 7 % 3 positive result
} -body {
  expr {7 % 3}
} -result {1}}

###############################################################################

runTest {test exprformat-4.4 {
  R-49019-45026: modulo -7 % -3 has same sign as divisor
} -body {
  expr {-7 % -3}
} -result {-1}}

###############################################################################
#
# Section 5 -- expr: Logical operators produce integer 0 or 1 (R-33950-35991)
#
###############################################################################

runTest {test exprformat-5.1 {
  R-33950-35991: && produces integer 1 when both true
} -body {
  expr {5 && 3}
} -result {1}}

###############################################################################

runTest {test exprformat-5.2 {
  R-33950-35991: && produces integer 0 when one is false
} -body {
  expr {5 && 0}
} -result {0}}

###############################################################################

runTest {test exprformat-5.3 {
  R-33950-35991: || produces integer 1 when one is true
} -body {
  expr {0 || 7}
} -result {1}}

###############################################################################

runTest {test exprformat-5.4 {
  R-33950-35991: || produces integer 0 when both false
} -body {
  expr {0 || 0}
} -result {0}}

###############################################################################

runTest {test exprformat-5.5 {
  R-33950-35991: && result is integer not boolean string
} -setup {
} -body {
  set result [expr {10 && 20}]
  if {$result eq "1"} then {
    set result "integer"
  } else {
    set result "other"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {integer}}

###############################################################################

runTest {test exprformat-5.6 {
  R-33950-35991: || result is integer not boolean string
} -setup {
} -body {
  set result [expr {0 || 0}]
  if {$result eq "0"} then {
    set result "integer"
  } else {
    set result "other"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {integer}}

###############################################################################
#
# Section 6 -- expr: Any nonzero integer is true (R-14317-25004)
#
###############################################################################

runTest {test exprformat-6.1 {
  R-14317-25004: nonzero integer 42 is true in ternary
} -body {
  expr {42 ? "yes" : "no"}
} -result {yes}}

###############################################################################

runTest {test exprformat-6.2 {
  R-14317-25004: negative integer -1 is true in ternary
} -body {
  expr {-1 ? "yes" : "no"}
} -result {yes}}

###############################################################################

runTest {test exprformat-6.3 {
  R-14317-25004: zero is false in ternary
} -body {
  expr {0 ? "yes" : "no"}
} -result {no}}

###############################################################################

runTest {test exprformat-6.4 {
  R-14317-25004: large nonzero integer is true in ternary
} -body {
  expr {999999 ? "yes" : "no"}
} -result {yes}}

###############################################################################
#
# Section 7 -- format: Left-justify flag - (R-30604-44538)
#
###############################################################################

runTest {test exprformat-7.1 {
  R-30604-44538: format left-justify flag with %s
} -body {
  format {%-10s} hello
} -result {hello     }}

###############################################################################

runTest {test exprformat-7.2 {
  R-30604-44538: format left-justify flag with %d
} -body {
  format {%-10d.} 42
} -result {42        .}}

###############################################################################
#
# Section 8 -- format: Sign flag + (R-57537-52532)
#
###############################################################################

runTest {test exprformat-8.1 {
  R-57537-52532: format + sign flag positive
} -body {
  format {%+d} 42
} -result {+42}}

###############################################################################

runTest {test exprformat-8.2 {
  R-57537-52532: format + sign flag negative
} -body {
  format {%+d} -42
} -result {-42}}

###############################################################################

runTest {test exprformat-8.3 {
  R-57537-52532: format + sign flag zero
} -body {
  format {%+d} 0
} -result {+0}}

###############################################################################
#
# Section 9 -- format: Zero-padding flag 0 (R-56711-61056)
#
###############################################################################

runTest {test exprformat-9.1 {
  R-56711-61056: format zero-padding flag
} -body {
  format {%05d} 42
} -result {00042}}

###############################################################################

runTest {test exprformat-9.2 {
  R-56711-61056: format zero-padding with larger value
} -body {
  format {%08d} 12345
} -result {00012345}}

###############################################################################

runTest {test exprformat-9.3 {
  R-56711-61056: format zero-padding negative value
} -body {
  format {%06d} -7
} -result {-00007}}

###############################################################################
#
# Section 10 -- format: Alternate form flag # with %o (R-03770-38577)
#
###############################################################################

runTest {test exprformat-10.1 {
  R-03770-38577: format # flag with %o adds leading 0
} -body {
  format {%#o} 8
} -result {010}}

###############################################################################

runTest {test exprformat-10.2 {
  R-03770-38577: format # flag with %o zero value
} -body {
  format {%#o} 0
} -result {0}}

###############################################################################
#
# Section 11 -- format: Alternate form flag # with %x/%X (R-14169-30646)
#
###############################################################################

runTest {test exprformat-11.1 {
  R-14169-30646: format # flag with %x adds 0x prefix
} -body {
  format {%#x} 255
} -result {0xff}}

###############################################################################

runTest {test exprformat-11.2 {
  R-14169-30646: format # flag with %X adds 0X prefix
} -body {
  format {%#X} 255
} -result {0XFF}}

###############################################################################

runTest {test exprformat-11.3 {
  R-14169-30646: format # flag with %x zero value
} -body {
  format {%#x} 0
} -result {0x0}}

###############################################################################
#
# Section 12 -- format: Width from argument with * (R-61231-38333)
#
###############################################################################

runTest {test exprformat-12.1 {
  R-61231-38333: format * takes width from argument
} -body {
  format {%*d} 5 42
} -result {   42}}

###############################################################################

runTest {test exprformat-12.2 {
  R-61231-38333: format * with string conversion
} -body {
  format {%*s} 10 hello
} -result {     hello}}

###############################################################################

runTest {test exprformat-12.3 {
  R-61231-38333: format * with negative width left-justifies
} -body {
  format {%*d.} -10 42
} -result {42        .}}

###############################################################################
#
# Section 13 -- format: Insufficient arguments is error (R-46212-64941)
#
###############################################################################

runTest {test exprformat-13.1 {
  R-46212-64941: format insufficient arguments is error
} -setup {
} -body {
  list [catch {format {%d %d} 1} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test exprformat-13.2 {
  R-46212-64941: format with no value arguments is error
} -setup {
} -body {
  list [catch {format {%d}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test exprformat-13.3 {
  R-46212-64941: format with no arguments at all is error
} -setup {
} -body {
  list [catch {format} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 14 -- eval: Concatenates arguments like concat (R-27941-12603)
#
###############################################################################

runTest {test exprformat-14.1 {
  R-27941-12603: eval concatenates args like concat
} -setup {
} -body {
  eval set x {hello world}
  set x
} -cleanup {
  unset -nocomplain x
} -returnCodes 1 -match glob -result {wrong # args: *}}

###############################################################################

runTest {test exprformat-14.2 {
  R-27941-12603: eval multi-arg forms valid command
} -setup {
} -body {
  set result [eval expr {1 + 2}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {3}}

###############################################################################

runTest {test exprformat-14.3 {
  R-27941-12603: eval concatenation with list
} -setup {
} -body {
  eval [list set x "hello world"]
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello world}}

###############################################################################

source tests/epilogue.tcl
