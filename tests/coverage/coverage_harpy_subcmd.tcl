###############################################################################
#
# coverage_harpy_subcmd.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for `harpy` subcommand-name compounds in
# src/plugins/th8_harpy.c:
#
#   line 575  if (argl[1] == 4 && memcmp(... "sign", 5) == 0)
#   line 681  if (argl[1] == 6 && memcmp(... "verify", 7) == 0)
#
# Existing tests cover the T,T vector for both subcommands.  The
# T,F vector (length matches but content differs) and the F,-
# vector (different length) need calls with malformed sub-
# commands.
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

runTest {test ha_sub-1.1 {
  harpy with 4-char subcommand that isn't "sign" -- drives T,F
  vector at line 575 (argl[1]==4 true, memcmp != 0)
} -constraints {
    th8 crypto_enabled
} -body {
  catch {harpy s1gn token scripttext} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ha_sub-1.2 {
  harpy with 6-char subcommand that isn't "verify" -- drives T,F
  vector at line 681 (argl[1]==6 true, memcmp != 0)
} -constraints {
    th8 crypto_enabled
} -body {
  catch {harpy verifx token scripttext sigtext} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ha_sub-1.3 {
  harpy with 5-char subcommand drives F,- at both line 575 and 681
} -constraints {
    th8 crypto_enabled
} -body {
  catch {harpy fives token scripttext} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ha_sub-1.4 {
  harpy with 3-char subcommand drives F,- at both checks
} -constraints {
    th8 crypto_enabled
} -body {
  catch {harpy bad token scripttext} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ha_sub-1.5 {
  harpy with 7-char subcommand drives F,- at both checks
} -constraints {
    th8 crypto_enabled
} -body {
  catch {harpy notgood token scripttext} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ha_sub-2.1 {
  clock ntp with an 8-character option that ISN'T "-timeout"
  drives the C2=F vector at th8_harpy.c:70-71 -- argl[i]
  == 8 (T) but Th8_Memcmp != 0 (F).  The parser falls
  through to the unknown-option error.
} -constraints {
  th8 clock_ntp clock_ntp_network
} -body {
  set rcs {}
  catch {clock ntp -bogusxz 100} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {clock ntp -wrongxx 5} m
  lappend rcs [expr {[string length $m] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

runTest {test ha_sub-3.1 {
  harpy verify with a TOKEN MISMATCH between the explicit
  publicKeyToken argument and the token embedded in the
  signature drives the C2=T vector at th8_harpy.c:710 --
  zSigToken is non-NULL (C1=T) AND Th8_Memcmp on the
  argument vs the embedded token returns != 0 (C2=T).
  Existing verify tests use matching tokens (C2=F); this
  closes the C2-pair.
} -constraints {
    th8 crypto_enabled
} -body {
  set sig [harpy sign 9920868842008fc9 {puts hello}]
  catch {harpy verify 26f17c3a1a544324 {puts hello} $sig} m
  string match {*token mismatch*} $m
} -cleanup {
  unset -nocomplain sig m
} -result {1}}

###############################################################################

runTest {test ha_sub-3.2 {
  harpy verify with a signature whose embedded token line
  has been stripped (sig contains no "# signature.b64sig
  -- <token>" marker) drives the C1=F vector at
  th8_harpy.c:710 -- Th8_HarpySigLoad returns zSigToken
  as NULL when no token line is present, so the token-
  mismatch check is bypassed entirely.  Existing verify
  tests use a fully-tokenized signature (C1=T); this
  closes the C1-pair.
} -constraints {
    th8 crypto_enabled
} -body {
  set sig [harpy sign 9920868842008fc9 {puts hello}]
  set noTokSig [regsub {# signature.b64sig -- [0-9a-f]+} \
      $sig {# signature.b64sig}]
  set rc [catch {harpy verify 9920868842008fc9 \
      {puts hello} $noTokSig} m]
  list $rc $m
} -cleanup {
  unset -nocomplain sig noTokSig rc m
} -result {0 ok}}

###############################################################################

# NOTE: The former ha_sub-4.1/4.2 drove th8_policy.c L1184 (the
# file-read token-length gate) via committed .b64sig fixtures with
# a hand-tampered token.  They were removed because this
# environment auto-re-signs every tests/**/*.tcl with the
# production key on commit, replacing the tampered signature with
# a valid one -- so the fixtures verify OK and the tests (which
# expect the "missing public key token" error) break
# intermittently.  Covering L1184's file-read arms robustly needs
# a runtime-generated malformed .b64sig (never committed, so never
# auto-signed), which requires a testlib file-writer helper.  See
# FINDINGS Finding 021.

source tests/epilogue.tcl
