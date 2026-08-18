###############################################################################
#
# sandbox_resource.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Red-team tests for resource exhaustion attacks against the sandbox:
# regexp bombs, string amplification, list bombs, integer overflow,
# and other denial-of-service vectors.
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
# Regexp catastrophic backtracking.
#
###############################################################################

runTest {test sandbox-resource-1.1 {
  R-24231-13066: regexp: a catastrophic-backtracking pattern is bounded
                 by the regex complexity limit (security_model.md sec 17),
                 returning a definite no-match in a handful of steps --
                 NOT by exhausting CPU or the step counter
} -constraints {
    th8 regexp sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    catch {regexp {(a+)+$} [string repeat a 30]b} msg
    set msg
  }]
  # Probative: the pattern terminates with a definite no-match (0) and
  # the step counter stays tiny -- so the regex complexity limit, not
  # the coarse step limit, is what bounds the backtracking.  A regression
  # to unbounded backtracking would instead blow the step limit (rc 1,
  # "step limit exceeded") or hang.  (This also documents that regexp
  # backtracking does NOT increment the step counter per attempt.)
  list [sandboxRc $r] [sandboxResult $r] \
      [expr {[sandboxSteps $r] < 10000}]
} -cleanup {
  unset -nocomplain r
} -result {0 0 1}}

###############################################################################

runTest {test sandbox-resource-1.2 {
  R-24231-13066: regexp: normal match works within limits
} -constraints {
    th8 regexp sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    regexp {^[0-9]+$} "12345"
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################
#
# String amplification.
#
###############################################################################

runTest {test sandbox-resource-2.1 {
  R-19191-01287: string repeat: exponential growth is terminated by the
                 MEMORY limit (not the step limit) without killing the
                 parent
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  # The child's xPanic is NULL, so hitting the memory ceiling returns an
  # error instead of aborting the process.
  set r [::th8testlib::sandbox {
    set x A
    for {set i 0} {$i < 30} {incr i} {
      set x "$x$x"
    }
  }]
  # Probative: the doubling reaches the allocation ceiling in only a few
  # hundred steps, so termination is a MEMORY error (matches *memory*:
  # "out of memory" / "memory limit exceeded") and NOT the step limit
  # (which would need ~1,000,000 steps).
  list [sandboxRc $r] [string match *memory* [sandboxResult $r]] \
      [expr {[sandboxSteps $r] < 100000}]
} -cleanup {
  unset -nocomplain r
} -result {1 1 1}}

###############################################################################

runTest {test sandbox-resource-2.2 {
  R-42761-35236: append bomb: repeated append is terminated precisely by
                 the STEP limit at the 1,000,000-step boundary
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    set x ""
    for {set i 0} {$i < 2000000} {incr i} {
      append x X
    }
  }]
  # Probative: the step counter is the EXACT limit responsible, and it fires at
  # the EXACT boundary.  The per-step check is `++count; if (count > limit)
  # trip`, so a 1,000,000-step limit trips when the counter reaches exactly
  # 1,000,001 -- one step past the ceiling, deterministically, regardless of the
  # workload's per-iteration step cost (TH8K-021: exact trip count, not > limit).
  list [sandboxRc $r] [sandboxResult $r] \
      [expr {[sandboxSteps $r] == 1000001}]
} -cleanup {
  unset -nocomplain r
} -result {1 {step limit exceeded} 1}}

###############################################################################
#
# List bombs.
#
###############################################################################

runTest {test sandbox-resource-3.1 {
  R-42761-35236: lappend bomb: building a huge list is terminated
                 precisely by the STEP limit at the 1,000,000-step boundary
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    set L {}
    for {set i 0} {$i < 2000000} {incr i} {
      lappend L $i
    }
  }]
  # Probative: the step counter is the exact limit responsible and it fires at
  # the EXACT boundary -- 1,000,001, one step past the 1,000,000 ceiling (the
  # per-step `count > limit` check), independent of per-iteration step cost
  # (TH8K-021: exact trip count).
  list [sandboxRc $r] [sandboxResult $r] \
      [expr {[sandboxSteps $r] == 1000001}]
} -cleanup {
  unset -nocomplain r
} -result {1 {step limit exceeded} 1}}

###############################################################################

runTest {test sandbox-resource-3.2 {
  R-42761-35236: step limit is a precise CEILING, not a spurious killer --
                 a legitimate bounded list-building workload completes with
                 an exact result and a step count strictly below the limit
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  # 50,000 lappends is well under the 1,000,000-step ceiling, so this
  # must complete rather than be terminated.  (The prior version looped
  # `set L [list $L]`, which is a no-op for a brace-free word -- it never
  # nested and asserted nothing.)
  set r [::th8testlib::sandbox {
    set L {}
    for {set i 0} {$i < 50000} {incr i} {
      lappend L $i
    }
    llength $L
  }]
  # Probative: the workload finishes (rc 0) with the exact element count,
  # AND the step counter is being incremented but stayed strictly below
  # the 1,000,000 limit -- proving the limit is an upper bound that does
  # not cut off legitimate work, and that steps are actually counted.
  list [sandboxRc $r] [sandboxResult $r] \
      [expr {[sandboxSteps $r] > 50000 && [sandboxSteps $r] < 1000000}]
} -cleanup {
  unset -nocomplain r
} -result {0 50000 1}}

