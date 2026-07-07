###############################################################################
#
# introspect.tcl --
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
# Section 1 -- source and info script
#
###############################################################################

runTest {test introspect-1.1 {
  R-61728-51322: source records script name
} -body {
  source tests/helpers/info_script.tcl
  string match *info_script.tcl $::_cf_test_script
} -cleanup {
  unset -nocomplain ::_cf_test_script
} -result {1}}

###############################################################################

runTest {test introspect-1.2 {
  R-05497-60890: nested source maintains name stack
} -body {
  source tests/helpers/info_script_outer.tcl
  list \
      [string match *info_script_outer.tcl $::_cf_before_inner] \
      [string match *info_script_inner.tcl $::_cf_inner_script] \
      [string match *info_script_outer.tcl $::_cf_after_inner]
} -cleanup {
  unset -nocomplain ::_cf_before_inner
  unset -nocomplain ::_cf_inner_script
  unset -nocomplain ::_cf_after_inner
} -result {1 1 1}}

###############################################################################

runTest {test introspect-1.3 {
  R-38991-58513: info script returns current test file
} -body {
  expr {[string length [info script]] > 0}
} -result {1}}

###############################################################################
#
# Section 2 -- namespace commands
#
###############################################################################

runTest {test introspect-2.1 {
  R-53980-30653: namespace current at global is ::
} -body {
  namespace current
} -result {::}}

###############################################################################

runTest {test introspect-2.2 {
  R-10701-37097: namespace eval creates namespace
} -setup {
} -body {
  namespace eval ::testns_intr { set x 1 }
  namespace exists ::testns_intr
} -cleanup {
  catch {namespace delete ::testns_intr}
} -result {1}}

###############################################################################

runTest {test introspect-2.3 {
  R-10701-37097: namespace exists returns 0
} -body {
  namespace exists ::nonexistent_ns_xyz
} -result {0}}

###############################################################################

runTest {test introspect-2.4 {
  R-10701-37097: namespace children lists children
} -setup {
} -body {
  namespace eval ::testns_parent {
    namespace eval child1 {}
    namespace eval child2 {}
  }
  set kids [namespace children ::testns_parent]
  expr {[llength $kids] == 2}
} -cleanup {
  unset -nocomplain kids
  catch {namespace delete ::testns_parent}
} -result {1}}

###############################################################################

runTest {test introspect-2.5 {
  R-10701-37097: namespace delete removes namespace
} -setup {
} -body {
  namespace eval ::testns_del {}
  set before [namespace exists ::testns_del]
  namespace delete ::testns_del
  set after [namespace exists ::testns_del]
  list $before $after
} -cleanup {
  unset -nocomplain before after
} -result {1 0}}

###############################################################################

runTest {test introspect-2.6 {
  R-10701-37097: namespace parent returns parent
} -setup {
} -body {
  namespace eval ::testns_par {
    namespace eval child {}
  }
  namespace parent ::testns_par::child
} -cleanup {
  catch {namespace delete ::testns_par}
} -result {::testns_par}}

###############################################################################

runTest {test introspect-2.7 {
  R-10701-37097: namespace export and import
} -setup {
} -body {
  namespace eval ::testns_exp {
    namespace export myCmd
    proc myCmd {} { return "exported" }
  }
  namespace import ::testns_exp::myCmd
  myCmd
} -cleanup {
  catch {rename myCmd ""}
  catch {namespace delete ::testns_exp}
} -result {exported}}

###############################################################################
#
# Section 3 -- package commands
#
###############################################################################

runTest {test introspect-3.1 {
  R-10701-37097: package provide registers version
} -body {
  package provide TestPkg_Intr 2.0
  package provide TestPkg_Intr
} -cleanup {
  catch {package forget TestPkg_Intr}
} -result {2.0}}

###############################################################################

runTest {test introspect-3.2 {
  R-10701-37097: package ifneeded registers script
} -setup {
} -body {
  package ifneeded TestIfNeeded_Intr 1.0 {set x loaded}
  package ifneeded TestIfNeeded_Intr 1.0
} -cleanup {
  catch {package forget TestIfNeeded_Intr}
  unset -nocomplain x
} -result {set x loaded}}

###############################################################################

runTest {test introspect-3.3 {
  R-10701-37097: package names lists packages
} -body {
  package provide TestNames_Intr 1.0
  expr {[lsearch [package names] TestNames_Intr] >= 0}
} -cleanup {
  catch {package forget TestNames_Intr}
} -result {1}}

###############################################################################
#
# Section 4 -- error model
#
###############################################################################

