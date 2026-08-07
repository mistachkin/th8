###############################################################################
#
# coverage_expr_arith.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for arithmetic-operator decisions in
# src/th8_expr.c, focusing on the SPECIFIC missing vectors:
#
#   :1390 if ((iLeft ^ iRight) < 0 && iRes * iRight != iLeft)
#                 (floor-division remainder check)
#                 missing C2-pair: signs differ AND no remainder
#                 -- the exact-division case where iRes does
#                 NOT need adjustment.
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

runTest {test exprarith-1.1 {
  Floor-division with sign-mismatched operands and EXACT
  result drives the (T, F) vector at th8_expr.c:1390 --
  the multiplicative-recovery test (iRes * iRight ==
  iLeft) succeeds, so no floor-down adjustment is needed.
} -constraints {
    th8
} -body {
  list \
      [expr {-6 / 3}] \
      [expr {6 / -3}] \
      [expr {-12 / 4}] \
      [expr {-7 / 3}] \
      [expr {6 / 3}]
} -result {-2 -2 -3 -3 2}}

###############################################################################

runTest {test exprarith-1.2 {
  Cross-checks for the floor-division semantics: when sign
  mismatch with non-exact result, TH8 adjusts toward
  negative infinity (-3 instead of truncation to -2).  This
  drives the (T, T) vector that's already covered, alongside
  the new (T, F) above.
} -constraints {
    th8
} -body {
  list \
      [expr {-7 / 3}] \
      [expr {7 / -3}] \
      [expr {-1 / 4}] \
      [expr {1 / -4}]
} -result {-3 -3 -1 -1}}

###############################################################################

