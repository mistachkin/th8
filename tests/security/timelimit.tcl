###############################################################################
#
# timelimit.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Red-team tests for the wall-clock deadline (TH8K-010): Th8_SetTimeLimitMs
# / Th8_SetDeadline bound an interpreter's REAL run time, not just its step
# count.  Exercised via ::th8testlib::timelimit MS SCRIPT, which evaluates
# SCRIPT in a child that has a deadline of MS milliseconds and NO step limit,
# so a compute-bound loop is stopped by the TIME limit.  th8-constrained: the
# deadline is a TH8 C-API resource limit with no reference-Tcl equivalent.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test timelimit-1.1 {
  A compute-bound loop that would run forever is stopped by the
  wall-clock deadline with "time limit exceeded" -- the TIME limit, not
  the step counter (the child has no step limit).
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::timelimit 50 { while {1} { set x 1 } }]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {1 {time limit exceeded}}}

###############################################################################

runTest {test timelimit-1.2 {
  A fast script completes normally within a generous deadline (the
  deadline is a ceiling, not a spurious killer).
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::timelimit 5000 { expr {6 * 7} }]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {0 42}}

###############################################################################

runTest {test timelimit-1.3 {
  A bounded loop of known size finishes within a generous deadline,
  returning its exact result (not cut off).
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::timelimit 5000 {
    set sum 0
    for {set i 0} {$i < 1000} {incr i} { incr sum $i }
    set sum
  }]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {0 499500}}

###############################################################################

runTest {test timelimit-1.4 {
  A deadline of 0 means no limit: a compute-bound but bounded script
  runs to completion (0 does not arm a deadline).
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::timelimit 0 {
    set n 0
    for {set i 0} {$i < 5000} {incr i} { incr n }
    set n
  }]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {0 5000}}

###############################################################################

runTest {test timelimit-2.1 {
  MEASURED worst-case deadline latency (TH8K-009): a compute-bound infinite loop
  under a 100 ms cooperative deadline stops with a BOUNDED wall-clock elapsed
  time -- it must not run unbounded.  ::th8testlib::deadline_overshoot measures
  the actual elapsed milliseconds via the platform clock; it must be within
  [50, 800] ms (at least half the deadline, and well under a runaway) -- a wide
  band so a loaded CI machine does not flake while still proving the deadline is
  honored promptly.  Skips where the platform has no monotonic clock.
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain e
} -body {
  set e [::th8testlib::deadline_overshoot 100]
  expr {$e eq "skip:no-clock" || ($e >= 50 && $e < 800)}
} -cleanup {
  unset -nocomplain e
} -result {1}}

###############################################################################

source tests/epilogue.tcl
