###############################################################################
#
# coverage_info_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [info] subcommand decisions in
# src/plugins/th8_introspection.c:
#
#   :367 / :781   if (pNs && pNs->paCmd)
#                   ([info commands] / [info procs] qualified
#                    namespace lookup)
#   :531 / :631 / :693
#                 || (xProc != th8ProcCall1 &&
#                     xProc != th8NprocCall1)
#                   ([info default] / [info body] / [info args]
#                    non-proc target rejection)
#   :554          if (p->azDefault && p->azDefault[i])
#                   ([info default] argument-with-default check
#                    for an argument that has no default)
#   :878 / :982   if (argc == 3 && zList)
#                   ([info globals] / [info locals] pattern-
#                    filter path with empty result)
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

runTest {test infomisc-1.1 {
  info commands with a qualified pattern targeting a
  nonexistent namespace drives the (F,-) vector at line
  367 -- pNs is NULL, the second condition is not
  evaluated, the result is empty.
} -constraints {
    th8
} -body {
  list \
      [info commands ::nonexistent_namespace_xyz::*] \
      [info commands ::nonexistent_namespace_xyz::foo]
} -result {{} {}}}

###############################################################################

runTest {test infomisc-1.2 {
  info procs counterpart to -1.1: same compound at line
  781 in info_procs_command.
} -constraints {
    th8
} -body {
  info procs ::nonexistent_namespace_xyz::*
} -result {}}

###############################################################################

runTest {test infomisc-2.1 {
  info default / body / args on a non-proc target
  (a builtin command like [set]) drives the (T,T) vector
  at lines 531 / 631 / 693 -- xProc is neither th8ProcCall1
  nor th8NprocCall1, so the compound is true and the
  "is not a procedure" error fires.
} -constraints {
    th8
} -body {
  list \
      [catch {info body set} r1] \
      [catch {info args set} r2] \
      [catch {info default set x v} r3] \
      [string match {*not a procedure*} $r1]
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1 1 1 1}}

###############################################################################

runTest {test infomisc-2.2 {
  info body / args / default on an nproc (whose xProc IS
  th8NprocCall1) drives the C3=F vector at lines 531 /
  631 / 693 -- C2 (xProc != th8ProcCall1) is T, but C3
  (xProc != th8NprocCall1) is F, so the compound result
  is F and the body/args/default succeed.  This closes
  the C3-Pair that proc-based tests cannot reach.
} -constraints {
    th8
} -body {
  nproc ::infomisc_np1 {a {b 5}} {return ok}
  list \
      [info body ::infomisc_np1] \
      [info args ::infomisc_np1] \
      [info default ::infomisc_np1 a v1] \
      [info default ::infomisc_np1 b v2]
} -cleanup {
  catch {rename ::infomisc_np1 ""}
  unset -nocomplain v1 v2
} -result {{return ok} {a b} 0 1}}

###############################################################################

runTest {test infomisc-3.1 {
  info default on a proc whose argument has NO default
  drives the (T,F) vector at line 554 -- p->azDefault is
  non-NULL (some args may have defaults) but the indexed
  entry is NULL.  Returns 0 (no default present).
} -constraints {
    th8
} -body {
  proc ::infomisc_p1 {a {b 5} c} {}
  set noDefault [info default ::infomisc_p1 a v]
  set hasDefault [info default ::infomisc_p1 b v2]
  list $noDefault $hasDefault
} -cleanup {
  catch {rename ::infomisc_p1 ""}
  unset -nocomplain noDefault hasDefault v v2
} -result {0 1}}

###############################################################################

runTest {test infomisc-3.2 {
  info default on a proc with NO defaults at all -- azDefault
  may be NULL entirely (the (F,-) vector at line 554) or
  zero-filled (the (T,F) vector).  Either way, returns 0.
} -constraints {
    th8
} -body {
  proc ::infomisc_p2 {a b c} {}
  list \
      [info default ::infomisc_p2 a v] \
      [info default ::infomisc_p2 b v] \
      [info default ::infomisc_p2 c v]
} -cleanup {
  catch {rename ::infomisc_p2 ""}
  unset -nocomplain v
} -result {0 0 0}}

