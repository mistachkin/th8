###############################################################################
#
# lindex.tcl --
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
# Section 2 -- lindex: Extract element by index from a list
#
###############################################################################

runTest {test lindex-2.1 {
  R-37667-43203: lindex first element
} -setup {
} -body {
  set mylist {a b c d e}
  lindex $mylist 0
} -cleanup {
  unset -nocomplain mylist
} -result {a}}

###############################################################################

runTest {test lindex-2.2 {
  R-62854-48784: lindex middle element
} -setup {
} -body {
  set mylist {a b c d e}
  lindex $mylist 2
} -cleanup {
  unset -nocomplain mylist
} -result {c}}

###############################################################################

runTest {test lindex-2.3 {
  R-62854-48784: lindex last element with numeric index
} -setup {
} -body {
  set mylist {a b c d e}
  lindex $mylist 4
} -cleanup {
  unset -nocomplain mylist
} -result {e}}

###############################################################################

runTest {test lindex-2.4 {
  R-57862-26416: lindex end
} -setup {
} -body {
  set mylist {a b c d e}
  lindex $mylist end
} -cleanup {
  unset -nocomplain mylist
} -result {e}}

###############################################################################

runTest {test lindex-2.5 {
  R-07541-34977: lindex end-1
} -setup {
} -body {
  set mylist {a b c d e}
  lindex $mylist end-1
} -cleanup {
  unset -nocomplain mylist
} -result {d}}

###############################################################################

runTest {test lindex-2.6 {
  R-07541-34977: lindex end-N for second element
} -setup {
} -body {
  set mylist {a b c d e}
  lindex $mylist end-3
} -cleanup {
  unset -nocomplain mylist
} -result {b}}

###############################################################################

runTest {test lindex-2.7 {
  R-13187-19003: lindex out of range (positive)
} -setup {
} -body {
  set mylist {a b c}
  lindex $mylist 10
} -cleanup {
  unset -nocomplain mylist
} -result {}}

###############################################################################

runTest {test lindex-2.8 {
  R-13187-19003: lindex negative index
} -setup {
} -body {
  set mylist {a b c}
  lindex $mylist -1
} -cleanup {
  unset -nocomplain mylist
} -result {}}

###############################################################################

runTest {test lindex-2.9 {
  R-13187-19003: lindex on empty list
} -body {
  lindex {} 0
} -result {}}

###############################################################################

runTest {test lindex-2.10 {
  R-37667-43203: lindex single element list
} -body {
  lindex {hello} 0
} -result {hello}}

###############################################################################

runTest {test lindex-2.11 {
  R-62854-48784: lindex element with spaces
} -setup {
} -body {
  set mylist [list "hello world" foo bar]
  lindex $mylist 0
} -cleanup {
  unset -nocomplain mylist
} -result {hello world}}

###############################################################################

runTest {test lindex-2.12 {
  R-62854-48784: lindex nested list element
} -setup {
} -body {
  set mylist {a {b c d} e}
  lindex $mylist 1
} -cleanup {
  unset -nocomplain mylist
} -result {b c d}}

###############################################################################

runTest {test lindex-2.err.1 {
  R-62854-48784: lindex wrong # args (zero)
} -setup {
} -body {
  list [catch {lindex} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 3 -- lindex: nested list indexing
#
###############################################################################

runTest {test lindex-3.1 {
  R-43980-53018: nested lindex with two indices
} -body {
  lindex {a {b c d} e} 1 1
} -result {c}}

###############################################################################

runTest {test lindex-3.2 {
  R-43980-53018: nested lindex with three indices
} -body {
  lindex {{a {b c}} {d {e f}}} 1 1 0
} -result {e}}

###############################################################################

runTest {test lindex-3.3 {
  R-43980-53018: nested lindex out of range returns empty
} -body {
  lindex {a {b c} d} 1 5
} -result {}}

###############################################################################

source tests/epilogue.tcl
