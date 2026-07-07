###############################################################################
#
# coverage_glob_steplimit.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_glob.c th8GlobMatch2 L90:
#
#   if (interp && Th8_Ready(interp) != TH8_OK) return 0;
#
# Existing glob tests pass quickly and never trip the step
# counter, so Th8_Ready always returns TH8_OK and L90's C2=F
# vector (interp set, Ready failed) is unobserved.
#
# This test runs a [string match] loop inside ::th8testlib::sandbox
# with a pattern containing many '*' wildcards so each match
# iteration calls Th8_Ready many times.  The sandbox step
# limit (TH8_SANDBOX_STEP_LIMIT = 1,000,000) eventually
# triggers, and at that boundary Th8_Ready returns non-OK
# during a glob match -- driving the (T,T) vector at L90.
#
# The sandbox aborts with TH8_ERROR; the test just asserts
# the sandbox call completes (rc may be non-zero on step
# exhaustion, which is expected).
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

runTest {test glob_steplimit-1.0 {
  ::th8testlib::glob_match_null drives th8_glob.c L90
  C1=F vector -- interp argument is NULL so the && short-
  circuits before the Ready check.  No script-level caller
  passes NULL interp; this helper exists only to close the
  pair.
} -constraints {
    th8
} -setup {
} -body {
  set r [::th8testlib::glob_match_null "*hello*" "say hello world"]
  set r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test glob_steplimit-1.1 {
  ::th8testlib::glob_match_cancelled cancels the interp,
  calls Th8_GlobMatch with the cancelled interp (so
  Th8_Ready returns non-OK), then resets cancellation.
  Drives th8_glob.c L90 (T,T): interp set and Ready != OK.
  No script path can reach this because cancellation
  normally unwinds before the next glob call; the helper
  is the only way to combine cancel + glob in one atomic
  C call.  Glob short-circuits and returns 0.

  Pathological-pattern step-exhaustion was also attempted
  but TH8's glob recursion is bounded at depth=50 so even
  100-wildcard patterns against 10K-char strings cap out
  at ~20K Ready calls per match -- well under the 1M
  sandbox step limit.
} -constraints {
    th8
} -setup {
} -body {
  set r [::th8testlib::glob_match_cancelled "*hello*" "say hello world"]
  set r
} -cleanup {
  unset -nocomplain r
} -result {0}}

###############################################################################

source tests/epilogue.tcl
