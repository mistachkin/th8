###############################################################################
#
# lassign.tcl --
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
# Section 1 -- lassign: Assign list elements to variables
#
###############################################################################

runTest {test lassign-1.1 {
  R-35860-32063: basic assignment
} -setup {
} -body {
  lassign {x y z} a b c
  list $a $b $c
} -cleanup {
  unset -nocomplain a b c
} -result {x y z}}

###############################################################################

runTest {test lassign-1.2 {
  R-35860-32063: more vars than elements (extras get empty string)
} -setup {
} -body {
  lassign {x y} a b c d
  list $a $b $c $d
} -cleanup {
  unset -nocomplain a b c d
} -result {x y {} {}}}

###############################################################################

runTest {test lassign-1.3 {
  R-48926-20099: more elements than vars (return unassigned)
} -setup {
} -body {
  lassign {x y z w} a b
} -cleanup {
  unset -nocomplain a b
} -result {z w}}

###############################################################################

runTest {test lassign-1.4 {
  R-61728-16131: no variables (return whole list)
} -constraints {not_eagle} -body {
  lassign {x y z}
} -result {x y z}}

###############################################################################

runTest {test lassign-1.5 {
  R-05534-35124: empty list
} -setup {
} -body {
  lassign {} a b
  list $a $b
} -cleanup {
  unset -nocomplain a b
} -result {{} {}}}

###############################################################################

runTest {test lassign-2.1 {
  R-35860-32063: wrong # args (no args)
} -setup {
} -body {
  list [catch {lassign} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test lassign-3.1 {
  R-35860-32063: single variable
} -setup {
} -body {
  lassign {x y z} a
} -cleanup {
  unset -nocomplain a
} -result {y z}}

###############################################################################

runTest {test lassign-3.2 {
  R-48926-20099: all elements assigned, nothing left
} -setup {
} -body {
  lassign {x y z} a b c
} -cleanup {
  unset -nocomplain a b c
} -result {}}

###############################################################################

runTest {test lassign-3.3 {
  R-18329-05882: nested list elements
} -setup {
} -body {
  lassign {{1 2} {3 4}} a b
  list $a $b
} -cleanup {
  unset -nocomplain a b
} -result {{1 2} {3 4}}}

###############################################################################

runTest {test lassign-3.4 {
  R-35860-32063: empty string elements
} -setup {
} -body {
  lassign [list "" x ""] a b c
  list $a $b $c
} -cleanup {
  unset -nocomplain a b c
} -result {{} x {}}}

###############################################################################
#
# Section 4 -- R-marker coverage: remaining variables set to empty
#
###############################################################################

runTest {test lassign-4.1 {
  R-54553-37675: lassign sets remaining variables to empty if list is short
} -setup {
} -body {
  lassign {alpha beta} a b c d e
  list $a $b $c $d $e
} -cleanup {
  unset -nocomplain a b c d e
} -result {alpha beta {} {} {}}}

###############################################################################

source tests/epilogue.tcl
