###############################################################################
#
# expansion.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the {*} expansion operator and the generalized
# {tag} expansion mechanism.
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
# Section 1 -- {*} basic expansion
#
###############################################################################

runTest {test expansion-1.1 {
  R-12647-27646: {*} splits word as list and each element becomes a separate
                 argument
} -setup {
} -body {
  set result [list {*}{a b c}]
} -cleanup {
  unset -nocomplain result
} -result {a b c}}

###############################################################################

runTest {test expansion-1.2 {
  R-12647-27646: {*} with variable expansion
} -setup {
} -body {
  set args {hello world}
  set result [list {*}$args]
} -cleanup {
  unset -nocomplain args result
} -result {hello world}}

###############################################################################

runTest {test expansion-1.3 {
  R-12647-27646: multiple {*} expansions in same command
} -setup {
} -body {
  set a {1 2}
  set b {3 4}
  set result [list {*}$a {*}$b]
} -cleanup {
  unset -nocomplain a b result
} -result {1 2 3 4}}

###############################################################################

runTest {test expansion-1.4 {
  R-49538-15893: expansion of empty value produces zero arguments
} -setup {
} -body {
  set result [list {*}{}]
} -cleanup {
  unset -nocomplain result
} -result {}}

###############################################################################

runTest {test expansion-1.5 {
  R-12647-27646: mixed regular and expanded arguments
} -setup {
} -body {
  set result [list before {*}{x y} after]
} -cleanup {
  unset -nocomplain result
} -result {before x y after}}

###############################################################################

runTest {test expansion-1.6 {
  standalone {*} is a literal string (backward compatibility)
} -setup {
} -body {
  set result {*}
} -cleanup {
  unset -nocomplain result
} -result {*}}

###############################################################################

runTest {test expansion-1.7 {
  R-12647-27646: {*} with command substitution
} -setup {
} -body {
  set result [list {*}[list a b c]]
} -cleanup {
  unset -nocomplain result
} -result {a b c}}

###############################################################################

runTest {test expansion-1.8 {
  R-12647-27646: {*} with single-element list
} -setup {
} -body {
  set result [list {*}{hello}]
} -cleanup {
  unset -nocomplain result
} -result {hello}}

###############################################################################
#
# Section 2 -- unknown expansion tags
#
###############################################################################

runTest {test expansion-2.1 {
  R-53729-50924: unregistered tag produces parse error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {list {json}hello} msg] \
      [string match "*unknown expansion operator*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test expansion-2.2 {
  R-53729-50924: error message includes tag name
} -constraints {
    th8
} -setup {
} -body {
  catch {list {foobar}hello} msg
  string match "*foobar*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 3 -- custom expansion operators (via testlib)
#
###############################################################################

runTest {test expansion-3.1 {
  R-50966-37458: register custom "reverse" expansion operator
} -constraints {
    loadLib th8
} -body {
  th8testlib::expansion register reverse
  set result [list {reverse}{a b c d}]
  th8testlib::expansion unregister reverse
  set result
} -cleanup {
  unset -nocomplain result
} -result {d c b a}}

###############################################################################

runTest {test expansion-3.2 {
  R-50966-37458: register custom "upper" expansion operator
} -constraints {
    loadLib th8
} -body {
  th8testlib::expansion register upper
  set result [list {upper}{hello world}]
  th8testlib::expansion unregister upper
  set result
} -cleanup {
  unset -nocomplain result
} -result {HELLO WORLD}}

###############################################################################

runTest {test expansion-3.3 {
  R-50966-37458: custom expansion with variable
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::expansion register reverse
  set data {x y z}
  set result [list {reverse}$data]
  th8testlib::expansion unregister reverse
  set result
} -cleanup {
  unset -nocomplain data result
} -result {z y x}}

###############################################################################

runTest {test expansion-3.4 {
  R-50966-37458: unregistered custom tag returns to error
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::expansion register reverse
  th8testlib::expansion unregister reverse
  list [catch {list {reverse}{a b}} msg] \
      [string match "*unknown expansion*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test expansion-3.5 {
  R-50966-37458: custom expansion producing zero elements
} -constraints {
    loadLib th8
} -body {
  th8testlib::expansion register reverse
  set result [list before {reverse}{} after]
  th8testlib::expansion unregister reverse
  set result
} -cleanup {
  unset -nocomplain result
} -result {before after}}

###############################################################################

runTest {test expansion-3.6 {
  expansion operator wrong # args
} -constraints {
    loadLib th8
} -setup {
} -body {
  list [catch {th8testlib::expansion} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
