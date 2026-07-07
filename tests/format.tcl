###############################################################################
#
# format.tcl --
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
# Section 1 -- format: %d (decimal integer)
#
###############################################################################

runTest {test format-1.1 {
  R-55206-47918: format %d basic
} -body {
  format "%d" 42
} -result {42}}

###############################################################################

runTest {test format-1.2 {
  R-02380-17814: format %d negative
} -body {
  format "%d" -7
} -result {-7}}

###############################################################################

runTest {test format-1.3 {
  R-16079-32511: format %d zero
} -body {
  format "%d" 0
} -result {0}}

###############################################################################
#
# Section 2 -- format: %s (string)
#
###############################################################################

runTest {test format-2.1 {
  R-58954-60169: format %s basic
} -body {
  format "%s" "hello"
} -result {hello}}

###############################################################################

runTest {test format-2.2 {
  R-2600-0202: format %s with other text
} -body {
  format "Hello, %s!" "world"
} -result {Hello, world!}}

###############################################################################

runTest {test format-2.3 {
  R-2600-0203: format multiple %s
} -body {
  format "%s and %s" "alpha" "beta"
} -result {alpha and beta}}

###############################################################################
#
# Section 3 -- format: %x (hexadecimal)
#
###############################################################################

runTest {test format-3.1 {
  R-29851-05339: format %x basic
} -body {
  format "%x" 255
} -result {ff}}

###############################################################################

runTest {test format-3.2 {
  R-2600-0302: format %x zero
} -body {
  format "%x" 0
} -result {0}}

###############################################################################

runTest {test format-3.3 {
  R-2600-0303: format %X uppercase
} -body {
  format "%X" 255
} -result {FF}}

###############################################################################
#
# Section 4 -- format: %o (octal)
#
###############################################################################

runTest {test format-4.1 {
  R-63435-44549: format %o basic
} -body {
  format "%o" 8
} -result {10}}

###############################################################################

runTest {test format-4.2 {
  R-2600-0402: format %o zero
} -body {
  format "%o" 0
} -result {0}}

###############################################################################
#
# Section 5 -- format: %c (character)
#
###############################################################################

runTest {test format-5.1 {
  R-06602-58148: format %c ASCII character
} -body {
  format "%c" 65
} -result {A}}

###############################################################################

runTest {test format-5.2 {
  R-2600-0502: format %c space character
} -body {
  format "%c" 32
} -result { }}

###############################################################################
#
# Section 6 -- format: %f (floating point)
#
###############################################################################

runTest {test format-6.1 {
  R-54170-43783: format %f basic
} -body {
  format "%f" 3.14
} -match regexp -result {^3\.140*$}}

###############################################################################

runTest {test format-6.2 {
  R-2600-0602: format %f integer value
} -body {
  format "%f" 5
} -match regexp -result {^5\.0+$}}

###############################################################################
#
# Section 7 -- format: %e (scientific notation)
#
###############################################################################

runTest {test format-7.1 {
  R-20004-22166: format %e scientific notation
} -body {
  format "%e" 100000.0
} -match regexp -result {^1\.0+e\+0*5$}}

###############################################################################
#
# Section 8 -- format: %g (general floating point)
#
###############################################################################

runTest {test format-8.1 {
  R-51311-13182: format %g general floating point
} -body {
  format "%g" 3.14
} -result {3.14}}

###############################################################################

runTest {test format-8.2 {
  R-2600-0802: format %g integer value
} -body {
  format "%g" 100.0
} -result {100}}

###############################################################################
#
# Section 9 -- format: width and precision
#
###############################################################################

runTest {test format-9.1 {
  R-2600-0901: format with field width
} -body {
  format "%10d" 42
} -result {        42}}

###############################################################################

runTest {test format-9.2 {
  R-2600-0902: format with left justify
} -body {
  format "%-10d." 42
} -result {42        .}}

###############################################################################

runTest {test format-9.3 {
  R-2600-0903: format with zero padding
} -body {
  format "%05d" 42
} -result {00042}}

###############################################################################

runTest {test format-9.4 {
  R-2600-0904: format string with precision
} -body {
  format "%.3s" "hello"
} -result {hel}}

###############################################################################

runTest {test format-9.5 {
  R-51483-24977: format float precision
} -body {
  format "%.2f" 3.14159
} -result {3.14}}

###############################################################################
#
# Section 10 -- format: %% literal percent
#
###############################################################################

runTest {test format-10.1 {
  R-08455-55100: format %% produces literal percent
} -body {
  format "100%%"
} -result {100%}}

###############################################################################

runTest {test format-10.2 {
  R-2600-1002: format %% with other conversions
} -body {
  format "%d%%" 50
} -result {50%}}

###############################################################################
#
# Section 11 -- format: error cases
#
###############################################################################