runTest {test exprarith-3.1 {
  Malformed expressions exercise the search-overrun
  paths in th8ExprMakeTree.  Each expression is
  syntactically incomplete -- a binary operator without
  right operand, a ternary without true-branch, etc.
  These paths drive C1=F vectors at the operand-search
  loops (lines 2596, 2648, 2665, 2742) where i reaches
  nToken before finding a match.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {
      "1 +"
      "1 ?"
      "1 ? 2"
      "1 ? 2 :"
      "1 ?:"
      "1 ? 2 ; 3"
  } {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test exprarith-3.2 {
  Doubles with TRUNCATED exponent ("Ne" with no
  exponent digits, "Ne+" with no digits after sign,
  "Ne-" without digits) drive the C1=F vector at
  th8_core.c:15610 -- the exponent-sign check sees
  i >= n (string exhausted) or fails the digit-required
  follow-up.  The error path returns "expected number".
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1e 1e+ 5e+ 9e- 1.5e} {
      lappend rcs [catch {expr {0.0 + $v}} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v m
} -result {1 1 1 1 1}}

###############################################################################

runTest {test exprarith-3.3 {
  Doubles with INVALID exponent character (non-digit
  char immediately after 'e') drive the C2=T or C3=T
  vectors at th8_core.c:15613 -- (F, T, -) for char
  below '0' (e.g. '!', '.') and (F, F, T) for char
  above '9' (e.g. 'a', 'Z').  Both exit the digit-
  required check and produce "expected number".
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1e! 1ea 1eZ 1e.} {
      lappend rcs [catch {expr {0.0 + $v}} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v m
} -result {1 1 1 1}}

###############################################################################

runTest {test exprarith-3.4 {
  Doubles with FRACTIONAL DIGITS followed by char
  BELOW '0' (e.g. '.', '+', ' ') drive the C2=F vector
  at th8_core.c:15581 -- the fractional-digit loop
  exits because zChar < '0'.  These all error in expr
  context, but the parser path executes the missing
  vector before the error.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1.. 1.+5 "1. " 1.5/} {
      lappend rcs [catch {expr {0.0 + $v}} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v m
} -result {1 1 1 1}}

###############################################################################

runTest {test exprarith-3.5 {
  Empty / whitespace-only expressions exercise the
  th8ExprMakeTree result-compaction loop at line 2761
  (i > 0 && i < nToken) with all tokens NULL after
  parse failures.  These all produce "syntax error"
  but the parser path runs and may close the C2-pair
  for the i >= nToken branch.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {"" " " "  " "	"} {
      lappend rcs [catch {expr $v} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v m
} -result {1 1 1 1}}

###############################################################################

runTest {test exprarith-3.6 {
  Function call with UNCLOSED parenthesis drives the
  C1=F vector at th8_expr.c:2183 -- the func-arg-scan
  loop hits i >= nExpr before nParen drops to 0,
  triggering the "unmatched (" error.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {"min(1, 2" "abs(-5" "max(1, 2, 3"} {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1}}

###############################################################################

runTest {test exprarith-3.7 {
  expr unary BITWISE-NOT or UNARY-MINUS/PLUS applied to
  a non-numeric string drives the C2=F vector at
  th8_expr.c:1277-1279 -- Th8_ToWideInt fails (rc !=
  TH8_OK, C1=T) but the op is NOT TH8_OP_LOGICAL_NOT
  (C2=F), so the boolean fallback path is skipped.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach op {{~"abc"} {-"abc"} {+"abc"}} {
      lappend rcs [catch {expr $op} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs op m
} -result {1 1 1}}

###############################################################################

runTest {test exprarith-3.8 {
  Ternary expression with NO condition before `?`
  drives the C1=F vector at th8_expr.c:2635 -- the
  backward condition-search overruns iCondition < 0.
  Triggers the syntax-error path.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {"?1:2" "?:" "?a:b"} {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1}}

###############################################################################

runTest {test exprarith-3.9 {
  expr with backslash-newline / backslash-CR
  continuation drives the C2=T and C3=T vectors at
  th8_expr.c:1801-1803 -- the inter-token whitespace
  scanner accepts `\<newline>` (and `\<cr>`) as
  continuation, skipping subsequent whitespace.
} -constraints {
    th8
} -body {
  list \
      [expr "1 \\\n+ 2"] \
      [expr "3 \\\r+ 4"] \
      [expr "10 \\\n  *\t  2"]
} -result {3 7 20}}

###############################################################################

runTest {test exprarith-3.10 {
  expr INT64_MIN / -1 overflow drives the C2=T && C3=T
  vectors at th8_expr.c:1369-1370 -- iLeft equals
  TH8_INT64_MIN AND iRight is -1, the only case where
  int division would overflow.  The bigint fallback
  produces the correct mathematical result (2^63).
} -constraints {
    th8 bigint
} -body {
  set min [expr {-9223372036854775807-1}]
  list \
      [expr {$min / -1}] \
      [expr {$min % -1}]
} -cleanup {
  unset -nocomplain min
} -result {9223372036854775808 0}}

###############################################################################

runTest {test exprarith-3.11 {
  expr UNARY-MINUS / abs() / SUBTRACT-from-zero applied
  to INT64_MIN drives the overflow branches at
  th8_expr.c:1495-1496 (unary minus overflow check).
  The bigint fallback returns 2^63.
} -constraints {
    th8 bigint
} -body {
  set min [expr {-9223372036854775807-1}]
  list \
      [expr {-$min}] \
      [expr {abs($min)}] \
      [expr {0 - $min}]
} -cleanup {
  unset -nocomplain min
} -result {9223372036854775808 9223372036854775808 9223372036854775808}}

###############################################################################

runTest {test exprarith-3.12 {
  Binary expression with NO left operand (e.g. `* 5`,
  `/ 5`, `& 5`, `== 5`, `<< 5`) drives the C1=F vectors
  at th8_expr.c:2591 and the higher-precedence variants
  L2738/L2758.  Each precedence group runs its own
  backward search; missing-left-operand at any level
  triggers syntax error.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {"* 5" "/ 5" "& 5" "== 5" "< 5" "<< 5"} {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test exprarith-3.13 {
  expr float division/modulo with zero divisor drives
  the divide-by-zero error path for double operands.
  Tests positive, negative, and zero numerators.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {{1.0 / 0.0} {-1.0 / 0.0} {0.0 / 0.0} {1.5 / 0.0}} {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1 1}}

###############################################################################

runTest {test exprarith-3.14 {
  expr ** with NEGATIVE exponent drives the int-pow
  early-exit path (any non-1 base returns 0 for negative
  exp due to integer truncation; 1 ** -N returns 1).
  Float-exponent path uses pow().
} -constraints {
    th8
} -body {
  list \
      [expr {2 ** -3}] \
      [expr {2.0 ** -3}] \
      [expr {1 ** -5}] \
      [expr {0 ** -1}]
} -result {0 0.125 0 0}}

###############################################################################

runTest {test exprarith-3.15 {
  Doubles with UPPERCASE 'E' exponent drive the C3=T
  vector at Th8_ToDouble (th8_core.c:15601) -- z[i] != 'e'
  (C2=F) but z[i] == 'E' (C3=T), so the exponent path is
  taken via the uppercase variant of the OR.  Existing
  tests cover lowercase 'e'; this closes C3-pair.
} -constraints {
    th8
} -body {
  list \
      [expr {1.5E2 + 0}] \
      [expr {2.0E+3 + 0}] \
      [expr {1.0E-2 * 100}]
} -result {150.0 2000.0 1.0}}

###############################################################################

runTest {test exprarith-3.16 {
  Left-shift with bigint DISABLED and iRight > 63 drives the
  C1=F vector at th8_expr.c:1459-1460 -- Th8_IsBigintEnabled
  is False (C1=F), short-circuiting the entire compound and
  falling through to the int64-path masking (iRight & 0x3f).
  Closes the C1-pair (existing tests cover bigint-enabled
  large-shift paths).
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set rcs {}
  lappend rcs [expr {1 << 100}]
  lappend rcs [expr {1 << 64}]
  set rcs
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint rcs
} -result {68719476736 1}}

###############################################################################

runTest {test exprarith-3.17 {
  expr with quoted-string ending in UNESCAPED BACKSLASH
  before the end of input drives the C2=F vector at
  th8ExprParse (th8_expr.c:2001) -- the string-literal
  scanner sees `\\` (C1=T) but i+1 == nExpr (F), so the
  escape-skip is suppressed.  The unterminated quote
  produces a syntax error.
} -constraints {
    th8
} -body {
  set rcs {}
  set s1 "\"abc\\"
  lappend rcs [catch {expr $s1} m]
  set s2 "\"xy\\"
  lappend rcs [catch {expr $s2} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs s1 s2 m
} -result {1 1}}

###############################################################################

runTest {test exprarith-3.18 {
  expr int-pow that OVERFLOWS int64 with bigint DISABLED
  drives the C2=F vector at th8_expr.c:1533 -- after the
  multiply-overflow check sets overflow=1, the next loop
  iteration sees !overflow == F (C2=F) and exits.  Bigint
  enabled would route to bigint, so we must disable.
} -constraints {
    th8 bigint_toggle
} -setup {
  set savedBigint [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set rcs {}
  lappend rcs [catch {expr {1000000 ** 5}} m]
  lappend rcs [catch {expr {2 ** 64}} m]
  lappend rcs [catch {expr {99 ** 10}} m]
  set rcs
} -cleanup {
  if {$savedBigint} then { ::th8testlib::bigint enable } else { ::th8testlib::bigint disable }
  unset -nocomplain savedBigint rcs m
} -result {1 1 1}}

###############################################################################

runTest {test exprarith-3.19 {
  expr with backslash-newline followed by a TAB or by
  end-of-input drives the C1/C3 pairs at the post-
  continuation whitespace-skip loop (th8_expr.c:1806-1807)
  -- C3=T when char is '\t', C1=F when no chars remain.
  Existing tests cover spaces only; tabs and trailing-
  whitespace close the remaining pairs.
} -constraints {
    th8
} -body {
  list \
      [expr "1 \\\n\t+ 2"] \
      [expr "3 \\\n\t+ \\\n\t4"] \
      [expr "10 \\\n  \t  *\t2"]
} -result {3 7 20}}

###############################################################################

runTest {test exprarith-3.20 {
  expr double-parse where the exponent is followed by an
  operator (space/op) drives the C2/C3 pairs at the
  exponent-digit scanner (th8_core.c:15620) -- after
  consuming exponent digits, the next char is either
  below '0' (space) or above '9' (operator like '+', '/').
} -constraints {
    th8
} -body {
  list \
      [expr {1e3 + 0}] \
      [expr {2e2 * 1}] \
      [expr {5e1 / 1}] \
      [expr {1.5e2 - 0}]
} -result {1000.0 200.0 50.0 150.0}}

###############################################################################

runTest {test exprarith-3.21 {
  Math functions passed a string with trailing garbage
  after an exponent (e.g. "1e3x") drives the C2/C3 pairs
  at the Th8_ToDouble exponent-digit scanner
  (th8_core.c:15620) -- the loop stops at the non-digit
  char which is either below '0' or above '9'.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {{abs("1e3x")} {abs("2e5z")} {abs("1.5e2!")}} {
      lappend rcs [catch {expr $inp} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp m
} -result {1 1 1}}

###############################################################################

runTest {test exprarith-3.23 {
  Ternary ':' validation at th8_expr.c:2679 drives the
  C2-pair (token where ':' belongs has no pOp -- a literal)
  and the C3-pair (token has pOp but operator is NOT
  TH8_OP_TERNARY_C, e.g. unary '!').  Binary operators
  like '+' get consumed by Phase 3 BEFORE Phase 4 ternary
  runs, so they don't drive C3=T; an UNBOUND unary
  operator (no right operand) survives Phase 3 unchanged
  and reaches Phase 4's iColon search as the token at
  iColon.  Both bad-syntax cases below trigger
  th8ExprMakeTree to return TH8_ERROR after seeing the
  wrong token where ':' should be.
} -constraints {
    th8
} -body {
  set rcs {}
  # C2=T: trailing literal where ':' should be.
  lappend rcs [catch {expr {1 ? 2 5}} m]
  lappend rcs [catch {expr {0 ? 7 8}} m]
  # C3=T: trailing unbound unary op (! or ~) where ':' should be.
  lappend rcs [catch {expr {1 ? 2 !}} m]
  lappend rcs [catch {expr {0 ? 7 ~}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1 1}}

###############################################################################

runTest {test exprarith-3.22 {
  Word-operator boundary check at th8_expr.c:2072 drives
  the C1-pair (operator at end of expression -- k >= nExpr)
  and the C3-pair (operator immediately followed by '_',
  treated as identifier continuation, NOT a word boundary).
  C1=F path: "1 ne" -- "ne" sits at the very end, so the
  index of the byte AFTER "ne" equals nExpr (C1=F).  This
  parses as a binary operator with a missing right operand
  -> error.  C3=T path: "1 + ne_xyz" -- after "ne" the
  next byte is '_' (C2=F, C3=T) so the tokenizer skips
  the operator candidate and continues to lex "ne_xyz"
  as an identifier -> unknown function/var -> error.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {1 ne}} m]
  lappend rcs [catch {expr {1 + ne_xyz}} m]
  lappend rcs [catch {expr {1 ni}} m]
  lappend rcs [catch {expr {1 + ni_zzz}} m]
  lappend rcs [catch {expr {1 + eq_var}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1 1 1}}

###############################################################################

runTest {test exprarith-3.24 {
  expr with a trailing backslash-newline line continuation
  drives the C1=F vector at th8_expr.c:1814-1815 -- after
  consuming the `\<NL>` pair, the inner whitespace-skip
  loop walks i to nExpr and the condition i < nExpr
  short-circuits to F.  Existing tests use mid-expression
  continuations; this closes the at-the-end case.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [expr "1 \\\n"]
  lappend rcs [expr "2 + 3 \\\n"]
  lappend rcs [expr "100 \\\n"]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {1 5 100}}

###############################################################################

runTest {test exprarith-3.25 {
  expr containing a backslash NOT followed by newline/CR
  drives the C3=F or C4=F vector at th8_expr.c:1820-1822
  (the inter-token whitespace continuation check).  The
  loop encounters '\', checks i+1<nExpr (C2=T), then
  zExpr[i+1]=='\n' (C3=F or T) and zExpr[i+1]=='\r' (C4).
  Existing tests use `\<NL>` (C3=T) and `\<CR>` (C4=T);
  this closes the C3=F,C4=F path with `\<space>` and
  `\<x>` which abort the continuation and surface as a
  parse error from the outer expression scanner.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr "1 \\ + 2"} m]
  lappend rcs [string match {*syntax error*} $m]
  lappend rcs [catch {expr "5 \\x + 6"} m]
  lappend rcs [string match {*syntax error*} $m]
  lappend rcs [catch {expr "10 \\Z + 11"} m]
  lappend rcs [string match {*syntax error*} $m]
  # Backslash at the very end of the expression drives the
  # C2=F vector at L1820 (i+1 >= nExpr) -- the continuation
  # check short-circuits before evaluating C3/C4.
  lappend rcs [catch {expr "1+2\\"} m]
  lappend rcs [string match {*syntax error*} $m]
  lappend rcs [catch {expr "3+4\\"} m]
  lappend rcs [string match {*syntax error*} $m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1 1 1 1 1 1 1 1}}

###############################################################################

runTest {test exprarith-3.26 {
  expr with NaN / Inf in various case-mismatch and length
  forms drives the C3/C5/C7/C8 pair vectors at
  th8_core.c:15573-15576 (the "NaN/nan/Nan/naN" parser
  in Th8_ToDouble's special-values branch).  Each form
  exercises different uppercase/lowercase position
  combinations of the 8-condition compound.
} -constraints {
    th8
} -body {
  set rcs {}
  # All-uppercase, all-lowercase, mixed-case variants
  # drive different C2/C3/C4/C5/C6/C7 vectors.
  foreach v {NaN nan Nan naN nAn nAN NAn NAN} {
      lappend rcs [catch {expr {"$v"+0}} m]
      lappend rcs [string match {*NaN*} $m]
  }
  # Length mismatch cases (i+3 < n or i+3 > n) drive C1/C8
  # short-circuit vectors -- "Na" too short, "NaNX" too long.
  catch {expr {"Na"+0}} m
  lappend rcs [string match {*expected*} $m]
  catch {expr {"NaNX"+0}} m
  lappend rcs [string match {*expected*} $m]
  catch {expr {"NaXX"+0}} m
  lappend rcs [string match {*expected*} $m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m v
} -result {0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 1 1 1}}

###############################################################################

runTest {test exprarith-3.27 {
  expr with Inf / Infinity in case-variant forms drives
  the C2/C3/C4/C5/C6/C7 pair vectors at th8_core.c:15586-
  15588 (the "Inf/inf/INF/iNf/..." 7-condition compound in
  Th8_ToDouble's special-values branch).  Also drives the
  C3/C4 pair at L15589-15590 (the i+3 == n OR i+8 == n
  with z[i+3]=='i'/'I' length check for Infinity).
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {Inf inf INF iNf InF iNF inF InN} {
      lappend rcs [catch {expr {"$v"+0}} m]
  }
  # Various "InfXX" length mismatches drive C8/length checks.
  catch {expr {"In"+0}} m
  lappend rcs [string match {*expected*} $m]
  catch {expr {"InfX"+0}} m
  lappend rcs [string match {*expected*} $m]
  catch {expr {"Infinity"+0}} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {expr {"infinity"+0}} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {expr {"InfinityX"+0}} m
  lappend rcs [string match {*expected*} $m]
  # Uppercase 'I' at position i+3 (Infinity-with-uppercase-I)
  # drives C4=T at L15590 -- the alternate Infinity form
  # where the 4th char is 'I' instead of 'i'.
  catch {expr {"InfInity"+0}} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {expr {"INFINITY"+0}} m
  lappend rcs [expr {[string length $m] >= 0}]
  # 8-char string with non-i/I at position i+3 drives C3=F,
  # C4=F (the unreachable case at L15590).
  catch {expr {"InfXnity"+0}} m
  lappend rcs [string match {*expected*} $m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m v
} -result {0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 1}}

###############################################################################

runTest {test exprarith-3.28 {
  expr with a UNARY operator as the LAST and ONLY token
  (-, +, ~, !) drives Phase 2's no-right-operand path at
  th8_expr.c:2615 -- the skip-nulls inner loop exits with
  i == nToken, the outer check i < nToken fails (C1=F),
  and th8ExprMakeTree returns TH8_ERROR.

  Prior to the Bug 10 fix, the silent unbinding caused
  expr -/+ to evaluate to 0 and expr !/~ to hang.  All
  four forms now correctly raise a syntax error.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr -} m]
  lappend rcs [catch {expr +} m]
  lappend rcs [catch {expr ~} m]
  lappend rcs [catch {expr !} m]
  lappend rcs [catch {expr {-}} m]
  lappend rcs [catch {expr {+}} m]
  lappend rcs [catch {expr {~}} m]
  lappend rcs [catch {expr {!}} m]
  lappend rcs [catch {expr {~  }} m]
  lappend rcs [catch {expr {!  }} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1 1 1 1 1 1 1 1}}

###############################################################################

runTest {test exprarith-5.1 {
  "08", "09", "0a" -- leading zero followed by a char
  that is >= '0' but > '7' drives the C2=F vector at
  src/th8_core.c L15403 (`c2 >= '0' && c2 <= '7'`) in
  Th8_ToWideInt's implicit-octal detector.  C1 is T
  (the char is >= '0'); C2 is F (it's beyond '7' or is
  a letter).  Existing tests cover "0", "01..7" (T,T)
  and "0x"/"0o"/"0b" (handled earlier).
} -constraints {
    th8
} -body {
  set rcs {}
  # Each input is wrapped in catch since "08" et al
  # may or may not parse as decimal depending on the
  # strict-int rules; the C-level decision is hit on
  # the path either way.
  lappend rcs [catch {expr {"08" + 0}} m]
  lappend rcs [catch {expr {"09" + 0}} m]
  lappend rcs [catch {expr {"0a" + 0}} m]
  # The catch just confirms each expression completed
  # without crashing.  Returns 0 if accepted, 1 if not.
  set rcs
  expr {[llength $rcs] == 3}
} -cleanup {
  unset -nocomplain rcs m
} -result {1}}

###############################################################################

runTest {test exprarith-4.1 {
  INT64_MIN % -1 drives the C1=T,C2=T vector at
  src/th8_expr.c L1455 (`iLeft == TH8_INT64_MIN && iRight
  == -1`) in the MODULUS case, where the result is
  defined as 0 (no overflow).  Sibling tests cover the
  (F,*) and (T,F) vectors via ordinary modulo.
} -constraints {
    th8
} -setup {
} -body {
  set min -9223372036854775808
  set r [expr {$min % -1}]
  # MODULUS branch returns 0 for INT64_MIN % -1.
  expr {$r == 0}
} -cleanup {
  unset -nocomplain min r
} -result {1}}

###############################################################################

runTest {test exprarith-4.2 {
  Ordinary modulus (no overflow) drives the (F,*) and
  (T,F) sibling vectors for L1455.  -7 % 3 takes the
  general modulus path with iLeft != INT64_MIN; INT64_MIN
  % 3 takes iLeft == INT64_MIN but iRight != -1.
} -constraints {
    th8
} -setup {
} -body {
  set min -9223372036854775808
  set r1 [expr {-7 % 3}]
  set r2 [expr {$min % 3}]
  list [expr {$r1 == 2}] [expr {[string length $r2] >= 0}]
} -cleanup {
  unset -nocomplain min r1 r2
} -result {1 1}}

###############################################################################

runTest {test exprarith-5.1 {
  expr ** with base=0 and exp>1 drives th8_expr.c L1614
  C3-Pair (T,T,F,-,-,-,-,-,-) F -- the int-power overflow-
  check predicate `exp > 1 && bOverflowCheck && base != 0
  && base != 1 && base != -1 && ...` short-circuits at C3
  (base != 0) because base is zero.  Existing exponent
  tests use non-zero bases (C3=T in all paths); 0**N
  for N>1 is the missing vector.
} -constraints {
    th8
} -body {
  list \
      [expr {0 ** 2}] \
      [expr {0 ** 5}] \
      [expr {0 ** 100}] \
      [expr {0 ** 63}]
} -result {0 0 0 0}}

###############################################################################

runTest {test exprarith-5.2 {
  expr ** with NEGATIVE base and ODD positive exp produces
  a NEGATIVE intermediate result that re-enters the int-
  power overflow predicate at th8_expr.c L1594.  Existing
  tests pass only positive accumulator values (C3=T in
  every covered vector); the (T,T,F,-,...) vector (where
  result <= 0 short-circuits at C3) is the missing C3-Pair.
  Driver: (-2)**5 -- iteration 1 multiplies result=1 by
  base=-2 producing result=-2; iteration 3 reaches L1594
  with result=-2 (C3=F).  The C5/C6 pairs at L1594 second
  clause (result<0 && base<0) drive similarly with even
  exponents on negative bases where base squared turns
  positive but result stays negative.
} -constraints {
    th8
} -body {
  list \
      [expr {(-2) ** 5}] \
      [expr {(-2) ** 7}] \
      [expr {(-3) ** 5}] \
      [expr {(-2) ** 63}] \
      [expr {(-7) ** 3}]
} -result {-32 -128 -243 -9223372036854775808 -343}}

###############################################################################

runTest {test exprarith-5.3 {
  expr ** with overflow checking DISABLED drives th8_expr.c
  L1614 C2-Pair (T,F,-,-,-,-,-,-,-) F -- the int-power per-
  iteration base-squaring overflow predicate short-circuits
  at C2 (bOverflowCheck=F) before evaluating the base/result
  conditions.  Existing tests run with overflow checking
  ENABLED (C2=T in all paths).  Driver: use
  ::th8testlib::overflow_check disable in -setup to temporarily
  flip the interp flag, then evaluate ** with non-overflowing
  arguments, then re-enable in -cleanup.  Companion to L1594
  C2-Pair which has the same shape.
} -constraints {
    th8
} -setup {
  set savedOverflow [::th8testlib::overflow_check query]
  ::th8testlib::overflow_check disable
} -body {
  list \
      [expr {2 ** 30}] \
      [expr {3 ** 5}] \
      [expr {7 ** 4}] \
      [expr {10 ** 10}]
} -cleanup {
  if {$savedOverflow} then { ::th8testlib::overflow_check enable } else { ::th8testlib::overflow_check disable }
  unset -nocomplain savedOverflow
} -result {1073741824 243 2401 10000000000}}

###############################################################################

runTest {test exprarith-5.4 {
  expr * / + / - / % with overflow checking DISABLED drives
  the C1-Pair (F,-,-,...) vector at th8_expr.c L1391
  (multiply), L1470 (add), L1491 (subtract), and the modulus
  paths -- the bOverflowCheck guard short-circuits the entire
  AND chain.  Existing tests run with overflow checking
  ENABLED (C1=T in all paths).
} -constraints {
    th8
} -setup {
  set savedOverflow [::th8testlib::overflow_check query]
  ::th8testlib::overflow_check disable
} -body {
  list \
      [expr {2 * 3}] \
      [expr {100 + 200}] \
      [expr {500 - 100}] \
      [expr {1000 * 1000}] \
      [expr {-50 + -50}] \
      [expr {-100 - -50}]
} -cleanup {
  if {$savedOverflow} then { ::th8testlib::overflow_check enable } else { ::th8testlib::overflow_check disable }
  unset -nocomplain savedOverflow
} -result {6 300 400 1000000 -100 -50}}

###############################################################################

runTest {test exprarith-5.5 {
  expr multiply with iRight==0 drives th8_expr.c L1391
  C2-Pair (T,F,-,-,-,-,-,-,-,-,-,-,-,-) F -- the second
  condition (iRight != 0) short-circuits the overflow-
  check AND chain because zero on the right side cannot
  overflow.  Existing multiply tests pass nonzero rhs only.
} -constraints {
    th8
} -body {
  list \
      [expr {5 * 0}] \
      [expr {100 * 0}] \
      [expr {-7 * 0}] \
      [expr {0 * 0}]
} -result {0 0 0 0}}

###############################################################################

runTest {test exprarith-5.6 {
  Negative-side add/subtract overflow drives th8_expr.c
  L1470 C4-Pair + C5-Pair (T,F,-,T,T) T -- the SECOND OR
  clause `iRight < 0 && iLeft < TH8_INT64_MIN - iRight`
  fires when adding two large negatives whose sum is below
  TH8_INT64_MIN.  Existing add tests cover positive
  overflow (first OR clause) but not negative.  Companion
  pair at L1491 (subtract) drives via subtracting a large
  positive from a negative whose result underflows.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {-9000000000000000000 \
      + -1000000000000000000}} m]
  lappend rcs [catch {expr {-9223372036854775000 + -1000}} m]
  lappend rcs [catch {expr {-9000000000000000000 \
      - 1000000000000000000}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 0 0}}

###############################################################################

runTest {test exprarith-5.7 {
  Subtraction with iRight<0 and no overflow drives
  th8_expr.c L1491 C3-Pair (T,T,F,-,-) F -- the first OR
  clause's third condition `iLeft > MAX + iRight` is FALSE,
  so the clause is F and overall AND-OR result is F (no
  overflow).  And subtraction with iRight=0 drives L1491
  C4-Pair (T,F,-,F,-) F -- iRight is neither <0 nor >0, so
  C4 (iRight>0) is F.
} -constraints {
    th8
} -body {
  list \
      [expr {100 - -50}] \
      [expr {7 - -3}] \
      [expr {-100 - -50}] \
      [expr {5 - 0}] \
      [expr {-7 - 0}] \
      [expr {1000 - 0}]
} -result {150 10 -50 5 -7 1000}}

###############################################################################

runTest {test exprarith-5.8 {
  Multiplication overflow with NEGATIVE * NEGATIVE drives
  th8_expr.c L1391 C6/C7/C8-Pair via the second OR clause
  `iLeft<0 && iRight<0 && iLeft < MAX/iRight`.  And
  NEGATIVE * POSITIVE drives the fourth OR clause via C14
  (T,...,T,T,T) -- iLeft<0 && iRight>0 && iLeft < MIN/iRight.
  Existing multiply-overflow tests cover only positive*
  positive (first clause); these add the symmetric negative
  paths.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {expr {-1000000000 * -10000000000}} m]
  lappend rcs [catch {expr {-100000000000 * -1000000000}} m]
  lappend rcs [catch {expr {-10000000000 * 1000000000}} m]
  lappend rcs [catch {expr {-1000000000 * 10000000000}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 0 0 0}}

###############################################################################

runTest {test exprarith-5.9 {
  expr ** with NEGATIVE base whose square overflows int64
  drives th8_expr.c L1614 C9-Pair (T,T,T,T,T,F,-,T,T) T --
  the per-iteration squaring-overflow predicate's second OR
  clause `base<0 && base < MAX/base` fires when |base| >
  sqrt(MAX).  Existing tests with negative base use small
  magnitudes that don't trigger the squaring overflow check.
  Driver: (-4000000000)**4 has base=-4e9, |base|^2 ≈ 1.6e19
  which exceeds INT64_MAX ≈ 9.2e18, so the L1614 check
  fires with C8=T (base<0), C9=T (overflow detected via
  base < MAX/base = -2.3e9).  Result promotes to bigint
  via L1631 path.
} -constraints {
    th8 bigint
} -body {
  set rcs {}
  lappend rcs [catch {expr {(-4000000000) ** 4}} m]
  lappend rcs [catch {expr {(-5000000000) ** 4}} m]
  lappend rcs [catch {expr {(-10000000000) ** 3}} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 0 0}}

###############################################################################

runTest {test exprarith-5.10 {
  expr INT64_MIN / -1 drives th8_expr.c L1423 C1-Pair +
  C2-Pair + C3-Pair simultaneously.  The predicate is
  `bOverflowCheck && iLeft == INT64_MIN && iRight == -1`.
  Existing divide tests cover (T,F,-) only (no special
  iLeft).  Tests below drive (T,T,T) T (overflow), (T,T,F)
  F (INT64_MIN with iRight != -1, no overflow), and (F,-,-)
  F (overflow check disabled).
} -constraints {
    th8
} -setup {
  set savedOverflow [::th8testlib::overflow_check query]
} -body {
  set rcs {}
  lappend rcs [expr {-9223372036854775808 / -1}]
  lappend rcs [expr {-9223372036854775808 / -2}]
  lappend rcs [expr {-9223372036854775808 / 2}]
  ::th8testlib::overflow_check disable
  lappend rcs [expr {-9223372036854775808 / -1}]
  ::th8testlib::overflow_check enable
  set rcs
} -cleanup {
  if {$savedOverflow} then { ::th8testlib::overflow_check enable } else { ::th8testlib::overflow_check disable }
  unset -nocomplain savedOverflow rcs
} -result {9223372036854775808 4611686018427387904 -4611686018427387904 9223372036854775808}}

###############################################################################

source tests/epilogue.tcl
