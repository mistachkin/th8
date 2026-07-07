###############################################################################
#
# lreplace.tcl --
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
# Section 4 -- lreplace: Replace elements in a list
#
###############################################################################

runTest {test lreplace-4.1 {
  R-07529-35170: lreplace middle element
} -setup {
} -body {
  set mylist {a b c d e}
  lreplace $mylist 2 2 X
} -cleanup {
  unset -nocomplain mylist
} -result {a b X d e}}

###############################################################################

runTest {test lreplace-4.2 {
  R-07529-35170: lreplace range of elements
} -setup {
} -body {
  set mylist {a b c d e}
  lreplace $mylist 1 3 X Y
} -cleanup {
  unset -nocomplain mylist
} -result {a X Y e}}

###############################################################################

runTest {test lreplace-4.3 {
  R-07529-35170: lreplace first element
} -setup {
} -body {
  set mylist {a b c d e}
  lreplace $mylist 0 0 Z
} -cleanup {
  unset -nocomplain mylist
} -result {Z b c d e}}

###############################################################################

runTest {test lreplace-4.4 {
  R-07529-35170: lreplace last element
} -setup {
} -body {
  set mylist {a b c d e}
  lreplace $mylist end end Z
} -cleanup {
  unset -nocomplain mylist
} -result {a b c d Z}}

###############################################################################

runTest {test lreplace-4.5 {
  R-49118-09608: lreplace delete elements (no replacement)
} -setup {
} -body {
  set mylist {a b c d e}
  lreplace $mylist 1 3
} -cleanup {
  unset -nocomplain mylist
} -result {a e}}

###############################################################################

runTest {test lreplace-4.6 {
  R-03155-13217: lreplace insert before first (prepend)
} -setup {
} -body {
  set mylist {a b c}
  lreplace $mylist 0 -1 X Y
} -cleanup {
  unset -nocomplain mylist
} -result {X Y a b c}}

###############################################################################

runTest {test lreplace-4.7 {
  R-03155-13217: lreplace insert at end (append)
} -constraints {
    lreplaceAppend
} -setup {
} -body {
  set mylist {a b c}
  lreplace $mylist 3 3 X Y
} -cleanup {
  unset -nocomplain mylist
} -result {a b c X Y}}

###############################################################################

runTest {test lreplace-4.8 {
  R-07529-35170: lreplace replace all elements
} -setup {
} -body {
  set mylist {a b c}
  lreplace $mylist 0 end X
} -cleanup {
  unset -nocomplain mylist
} -result {X}}

###############################################################################

runTest {test lreplace-4.9 {
  R-07529-35170: lreplace with end-N index
} -setup {
} -body {
  set mylist {a b c d e}
  lreplace $mylist end-1 end Z
} -cleanup {
  unset -nocomplain mylist
} -result {a b c Z}}

###############################################################################

runTest {test lreplace-4.10 {
  R-07529-35170: lreplace replace with more elements than removed
} -setup {
} -body {
  set mylist {a b c}
  lreplace $mylist 1 1 X Y Z
} -cleanup {
  unset -nocomplain mylist
} -result {a X Y Z c}}

###############################################################################

runTest {test lreplace-4.11 {
  R-07529-35170: lreplace single element list
} -body {
  lreplace {a} 0 0 X
} -result {X}}

###############################################################################

runTest {test lreplace-4.err.1 {
  R-07529-35170: lreplace wrong # args
} -setup {
} -body {
  list [catch {lreplace {a b c}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
