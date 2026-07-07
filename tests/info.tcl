###############################################################################
#
# info.tcl --
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
# Section 1 -- info exists
#
###############################################################################

runTest {test info-1.1 {
  R-11017-47048: info exists on existing variable
} -setup {
} -body {
  set x "hello"
  info exists x
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test info-1.2 {
  R-2800-0102: info exists on nonexistent variable
} -setup {
} -body {
  info exists nosuchvar
} -result {0}}

###############################################################################

runTest {test info-1.3 {
  R-2800-0103: info exists on array element
} -setup {
} -body {
  set arr(key) "value"
  list [info exists arr(key)] [info exists arr(nosuch)]
} -cleanup {
  unset -nocomplain arr
} -result {1 0}}

###############################################################################
#
# Section 2 -- info commands
#
###############################################################################

runTest {test info-2.1 {
  R-25423-56315: info commands returns a list
} -body {
  expr {[llength [info commands]] > 0}
} -result {1}}

###############################################################################

runTest {test info-2.2 {
  R-2800-0202: info commands with pattern
} -body {
  expr {[lsearch [info commands "set"] "set"] >= 0}
} -result {1}}

###############################################################################

runTest {test info-2.3 {
  R-2800-0203: info commands with glob pattern
} -body {
  set cmds [info commands "s*"]
  expr {[lsearch $cmds "set"] >= 0}
} -cleanup {
  unset -nocomplain cmds
} -result {1}}

###############################################################################

runTest {test info-2.4 {
  R-2800-0204: info commands includes user-defined procs
} -setup {
} -body {
  proc myTestCmd {} {return "test"}
  set found [expr {[lsearch [info commands "myTestCmd"] "myTestCmd"] >= 0}]
  set found
} -cleanup {
  catch {rename myTestCmd ""}
  unset -nocomplain found
} -result {1}}

###############################################################################
#
# Section 3 -- info vars
#
###############################################################################

runTest {test info-3.1 {
  R-61970-65350: info vars returns a list
} -body {
  expr {[llength [info vars]] > 0}
} -result {1}}

###############################################################################

runTest {test info-3.2 {
  R-2800-0302: info vars with pattern
} -setup {
} -body {
  set testvar_unique123 "hello"
  set result [lsearch [info vars "testvar_unique123"] "testvar_unique123"]
  expr {$result >= 0}
} -cleanup {
  unset -nocomplain testvar_unique123
  unset -nocomplain result
} -result {1}}

###############################################################################
#
# Section 4 -- info level
#
###############################################################################

runTest {test info-4.1 {
  R-21715-33701: info level at global scope
} -body {
  uplevel #0 [list info level]
} -result {0}}

###############################################################################

runTest {test info-4.2 {
  R-29828-17756: info level inside proc
} -setup {
} -body {
  proc myLevel {} {
    info level
  }
  expr {[myLevel] >= 1}
} -cleanup {
  catch {rename myLevel ""}
} -result {1}}

###############################################################################

runTest {test info-4.3 {
  R-2800-0403: info level nested procs
} -setup {
} -body {
  proc innerLevel {} {
    info level
  }
  proc outerLevel {} {
    innerLevel
  }
  expr {[outerLevel] >= 2}
} -cleanup {
  catch {rename innerLevel ""}
  catch {rename outerLevel ""}
} -result {1}}

###############################################################################
#
# Section 5 -- info procs
#
###############################################################################

runTest {test info-5.1 {
  R-00425-56412: info procs returns user-defined procs
} -setup {
} -body {
  proc myTestProc {} {return "test"}
  set found [expr {[lsearch [info procs "myTestProc"] "myTestProc"] >= 0}]
  set found
} -cleanup {
  catch {rename myTestProc ""}
  unset -nocomplain found
} -result {1}}

###############################################################################

runTest {test info-5.2 {
  R-2800-0502: info procs with pattern
} -setup {
} -body {
  proc testProcA {} {}
  proc testProcB {} {}
  set procs [info procs "testProc*"]
  expr {[llength $procs] >= 2}
} -cleanup {
  catch {rename testProcA ""}
  catch {rename testProcB ""}
  unset -nocomplain procs
} -result {1}}

###############################################################################
#
# Section 6 -- info script
#
###############################################################################

runTest {test info-6.1 {
  R-38991-58513: info script returns current source file
} -body {
  info script
} -match regexp -result {^(?:tests/info.tcl|tests\\info.tcl)$}}

###############################################################################

runTest {test info-6.2 {
  R-51523-46504: info script with arg sets script name
} -setup {
  set origScript [info script]
} -body {
  info script "test.tcl"
} -cleanup {
  info script $origScript
  unset -nocomplain origScript
} -result {test.tcl}}

###############################################################################
#
# Section 7 -- info: other subcommands
#
###############################################################################

runTest {test info-7.1 {
  R-04279-51073: info body returns proc body
} -setup {
} -body {
  proc myProc {} {
    return "hello"
  }
  info body myProc
} -cleanup {
  catch {rename myProc ""}
} -match glob -result {*return*hello*}}

###############################################################################

runTest {test info-7.2 {
  R-39692-20126: info args returns proc arguments
} -setup {
} -body {
  proc myProc {a b c} {
    return ""
  }
  info args myProc
} -cleanup {
  catch {rename myProc ""}
} -result {a b c}}

###############################################################################
#
# Section 8 -- info: error cases
#
###############################################################################

runTest {test info-8.1 {
  R-2800-0801: info with no subcommand is error
} -setup {
} -body {
  list [catch {info} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test info-8.2 {
  R-2800-0802: info with unknown subcommand is error
} -setup {
} -body {
  list [catch {info nosuchsub} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test info-8.3 {
  R-02797-01349: info body on nonexistent proc is error
} -setup {
} -body {
  list [catch {info body nosuchproc} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 9 -- info library
#
###############################################################################

runTest {test info-9.1 {
  R-35603-27691: info library returns path when th8 package provided
} -constraints {
    th8
} -body {
  #
  # The th8 package is provided by the shell's auto_path
  # loading.  Verify the result is the expected library path.
  #
  info library
} -result {./lib/th8}}

###############################################################################

runTest {test info-9.2 {
  R-00034-24955: info library returns empty after package forget
} -constraints {
    th8
} -setup {
  #
  # Remember the current package version so we can restore it.
  #
  set _saved_ver [package provide th8]
} -body {
  package forget th8
  info library
} -cleanup {
  if {$_saved_ver ne ""} then {
    package provide th8 $_saved_ver
  }
  unset -nocomplain _saved_ver
} -result {}}

###############################################################################

runTest {test info-9.3 {
  R-36839-38193: info library reflects package state not directory
} -constraints {
    th8
} -setup {
  set _saved_ver [package provide th8]
} -body {
  package forget th8
  set before [info library]
  package provide th8 1.0
  set after [info library]
  list $before $after
} -cleanup {
  package forget th8
  if {$_saved_ver ne ""} then {
    package provide th8 $_saved_ver
  }
  unset -nocomplain _saved_ver before after
} -result {{} ./lib/th8}}

###############################################################################

runTest {test info-9.4 {
  R-35603-27691: info library wrong # args
} -setup {
} -body {
  list [catch {info library extra} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 10 -- info cmdcount (Section 20.0f)
#
###############################################################################

runTest {test info-10.1 {
  R-42349-45141: info cmdcount returns a non-negative integer
} -setup {
} -body {
  set n [info cmdcount]
  expr {[string is integer $n] && $n > 0}
} -cleanup {
  unset -nocomplain n
} -result {1}}

###############################################################################

runTest {test info-10.2 {
  R-42349-45141: info cmdcount increases after evaluating commands
} -setup {
} -body {
  set before [info cmdcount]
  set dummy 1
  set after [info cmdcount]
  expr {$after > $before}
} -cleanup {
  unset -nocomplain before after dummy
} -result {1}}

###############################################################################

runTest {test info-10.3 {
  info cmdcount: wrong # args
} -setup {
} -body {
  list [catch {info cmdcount extra} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {wrong # args:*}}}

###############################################################################
#
# Section 11 -- info vars: namespace-qualified patterns
#
###############################################################################

runTest {test info-11.1 {
  info vars with namespace-qualified glob returns variables
} -body {
  expr {[llength [info vars ::th8test::*]] > 0}
} -result {1}}

###############################################################################
#
# Section 12 -- info nameofexecutable and info sharedlibextension
#
###############################################################################

runTest {test info-12.1 {
  R-38164-60602: info sharedlibextension returns platform extension
} -setup {
} -body {
  set ext [info sharedlibextension]
  expr {$ext eq ".so" || $ext eq ".dylib" || $ext eq ".dll"}
} -cleanup {
  unset -nocomplain ext
} -result {1}}

###############################################################################

runTest {test info-12.2 {
  R-19219-42106: info nameofexecutable returns a string
} -setup {
} -body {
  set result [info nameofexecutable]
  string is ascii $result
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################
#
# Section 13 -- info loaded
#
###############################################################################

runTest {test info-13.1 {
  R-45786-18817: info loaded returns a list
} -setup {
} -body {
  set result [info loaded]
  expr {[llength $result] >= 0}
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################
#
# Section 14 -- R-marker coverage: info exists on array variables
#
###############################################################################

runTest {test info-14.1 {
  R-09588-64534: info exists returns 1 for array variables
} -constraints {
    array_set
} -setup {
} -body {
  array set myarr {k1 v1 k2 v2}
  #
  # The bare array name (no subscript) should report as existing.
  #
  info exists myarr
} -cleanup {
  unset -nocomplain myarr
} -result {1}}

###############################################################################

source tests/epilogue.tcl
