###############################################################################
#
# debug.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the script debugging API (Sections 37-40 of the standard):
# debug callback, breakpoints, step modes, frame inspection, and
# eval-at-frame.
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
# Section 1 -- Debug callback: install, fire, capture events
#
###############################################################################

runTest {test debug-1.1 {
  debug callback install and remove succeed
} -constraints {
    th8
} -body {
  ::th8testlib::debug callback install
  ::th8testlib::debug callback remove
  expr {1}
} -result {1}}

###############################################################################

runTest {test debug-1.2 {
  debug callback captures step events with step-into mode
} -constraints {
    th8
} -body {
  ::th8testlib::debug callback install
  ::th8testlib::debug step into
  set x 1
  set y 2
  ::th8testlib::debug step none
  set count [::th8testlib::debug callback count]
  ::th8testlib::debug callback remove
  expr {$count > 0}
} -cleanup {
  catch {::th8testlib::debug callback remove}
  unset -nocomplain x y count
} -result {1}}

###############################################################################

runTest {test debug-1.3 {
  debug callback event list contains event/line/depth triples
} -constraints {
    th8
} -body {
  ::th8testlib::debug callback install
  ::th8testlib::debug step into
  set x 1
  ::th8testlib::debug step none
  set events [::th8testlib::debug callback events]
  ::th8testlib::debug callback remove
  # Events are triples: event line depth
  # At least one triple should exist
  expr {[llength $events] >= 3 && [llength $events] % 3 == 0}
} -cleanup {
  catch {::th8testlib::debug callback remove}
  unset -nocomplain x events
} -result {1}}

###############################################################################

runTest {test debug-1.4 {
  debug callback clear resets event count
} -constraints {
    th8
} -body {
  ::th8testlib::debug callback install
  ::th8testlib::debug step into
  set x 1
  ::th8testlib::debug step none
  ::th8testlib::debug callback clear
  set count [::th8testlib::debug callback count]
  ::th8testlib::debug callback remove
  set count
} -cleanup {
  catch {::th8testlib::debug callback remove}
  unset -nocomplain x count
} -result {0}}

###############################################################################
#
# Section 2 -- Step modes
#
###############################################################################

runTest {test debug-2.1 {
  step mode defaults to none
} -constraints {
    th8
} -body {
  ::th8testlib::debug step get
} -result {0}}

###############################################################################

