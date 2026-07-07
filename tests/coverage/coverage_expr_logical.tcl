###############################################################################
#
# coverage_expr_logical.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the C-level logical-AND and logical-OR
# operators in src/th8_expr.c:
#
#   line 1491: iRes = iLeft && iRight;
#   line 1493: iRes = iLeft || iRight;
#
# Existing expr tests cover the success vector (typically T,T)
# but the F,- and T,F vectors require explicit operand-zero
# combinations.  Each MC/DC compound has 2 conditions, requiring
# 3 vectors total: F,- ; T,F ; T,T.
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
# Section 1 -- logical AND vectors
#
###############################################################################

runTest {test exprlog-1.1 {
  expr 0 && 1 drives F,- vector at line 1491
} -constraints {
    th8
} -body {
  expr {0 && 1}
} -result {0}}

###############################################################################

runTest {test exprlog-1.2 {
  expr 1 && 0 drives T,F vector at line 1491
} -constraints {
    th8
} -body {
  expr {1 && 0}
} -result {0}}

###############################################################################

runTest {test exprlog-1.3 {
  expr 1 && 1 drives T,T vector at line 1491
} -constraints {
    th8
} -body {
  expr {1 && 1}
} -result {1}}

###############################################################################

runTest {test exprlog-1.4 {
  expr 0 && 0 drives F,- vector
} -constraints {
    th8
} -body {
  expr {0 && 0}
} -result {0}}

###############################################################################

runTest {test exprlog-1.5 {
  expr with non-trivial AND of variable values
} -constraints {
    th8
} -body {
  set a 5
  set b 0
  list [expr {$a && $b}] [expr {$a && 7}] [expr {0 && $a}]
} -cleanup {
  unset -nocomplain a b
} -result {0 1 0}}

###############################################################################
#
# Section 2 -- logical OR vectors
#
###############################################################################

runTest {test exprlog-2.1 {
  expr 0 || 0 drives F,F vector at line 1493
} -constraints {
    th8
} -body {
  expr {0 || 0}
} -result {0}}

###############################################################################

runTest {test exprlog-2.2 {
  expr 1 || 0 drives T,- vector at line 1493
} -constraints {
    th8
} -body {
  expr {1 || 0}
} -result {1}}

###############################################################################

runTest {test exprlog-2.3 {
  expr 0 || 1 drives F,T vector at line 1493
} -constraints {
    th8
} -body {
  expr {0 || 1}
} -result {1}}

###############################################################################

runTest {test exprlog-2.4 {
  expr 1 || 1 drives T,- vector
} -constraints {
    th8
} -body {
  expr {1 || 1}
} -result {1}}

###############################################################################

runTest {test exprlog-2.5 {
  expr with non-trivial OR of variable values
} -constraints {
    th8
} -body {
  set a 5
  set b 0
  list [expr {$a || $b}] [expr {$b || $a}] [expr {0 || 0}]
} -cleanup {
  unset -nocomplain a b
} -result {1 1 0}}

###############################################################################

runTest {test exprlog-3.1 {
  expr with logical-NOT applied to a NON-NUMERIC,
  NON-BOOLEAN string drives the C1=F vector at
  th8_expr.c:1289 -- Th8_ToWideInt fails (string isn't
  a number), then Th8_ToBoolean also fails (string isn't
  a boolean keyword), so rc != TH8_OK and the right-
  operand check is short-circuited.  expr returns an
  error rather than a value.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {abc xyzzy not_a_bool 12abc} {
      set s "!\"$inp\""
      lappend rcs [catch {expr $s} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp s m
} -result {1 1 1 1}}

###############################################################################

runTest {test exprlog-4.1 {
  expr `in` operator with a search value whose length
  differs from any list element drives the C1=F vector
  at th8_expr.c:1638 -- the length-equality short-
  circuit fires for every element, returning iFound=0
  without ever calling Th8_Memcmp.
} -constraints {
    th8
} -body {
  list \
      [expr {"x" in {alpha beta gamma}}] \
      [expr {"verylong" in {a b c d e}}] \
      [expr {"abc" in {alpha beta gamma}}] \
      [expr {"alpha" in {alpha beta gamma}}]
} -result {0 0 0 1}}

###############################################################################

source tests/epilogue.tcl
