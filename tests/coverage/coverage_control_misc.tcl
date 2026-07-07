###############################################################################
#
# coverage_control_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [if] / [switch] / [try]
# decisions in src/plugins/th8_control.c, focusing on the
# SPECIFIC missing vectors per llvm-cov:
#
#   :803  if (i < argc && th8StrEq(... "then"))
#                 ([if] optional "then" keyword)
#                 missing C1-pair: i >= argc -- short-circuit
#                 because no more arguments after expr
#
#   :1054 if (a >= 'A' && a <= 'Z') a += 32;
#                 (switch -nocase ASCII-fold)
#                 missing C2-pair: a >= 'A' AND a > 'Z'
#                 -- lowercase letters in pattern/string
#
#   :1601 || Th8_Memcmp(... "finally", 8) != 0
#                 (try-finally validation)
#                 missing C1-pair: argl[2] != 7 -- different
#                 length keyword (e.g. "other" of length 5)
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

runTest {test ctlmisc-1.1 {
  if with no body (just an expression) drives the (F,-)
  vector at th8_control.c:803 -- after parsing the
  condition, i has advanced past argc, so the optional
  "then" check short-circuits on i < argc.
} -constraints {
    th8
} -body {
  list \
      [catch {if {1}}] \
      [catch {if {1} then}]
} -result {1 1}}

###############################################################################

runTest {test ctlmisc-2.1 {
  switch -nocase with LOWERCASE patterns and strings drives
  the (T, F) vector at th8_control.c:1054 -- the case-fold
  inner check finds a >= 'A' but a > 'Z' (a is in 'a'..'z'
  range), so the "fold" branch is skipped and the char is
  used as-is.  Existing -nocase tests use uppercase
  patterns which only drive (T,T).
} -constraints {
    th8
} -body {
  set r {}
  switch -nocase -- "abc" {
      abc { set r ok }
      default { set r wrong }
  }
  set r
} -cleanup {
  unset -nocomplain r
} -result {ok}}

###############################################################################

runTest {test ctlmisc-3.1 {
  try ... <nonexistent-keyword> ... drives the (T, -)
  vector at th8_control.c:1601 -- argl[2] != 7 short-
  circuits the OR, and the "expected finally" error fires
  with the offending keyword name embedded.
} -constraints {
    th8
} -body {
  set rc [catch {try {set x 1} other {set x 2}} m]
  list $rc [string match {*expected*finally*} $m]
} -cleanup {
  unset -nocomplain x rc m
} -result {1 1}}

###############################################################################

runTest {test ctrl_misc-4.1 {
  switch with FALL-THROUGH using "-" body drives the
  C1/C2 pairs at th8_control.c:1265-1267 -- when a
  pattern matches but its body is "-", the fall-through
  loop walks j past consecutive "-" bodies.  We use the
  multi-arg form (pattern/body as SEPARATE args) to hit
  the L1265 path; the single-list-arg form takes the
  parallel L1222 path.
} -constraints {
    th8
} -body {
  set rr {}
  switch x  x  - y { set rr matched_y }
  set r1 $rr
  switch a  a  - b - c { set rr matched_c }
  set r2 $rr
  switch -- z  x { set rr wrong } z - y { set rr matched_y_via_z }
  set r3 $rr
  list $r1 $r2 $r3
} -cleanup {
  unset -nocomplain r1 r2 r3 rr
} -result {matched_y matched_c matched_y_via_z}}

###############################################################################

