###############################################################################
#
# coverage_cache_double_remove_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_cache.c L792 in
# `th8RemoveFromCache`:
#
#   if (pEntry && pEntry->pData) {
#
# 2-condition AND.  Pre-test report: 50% MC/DC -- C1-Pair
# covered (no-entry case from Th8_HashFind returning NULL)
# plus the (T,T) success case; C2-Pair (T,F) open because
# the tombstone scenario (entry kept after first remove
# with pData=NULL) is not produced by any in-tree caller --
# nothing in th8 calls th8RemoveFromCache twice in a row
# for the same key without intervening reuse.
#
# The new ::th8testlib::cache_double_remove helper does
# exactly that: populates the int cache then issues two
# back-to-back removes for "12345".  The second call hits
# the tombstoned entry, driving (T,F).
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

runTest {test cache_double_remove-1.1 {
  th8_cache.c L792 (T,F) -- back-to-back
  th8RemoveFromCache for the same key drives the
  tombstoned-entry vector.  Helper returns the empty
  string on success.
} -constraints {
    th8
} -setup {
} -body {
  th8testlib::cache_double_remove
} -cleanup {
} -result {}}

###############################################################################

source tests/epilogue.tcl
