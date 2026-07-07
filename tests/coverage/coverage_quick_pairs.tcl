###############################################################################
#
# coverage_quick_pairs.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for one-pair-missing decisions across
# several src/*.c files that are reachable from ordinary script
# tests but were never driven:
#
#   - src/plugins/th8_formatting.c scan_command L943-945: %x
#     prefix-detection compound `iStr+1 < nStr && zStr[iStr] == '0'
#     && (zStr[iStr+1] == 'x' || zStr[iStr+1] == 'X')`.  Existing
#     tests cover the full T,T,*,* vectors.  Missing: C1=F
#     (single-char input "0") and C3/C4=F (leading "0" not
#     followed by x/X).
#
#   - src/plugins/th8_procedures.c apply_command L831: lambda's
#     optional namespace argument `azLambda[2][0] != ':' ||
#     azLambda[2][1] != ':'`.  Existing tests cover (F,F) ::-
#     prefixed and (T,-) no-prefix.  Missing: (F,T) -- starts
#     with single ':' but second char is NOT ':' (e.g. ":foo").
#
#   - src/th8_expr.c th8FindMathFunc-callsite at L700 in
#     th8ExprFunctionCall: an unknown math function name forces
#     Th8_HashFind to return NULL, driving the L297 C1=T vector
#     `if (!pHash || NEVER(!pHash->pData))`.  Existing tests
#     only call defined math functions.
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

runTest {test qpairs-scan-1.1 {
  scan with %x on a single-character "0" input drives the
  C1=F vector at th8_formatting.c L943 -- iStr+1 < nStr is
  FALSE because nStr==1 and iStr==0.  The 0x-prefix
  compound short-circuits and the single 0 is parsed as
  the hex value 0.
} -constraints {
    th8
} -setup {
} -body {
  set r [scan "0" %x]
  set r
} -cleanup {
  unset -nocomplain r
} -result {0}}

###############################################################################

runTest {test qpairs-scan-1.2 {
  scan with %x on "0123" drives C3=F and C4=F at
  th8_formatting.c L944-945 -- leading '0' but the next char
  is '1', not 'x' or 'X'.  The prefix skip is not taken;
  the digits parse as hex 0x123 = 291.
} -constraints {
    th8
} -setup {
} -body {
  set r [scan "0123" %x]
  set r
} -cleanup {
  unset -nocomplain r
} -result {291}}

###############################################################################

### Note: deliberately omitting an apply :foo test for
### th8_procedures.c L831 C2-pair: any single-colon-prefixed
### namespace name triggers Bug 15 (doc/internal/incomplete.md)
### -- :::foo cannot be cleanly deleted and may hang on interp
### teardown.  Test was prototyped, observed to PASS but leave
### a permanent :::foo namespace, then removed.

###############################################################################

runTest {test qpairs-mathfn-1.1 {
  expr with an unknown math function name forces
  Th8_FindMathFunc to fail at L297 (pHash is NULL from
  HashFind), driving the C1=T vector.  Existing tests
  only call defined functions, so this is the only
  trigger.
} -constraints {
    th8
} -setup {
} -body {
  catch {expr {nofuncqp(1)}} r
  expr {[string match {*unknown math function*} $r]}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
