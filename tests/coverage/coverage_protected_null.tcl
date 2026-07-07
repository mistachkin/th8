###############################################################################
#
# coverage_protected_null.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_protect.c L365
# (th8ProtectedCheckCanary) and L435 (th8ProtectedData):
# both predicates are `NEVER(!pRegion) || !pRegion->pPage`.
# Under TH8_OMIT_AUXILIARY_SAFETY_CHECKS the NEVER macro
# folds to constant false, leaving `!pRegion->pPage` as the
# only live condition.  The {C,F} vector (pPage non-NULL)
# is exercised by every protected-allocation call.  The
# {C,T} vector requires a region whose pPage is NULL --
# which the production allocator never produces (allocation
# either succeeds with pPage set, or returns NULL outright;
# no caller holds a region whose struct is valid but whose
# pPage was zeroed after the fact).
#
# th8testlib::protected_null_page synthesises such a region
# in stack storage and routes the call through the internal
# stubs table (TH8_INTERNAL functions are hidden-visibility
# symbols not reachable via libth8stub.a).
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

runTest {test protected_null-1.1 {
  Drive th8_protect.c L352 (th8ProtectedCheckCanary) and L424
  (th8ProtectedData) defensive `if (!pRegion || !pRegion->pPage)`
  guards.  The exerciser covers both C2-Pair {C,T} (pPage==NULL)
  and C1-Pair {T,-} (pRegion==NULL) vectors.  Both functions must
  return their failure indicators (TH8_ERROR / NULL) without
  dereferencing pPage in either branch.
} -constraints {
    th8 loadLib crypto_enabled
} -body {
  th8testlib::protected_null_page
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
