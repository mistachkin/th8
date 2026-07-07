###############################################################################
#
# coverage_wide_cache_oom.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the C1=F (pCached=NULL on OOM) vector at
# th8_core.c:15345 -- the wide-integer cache lookup
# returns NULL when th8CacheGetHash fails due to OOM
# during slot allocation, falling through to the parse
# path.  Existing tests never see pCached=NULL because
# Th8_FindInCache succeeds (allocates a fresh slot)
# under normal allocation conditions.
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

runTest {test widecacheoom-1.1 {
  Th8_FindInCache forced-NULL for TH8_CACHE_WIDE drives
  the (F,-) C1-Pair at th8_core.c:15345 -- pCached is
  NULL via the dedicated cache-lookup-failure hook
  (bypasses allocation-site timing), so the cache-hit
  branch is skipped and the function falls through to
  parse the wide integer directly.  Use [format "%d"]
  which calls Th8_ToWideInt with the real interp
  (unlike th8ExprEval's type-detection path which
  passes interp=NULL to avoid cache pollution).
  Verify the hook actually fires.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    format "%d" 12345
  } -failCacheLookup TH8_CACHE_WIDE]
  set c [::th8testlib::fault counters]
  expr {[lindex $c 4] > 0}
} -cleanup {
  unset -nocomplain r c
} -result {1}}

###############################################################################

runTest {test widecacheoom-2.1 {
  Th8_FindInCache forced-NULL for TH8_CACHE_DOUBLE drives
  the Bug-28 fall-through path in Th8_ToDouble's cache
  consultation.  Use [format "%f"] which calls Th8_ToDouble
  with the real interp.  Verify the hook fires.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    format "%f" 1.5
  } -failCacheLookup TH8_CACHE_DOUBLE]
  set c [::th8testlib::fault counters]
  expr {[lindex $c 4] > 0}
} -cleanup {
  unset -nocomplain r c
} -result {1}}

###############################################################################

runTest {test widecacheoom-3.1 {
  Th8_FindInCache forced-NULL for TH8_CACHE_LIST drives
  the splitlist Bug-28 fall-through path -- llength calls
  Th8_SplitList which consults the list cache with the
  real interp.  Verify the hook fires.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    llength {a b c d e}
  } -failCacheLookup TH8_CACHE_LIST]
  set c [::th8testlib::fault counters]
  expr {[lindex $c 4] > 0}
} -cleanup {
  unset -nocomplain r c
} -result {1}}

###############################################################################

source tests/epilogue.tcl
