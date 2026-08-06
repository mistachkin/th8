###############################################################################
#
# apicontract.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Regression tests for TH8 C-API return-value contracts that were once
# documented one way but implemented another, so callers that followed
# the public contract behaved incorrectly.  These are exercised through
# test-only ::th8testlib primitives that surface the C-level return
# value to script.
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
# Section 1 -- Th8_ErrorMessage returns TH8_ERROR (not TH8_OK)
#
# The public header documents Th8_ErrorMessage as returning TH8_ERROR so
# callers can write `return Th8_ErrorMessage(interp, ...)`; setting an
# error message is always a failure path.  The implementation formerly
# returned TH8_OK, so those `return Th8_ErrorMessage(...)` sites reported
# SUCCESS while leaving an error message in the result.  The testlib
# subcommand dispatchers use exactly that idiom for "unknown
# subcommand", so an unknown subcommand must now raise a script error.
#
###############################################################################

runTest {test apicontract-1.1 {
  An unknown testlib subcommand (dispatched via `return
  Th8_ErrorMessage(...)`) raises a script error (rc == 1), not a silent
  success.
} -constraints {
    loadLib th8
} -body {
  catch {::th8testlib::null_guard bogus_subcommand} msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test apicontract-1.2 {
  The error result carries the composed message, and a VALID subcommand
  still succeeds (rc == 0) -- the contract change only affects the
  error path.
} -constraints {
    loadLib th8
} -body {
  set rc [catch {::th8testlib::null_guard bogus_subcommand} msg]
  list $rc [expr {[string match "*unknown subcommand*" $msg] ? 1 : 0}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################
#
# Section 2 -- Th8_NREval propagates a scheduling (allocation) failure
#
# Th8_NREval's only allocation is the callback pushed by
# Th8_NRAddCallback.  It formerly discarded that return value and
# unconditionally returned TH8_OK, so an allocation failure while
# scheduling the deferred eval was reported as success (and the eval
# silently never ran).  The nreval_schedfail primitive installs the
# fault layer with nAllocFailAfter == 1, calls Th8_NREval, and reports
# whether it correctly surfaced TH8_ERROR.
#
###############################################################################

runTest {test apicontract-2.1 {
  Th8_NREval returns TH8_ERROR when scheduling the callback fails
  (forced first-allocation failure), instead of reporting success.
} -constraints {
    loadLib th8 fault_injection
} -body {
  ::th8testlib::nreval_schedfail
} -result {1}}

###############################################################################
#
# Section 3 -- th8BufWrite / Th8_Buffer append failure is not silently
# truncated (Bug 61)
#
# The growable Th8_Buffer used to build command words, [subst] output,
# and argument vectors appended via a void th8BufWrite that SILENTLY
# DROPPED a write when its growth allocation failed.  A word built under
# out-of-memory therefore came out truncated ("concat" -> "con",
# "XYZ" -> "YZ") and the truncation was published as SUCCESS.  A sticky
# `bFail` flag on the buffer now records the failure; every finalization
# point checks it and fails the operation with an error instead.
#
###############################################################################

runTest {test apicontract-3.1 {
  A Th8_Buffer append failure (out of memory) is surfaced as an error
  at finalization, never as a truncated success.  bufwrite_oom sweeps a
  forced single allocation failure over Th8_Subst of plain input and
  reports 1 only if every ordinal produced either the full correct
  result or TH8_ERROR (never a truncated TH8_OK result).
} -constraints {
    loadLib th8 fault_injection
} -body {
  ::th8testlib::bufwrite_oom
} -result {1}}

###############################################################################

runTest {test apicontract-3.2 {
  End-to-end: under a forced allocation failure while parsing a command,
  a command word / argument is NEVER silently truncated and dispatched
  as success.  Sweeps a single forced alloc failure over the parse of
  [concat STR]; a violation is a success (rc == 0) whose result is a
  non-empty proper substring of the expected output (a truncated word).
  The separate OOM-propagation siblings (results "out of memory" / "")
  are out of scope for this th8BufWrite invariant.
} -constraints {
    loadLib th8 fault_injection
} -body {
  set bad {}
  foreach str {XYZ FORTYTWO concatenate abcdefghij} {
    for {set n 1} {$n <= 30} {incr n} {
      set r [::th8testlib::fault eval [list concat $str] -allocFailAfter $n]
      lassign $r rc result ac afc
      if {$afc > 0 && $rc == 0 && $result ne $str && $result ne "" &&
          [string first $result $str] >= 0} {
        lappend bad "str=$str n=$n res=<$result>"
      }
    }
  }
  set bad
} -cleanup {
  unset -nocomplain bad r rc result ac afc n str
} -result {}}

###############################################################################
#
# Section 4 -- Th8_StringAppend callers do not publish a truncated
# string as success (Bug 61 sibling)
#
# Th8_StringAppend returns TH8_ERROR on a growth-allocation failure, but
# the great majority of its ~240 call sites build a result string with
# the (char **, size_t *) accumulator pattern and IGNORED that return,
# so under OOM the string was silently truncated ("format FORTYTWO" ->
# "FRTYTWO") and published as success.  Every call site now routes
# through the TH8_STR_APPEND macro, which does `goto oom;` on failure to
# a per-function well-known cleanup label that frees local state and
# returns TH8_ERROR, propagating the failure up the call stack.  There
# are zero remaining direct Th8_StringAppend calls, so no caller can
# ignore the result.  Non-OOM operation is unaffected (the macro only
# branches on a real allocation failure).
#
# The invariant: under a single forced allocation failure while
# evaluating a string-building command, the command must NEVER report
# success (rc == 0) with a truncated / wrong result.  A truncation is a
# result that differs from the correct output and is neither of the two
# recognized error-sentinels ("" / "out of memory").
#
# Section 4.2 covers the sibling once tracked as "still open": a shared
# eval-path allocation (th8EvalLocal's Th8_NRAddCallback callback push,
# ~ordinal 3, command-independent) whose TH8_ERROR was swallowed,
# leaving the "out of memory" result in place while th8EvalLocal
# returned TH8_OK.  th8EvalLocal now propagates the scheduling failure.
#
###############################################################################

runTest {test apicontract-4.1 {
  A Th8_StringAppend-built command result is never silently truncated
  and reported as success under out-of-memory.  Sweeps a forced single
  allocation failure over several string-building commands ([format],
  [string toupper], [string repeat], [join]); a violation is a success
  (rc == 0) whose result is wrong but not an error-sentinel.
} -constraints {
    loadLib th8 fault_injection
} -body {
  set bad {}
  foreach {cmd exp} {
    {format FORTYTWO} FORTYTWO
    {string toupper hello} HELLO
    {string repeat ab 3} ababab
    {join {aa bb cc} +} aa+bb+cc
  } {
    for {set n 1} {$n <= 30} {incr n} {
      set r [::th8testlib::fault eval $cmd -allocFailAfter $n]
      lassign $r rc result ac afc
      if {$afc > 0 && $rc == 0 && $result ne $exp && $result ne "" &&
          $result ne "out of memory"} {
        lappend bad "cmd=<$cmd> n=$n res=<$result>"
      }
    }
  }
  set bad
} -cleanup {
  unset -nocomplain bad r rc result ac afc n cmd exp
} -result {}}

###############################################################################

runTest {test apicontract-4.2 {
  The shared eval-path allocation failure (th8EvalLocal's NRE callback
  push) is propagated, not swallowed as a false success (Bug 61
  Sibling 2).  Forcing the callback-scheduling allocation to fail while
  evaluating any command must yield a genuine error (rc == 1), not a
  success (rc == 0) that leaves an "out of memory" / empty result.
  Command-independent: the failing allocation is in the eval setup, not
  the command body, so it reproduces identically for [set], [format],
  [concat] and [list].
} -constraints {
    loadLib th8 fault_injection
} -body {
  set bad {}
  foreach cmd {{set x V} {format V} {concat V} {list a b}} {
    for {set n 1} {$n <= 6} {incr n} {
      set r [::th8testlib::fault eval $cmd -allocFailAfter $n]
      lassign $r rc result ac afc
      if {$afc > 0 && $rc == 0 &&
          ($result eq "out of memory" || $result eq "")} {
        lappend bad "cmd=<$cmd> n=$n res=<$result>"
      }
    }
  }
  set bad
} -cleanup {
  unset -nocomplain bad r rc result ac afc n cmd
} -result {}}

###############################################################################

runTest {test apicontract-5.1 {
  R-63239-00519: Th8_SplitList returns *pazElem and *panElem as pointers into
                 a SINGLE allocation block -- *panElem is the interior lengths
                 array laid out immediately after the element pointers
                 (== &(*pazElem)[nCount]), so only *pazElem (the block start)
                 is a valid Th8_Free target and *panElem MUST NOT be freed
                 separately.  splitlist_probe block does a full split, asserts
                 the exact interior relationship, and reclaims the whole block
                 with a single free (the debug heap checks would flag a
                 wrong/partial free).
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::splitlist_probe block {alpha beta gamma delta}
} -result {interior}}

###############################################################################

runTest {test apicontract-6.1 {
  R-10315-21713: If the platform does not provide a path normalization
                 callback, [file normalize] returns its argument unchanged.
                 normalize_no_callback runs [file normalize] in a child
                 interpreter whose xNormalizePath has been cleared, and
                 confirms the argument is echoed back unchanged.
} -constraints {
    loadLib th8
} -body {
  set result [::th8testlib::normalize_no_callback]
  set idx [lsearch -exact $result "unchanged"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  unset -nocomplain result idx
} -result {ok}}

###############################################################################

runTest {test apicontract-7.1 {
  R-64687-40860: Th8_OutputError SHALL query the current error output channel
                 via xGetErrorOutput before invoking xOutputError, passing that
                 channel pointer to xOutputError.  output_error_channel installs
                 a matched callback pair on a child interpreter (xGetErrorOutput
                 yields a sentinel channel; xOutputError captures the channel it
                 receives) and confirms the two are the same pointer.
} -constraints {
    loadLib th8
} -body {
  set result [::th8testlib::output_error_channel]
  set idx [lsearch -exact $result "channel_passed"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  unset -nocomplain result idx
} -result {ok}}

###############################################################################

runTest {test apicontract-8.1 {
  R-16003-05438: The xCloseTemporaryData platform callback SHALL be called
                 before explicitly closing a temporary channel; if it returns
                 non-TH8_OK the close is vetoed and the channel remains open.
                 close_veto installs a vetoing xCloseTemporaryData on a child
                 interpreter, creates an in-memory temporary channel via [file
                 tempname], and confirms [close] is refused with the veto error.
} -constraints {
    loadLib th8
} -body {
  set result [::th8testlib::close_veto]
  set idx [lsearch -exact $result "vetoed"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  unset -nocomplain result idx
} -result {ok}}

###############################################################################

runTest {test apicontract-9.1 {
  R-00313-45995: The environment variable backend SHALL serialize all
                 operations with a file-scope mutex to prevent data races on
                 concurrent access to the process environment.  env_stress
                 spawns 8 worker threads, each with its own interpreter, that
                 concurrently set and unset the SAME ::env key 200 times
                 (1600 mutex-serialized setenv/unsetenv pairs).  A correct
                 mutex serializes them so no cycle is lost or corrupted --
                 the helper returns "ok" only when all 1600 cycles complete.
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::env_stress 8 200
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
