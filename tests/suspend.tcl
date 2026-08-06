###############################################################################
#
# suspend.tcl --
#
# Tcl Language Standard
# Conformance Test File
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
# Section 1 -- basic freeze/thaw
#
###############################################################################

runTest {test suspend-1.1 {
  R-02127-03083: freeze suspends then thaw resumes; remaining commands run
                 after thaw
} -constraints {
    loadLib th8
} -body {
  set x before
  th8testlib::freezecycle {
    set x during
    th8testlib::freeze
    set x after
  }
  set x
} -cleanup {
  catch {unset x}
} -result {after}}

###############################################################################

runTest {test suspend-1.2 {
  R-02127-03083: issuspended returns 0 when not suspended
} -constraints {
    loadLib th8
} -body {
  th8testlib::issuspended
} -result {0}}

###############################################################################

runTest {test suspend-1.3 {
  R-13096-49145: freezecycle resumes and runs remaining commands
} -constraints {
    loadLib th8
} -body {
  th8testlib::freezecycle {
    set v hello
    th8testlib::freeze
    set v goodbye
  }
  set v
} -cleanup {
  catch {unset v}
} -result {goodbye}}

###############################################################################
#
# Section 2 -- catch does not intercept TH8_SUSPEND
#
###############################################################################

runTest {test suspend-2.1 {
  R-18963-52029: catch does not intercept TH8_SUSPEND -- suspension propagates
                 past catch, then thaw resumes
} -constraints {
    loadLib th8
} -body {
  set x before
  th8testlib::freezecycle {
    set x inside
    catch {th8testlib::freeze} msg
    set x resumed
  }
  set x
} -cleanup {
  catch {unset x}
  unset -nocomplain msg
} -result {resumed}}

###############################################################################

runTest {test suspend-2.2 {
  R-21420-54044: Th8_Thaw clears the suspension flag so that Th8_Ready returns
                 TH8_OK; resumed commands execute normally
} -constraints {
    loadLib th8
} -setup {
} -body {
  set _s22_result before
  th8testlib::freezecycle {
    set _s22_result frozen
    th8testlib::freeze
    set _s22_result thawed
  }
  set _s22_result
} -cleanup {
  unset -nocomplain _s22_result
} -result {thawed}}

###############################################################################
#
# Section 3 -- state preservation
#
###############################################################################

runTest {test suspend-3.1 {
  R-15699-25001: variables set before freeze survive thaw and commands after
                 freeze run on resume
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::freezecycle {
    set a 10
    set b 20
    set c [expr {$a + $b}]
    th8testlib::freeze
    set d [expr {$a * $b}]
  }
  list $a $b $c $d
} -cleanup {
  catch {unset a}
  catch {unset b}
  catch {unset c}
  catch {unset d}
} -result {10 20 30 200}}

###############################################################################

runTest {test suspend-3.2 {
  R-15699-25001: resume continues from freeze point
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::freezecycle {
    set x alpha
    set y beta
    set z [string length "$x$y"]
    th8testlib::freeze
    set x changed
  }
  list $x $y $z
} -cleanup {
  catch {unset x}
  catch {unset y}
  catch {unset z}
} -result {changed beta 9}}

###############################################################################
#
# Section 4 -- script queuing on frozen interpreter
#
###############################################################################

