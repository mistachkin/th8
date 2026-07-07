###############################################################################
#
# coverage_harpy_sigload_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for Th8_HarpySigLoad in
# src/plugins/harpy/th8_harpy.c.  The function is public
# TH8_API but only ever called from policy.c on the well-
# formed signed-file path, so its defensive entry guards and
# parser-edge branches stay 0% covered:
#
#   L103: if (!zData || !ppSig || !pnSig)   -- 3-cond OR
#   L132: while (i < nData && zData[i] != '\n')  -- 2-cond AND
#         (C1=F branch when buffer ends without \n)
#   L232: if (!zB64 || nB64 == 0)           -- 2-cond OR
#
# Direct call via the new ::th8testlib::harpysigload helper
# lets us drive each vector with one-line synthetic inputs.
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

runTest {test harpysigload-1.1 {
  Th8_HarpySigLoad L103 (T,_,_) -- zData==NULL drives the
  C1=T short-circuit of `if (!zData || !ppSig || !pnSig)`.
  Expect TH8_ERROR with "Harpy: invalid arguments".
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::harpysigload nulldata]
  list [lindex $r 0] [string match "*Harpy: invalid arguments*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test harpysigload-1.2 {
  Th8_HarpySigLoad L103 (F,T,_) -- ppSig==NULL drives the
  C2=T short-circuit.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::harpysigload nosig]
  list [lindex $r 0] [string match "*Harpy: invalid arguments*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test harpysigload-1.3 {
  Th8_HarpySigLoad L103 (F,F,T) -- pnSig==NULL drives the
  C3=T vector.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::harpysigload nolen]
  list [lindex $r 0] [string match "*Harpy: invalid arguments*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test harpysigload-2.1 {
  Th8_HarpySigLoad emptybody -- valid header but no base64
  body.  Reaches the L232 `if (!zB64 || nB64 == 0)` empty-
  body guard and reports "no base64 signature data found".
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::harpysigload emptybody]
  list [lindex $r 0] [string match "*no base64 signature data found*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test harpysigload-2.2 {
  Th8_HarpySigLoad nonewline -- header line without a
  trailing newline.  Forces the L132 while-loop to exit on
  i >= nData (C1=F) rather than on the \n test, closing the
  C1-Pair.  No body bytes either, so the call still returns
  TH8_ERROR via the L232 empty-body guard.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::harpysigload nonewline]
  list [lindex $r 0]
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test harpysigload-3.1 {
  Th8_HarpySigLoad basic -- 3-comment header with a
  "name -- TOKEN" line plus a base64 body.  Drives the
  comment-loop iteration count past 3, the L151 line-3
  token extraction path, and the body collector/base64
  decode.  Expect TH8_OK with non-empty signature output.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::harpysigload basic]
  lindex $r 0
} -cleanup {
  unset -nocomplain r
} -result {0}}

###############################################################################

source tests/epilogue.tcl
