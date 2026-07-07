###############################################################################
#
# coverage_secure_canary_noppage_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/crypto/th8_secure.c L167
# in `th8SecureCheckCanary`:
#
#   if (!pKS || !pKS->pPage) return TH8_ERROR;
#
# 2-cond OR.  Pre-test report: 50% MC/DC -- C1-Pair
# covered (existing testlib drive with NULL pKS plus
# normal valid-keystore (F,F) baseline), C2-Pair (F,T)
# open because no in-tree caller produces a non-NULL
# pKS with pPage==NULL.  The new
# ::th8testlib::securecanary_noppage helper passes a
# 1024-byte zeroed memory region cast to void*; the
# struct's first field is `unsigned char *pPage`
# (th8_secure.c:128) so pKS->pPage is NULL by struct-
# layout invariant.
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

runTest {test securecanary_noppage-1.1 {
  th8_secure.c L167 (F,T) -- th8SecureCheckCanary with
  non-NULL pKS-but-pPage=NULL drives C2=T short-circuit.
  Helper returns 1 if TH8_ERROR was returned.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  th8testlib::securecanary_noppage
} -cleanup {
} -result {1}}

###############################################################################

source tests/epilogue.tcl
