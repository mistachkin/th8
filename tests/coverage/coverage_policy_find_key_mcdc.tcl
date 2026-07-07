###############################################################################
#
# coverage_policy_find_key_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L1757
# in `Th8_PolicyFindKey`:
#
#   if (!p || !p->paKeys || !zToken) return NULL;
#
# 3-cond OR.  Pre-test report: 66.67% MC/DC -- C1-Pair
# (p=NULL) and C3-Pair (zToken=NULL) covered, but
# C2-Pair (p->paKeys=NULL with non-NULL p and non-NULL
# zToken) was open because all in-tree callers preload
# at least one key before invoking FindKey.  Direct
# invocation immediately after Th8_InstallSignedPolicy
# (which leaves paKeys=NULL until first preload) drives
# (F,T,F).
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

runTest {test policyfindkey-1.1 {
  th8_policy.c L1757 (F,T,-) -- Th8_PolicyFindKey called
  with non-NULL pCtx (fresh install, paKeys=NULL) and a
  valid 16-char zToken drives C2=T short-circuit.
  Returns NULL (no key cache).
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  th8testlib::policyfindkey_nopaeys
} -cleanup {
} -result {1}}

###############################################################################

source tests/epilogue.tcl