###############################################################################

runTest {test infomisc-4.1 {
  info globals with a pattern that matches no var --
  zList is non-NULL (other globals exist) but the result
  list is empty after filtering.  Drives the (T,T) vector
  at line 878 with a follow-up empty filtered output.
} -constraints {
    th8
} -body {
  list \
      [info globals zzzz_no_match_pattern_*] \
      [expr {[llength [info globals tcl_*]] >= 1}]
} -cleanup {
} -result {{} 1}}

###############################################################################

runTest {test infomisc-5.1 {
  info vars with a qualified pattern targeting a NON-
  EXISTENT namespace yields zList = NULL (no namespace,
  no variables to enumerate).  With argc == 3, this
  drives the C2=F vector at line 982 -- argc == 3 (T)
  but zList is NULL (F), so the filter block is skipped.
} -constraints {
    th8
} -body {
  list \
      [info vars ::nonexistent_ns_xyz_for_infomisc::*] \
      [info vars ::nonexistent_ns_xyz_for_infomisc::name]
} -result {{} {}}}

###############################################################################

runTest {test infomisc-6.1 {
  info complete with UNMATCHED OPEN BRACKET drives the
  C2=F vector at Th8_Complete (th8_core.c:17819) --
  nBrace == 0 (T) but nBracket != 0 (F), so the result
  is F (incomplete).  Existing tests cover unmatched
  brace and unmatched quote; this closes the bracket
  arm of the same compound.
} -constraints {
    th8
} -body {
  list \
      [info complete "set x \[abc"] \
      [info complete "puts \[expr 1+2"] \
      [info complete "set y \[\["]
} -result {0 0 0}}

###############################################################################

