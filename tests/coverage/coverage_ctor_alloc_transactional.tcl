###############################################################################
#
# coverage_ctor_alloc_transactional.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# TH8K-002: interpreter construction is COMPLETE-OR-NOTHING at EVERY allocation
# trip point -- the constructor half of the project-wide transactional rule.
# ::th8testlib::ctor_alloc_transactional sweeps a one-shot xMalloc fault across
# the whole construction allocation sequence.  The correct transactional invariant
# is complete-or-nothing (NOT "fired => NULL"): whenever the fault fired,
# Th8_CreateInterp must return either NULL or a FULLY COMPLETE interpreter -- never
# one with truncated PUBLISHED state.  A survived-with-fault interp is legitimate
# ONLY when the failed allocation was an optional internal-rep cache-warming or
# transient scratch buffer (Th8_FindInCache / Th8_StringAppend / Eagle_JoinList,
# reached while th8InitGlobals builds the source/compileOptions lists), whose miss
# leaves the correct value and a cold cache -- a valid complete state.  On every
# survived trip the command asserts the interp is complete: both list globals have
# their exact element counts AND the eager hashes are functional (a global can be
# set/read -> paVar and a command registered -> paCmd + lazy paCmdToken).  It
# returns {fired viol survived}; viol must be 0 (no truncated published state),
# fired > 0 (sweep reached real allocations), and survived > 0 (at least one
# optional-allocation fault was tolerated -- non-vacuity of the completeness
# check).  This complements ctor_globals_oneshot (the th8InitGlobals list-globals
# stage) by exercising the WHOLE constructor and proving the eager hashes are
# usable, not merely present.
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

runTest {test ctor_alloc_transactional-1.1 {
  Interpreter construction is complete-or-nothing: whenever a one-shot allocation
  fault fires during Th8_CreateInterp, it returns either NULL or a FULLY COMPLETE
  interpreter (never one with truncated published state -- short list globals or a
  non-functional eager hash).  The violation count (index 1) must be 0, the fault
  must have fired (index 0 > 0), and at least one fault must have been tolerated by
  an optional-allocation site (index 2 > 0), proving the completeness check is
  non-vacuous (TH8K-002).
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::ctor_alloc_transactional]
  list [expr {[lindex $r 1] == 0}] [expr {[lindex $r 0] > 0}] \
      [expr {[lindex $r 2] > 0}]
} -cleanup {
  unset -nocomplain r
} -result {1 1 1}}

###############################################################################

source tests/epilogue.tcl
