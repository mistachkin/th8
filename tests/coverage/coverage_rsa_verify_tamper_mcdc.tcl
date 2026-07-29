###############################################################################
#
# coverage_rsa_verify_tamper_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Signature-failure drives for Th8_RsaVerify in
# src/plugins/harpy/th8_snk.c.  These exercise the pre-
# verification sanity guards that reject malformed or
# oversized signatures before any OpenSSL work -- the
# "signature-failure" half of the crypto tamper fixtures.
#
# The genuine cryptographic-mismatch arm (a tampered but
# correctly-sized signature -> "RSA verify: signature
# mismatch") is already covered by tests/harpy.tcl; the
# OpenSSL-internal failure arms are covered by
# coverage_ossl_fault.tcl.  This file targets the oversize-
# signature guard (th8_snk.c L1331,
# `nSig > nModulus + 64 || nSig > TH8_RSA_MAX_SIG_BYTES`),
# whose TRUE arm no normal signature reaches.
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

runTest {test rsa_verify_tamper-1.1 {
  Baseline (control): a valid harpy verify over a freshly
  signed payload succeeds, confirming the fixture key and
  signature are good so that 1.2's failure is attributable
  solely to the oversized signature.
} -constraints {
    th8 crypto_enabled harpy_sign
} -setup {
  set t [th8testlib::load_snk tests/helpers/th8_test_key.snk]
  set d "rsa verify tamper baseline"
  set s [harpy sign $t $d]
} -body {
  harpy verify $t $d $s
} -cleanup {
  unset -nocomplain t d s
} -result {ok}}

###############################################################################

runTest {test rsa_verify_tamper-1.2 {
  Th8_RsaVerify oversize-signature guard (th8_snk.c L1331).
  Repeating the valid base64 signature body eight times keeps
  it well-formed base64 (so it survives Harpy signature
  parsing) but decodes to far more bytes than the modulus,
  driving `nSig > nModulus + 64` (T) and reporting "RSA
  verify: signature too large" -- an allocation-bomb guard the
  normal path never reaches.
} -constraints {
    th8 crypto_enabled harpy_sign
} -setup {
  set t [th8testlib::load_snk tests/helpers/th8_test_key.snk]
  set d "rsa verify tamper oversize"
  set s [harpy sign $t $d]
} -body {
  list [catch {harpy verify $t $d [string repeat $s 8]} m] \
      [string match "*signature too large*" $m]
} -cleanup {
  unset -nocomplain t d s m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
