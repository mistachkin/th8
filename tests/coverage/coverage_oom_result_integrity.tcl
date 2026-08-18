###############################################################################
#
# coverage_oom_result_integrity.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# TH8K-030 differential OOM result-integrity gate.  Under an injected
# allocation failure, a value-producing command must return EITHER the
# (semantically) correct result OR a reported error -- never TH8_OK with a
# silently truncated, empty, or otherwise wrong result.
#
# For each command in the corpus this test captures a fault-free baseline
# {rc0 res0 allocCount}, then sweeps a single allocation failure across every
# allocation the command makes (::th8testlib::fault eval -allocFailAfter N).
# A DEGRADATION is a trip where the fault fired, the command returned success
# (rc==0), yet the result differs from the baseline.  The comparison is
# list-aware: [list a b] under memory pressure may fall back to the equally
# valid quoting "{a} b", which is the SAME list and NOT a degradation.
#
# The gap this closes: the existing OOM tests were coverage-reachability
# harnesses (assert the faulted branch ran without crashing); none asserted the
# observable result was correct-or-errored.  Nearly the whole command surface
# silently returned empty/"out of memory" with a success code when the terminal
# Th8_SetResult (or a variable store, or an NRE callback push) hit OOM.  The fix
# is a dispatcher chokepoint (th8InvokeCommand / th8EvalCommon promote a
# would-be success to an error when a result-building allocation failed); this
# test guards against regression.  The result must be an EMPTY list.
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

runTest {test oom_result_integrity-1.1 {
  No value-producing command silently degrades its result under an injected
  allocation failure: for every fired-fault trip that returns success, the
  result equals the fault-free baseline (list-aware, so alternate valid quoting
  is accepted).  Any degrader is listed as "cmd@trip:(result)".  The result
  must be empty (TH8K-030).
} -constraints {
    th8 fault_injection
} -setup {
  unset -nocomplain degraders corpus s g res0 ac t o r ok
} -body {
  set corpus {
    {format ABC} {format %05d 42} {string repeat Z 5} {string toupper abcdef}
    {string range abcdefgh 2 5} {string map {a X b Y} aabbcc}
    {string trim "  hi  "} {string cat a b c} {string length hello}
    {string reverse abcde} {list a b c d e} {concat a b c d} {join {a b c} -}
    {split a,b,c ,} {lreverse {a b c d}} {lsort {c a b}} {lrange {a b c} 0 1}
    {linsert {a b c} 1 X} {lreplace {a b c} 1 1 X} {llength {a b c}}
    {lindex {a b c} 1} {lsearch {a b c} b} {subst {abc}} {expr {2+2}}
    {return hello} {set _oomv hello} {append _ooma hi} {lappend _ooml x y}
    {incr _oomn 5} {binary format a3 abc} {dict get {a 1 b 2} a}
    {dict keys {a 1 b 2}} {dict merge {a 1} {b 2}}
  }
  set degraders {}
  foreach s $corpus {
    set g [::th8testlib::fault eval $s]
    if {[lindex $g 0] != 0} continue
    set res0 [lindex $g 1]
    set ac [lindex $g 2]
    for {set t 1} {$t <= $ac} {incr t} {
      set o [::th8testlib::fault eval $s -allocFailAfter $t]
      if {[lindex $o 3] == 0 || [lindex $o 0] != 0} continue
      set r [lindex $o 1]
      set ok [expr {$r eq $res0}]
      if {!$ok} {
        catch {
          set ok [expr {[llength $r] == [llength $res0] &&
                        [join $r \x00] eq [join $res0 \x00]}]
        }
      }
      if {!$ok} {
        lappend degraders "$s@$t:($r)"
        break
      }
    }
  }
  set degraders
} -cleanup {
  unset -nocomplain degraders corpus s g res0 ac t o r ok
} -result {}}

###############################################################################

source tests/epilogue.tcl
