###############################################################################
#
# coverage_switch_oom.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for the OOM compound at
# src/plugins/th8_control.c:1042 in th8SwitchMatch:
#
#   if (!zLPat || !zLStr) { Th8_Free(...); Th8_Free(...); return 0; }
#
# 2-condition short-circuit OR.  Normal execution always has both
# allocations succeed -- only the (F,F) vector fires.  Sweeping
# the alloc-fail position across the two TH8_ALLOC_STR call sites
# at L1040 / L1041 drives the (T,_) and (F,T) vectors.
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

runTest {test sw_oom-1.1 {
  th8SwitchMatch L1042 (!zLPat || !zLStr) OOM compound: sweep
  -allocFailAfter across the th8_control.c:1040-1042 range.
  Each iteration positions the failure at a different allocation;
  at least one lands on the L1040 (zLPat) site and at least one
  on the L1041 (zLStr) site, driving both C1 and C2 (T,_) vectors.
  The script does a `switch -nocase -glob -- $val` where the value
  contains mixed-case letters so the nocase fold path is taken.
  Errors are silently swallowed since the goal is reaching the
  alloc sites, not a deterministic eval result.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  for {set n 1} {$n <= 8} {incr n} {
    catch {::th8testlib::fault eval {
      switch -nocase -glob -- "Hello" {
        hello { set _ matched }
        default { set _ default }
      }
    } -allocFailSite th8_control.c:1040-1042 \
      -allocFailAfter $n}
  }
  expr {1}
} -cleanup {
  unset -nocomplain n
} -result {1}}

###############################################################################

source tests/epilogue.tcl
