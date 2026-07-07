###############################################################################
#
# coverage_sensitive_release.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c th8ReleaseOldResult L4886:
#
#   if (interp->bResultSensitive && interp->zResult) {
#       Th8_SecureZero(...);
#       interp->bResultSensitive = 0;
#   }
#
# Only (F,-) is exercised by ordinary tests: every call to
# Th8_SetResult eventually releases the prior result, but
# bResultSensitive is false unless the caller explicitly marked
# the result sensitive AND that result lives in regular heap (not
# the PROTECTED region used by [secure] variables, which takes
# an earlier branch).  No script-level API combines
# Th8_SetResult + Th8_MarkResultSensitive atomically before the
# result is released, so a C helper is required.
#
# ::th8testlib::mark_sensitive_release performs all three steps
# inside one C call: heap-allocate via Th8_SetResult, mark
# sensitive via Th8_MarkResultSensitive, then overwrite via
# Th8_SetResultStatic.  The overwrite invokes
# th8ReleaseOldResult with bResultSensitive=T and zResult!=NULL,
# driving the (T,T) MC/DC vector.
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

runTest {test sens_release-1.1 {
  ::th8testlib::mark_sensitive_release drives the (T,T)
  vector at th8_core.c L4886 -- secure-zero-on-release of a
  heap-allocated sensitive result.  Returns "ok" if the
  three internal steps (heap SetResult, Mark, overwrite)
  completed.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [::th8testlib::mark_sensitive_release]
  set r
} -cleanup {
  unset -nocomplain r
} -result {ok}}

###############################################################################

runTest {test sens_release-1.2 {
  ::th8testlib::mark_sensitive_release empty drives the
  (T,F) vector at th8_core.c L4886: bResultSensitive=T but
  zResult=NULL.  Achieved by clearing the result first
  (Th8_SetResult with NULL), then marking sensitive, then
  overwriting.  The C1=T path enters the if but C2=F
  short-circuits the && so SecureZero is not called.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [::th8testlib::mark_sensitive_release empty]
  set r
} -cleanup {
  unset -nocomplain r
} -result {ok-empty}}

###############################################################################

source tests/epilogue.tcl
