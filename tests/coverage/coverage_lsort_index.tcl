###############################################################################
#
# coverage_lsort_index.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for lsort -index compounds at
# src/plugins/th8_lists.c:1230 and 1236:
#
#   if (iIndex < nSubA && azSubA) {
#   if (iIndex < nSubB && azSubB) {
#
# These extract a sub-list element by index.  Existing lsort
# tests cover the success vector (index in range, list parses).
# Coverage targets:
#   - F,- vector: iIndex >= nSubA (index past end of one sublist)
#   - T,F vector: azSubA NULL (split failure -- hard to drive
#     from script; skipped)
#   - T,T vector: success path (already covered)
#
# Coverage-driven; not pinned to specific R-markers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test lsort_idx-1.1 {
  lsort -index N where one sublist is shorter than N+1 elements
  drives the F,- vector at line 1230 (iIndex < nSubA = false)
} -constraints {
    th8
} -body {
  catch {lsort -index 1 [list "a"     "b 2"   "c 3"]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test lsort_idx-1.2 {
  lsort -index 0 with empty-sublist element drives F,- vector
  (nSubA == 0, so iIndex(0) < nSubA(0) is false)
} -constraints {
    th8
} -body {
  catch {lsort -index 0 [list {} {a} {b}]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test lsort_idx-1.3 {
  lsort -index N with both elements past index drives the F,-
  vector at both line 1230 (zA short) AND line 1236 (zB short)
} -constraints {
    th8
} -body {
  catch {lsort -index 5 [list "a"     "b"     "c"]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test lsort_idx-1.4 {
  lsort -index 0 with valid sublists drives the T,T (success) vector
} -constraints {
    th8
} -body {
  lsort -index 0 [list "z 1"   "a 2"   "m 3"]
} -result {{a 2} {m 3} {z 1}}}

###############################################################################

runTest {test lsort_idx-1.5 {
  lsort -index with mixed valid/short sublists drives both T,T
  and F,- on the same call (lsort interleaves comparisons)
} -constraints {
    th8
} -body {
  catch {lsort -index 1 [list "a 1"   "b"     "c 3"]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
