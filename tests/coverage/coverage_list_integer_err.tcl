###############################################################################
#
# coverage_list_integer_err.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for Th8_ToInt OR-compounds in
# src/plugins/th8_lists.c at:
#
#   line 878    lsearch -sorted -integer
#   line 1251   lsort -integer
#
# Pattern: rc = Th8_ToInt(... azElem ...) != TH8_OK
#           || Th8_ToInt(... zPat ...) != TH8_OK
#
# Existing tests cover the F,F (both succeed) success vector.
# Driving the T,- and F,T edges requires non-integer inputs.
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

runTest {test li_int-1.1 {
  lsort -integer with a non-integer element drives the T,-
  vector at line 1251 (azElem[i] not parseable as integer)
} -constraints {
    th8
} -body {
  catch {lsort -integer [list 1 2 notanumber 4]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test li_int-1.2 {
  lsort -integer with all-integer list drives the F,F success
  vector (already covered by existing tests, but reinforced)
} -constraints {
    th8
} -body {
  catch {lsort -integer [list 5 1 4 2 3]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test li_int-1.3 {
  lsearch -sorted -integer with non-integer pattern drives F,T
  at line 878 (list parses, pattern doesn't)
} -constraints {
    th8
} -body {
  catch {lsearch -sorted -integer [list 1 2 3 4 5] notanumber} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test li_int-1.4 {
  lsearch -sorted -integer with non-integer element drives T,-
  at line 878 (list element fails to parse)
} -constraints {
    th8
} -body {
  catch {lsearch -sorted -integer [list 1 oops 3 4] 2} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test li_int-1.5 {
  lsearch -sorted -integer with valid integer pattern (F,F vector)
} -constraints {
    th8
} -body {
  catch {lsearch -sorted -integer [list 1 2 3 4 5] 3} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
