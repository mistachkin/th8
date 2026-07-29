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
# Historical note (resolved 2026-07-27): the companion
# `if (nCipher == 0 || nPlainLen > nCipher)` guard and the
# other validation arms once "returned OK with empty
# result rather than the expected error."  That was not a
# hard-to-drive test -- it was Bug 71: th8SecureLoad
# returned a stale TH8_OK on every `goto done` failure and
# cleared the message, so tamper detection failed OPEN.
# With Bug 71 fixed, all those arms (bad magic, bad
# version, corrupted-length, GCM auth failure, oversized)
# are driven and asserted in the companion file
# coverage_secure_load_tamper_mcdc.tcl.  This file retains
# the too-short (F,T) closure, which always worked (it
# `return`s before rc is clobbered).
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
