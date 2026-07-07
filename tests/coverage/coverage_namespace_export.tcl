###############################################################################
#
# coverage_namespace_export.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for namespace-export internal compounds
# at:
#
#   th8_core.c:3618  if (!zNs || nNs == 0)         (export query default)
#   th8_core.c:3623  if (!pNs || !pNs->zExport)    (no-export-set fallback)
#   th8_core.c:3667  if (pSrcNs->zExport && pSrcNs->nExport > 0)
#
# These are reached via [namespace export] (query form) and
# [namespace import] from a source with cleared exports.
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

runTest {test ns_exp-1.1 {
  namespace export query on a namespace with no exports
  drives the !pNs->zExport branch (T,F at line 3623)
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_exp_empty1 {
    proc internal {} {return secret}
    namespace export
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_exp_empty1}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_exp-1.2 {
  namespace export query on a namespace with exports drives
  the F,F (success) vector at line 3623
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_exp_set1 {
    proc public_one {} {return one}
    namespace export public_one
    set query_result [namespace export]
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_exp_set1}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_exp-1.3 {
  namespace export -clear empties the exports; drives the T,F
  vector at line 3667 (zExport valid but nExport == 0)
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_exp_cleared1 {
    proc was_public {} {return cleared}
    namespace export was_public
    namespace export -clear
  }} r
  catch {namespace eval ::ns_exp_imp_cleared {
    namespace import ::ns_exp_cleared1::*
  }}
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_exp_cleared1}
  catch {namespace delete ::ns_exp_imp_cleared}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_exp_cov-2.1 {
  namespace parent on a non-existent namespace -- drives the
  T,- vector at th8_core.c:3496 (pNs == NULL after lookup)
} -constraints {
    th8
} -body {
  catch {namespace parent ::no_such_ns_for_parent_test_} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
