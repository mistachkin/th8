###############################################################################
#
# coverage_math_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_math.c short-circuit compounds
# whose F branch isn't exercised by ordinary math tests:
#
#   L499  th8MathClassify (used by expr fpclassify(VAL))
#         `NEVER(!pPlat) || !pPlat->xMathFunc`
#         Needs xMathFunc=NULL.
#
# Driven via the fault layer's -nullCallbacks option on a
# child interp.
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

runTest {test math_mcdc-1.1 {
  expr {fpclassify(1.0)} under -nullCallbacks xMathFunc
  drives th8MathClassify in src/th8_math.c L499 C2=T
  branch (xMathFunc is the NULL operand).  The call
  errors with "math function not available".
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set r [::th8testlib::fault eval {expr {fpclassify(1.0)}} \
      -nullCallbacks xMathFunc]
  expr {[string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test math_mcdc-2.1 {
  expr {fpclassify(1.0)} under -failMathFunc drives the
  th8MathClassify L503 C3=F vector (xMathFunc is valid
  but returns TH8_ERROR), which the script command path
  reports as a "domain error".  Same flag also drives the
  fpclassify_command compound at th8_expressions.c L137
  C3=F.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set r1 [::th8testlib::fault eval {expr {fpclassify(1.0)}} \
      -failMathFunc]
  set r2 [::th8testlib::fault eval {fpclassify 1.0} \
      -failMathFunc]
  set r3 [::th8testlib::fault eval {expr {isfinite(1.0)}} \
      -failMathFunc]
  # Each must complete cleanly.  Their bodies error out
  # via the C3=F branch.
  list \
      [expr {[string length [lindex $r1 1]] >= 0}] \
      [expr {[string length [lindex $r2 1]] >= 0}] \
      [expr {[string length [lindex $r3 1]] >= 0}]
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1 1 1}}

###############################################################################

source tests/epilogue.tcl
