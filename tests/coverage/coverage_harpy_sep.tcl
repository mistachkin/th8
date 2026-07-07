###############################################################################
#
# coverage_harpy_sep.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L414-415 --
# the structural-separator validation inside
# th8PolicyParseTimestamp:
#
#   if (z[4] != '_' || z[7] != '_' || z[10] != 'T'
#         || z[13] != '_' || z[16] != '_' || z[19] != 'Z') {
#       return TH8_ERROR;
#   }
#
# Six conditions in OR.  Existing tests pass either valid 20-char
# timestamps (all conditions F) or non-20-char strings (returns
# at L411 before L414).  Each condition's independent-effect
# vector requires a 20-char string with EXACTLY ONE wrong
# separator and the other five correct.
#
# Six helpers feed those vectors:
#   ann_sep4   -- z[4]  = 'X' instead of '_'
#   ann_sep7   -- z[7]  = 'X' instead of '_'
#   ann_sep10  -- z[10] = 'X' instead of 'T'
#   ann_sep13  -- z[13] = 'X' instead of '_'
#   ann_sep16  -- z[16] = 'X' instead of '_'
#   ann_sep19  -- z[19] = 'X' instead of 'Z'
#
# Each drives one condition's T result; the other five are F.
# Combined with the all-F valid-timestamp baseline (covered by
# existing harpy annotation tests), closes the six independent-
# effect pairs at L414/415.
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

runTest {test harpy_sep-1.1 {
  z[4] wrong-separator drives th8_policy.c L414 C1=T
  (other 5 are F).
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_sep4.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_sep-1.2 {
  z[7] wrong-separator drives th8_policy.c L414 C2=T.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_sep7.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_sep-1.3 {
  z[10] wrong-separator drives th8_policy.c L414 C3=T.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_sep10.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_sep-1.4 {
  z[13] wrong-separator drives th8_policy.c L415 C4=T.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_sep13.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_sep-1.5 {
  z[16] wrong-separator drives th8_policy.c L415 C5=T.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_sep16.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test harpy_sep-1.6 {
  z[19] wrong-separator drives th8_policy.c L415 C6=T.
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_sep19.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

source tests/epilogue.tcl
