###############################################################################
#
# ctor_globals_oom.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Probative one-shot-OOM regression test for the constructor's all-or-nothing
# global-variable initialization (TH8K-003).  th8InitGlobals builds
# ::tcl_platform(source) and ::tcl_platform(compileOptions) with Th8_ListAppend,
# whose OOM path returns TH8_ERROR.  The defect was that those returns were
# ignored, so a TRANSIENT (one-shot) allocation failure could truncate a list
# and then a later Th8_SetVar succeed -- publishing a short list with a success
# return.  The persistent fail-after injector cannot expose this (the later
# Th8_SetVar also fails); ::th8testlib::ctor_globals_oneshot uses a ONE-SHOT
# injector that fails exactly one allocation and then resumes, sweeping the trip
# across every constructor-global allocation of both Th8_CreateInterp and
# Th8_RestoreInterp(TH8_RESTORE_VARIABLES).  th8-constrained: driven entirely by
# the TH8 C-API construction path via testlib, with no reference-Tcl equivalent.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test ctor_globals_oom-1.1 {
  TH8K-003: constructor global initialization is all-or-nothing under a
  transient one-shot allocation failure.  ::th8testlib::ctor_globals_oneshot
  sweeps a one-shot OOM across every allocation of Th8_CreateInterp and of
  Th8_RestoreInterp(TH8_RESTORE_VARIABLES), returning
  {createFired createErr createViol restoreFired restoreErr restoreViol}.  A
  "viol" is a case where the call SUCCEEDED (non-NULL interp / TH8_OK) yet
  published a TRUNCATED ::tcl_platform(source) or (compileOptions) list.  Both
  violation counts must be zero: a Th8_ListAppend OOM now aborts the whole
  initialization instead of publishing a short list.
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::ctor_globals_oneshot]
  list [lindex $r 2] [lindex $r 5]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

runTest {test ctor_globals_oom-1.2 {
  The one-shot sweep is non-vacuous: in BOTH the create and restore sweeps the
  injected fault actually fired and drove the all-or-nothing failure path (a
  NULL interp from Th8_CreateInterp, a TH8_ERROR from Th8_RestoreInterp) at
  least once.  This guards against a future change that stops the fault from
  landing -- which would make the zero violation counts in ctor_globals_oom-1.1
  pass for the wrong reason.
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::ctor_globals_oneshot]
  list [expr {[lindex $r 0] > 0}] [expr {[lindex $r 1] > 0}] \
       [expr {[lindex $r 3] > 0}] [expr {[lindex $r 4] > 0}]
} -cleanup {
  unset -nocomplain r
} -result {1 1 1 1}}

###############################################################################

source tests/epilogue.tcl
