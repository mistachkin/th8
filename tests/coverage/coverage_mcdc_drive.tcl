###############################################################################
#
# coverage_mcdc_drive.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives an input-reachable MC/DC decision that the reachable-denominator
# gate (docs/internal/mcdc_reachable_criterion.md) leaves as genuine debt
# rather than an intrinsic waiver:
#
#   mcdcdrive-1.1  src/th8_expr.c  th8ExprEvalOne radix-prefix parse --
#                  closes the c=='o'||'O' (octal) and c=='b'||'B' (binary)
#                  prefix decisions with both letter cases.  (Verified: these
#                  two decisions move to 100% MC/DC; this closure is
#                  load-bearing for the >=95% reachable gate.)
#
# The remaining tests are REGRESSION coverage for behavior adjacent to still-
# open debt decisions (coroutine yield/return/done, command-parse separators,
# a brace-first command word, "::" namespace children, array-element var
# names).  They exercise those paths but do NOT by themselves close the exact
# MC/DC independence pairs for coro_resume_command / th8NextCommand /
# th8ResolveNsPattern / th8AnalyzeVarName -- those need more specific vectors
# and remain tracked debt in the gate (they are honestly NOT claimed as
# covered here).
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

runTest {test mcdcdrive-1.1 {
  expr radix-prefix parsing drives th8_expr.c th8ExprEvalOne octal
  (0o/0O), binary (0b/0B), hex (0x/0X) and bare-octal (0NN) prefix
  decisions with both letter cases, closing the octal- and binary-
  prefix MC/DC decisions.
} -constraints {
    th8
} -body {
  list [expr {0o17}] [expr {0O17}] [expr {0b101}] [expr {0B101}] \
      [expr {0x1f}] [expr {0X1F}] [expr {017}]
} -result {15 15 5 5 31 31 15}}

###############################################################################

runTest {test mcdcdrive-2.1 {
  Regression: coroutine yield / return / done states -- create,
  resume twice, then resume the completed coroutine.
} -constraints {
    th8
} -body {
  coroutine mcdccoro apply {{} {yield A; yield B; return C}}
  set r1 [mcdccoro]
  set r2 [mcdccoro]
  set done [catch {mcdccoro} m]
  list $r1 $r2 $done
} -cleanup {
  catch {rename mcdccoro {}}
  unset -nocomplain r1 r2 done m
} -result {B C 1}}

###############################################################################

runTest {test mcdcdrive-3.1 {
  Regression: command-parse handling of leading separators (space,
  semicolon, newline) and a brace-first command word.
} -constraints {
    th8
} -body {
  eval "  ;\n set ::mcdcv1 1"
  eval "\{set\} ::mcdcv2 2"
  eval "set ::mcdcv3 3; set ::mcdcv4 4\nset ::mcdcv5 5"
  list $::mcdcv1 $::mcdcv2 $::mcdcv3 $::mcdcv4 $::mcdcv5
} -cleanup {
  unset -nocomplain ::mcdcv1 ::mcdcv2 ::mcdcv3 ::mcdcv4 ::mcdcv5
} -result {1 2 3 4 5}}

###############################################################################

runTest {test mcdcdrive-4.1 {
  Regression: a "::"-anchored namespace-children query and array-
  element variable names.
} -constraints {
    th8
} -setup {
  namespace eval ::mcdcns::sub {}
  set ::mcdcarr(alpha) 1
  set ::mcdcarr(beta) 2
} -body {
  list [namespace children ::mcdcns] \
      [lsort [array names ::mcdcarr]] \
      [set ::mcdcarr(alpha)]
} -cleanup {
  namespace delete ::mcdcns
  array unset ::mcdcarr
} -result {::mcdcns::sub {alpha beta} 1}}

###############################################################################

source tests/epilogue.tcl
