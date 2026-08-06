###############################################################################
#
# incr.tcl --
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
# Section 1 -- incr: basic increment by 1
#
###############################################################################

runTest {test incr-1.1 {
  R-09737-12855: default increment is 1
} -setup {
} -body {
  set x 0
  incr x
  set x
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test incr-1.2 {
  R-20499-60888: incr returns new value
} -setup {
} -body {
  set x 5
  incr x
} -cleanup {
  unset -nocomplain x
} -result {6}}

###############################################################################

runTest {test incr-1.3 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x 0
  incr x
  incr x
  incr x
  set x
} -cleanup {
  unset -nocomplain x
} -result {3}}

###############################################################################
#
# Section 2 -- incr: custom increment
#
###############################################################################

runTest {test incr-2.1 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x 10
  incr x 5
  set x
} -cleanup {
  unset -nocomplain x
} -result {15}}

###############################################################################

runTest {test incr-2.2 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x 0
  incr x 1000
  set x
} -cleanup {
  unset -nocomplain x
} -result {1000}}

###############################################################################
#
# Section 3 -- incr: decrement (negative increment)
#
###############################################################################

runTest {test incr-3.1 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x 10
  incr x -3
  set x
} -cleanup {
  unset -nocomplain x
} -result {7}}

###############################################################################

runTest {test incr-3.2 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x 0
  incr x -5
  set x
} -cleanup {
  unset -nocomplain x
} -result {-5}}

###############################################################################

runTest {test incr-3.3 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x 5
  incr x -1
  set x
} -cleanup {
  unset -nocomplain x
} -result {4}}

###############################################################################
#
# Section 4 -- incr: create from zero
#
###############################################################################

runTest {test incr-4.1 {
  R-42497-30621: incr creates variable with initial value 0 if nonexistent
} -constraints {not_eagle} -setup {
} -body {
  incr x
  set x
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test incr-4.2 {
  R-42497-30621: incr creates variable with initial value 0 if nonexistent
} -constraints {not_eagle} -setup {
} -body {
  incr x 42
  set x
} -cleanup {
  unset -nocomplain x
} -result {42}}

###############################################################################
#
# Section 5 -- incr: negative starting values
#
###############################################################################

runTest {test incr-5.1 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x -10
  incr x 3
  set x
} -cleanup {
  unset -nocomplain x
} -result {-7}}

###############################################################################

runTest {test incr-5.2 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x -5
  incr x -5
  set x
} -cleanup {
  unset -nocomplain x
} -result {-10}}

###############################################################################
#
# Section 6 -- incr: error cases
#
###############################################################################

runTest {test incr-6.1 {
  R-57202-38464: error if variable's current value is not integer
} -setup {
} -body {
  set x "abc"
  list [catch {incr x} msg] $msg
} -cleanup {
  unset -nocomplain x
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test incr-6.2 {
  R-59857-46589: error if increment argument is not integer
} -setup {
} -body {
  set x 5
  list [catch {incr x "abc"} msg] $msg
} -cleanup {
  unset -nocomplain x
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test incr-6.3 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  list [catch {incr} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test incr-6.4 {
  R-07001-16527: incr adds integer increment to variable
} -setup {
} -body {
  set x 0
  list [catch {incr x 1 2} msg] $msg
} -cleanup {
  unset -nocomplain x
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
