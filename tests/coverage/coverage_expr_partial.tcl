###############################################################################
#
# coverage_expr_partial.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC partial closures in th8_expr.c.  Two decisions are
# targeted:
#
#   (a) th8ExprBuild ternary post-colon search (L2755-2757):
#	`iColon >= nToken || !apToken[iColon]->pOp ||
#	 apToken[iColon]->pOp->eOp != TH8_OP_TERNARY_C`.
#	The C3-Pair (F,F,T) is hit when a ternary expression is
#	syntactically followed by a non-':' operator instead of
#	the expected colon.  `expr {1?2+3}` parses as
#	[1, ?, 2, +, 3]; iColon lands on '+', which has a pOp
#	whose eOp is TH8_OP_ADD (not TERNARY_C).
#
#   (b) th8ExprFindTopComma backslash-escape (L3061):
#	`c == '\\' && i + 1 < nExpr`.  The C2-Pair (T,F) is hit
#	when a backslash is the LAST byte of the expression
#	text.  Existing tests use brace quoting which adds a
#	closing brace after the `\\`; double-quote substitution
#	produces a string ending exactly at the backslash, with
#	no successor byte.
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
# Section 1 -- ternary without ':' drives C3-Pair at L2755
#
###############################################################################

runTest {test expr-partial-ternary-no-colon-1.1 {
  ternary `1?2+3` (missing colon) errors with
  "syntax error in expression" -- exercises the C3 branch
  of the post-true-branch search where the next token has a
  pOp but eOp != TH8_OP_TERNARY_C.
} -constraints {
    th8
} -body {
  catch {expr {1?2+3}} msg
  string match "syntax error*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test expr-partial-ternary-no-colon-1.2 {
  ternary `1?5-2` (different non-colon op) also errors --
  confirms the C3 path is operator-agnostic.  Note: this
  case actually combines as `1?(5-2)` because subtraction
  has higher precedence (4) than ternary (14), then the
  ternary post-true-branch search finds end-of-tokens, so
  it drives C1=T not C3=T.  Retained for path-diversity.
} -constraints {
    th8
} -body {
  catch {expr {1?5-2}} msg
  string match "syntax error*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test expr-partial-ternary-no-colon-1.3 {
  ternary `1?2:=3` -- the `:=` (VAR_ASSIGN, precedence 15)
  has LOWER precedence than `?:` (14), so at Phase 4
  (ternary) `:=` has NOT yet been processed and apToken[3]
  is still a standalone operator token.  iColon lands on
  `:=`, apToken[iColon]->pOp exists, pOp->eOp ==
  TH8_OP_VAR_ASSIGN != TH8_OP_TERNARY_C.  Drives C3=T
  (the (F,F,T) vector) which closes the C3-Pair.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  catch {expr {1?2:=3}} msg
  string match "syntax error*" $msg
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved msg
} -result {1}}

###############################################################################
#
# Section 2 -- trailing backslash at end-of-expression drives C2-Pair at L3061
#
# The top-comma scanner's escape-skip branch (`c == '\\' && i + 1 < nExpr`)
# previously had only (T,T) and (F,-) vectors; the (T,F) vector requires the
# backslash to be the LAST byte.  Brace quoting cannot produce this because
# the closing '}' becomes the successor byte.  We build the input via
# double-quote substitution so the post-substitution string ends exactly at
# the backslash.
#
###############################################################################

runTest {test expr-partial-bslash-eof-2.1 {
  expression text "1,2\\" (4 bytes, last byte = '\\') under
  TH8_EXPR_TOP_COMMA -- scanner reaches the trailing
  backslash with no successor; expr errors out because the
  trailing sub-expression "2\\" is malformed.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set s "1,2\\"
  set rc [catch {expr $s} msg]
  list [string length $s] $rc
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved s rc msg
} -result {4 1}}

###############################################################################

runTest {test expr-partial-bslash-eof-2.2 {
  single-byte expression "\\" (just a backslash) under
  TH8_EXPR_TOP_COMMA -- scanner sees only the trailing
  backslash; (T,F) vector recorded on the very first
  iteration.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set s "\\"
  set rc [catch {expr $s} msg]
  list [string length $s] $rc
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved s rc msg
} -result {1 1}}

###############################################################################
#
# th8ExprEvalOne non-decimal-literal normalisation:
# 0-prefixed expression results that DO NOT match the radix-
# letter set still flow through the four-way else-if chain
# at L2981-2984.  These tests drive specific C-pair vectors.
#
###############################################################################

runTest {test expr_partial-3.1 {
  expr {08} returns the string "08" (8 is not a valid octal
  digit so no conversion).  Drives the L2984 (T, F) C2
  vector: zRes[1]='8' satisfies c>='0' but c>'7' so the
  conjunction is false.
} -constraints {
    th8
} -body {
  set r [expr {08}]
  set r
} -cleanup {
  unset -nocomplain r
} -result {08}}

###############################################################################

runTest {test expr_partial-3.2 {
  expr {09} same shape -- drives the L2984 (T, F) vector
  with a different second digit.
} -constraints {
    th8
} -body {
  expr {09}
} -result {09}}

###############################################################################

runTest {test expr_partial-3.3 {
  Two-byte "0X" through expr: 0 followed by 'X' is left as
  string when there are no hex digits.  Th8 returns "0X"
  verbatim, exercising the nRes == 2 inner block (L2991)
  with c='X' which is > '7' so the L2992 compound is
  (F, T) -- the C2-T vector.
} -constraints {
    th8
} -body {
  expr {0X}
} -result {0X}}

###############################################################################

source tests/epilogue.tcl
