###############################################################################
#
# lsearch.tcl --
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
# Section 5 -- lsearch: Search for an element in a list
#
###############################################################################

runTest {test lsearch-5.1 {
  R-33422-41481: lsearch finds element at beginning of list
} -body {
  lsearch {a b c d e} a
} -result {0}}

###############################################################################

runTest {test lsearch-5.2 {
  R-33422-41481: lsearch finds element in middle of list
} -body {
  lsearch {a b c d e} c
} -result {2}}

###############################################################################

runTest {test lsearch-5.3 {
  R-33422-41481: lsearch finds element at end of list
} -body {
  lsearch {a b c d e} e
} -result {4}}

###############################################################################

runTest {test lsearch-5.4 {
  R-03526-22310: lsearch returns -1 when element not found
} -body {
  lsearch {a b c d e} z
} -result {-1}}

###############################################################################

runTest {test lsearch-5.5 {
  R-33422-41481: lsearch returns index of first match with duplicates
} -body {
  lsearch {a b c b d} b
} -result {1}}

###############################################################################

runTest {test lsearch-5.6 {
  R-03526-22310: lsearch on empty list returns -1
} -body {
  lsearch {} anything
} -result {-1}}

###############################################################################

runTest {test lsearch-5.7 {
  R-33422-41481: lsearch finds element in single-element list
} -body {
  lsearch {hello} hello
} -result {0}}

###############################################################################

runTest {test lsearch-5.8 {
  R-03526-22310: lsearch returns -1 for single-element list with no match
} -body {
  lsearch {hello} world
} -result {-1}}

###############################################################################

runTest {test lsearch-5.9 {
  R-33422-41481: lsearch exact match with no glob characters
} -body {
  lsearch {apple banana cherry} banana
} -result {1}}

###############################################################################

runTest {test lsearch-5.10 {
  R-03526-22310: lsearch glob pattern returns -1 when no match
} -body {
  lsearch {apple banana cherry} z*
} -result {-1}}

###############################################################################

runTest {test lsearch-5.11 {
  R-33422-41481: lsearch finds element in list of numeric strings
} -body {
  lsearch {10 20 30 40 50} 30
} -result {2}}

###############################################################################

runTest {test lsearch-5.12 {
  R-33422-41481: lsearch uses default glob matching with wildcard pattern
} -body {
  lsearch {apple banana cherry} b*
} -result {1}}

###############################################################################

runTest {test lsearch-5.err.1 {
  R-33422-41481: lsearch with wrong number of args is error
} -setup {
} -body {
  list [catch {lsearch} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
