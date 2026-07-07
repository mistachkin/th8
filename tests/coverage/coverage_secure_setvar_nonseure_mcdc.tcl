###############################################################################
#
# coverage_secure_setvar_nonseure_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/crypto/th8_secure.c L1339
# in `th8SecureSetVar` (which fires on EVERY `set` once
# the secure-var hash exists):
#
#   if (!pEntry || !pEntry->pData) return TH8_OK;
#
# 2-cond OR.  Pre-test report: 0% MC/DC -- only (F,F)
# observed (calls for existing secure vars where the
# hash lookup succeeds with valid pData).  C1-Pair
# (T,-) requires the paSecure hash to exist AND a
# non-secure var to be `set` -- the lookup returns
# pEntry=NULL, C1=T short-circuits, returns OK without
# doing the secure-var work.  The existing secure.tcl
# tests don't mix `secure create` with plain `set` in
# the same interp.
#
# C2-Pair (F,T) is the tombstone scenario per the
# Th8_HashFind-actually-deletes finding in batch #24's
# investigation -- intrinsic-dead unless an orphan
# create=1 entry without pData can be manufactured.
# Documented as cat2 intrinsic-dead-in-scope.
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

runTest {test secure_setvar_nonsecure-1.1 {
  th8_secure.c L1339 (T,-) -- after creating a secure
  var (populates paSecure hash), set a different
  non-secure var.  th8SecureSetVar's L1338 HashFind
  for the non-secure name returns NULL; L1339 C1=T
  short-circuits, returns OK without secure-var
  bookkeeping.  Verify the non-secure var was set.
} -constraints {
    th8 crypto_testlib
} -setup {
} -body {
  secure create _setvar_nsec_sec "secret"
  set _setvar_nsec_plain "plaintext"
  set r [list [info exists _setvar_nsec_plain] $_setvar_nsec_plain]
  secure delete _setvar_nsec_sec
  unset -nocomplain _setvar_nsec_plain
  set r
} -cleanup {
  unset -nocomplain r
  catch {secure delete _setvar_nsec_sec}
  unset -nocomplain _setvar_nsec_plain
} -result {1 plaintext}}

###############################################################################

source tests/epilogue.tcl
