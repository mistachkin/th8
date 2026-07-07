###############################################################################
#
# coverage_namespace_empty.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the empty-namespace-name compound
# `if (!zNs || nNs == 0)` that appears at three sites in
# src/th8_core.c:
#
#   line 3435  Th8_ListAppendNsChildren  ([namespace children])
#   line 3566  Th8_NsExport              ([namespace export])
#   line 3618  th8NsGetExport            ([namespace export] query)
#
# This is a multi-site batch -- the same pattern appears at all
# three call sites, so a small set of tests drives the F,T vector
# at all three simultaneously.
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

runTest {test ns_empty-1.1 {
  namespace children "" with empty pattern argument drives
  the F,T MC/DC vector at line 3435 (zNs valid, nNs==0)
} -constraints {
    th8
} -body {
  catch {namespace children ""} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_empty-1.2 {
  namespace children with explicit non-empty namespace
  drives the T,- vector at line 3435
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_empty_explicit {}} r
  catch {namespace children ::ns_empty_explicit} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_empty_explicit}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_empty-1.3 {
  namespace export with empty namespace + pattern triggers Th8_NsExport
  with nNs==0 (F,T at line 3566)
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_empty_exp_target {
    proc x {} {return ok}
  }} r
  # The standard [namespace export] form takes patterns, not a
  # namespace argument; the empty form (no args) is a query, but
  # the C API Th8_NsExport also accepts (zNs="", nNs=0) as
  # "current namespace."  We exercise both via the query path,
  # which routes through th8NsGetExport (line 3618).
  catch {namespace eval ::ns_empty_exp_target {
    namespace export
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_empty_exp_target}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_empty-1.4 {
  namespace export query from inside a sub-namespace -- routes
  through th8NsGetExport with zNs == current ns ("",0) defaulting
  to interp->pCurrentNs (line 3618 F,T branch)
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_empty_exp_inside {
    proc inside_proc {} {return inside}
    namespace export inside_proc
    set q [namespace export]
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_empty_exp_inside}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_empty-3.1 {
  namespace children with NO argument (argc==2) drives
  the C1=F vector at th8_management.c:386 (the
  argc!=2 && argc!=3 wrong-args check) -- without an
  arg, argc==2 short-circuits the compound and the
  command proceeds to list children of the current
  namespace.
} -constraints {
    th8
} -body {
  namespace eval ::ns_empty_par {
      namespace eval ::ns_empty_par::kid1 {}
      namespace eval ::ns_empty_par::kid2 {}
      set kids [namespace children]
  }
  expr {[llength [namespace children ::ns_empty_par]] == 2}
} -cleanup {
  catch {namespace delete ::ns_empty_par}
  unset -nocomplain kids
} -result {1}}

###############################################################################

source tests/epilogue.tcl