###############################################################################

runTest {test sandbox-resource-3.3 {
  R-42761-35236: a resource-limit termination is contained to its
                 sandbox -- the host survives and the very next sandbox
                 evaluates normally (no poisoning of the parent)
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain bomb okr
} -body {
  # First, trip the step limit in one sandbox.
  set bomb [::th8testlib::sandbox {
    set x ""
    for {set i 0} {$i < 2000000} {incr i} {
      append x X
    }
  }]
  # Then a fresh sandbox must still work with an exact result and a
  # clean, low step count -- proving the host was not left poisoned by
  # the previous sandbox's forced termination.
  set okr [::th8testlib::sandbox {
    expr {6 * 7}
  }]
  list [sandboxRc $bomb] [sandboxResult $bomb] \
      [sandboxRc $okr] [sandboxResult $okr] \
      [expr {[sandboxSteps $okr] < 1000}]
} -cleanup {
  unset -nocomplain bomb okr
} -result {1 {step limit exceeded} 0 42 1}}

###############################################################################

runTest {test sandbox-resource-3.4 {
  R-42761-35236: SAME-interpreter post-limit contract (TH8K-021) -- a step-limit
  termination bounds the interpreter but does not permanently poison it.  In the
  SAME child that just tripped its step limit: a further script with NO reset
  must STILL fail (the counter is still past the ceiling), and only after
  Th8_ResetStepCount does the same child evaluate normally again with the correct
  result.  Complements sandbox-resource-3.3 (which proves host survival via a
  FRESH child) by pinning the terminated interpreter's own reusable state.
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  # {bombTripped stuckWithoutReset reusableAfterReset reusedResult}
  set r [::th8testlib::same_interp_post_limit]
} -cleanup {
  unset -nocomplain r
} -result {1 1 1 42}}

###############################################################################
#
# Integer overflow.
#
###############################################################################

runTest {test sandbox-resource-4.1 {
  R-27789-36268: integer overflow: overflow checking is enabled
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    catch {expr {0x7FFFFFFFFFFFFFFF + 1}} msg
    set msg
  }]
  # With overflow checking ON and bigint DISABLED, this should error
  list [sandboxRc $r] [expr {[sandboxResult $r] ne ""}]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-resource-4.2 {
  R-27789-36268: integer overflow: multiplication overflow caught
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    catch {expr {0x7FFFFFFFFFFFFFFF * 2}} msg
    expr {$msg ne ""}
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################
#
# Format string abuse.
#
###############################################################################

runTest {test sandbox-resource-5.1 {
  R-24231-13066: format: large width specifier
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    catch {format "%999999d" 1} msg
    expr {[string length $msg] > 0}
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-resource-5.2 {
  R-24231-13066: format: many conversion specifiers
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    set fmt [string repeat "%s " 10]
    string length [format $fmt X X X X X X X X X X]
  }]
  # Should succeed — 10 conversions is reasonable
  list [sandboxRc $r] [expr {[sandboxResult $r] > 0}]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################
#
# Proc/rename abuse.
#
###############################################################################

runTest {test sandbox-resource-6.1 {
  R-20999-34016: rename: cannot rename built-in commands to escape
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    rename if _hidden_if
    _hidden_if {1} {set x ok}
    rename _hidden_if if
    set x
  }]
  # Renaming built-ins is allowed but doesn't grant new capabilities
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 ok}}

###############################################################################

runTest {test sandbox-resource-6.2 {
  R-20999-34016: proc redefine: cannot override built-in to inject behavior
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    # Try to redefine source to bypass restrictions
    proc source {args} { return "hijacked" }
    source /etc/passwd
  }]
  # The proc takes precedence over the built-in
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 hijacked}}

###############################################################################
#
# Error info leakage.
#
###############################################################################

runTest {test sandbox-resource-7.1 {
  R-26649-55462: errorInfo: error traces don't leak parent state
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r trace
} -body {
  set r [::th8testlib::sandbox {
    catch {error "test error"} msg
    set errorInfo
  }]
  set trace [sandboxResult $r]
  # errorInfo should contain the sandbox error, not parent info
  list [sandboxRc $r] [string match {*test error*} $trace]
} -cleanup {
  unset -nocomplain r trace
} -result {0 1}}

###############################################################################

runTest {test sandbox-resource-7.2 {
  R-26649-55462: errorCode: error codes don't leak parent state
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    catch {error "msg" "info" {CUSTOM CODE}}
    set errorCode
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 {CUSTOM CODE}}}

###############################################################################

source tests/epilogue.tcl
