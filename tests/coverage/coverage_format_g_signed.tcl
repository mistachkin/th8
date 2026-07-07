###############################################################################
#
# coverage_format_g_signed.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the sign-skip branch of the %g format carry loop
# at th8_formatting.c:536-537 -- the `(zStart < fp && (*zStart
# == '-' || *zStart == '+'))` compound that protects the carry
# from rolling into the sign byte.
#
# Vectors:
#   (T, T, -)  negative number with carry  -- *zStart == '-'
#   (T, F, T)  explicit '+' (via %+g) with carry  -- *zStart == '+'
#
# Existing tests use positive numbers without forced sign,
# so the (T, F, F) [no sign byte to skip] vector is the only
# one currently covered.
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

runTest {test fmtgsign-1.1 {
  Negative %g with fractional carry drives the (T, T, -)
  vector at th8_formatting.c:536-537 -- the leading '-'
  byte is skipped so the carry loop walks just the digits.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [format "%.3g" -9.9995]
  lappend rcs [format "%.4g" -99.9995]
  lappend rcs [format "%.2g" -0.995]
  lappend rcs [format "%g" -9.99999e-5]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -match glob -result {*}}

###############################################################################

runTest {test fmtgsign-1.2 {
  Positive %+g with fractional carry drives the (T, F, T)
  vector at th8_formatting.c:536-537 -- explicit '+' sign
  is skipped so the carry loop walks digits only.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [format "%+.3g" 9.9995]
  lappend rcs [format "%+.4g" 99.9995]
  lappend rcs [format "%+.2g" 0.995]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -match glob -result {*}}

###############################################################################

source tests/epilogue.tcl
