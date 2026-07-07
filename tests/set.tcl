###############################################################################
#
# set.tcl --
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
# Section 10 -- set: Assign and query variable values
#
###############################################################################

runTest {test set-10.1 {
  R-09798-25882: set with two args assigns value
} -setup {
} -body {
  set result [set x hello]
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test set-10.2 {
  R-62364-23287: set with two args returns the assigned value
} -setup {
} -body {
  set x 42
} -cleanup {
  unset -nocomplain x
} -result {42}}

###############################################################################

runTest {test set-10.3 {
  R-06168-42171: set with one arg returns current value
} -setup {
  set x "query test"
} -body {
  set x
} -cleanup {
  unset -nocomplain x
} -result {query test}}

###############################################################################

runTest {test set-10.4 {
  R-26988-54227: set with one arg on nonexistent var produces error
} -setup {
} -body {
  catch {set nosuchvar} msg
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain nosuchvar
} -result {1}}

###############################################################################

runTest {test set-10.5 {
  R-47069-65353: set overwrites existing value
} -setup {
} -body {
  set x first
  set x second
} -cleanup {
  unset -nocomplain x
} -result {second}}

###############################################################################

runTest {test set-10.6 {
  R-37037-25374: set with empty string value assigns empty string
} -setup {
} -body {
  set x ""
} -cleanup {
  unset -nocomplain x
} -result {}}

###############################################################################

runTest {test set-10.7 {
  R-09798-25882: set with special characters assigns value
} -setup {
} -body {
  set x "hello\tworld\n"
} -cleanup {
  unset -nocomplain x
} -result "hello\tworld\n"}

###############################################################################

runTest {test set-10.8 {
  R-56696-36447: set with varName(key) accesses array element
} -setup {
} -body {
  set arr(key) value
} -cleanup {
  unset -nocomplain arr
} -result {value}}

###############################################################################

runTest {test set-10.9 {
  R-56696-36447: set with varName(key) queries array element
} -setup {
  set arr(x) 42
} -body {
  set arr(x)
} -cleanup {
  unset -nocomplain arr
} -result {42}}

###############################################################################

runTest {test set-10.err.1 {
  R-28379-13828: set with zero arguments produces wrong # args error
} -setup {
} -body {
  list [catch {set} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test set-10.10 {
  R-09798-25882: set within proc creates local variable not global
} -setup {
} -body {
  proc mySetProc {} {
    set localtest "local value"
    return $localtest
  }
  set result [mySetProc]
  list $result [info exists ::localtest]
} -cleanup {
  unset -nocomplain ::localtest
  unset -nocomplain result
  catch {rename mySetProc ""}
} -result {{local value} 0}}

###############################################################################

runTest {test set-10.11 {
  R-09798-25882: set with namespace-qualified name accesses global
} -setup {
} -body {
  proc mySetProc2 {} {
    set ::globalvar "from proc"
  }
  mySetProc2
  set ::globalvar
} -cleanup {
  unset -nocomplain ::globalvar
  catch {rename mySetProc2 ""}
} -result {from proc}}

###############################################################################

runTest {test set-10.12 {
  R-56696-36447: set creates a new array element
} -setup {
} -body {
  set arr(first) "one"
  set arr(second) "two"
  list [set arr(first)] [set arr(second)]
} -cleanup {
  unset -nocomplain arr
} -result {one two}}

###############################################################################

runTest {test set-10.13 {
  R-62364-23287: set returns value for array elements
} -setup {
} -body {
  set result [set arr(key) "the value"]
} -cleanup {
  unset -nocomplain arr
  unset -nocomplain result
} -result {the value}}

###############################################################################

runTest {test set-10.14 {
  R-09798-25882: set with very long string value
} -setup {
} -body {
  set x [string repeat "abcdefghij" 1000]
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {10000}}

###############################################################################

source tests/epilogue.tcl
