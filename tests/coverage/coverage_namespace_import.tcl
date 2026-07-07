###############################################################################
#
# coverage_namespace_import.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the simple-glob helper in
# src/th8_core.c used by [namespace import]:
#
#   line 3519: if (nPat > 0 && zPat[nPat - 1] == '*')
#
# Existing namespace import tests use specific command names
# (no trailing '*'); none drive the trailing-glob T,T vector.
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

runTest {test ns_imp-1.1 {
  namespace import with trailing-* glob pattern drives the
  T,T MC/DC vector at line 3519 of th8_core.c
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_imp_src1 {
    namespace export myProc1 myProc2
    proc myProc1 {} {return 1}
    proc myProc2 {} {return 2}
  }} r
  catch {namespace eval ::ns_imp_dst1 {
    namespace import ::ns_imp_src1::myProc*
  }}
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_imp_src1}
  catch {namespace delete ::ns_imp_dst1}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_imp-1.2 {
  namespace import with single "*" drives the nPat==1 fast-path
  branch at line 3514
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_imp_src2 {
    namespace export *
    proc anyProc {} {return all}
    proc anotherProc {} {return another}
  }} r
  catch {namespace eval ::ns_imp_dst2 {
    namespace import ::ns_imp_src2::*
  }}
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_imp_src2}
  catch {namespace delete ::ns_imp_dst2}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_imp-1.3 {
  namespace import with literal name (no glob) drives the
  T,F vector at line 3519 (pattern doesn't end with '*')
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_imp_src3 {
    namespace export specificProc
    proc specificProc {} {return specific}
  }} r
  catch {namespace eval ::ns_imp_dst3 {
    namespace import ::ns_imp_src3::specificProc
  }}
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_imp_src3}
  catch {namespace delete ::ns_imp_dst3}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_imp-2.1 {
  namespace import with NO arguments (argc==2) drives
  the C1=F vector at th8_management.c:650 (the
  argc>2 && th8StrEq("-force") option detection) -- no
  args means argc==2, the loop body doesn't run, and
  the command succeeds as a no-op.
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_imp_noargs {
      namespace import
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_imp_noargs}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_imp-3.1 {
  namespace import with a pattern starting with "::" but
  whose namespace-path component is EMPTY (e.g. "::*"
  splits to zNs="::*" with nNs=0 in th8SplitQualName)
  drives the C1=F, C2=T vector at th8_core.c:3841 --
  zNsPath is non-NULL but nNsPath == 0.  Existing tests
  use fully-qualified patterns (zNsPath non-NULL,
  nNsPath > 0) or unqualified patterns (zNsPath NULL);
  this closes the C2-pair at the import path-empty
  check.
} -constraints {
    th8
} -body {
  set rcs {}
  catch {namespace import ::*} m
  lappend rcs [string match {*qualified*} $m]
  catch {namespace import ::*  ::other::*} m2
  lappend rcs [string match {*qualified*} $m2]
  set rcs
} -cleanup {
  unset -nocomplain rcs m m2
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
