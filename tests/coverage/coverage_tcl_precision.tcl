###############################################################################
#
# coverage_tcl_precision.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the ::tcl_precision-driven
# fixed-digit formatting path in src/th8_core.c::
# Th8_SetResultDouble.  Default operation (nSig == 0) uses
# Grisu + shortest-round-trip search; the fixed-precision
# else-branch at L18365-18387 is reached only when the
# script sets ::tcl_precision to a value in 1..17.  Without
# that branch executing, the rounding/carry/strip loops at
# L18370, L18371, L18404 are all 0% MC/DC.  These tests also
# drive the validation compound at L18154 (Th8_ToInt OK &&
# v >= 0 && v <= 17) by writing invalid / negative / too-
# large tcl_precision values.
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

runTest {test tcl_precision-1.1 {
  ::tcl_precision set to a valid in-range integer (5)
  drives the L18154 (T, T, T) vector (already covered by
  default runs) AND forces the nSig != 0 else-branch at
  L18365-18387 so the rounding/carry/strip loops execute.
  Computed pi (an arithmetic expression -- not a literal,
  which would short-circuit through the parser) rounded to
  5 significant digits is 3.1416, requiring the carry path
  at L18370/L18371 because zDig[5] >= '5'.
} -constraints {
    th8
} -body {
  set ::tcl_precision 5
  set rResult [expr {2.0 * 1.5707963267948966}]
  set ::tcl_precision "0"
  set rResult
} -cleanup {
  unset -nocomplain rResult
} -result {3.1416}}

###############################################################################

runTest {test tcl_precision-1.2 {
  ::tcl_precision = 17 (upper boundary).  Drives L18154
  C3-Pair: confirms v <= 17 is T.  Also forces the strip-
  trailing-zeros loop at L18404 because the 17-digit form
  of 1.0 is "10000000000000000" which strips to "1".
  Computed via arithmetic so Th8_SetResultDouble actually
  runs.
} -constraints {
    th8
} -body {
  set ::tcl_precision 17
  set rResult [expr {0.5 + 0.5}]
  set ::tcl_precision "0"
  set rResult
} -cleanup {
  unset -nocomplain rResult
} -result {1.0}}

###############################################################################

runTest {test tcl_precision-1.3 {
  ::tcl_precision = 1 (lower boundary).  Drives the
  L18370 carry-on-rounding-up path: 9.5 rounds to 10 at 1
  digit, requiring the carry propagation at L18371 plus
  the leading-1 shift at L18380-18386 (zDig[0] = '1'; expn++).
  Computed via arithmetic so the formatter runs.
} -constraints {
    th8
} -body {
  set ::tcl_precision 1
  set rResult [expr {4.5 + 5.0}]
  set ::tcl_precision "0"
  set rResult
} -cleanup {
  unset -nocomplain rResult
} -result {1e+01}}

###############################################################################

runTest {test tcl_precision-1.4 {
  ::tcl_precision = 3 with a value whose digit 4 is < '5'.
  Drives the L18370 (T, F) vector: nDig < 17 is T, but
  zDig[nDig] >= '5' is F (no carry).  pi at 3 digits has
  zDig[3] = '1' so no rounding occurs.  Computed via
  arithmetic.
} -constraints {
    th8
} -body {
  set ::tcl_precision 3
  set rResult [expr {2.0 * 1.5707963267948966}]
  set ::tcl_precision "0"
  set rResult
} -cleanup {
  unset -nocomplain rResult
} -result {3.14}}

###############################################################################

runTest {test tcl_precision-2.1 {
  ::tcl_precision set to a non-integer string drives the
  L18154 C1=F vector (Th8_ToInt fails).  Default precision
  (0 = shortest round-trip) is used and the result matches
  the standard 17-digit form.  Computed via arithmetic.
} -constraints {
    th8
} -body {
  set ::tcl_precision "not_an_integer"
  set rResult [expr {2.0 * 1.5707963267948966}]
  set ::tcl_precision "0"
  set rResult
} -cleanup {
  unset -nocomplain rResult
} -result {3.141592653589793}}

###############################################################################

runTest {test tcl_precision-2.2 {
  ::tcl_precision set to a negative integer (-1) drives the
  L18154 C2=F vector (v >= 0 is F).  Falls back to nSig = 0
  (shortest round-trip).  Computed via arithmetic.
} -constraints {
    th8
} -body {
  set ::tcl_precision -1
  set rResult [expr {2.0 * 1.5707963267948966}]
  set ::tcl_precision "0"
  set rResult
} -cleanup {
  unset -nocomplain rResult
} -result {3.141592653589793}}

###############################################################################

runTest {test tcl_precision-2.3 {
  ::tcl_precision set to a value > 17 drives the L18154
  C3=F vector (v <= 17 is F).  Falls back to nSig = 0
  (shortest round-trip).  Computed via arithmetic.
} -constraints {
    th8
} -body {
  set ::tcl_precision 18
  set rResult [expr {2.0 * 1.5707963267948966}]
  set ::tcl_precision "0"
  set rResult
} -cleanup {
  unset -nocomplain rResult
} -result {3.141592653589793}}

###############################################################################

source tests/epilogue.tcl
