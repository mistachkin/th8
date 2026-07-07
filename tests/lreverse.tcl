###############################################################################
#
# lreverse.tcl --
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
# Section 1 -- lreverse: Reverse the elements of a list
#
###############################################################################

runTest {test lreverse-1.1 {
  R-03143-05122: reverse a simple list
} -body {
  lreverse {a b c d e}
} -result {e d c b a}}

###############################################################################

runTest {test lreverse-1.2 {
  R-03143-05122: reverse a single element
} -body {
  lreverse {hello}
} -result {hello}}

###############################################################################

runTest {test lreverse-1.3 {
  R-20941-53492: reverse an empty list
} -body {
  lreverse {}
} -result {}}

###############################################################################

runTest {test lreverse-1.4 {
  R-18329-05882: reverse with nested lists (elements stay intact)
} -body {
  lreverse {a {b c} d}
} -result {d {b c} a}}

###############################################################################

runTest {test lreverse-2.1 {
  R-03143-05122: wrong # args (no args)
} -setup {
} -body {
  list [catch {lreverse} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test lreverse-2.2 {
  R-03143-05122: wrong # args (too many args)
} -setup {
} -body {
  list [catch {lreverse {a b} extra} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test lreverse-3.1 {
  R-03143-05122: reverse of reverse is identity
} -body {
  lreverse [lreverse {a b c d}]
} -result {a b c d}}

###############################################################################

runTest {test lreverse-3.2 {
  R-03143-05122: reverse a two-element list
} -body {
  lreverse {x y}
} -result {y x}}

###############################################################################

source tests/epilogue.tcl
