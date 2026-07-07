###############################################################################
#
# lappend.tcl --
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
# Section 7 -- lappend: Append elements to a list variable
#
###############################################################################

runTest {test lappend-7.1 {
  R-41293-44424: lappend appends element to existing list
} -setup {
  set mylist {a b}
} -body {
  lappend mylist c
} -cleanup {
  unset -nocomplain mylist
} -result {a b c}}

###############################################################################

runTest {test lappend-7.2 {
  R-37687-02473: lappend creates new variable if it does not exist
} -setup {
} -body {
  lappend newvar hello
} -cleanup {
  unset -nocomplain newvar
} -result {hello}}

###############################################################################

runTest {test lappend-7.3 {
  R-41293-44424: lappend appends multiple values at once
} -setup {
  set mylist {a}
} -body {
  lappend mylist b c d
} -cleanup {
  unset -nocomplain mylist
} -result {a b c d}}

###############################################################################

runTest {test lappend-7.4 {
  R-41293-44424: lappend appends to empty list variable
} -setup {
  set mylist {}
} -body {
  lappend mylist x
} -cleanup {
  unset -nocomplain mylist
} -result {x}}

###############################################################################

runTest {test lappend-7.5 {
  R-41293-44424: lappend properly quotes value containing spaces
} -setup {
  set mylist {a}
} -body {
  lappend mylist "hello world"
  llength $mylist
} -cleanup {
  unset -nocomplain mylist
} -result {2}}

###############################################################################

runTest {test lappend-7.6 {
  R-41293-44424: lappend value with spaces is single element
} -setup {
} -body {
  lappend mylist "hello world"
  lindex $mylist 0
} -cleanup {
  unset -nocomplain mylist
} -result {hello world}}

###############################################################################

runTest {test lappend-7.7 {
  R-58254-03308: lappend returns current list value
} -setup {
  set mylist {a b}
} -body {
  set result [lappend mylist c]
  expr {$result eq $mylist}
} -cleanup {
  unset -nocomplain mylist
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test lappend-7.8 {
  R-45401-22079: lappend with no values returns variable value
} -setup {
  set mylist {a b}
} -body {
  lappend mylist
} -cleanup {
  unset -nocomplain mylist
} -result {a b}}

###############################################################################

runTest {test lappend-7.9 {
  R-41293-44424: lappend empty string element increases list length
} -setup {
  set mylist {a}
} -body {
  lappend mylist ""
  llength $mylist
} -cleanup {
  unset -nocomplain mylist
} -result {2}}

###############################################################################

runTest {test lappend-7.10 {
  R-41293-44424: lappend called multiple times accumulates elements
} -setup {
  set mylist {}
} -body {
  lappend mylist a
  lappend mylist b
  lappend mylist c
  set mylist
} -cleanup {
  unset -nocomplain mylist
} -result {a b c}}

###############################################################################

runTest {test lappend-7.11 {
  R-41293-44424: lappend with special characters in value
} -setup {
} -body {
  lappend mylist "a\tb"
  lindex $mylist 0
} -cleanup {
  unset -nocomplain mylist
} -result "a\tb"}

###############################################################################

runTest {test lappend-7.err.1 {
  R-41293-44424: lappend with zero args is error
} -setup {
} -body {
  list [catch {lappend} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
