###############################################################################
#
# coverage_harpy_timestamp.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for the timestamp range-validation checks in
# src/plugins/harpy/th8_policy.c (`th8PolicyParseTimestamp`):
#
#   L449  if (month < 1 || month > 12) return TH8_ERROR;
#   L451  if (day < 1   || day > maxDay) return TH8_ERROR;
#
# Existing `ann_bad_month` / `ann_bad_day` helpers exercise the
# C2-pair (value above the upper bound: month=13, day=30 in Feb).
# These tests exercise the C1-pair via zero values for month / day,
# which take the `< 1` branch.
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

runTest {test harpy_ts-1.1 {
  Annotation with month=00 drives src/plugins/harpy/
  th8_policy.c L449 C1-pair (month < 1) -- the lower-
  bound rejection that no existing test exercises.
  Annotation parser rejects the script with "invalid
  notBefore timestamp".
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  set rc [catch {source tests/helpers/ann_zero_month.tcl} m]
  # Expect rc=1 (annotation parse failure) and a non-
  # empty error message that contains "notBefore" or
  # "timestamp" or "annotation".
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain result rc m
} -result {1 1}}

###############################################################################

runTest {test harpy_ts-1.2 {
  Annotation with day=00 drives src/plugins/harpy/
  th8_policy.c L451 C1-pair (day < 1).  Same scheme as
  -1.1 but for notAfter and the day field.
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  set rc [catch {source tests/helpers/ann_zero_day.tcl} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain result rc m
} -result {1 1}}

###############################################################################

runTest {test harpy_ts-2.1 {
  Annotation with century non-leap year (notAfter:2100
  Feb 01) drives the leap-year compound at src/plugins/
  harpy/th8_policy.c L364 with vector (T,F,F) --
  year%4==0, year%100==0 (so C2: year%100!=0 is F),
  year%400!=0 (so C3 is F).  Result: not a leap year,
  maxDay=28.  Day 01 is valid so the annotation parses
  successfully.  Uses notAfter (not notBefore) so the
  future date passes the script-validity gate.
  Complements existing leap-year tests that drive
  (T,T,-) for ordinary 4-year leaps (e.g. 2024).
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  source tests/helpers/ann_century_nonleap.tcl
  set result
} -cleanup {
  unset -nocomplain result
} -result {century-nonleap}}

###############################################################################

runTest {test harpy_ts-2.2 {
  Annotation with century leap year (2000) Feb 29 drives
  L364 with vector (T,F,T) -- year%400==0 fires via C3
  after C1 short-circuits via the !=0 inner check.  Feb
  29 is valid in 2000 so the timestamp parses
  successfully; maxDay=29.
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  source tests/helpers/ann_century_leap.tcl
  set result
} -cleanup {
  unset -nocomplain result
} -result {century-leap}}

###############################################################################

source tests/epilogue.tcl
