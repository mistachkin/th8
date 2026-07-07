###############################################################################
#
# coverage_load_name_match_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_load.c L258 in th8LoadNameMatch:
#
#   if (nSymA > 0 && Th8_Memcmp(interp, zSymA, zSymB, nSymA) != 0) {
#
# 2 conditions; pre-test report shows the (T,T) and (T,F)
# vectors covered but the (F,-) C1-Pair never observed.
# Through the normal `load LIB ?SYM?` path th8CanonLoadName
# always emits a "lib:sym" canonical form so nSymA is always
# > 0; the C1=F vector requires calling th8LoadNameMatch
# directly with a load-name string that has no colon (and
# therefore zero-length symbol component).
#
# The new internal-stubs entry th8_LoadNameMatch plus the
# `::th8testlib::loadnamematch` testlib wrapper give the
# script direct access to the helper.  Two equal colon-less
# names drive (F, _ = T) -- the for the C1-Pair the second
# vector is the existing (T, *).
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

runTest {test loadnamematch-1.1 {
  th8_load.c L258 (F,-) -- two colon-less names both with
  empty symbol component.  nSymA == 0 short-circuits the
  AND, decision = F, falls through to the L261 lib-equality
  check which returns 1 (identical "libfoo" names match).
} -constraints {
    th8 loadLib
} -setup {
} -body {
  th8testlib::loadnamematch libfoo libfoo
} -cleanup {
} -result {1}}

###############################################################################

runTest {test loadnamematch-1.2 {
  th8_load.c L258 (F,-) variant -- different colon-less
  names.  nSymA == 0, falls through to L261 which detects
  different libs and returns 0 (or 1 if Th8_SameFile decides
  the names canonicalize to the same path -- both should
  fail to resolve, returning 0).
} -constraints {
    th8 loadLib
} -setup {
} -body {
  th8testlib::loadnamematch libfoo libbar
} -cleanup {
} -result {0}}

###############################################################################

runTest {test loadnamematch-1.3 {
  th8_load.c L258 (T,T) C1+C2-Pair closure (2026-06-18,
  batch #44).  Two colon-separated names with the SAME
  symbol-component LENGTH but DIFFERENT contents:
  "libfoo:sym1" vs "libfoo:sym2".  th8SplitLoadName
  extracts zSymA=sym1 (nSymA=4), zSymB=sym2 (nSymB=4).
  Length-equal check at L257 passes.  Then L258
  nSymA > 0 (T) AND Memcmp(sym1, sym2, 4) != 0 (T) ->
  result T -> return 0.  Paired with the existing (T,F)
  vector (load tests with matching symbol names) and
  (F,-) vector (above), this closes both C1- and
  C2-Pair at L258.
} -constraints {
    th8 loadLib
} -setup {
} -body {
  th8testlib::loadnamematch libfoo:sym1 libfoo:sym2
} -cleanup {
} -result {0}}

###############################################################################

source tests/epilogue.tcl
