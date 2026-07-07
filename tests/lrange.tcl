###############################################################################
#
# lrange.tcl --
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
# Section 3 -- lrange: Extract a range of elements from a list
#
###############################################################################

runTest {test lrange-3.1 {
  R-20123-18594: lrange basic range
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 1 3
} -cleanup {
  unset -nocomplain mylist
} -result {b c d}}

###############################################################################

runTest {test lrange-3.2 {
  R-20123-18594: lrange from start
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 0 2
} -cleanup {
  unset -nocomplain mylist
} -result {a b c}}

###############################################################################

runTest {test lrange-3.3 {
  R-14342-04254: lrange to end keyword
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 2 end
} -cleanup {
  unset -nocomplain mylist
} -result {c d e}}

###############################################################################

runTest {test lrange-3.4 {
  R-14342-04254: lrange with end-N
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 1 end-1
} -cleanup {
  unset -nocomplain mylist
} -result {b c d}}

###############################################################################

runTest {test lrange-3.5 {
  R-20123-18594: lrange full range
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 0 end
} -cleanup {
  unset -nocomplain mylist
} -result {a b c d e}}

###############################################################################

runTest {test lrange-3.6 {
  R-43443-37988: lrange empty result (first > last)
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 3 1
} -cleanup {
  unset -nocomplain mylist
} -result {}}

###############################################################################

runTest {test lrange-3.7 {
  R-20123-18594: lrange single element
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 2 2
} -cleanup {
  unset -nocomplain mylist
} -result {c}}

###############################################################################

runTest {test lrange-3.8 {
  R-20123-18594: lrange out of range (last beyond end)
} -setup {
} -body {
  set mylist {a b c}
  lrange $mylist 1 100
} -cleanup {
  unset -nocomplain mylist
} -result {b c}}

###############################################################################

runTest {test lrange-3.9 {
  R-43443-37988: lrange on empty list
} -body {
  lrange {} 0 end
} -result {}}

###############################################################################

runTest {test lrange-3.10 {
  R-20123-18594: lrange negative first index
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist -2 2
} -cleanup {
  unset -nocomplain mylist
} -result {a b c}}

###############################################################################

runTest {test lrange-3.11 {
  R-20123-18594: lrange preserves list structure
} -setup {
} -body {
  set mylist [list "hello world" foo bar baz]
  lrange $mylist 0 1
} -cleanup {
  unset -nocomplain mylist
} -result {{hello world} foo}}

###############################################################################

runTest {test lrange-3.err.1 {
  R-20123-18594: lrange wrong # args
} -setup {
} -body {
  list [catch {lrange {a b c}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
