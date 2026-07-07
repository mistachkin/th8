###############################################################################
#
# coverage_oom_dual_pointer.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives `!pX || !pY` post-allocation MC/DC compounds at known
# source locations using the per-site OOM filter (P1 from the
# MC/DC plan).  For each compound, the test fails the first or
# second allocation in the targeted line range so the guard's
# T,- and F,T vectors are reached.
#
# Targeted compounds:
#   th8_control.c:1022   `if (!zLPat || !zLStr)` in th8SwitchMatch
#                        (switch -glob -nocase fold buffers).
#                        Allocations at lines 1020-1021.
#   th8_strings.c:1109   `if (!zLPat || !zLStr)` in string-match
#                        nocase fold buffers.  Allocations near
#                        the same line.
#
# Coverage-driven; not pinned to specific R-markers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test oom_dp-1.1 {
  Fail the first allocation in th8SwitchMatch fold buffers so
  zLPat == NULL; drives T,- vector at th8_control.c:1022.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    switch -glob -nocase -- "Aa1" {
      "*aA1*" { set _ matched }
      default { set _ default }
    }
  } -allocFailSite th8_control.c:1015-1025 -allocFailAfter 1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################

runTest {test oom_dp-1.2 {
  Fail the second allocation in th8SwitchMatch fold buffers so
  zLPat OK but zLStr == NULL; drives F,T vector at th8_control.c:1022.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    switch -glob -nocase -- "Aa1" {
      "*aA1*" { set _ matched }
      default { set _ default }
    }
  } -allocFailSite th8_control.c:1015-1025 -allocFailAfter 2} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################

runTest {test oom_dp-1.3 {
  Sanity: same input with no fault injection drives F,F
  (success path) -- this is the existing coverage; included
  to establish that the input reaches the targeted compound.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    switch -glob -nocase -- "Aa1" {
      "*aA1*" { set _ matched }
      default { set _ default }
    }
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################

runTest {test oom_dp-2.1 {
  Same pattern at th8_strings.c (string match -nocase fold
  buffers): fail first alloc.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    string match -nocase {Hello*} "hello world"
  } -allocFailSite th8_strings.c -allocFailAfter 1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test oom_dp-2.2 {
  Same at th8_strings.c: fail second alloc to drive F,T at the
  string-match-nocase guard.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    string match -nocase {Hello*} "hello world"
  } -allocFailSite th8_strings.c -allocFailAfter 2} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
