###############################################################################
#
# coverage_expr_strict.tcl --
#
# Strict-mode regression tests for the opt-in expression-grammar
# features (TH8_EXPR_TOP_COMMA, TH8_EXPR_VAR_ASSIGN).  These tests
# pin the contract that, with TH8_EXPR_NONE set (the default),
# inputs that would otherwise be enabled by the new features
# produce IDENTICAL behavior to today: the same TH8_ERROR return
# AND the same error-message text.  Anything less is a silent
# regression of strict expr(n) compliance.
#
# The expected error messages were captured from the codebase
# BEFORE any of the feature work landed; they are intentionally
# encoded as exact-match strings so any drift in tokenisation
# or parser routing surfaces immediately.
#
# Two error categories appear:
#
#   "syntax error in expression: \"<input>\""
#     -- raised when the parser falls through every operand-
#        recogniser and every operator-table row.  The captured
#        bytes after the colon are the original expression text.
#
#   "invalid bareword \"<id>\""
#     -- raised when the parser reaches the identifier-fallback
#        path with an identifier that does not name a registered
#        function and is not a recognised boolean / Inf / NaN.
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
# Section 1 -- TH8_EXPR_TOP_COMMA inputs must error in strict mode
#
# Default flags = TH8_EXPR_NONE.  Each input below would be
# ACCEPTED if TH8_EXPR_TOP_COMMA were set, but in strict mode
# falls through to a captured baseline error.  These tests must
# pass identically to how an UNMODIFIED pre-feature codebase
# would handle them.
#
###############################################################################

runTest {test expr-strict-1.1 {
  strict mode: top-level comma errors with baseline message
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {1, 2}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {syntax error in expression: "1, 2"}}}

###############################################################################

runTest {test expr-strict-1.2 {
  strict mode: comma between identifier-shaped tokens errors at the bareword
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {a, b}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {invalid bareword "a"}}}

###############################################################################

runTest {test expr-strict-1.3 {
  strict mode: multi-comma input errors with full input echoed
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {1, 2, 3}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {syntax error in expression: "1, 2, 3"}}}

###############################################################################

runTest {test expr-strict-1.4 {
  strict mode: comma inside parens errors with the parenthesised input
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {(1, 2)}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {syntax error in expression: "(1, 2)"}}}

###############################################################################

runTest {test expr-strict-1.5 {
  strict mode: command-sub LHS does NOT execute when parse fails
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {[set ::__must_not_run 1], 2}} msg]
  set ran [info exists ::__must_not_run]
  list $rc $ran [string match {*syntax error*} $msg]
} -cleanup {
  unset -nocomplain rc msg ran ::__must_not_run
} -result {1 0 1}}

###############################################################################

runTest {test expr-strict-1.6 {
  strict mode: comma inside ternary errors at the bareword
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {a ? b, c : d}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {invalid bareword "a"}}}

###############################################################################
#
# Section 2 -- TH8_EXPR_VAR_ASSIGN inputs must error in strict mode
#
# Same contract: the := operator must NOT be reachable when the
# flag is clear.  Each input falls through to one of the two
# captured baseline error categories.
#
###############################################################################

runTest {test expr-strict-2.1 {
  strict mode: := with quoted LHS errors with baseline message
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {"x" := 5}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {syntax error in expression: ""x" := 5"}}}

###############################################################################

runTest {test expr-strict-2.2 {
  strict mode: := with brace-quoted LHS errors with baseline message
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {{x} := 5}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {syntax error in expression: "{x} := 5"}}}

###############################################################################

runTest {test expr-strict-2.3 {
  strict mode: := with bareword LHS errors at the bareword
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {x := 5}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {invalid bareword "x"}}}

###############################################################################

runTest {test expr-strict-2.4 {
  strict mode: := with substituted LHS errors with baseline message
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {[set name x] := 5}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg name
} -result {1 {syntax error in expression: "[set name x] := 5"}}}

###############################################################################

