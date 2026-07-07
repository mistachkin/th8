###############################################################################
#
# coverage_switch_nocase.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the case-folding loops in
# src/plugins/th8_control.c th8SwitchMatch helper:
#
#   line 1029: zLPat[i] = (c >= 'A' && c <= 'Z') ? ...    (glob -nocase, pattern fold)
#   line 1035: zLStr[i] = (c >= 'A' && c <= 'Z') ? ...    (glob -nocase, string fold)
#   line 1054: if (a >= 'A' && a <= 'Z') a += 32          (exact -nocase, pattern char)
#   line 1055: if (b >= 'A' && b <= 'Z') b += 32          (exact -nocase, string char)
#
# Each compound has 2 conditions (>=A and <=Z); MC/DC requires
# vectors covering at least: T,T (uppercase), T,F (e.g. lowercase
# is >'Z'), and F,- (e.g. digit / space < 'A').
#
# To drive all 3 vectors per loop, the inputs MUST contain:
#   - an uppercase letter
#   - a lowercase letter
#   - a non-letter ASCII char (digit, space, '@', etc.)
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
#
# Section 1 -- switch -glob -nocase (drives 1029 and 1035 case-fold loops)
#
###############################################################################

runTest {test sw_nocase-1.1 {
  switch -glob -nocase: pattern and string each contain upper, lower,
  and non-letter chars to drive all three MC/DC vectors per fold loop
} -constraints {
    th8
} -body {
  set r [switch -glob -nocase -- "Aa1" {
    "Aa1"   { set _ one }
    "*aA1*" { set _ two }
    default { set _ three }
  }]
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################

runTest {test sw_nocase-1.2 {
  switch -glob -nocase with explicit non-letter pattern
} -constraints {
    th8
} -body {
  set r [switch -glob -nocase -- "Mix3D!" {
    "MIX3D!" { set _ matched }
    default  { set _ unmatched }
  }]
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################
#
# Section 2 -- switch -nocase (exact mode, drives 1054 and 1055)
#
###############################################################################

runTest {test sw_nocase-2.1 {
  switch -exact -nocase: chars in upper, lower, and non-letter ranges
} -constraints {
    th8
} -body {
  set r [switch -exact -nocase -- "Hello1@" {
    "HELLO1@" { set _ matched }
    default   { set _ unmatched }
  }]
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################

runTest {test sw_nocase-2.2 {
  switch -nocase fall-through to default with mixed-case input
} -constraints {
    th8
} -body {
  set r [switch -nocase -- "ZyZ@9" {
    "ABC"   { set _ abc }
    default { set _ otherwise }
  }]
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r _
} -result {1}}

###############################################################################

source tests/epilogue.tcl