runTest {test introspect-4.1 {
  R-34446-28679: errorInfo is set on error
} -setup {
} -body {
  catch {error "test error"} msg
  expr {[string length $::errorInfo] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test introspect-4.2 {
  R-34446-28679: catch captures error message
} -setup {
} -body {
  catch {error "captured"} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {captured}}

###############################################################################

runTest {test introspect-4.3 {
  R-34446-28679: error command sets result
} -setup {
} -body {
  list [catch {error "oops"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 oops}}

###############################################################################

runTest {test introspect-4.4 {
  R-34446-28679: errorInfo accumulates stack trace
} -setup {
} -body {
  proc ::errproc {} { error "deep" }
  catch {::errproc} msg
  string match "*deep*errproc*" $::errorInfo
} -cleanup {
  catch {rename ::errproc ""}
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 5 -- clock and pid
#
###############################################################################

runTest {test introspect-5.1 {
  R-34446-28679: clock seconds returns integer
} -constraints {
    clock_seconds
} -body {
  set t [clock seconds]
  expr {[string is integer $t] && $t > 0}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################

runTest {test introspect-5.2 {
  R-34446-28679: pid returns integer
} -body {
  set p [pid]
  string is integer $p
} -cleanup {
  unset -nocomplain p
} -result {1}}

###############################################################################
#
# Section 6 -- tcl_platform array
#
###############################################################################

runTest {test introspect-6.1 {
  R-35060-23937: tcl_platform(user) contains user name
} -body {
  expr {[string length $::tcl_platform(user)] > 0}
} -result {1}}

###############################################################################

runTest {test introspect-6.2 {
  R-48861-57662: tcl_platform(host) contains hostname
} -constraints {
    th8
} -body {
  expr {[string length $::tcl_platform(host)] > 0}
} -result {1}}

###############################################################################

runTest {test introspect-6.3 {
  R-01127-40264: tcl_platform(platform) is windows or unix
} -body {
  expr {$::tcl_platform(platform) eq "unix"
    || $::tcl_platform(platform) eq "windows"}
} -result {1}}

###############################################################################

runTest {test introspect-6.4 {
  R-35186-60631: tcl_platform(engine) identifies the implementation (TIP #440,
                 Tcl 8.6.6+)
} -constraints {
    tip440
} -body {
  expr {[string length $::tcl_platform(engine)] > 0}
} -result {1}}

###############################################################################

runTest {test introspect-6.5 {
  R-50835-55889: tcl_platform(patchLevel) is a version string
} -constraints {
    th8
} -body {
  regexp {^\d+\.\d+} $::tcl_platform(patchLevel)
} -result {1}}

###############################################################################

runTest {test introspect-6.6 {
  R-30439-28378: tcl_platform(debug) is "1" on debug builds and absent on
                 release builds
} -constraints {
    th8
} -body {
  #
  # Either the element is absent (release build) or it equals "1" (debug
  # build).  No other value is valid.  We do not assert which build this
  # is — that depends on TH8_DEBUG at compile time — only that the
  # invariant holds.
  #
  expr {![info exists ::tcl_platform(debug)]
    || $::tcl_platform(debug) eq "1"}
} -result {1}}

###############################################################################

runTest {test introspect-6.7 {
  R-60831-14829: tcl_version global scalar is the "major.minor" Tcl language
                 version string ("8.6" under TH8)
} -constraints {
    th8
} -body {
  set ::tcl_version
} -result {8.6}}

###############################################################################

runTest {test introspect-6.8 {
  R-55601-40893: tcl_patchLevel global scalar is the "major.minor.patch" Tcl
                 language patch level string with leading "major.minor"
                 matching ::tcl_version ("8.6.19" under TH8)
} -constraints {
    th8
} -body {
  set ::tcl_patchLevel
} -result {8.6.19}}

###############################################################################
#
# Section 7 -- R-marker coverage: time command
#
###############################################################################

runTest {test introspect-7.1 {
  R-21676-43295: time evaluates script count times, returns timing string
} -constraints {
    time
} -setup {
} -body {
  set result [time {expr {1+1}} 10]
  #
  # The result must match "N microseconds per iteration"
  # where N is a number (integer in TH8, may be float in Tcl).
  #
  regexp {^[0-9.e+\-]+ microseconds per iteration$} $result
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test introspect-7.2 {
  R-36514-28636: time defaults to single iteration
} -constraints {
    time
} -setup {
} -body {
  set result [time {expr {2+2}}]
  regexp {^[0-9.e+\-]+ microseconds per iteration$} $result
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test introspect-7.3 {
  R-33048-52619: time propagates script errors
} -constraints {
    time
} -setup {
} -body {
  set rc [catch {time {error "boom"} 5} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 boom}}

###############################################################################

source tests/epilogue.tcl
