###############################################################################
#
# sandbox_info.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Red-team tests for information leakage from the sandbox: process
# identity, system configuration, timing channels, and introspection.
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
# Process identity leakage.
#
###############################################################################

runTest {test sandbox-info-1.1 {
  R-19191-01287: pid: sandbox returns a process ID
} -constraints {
    th8 pid sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {pid}]
  list [sandboxRc $r] [expr {[sandboxResult $r] > 0}]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-1.2 {
  R-34935-39021: info nameofexecutable: sandbox sees executable path
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {info nameofexecutable}]
  list [sandboxRc $r] [string match "./*" [sandboxResult $r]]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-1.3 {
  R-34935-39021: info nameofexecutable: not an absolute path
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {info nameofexecutable}]
  list [sandboxRc $r] [string match "/*" [sandboxResult $r]]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

runTest {test sandbox-info-1.4 {
  R-55895-04504: pwd: sandbox sees base directory only
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {pwd}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 .}}

###############################################################################
#
# Platform / configuration leakage.
#
###############################################################################

runTest {test sandbox-info-2.1 {
  R-19191-01287: tcl_platform: sandbox has platform array
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {info exists tcl_platform(engine)}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-2.2 {
  R-19191-01287: tcl_platform(platform): sandbox sees OS family
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    info exists tcl_platform(platform)
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-2.3 {
  R-19191-01287: th8_security array exists in sandbox
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {info exists th8_security(policy)}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-2.4 {
  R-19191-01287: th8_security(policy) defaults to none in sandbox
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {set th8_security(policy)}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 none}}

###############################################################################
#
# Introspection abuse.
#
###############################################################################

runTest {test sandbox-info-3.1 {
  R-20999-34016: info commands: sandbox has only built-in commands
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {llength [info commands]}]
  # Should have the standard language commands only
  # (no testlib, no harpy, no application commands)
  set nCmds [sandboxResult $r]
  list [sandboxRc $r] [expr {$nCmds > 10 && $nCmds < 200}]
} -cleanup {
  unset -nocomplain r nCmds
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-3.2 {
  R-20999-34016: info globals: limited set of globals in sandbox
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {llength [info globals]}]
  set nGlobals [sandboxResult $r]
  list [sandboxRc $r] [expr {$nGlobals < 20}]
} -cleanup {
  unset -nocomplain r nGlobals
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-3.3 {
  R-12924-62009: info script: no active script in sandbox
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {info script}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 {}}}

###############################################################################
#
# Timing and side channels.
#
###############################################################################

runTest {test sandbox-info-4.1 {
  R-19191-01287: clock seconds: sandbox can read wall-clock time
} -constraints {
    th8 clock sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {clock seconds}]
  # Time is visible — verify it's a reasonable epoch value
  list [sandboxRc $r] [expr {[sandboxResult $r] > 1700000000}]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-4.2 {
  R-19191-01287: time command: sandbox can measure elapsed time
} -constraints {
    th8 time sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    lindex [time {expr {1+1}} 100] 0
  }]
  # Returns microseconds per iteration — should be small
  list [sandboxRc $r] [expr {[sandboxResult $r] < 1000}]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################
#
# Modification of read-only state.
#
###############################################################################

runTest {test sandbox-info-5.1 {
  R-19191-01287: th8_security array: sandbox child has it
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    info exists th8_security(policy)
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-info-5.2 {
  R-19191-01287: tcl_platform array: sandbox child has it
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    info exists tcl_platform(engine)
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

source tests/epilogue.tcl
