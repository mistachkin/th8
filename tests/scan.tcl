###############################################################################
#
# scan.tcl --
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
# Section 1 -- scan: %d (decimal integer)
#
###############################################################################

runTest {test scan-1.1 {
  R-56814-07430: scan %d basic integer
} -setup {
} -body {
  scan "42" "%d" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {42}}

###############################################################################

runTest {test scan-1.2 {
  R-2700-0102: scan %d negative integer
} -setup {
} -body {
  scan "-7" "%d" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {-7}}

###############################################################################

runTest {test scan-1.3 {
  R-03334-00938: scan %d returns count of conversions
} -setup {
} -body {
  scan "42" "%d" x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {1}}

###############################################################################
#
# Section 2 -- scan: %s (string)
#
###############################################################################

runTest {test scan-2.1 {
  R-38513-56453: scan %s basic string
} -setup {
} -body {
  scan "hello" "%s" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {hello}}

###############################################################################

runTest {test scan-2.2 {
  R-2700-0202: scan %s stops at whitespace
} -setup {
} -body {
  scan "hello world" "%s" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {hello}}

###############################################################################

runTest {test scan-2.3 {
  R-2700-0203: scan multiple %s
} -setup {
} -body {
  scan "hello world" "%s %s" a b
  list $a $b
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
} -constraints {scan} -result {hello world}}

###############################################################################
#
# Section 3 -- scan: %x (hexadecimal)
#
###############################################################################

runTest {test scan-3.1 {
  R-2700-0301: scan %x hexadecimal
} -setup {
} -body {
  scan "ff" "%x" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {255}}

###############################################################################

runTest {test scan-3.2 {
  R-47387-33797: scan %x with 0x prefix
} -setup {
} -body {
  scan "0xff" "%x" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {255}}

###############################################################################
#
# Section 4 -- scan: %o (octal)
#
###############################################################################

runTest {test scan-4.1 {
  R-33929-04283: scan %o octal
} -setup {
} -body {
  scan "10" "%o" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {8}}

###############################################################################
#
# Section 5 -- scan: %c (character)
#
###############################################################################

runTest {test scan-5.1 {
  R-30567-08335: scan %c character code
} -setup {
} -body {
  scan "A" "%c" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {65}}

###############################################################################
#
# Section 6 -- scan: %f (floating point)
#
###############################################################################

runTest {test scan-6.1 {
  R-17663-64974: scan %f floating point
} -setup {
} -body {
  scan "3.14" "%f" x
  format "%.2f" $x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {3.14}}

###############################################################################
#
# Section 7 -- scan: %n (count of characters consumed)
#
###############################################################################

runTest {test scan-7.1 {
  R-27160-02205: scan %n characters consumed
} -setup {
} -body {
  scan "hello world" "%s%n" x n
  set n
} -cleanup {
  unset -nocomplain x
  unset -nocomplain n
} -constraints {scan} -result {5}}

###############################################################################
#
# Section 8 -- scan: list mode (no variable args)
#
###############################################################################

runTest {test scan-8.1 {
  R-27422-38271: scan in list mode returns list
} -body {
  scan "42 hello" "%d %s"
} -constraints {scan} -result {42 hello}}

###############################################################################

runTest {test scan-8.2 {
  R-2700-0802: scan list mode multiple integers
} -body {
  scan "1 2 3" "%d %d %d"
} -constraints {scan} -result {1 2 3}}

###############################################################################

runTest {test scan-8.3 {
  R-2700-0803: scan list mode with hex
} -body {
  scan "ff" "%x"
} -constraints {scan} -result {255}}

###############################################################################
#
# Section 9 -- scan: mixed conversions
#
###############################################################################

runTest {test scan-9.1 {
  R-2700-0901: scan mixed integer and string
} -setup {
} -body {
  scan "42 hello" "%d %s" a b
  list $a $b
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
} -constraints {scan} -result {42 hello}}

###############################################################################

runTest {test scan-9.2 {
  R-2700-0902: scan returns number of conversions
} -setup {
} -body {
  scan "42 hello" "%d %s" a b
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
} -constraints {scan} -result {2}}

###############################################################################
#
# Section 10 -- scan: error cases
#
###############################################################################

runTest {test scan-10.1 {
  R-2700-1001: scan with no args is error
} -setup {
} -body {
  list [catch {scan} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -constraints {scan} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test scan-10.2 {
  R-2700-1002: scan with insufficient args is error
} -setup {
} -body {
  list [catch {scan "hello"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -constraints {scan} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
