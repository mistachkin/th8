###############################################################################
#
# coverage_ossl_fault.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for the OpenSSL failure arms in
# src/plugins/harpy/th8_snk.c.  These arms (e.g. the
# `EVP_PKEY_fromdata_init(kctx) != 1` check in the RSA public-key
# construction path) never execute in normal runs because the
# OpenSSL calls succeed on valid keys.  The OSSL_CALL() fault
# wrapper -- armed via Th8_FaultConfig.nFailOsslMask and driven by
# ::th8testlib::osslfaulteval -- forces the wrapped call to report
# failure WITHOUT invoking OpenSSL, so the error arm runs while
# the surrounding decision's condition text is unchanged (MC/DC
# counts stay honest).
#
# Proof-of-concept: TH8_OSSL_OP_FROMDATA_INIT (bit 0) drives the
# "RSA verify: key construction failed" arm.  Additional op IDs
# and their wrapped sites are added incrementally.
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

runTest {test osslfault-1.1 {
  Baseline (control): a valid harpy verify succeeds with no
  fault armed, confirming the fixture key material is good so
  the fault is the sole cause of the failure in osslfault-1.2.
} -constraints {
    th8 crypto_enabled harpy_sign fault_injection
} -setup {
  set t [th8testlib::load_snk tests/helpers/testkey2048.snk]
  set d "ossl fault poc"
  set s [harpy sign $t $d]
} -body {
  harpy verify $t $d $s
} -cleanup {
  unset -nocomplain t d s
} -result {ok}}

###############################################################################

runTest {test osslfault-1.2 {
  Arming TH8_OSSL_OP_FROMDATA_INIT (bit 0) forces
  EVP_PKEY_fromdata_init() to fail in th8_snk.c's RSA
  public-key construction, driving the
  `EVP_PKEY_fromdata_init(kctx) != 1` error arm and the
  "RSA verify: key construction failed" result -- a decision
  otherwise uncovered because the call always succeeds on valid
  input.  The fault is armed and evaluated in the current interp
  (not the isolated `fault eval` child) so the signed-only
  crypto path is fully live.
} -constraints {
    th8 crypto_enabled harpy_sign fault_injection
} -setup {
  set t [th8testlib::load_snk tests/helpers/testkey2048.snk]
  set d "ossl fault poc"
  set s [harpy sign $t $d]
} -body {
  set rc [catch {th8testlib::osslfaulteval 0 "harpy verify $t {$d} {$s}"} m]
  list $rc [string match "*key construction failed*" $m]
} -cleanup {
  unset -nocomplain t d s rc m
} -result {1 1}}

###############################################################################

runTest {test osslfault-1.3 {
  Arming TH8_OSSL_OP_FROMDATA (bit 1) forces the SECOND call in
  the same compound -- EVP_PKEY_fromdata() -- to fail while
  EVP_PKEY_fromdata_init() still succeeds, driving the C2=T
  vector (F,T) of the `... || EVP_PKEY_fromdata(...) != 1`
  decision.  Together with the baseline (F,F) and osslfault-1.2's
  (T,-), this closes the decision's two condition pairs to 100%
  MC/DC -- demonstrating that a two-condition OpenSSL compound
  needs one op ID per wrapped call.
} -constraints {
    th8 crypto_enabled harpy_sign fault_injection
} -setup {
  set t [th8testlib::load_snk tests/helpers/testkey2048.snk]
  set d "ossl fault poc"
  set s [harpy sign $t $d]
} -body {
  set rc [catch {th8testlib::osslfaulteval 1 "harpy verify $t {$d} {$s}"} m]
  list $rc [string match "*key construction failed*" $m]
} -cleanup {
  unset -nocomplain t d s rc m
} -result {1 1}}

###############################################################################

runTest {test osslfault-2.1 {
  Rollout: every OpenSSL call in the RSA VERIFY path
  (th8RsaVerify) is wrapped with OSSL_CALL / OSSL_CALL_PTR.
  Arming each op (V_BN_N .. V_DVUPDATE, ids 2..11) in turn and
  running harpy verify forces that call to fail and drives its
  error arm; every armed op must make verify error (rc==1).
  The `fails` list is the set of ops that did NOT error --
  expected empty.
} -constraints {
    th8 crypto_enabled harpy_sign fault_injection
} -setup {
  set t [th8testlib::load_snk tests/helpers/testkey2048.snk]
  set d "ossl rollout verify"
  set s [harpy sign $t $d]
} -body {
  set fails [list]
  foreach op {2 3 4 5 6 7 8 9 10 11} {
    set rc [catch {th8testlib::osslfaulteval $op "harpy verify $t {$d} {$s}"} m]
    if {$rc != 1} then { lappend fails "op$op=rc$rc" }
  }
  set fails
} -cleanup {
  unset -nocomplain t d s fails rc m op
} -result {}}

###############################################################################

runTest {test osslfault-3.1 {
  Rollout: every OpenSSL call in the RSA SIGN path (th8RsaSign)
  is wrapped.  Arming each op (S_BN_N .. S_DSFINAL2, ids 12..34)
  in turn and running harpy sign forces that call to fail and
  drives its error arm; every armed op must make sign error
  (rc==1).  Excludes the nested CRT-block allocations
  (bnctx/pm1/qm1/dmp1/dmq1/iqmp), a follow-up.  `fails` expected
  empty.
} -constraints {
    th8 crypto_enabled harpy_sign fault_injection
} -setup {
  set t [th8testlib::load_snk tests/helpers/testkey2048.snk]
  set d "ossl rollout sign"
} -body {
  set fails [list]
  for {set op 12} {$op <= 34} {incr op} {
    set rc [catch {th8testlib::osslfaulteval $op "harpy sign $t {$d}"} m]
    if {$rc != 1} then { lappend fails "op$op=rc$rc" }
  }
  set fails
} -cleanup {
  unset -nocomplain t d fails rc m op
} -result {}}

###############################################################################

source tests/epilogue.tcl
