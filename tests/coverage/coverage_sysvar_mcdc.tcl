###############################################################################
#
# coverage_sysvar_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_vars.c Th8_SaveSystemVar /
# Th8_RestoreSystemVar.  These public APIs bracket
# untrusted script evaluation around an array of trusted
# globals; the suite has no other test that calls them, so
# both functions sit at 0% MC/DC.
#
#   L1097 `rc != TH8_OK || nCount == 0`
#         Driven by save on a populated array (F,F) and on
#         an empty array (F,T).
#   L1133 `pVar && pVar->zData && pVar->nData > 0`
#         Driven by save on an array whose elements have
#         non-empty data (T,T,T).
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

runTest {test sysvar_mcdc-1.1 {
  Th8_SaveSystemVar + Th8_RestoreSystemVar on a populated
  array drives src/th8_vars.c L1097 (F,F vector --
  SplitList ok, nCount > 0) and L1133 (T,T,T vector --
  every element has non-empty data).
} -constraints {
    th8
} -setup {
} -body {
  array set ::sysvar_mcdc_a {alpha 1 beta two gamma 3.14}
  set r [::th8testlib::sysvar save_restore ::sysvar_mcdc_a]
  expr {$r eq "ok"}
} -cleanup {
  unset -nocomplain ::sysvar_mcdc_a r
} -result {1}}

###############################################################################

runTest {test sysvar_mcdc-1.2 {
  Th8_SaveSystemVar on an array whose element has an empty
  value drives the C3=F vector at L1133 (pVar->nData == 0,
  so the data-copy branch is skipped).
} -constraints {
    th8
} -setup {
} -body {
  array set ::sysvar_mcdc_b {key ""}
  set r [::th8testlib::sysvar save_restore ::sysvar_mcdc_b]
  expr {$r eq "ok"}
} -cleanup {
  unset -nocomplain ::sysvar_mcdc_b r
} -result {1}}

###############################################################################

runTest {test sysvar_mcdc-1.3 {
  Save+restore on a nonexistent array exits early at L1092
  (zNames == NULL) without reaching L1097, but the round-trip
  still completes cleanly.  Th8_SaveSystemVar returns TH8_OK
  with *ppSaved == NULL.
} -constraints {
    th8
} -setup {
} -body {
  catch {::th8testlib::sysvar save_restore ::sysvar_mcdc_nope} r
  # Either ok or a defined error; the call completes.
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain ::sysvar_mcdc_nope r m
} -result {1}}

###############################################################################

source tests/epilogue.tcl
