###############################################################################
#
# lremove.tcl --
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
# Section 1 -- lremove: Remove elements from a list by index
#
###############################################################################

runTest {test lremove-1.1 {
  R-05764-04130: remove single element by index
} -body {
  lremove {a b c d e} 2
} -result {a b d e}}

###############################################################################

runTest {test lremove-1.2 {
  R-05764-04130: remove multiple elements
} -body {
  lremove {a b c d e} 0 4
} -result {b c d}}

###############################################################################

runTest {test lremove-1.3 {
  R-57118-58718: remove with end index
} -body {
  lremove {a b c d e} end
} -result {a b c d}}

###############################################################################

runTest {test lremove-1.4 {
  R-57118-58718: remove with end-1 index
} -body {
  lremove {a b c d e} end-1
} -result {a b c e}}

###############################################################################

runTest {test lremove-1.5 {
  R-05764-04130: remove from empty list
} -body {
  lremove {} 0
} -result {}}

###############################################################################

runTest {test lremove-1.6 {
  R-64017-07410: no indices (returns original)
} -body {
  lremove {a b c d e}
} -result {a b c d e}}

###############################################################################

runTest {test lremove-2.1 {
  R-05764-04130: wrong # args (no args)
} -setup {
} -body {
  list [catch {lremove} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test lremove-3.1 {
  R-12457-39560: out-of-range index silently ignored
} -body {
  lremove {a b c} 10
} -result {a b c}}

###############################################################################

runTest {test lremove-3.2 {
  R-12457-39560: duplicate indices
} -body {
  lremove {a b c d e} 1 1
} -result {a c d e}}

###############################################################################

runTest {test lremove-3.3 {
  R-05764-04130: remove all elements
} -body {
  lremove {a b c} 0 1 2
} -result {}}

###############################################################################

source tests/epilogue.tcl
