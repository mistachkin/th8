###############################################################################
#
# coverage_mcdc_fruit.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Harvests additional input-drivable MC/DC decisions the reachable-
# denominator gate (docs/internal/mcdc_reachable_criterion.md) leaves as
# genuine debt -- raising the margin above the 95% RTM threshold.  Every
# input below was verified to reach its target function before use:
#
#   src/th8_expr.c  th8ExprEval    rc == TH8_OK && eArgType == TH8_ARG_STRING
#                   (a string-valued expr operand, e.g. "a" eq "a").
#   src/th8_expr.c  th8ExprEvalOne c < '0' || c > '7' (the 2-char radix-shape
#                   edge: "0." gives c < '0', "0o" gives c > '7', "07" the
#                   valid-octal-digit F/F case).
#   src/th8_core.c  Th8_SetResultDouble (double-result round-trip),
#                   th8GetCachedPow10 (prodE in [-60,-32] vs outside), and
#                   th8ScaleByPow10 (round-bit path) -- driven by doubles of
#                   varied magnitude and a non-terminating decimal.
#   src/plugins/th8_harpy.c  th8HarpyClockNtpCommand -- the "-attempts"
#                   option-name check (argl == 9 && memcmp), driven OFFLINE by
#                   `clock ntp` option parsing that errors before any network:
#                   a matching option, a 9-char non-matching option, and a
#                   short option.
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

runTest {test mcdcfruit-1.1 {
  A string-valued expr operand (eq/ne) versus a numeric one drives
  th8_expr.c th8ExprEval's rc == TH8_OK && eArgType == TH8_ARG_STRING
  decision (STRING vs INTEGER arg type).
} -constraints {
    th8
} -body {
  list [expr {"a" eq "a"}] [expr {"a" ne "b"}] [expr {1 + 1}]
} -result {1 1 2}}

###############################################################################

runTest {test mcdcfruit-1.2 {
  Two-character radix-shape operands drive th8_expr.c th8ExprEvalOne's
  octal-digit decision (c < '0' || c > '7'): "0." -> c < '0' (T),
  "0o" -> c > '7' (T), "07" -> neither (valid octal digit, F/F).
} -constraints {
    th8
} -body {
  list [expr {0.}] [expr {0o}] [expr {07}]
} -result {0. 0o 7}}

###############################################################################

runTest {test mcdcfruit-1.3 {
  Doubles of varied magnitude drive th8_core.c Th8_SetResultDouble
  (string<->double round-trip), th8GetCachedPow10 (scale exponent
  inside vs outside [-60,-32]) and th8ScaleByPow10 (rounding).
} -constraints {
    th8
} -body {
  list [expr {1.5}] [expr {123.456}] [expr {1.0e-10}] [expr {1.0e-70}] \
      [expr {0.1 + 0.2}]
} -result {1.5 123.456 1e-10 1e-70 0.30000000000000004}}

###############################################################################

runTest {test mcdcfruit-2.1 {
  `clock ntp` option-name parsing drives th8_harpy.c
  th8HarpyClockNtpCommand's "-attempts" check (argl == 9 && memcmp):
  a matching option (T,T), a 9-char non-matching option (T,F), and a
  short option (F).  All error during option parsing BEFORE any
  network access, so this is offline.
} -constraints {
    th8 clock_ntp
} -body {
  list \
      [catch {clock ntp -attempts 2 -badxyz99} m1] \
      [catch {clock ntp -attemptX 5} m2] \
      [catch {clock ntp -z 1} m3]
} -cleanup {
  unset -nocomplain m1 m2 m3
} -result {1 1 1}}

###############################################################################

source tests/epilogue.tcl
