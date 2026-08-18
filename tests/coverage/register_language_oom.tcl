###############################################################################
#
# register_language_oom.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Probative OOM regression test for Th8_RegisterLanguage's all-or-nothing
# contract (TH8K-006).  Registration must return TH8_ERROR -- never TH8_OK with
# a partial, inconsistent language -- if any registration allocation fails.  The
# reopened TH8K-006 defects were that the security-array reset ignored its seven
# Th8_SetVar results and the ensemble force-init loop discarded every eval error;
# the fixes also surfaced Bug 85 (unchecked create=1 Th8_HashFind / Th8_Strdup in
# the package subsystem and Th8_DeclareSystemVar).  ::th8testlib::register_language_oom
# runs two sweeps over Th8_RegisterLanguage: a PERSISTENT one (fail an allocation
# and every later one -> registration must return TH8_ERROR whenever the fault
# fired; the all-or-nothing assertion) and a ONE-SHOT one (fail exactly one
# allocation then resume -> crash detection for the Bug 85 class).  th8-constrained:
# driven entirely by the TH8 C-API registration path via testlib.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test register_language_oom-1.1 {
  TH8K-006: Th8_RegisterLanguage is all-or-nothing under a persistent OOM.
  ::th8testlib::register_language_oom returns
  {persFired persErr persViol osFired osErr osViol}.  In the PERSISTENT sweep a
  failed allocation and every later one fail, so whenever the fault fired
  registration MUST return TH8_ERROR; persViol counts the fired-yet-TH8_OK
  (partial-language) violations and must be zero.
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::register_language_oom]
  lindex $r 2
} -cleanup {
  unset -nocomplain r
} -result {0}}

###############################################################################

runTest {test register_language_oom-1.2 {
  The sweeps are non-vacuous: the persistent sweep actually fired and drove the
  TH8_ERROR path at least once, and the one-shot (crash-detection) sweep also
  fired and ran to completion -- a Bug-85-class unchecked-NULL dereference under
  a transient OOM would abort the process here instead of returning counts.
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::register_language_oom]
  list [expr {[lindex $r 0] > 0}] [expr {[lindex $r 1] > 0}] \
       [expr {[lindex $r 3] > 0}]
} -cleanup {
  unset -nocomplain r
} -result {1 1 1}}

###############################################################################

runTest {test register_language_oom-2.1 {
  R-31220-39790 / TH8K-006/-020: the public Th8_ResetSecurityArray now REPORTS
  an allocation failure (int return) instead of silently swallowing it (its old
  void contract, which could hand a caller a partial security array).
  ::th8testlib::reset_security_oom registers a fresh child (so ::th8_security
  exists as a seven-element system var) and sweeps a fail-after OOM across the
  reset's seven Th8_SetVar writes, asserting that whenever a write was actually
  failed the call returned TH8_ERROR (never TH8_OK -- reverting the fix makes a
  fired-yet-TH8_OK case appear and FAILs), that a disarmed reset returns TH8_OK,
  and that the fault fired at least once (non-vacuous).  Returns "ok" on success
  or a FAIL:/skip: diagnostic.
} -constraints {
    th8
} -body {
  ::th8testlib::reset_security_oom
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

source tests/epilogue.tcl
