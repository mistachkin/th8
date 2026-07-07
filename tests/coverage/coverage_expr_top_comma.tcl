###############################################################################
#
# coverage_expr_top_comma.tcl --
#
# Tests for the TH8_EXPR_TOP_COMMA opt-in expression-grammar
# feature.  When the flag is set, top-level `,` separates
# sub-expressions; the result of the LAST sub-expression is the
# overall expression value.  Variant B: the comma is a separator
# at the TOP of the expression only -- it is NOT a binary
# operator and may NOT appear inside parentheses, ternary
# operands, or any other scope without parens.
#
# Each test sets up the feature flag in -setup, exercises the
# behaviour in -body, and restores the prior flag set in
# -cleanup so the rest of the suite continues to run in strict
# mode.
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
# Section 1 -- basic top-level sequence
#
###############################################################################

runTest {test expr-top-comma-1.1 {
  enabled: 1, 2, 3 returns the last sub-expression
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {1, 2, 3}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {3}}

###############################################################################

runTest {test expr-top-comma-1.2 {
  enabled: a single sub-expression behaves identically to strict
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {1 + 2}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {3}}

###############################################################################

runTest {test expr-top-comma-1.3 {
  enabled: side effects in left sub-expressions persist
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {[set x 7], [set y 11], $x * $y}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved x y
} -result {77}}

###############################################################################

runTest {test expr-top-comma-1.4 {
  enabled: error in left sub-expression aborts the chain
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rc [catch {expr {1/0, [set ::__must_not_run 1]}} msg]
  list $rc [info exists ::__must_not_run]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg ::__must_not_run
} -result {1 0}}

###############################################################################
#
# Section 2 -- variant-B contract: comma confined to top level
#
# Top-level-only means: commas inside `(...)`, ternary operands,
# or function-call argument lists are NOT treated as the new
# operator.  Each test below verifies one of those scopes.
#
###############################################################################

runTest {test expr-top-comma-2.1 {
  enabled: comma inside parens still errors (variant B)
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rc [catch {expr {(1, 2)}} msg]
  list $rc $msg
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 {syntax error in expression: "(1, 2)"}}}

###############################################################################

runTest {test expr-top-comma-2.2 {
  enabled: comma inside ternary errors at the bareword
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rc [catch {expr {a ? b, c : d}} msg]
  list $rc $msg
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 {invalid bareword "a"}}}

###############################################################################

runTest {test expr-top-comma-2.3 {
  enabled: pow(2,3) still works (function-call comma unaffected)
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {pow(2, 3)}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {8.0}}

###############################################################################

runTest {test expr-top-comma-2.4 {
  enabled: comma inside braces is literal text, not a separator
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {{a,b}}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {a,b}}

###############################################################################

runTest {test expr-top-comma-2.5 {
  enabled: comma inside double quotes is literal text
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {"a,b"}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {a,b}}

###############################################################################

runTest {test expr-top-comma-2.6 {
  enabled: comma inside command-sub bytes is owned by inner Tcl rules
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {[set ::__inner "a,b"]}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved ::__inner
} -result {a,b}}

###############################################################################
#
# Section 3 -- whitespace and continuation handling
#
###############################################################################

runTest {test expr-top-comma-3.1 {
  enabled: whitespace around commas is fine
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  expr {1   ,   2   ,   3}
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {3}}

###############################################################################

runTest {test expr-top-comma-3.2 {
  enabled: trailing comma errors (empty trailing sub-expression)
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rc [catch {expr {1,}} msg]
  list $rc [string match {*syntax error*} $msg]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 1}}

###############################################################################

runTest {test expr-top-comma-3.3 {
  enabled: leading comma errors (empty leading sub-expression)
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rc [catch {expr {,1}} msg]
  list $rc [string match {*syntax error*} $msg]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 1}}

###############################################################################

runTest {test expr-top-comma-3.4 {
  enabled: empty middle sub-expression errors
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rc [catch {expr {1,,2}} msg]
  list $rc [string match {*syntax error*} $msg]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rc msg
} -result {1 1}}

###############################################################################

runTest {test expr-top-comma-4.1 {
  enabled: comma INSIDE a `[...]` command-sub at top level
  drives the C2=F vector at th8ExprFindTopComma
  (th8_expr.c:3010-3011) -- parenDepth == 0 (T) but
  bracketDepth != 0 (F), so the inner comma is NOT
  treated as a top-level separator.  The OUTER comma
  between `[...]` and the next term IS the top-level
  separator and is the one returned.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  list \
      [expr {[string length a,b,c], 7}] \
      [expr {[string length x,y], 13}]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {7 13}}

###############################################################################

runTest {test expr-top-comma-4.2 {
  enabled: literal `]` inside `"..."` quote WITHOUT a
  matching prior `[` drives the C2=F vector at
  th8ExprFindTopComma (th8_expr.c:2980) -- inside the
  inQuote handler, the scanner sees `]` (C1=T) but
  bracketDepth is 0 (C2=F), so no decrement happens.
  The top-level comma after the closing quote is the
  separator.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  list \
      [expr {"a]b", 7}] \
      [expr {"x]]y", 11}]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {7 11}}

###############################################################################

runTest {test expr-top-comma-5.1 {
  enabled: a BACKSLASH-X escape sequence in the expression
  drives the C1-pair vector at th8ExprFindTopComma
  (th8_expr.c:3009) -- the scanner sees '\\' (C1=T)
  followed by a non-end character, so the 2-byte consume
  branch fires.  Existing top-comma tests use unescaped
  text (C1=F throughout); this closes the C1-pair.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  list \
      [expr {"a\nb", 5}] \
      [expr {"x\\y", 9}] \
      [expr {"p\tq", 12}] \
      [expr {"\"esc\"", 13}]
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved
} -result {5 9 12 13}}

###############################################################################

runTest {test expr-top-comma-7.1 {
  TRAILING backslash in the expression buffer drives
  src/th8_expr.c L3061 (T,F) -- `c == '\\' && i + 1 <
  nExpr`.  When the lone backslash is the final byte, C2
  is false and the scanner exits the consume-2-byte
  fast-path.  The expression errors (lone backslash is
  syntactically invalid) but the parser still reaches
  this branch before failing.
} -constraints {
    th8
} -setup {
  set saved [th8testlib::expr_features set top-comma]
} -body {
  set rcs {}
  # Backslash as the trailing byte of the comma-scanner
  # input -- the parser scans through the expression
  # looking for top-level commas and hits the trailing
  # '\\' as its final char.
  lappend rcs [catch {expr {1,2\\}} m]
  lappend rcs [catch {expr {1\\}} m]
  set rcs
} -cleanup {
  th8testlib::expr_features set $saved
  unset -nocomplain saved rcs m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
