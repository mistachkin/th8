###############################################################################
#
# after.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the [after] command (cancellation-aware sleep).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test after-1.1 {
  R-40471-05227: after returns the empty string
} -constraints {
    th8
} -body {
  after 0
} -result {}}

###############################################################################

runTest {test after-1.2 {
  R-40471-05227: after 0 returns immediately
} -constraints {
    th8 time
} -setup {
} -body {
  set elapsed [lindex [time {after 0}] 0]
  expr {$elapsed < 100000}
} -cleanup {
  unset -nocomplain elapsed
} -result {1}}

###############################################################################

runTest {test after-1.3 {
  R-40471-05227: after with a positive value sleeps for approximately that
                 duration
} -constraints {
    th8 clock_seconds
} -setup {
} -body {
  set t1 [clock seconds]
  after 1100
  set t2 [clock seconds]
  expr {($t2 - $t1) >= 1}
} -cleanup {
  unset -nocomplain t1 t2
} -result {1}}

###############################################################################

runTest {test after-1.4 {
  R-52937-01216: after is interrupted by interp cancel
} -constraints {
    th8 tip285
} -body {
  list [catch {interp cancel; after 10000} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {eval canceled}}}

###############################################################################
#
# Section 2 -- R-marker coverage: after sleep and cancellation
#
###############################################################################

runTest {test after-2.1 {
  R-40471-05227: after sleeps for specified milliseconds, returns empty
} -constraints {
    th8
} -setup {
} -body {
  set result [after 0]
  #
  # The return value must be the empty string.
  #
  list [string length $result] $result
} -cleanup {
  unset -nocomplain result
} -result {0 {}}}

###############################################################################

runTest {test after-2.2 {
  R-52937-01216: after checks for cancellation during sleep
} -constraints {
    th8 interp_cancel test_only_exec
} -setup {
} -body {
  set rc [catch {
    test_only_exec tests/helpers/cancel_basic.tcl
  } msg]
  list $rc [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
