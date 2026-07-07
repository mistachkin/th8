###############################################################################
#
# coverage_expr_var_assign.tcl --
#
# Tests for the TH8_EXPR_VAR_ASSIGN opt-in expression-grammar
# feature.  When the flag is set, `:=` is a binary operator at
# the lowest precedence; its left operand is an ordinary expr(n)
# operand whose substituted-string value names a variable, and
# its right operand is the value to assign.  The result of the
# expression is the assigned value (C-style), which makes
# chained assignments like `"a" := "b" := 1` set both `a` and
# `b` to 1.
#
# Bare-word LHS (e.g. `x := 5`) is NOT supported and remains
# rejected by the existing bareword-rejection security envelope.
# Only operand forms 4 (quoted), 5 (brace-quoted), 6 (command
# substitution), and 7 (any sub-expression yielding a string)
# are valid LHS forms.
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
# Section 1 -- basic assignment, each LHS operand form
#
###############################################################################

runTest {test expr-var-assign-1.1 {
  enabled: quoted LHS sets variable, returns assigned value
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {"x" := 5}]
  list $r $x
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r x
} -result {5 5}}

###############################################################################

runTest {test expr-var-assign-1.2 {
  enabled: brace-quoted LHS sets variable, returns assigned value
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {{x} := 7}]
  list $r $x
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r x
} -result {7 7}}

###############################################################################

runTest {test expr-var-assign-1.3 {
  enabled: command-sub LHS provides the variable name
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {[set name target] := 99}]
  list $r $target
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r name target
} -result {99 99}}

###############################################################################

runTest {test expr-var-assign-1.4 {
  enabled: $-substituted LHS provides the variable name
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
  set namevar dst
} -body {
  set r [expr {$namevar := 42}]
  list $r $dst
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r namevar dst
} -result {42 42}}

###############################################################################

runTest {test expr-var-assign-1.5 {
  enabled: bareword LHS still errors (security envelope intact)
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set rc [catch {expr {x := 5}} msg]
  list $rc $msg
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 {invalid bareword "x"}}}

###############################################################################
#
# Section 2 -- right-associative chaining
#
###############################################################################

runTest {test expr-var-assign-2.1 {
  enabled: a := b := 1 sets both, right-associative
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {"a" := "b" := 1}]
  list $r $a $b
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r a b
} -result {1 1 1}}

###############################################################################

runTest {test expr-var-assign-2.2 {
  enabled: triple chain a := b := c := 7 sets all three to 7
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {"a" := "b" := "c" := 7}]
  list $r $a $b $c
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r a b c
} -result {7 7 7 7}}

###############################################################################

runTest {test expr-var-assign-2.3 {
  enabled: chained assignment with arithmetic RHS
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {"a" := "b" := 3 * 4 + 1}]
  list $r $a $b
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r a b
} -result {13 13 13}}

###############################################################################
#
# Section 3 -- assignment as part of a larger expression
#
###############################################################################

runTest {test expr-var-assign-3.1 {
  enabled: assignment value can feed a parent operator (parens required)
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {("x" := 5) + 1}]
  list $r $x
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r x
} -result {6 5}}

###############################################################################

runTest {test expr-var-assign-3.2 {
  enabled: ternary with parenthesised := per branch
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {1 ? ("x" := 100) : ("x" := 200)}]
  list $r $x
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r x
} -result {100 100}}

###############################################################################

runTest {test expr-var-assign-3.3 {
  enabled: ternary false-branch chosen, only false-branch assignment runs
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set r [expr {0 ? ("x" := 1) : ("y" := 2)}]
  list $r [info exists x] [info exists y] $y
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r x y
} -result {2 0 1 2}}

###############################################################################
#
# Section 4 -- combined with TH8_EXPR_TOP_COMMA
#
# The user's motivating use case: C-style multi-statement
# expressions of the form `expr {x := 0, y := 10, x + y}`.
#
###############################################################################

runTest {test expr-var-assign-4.1 {
  enabled both: the motivating C-style sequence works
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma,var-assign]
} -body {
  set r [expr {"x" := 0, "y" := 10, $x + $y}]
  list $r $x $y
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r x y
} -result {10 0 10}}

###############################################################################

runTest {test expr-var-assign-4.2 {
  enabled both: side-effecting assignment sequence
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma,var-assign]
} -body {
  set r [expr {"a" := 1, "b" := 2, "c" := $a + $b, $a * $b * $c}]
  list $r $a $b $c
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved r a b c
} -result {6 1 2 3}}

###############################################################################

runTest {test expr-var-assign-4.3 {
  enabled both: only TOP_COMMA enabled -- := still errors
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rc [catch {expr {"x" := 5, 1}} msg]
  list $rc [string match {*syntax error*} $msg]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 1}}

###############################################################################

runTest {test expr-var-assign-4.4 {
  enabled both: only VAR_ASSIGN enabled -- comma still errors
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set rc [catch {expr {"x" := 5, "y" := 6}} msg]
  list $rc [string match {*syntax error*} $msg]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 1}}

###############################################################################
#
# Section 5 -- type-handling on the assigned value
#
# `:=` simply uses the RHS's substituted string value.  No type
# coercion happens at assignment time -- the variable just
# receives whatever the RHS produced.
#
###############################################################################

runTest {test expr-var-assign-5.1 {
  enabled: integer RHS yields integer-valued variable
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  expr {"x" := 42}
  set x
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved x
} -result {42}}

###############################################################################

runTest {test expr-var-assign-5.2 {
  enabled: floating-point RHS yields floating-point variable
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  expr {"x" := 3.14}
  set x
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved x
} -result {3.14}}

###############################################################################

runTest {test expr-var-assign-5.3 {
  enabled: string RHS via [string ...] command sub
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  expr {"x" := [string toupper hello]}
  set x
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved x
} -result {HELLO}}

###############################################################################

runTest {test expr-var-assign-5.4 {
  Var-assign := with NO left operand drives the C1=F vector
  at th8_expr.c:2741 (th8ExprMakeTree assignment phase): the
  left-operand-finding loop walks iLeft down from jj-1 and,
  with no preceding token, exhausts iLeft >= 0 before
  finding a non-null token.  TH8 raises a syntax error,
  which the test catches.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set rcs {}
  lappend rcs [catch {expr {:= 5}} m]
  lappend rcs [catch {expr {:= "hello"}} m]
  lappend rcs [catch {expr {:= 42 + 1}} m]
  set rcs
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain rcs m saved
} -result {1 1 1}}

###############################################################################

runTest {test expr-var-assign-5.5 {
  Var-assign := with NO right operand drives the C1=F vector
  at th8_expr.c:2746 (th8ExprMakeTree right-operand search):
  the right-operand-finding loop walks i up from jj+1 and,
  with no following token, exhausts i < nToken before
  finding a non-null token.  The follow-up
  `i >= nToken` guard at L2751 returns TH8_ERROR.  TH8
  raises a syntax error caught by the test.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set var-assign]
} -body {
  set rcs {}
  lappend rcs [catch {expr {"x" :=}} m]
  lappend rcs [catch {expr {"y" := }} m]
  lappend rcs [catch {expr {"z" :=  }} m]
  set rcs
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain rcs m saved
} -result {1 1 1}}

###############################################################################

source tests/epilogue.tcl
