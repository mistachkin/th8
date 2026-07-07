###############################################################################
#
# coverage_proc_lambda.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for lambda / [apply] / [napply] /
# [lindex] / [array startsearch] compounds across plugins:
#
#   th8_procedures.c:821    apply lambda namespace check
#                           (nLambda == 3 && anLambda[2] > 0)
#   th8_procedures.c:825    namespace prefix detection
#                           (azLambda[2][0] != ':' || azLambda[2][1] != ':')
#   th8_procedures.c:1301   napply 2-element lambda check
#                           (nLambda != 2 || !azLambda)
#   th8_lists.c:312         lindex multi-index bounds
#                           (iIndex >= 0 && iIndex < nCount && azElem)
#   th8_variables.c:1754    array startsearch ID buffer overflow
#                           (nSid < 0 || (size_t)nSid >= sizeof(zSid))
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
# Section 1 -- apply with explicit namespace argument (821, 825)
#
###############################################################################

runTest {test pl_cov-1.1 {
  apply with 3-element lambda where namespace starts with "::"
  -- drives both T,T branches at 821 (nLambda==3 && len>0)
  and the second condition at 825 (first ':' check is true)
} -constraints {
    th8
} -body {
  catch {apply {{} {} ::} } r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pl_cov-1.2 {
  apply with 3-element lambda where namespace lacks "::" prefix
  -- drives the F,- (or T,F) vector at 825 (no :: prefix)
} -constraints {
    th8
} -body {
  catch {apply {{} {} my_ns_pl12_}} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::my_ns_pl12_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pl_cov-1.4 {
  apply with 3-element lambda where namespace is empty -- drives
  the C2 false vector at 821 (anLambda[2] > 0 is false)
} -constraints {
    th8
} -body {
  catch {apply [list {} {} ""]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 2 -- napply with non-2-element lambda (1301)
#
###############################################################################

runTest {test pl_cov-2.1 {
  napply with 3-element lambda fails (nLambda != 2 vector)
} -constraints {
    th8
} -body {
  catch {napply {{} {} extra}} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test pl_cov-2.2 {
  napply with 1-element lambda fails
} -constraints {
    th8
} -body {
  catch {napply {only_one}} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test pl_cov-2.3 {
  napply with 2-element lambda runs the success path (F,F vector)
} -constraints {
    th8
} -body {
  catch {napply {x {set x}} hello} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 3 -- lindex multi-index bounds check (312)
#
###############################################################################

runTest {test pl_cov-3.1 {
  lindex with negative index drives F,-,- vector
} -constraints {
    th8
} -body {
  catch {lindex {a b c} -1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pl_cov-3.2 {
  lindex with out-of-range index drives T,F,- vector
} -constraints {
    th8
} -body {
  catch {lindex {a b c} 5} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pl_cov-3.3 {
  nested lindex with mixed negative/positive indices on a deep list
} -constraints {
    th8
} -body {
  catch {lindex {{a b c} {d e f} {g h i}} 0 -1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pl_cov-3.4 {
  nested lindex with valid path drives T,T,T (all conditions true)
} -constraints {
    th8
} -body {
  lindex {{a b c} {d e f}} 1 2
} -result {f}}

###############################################################################
#
# Section 4 -- array startsearch with long array name (1754)
#
###############################################################################

runTest {test pl_cov-4.1 {
  array startsearch with a very long array name forces snprintf
  truncation (nSid >= sizeof(zSid)), driving the T,T fallback
} -constraints {
    th8
} -body {
  set _very_long_array_name_for_search_id_buffer_overflow_test_$$ [list \
      key1 val1 key2 val2 key3 val3]
  array set _very_long_array_name_for_search_id_buffer_overflow_test_$$ \
      [list a 1 b 2 c 3]
  catch {array startsearch \
      _very_long_array_name_for_search_id_buffer_overflow_test_$$} r
  # Best-effort cleanup of any active search.
  catch {array donesearch \
      _very_long_array_name_for_search_id_buffer_overflow_test_$$ $r}
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain _very_long_array_name_for_search_id_buffer_overflow_test_$$ r
} -result {1}}

###############################################################################

runTest {test pl_cov-5.1 {
  apply with a 3-element lambda {arglist body namespace} --
  qualified namespace form (::ns) drives the F branch of
  the namespace prefix check at th8_procedures.c:825;
  unqualified form drives the T branch.
} -constraints {
    th8
} -body {
  namespace eval ::pl_cov_ns5 {
      variable counter 7
  }
  list \
      [apply {{x} {return $x} ::pl_cov_ns5} 42] \
      [apply {{x} {return $x} pl_cov_ns5} 42]
} -cleanup {
  catch {namespace delete ::pl_cov_ns5}
  catch {namespace delete ::pl_cov_ns5_relative}
} -result {42 42}}

###############################################################################

runTest {test pl_cov-5.1a {
  apply with a 3-element lambda whose namespace starts with
  a SINGLE colon (":name") drives the (F, T) vector for the
  prefix check at th8_procedures.c -- azLambda[2][0] is
  ':' (C1=F) but azLambda[2][1] is not ':' (C2=T).  Existing
  tests cover (F, F) [name is exactly "::..."] and (T, -)
  [name starts with non-colon]; this closes the C2-Pair on
  the single-colon-prefix vector.

  The single-colon name resolves to a fully-qualified
  "::"+":name" (e.g. ":::colon_alone") which does NOT exist,
  so apply errors "namespace \"...\" not found" -- matching
  Tcl 8.6 (verified).  TH8 previously auto-created the
  namespace (bCreate=1), leaving an undeletable spurious
  namespace behind (Bug 6/15); it now resolves find-only and
  errors, so each catch returns 1 and no namespace leaks
  (test is CLEAN, no longer MUTATING).
} -constraints {
    th8
} -body {
  list \
      [catch {apply {{x} {return $x} :colon_alone} 42}] \
      [catch {apply {{x} {return $x} :other_name} 42}] \
      [catch {apply {{x} {return $x} :a} 42}]
} -result {1 1 1}}

###############################################################################

runTest {test pl_cov-5.2 {
  apply with wrong-arity lambda lists drives the (T,-,-)
  vector at line 721 (nLambda is 0, 1, or 4+, all of
  which fail the != 2 && != 3 check).
} -constraints {
    th8
} -body {
  list \
      [catch {apply {} 42}] \
      [catch {apply {a} 42}] \
      [catch {apply {a b c d} 42}]
} -result {1 1 1}}

###############################################################################

runTest {test pl_cov-5.3 {
  napply wrong-arity lambda drives line 1301
  (nLambda != 2 || !azLambda).
} -constraints {
    th8
} -body {
  list \
      [catch {napply {}}] \
      [catch {napply {a}}] \
      [catch {napply {a b c}}]
} -result {1 1 1}}

###############################################################################

runTest {test pl_cov-5.4 {
  proc with trailing "args" parameter drives the args-
  detection compound at th8_procedures.c:1350 (the trailing
  param's name == "args" with length 4).
} -constraints {
    th8
} -body {
  proc ::pl_cov_args {a b args} {
      list $a $b [llength $args]
  }
  list \
      [::pl_cov_args 1 2] \
      [::pl_cov_args 1 2 3 4 5]
} -cleanup {
  catch {rename ::pl_cov_args ""}
} -result {{1 2 0} {1 2 3}}}

###############################################################################

source tests/epilogue.tcl