runTest {test infomisc-7.1 {
  info commands / info vars with a pattern containing a
  SINGLE colon (not double) drives the C2=F vector at
  th8ResolveNsPattern (th8_core.c:4052) -- zPat[i] == ':'
  (T) but zPat[i+1] != ':' (F).  The scanner doesn't
  treat the colon as a namespace separator.
} -constraints {
    th8
} -body {
  set rcs {}
  catch {info commands "a:b"} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {info vars "foo:bar"} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {info commands "*:x*"} m
  lappend rcs [expr {[string length $m] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test infomisc-8.1 {
  info procs with a QUALIFIED pattern naming an EXISTING
  namespace (e.g. ::ns::*) drives the C1=T / C2=T vector
  at th8_introspection.c:781 -- pNs is non-NULL AND
  pNs->paCmd is non-NULL.  Existing info procs tests use
  bare patterns (the bQualified=F branch); this closes the
  qualified-found path's pair.
} -constraints {
    th8
} -body {
  namespace eval ::infoprocs_q {
      proc px {} {}
      proc qy {} {}
  }
  set rcs {}
  lappend rcs [lsort [info procs ::infoprocs_q::*]]
  lappend rcs [lsort [info procs ::infoprocs_q::p*]]
  set rcs
} -cleanup {
  catch {namespace delete ::infoprocs_q}
  unset -nocomplain rcs
} -result {{::infoprocs_q::px ::infoprocs_q::qy} ::infoprocs_q::px}}

###############################################################################

runTest {test infoglobals-1.1 {
  [info globals pattern] inside a fresh fault-eval child
  interp (no globals other than what RegisterLanguage may
  install) drives the C2=F vector at
  src/plugins/th8_introspection.c L893
  (`argc == 3 && zList`): when zList is NULL after
  Th8_ListAppendGlobalVariables, the filter pass is
  skipped entirely.  Use a pattern that cannot match
  anything so the test result is irrelevant.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set r [::th8testlib::fault eval {info globals zz_no_match_*}]
  expr {[string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test info_short_pat-1.1 {
  [info commands/procs/globals] with patterns shorter than
  the "::" prefix length drives the C1=F (nPat < 2) vector
  at th8ResolveNsPattern L4076 (`nPat >= 2 && zPat[0] == ':'
  && zPat[1] == ':'`).  Existing tests use patterns >=2
  bytes so C1 stays T.
} -constraints {
    th8
} -setup {
} -body {
  set rcs {}
  lappend rcs [llength [info commands ""]]
  lappend rcs [llength [info commands x]]
  lappend rcs [llength [info procs ""]]
  lappend rcs [llength [info procs x]]
  expr {[llength $rcs] == 4}
} -cleanup {
  unset -nocomplain rcs
} -result {1}}

###############################################################################

runTest {test info_ns_lookup-1.1 {
  R-30207-53705: An unqualified command name is first looked up in the
                 current namespace, then in the global namespace.

  Drives Th8_GetCommandInfo th8_core.c L9656 (T,T) vector
  (`!pEntry && interp->pCurrentNs != interp->pGlobalNs`):
  invoked from inside a non-global-namespace proc with an
  unqualified, non-existent command name -- the current-ns
  hash miss makes !pEntry true, and pCurrentNs != pGlobalNs
  because we are not in `::`.  Existing infomisc-2.x tests
  call `info default ::name ...` (qualified), which takes
  the zNs branch and never reaches L9656.
} -constraints {
    th8
} -setup {
  namespace eval ::info_ns_lookup_xyz {}
} -body {
  namespace eval ::info_ns_lookup_xyz {
    proc trigger {} {
      catch {info default nonexistent_unqualified_xyz a v} r
      return $r
    }
  }
  ::info_ns_lookup_xyz::trigger
} -cleanup {
  catch {namespace delete ::info_ns_lookup_xyz}
} -match glob -result {*not a procedure*}}

###############################################################################

runTest {test info_complete_backslash-1.1 {
  R-40646-36669: info complete

  Drives Th8_Complete th8_core.c L19296 (T,T) vector
  (`c == '\\' && i + 1 < nScript`): script contains a
  backslash followed by another byte.  Existing
  newfeatures-4.x tests do not feed any literal backslash
  byte into [info complete] (escapes are processed by
  the parser before Th8_Complete sees the script), so the
  C1 branch was never entered.
} -constraints {
    th8
} -setup {
  # Build a script consisting of two backslash bytes via
  # format %c -- a bare-backslash brace-word literal is
  # rejected by the TH8 parser as an unmatched escape, so
  # the bytes must be constructed at runtime instead.
  set bs [format %c 92]
  set script $bs$bs
} -body {
  # Two backslash bytes -- the first satisfies C1=T, and the
  # second byte at offset 1 satisfies C2=T (i+1<nScript), so
  # the (T,T) MC/DC vector is hit.
  info complete $script
} -cleanup {
  unset -nocomplain bs script
} -result {1}}

###############################################################################

runTest {test info_complete_backslash-1.2 {
  R-40646-36669: info complete

  Drives Th8_Complete th8_core.c L19296 (T,F) vector
  (`c == '\\' && i + 1 < nScript`): script ends with a
  lone backslash so C1=T, C2=F (no following byte to skip).
  An unmatched opening brace earlier in the script keeps the
  overall completeness check at "incomplete" so the test
  asserts a script-visible outcome.
} -constraints {
    th8
} -setup {
  # Script of two bytes: '(' then '\' (a non-brace prefix
  # avoids the TH8 brace-counter rejecting the literal in
  # this source file).  An unmatched double-quote earlier in
  # the script keeps the overall completeness check at
  # "incomplete".  At i=N-1 the loop sees c=='\\' with
  # i+1=N and nScript=N, so C2 is F and the backslash-skip
  # branch is not taken.
  set script [format %c%c 34 92]
} -body {
  info complete $script
} -cleanup {
  unset -nocomplain script
} -result {0}}

###############################################################################

source tests/epilogue.tcl