runTest {test expr-strict-2.5 {
  strict mode: chained := errors with baseline message
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {"a" := "b" := 1}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -result {1 {syntax error in expression: ""a" := "b" := 1"}}}

###############################################################################

runTest {test expr-strict-2.6 {
  strict mode: combined := and , errors and sets neither variable
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {"x" := 0, "y" := 10, $x + $y}} msg]
  set xExists [info exists x]
  set yExists [info exists y]
  list $rc $xExists $yExists [string match {*syntax error*} $msg]
} -cleanup {
  unset -nocomplain rc msg xExists yExists x y
} -result {1 0 0 1}}

###############################################################################
#
# Section 3 -- function-call commas must NOT be feature-gated
#
# Function-call argument commas (`pow(x, y)`) are a separate
# parser path that has nothing to do with TH8_EXPR_TOP_COMMA.
# They worked before the feature was added, must work in strict
# mode now, and must continue working when the feature is off.
#
###############################################################################

runTest {test expr-strict-3.1 {
  strict mode: pow(2,3) still works (function-call comma not gated)
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  expr {pow(2, 3)}
} -result {8.0}}

###############################################################################

runTest {test expr-strict-3.2 {
  strict mode: nested function calls with commas still work
} -constraints {
    th8
} -setup {
  th8testlib::expr_features set none
} -body {
  expr {pow(2, max(1, 3))}
} -result {8.0}}

###############################################################################
#
# Section 4 -- disable-path round-trip integrity
#
# After enabling features and then disabling them again, the
# strict-mode error messages must match the baseline EXACTLY.
# Failure here would mean the flag-write path corrupted some
# parser state that the strict path depends on.
#
###############################################################################

runTest {test expr-strict-4.1 {
  disable path: enable TOP_COMMA, then restore -- comma errors again
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set none]
  th8testlib::expr_features set top-comma
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {1, 2}} msg]
  list $rc $msg
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain rc msg saved
} -result {1 {syntax error in expression: "1, 2"}}}

###############################################################################

runTest {test expr-strict-4.2 {
  disable path: enable VAR_ASSIGN, then restore -- := errors again
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set none]
  th8testlib::expr_features set var-assign
  th8testlib::expr_features set none
} -body {
  set rc [catch {expr {"x" := 5}} msg]
  list $rc $msg
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain rc msg saved
} -result {1 {syntax error in expression: ""x" := 5"}}}

###############################################################################

runTest {test expr-strict-4.3 {
  disable path: enable both, then restore -- both error again
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set none]
  th8testlib::expr_features set top-comma,var-assign
  th8testlib::expr_features set none
} -body {
  set rc1 [catch {expr {1, 2}} m1]
  set rc2 [catch {expr {"x" := 5}} m2]
  list $rc1 $m1 $rc2 $m2
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain rc1 rc2 m1 m2 saved
} -result {1 {syntax error in expression: "1, 2"} 1 {syntax error in expression: ""x" := 5"}}}

###############################################################################
#
# Section 5 -- Get/Set API round-trip integrity
#
# Th8_GetExprFeatures must report exactly what the prior
# Th8_SetExprFeatures set, and Th8_SetExprFeatures must return
# the previous flag set so callers can save-and-restore.
#
###############################################################################

runTest {test expr-strict-5.1 {
  API: Get reflects the most recent Set
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set none]
} -body {
  th8testlib::expr_features set top-comma
  set after1 [th8testlib::expr_features get]
  th8testlib::expr_features set var-assign
  set after2 [th8testlib::expr_features get]
  th8testlib::expr_features set top-comma,var-assign
  set after3 [th8testlib::expr_features get]
  th8testlib::expr_features set none
  set after4 [th8testlib::expr_features get]
  list $after1 $after2 $after3 $after4
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved after1 after2 after3 after4
} -result {1 2 3 0}}

###############################################################################

runTest {test expr-strict-5.2 {
  API: Set returns the previous flag set
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set none]
} -body {
  set prev1 [th8testlib::expr_features set top-comma]
  set prev2 [th8testlib::expr_features set var-assign]
  set prev3 [th8testlib::expr_features set none]
  list $prev1 $prev2 $prev3
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved prev1 prev2 prev3
} -result {0 1 2}}

###############################################################################

source tests/epilogue.tcl
