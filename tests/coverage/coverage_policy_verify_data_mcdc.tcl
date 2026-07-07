###############################################################################
#
# coverage_policy_verify_data_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L1100 in
# th8PolicyVerifyData:
#
#   if (!zName || nName == 0) {
#       Th8_SetResultStatic(
#           interp, "signed-only: script has no origin name", TH8_NOLEN);
#       return TH8_ERROR;
#   }
#
# 2 conditions; pre-test report shows only (F,F) (zName non-NULL,
# nName > 0 -- file paths always satisfy this) so both C1-Pair
# and C2-Pair are missing.  The function is reachable only via
# the policy callback's TH8_PHASE_PRE|TH8_PHASE_READ phase
# (driven by Th8_EvalFile), which always supplies a valid file
# path; there is no in-tree caller that produces NULL or zero-
# length zName.
#
# The new internal-stubs entry th8_PolicyVerifyData plus the
# `::th8testlib::policyverifydata null|empty|name` testlib
# wrapper give the script direct access to the helper after
# setting up a signed-policy context on a child interpreter.
# null drives (T,_); empty drives (F,T).  Both should produce
# TH8_ERROR with the "no origin name" message.
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

runTest {test policyverifydata-1.1 {
  th8_policy.c L1100 (T,-) -- zName==NULL drives the C1=T
  short-circuit; expect TH8_ERROR with "no origin name".
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyverifydata null]
  list [lindex $r 0] [string match "*no origin name*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test policyverifydata-1.2 {
  th8_policy.c L1100 (F,T) -- zName non-NULL but nName==0
  drives C2=T; expect TH8_ERROR with "no origin name".
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyverifydata empty]
  list [lindex $r 0] [string match "*no origin name*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test policyverifydata-2.1 {
  th8_policy.c L1106 (T,T) -- absolute path drives both
  !IsRelativePath=T AND !IsHttpUri=T.  Expect TH8_ERROR
  with "must be a relative path or HTTP(S) URI".  This
  vector pairs with the existing L1106 (F,-) baseline to
  close the C1-Pair.  The L1106 (T,F) C2-Pair is intrinsic-
  unreachable -- documented in the testlib header.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyverifydata absolute]
  list [lindex $r 0] [string match "*must be a relative path*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
