###############################################################################
#
# event.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for [update] and [vwait], the script-level event-loop
# commands.  Events are queued cross-thread by an embedder via
# the public Th8_QueueEvent C API; the test-only command
# [::th8testlib::queue_event TIME_MS SCRIPT] wraps that path
# (POSIX only — gates on the queue_event constraint).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- [update]: basic drain semantics
#
###############################################################################

runTest {test event-1.1 {
  R-05853-51502: [update] with no pending events is a no-op
} -constraints {
    th8 queue_event
} -body {
  update
} -result {}}

###############################################################################

runTest {test event-1.2 {
  R-05853-51502: a worker thread can enqueue an event that [vwait] then drains
} -constraints {
    th8 queue_event
} -setup {
} -body {
  ::th8testlib::queue_event 10 {set done 42}
  vwait -timeout 1000 done
  set done
} -cleanup {
  unset -nocomplain done
} -result {42}}

###############################################################################

runTest {test event-1.3 {
  R-50111-34183: [vwait] drains pending events while waiting; the
  ordering between worker-thread enqueues is non-deterministic,
  but FIFO ordering of the QUEUE itself ensures every queued
  event runs before [vwait] returns
} -constraints {
    th8 queue_event
} -setup {
} -body {
  set counter 0
  # Queue several events.  Each worker thread sleeps a tiny
  # amount before calling Th8_QueueEvent so that all five
  # worker threads have a chance to be in their sleep loop
  # before any of them actually enqueues.
  for {set i 0} {$i < 5} {incr i} {
    ::th8testlib::queue_event 0 {incr counter}
  }
  # Sentinel: queued last (after a longer delay) so it
  # arrives in the queue strictly AFTER the 5 above.  When
  # [vwait] returns on `waited`, all 5 prior events must
  # have already fired (FIFO drain order).
  ::th8testlib::queue_event 100 {set waited "done"}
  vwait -timeout 10000 waited
  set counter
} -cleanup {
  unset -nocomplain counter waited i
} -result {5}}

###############################################################################
#
# Section 2 -- [vwait]: variable-signal detection
#
###############################################################################

