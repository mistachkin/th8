###############################################################################
#
# coverage_harpy_range.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L452, L453,
# L454 -- the upper-bound range checks for hour, minute, second
# inside th8PolicyParseTimestamp:
#
#   if (hour < 0 || hour > 23) return TH8_ERROR;
#   if (minute < 0 || minute > 59) return TH8_ERROR;
#   if (second < 0 || second > 60) return TH8_ERROR;
#
# Each value is parsed from two ASCII digits, so the field
# range is 0..99 and the C1 condition (`< 0`) is intrinsically
# dead.  Existing annotation tests feed valid time values
# (hour=00, min=00, sec=00) so only (F,F) is hit; the (F,T)
# upper-bound rejection vector is uncovered.
#
# Each helper here injects a digit pair > the field's upper
# bound: hour=25, minute=99, second=99.  The L452/L453/L454
# range checks fire with C1=F,C2=T, closing the C2-pair on
# the live half of each decision.  Parsing fails with
# "annotation: invalid notBefore timestamp".
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

runTest {test harpy_rng-1.1 {
  Hour=25 drives th8_policy.c L452 (F,T): hour parsed as 25,
  C1=F (>=0), C2=T (>23).  Parse fails at hour range check.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_big_hour.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_rng-1.2 {
  Minute=99 drives th8_policy.c L453 (F,T): minute parsed
  as 99, C2=T (>59).  Hour passes; minute fails.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_big_minute.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_rng-1.3 {
  Second=99 drives th8_policy.c L454 (F,T): second parsed
  as 99, C2=T (>60).  Hour + minute pass; second fails.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_big_second.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

source tests/epilogue.tcl