runTest {test format-11.1 {
  R-2600-1101: format with no args is error
} -setup {
} -body {
  list [catch {format} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test format-11.2 {
  R-2600-1102: format with too few values is error
} -setup {
} -body {
  list [catch {format "%d %d" 1} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 12 -- format: field width and justification
#
###############################################################################

runTest {test format-12.1 {
  R-55206-47918: format with field width right-justified
} -body {
  format "%10d" 42
} -result {        42}}

###############################################################################

runTest {test format-12.2 {
  R-55206-47918: format with left justification
} -body {
  format "%-10d." 42
} -result {42        .}}

###############################################################################

runTest {test format-12.3 {
  R-55206-47918: format with zero-padding
} -body {
  format "%05d" 42
} -result {00042}}

###############################################################################

runTest {test format-12.4 {
  R-55206-47918: format with + flag positive
} -body {
  format "%+d" 42
} -result {+42}}

###############################################################################

runTest {test format-12.5 {
  R-55206-47918: format with + flag negative
} -body {
  format "%+d" -42
} -result {-42}}

###############################################################################

runTest {test format-12.6 {
  R-55206-47918: format with + flag zero
} -body {
  format "%+d" 0
} -result {+0}}

###############################################################################
#
# Section 13 -- format: %e scientific notation additional tests
#
###############################################################################

runTest {test format-13.1 {
  R-20004-22166: format %e with small value
} -body {
  format "%e" 0.001
} -match regexp -result {^1\.0+e-0*3$}}

###############################################################################

runTest {test format-13.2 {
  R-20004-22166: format %e with value 1.0
} -body {
  format "%e" 1.0
} -match regexp -result {^1\.0+e\+0+$}}

###############################################################################

runTest {test format-13.3 {
  R-20004-22166: format %e with precision
} -body {
  format "%.2e" 12345.6789
} -match regexp -result {^1\.23e\+0*4$}}

###############################################################################
#
# Section 14 -- format: %g general floating point additional tests
#
###############################################################################

runTest {test format-14.1 {
  R-51311-13182: format %g uses scientific for very large
} -body {
  format "%g" 1e20
} -match regexp -result {1e\+0*20}}

###############################################################################

runTest {test format-14.2 {
  R-51311-13182: format %g uses scientific for very small
} -body {
  format "%g" 1e-10
} -match regexp -result {1e-0*10}}

###############################################################################

runTest {test format-14.3 {
  R-51311-13182: format %g uses fixed for moderate values
} -body {
  format "%g" 12.5
} -result {12.5}}

###############################################################################

runTest {test format-14.4 {
  R-51311-13182: format %g trims trailing zeros
} -body {
  format "%g" 1.50
} -result {1.5}}

###############################################################################
#
# Section 15 -- format: %s precision truncation
#
###############################################################################

runTest {test format-15.1 {
  R-58954-60169: format %s with precision truncation
} -body {
  format "%.3s" "hello"
} -result {hel}}

###############################################################################

runTest {test format-15.2 {
  R-58954-60169: format %s precision greater than length
} -body {
  format "%.10s" "hi"
} -result {hi}}

###############################################################################

runTest {test format-15.3 {
  R-58954-60169: format %s with width and precision
} -body {
  format "%10.3s" "hello"
} -result {       hel}}

###############################################################################
#
# Section 16 -- format: %x, %X, %o additional tests
#
###############################################################################

runTest {test format-16.1 {
  R-29851-05339: format %x lowercase hex
} -body {
  format "%x" 4660
} -result {1234}}

###############################################################################

runTest {test format-16.2 {
  R-29851-05339: format %x with large value
} -body {
  format "%x" 65535
} -result {ffff}}

###############################################################################

runTest {test format-16.3 {
  R-2600-0303: format %X uppercase hex
} -body {
  format "%X" 4660
} -result {1234}}

###############################################################################

runTest {test format-16.4 {
  R-2600-0303: format %X with large value
} -body {
  format "%X" 65535
} -result {FFFF}}

###############################################################################

runTest {test format-16.5 {
  R-63435-44549: format %o larger value
} -body {
  format "%o" 255
} -result {377}}

###############################################################################

runTest {test format-16.6 {
  R-63435-44549: format %o with width and zero-padding
} -body {
  format "%06o" 8
} -result {000010}}

###############################################################################
#
# Section 17 -- format: %% literal percent additional tests
#
###############################################################################

runTest {test format-17.1 {
  R-08455-55100: format %% standalone
} -body {
  format "%%"
} -result {%}}

###############################################################################

runTest {test format-17.2 {
  R-08455-55100: format multiple %% in format string
} -body {
  format "%d%% of %d%%" 50 100
} -result {50% of 100%}}

###############################################################################
#
# Section 18 -- format: %f precision specifier
#
###############################################################################

runTest {test format-18.1 {
  R-51483-24977: format %f with 0 precision
} -body {
  format "%.0f" 3.7
} -result {4}}

###############################################################################

runTest {test format-18.2 {
  R-51483-24977: format %f with 4 decimal places
} -body {
  format "%.4f" 3.14159
} -result {3.1416}}

###############################################################################

runTest {test format-18.3 {
  R-54170-43783: format %f negative value
} -body {
  format "%f" -2.5
} -match regexp -result {^-2\.50*$}}

###############################################################################

source tests/epilogue.tcl
