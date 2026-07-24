###############################################################################
#
# taint.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the TH8 taint-tracking mechanism.  Taint is carried in the
# high bit (TH8_TAINT_BIT) of a size_t length field; a value derived
# from untrusted input is "tainted".  The evaluator refuses to run a
# tainted script, and taint must propagate through result storage,
# variable storage, substitution, concatenation, lists, and stored
# code so the gate cannot be bypassed by laundering the tag away.
#
# The test-only ::th8testlib::taint / result_tainted / arg_tainted /
# eval_tainted commands (src/test/th8_testlib.c) create and inspect
# taint at the C boundary, which ordinary script code cannot do.
#
# R-markers are attached once the taint-propagation requirements are
# formalized in the language standard.
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
# Section 1 -- Result storage (Th8_SetResult family)
#
###############################################################################

runTest {test taint-1.1 {
  Th8_SetResult must preserve the taint bit in the stored result
  length (interp->nResult).  result_tainted stores a tainted value
  and reports TH8_TAINTED(interp->nResult) via Th8_GetResult.
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::result_tainted hello
} -result {1}}

###############################################################################

runTest {test taint-1.2 {
  A plain (untainted) value must NOT report as tainted -- taint is not
  spuriously set.
} -constraints {
    loadLib th8
} -body {
  string is tainted clean
} -result {0}}

###############################################################################
#
# Section 2 -- Evaluation gate (the headline defect)
#
###############################################################################

runTest {test taint-2.1 {
  A tainted script must be REJECTED by the evaluator and must NOT
  produce a side effect.  eval_tainted forces the taint bit on the
  length passed to Th8_Eval; it returns 1 if the script executed
  (gate failed) and 0 if rejected.  The probe variable proves no
  side effect ran.
} -constraints {
    loadLib th8
} -setup {
  set ::taint_probe none
} -body {
  set executed [::th8testlib::eval_tainted {set ::taint_probe hit}]
  list $executed $::taint_probe
} -cleanup {
  unset -nocomplain ::taint_probe executed
} -result {0 none}}

###############################################################################

runTest {test taint-2.2 {
  A clean script through the same primitive path still runs (the gate
  rejects only tainted scripts).  eval_tainted always forces taint, so
  a dedicated clean control uses [eval] instead.
} -constraints {
    loadLib th8
} -setup {
  set ::taint_probe2 none
} -body {
  eval {set ::taint_probe2 ran}
  set ::taint_probe2
} -cleanup {
  unset -nocomplain ::taint_probe2
} -result {ran}}

###############################################################################
#
# Section 3 -- Command-substitution propagation
#
###############################################################################

runTest {test taint-3.1 {
  A tainted command-substitution result must arrive at the enclosing
  command as a tainted argument.  arg_tainted reports
  TH8_TAINTED(argl[1]).
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::arg_tainted [::th8testlib::taint x]
} -result {1}}

###############################################################################

runTest {test taint-3.2 {
  string is tainted observes the propagated tag on a command
  substitution.
} -constraints {
    loadLib th8
} -body {
  string is tainted [::th8testlib::taint x]
} -result {1}}

###############################################################################
#
# Section 4 -- Variable storage / substitution propagation
#
###############################################################################

