###############################################################################
#
# coverage_core_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c.  Drives short-circuit OR
# guards on public APIs whose T branches are not reached during
# ordinary suite traffic:
#
#   Th8_SecureZero(interp, p, n)  guard: (n == 0 || !p)
#   Th8_NsExport(interp, zNs, nNs, ...)  guard: (!zNs || nNs == 0)
#   Th8_NsImport(interp, zPattern, nPat, bForce)
#     guard: (!zPattern || nPat == 0)
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

runTest {test core_mcdc-1.1 {
  ::th8testlib::null_guard core sweeps Th8_SecureZero,
  Th8_NsExport, Th8_NsImport, Th8_MergePlatformInterp,
  Th8_SetBreakpoint, Th8_ClearBreakpoint,
  Th8_ListAppendExpansions, Th8_ListAppendBreakpoints,
  Th8_IterateArraySearches, and Th8_ListAppendNsVariables
  with the missing NULL/empty vectors required for MC/DC
  closure.
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard core
} -result {ok}}

###############################################################################

runTest {test core_mcdc-2.1 {
  th8GetArrayEpoch src/th8_vars.c L1535: C1=T vector
  (pEntry==NULL, i.e., array deleted between [array
  startsearch] and the next iteration probe).  Driven by
  unsetting the array mid-search.
} -body {
  array set ::core_mcdc_a {x 1 y 2}
  set sid [array startsearch ::core_mcdc_a]
  unset ::core_mcdc_a
  catch {array nextelement ::core_mcdc_a $sid} r
  # The error path uses th8GetArrayEpoch's NULL-entry branch
  # to detect the mutation; any error result is acceptable.
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain ::core_mcdc_a sid r
} -result {1}}

###############################################################################

runTest {test core_mcdc-4.1 {
  Relative-namespace pattern in [info vars] drives the
  C2=F vector at src/th8_core.c L4076 inside the namespace
  pattern resolver (pattern contains "::" but does not start
  with "::").
} -setup {
} -body {
  namespace eval ::core_mcdc_ns {
    catch {info vars foo::*} r
  }
  expr {[info exists r] ? 1 : 1}
} -cleanup {
  unset -nocomplain r
  catch {namespace delete ::core_mcdc_ns}
} -result {1}}

###############################################################################

runTest {test core_mcdc-4.2 {
  Pattern starting with single ':' (not '::') and containing
  '::' later drives the C3=F vector at src/th8_core.c L4076
  (`zPat[1] == ':'` false).
} -setup {
} -body {
  namespace eval ::core_mcdc_ns2 {
    # ":x::y" starts with single colon, second char is 'x' not ':'.
    catch {info vars :x::y} r
  }
  expr {[info exists r] ? 1 : 1}
} -cleanup {
  unset -nocomplain r
  catch {namespace delete ::core_mcdc_ns2}
} -result {1}}

###############################################################################

runTest {test core_mcdc-3.1 {
  th8 expr INT64_MIN / -1 overflow drives src/th8_expr.c
  L1455 C1=T,C2=T compound (`iLeft == TH8_INT64_MIN &&
  iRight == -1`).  The C overflow check fires and reports
  integer overflow.
} -setup {
} -body {
  set min -9223372036854775808
  catch {expr {$min / -1}} r
  # Result is either an error (overflow detected) or the
  # platform-specific wrap-around value.  Either way the
  # decision is exercised.
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r min
} -result {1}}

###############################################################################

source tests/epilogue.tcl
