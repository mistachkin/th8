###############################################################################
#
# coverage_bug25_diag.tcl --
#
# Tcl Language Standard
# Regression Test File for Bug 25
#
# Bug 25 was: secure variables decrypted via Th8_GetVar in a child
# interpreter failed at EVP_DecryptUpdate (AAD step) with an empty
# error message.  Root cause (found 2026-06-07): callers that pass
# nVar == TH8_NOLEN to th8SecureGetVar / th8SecureSetVar were not
# normalised before the value flowed into th8SecureDecrypt /
# th8SecureEncrypt; the AAD-length cast `(int)nName` produced a
# huge negative int, and OpenSSL correctly rejected the AAD with
# error 0x030000DD "invalid length".  The fix normalises nVar /
# nNewVal / nPlain / nName via Th8_Strlen() in the get/set entries
# AND in encrypt/decrypt themselves (defense in depth).
#
# This test uses the ::th8testlib::bug25_diag helper which spins
# up a child interp, installs a master key, runs `secure create
# ::auto_path /tmp`, and reads it back with Th8_GetVar.  Expected
# result post-fix: {0 {} 0 /tmp}.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test bug25-secure-child-interp-1.1 {
  Secure variable create + get in a child interpreter, exercising
  the path that previously failed at the AAD EVP_DecryptUpdate
  due to TH8_NOLEN flowing unnormalised into the int cast.
  Post-fix the chain succeeds and the decrypted plaintext
  matches the originally encrypted value.
} -constraints {
    th8 crypto_testlib
} -body {
  ::th8testlib::bug25_diag
} -result {0 {} 0 /tmp}}

###############################################################################

source tests/epilogue.tcl