runTest {test taint-4.1 {
  Storing a tainted value in a variable and reading it back must
  preserve taint (variable-read substitution).
} -constraints {
    loadLib th8
} -setup {
  set v [::th8testlib::taint x]
} -body {
  ::th8testlib::arg_tainted $v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################

runTest {test taint-4.2 {
  string is tainted on a tainted variable value.
} -constraints {
    loadLib th8
} -setup {
  set v [::th8testlib::taint x]
} -body {
  string is tainted $v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################
#
# Section 5 -- Concatenation (append) taint is old OR appended
#
###############################################################################

runTest {test taint-5.1 {
  Appending a tainted value to a clean variable taints the result.
} -constraints {
    loadLib th8
} -setup {
  set v clean
} -body {
  append v [::th8testlib::taint dirty]
  string is tainted $v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################

runTest {test taint-5.2 {
  Appending a clean value to a tainted variable leaves it tainted.
} -constraints {
    loadLib th8
} -setup {
  set v [::th8testlib::taint dirty]
} -body {
  append v clean
  string is tainted $v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################
#
# Section 6 -- End-to-end: [eval] of a tainted script is rejected
#
###############################################################################

runTest {test taint-6.1 {
  The script-level [eval] of a tainted script must fail closed and not
  run: result storage + command substitution + the eval command must
  carry the tag to the evaluation gate.
} -constraints {
    loadLib th8
} -setup {
  set ::taint_e2e none
} -body {
  set rc [catch {eval [::th8testlib::taint {set ::taint_e2e hit}]} msg]
  list $rc $::taint_e2e
} -cleanup {
  unset -nocomplain ::taint_e2e rc msg
} -result {1 none}}

###############################################################################
#
# Section 7 -- Stored code (proc / apply bodies)
#
###############################################################################

runTest {test taint-7.1 {
  A proc whose body is tainted must be rejected when the proc runs
  (the stored body reaches the evaluation gate) with no side effect.
} -constraints {
    loadLib th8
} -setup {
  set ::taint_proc_probe none
  proc taint_tp {} [::th8testlib::taint {set ::taint_proc_probe hit}]
} -body {
  set rc [catch {taint_tp} msg]
  list $rc $::taint_proc_probe
} -cleanup {
  catch {rename taint_tp {}}
  unset -nocomplain ::taint_proc_probe rc msg
} -result {1 none}}

###############################################################################

runTest {test taint-7.2 {
  apply with a tainted lambda body is rejected at evaluation.
} -constraints {
    loadLib th8
} -setup {
  set ::taint_apply_probe none
} -body {
  set body [::th8testlib::taint {set ::taint_apply_probe hit}]
  set rc [catch {apply [list {} $body]} msg]
  list $rc $::taint_apply_probe
} -cleanup {
  unset -nocomplain ::taint_apply_probe body rc msg
} -result {1 none}}

###############################################################################
#
# Section 8 -- Lists: elements of a tainted list are tainted
#
###############################################################################

runTest {test taint-8.1 {
  An element extracted from a tainted serialized list is tainted
  (splitting conservatively tags every returned element).
} -constraints {
    loadLib th8
} -setup {
  set ::tl [::th8testlib::taint {alpha beta gamma}]
} -body {
  string is tainted -strict [lindex $::tl 1]
} -cleanup {
  unset -nocomplain ::tl
} -result {1}}

###############################################################################

runTest {test taint-8.2 {
  lrange of a tainted list yields a tainted sublist.
} -constraints {
    loadLib th8
} -setup {
  set ::tl [::th8testlib::taint {alpha beta gamma delta}]
} -body {
  string is tainted -strict [lrange $::tl 1 2]
} -cleanup {
  unset -nocomplain ::tl
} -result {1}}

###############################################################################

runTest {test taint-8.3 {
  Cache raw + re-apply, clean -> tainted order: a clean split of a
  list value populates the split cache; a later tainted split of the
  same bytes (a warm cache hit) must still yield tainted elements --
  the clean cache entry must not launder the tainted value.
} -constraints {
    loadLib th8
} -setup {
  set clean {alpha beta gamma}
  set dirty [::th8testlib::taint {alpha beta gamma}]
} -body {
  set a [string is tainted -strict [lindex $clean 1]]
  set b [string is tainted -strict [lindex $dirty 1]]
  list $a $b
} -cleanup {
  unset -nocomplain clean dirty a b
} -result {0 1}}

###############################################################################

runTest {test taint-8.4 {
  Cache raw + re-apply, tainted -> clean order: a tainted split
  populates the cache with RAW elements; a later clean split of the
  same bytes (warm cache hit) must yield clean elements -- the tainted
  split must not contaminate the cache.
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {one two three}]
  set clean {one two three}
} -body {
  set a [string is tainted -strict [lindex $dirty 1]]
  set b [string is tainted -strict [lindex $clean 1]]
  list $a $b
} -cleanup {
  unset -nocomplain dirty clean a b
} -result {1 0}}

###############################################################################

runTest {test taint-8.5 {
  llength of a tainted list is a count-only split (element pointers
  NULL); it must not crash or mis-tag, and the count is a clean
  integer.
} -constraints {
    loadLib th8
} -setup {
  set tl [::th8testlib::taint {a b c d}]
} -body {
  list [llength $tl] [string is tainted -strict [llength $tl]]
} -cleanup {
  unset -nocomplain tl
} -result {4 0}}

###############################################################################

runTest {test taint-8.6 {
  A value extracted from a tainted dict is tainted.  The dict split
  arrays hold RAW lengths (so internal key comparison and copies work
  byte-exactly); dict get re-applies the source dict's taint to the
  value handed back to the script.
} -constraints {
    loadLib th8
} -setup {
  set td [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  string is tainted -strict [dict get $td k2]
} -cleanup {
  unset -nocomplain td
} -result {1}}

###############################################################################

runTest {test taint-8.7 {
  dict keys of a tainted dict yields a tainted key list; a clean dict
  yields a clean list (the taint derives from the source dict, not the
  raw split arrays).
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1 k2 v2}]
  set clean {k1 v1 k2 v2}
} -body {
  list [string is tainted -strict [dict keys $dirty]] \
      [string is tainted -strict [dict keys $clean]]
} -cleanup {
  unset -nocomplain dirty clean
} -result {1 0}}

###############################################################################

runTest {test taint-8.8 {
  dict values of a tainted dict yields a tainted value list; a clean
  dict yields a clean list.
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1 k2 v2}]
  set clean {k1 v1 k2 v2}
} -body {
  list [string is tainted -strict [dict values $dirty]] \
      [string is tainted -strict [dict values $clean]]
} -cleanup {
  unset -nocomplain dirty clean
} -result {1 0}}

###############################################################################

runTest {test taint-8.9 {
  Count-only split of a tainted list (the llength shape: element
  pointers NULL) must not crash and must return the correct count.
  Drives the Th8_SplitList element-tagging guard
  `if (nListTag && panElem && *panElem)` with {nListTag=T, panElem=NULL}
  on the cache-miss parse path (splitlist_probe forces taint and passes
  TH8_LIST_NO_CACHE).
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::splitlist_probe count {alpha beta gamma}
} -result {3}}

###############################################################################

runTest {test taint-8.10 {
  Lengths-only split of a tainted list (panElem supplied, pazElem NULL)
  leaves *panElem NULL because the element-copy block is skipped.
  Drives the same guard with {nListTag=T, panElem!=NULL, *panElem=NULL},
  and returns the element count.
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::splitlist_probe lens {alpha beta gamma}
} -result {3}}

###############################################################################
#
# Section 9 -- Derived strings retain taint when they keep bytes
#
###############################################################################

runTest {test taint-9.1 {
  string range of a tainted string retains taint (the output keeps
  attacker-controlled bytes).
} -constraints {
    loadLib th8
} -setup {
  set ::ts [::th8testlib::taint abcdef]
} -body {
  string is tainted -strict [string range $::ts 1 3]
} -cleanup {
  unset -nocomplain ::ts
} -result {1}}

###############################################################################

runTest {test taint-9.2 {
  string toupper of a tainted string retains taint.
} -constraints {
    loadLib th8
} -setup {
  set ::ts [::th8testlib::taint abcdef]
} -body {
  string is tainted -strict [string toupper $::ts]
} -cleanup {
  unset -nocomplain ::ts
} -result {1}}

###############################################################################
#
# Section 10 -- expr rejects a tainted complete expression
#
###############################################################################

runTest {test taint-10.1 {
  A tainted complete expression is rejected by expr (an expression
  can carry command substitution and side effects).
} -constraints {
    loadLib th8
} -body {
  catch {expr [::th8testlib::taint {1 + 1}]} msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test taint-10.2 {
  A clean expression consuming a tainted operand is allowed and
  produces a clean canonical result.
} -constraints {
    loadLib th8
} -setup {
  set ::to [::th8testlib::taint 41]
} -body {
  set r [expr {$::to + 1}]
  list $r [string is tainted -strict $r]
} -cleanup {
  unset -nocomplain ::to r
} -result {42 0}}

###############################################################################
#
# Section 11 -- a tainted substituted command name is rejected
#
###############################################################################

runTest {test taint-11.1 {
  A tainted substituted command name is rejected (selecting the
  command to run is code selection) with no side effect.
} -constraints {
    loadLib th8
} -setup {
  set ::taint_cmd_probe none
} -body {
  set c [::th8testlib::taint set]
  set rc [catch {$c ::taint_cmd_probe hit} msg]
  list $rc $::taint_cmd_probe
} -cleanup {
  unset -nocomplain ::taint_cmd_probe c rc msg
} -result {1 none}}

###############################################################################

runTest {test taint-11.2 {
  A tainted substituted command name is ALSO rejected on the NRE /
  command-substitution dispatch path.  The command text contains "["
  (the "[::th8testlib::taint set]" substitution), which forces
  th8NRCmdDispatch -- whose tainted-command-name check was previously
  missing (Bug 64).  Rejected with no side effect, matching taint-11.1
  (the synchronous path).
} -constraints {
    loadLib th8
} -setup {
  set ::taint_cmd_probe none
} -body {
  set rc [catch {[::th8testlib::taint set] ::taint_cmd_probe hit} msg]
  list $rc $::taint_cmd_probe
} -cleanup {
  unset -nocomplain ::taint_cmd_probe rc msg
} -result {1 none}}

###############################################################################
#
# Section 12 -- dict derive commands retain the source dict's taint
#
###############################################################################

runTest {test taint-12.1 {
  dict filter of a tainted dict yields a tainted subset; a clean dict
  yields a clean result.
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1 k2 v2}]
  set clean {k1 v1 k2 v2}
} -body {
  list [string is tainted -strict [dict filter $dirty key k*]] \
      [string is tainted -strict [dict filter $clean key k*]]
} -cleanup {
  unset -nocomplain dirty clean
} -result {1 0}}

###############################################################################

runTest {test taint-12.2 {
  dict merge taints the result if ANY input dict is tainted; a merge of
  only clean dicts stays clean.
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1}]
  set clean {k2 v2}
} -body {
  list [string is tainted -strict [dict merge $clean $dirty]] \
      [string is tainted -strict [dict merge $clean {k3 v3}]]
} -cleanup {
  unset -nocomplain dirty clean
} -result {1 0}}

###############################################################################

runTest {test taint-12.3 {
  dict remove from a tainted dict yields a tainted result.
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  string is tainted -strict [dict remove $dirty k1]
} -cleanup {
  unset -nocomplain dirty
} -result {1}}

###############################################################################

runTest {test taint-12.4 {
  dict replace on a tainted dict yields a tainted result even when the
  replacement value is clean (retained entries carry the source taint).
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  string is tainted -strict [dict replace $dirty k2 CLEAN]
} -cleanup {
  unset -nocomplain dirty
} -result {1}}

###############################################################################
#
# Section 13 -- dict variable-mutating commands: correctness on a
# tainted dict variable, and taint of the stored result.  The split
# arrays must be raw so key lookup (th8DictFind) matches byte-exactly;
# the taint is re-applied at write-back.
#
###############################################################################

runTest {test taint-13.1 {
  dict set on a tainted dict VARIABLE must still find and replace an
  existing key (not append a duplicate): the internal key comparison
  must not be defeated by the variable's taint bit.
} -constraints {
    loadLib th8
} -setup {
  set d [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  dict set d k2 CHANGED
  list [dict get $d k1] [dict get $d k2] [dict size $d]
} -cleanup {
  unset -nocomplain d
} -result {v1 CHANGED 2}}

###############################################################################

runTest {test taint-13.2 {
  The dict written back by dict set on a tainted variable stays tainted.
} -constraints {
    loadLib th8
} -setup {
  set d [::th8testlib::taint {k1 v1}]
} -body {
  dict set d k2 v2
  string is tainted -strict $d
} -cleanup {
  unset -nocomplain d
} -result {1}}

###############################################################################

runTest {test taint-13.3 {
  dict append on a tainted dict variable finds the existing key,
  concatenates to its value, and leaves the variable tainted.
} -constraints {
    loadLib th8
} -setup {
  set d [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  dict append d k1 XYZ
  list [dict get $d k1] [dict size $d] [string is tainted -strict $d]
} -cleanup {
  unset -nocomplain d
} -result {v1XYZ 2 1}}

###############################################################################

runTest {test taint-13.4 {
  dict unset on a tainted dict variable removes the key (found via the
  raw comparison) and keeps the remaining dict tainted.
} -constraints {
    loadLib th8
} -setup {
  set d [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  dict unset d k1
  list [dict size $d] [dict exists $d k1] \
      [string is tainted -strict $d]
} -cleanup {
  unset -nocomplain d
} -result {1 0 1}}

###############################################################################

runTest {test taint-13.5 {
  dict update on a tainted dict variable: the key is found (raw
  comparison), the value bound to the body variable is tainted (no
  laundering), and the written-back dict stays tainted.
} -constraints {
    loadLib th8
} -setup {
  set d [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  set boundTainted -1
  dict update d k1 lv {
    set boundTainted [string is tainted -strict $lv]
    set lv NEWVAL
  }
  list $boundTainted [dict get $d k1] [dict size $d] \
      [string is tainted -strict $d]
} -cleanup {
  unset -nocomplain d lv boundTainted
} -result {1 NEWVAL 2 1}}

###############################################################################

runTest {test taint-13.6 {
  dict with on a tainted dict variable: bound key variables are tainted,
  the key is found, and the rebuilt dict stays tainted.
} -constraints {
    loadLib th8
} -setup {
  set d [::th8testlib::taint {k1 v1 k2 v2}]
} -body {
  set boundTainted -1
  dict with d {
    set boundTainted [string is tainted -strict $k1]
    set k1 CHANGED
  }
  list $boundTainted [dict get $d k1] [dict size $d] \
      [string is tainted -strict $d]
} -cleanup {
  unset -nocomplain d k1 k2 boundTainted
} -result {1 CHANGED 2 1}}

###############################################################################

runTest {test taint-13.7 {
  dict update on a NON-EXISTENT variable creates it from the clean
  arguments and the result is clean (the write-back must not fabricate
  taint when there is no tainted source; exercises the var-absent path).
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain fu
} -body {
  dict update fu k lv {set lv NEW}
  list [dict get $fu k] [string is tainted -strict $fu]
} -cleanup {
  unset -nocomplain fu lv
} -result {NEW 0}}

###############################################################################

runTest {test taint-13.8 {
  dict with on a NON-EXISTENT variable creates a clean (empty) dict
  (var-absent path); the result carries no spurious taint.
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain fw
} -body {
  dict with fw {set nk val}
  list [info exists fw] [string is tainted -strict $fw]
} -cleanup {
  unset -nocomplain fw nk
} -result {1 0}}

###############################################################################
#
# Section 14 -- dict for / dict map propagate taint to iteration
# variables and (for map) to the collected result
#
###############################################################################

runTest {test taint-14.1 {
  dict for over a tainted dict binds tainted key and value variables;
  over a clean dict they are clean.
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1}]
  set clean {k1 v1}
} -body {
  set td 0; set cd 0
  dict for {k v} $dirty {set td [list [string is tainted -strict $k] \
      [string is tainted -strict $v]]}
  dict for {k v} $clean {set cd [list [string is tainted -strict $k] \
      [string is tainted -strict $v]]}
  list $td $cd
} -cleanup {
  unset -nocomplain dirty clean k v td cd
} -result {{1 1} {0 0}}}

###############################################################################

runTest {test taint-14.2 {
  dict map over a tainted dict yields a tainted result; over a clean
  dict the result is clean.
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {k1 v1 k2 v2}]
  set clean {k1 v1 k2 v2}
} -body {
  set od [dict map {k v} $dirty {string toupper $v}]
  set oc [dict map {k v} $clean {string toupper $v}]
  list [dict get $od k1] [string is tainted -strict $od] \
      [string is tainted -strict $oc]
} -cleanup {
  unset -nocomplain dirty clean od oc k v
} -result {V1 1 0}}

###############################################################################
#
# Section 15 -- [subst] (Th8_Subst) propagates taint and is length-safe
#
###############################################################################

runTest {test taint-15.1 {
  subst of a tainted template yields a tainted result (the template
  bytes flow into the output); a clean template yields a clean result.
  Also exercises the length path: a tainted length must be masked
  before it is used as the scan bound (no over-read).
} -constraints {
    loadLib th8
} -setup {
  set dirty [::th8testlib::taint {plain text}]
} -body {
  list [string is tainted -strict [subst $dirty]] \
      [string is tainted -strict [subst {plain text}]]
} -cleanup {
  unset -nocomplain dirty
} -result {1 0}}

###############################################################################

runTest {test taint-15.2 {
  subst content is preserved byte-for-byte from a tainted template
  (proves the scan bound was masked, not run past the buffer).
} -constraints {
    loadLib th8
} -body {
  subst [::th8testlib::taint {abcdef}]
} -result {abcdef}}

###############################################################################

runTest {test taint-15.3 {
  A clean template that substitutes a tainted variable yields a tainted
  result: substitution taint accumulates in the output buffer.
} -constraints {
    loadLib th8
} -setup {
  set ::sv [::th8testlib::taint DIRTY]
} -body {
  string is tainted -strict [subst {value=$::sv}]
} -cleanup {
  unset -nocomplain ::sv
} -result {1}}

###############################################################################
#
# Section 16 -- sensitive results keep taint (trust is independent of
# sensitivity: Th8_SetResultSensitive)
#
###############################################################################

runTest {test taint-16.1 {
  Storing a tainted value as a SENSITIVE result (protected region) must
  preserve the taint bit: sensitivity and trust are independent
  classifications, so a sensitive value from untrusted input stays
  tainted.
} -constraints {
    loadLib th8 crypto_testlib
} -body {
  ::th8testlib::result_sensitive_tainted secret
} -result {1}}

###############################################################################
#
# Section 17 -- lappend on a tainted variable: length-safe and
# taint-preserving.  The structural-validation walk must mask the
# tagged length before using it as a scan bound (a tagged length is
# ~256 MiB and would over-read), while the append preserves taint.
#
###############################################################################

runTest {test taint-17.1 {
  lappend onto a tainted variable produces the correct list AND keeps
  the result tainted.  The validation walk must not over-read on the
  tagged length.
} -constraints {
    loadLib th8
} -setup {
  set v [::th8testlib::taint {a b c}]
} -body {
  lappend v d
  list $v [string is tainted -strict $v]
} -cleanup {
  unset -nocomplain v
} -result {{a b c d} 1}}

###############################################################################

runTest {test taint-17.2 {
  lappend of a clean value onto a clean variable stays clean (no
  spurious taint from the length-masking change).
} -constraints {
    loadLib th8
} -setup {
  set v {a b}
} -body {
  lappend v c
  list $v [string is tainted -strict $v]
} -cleanup {
  unset -nocomplain v
} -result {{a b c} 0}}

###############################################################################
#
# Section 18 -- the [list] element cache must not launder taint.  The
# cache is keyed and stored on RAW element bytes (taint-insensitive);
# the aggregate element taint is re-applied to the result on every
# hit/miss.  A tagged element length must never reach the cache's
# hash / allocation / copy as a raw byte count.
#
# COVERAGE NOTE: these tests detect the taint-LAUNDERING regression
# (masking the physical lengths but forgetting to re-apply the aggregate
# taint) on any build -- 18.1/18.2 then fail because a cached value
# crosses taint states.  The MEMORY-SAFETY regression (feeding the
# tagged length straight to the cache hash/alloc/copy as a byte count)
# is caught under AddressSanitizer only: on a normal build the ~256 MiB
# over-read lands in adjacent mapped heap without faulting and the
# correct result still comes from the Th8_ListAppend fallback, so the
# assertion passes.  The sanitize suite runs taint.tcl, so the over-read
# is covered there.
#
###############################################################################

runTest {test taint-18.1 {
  A clean [list] is cached, then a [list] of the SAME bytes with a
  tainted element must still be tainted -- the clean cache entry must
  not launder the tainted call.
} -constraints {
    loadLib th8
} -body {
  set clean [list alpha beta gamma]
  set dirty [list [::th8testlib::taint alpha] beta gamma]
  list [string is tainted -strict $clean] [string is tainted -strict $dirty]
} -cleanup {
  unset -nocomplain clean dirty
} -result {0 1}}

###############################################################################

runTest {test taint-18.2 {
  A tainted [list] is cached, then a [list] of the SAME bytes with all
  clean elements must be clean -- the tainted call must not contaminate
  the cache.
} -constraints {
    loadLib th8
} -body {
  set dirty [list [::th8testlib::taint one] two three]
  set clean [list one two three]
  list [string is tainted -strict $dirty] [string is tainted -strict $clean]
} -cleanup {
  unset -nocomplain dirty clean
} -result {1 0}}

###############################################################################

runTest {test taint-18.3 {
  A [list] with any tainted element is tainted regardless of position,
  and the joined bytes are correct (no over-read/over-copy on the
  tagged element length).
} -constraints {
    loadLib th8
} -body {
  set r [list a [::th8testlib::taint bcd] e]
  list $r [string is tainted -strict $r]
} -cleanup {
  unset -nocomplain r
} -result {{a bcd e} 1}}

###############################################################################

source tests/epilogue.tcl