runTest {test event-2.1 {
  R-50111-34183: [vwait] fires when a queued event creates the variable
} -constraints {
  th8 queue_event
} -setup {
} -body {
  ::th8testlib::queue_event 10 {set v "created"}
  vwait -timeout 1000 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {created}}

###############################################################################

runTest {test event-2.2 {
  R-50111-34183: [vwait] fires when a queued event changes the variable
} -setup {
  set v "old"
} -constraints {
  th8 queue_event
} -body {
  ::th8testlib::queue_event 10 {set v "new"}
  vwait -timeout 1000 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {new}}

###############################################################################

runTest {test event-2.3 {
  R-50111-34183: [vwait] fires when a queued event unsets the variable
} -constraints {
  th8 queue_event
} -setup {
  set v "live"
} -body {
  ::th8testlib::queue_event 10 {unset v}
  vwait -timeout 1000 v
  info exists v
} -cleanup {
  unset -nocomplain v
} -result {0}}

###############################################################################

runTest {test event-2.4 {
  R-54984-57209: [vwait] on an array element fires on element write
} -constraints {
  th8 queue_event
} -setup {
  array set a {x 1}
} -body {
  ::th8testlib::queue_event 10 {set a(x) 99}
  vwait -timeout 1000 a(x)
  set a(x)
} -cleanup {
  unset -nocomplain a
} -result {99}}

###############################################################################

runTest {test event-2.5 {
  R-11279-35358: [vwait -timeout MS] errors with "vwait: timeout" if nothing fires
} -setup {
} -body {
  set rc [catch {vwait -timeout 50 nothingHappens} msg]
  list $rc [string match {*timeout*} $msg]
} -cleanup {
  unset -nocomplain msg rc
} -result {1 1}}

###############################################################################

runTest {test event-2.6 {
  R-50111-34183: re-wait after one [vwait] succeeds — second wait sees only the next signal
} -constraints {
  th8 queue_event
} -setup {
} -body {
  ::th8testlib::queue_event 10 {set v "first"}
  vwait -timeout 1000 v
  set firstResult $v
  ::th8testlib::queue_event 10 {set v "second"}
  vwait -timeout 1000 v
  list $firstResult $v
} -cleanup {
  unset -nocomplain v firstResult
} -result {first second}}

###############################################################################
#
# Section 3 -- [update] / [vwait]: error semantics
#
###############################################################################

runTest {test event-3.1 {
  R-48121-08664: [update -limit 0] is rejected as a degenerate value
} -setup {
} -body {
  list [catch {update -limit 0} msg] [string match {*>= 1*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test event-3.2 {
  R-50111-34183: [vwait] with no args raises wrong # args
} -setup {
} -body {
  list [catch {vwait} msg] [string match {*wrong # args*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test event-3.3 {
  R-11279-35358: [vwait -timeout abc varName] rejects non-integer timeout
} -setup {
} -body {
  catch {vwait -timeout abc done} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 4 -- Cross-thread stress tests
#
#   These exercise Th8_QueueEvent at concurrency: each test
#   spawns multiple worker threads, each with its OWN pState
#   (the canonical per-thread pattern), and verifies that the
#   queue mechanism stays correct under concurrent producers.
#   The test-only ::th8testlib::event_stress and
#   ::th8testlib::event_delete_race wrappers report a
#   formatted result string that the test asserts against.
#
###############################################################################

runTest {test event-4.1 {
  R-29415-33770: Th8_QueueEvent is callable concurrently from multiple
                 worker threads, each with its own pState; every queued
                 event drains exactly once when Th8_DrainQueueEvents
                 is invoked on the test thread after join
} -constraints {
    th8 queue_event
} -body {
  #
  # 4 worker threads × 100 events each = 400 expected dispatches.
  # Each worker has its own pState; after join the test thread
  # drains every pState's queue, bumping the matching counter
  # for each callback that fires.  The result string is
  # deterministic on a working queue: dispatched=expected,
  # failed=0.
  #
  ::th8testlib::event_stress 4 100
} -result {dispatched=400 failed=0 expected=400}}

###############################################################################

runTest {test event-4.2 {
  R-29415-33770: Th8_QueueEvent on a pState whose owning interpreter
                 has been deleted returns TH8_ERROR cleanly (no crash),
                 via the atomic nDeleted check.  Slow workers that
                 sleep past Th8_DeleteInterp observe the post-delete
                 state and back out without dereferencing freed memory.
                 Fast workers that queued before the delete still get
                 their callbacks dispatched on the pre-delete drain.
} -constraints {
    th8 queue_event
} -body {
  #
  # 2 fast workers + 2 slow workers (sleep 200 ms before
  # queueing).  Choreography on the test thread:
  #   1. spawn all 4 workers
  #   2. join just the fast ones (they finish promptly)
  #   3. drain the child interp's queue → fast counters bump
  #   4. delete the child interp
  #   5. join slow workers; their delayed Th8_QueueEvent now
  #      sees pState->nDeleted and returns TH8_ERROR.
  # Expected: every fast event dispatched, every slow event
  # failed.  The 200 ms slow-delay is comfortably longer than
  # the host thread's spawn+join+drain+delete window so the
  # race resolves the same way every run.
  #
  ::th8testlib::event_delete_race 2 2 200
} -result {fast_dispatched=2 slow_failed=2 total=4}}

###############################################################################

runTest {test event-5.1 {
  R-47665-55162: an event callback dispatched by [update] from
                 inside a coroutine MAY suspend the coroutine
                 via [yield].  When the coroutine resumes, the
                 drain loop continues from the suspension point
                 and the [update] invocation completes.
} -constraints {
    th8 queue_event coroutine
} -setup {
  set ::trace [list]
} -body {
  #
  # One worker enqueues a single yielding event.  Inside drainCoro,
  # [update] dispatches the callback; the callback runs the
  # pre-yield half (recording "before"), then [yield] suspends
  # the coroutine.  The trampoline saves the in-flight update_step
  # continuation as part of the coroutine state and returns control
  # to the outer scope.  Resuming the coroutine continues update_step,
  # which finds the queue empty and lets [update] return; the
  # coroutine completes and "after" gets recorded.  Net trace must
  # be {before after} -- deterministic, single-thread, single-event.
  #
  ::th8testlib::queue_event 0 {lappend ::trace before; yield}
  after 50
  coroutine drainCoro apply {{} {
    update
    lappend ::trace after
  }}
  drainCoro
  set ::trace
} -cleanup {
  catch {rename drainCoro {}}
  unset -nocomplain ::trace
} -result {before after}}

###############################################################################
#
# Section 9 -- cancellation between events / nesting
#
###############################################################################

runTest {test event-9.1 {
  R-27275-32732: nested [vwait] is permitted: a callback drained by
  an outer [vwait] may itself call [vwait] on a different variable
} -constraints {
    th8 queue_event
} -setup {
  set ::outer 0
  set ::inner 0
} -body {
  #
  # Outer [vwait] waits on ::outer.  The first queued callback sets
  # ::inner via a SECOND queued event then enters its own [vwait]
  # on ::inner.  When the inner event fires and sets ::inner, the
  # nested [vwait] returns; the callback then sets ::outer; the
  # outer [vwait] returns.  Net effect: both vwaits ran to
  # completion in nested fashion without errors.
  #
  ::th8testlib::queue_event 0 {
    ::th8testlib::queue_event 0 {set ::inner 1}
    vwait -timeout 1000 ::inner
    set ::outer 1
  }
  vwait -timeout 1000 ::outer
  list $::inner $::outer
} -cleanup {
  unset -nocomplain ::outer ::inner
} -result {1 1}}

###############################################################################

runTest {test event-9.2 {
  R-58860-53854: [vwait] checks for interpreter cancellation between
  events; a pending [interp cancel] aborts the wait with an error
} -constraints {
    th8 queue_event
} -body {
  #
  # Queue a callback that issues [interp cancel] from inside the
  # event loop, then have the outer [vwait] await a variable that
  # never gets set.  The cancel observed between events SHALL
  # abort the [vwait] with a script error.
  #
  ::th8testlib::queue_event 0 {interp cancel}
  set rc [catch {vwait -timeout 5000 ::neverSet} msg]
  list $rc [expr {[string length $msg] > 0}]
} -cleanup {
  unset -nocomplain rc msg ::neverSet
} -result {1 1}}

###############################################################################

runTest {test event-9.3 {
  R-59869-11636: [update] checks for interpreter cancellation between
  callbacks; a pending [interp cancel] halts event dispatch before
  the next callback runs
} -constraints {
    th8 queue_event
} -body {
  #
  # Queue two callbacks; the first issues [interp cancel].  The
  # second callback SHALL NOT execute: [update]'s
  # between-callbacks Th8_Ready check observes the cancel and
  # halts event dispatch.  The spec requires the dispatch halt
  # (observable as the second callback not running); whether the
  # halt also surfaces as a script error is implementation-
  # defined and not asserted here.
  #
  set ::ran 0
  ::th8testlib::queue_event 0 {interp cancel}
  ::th8testlib::queue_event 0 {incr ::ran}
  catch {update}
  set ::ran
} -cleanup {
  unset -nocomplain ::ran
} -result {0}}

###############################################################################

source tests/epilogue.tcl
