###############################################################################
#
# coverage_secure_load_tamper_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Tamper / corruption drives for th8SecureLoad in
# src/plugins/crypto/th8_secure.c.  Each test plants a
# crafted-bad encrypted-variable blob into the KV store
# under the "th8:secure:<var>" prefix (via the length-
# preserving ::th8testlib::kv set primitive), then calls
# "secure load" and asserts the SPECIFIC validation error
# now propagates to the script.
#
# These are the regression tests for Bug 71: th8SecureLoad
# reused its rc=TH8_ERROR default to hold the KV_GET result,
# so every "goto done" validation/decrypt-failure path (bad
# magic, bad version, corrupted blob, AES-GCM auth-tag
# mismatch, oversized blob) returned the stale TH8_OK and the
# error message was cleared -- tamper detection failed OPEN.
# Before the fix every case below returned rc=0 with an empty
# message; each drives a previously-0% MC/DC arm.
#
# Blob layout (TH8_SECURE_BLOB_HEADER = 40 bytes):
#   magic[4] "T8SV" | version[1] 1 | reserved[3] |
#   nonce[12] | tag[16] | plaintext-len[4, little-endian]
#   followed by ciphertext.
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

runTest {test secure_load_tamper-1.1 {
  th8SecureLoad bad-magic arm -- a 41-byte blob whose
  first 4 bytes are not "T8SV" drives the magic check
  (th8_secure.c L2244) and reports "invalid blob magic".
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
  ::th8testlib::kv set "th8:secure:_slt_magic" [binary format \
      a4ca3a12a16ia1 "XXXX" 1 "\x00\x00\x00" [string repeat \x00 12] \
      [string repeat \x00 16] 0 "A"]
} -body {
  list [catch {secure load _slt_magic} msg] $msg
} -cleanup {
  catch {::th8testlib::kv unset "th8:secure:_slt_magic"}
  ::th8testlib::secure_persist disable
  unset -nocomplain msg
} -result {1 {secure load: invalid blob magic}}}

###############################################################################

runTest {test secure_load_tamper-1.2 {
  th8SecureLoad bad-version arm -- a valid-magic blob with
  version byte != TH8_SECURE_BLOB_VERSION drives the version
  check (th8_secure.c L2249) and reports "unsupported blob
  version".
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
  ::th8testlib::kv set "th8:secure:_slt_ver" [binary format \
      a4ca3a12a16ia1 "T8SV" 2 "\x00\x00\x00" [string repeat \x00 12] \
      [string repeat \x00 16] 0 "A"]
} -body {
  list [catch {secure load _slt_ver} msg] $msg
} -cleanup {
  catch {::th8testlib::kv unset "th8:secure:_slt_ver"}
  ::th8testlib::secure_persist disable
  unset -nocomplain msg
} -result {1 {secure load: unsupported blob version}}}

###############################################################################

runTest {test secure_load_tamper-1.3 {
  th8SecureLoad corrupted-blob arm, first condition
  (nCipher == 0) -- an exactly-40-byte valid header with no
  ciphertext drives L2264 (T,-) and reports "corrupted blob".
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
  ::th8testlib::kv set "th8:secure:_slt_ncip0" [binary format \
      a4ca3a12a16i "T8SV" 1 "\x00\x00\x00" [string repeat \x00 12] \
      [string repeat \x00 16] 0]
} -body {
  list [catch {secure load _slt_ncip0} msg] $msg
} -cleanup {
  catch {::th8testlib::kv unset "th8:secure:_slt_ncip0"}
  ::th8testlib::secure_persist disable
  unset -nocomplain msg
} -result {1 {secure load: corrupted blob}}}

###############################################################################

runTest {test secure_load_tamper-1.4 {
  th8SecureLoad corrupted-blob arm, second condition
  (nPlainLen > nCipher) -- a 41-byte blob (1 ciphertext byte)
  whose header plaintext-length field claims 999 drives L2264
  (-,T) and reports "corrupted blob".
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
  ::th8testlib::kv set "th8:secure:_slt_plen" [binary format \
      a4ca3a12a16ia1 "T8SV" 1 "\x00\x00\x00" [string repeat \x00 12] \
      [string repeat \x00 16] 999 "A"]
} -body {
  list [catch {secure load _slt_plen} msg] $msg
} -cleanup {
  catch {::th8testlib::kv unset "th8:secure:_slt_plen"}
  ::th8testlib::secure_persist disable
  unset -nocomplain msg
} -result {1 {secure load: corrupted blob}}}

###############################################################################

runTest {test secure_load_tamper-1.5 {
  th8SecureLoad oversized-blob arm -- a valid-header blob
  whose ciphertext exceeds the protected region capacity
  drives L2292 and reports "blob exceeds protected region
  capacity".
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
  ::th8testlib::kv set "th8:secure:_slt_big" [binary format \
      a4ca3a12a16ia* "T8SV" 1 "\x00\x00\x00" [string repeat \x00 12] \
      [string repeat \x00 16] 1 [string repeat A 200000]]
} -body {
  list [catch {secure load _slt_big} msg] $msg
} -cleanup {
  catch {::th8testlib::kv unset "th8:secure:_slt_big"}
  ::th8testlib::secure_persist disable
  unset -nocomplain msg
} -result {1 {secure load: blob exceeds protected region capacity}}}

###############################################################################

runTest {test secure_load_tamper-2.1 {
  th8SecureLoad AES-GCM authentication arm -- save a real
  secure variable, flip one ciphertext byte in the persisted
  blob, then reload.  The GCM tag no longer matches, driving
  the EVP_DecryptFinal_ex failure (th8_secure.c L2338) and
  reporting "authentication failed".  This is the core tamper
  detector; before Bug 71 it was silently ignored.
} -constraints {
    th8 crypto_testlib secure_persist
} -setup {
  ::th8testlib::secure_persist enable
  secure create _slt_gcm "hello world secret value"
  secure save _slt_gcm
  set blob [::th8testlib::kv get "th8:secure:_slt_gcm"]
  set flip [expr {[string length $blob] - 1}]
  binary scan [string index $blob $flip] c b
  set blob [string replace $blob $flip $flip \
      [binary format c [expr {$b ^ 0xFF}]]]
  ::th8testlib::kv set "th8:secure:_slt_gcm" $blob
  secure delete _slt_gcm
} -body {
  list [catch {secure load _slt_gcm} msg] $msg
} -cleanup {
  catch {::th8testlib::kv unset "th8:secure:_slt_gcm"}
  catch {secure delete _slt_gcm}
  ::th8testlib::secure_persist disable
  unset -nocomplain msg blob flip b
} -result {1 {secure load: authentication failed (wrong master key or corrupted data)}}}

###############################################################################

source tests/epilogue.tcl