runTest {test suspend-4.1 {
  queuescript on a non-frozen interpreter errors
} -constraints {
    loadLib th8
} -setup {
} -body {
  list [catch {th8testlib::queuescript "set x 1"} msg] \
      [string match "*not frozen*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test suspend-4.2 {
  freeze-queue-thaw: queued script runs during thaw
} -constraints {
    loadLib th8
} -setup {
} -body {
  #
  # The initScript sets a variable, then freezes.
  # The queueScript modifies the variable.
  # After thaw, we should see the modification.
  #
  th8testlib::freezequeuethaw \
      {set ::_fqt_result "before"; th8testlib::freeze; set ::_fqt_result} \
      {set ::_fqt_result "injected"}
} -cleanup {
  unset -nocomplain ::_fqt_result
} -result {injected}}

###############################################################################

runTest {test suspend-4.3 {
  freeze-queue-thaw: queued script can access existing variables
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::freezequeuethaw \
      {set ::_fqt_x 10; th8testlib::freeze; set ::_fqt_x} \
      {set ::_fqt_y [expr {$::_fqt_x * 2}]}
  list $::_fqt_x $::_fqt_y
} -cleanup {
  unset -nocomplain ::_fqt_x ::_fqt_y
} -result {10 20}}

###############################################################################
#
# Section 5 -- interp cancel (Section 23.1)
#
# NOTE: interp cancel sets a persistent cancel flag on the interpreter.
# Tests that invoke interp cancel would abort the test runner, so
# behavioral tests use subprocess execution via exec.  Argument
# validation tests are safe to run in-process.
#
###############################################################################

runTest {test suspend-5.1 {
  interp cancel: wrong # args (no sub-command)
} -setup {
} -body {
  list [catch {interp} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test suspend-5.2 {
  R-07502-38675: interp cancel stops script execution (commands after cancel do
                 not run, TIP #285)
} -constraints {
    tip285 test_only_exec not_eagle
} -setup {
} -body {
  #
  # The helper script sets x=before, then cancels, then tries
  # to set x=after.  Since cancel stops execution, x stays
  # "before" and the process exits with an error.
  #
  set rc [catch {
    test_only_exec \
        tests/helpers/cancel_basic.tcl
  } msg]
  list $rc [expr {[string match "*eval canceled*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test suspend-5.3 {
  R-07502-38675: interp cancel with a custom result message (TIP #285)
} -constraints {
    tip285 test_only_exec not_eagle
} -setup {
} -body {
  set rc [catch {
    test_only_exec \
        tests/helpers/cancel_message.tcl
  } msg]
  list $rc [expr {[string match "*custom cancel message*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test suspend-5.4 {
  TIP #285: interp cancel with empty path (current interp)
} -constraints {
    tip285 interp_cancel
} -setup {
} -body {
  set rc [catch {interp cancel "" "path test"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {path test}}}

###############################################################################

runTest {test suspend-5.5 {
  TIP #285: interp cancel with non-empty path is an error
} -constraints {
    tip285 interp_cancel
} -setup {
} -body {
  set rc [catch {interp cancel "nosuch" "test"} msg]
  list $rc [string match "*could not find*" $msg]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test suspend-5.6 {
  TIP #285: single arg after switches is the path, not result
} -constraints {
    tip285 interp_cancel
} -setup {
} -body {
  set rc [catch {interp cancel "badpath"} msg]
  list $rc [string match "*could not find*" $msg]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test suspend-5.7 {
  TIP #285: interp cancel -unwind with empty path and message
} -constraints {
    tip285 test_only_exec not_eagle
} -setup {
} -body {
  set rc [catch {
    test_only_exec \
        tests/helpers/cancel_unwind.tcl
  } msg]
  list $rc [expr {[string match "*unwound*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################
#
# Section 6 -- interp cancel: catch interaction (TIP #285)
#
# These tests run in-process (no exec), verifying that [catch]
# properly intercepts non-unwind cancellation and that -unwind
# prevents interception.
#
###############################################################################

runTest {test suspend-6.1 {
  R-07502-38675 R-16309-49714: catch intercepts interp cancel (no -unwind).
                 At the C-API level this is the NRE trampoline consuming the
                 non-unwind cancellation flag in a one-shot manner and turning
                 it into an error, which is exactly what lets the surrounding
                 catch intercept it here (rc == 1, "eval canceled").
} -constraints {
    tip285 interp_cancel
} -setup {
} -body {
  set rc [catch {interp cancel} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -match glob -result {* {eval canceled}}}

###############################################################################

runTest {test suspend-6.2 {
  R-07502-38675: catch intercepts cancel with custom message
} -constraints {
    tip285 interp_cancel
} -setup {
} -body {
  set rc [catch {interp cancel -- "" "caught this"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -match glob -result {* {caught this}}}

###############################################################################

runTest {test suspend-6.3 {
  R-07502-38675: execution continues after caught cancel
} -constraints {
    tip285 interp_cancel
} -setup {
} -body {
  catch {interp cancel}
  set x survived
} -cleanup {
  unset -nocomplain x
} -result {survived}}

###############################################################################

runTest {test suspend-6.4 {
  R-00559-15113: catch does NOT intercept cancel -unwind (subprocess test --
                 unwind escapes all catches)
} -constraints {
    tip285 test_only_exec not_eagle
} -setup {
} -body {
  set rc [catch {
    test_only_exec \
        tests/helpers/cancel_unwind.tcl
  } msg]
  list $rc [expr {[string match "*unwound*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test suspend-6.5 {
  R-07502-38675: nested catch: inner catches cancel, outer sees OK
} -constraints {
    tip285 interp_cancel
} -setup {
} -body {
  set outer_rc [catch {
    set inner_rc [catch {interp cancel -- "inner"}]
  }]
  list $outer_rc $inner_rc
} -cleanup {
  unset -nocomplain inner_rc outer_rc
} -result {0 1}}

###############################################################################
#
# Section 7 -- exit command (Section 12.10)
#
# The [exit] command sets a sticky flag that causes Th8_Ready to
# return TH8_ERROR, unwinding the stack.  Because the flag is
# sticky, these tests must run in subprocesses.
#
###############################################################################

runTest {test suspend-7.1 {
  R-64643-23093: exit sets the interpreter exit flag; commands after exit do
                 not run
} -constraints {
    th8 test_only_exec
} -setup {
} -body {
  set rc [catch {
    test_only_exec \
        tests/helpers/exit_basic.tcl
  } msg]
  #
  # The process should exit with code 2; exec captures
  # stderr and non-zero exit in the error message.
  #
  list $rc [expr {[string match "*\\\[exit\\\]: 2*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test suspend-7.2 {
  R-48831-15594: exit with integer code emits it to stderr
} -constraints {
    th8 test_only_exec
} -setup {
} -body {
  set rc [catch {
    test_only_exec \
        tests/helpers/exit_code.tcl
  } msg]
  list $rc [expr {[string match "*\\\[exit\\\]: 42*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test suspend-7.3 {
  R-64643-23093: exit prevents subsequent commands from running
} -constraints {
    test_only_exec not_eagle
} -setup {
} -body {
  set rc [catch {
    test_only_exec \
        tests/helpers/exit_stops.tcl
  } msg]
  #
  # "after" should NOT appear in the output because
  # exit unwinds the stack before puts runs.
  #
  list [expr {[string match "*before*" $msg]}] \
      [expr {[string match "*after*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 0}}

###############################################################################

runTest {test suspend-7.4 {
  R-48831-15594: exit with non-integer code is an error
} -setup {
} -body {
  list [catch {exit notanumber} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test suspend-7.5 {
  exit: wrong # args
} -setup {
} -body {
  list [catch {exit 2 2} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {wrong # args:*}}}

###############################################################################

source tests/epilogue.tcl
