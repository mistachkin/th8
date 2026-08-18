###############################################################################
#
# coverage_create_command_oom.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# TH8K-005 / project-wide transactional-semantics rule: Th8_CreateCommand is
# 100% transactional for a QUALIFIED new name under allocation failure -- if any
# staged allocation fails, ALL incomplete state changes (including the
# intermediate namespaces the qualified name required) are rolled back, leaving
# state exactly as before the call.  ::th8testlib::create_command_oom sweeps a
# fault across every allocation trip of `Th8_CreateCommand("::txa::txb::txc::
# probe", ...)` in a child interpreter and, whenever the fault fired, asserts the
# call FAILED and left ZERO residue -- in particular the top namespace ::txa must
# not linger (a leftover empty namespace is the TH8K-005 defect).  It returns the
# 6-tuple {persFired persErr persViol osFired osErr osViol}.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test create_command_oom-1.1 {
  R-12425-26970: Th8_CreateCommand qualified-name registration is transactional
  under OOM -- a fired fault at any trip leaves NO residue (no half-created
  command, no leftover namespace hierarchy).  Both the persistent and one-shot
  sweeps must report zero violations (indices 2 and 5).
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::create_command_oom]
  list [lindex $r 2] [lindex $r 5]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

runTest {test create_command_oom-1.2 {
  R-12425-26970: the sweep is NON-VACUOUS -- the persistent fault actually fired
  at some trips and Th8_CreateCommand correctly failed (returned TH8_ERROR) on
  each, so the zero-violation result above reflects real fault coverage rather
  than a fault that never triggered.
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::create_command_oom]
  expr {[lindex $r 0] > 0 && [lindex $r 1] > 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
