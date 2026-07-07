###############################################################################
#
# coverage_harpy_flags_ann.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L598-599 --
# the `flags:` annotation prefix check:
#
#   } else if (nContent > 6
#             && Th8_Memcmp(interp, zContent, "flags:", 6) == 0) {
#
# Existing harpy annotation tests use `notBefore:` and
# `notAfter:` annotations but no test feeds a `flags:`
# annotation; the (T,T) vector is uncovered and L600+ is
# never executed (count=0).
#
# This test sources a helper with a valid complex flag value
# in a `<<flags:{1:abc}>>` annotation.  The L598 compound
# fires (T,T) -- content is long enough AND matches the
# `flags:` prefix -- and Th8_AttrFlagsParse runs on the
# remainder.  Combined with the existing (F,-) and (T,F)
# coverage from coverage_harpy_unknown_ann (short/unknown
# prefixes), closes both C1- and C2-pairs at L598.
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

runTest {test harpy_fann-1.1 {
  Valid `<<flags:{1:abc}>>` annotation drives th8_policy.c
  L598 (T,T) -- prefix matches, Th8_AttrFlagsParse with
  bComplex=1 succeeds on `{1:abc}`.  Script body runs
  normally.  Note: sourcing the helper sets
  ::th8_security(flags); the -cleanup must unset it so
  subsequent tests (crypto.tcl, security.tcl) see the
  default 6-element security array.
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  source tests/helpers/ann_flags_valid.tcl
  set result
} -cleanup {
  unset -nocomplain result
  array unset ::th8_security flags
} -result {flags-valid}}

###############################################################################

runTest {test harpy_fann-1.2 {
  Invalid `<<flags:+xyz>>` annotation: L598 fires (T,T)
  but Th8_AttrFlagsParse rejects "+xyz" (not a complex-
  format key).  Error: "annotation: invalid flags value".
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_flags_invalid.tcl
} -returnCodes 1 -match glob -result {annotation: invalid flags*}}

###############################################################################

source tests/epilogue.tcl
