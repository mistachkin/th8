###############################################################################
#
# linsert.tcl --
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
# Section 1 -- linsert: Insert elements into a list before an index
#
###############################################################################

runTest {test linsert-1.1 {
  R-50695-55336: linsert inserts elements before the element at index
} -body {
  linsert {a b c} 1 X
} -result {a X b c}}

###############################################################################

runTest {test linsert-1.2 {
  R-50695-55336: linsert at index 0 prepends
} -body {
  linsert {a b c} 0 X
} -result {X a b c}}

###############################################################################

runTest {test linsert-1.3 {
  R-50695-55336: linsert inserts multiple elements in order
} -body {
  linsert {a b c} 1 X Y Z
} -result {a X Y Z b c}}

###############################################################################

runTest {test linsert-1.4 {
  R-19079-46202: linsert with an index of end appends
} -body {
  linsert {a b c} end X
} -result {a b c X}}

###############################################################################

runTest {test linsert-1.5 {
  R-19079-46202: linsert with index end-1 inserts before the last element
} -body {
  linsert {a b c} end-1 X
} -result {a b X c}}

###############################################################################

runTest {test linsert-1.6 {
  R-29287-02583: linsert with a negative index inserts at the start
} -body {
  linsert {a b c} -5 X
} -result {X a b c}}

###############################################################################

runTest {test linsert-1.7 {
  R-29287-02583: linsert with an index past the end appends
} -body {
  linsert {a b c} 10 X
} -result {a b c X}}

###############################################################################

runTest {test linsert-1.8 {
  R-04133-51517: linsert with no element arguments returns the list unchanged
} -body {
  linsert {a b c} 1
} -result {a b c}}

###############################################################################

runTest {test linsert-1.9 {
  R-06207-62657: linsert raises an error on a malformed list argument
} -body {
  catch {linsert "a \{b c" 1 X}
} -result {1}}

###############################################################################

runTest {test linsert-1.10 {
  R-05158-45827: linsert raises an error on an invalid index argument
} -body {
  catch {linsert {a b c} notanindex X}
} -result {1}}

###############################################################################

source tests/epilogue.tcl
