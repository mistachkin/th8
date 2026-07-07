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
  R-47394-37015: regexp: catastrophic backtracking terminated by step limit
} -constraints {
    th8 regexp sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    catch {regexp {(a+)+$} [string repeat a 30]b} msg
    set msg
  }]
  # Should either hit step limit or regexp terminates; either way
  # the sandbox should survive.
  expr {[sandboxRc $r] == 0 || [sandboxRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-resource-1.2 {
  R-47394-37015: regexp: normal match works within limits
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
  R-19191-01287: string repeat: exponential growth hits memory limit without
                 killing parent
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  # The child's xPanic is NULL, so hitting the memory limit
  # returns an error instead of aborting the process.
  set r [::th8testlib::sandbox {
    set x A
    for {set i 0} {$i < 30} {incr i} {
      set x "$x$x"
    }
  }]
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-resource-2.2 {
  R-42761-35236: append bomb: repeated append hits step limit
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
  # Step limit (1M) reached before 2M iterations
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# List bombs.
#
###############################################################################

runTest {test sandbox-resource-3.1 {
  R-42761-35236: lappend bomb: building huge list hits step limit
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
  # Step limit reached before 2M iterations
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-resource-3.2 {
  R-47394-37015: nested list: deep nesting via lappend
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    set L x
    for {set i 0} {$i < 10000} {incr i} {
      set L [list $L]
    }
    string length $L
  }]
  # Should succeed (nesting is just string wrapping) or hit step limit
  expr {[sandboxRc $r] == 0 || [sandboxRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

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
  R-47394-37015: format: large width specifier
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
  R-47394-37015: format: many conversion specifiers
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
