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

runTest {test policyverifydata-3.1 {
  th8_policy.c token guard `if (!zToken || Th8_Strlen(...) != 16)`
  (F,T) -- a signature whose parsed public-key token is present but
  not 16 characters long.  The `badtoken` mode reads the coverage
  fixture tests/helpers/badtoken.tcl.b64sig (line-3 token "ABCD"),
  so Th8_HarpySigLoad returns a non-NULL 4-char token and the guard
  drives C2=T.  Expect TH8_ERROR with "missing public key token".
  Pairs with the (F,F) baseline of every real signed load to close
  the C2-Pair.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyverifydata badtoken]
  list [lindex $r 0] [string match "*missing public key token*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test policyverifydata-3.2 {
  th8_policy.c token guard `if (!zToken || Th8_Strlen(...) != 16)`
  (T,-) -- a signature file with no public-key token at all.  The
  `notoken` mode reads tests/helpers/notoken.tcl.b64sig, whose line 3
  omits the "-- TOKEN" pattern, so Th8_HarpySigLoad returns a NULL
  token and the guard drives C1=T (short-circuit).  Expect TH8_ERROR
  with "missing public key token".  Closes the C1-Pair; together with
  3.1 and the (F,F) baseline the token guard reaches 100% MC/DC.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyverifydata notoken]
  list [lindex $r 0] [string match "*missing public key token*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
