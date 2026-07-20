###############################################################################
#
# coverage_expr_sweep.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for th8ExprMakeTree's ternary false-branch scan
# (th8_expr.c L2763):
#
#     for (iFalseBranch = iColon + 1;
#          iFalseBranch < nToken && !apToken[iFalseBranch];
#          iFalseBranch++) { }
#
# The existing ternary tests use SIMPLE false branches, whose
# first token is non-NULL -- so only the (F,-) and (T,F) vectors
# were seen.  The (T,T) vector (iFalseBranch < nToken AND the
# token is NULL, i.e. skip-a-null) requires a false branch whose
# leading token was folded away by a higher-precedence operator
# (processed before the low-precedence ternary), leaving a NULL
# slot right after the ':'.  A binary-op false branch
# (e.g. `1 ? 9 : 2 + 3`) produces exactly that, closing both
# C-pairs of the decision.
#
# (Other th8_expr.c misses are not chased here: the overflow arms
# are already covered, L917/L968 are intrinsic-dead Bug-26
# malformed-tree guards, and several are clang-capped 14-condition
# compounds.)
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

runTest {test exprsweep-1.1 {
  A ternary whose false branch is a binary-op expression
  (`1 ? 9 : 2 + 3`) folds the branch's leading token to NULL, so
  th8ExprMakeTree's false-branch scan at th8_expr.c L2763 takes
  the (T,T) skip-a-null vector -- closing both C-pairs of
  `iFalseBranch < nToken && !apToken[iFalseBranch]` against the
  already-covered (F,-) end-of-tokens and (T,F) immediate-term
  vectors.  The true-condition and false-condition forms exercise
  the branch either way; results verify the arithmetic is intact.
} -constraints {
    th8
} -body {
  list [expr {1 ? 9 : 2 + 3}] [expr {0 ? 2 + 3 : 9}] \
      [expr {0 ? 1 : 8 * 2 + 1}] [expr {1 ? 5 : 10 % 3}]
} -cleanup {
} -result {9 9 17 5}}

###############################################################################

source tests/epilogue.tcl
