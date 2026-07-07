###############################################################################
#
# coverage_harpy_nondigit.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L423, L427,
# L431, L435, L439, L443 -- the six per-field digit-range
# guards inside th8PolicyParseTimestamp:
#
#   if (z[i] < '0' || z[i] > '9') return TH8_ERROR;
#
# Existing harpy annotation tests (annotation-2.2 "bad-month",
# 2.3 "bad-day", etc.) only feed timestamps with valid digit
# characters and invalid numeric values, so each loop's
# digit-range check sees the (F,F) vector at every iteration
# and never (T,-) or (F,T).  Coverage at all six lines: 0%.
#
# Each helper here injects an uppercase letter (e.g. 'X') in
# one of the digit positions so the L4xx compound fires with
# C2=T (z[i] > '9').  The structural separator check at L414
# still passes because the helpers preserve every '_'/'T'/'Z'
# position.  Parsing fails with "annotation: invalid notBefore
# timestamp", and the L4xx digit-range check is exercised.
#
# This closes the C2-pair at all six digit-range guards.
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

runTest {test harpy_nd-1.1 {
  Non-digit ('X') in year position drives th8_policy.c L423
  (F,T): z[i] >= '0' but z[i] > '9'.  Fails at the year
  loop's first non-digit; later fields are not reached.
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  source tests/helpers/ann_nondigit_year.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-1.2 {
  Non-digit in month position drives th8_policy.c L427
  (F,T).  Year passes (digits), month fails at first
  non-digit.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_nondigit_month.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-1.3 {
  Non-digit in day position drives th8_policy.c L431
  (F,T).  Year + month pass; day fails.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_nondigit_day.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-1.4 {
  Non-digit in hour position drives th8_policy.c L435
  (F,T).  Date fields pass; hour fails.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_nondigit_hour.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-1.5 {
  Non-digit in minute position drives th8_policy.c L439
  (F,T).  Date + hour pass; minute fails.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_nondigit_minute.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-1.6 {
  Non-digit in second position drives th8_policy.c L443
  (F,T).  Date + time-of-day pass; second fails.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_nondigit_second.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

# Section 2 -- C1=T (z[i] < '0') closures for each digit-range
# guard, using a low-ASCII char ('!' = 33).  Symmetric with
# Section 1 to close the C1-pair at L423/427/431/435/439/443.

###############################################################################

runTest {test harpy_nd-2.1 {
  Low char ('!') in year position drives th8_policy.c L423
  (T,-): z[i] < '0' short-circuits.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_lochar_year.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-2.2 {
  Low char in month position drives th8_policy.c L427 (T,-).
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_lochar_month.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-2.3 {
  Low char in day position drives th8_policy.c L431 (T,-).
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_lochar_day.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-2.4 {
  Low char in hour position drives th8_policy.c L435 (T,-).
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_lochar_hour.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-2.5 {
  Low char in minute position drives th8_policy.c L439 (T,-).
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_lochar_minute.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_nd-2.6 {
  Low char in second position drives th8_policy.c L443 (T,-).
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_lochar_second.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

source tests/epilogue.tcl