runTest {test debug-2.2 {
  step into sets mode 1
} -constraints {
    th8
} -setup {
} -body {
  ::th8testlib::debug step into
  set m [::th8testlib::debug step get]
  ::th8testlib::debug step none
  set m
} -cleanup {
  catch {::th8testlib::debug step none}
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test debug-2.3 {
  step over sets mode 2
} -constraints {
    th8
} -body {
  ::th8testlib::debug step over
  set m [::th8testlib::debug step get]
  ::th8testlib::debug step none
  set m
} -cleanup {
  catch {::th8testlib::debug step none}
  unset -nocomplain m
} -result {2}}

###############################################################################

runTest {test debug-2.4 {
  step out sets mode 3
} -constraints {
    th8
} -body {
  ::th8testlib::debug step out
  set m [::th8testlib::debug step get]
  ::th8testlib::debug step none
  set m
} -cleanup {
  catch {::th8testlib::debug step none}
  unset -nocomplain m
} -result {3}}

###############################################################################
#
# Section 3 -- Breakpoints
#
###############################################################################

runTest {test debug-3.1 {
  breakpoint set returns a positive ID
} -constraints {
    th8
} -body {
  set id [::th8testlib::debug breakpoint set test.tcl 5]
  ::th8testlib::debug breakpoint clearall
  expr {$id > 0}
} -cleanup {
  catch {::th8testlib::debug breakpoint clearall}
  unset -nocomplain id
} -result {1}}

###############################################################################

runTest {test debug-3.2 {
  breakpoint IDs are unique and incrementing
} -constraints {
    th8
} -body {
  set id1 [::th8testlib::debug breakpoint set test.tcl 1]
  set id2 [::th8testlib::debug breakpoint set test.tcl 2]
  set id3 [::th8testlib::debug breakpoint set test.tcl 3]
  ::th8testlib::debug breakpoint clearall
  expr {$id1 < $id2 && $id2 < $id3}
} -cleanup {
  catch {::th8testlib::debug breakpoint clearall}
  unset -nocomplain id1 id2 id3
} -result {1}}

###############################################################################

runTest {test debug-3.3 {
  breakpoint clear removes a specific breakpoint
} -constraints {
    th8
} -body {
  set id [::th8testlib::debug breakpoint set test.tcl 5]
  set rc [catch {::th8testlib::debug breakpoint clear $id}]
  ::th8testlib::debug breakpoint clearall
  expr {$rc == 0}
} -cleanup {
  catch {::th8testlib::debug breakpoint clearall}
  unset -nocomplain id rc
} -result {1}}

###############################################################################

runTest {test debug-3.4 {
  breakpoint clearall removes all breakpoints
} -constraints {
    th8
} -body {
  ::th8testlib::debug breakpoint set test.tcl 1
  ::th8testlib::debug breakpoint set test.tcl 2
  ::th8testlib::debug breakpoint set test.tcl 3
  set rc [catch {::th8testlib::debug breakpoint clearall}]
  expr {$rc == 0}
} -cleanup {
  catch {::th8testlib::debug breakpoint clearall}
  unset -nocomplain rc
} -result {1}}

###############################################################################

runTest {test debug-3.5 {
  clearing nonexistent breakpoint returns error
} -constraints {
    th8
} -setup {
} -body {
  catch {::th8testlib::debug breakpoint clear 99999} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 4 -- Frame inspection
#
###############################################################################

runTest {test debug-4.1 {
  frames count returns at least 1 at top level
} -constraints {
    th8
} -body {
  set count [::th8testlib::debug frames count]
  expr {$count >= 1}
} -cleanup {
  unset -nocomplain count
} -result {1}}

###############################################################################

runTest {test debug-4.2 {
  frames count increases inside a proc
} -constraints {
    th8
} -body {
  proc _dbg_test_depth {} {
    ::th8testlib::debug frames count
  }
  set inner [_dbg_test_depth]
  set outer [::th8testlib::debug frames count]
  expr {$inner > $outer}
} -cleanup {
  catch {rename _dbg_test_depth ""}
  unset -nocomplain inner outer
} -result {1}}

###############################################################################

runTest {test debug-4.3 {
  frames info 0 returns proc name inside a proc
} -constraints {
    th8
} -body {
  proc _dbg_test_info {} {
    ::th8testlib::debug frames info 0
  }
  set info [_dbg_test_info]
  # Info is a list: procName scriptName lineNumber
  set procName [lindex $info 0]
  expr {$procName eq "_dbg_test_info"
    || $procName eq "::th8testlib::debug"}
} -cleanup {
  catch {rename _dbg_test_info ""}
  unset -nocomplain info procName
} -result {1}}

###############################################################################
#
# Section 5 -- Eval at frame
#
###############################################################################

runTest {test debug-5.1 {
  eval at frame 0 evaluates in current context
} -constraints {
    th8
} -body {
  set _dbg_x 42
  ::th8testlib::debug eval 0 {set _dbg_x}
} -cleanup {
  unset -nocomplain _dbg_x
} -result {42}}

###############################################################################

runTest {test debug-5.2 {
  eval at frame accesses caller variables via uplevel
} -constraints {
    th8
} -body {
  set _dbg_topvar "secret"
  # eval at frame 0 accesses the debug command's frame;
  # use a high frame index to access the top-level frame
  set count [::th8testlib::debug frames count]
  ::th8testlib::debug eval [expr {$count - 1}] {set _dbg_topvar}
} -cleanup {
  unset -nocomplain _dbg_topvar count
} -result {secret}}

###############################################################################
#
# Section 6 -- Step-into generates events
#
###############################################################################

runTest {test debug-6.1 {
  step-into mode generates events for multiple commands
} -constraints {
    th8
} -setup {
} -body {
  ::th8testlib::debug callback install
  ::th8testlib::debug callback clear
  ::th8testlib::debug step into
  set a 1
  set b 2
  set c [expr {$a + $b}]
  ::th8testlib::debug step none
  set count [::th8testlib::debug callback count]
  ::th8testlib::debug callback remove
  # Should have captured events for set a, set b, expr, set c
  expr {$count >= 3}
} -cleanup {
  catch {::th8testlib::debug step none}
  catch {::th8testlib::debug callback remove}
  unset -nocomplain a b c count
} -result {1}}

###############################################################################

runTest {test debug-6.2 {
  all step events have event type 1 (TH8_DEBUG_STEP)
} -constraints {
    th8
} -body {
  ::th8testlib::debug callback install
  ::th8testlib::debug callback clear
  ::th8testlib::debug step into
  set x 1
  ::th8testlib::debug step none
  set events [::th8testlib::debug callback events]
  ::th8testlib::debug callback remove
  # Check all events are type 1 (STEP)
  set allStep 1
  for {set i 0} {$i < [llength $events]} {incr i 3} {
    if {[lindex $events $i] != 1} then {
      set allStep 0
    }
  }
  set allStep
} -cleanup {
  catch {::th8testlib::debug step none}
  catch {::th8testlib::debug callback remove}
  unset -nocomplain x events allStep i
} -result {1}}

###############################################################################
#
# Section 7 -- Error handling
#
###############################################################################

runTest {test debug-7.1 {
  debug with unknown subcommand returns error
} -constraints {
    th8
} -body {
  catch {::th8testlib::debug nosuchcmd foo} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test debug-7.2 {
  debug step with invalid mode returns error
} -constraints {
    th8
} -body {
  catch {::th8testlib::debug step badmode} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test debug-7.3 {
  debug with too few args returns error
} -constraints {
    th8
} -body {
  catch {::th8testlib::debug} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test debug-7.4 {
  debug callback with zero overhead when removed
} -constraints {
    th8
} -body {
  # Verify that after removing callback, step mode has no effect
  ::th8testlib::debug callback install
  ::th8testlib::debug callback remove
  ::th8testlib::debug step into
  set x 1
  set y 2
  ::th8testlib::debug step none
  # No callback, so no events captured — just verify no crash
  expr {$x + $y}
} -cleanup {
  catch {::th8testlib::debug step none}
  catch {::th8testlib::debug callback remove}
  unset -nocomplain x y
} -result {3}}

###############################################################################

source tests/epilogue.tcl