runTest {test ctrl_misc-4.2 {
  subst with a SHORT (length < 2) non-option arg between
  flags drives the C1=F vector at th8_control.c:1344
  (`argl[i] >= 2 && argv[i][0] == '-'`).  The 1-char
  arg fails C1, the loop breaks, and the post-loop
  arg-count check produces "wrong # args".  Using a
  non-dash multi-char arg drives C2=F similarly.
} -constraints {
    th8
} -body {
  set rcs {}
  # C1=F: 1-char positional arg between flag and body
  lappend rcs [catch {subst -nobackslashes - body} m]
  # C2=F: multi-char non-dash arg between flag and body
  lappend rcs [catch {subst -nobackslashes foo body} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

runTest {test ctrl_misc-4.3 {
  switch with MULTIPLE patterns where the FIRST matches
  with a NON-"-" body drives the C2=F vector at
  th8_control.c:1265-1267 -- the fall-through scanner
  enters the loop, sees argv[j] != "-" (real body), and
  exits via C2.  Existing fall-through tests cover C1=F
  (end-of-args) and C2=T ("-" body); this closes C2-pair.
} -constraints {
    th8
} -body {
  set rcs {}
  switch a  a  {set rr matched_a}  b  {set rr matched_b}
  lappend rcs $rr
  switch x  a  {set rr A}  x  {set rr X}  z  {set rr Z}
  lappend rcs $rr
  switch -- z  a  {set rr A}  b  {set rr B}  z  {set rr Z}
  lappend rcs $rr
  set rcs
} -cleanup {
  unset -nocomplain rcs rr
} -result {matched_a X Z}}

###############################################################################

runTest {test ctrl_misc-5.1 {
  Unknown command at TOP LEVEL (current ns IS global ns)
  drives the C2=F vector at th8NRCmdDispatch
  (th8_core.c:12793-12795) -- pEntry is NULL after the
  current-ns hash lookup, but interp->pCurrentNs ==
  pGlobalNs, so the fall-back search in the global ns is
  SKIPPED.  The unknown handler then fires.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {ctrl_misc_unknown_xyz_abcdefg} m]
  lappend rcs [catch {ctrl_misc_no_such_command_qwerty} m]
  lappend rcs [catch {ctrl_misc_zzzz_nope} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test ctrl_misc-6.1 {
  if/elseif EXPR with no following BODY drives the C1=F
  vector at th8_control.c:802 -- after the elseif EXPR
  is evaluated, i is advanced past argc, causing the
  optional "then" check to short-circuit on i < argc.
  Specifically `if 0 body1 elseif 1` (argc=5) reaches
  the elseif iteration which evaluates EXPR at i=4 then
  increments to i=5 == argc, driving C1=F.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {if 0 {set rr a} elseif 1} m]
  lappend rcs [catch {if 0 {set rr a} elseif 0 {set rr b} elseif 1} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs rr m
} -result {1 1}}

###############################################################################

runTest {test ctrl_misc-7.1 {
  yield with an EMPTY-STRING argument drives the C2=F
  vector at th8_core.c:8092 (Th8_CoroYield value-store
  branch) -- zValue is non-NULL (points to the empty
  string) but nValue == 0, so the allocate-and-copy
  branch is skipped and the alternate else-branch
  zeroes the yield-value fields.  Existing tests cover
  yield with a non-empty value (C2=T) and yield with no
  argument (C1=F); this closes the C2-pair.
} -constraints {
    th8
} -body {
  set rcs {}
  set co [coroutine ::ctlm71co apply {{} {
      yield ""
      yield ""
      return done
  }}]
  lappend rcs [string length $co]
  set r1 [::ctlm71co]
  lappend rcs [string length $r1]
  set r2 [::ctlm71co]
  lappend rcs $r2
  set rcs
} -cleanup {
  unset -nocomplain rcs co r1 r2
} -result {0 0 done}}

###############################################################################

runTest {test ctrl_misc-8.1 {
  Invoking an UNKNOWN command from the global namespace
  drives the C2=F vector at th8_core.c (th8NRCmdDispatch
  command-lookup, ~L12832) -- the command lookup in
  pCurrentNs (which IS pGlobalNs from the top level)
  returns NULL (C1=T) AND pCurrentNs == pGlobalNs (C2=F),
  so the namespace fallback to pGlobalNs is skipped.
  Existing tests typically invoke unknown commands from
  inside a namespace eval (C2=T); this closes the C2-pair
  at the top-level dispatch.  The forms below mix
  argument-substitution shapes ([expr], $var) to force
  the dispatcher down the NRE path (th8NRCmdDispatch),
  not just th8EvalIteration's fast path.
} -constraints {
    th8
} -body {
  set rcs {}
  set x 42
  catch {bogusGlobalCmd123 a b c} m1
  lappend rcs [string match {*no such command*} $m1]
  catch {neverDefinedXyz [expr {1+1}]} m2
  lappend rcs [string match {*no such command*} $m2]
  catch {alsoUnknown42 $x [expr {2*3}] $x} m3
  lappend rcs [string match {*no such command*} $m3]
  catch {missingCmd99 [list $x $x]} m4
  lappend rcs [string match {*no such command*} $m4]
  set rcs
} -cleanup {
  unset -nocomplain rcs x m1 m2 m3 m4
} -result {1 1 1 1}}

###############################################################################

source tests/epilogue.tcl
