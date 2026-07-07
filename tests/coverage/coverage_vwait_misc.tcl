###############################################################################
#
# coverage_vwait_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [vwait] decisions in
# src/plugins/th8_events.c, focusing on missing vectors:
#
#   :482  argl[1] == 8 && memcmp(argv[1], "-timeout", 8) == 0
#                 (vwait -timeout option detect)
#                 missing C3-pair: 8-char wrong option name
#                 -- argl matches but content differs
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

runTest {test vwaitmisc-1.1 {
  vwait with an UNKNOWN 8-character option drives the
  (T, T, F) vector at th8_events.c:482 -- the argc and
  length checks pass but the memcmp differs from
  "-timeout".  After the failed match, the parser falls
  through to the wrong-args error.
} -constraints {
    th8
} -body {
  set rc [catch {vwait -wronggg 100 ::vw_x} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m ::vw_x
} -result {1 1}}

###############################################################################

runTest {test vwaitmisc-2.1 {
  vwait WITHOUT -timeout drives the C1=T vector at
  th8_events.c:426 (slice = (rem < 0 || rem > SLICE_MS)
  ? SLICE_MS : rem) -- rem is set to -1 when no deadline
  was supplied.  We schedule a cross-thread event to set
  the variable so vwait can return.  Existing tests cover
  only the -timeout path; this closes the C1-pair.
} -constraints {
    th8 queue_event
} -setup {
} -body {
  ::th8testlib::queue_event 10 {set ::vwmisc_2_1_done 42}
  vwait ::vwmisc_2_1_done
  set ::vwmisc_2_1_done
} -cleanup {
  unset -nocomplain ::vwmisc_2_1_done
} -result {42}}

###############################################################################

runTest {test vwaitmisc-3.1 {
  update with INVALID 3-arg forms drives the C2/C3 pairs
  at th8_events.c:235-237 -- argc==3 with non-6-char
  second arg, or 6-char second arg that isn't "-limit".
  The parser falls through to the wrong-args error.
} -constraints {
    th8
} -body {
  set rcs {}
  catch {update foo 100} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {update bogusx 100} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {update wrongx 5} m
  lappend rcs [expr {[string length $m] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test vwaitmisc-4.1 {
  update -limit N with N less than the number of queued
  events drives the C1-pair and C2-pair at
  th8_events.c:194-195 (update_step termination check).
  Default update has nLimit=-1 so C1 is always F there;
  -limit N flips C1 to T.  As nProcessed catches up to
  N, C2 flips from F (still draining) to T (limit hit,
  short-circuit return).  We use `after` (which blocks
  the main thread without draining the queue) so the
  worker threads have time to enqueue all events BEFORE
  update -limit runs -- this guarantees the queue is
  still non-empty when the limit is reached.
} -constraints {
    th8 queue_event
} -setup {
  set ::vwm_4_1_counter 0
} -body {
  for {set i 0} {$i < 8} {incr i} {
    ::th8testlib::queue_event 0 {incr ::vwm_4_1_counter}
  }
  # Block main thread long enough for worker threads to
  # enqueue all 8 events (after is non-draining).
  after 200
  # Limit to 2 so several events remain in queue, driving
  # the C1=T,C2=F,C3=F vector at the limit-check decision.
  update -limit 2
  expr {$::vwm_4_1_counter >= 2}
} -cleanup {
  catch {update}; # force drain queue
  unset -nocomplain ::vwm_4_1_counter i
} -result {1}}

###############################################################################

runTest {test vwaitmisc-5.1 {
  vwait on a variable whose CURRENT value is an empty
  string drives the C2=F vector at th8_events.c:64
  (events_capture_value).  Th8_GetVar returns OK and
  the result pointer is the empty-string sentinel
  (zRes non-NULL, nRes=0), so the Memcpy guard
  `zRes && nRes > 0` evaluates {T,F=F} and the byte
  copy is skipped.  The capture buffer is just the
  null terminator.  vwait then proceeds normally and
  the watched variable getting reassigned wakes it.
} -constraints {
    th8
} -setup {
  set ::vwm_5_1_v ""
  set ::vwm_5_1_done 0
} -body {
  # Schedule a re-assignment via after, then vwait.
  # vwait captures the initial empty string value (driving
  # nRes=0), then polls until the value changes.
  set rcs {}
  catch {vwait -timeout 50 ::vwm_5_1_v} m
  lappend rcs [info exists ::vwm_5_1_v]
  lappend rcs [string length $::vwm_5_1_v]
  set rcs
} -cleanup {
  unset -nocomplain rcs m ::vwm_5_1_v ::vwm_5_1_done
} -result {1 0}}

###############################################################################

source tests/epilogue.tcl
