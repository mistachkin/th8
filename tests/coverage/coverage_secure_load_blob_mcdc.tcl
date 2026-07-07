###############################################################################
#
# coverage_secure_load_blob_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/crypto/th8_secure.c L1889
# `if (!zResult || nBlob < TH8_SECURE_BLOB_HEADER)`
# inside `th8SecureLoad`.  Pre-test report 0% MC/DC --
# the success path runs (F,F=F), but no test exercises
# a malformed (too-short) blob in the KV store.
#
# Drive 1.1: 1-byte blob -> L1889 (F,T)=T closes
# C2-Pair.
#
# Note on the L1927 `if (nCipher == 0 ||
# nPlainLen > nCipher)` companion guard: that path
# proved hard to drive via a script-constructed
# 40-byte blob (the secure-load code returns OK with
# empty result rather than the expected "corrupted
# blob" error -- likely because the master-key
# decrypt of an empty ciphertext succeeds with an
# empty plaintext, bypassing the L1927 check by some
# path I haven't isolated yet).  Logged as future
# work; the L1889 closure is the principal win for
# this batch.
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

runTest {test secure_load_blob-1.1 {
  th8SecureLoad L1889 C2-Pair (F,T) -- inject a 1-byte
  blob into the KV store under the secure-load prefix;
  th8SecureLoad reads it back, finds nBlob (1) <
  TH8_SECURE_BLOB_HEADER (40), drives (F,T=T) and
  returns the "invalid blob (too short)" error.
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
  ::th8testlib::kv set "th8:secure:_slb_short" "x"
} -body {
  set rc [catch {secure load _slb_short} msg]
  list $rc $msg
} -cleanup {
  catch {::th8testlib::kv unset "th8:secure:_slb_short"}
  ::th8testlib::secure_persist disable
  unset -nocomplain rc msg
} -result {1 {secure load: invalid blob (too short)}}}

###############################################################################

source tests/epilogue.tcl
