###############################################################################
#
# append.tcl --
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
# Section 1 -- append: basic append to existing variable
#
###############################################################################

runTest {test append-1.1 {
  R-06409-32483: append to existing variable
} -setup {
} -body {
  set x "hello"
  append x " world"
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello world}}

###############################################################################

runTest {test append-1.2 {
  R-39847-37686: append returns the new value
} -setup {
} -body {
  set x "hello"
  append x " world"
} -cleanup {
  unset -nocomplain x
} -result {hello world}}

###############################################################################

runTest {test append-1.3 {
  R-2300-0103: append multiple times
} -setup {
} -body {
  set x "a"
  append x "b"
  append x "c"
  set x
} -cleanup {
  unset -nocomplain x
} -result {abc}}

###############################################################################
#
# Section 2 -- append: create new variable
#
###############################################################################

runTest {test append-2.1 {
  R-29122-45410: append creates new variable if not exists
} -setup {
} -body {
  append newvar "hello"
  set newvar
} -cleanup {
  unset -nocomplain newvar
} -result {hello}}

###############################################################################

runTest {test append-2.2 {
  R-2300-0202: append creates new var, starts from empty string
} -setup {
} -body {
  append newvar "abc"
  append newvar "def"
  set newvar
} -cleanup {
  unset -nocomplain newvar
} -result {abcdef}}

###############################################################################
#
# Section 3 -- append: multiple values in one call
#
###############################################################################

runTest {test append-3.1 {
  R-2300-0301: append multiple values at once
} -setup {
} -body {
  set x "start"
  append x "A" "B" "C"
  set x
} -cleanup {
  unset -nocomplain x
} -result {startABC}}

###############################################################################

runTest {test append-3.2 {
  R-2300-0302: append multiple values to new variable
} -setup {
} -body {
  append x "one" "two" "three"
  set x
} -cleanup {
  unset -nocomplain x
} -result {onetwothree}}

###############################################################################

runTest {test append-3.3 {
  R-2300-0303: append with single value arg
} -setup {
} -body {
  set x ""
  append x "only"
  set x
} -cleanup {
  unset -nocomplain x
} -result {only}}

###############################################################################
#
# Section 4 -- append: empty append
#
###############################################################################

runTest {test append-4.1 {
  R-2300-0401: append empty string to existing
} -setup {
} -body {
  set x "hello"
  append x ""
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test append-4.2 {
  R-41306-09002: append with no value args
} -setup {
} -body {
  set x "hello"
  append x
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test append-4.3 {
  R-13329-33713: append with no values on nonexistent var is error
} -setup {
} -body {
  list [catch {append newvar} msg] [string match "*no such variable*" $msg]
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain newvar
} -result {1 1}}

###############################################################################
#
# Section 5 -- append: special values
#
###############################################################################

runTest {test append-5.1 {
  R-2300-0501: append with spaces
} -setup {
} -body {
  set x "hello"
  append x " " "world"
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello world}}

###############################################################################

runTest {test append-5.2 {
  R-2300-0502: append with special characters
} -setup {
} -body {
  set x "line1"
  append x "\n" "line2"
  set x
} -cleanup {
  unset -nocomplain x
} -result "line1\nline2"}

###############################################################################
#
# Section 6 -- append: error cases
#
###############################################################################

runTest {test append-6.1 {
  R-2300-0601: append with no args is error
} -setup {
} -body {
  list [catch {append} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test append-6.2 {
  R-06409-32483: append with multiple value arguments in one call
} -setup {
} -body {
  set x "base"
  append x "-" "mid" "-" "end"
  set x
} -cleanup {
  unset -nocomplain x
} -result {base-mid-end}}

###############################################################################

runTest {test append-6.3 {
  R-29122-45410: append creates variable when it does not exist
} -setup {
} -body {
  append brandnew "created"
  list [info exists brandnew] [set brandnew]
} -cleanup {
  unset -nocomplain brandnew
} -result {1 created}}

###############################################################################

runTest {test append-6.4 {
  R-39847-37686: append on existing variable returns full new value
} -setup {
} -body {
  set x "foo"
  set result [append x "bar"]
  list $result $x [expr {$result eq $x}]
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {foobar foobar 1}}

###############################################################################

runTest {test append-6.5 {
  R-06409-32483: append with empty string appends nothing
} -setup {
} -body {
  set x "original"
  append x ""
  append x "" ""
  set x
} -cleanup {
  unset -nocomplain x
} -result {original}}

###############################################################################

runTest {test append-6.6 {
  R-06409-32483: append preserves existing content correctly
} -setup {
} -body {
  set x "Hello"
  append x ", "
  append x "World"
  append x "!"
  set x
} -cleanup {
  unset -nocomplain x
} -result {Hello, World!}}

###############################################################################

source tests/epilogue.tcl
