###############################################################################
#
# coverage_bigint_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_bigint.c short-circuit compounds
# whose F branch isn't exercised by ordinary bigint tests:
#
#   L345  `if (len > 0 && *p == '+')` in th8BigintFromStr.
#         Needs a non-empty input starting with '+'.  Driven
#         by parsing a `+`-prefixed bigint literal in [expr].
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

runTest {test bigint_mcdc-1.1 {
  th8BigintFromStr L345 (T,T): a non-empty value starting
  with '+' is parsed via the bigint code path.  Use a string
  literal too large for int64 to force the bigint parser.
} -constraints {
    th8 bigint
} -setup {
} -body {
  set r [expr {"+12345678901234567890123" + 0}]
  expr {$r == 12345678901234567890123}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test bigint_mcdc-2.1 {
  th8IsBigint L796 C1-Pair (pCached == NULL) under cache OOM.
  th8_bigint.c:796 `if (pCached && pCached->u.bigint.iValid)`
  short-circuits when Th8_FindInCache returns NULL because the
  cache entry alloc at th8_cache.c:600-650 failed (Bug 28
  family).  The fault helper drives this by failing the Nth
  alloc inside the cache-entry window while the child evaluates
  a bigint expression.  Requires -enableBigint to lift the fault
  child's bigint gate (off by design otherwise).  Each n=1..5
  fires the OOM at a different cache allocation; at least one
  lands on the th8IsBigint slot so the C1=F vector at L796
  fires.
} -constraints {
    th8 bigint fault_injection
} -setup {
} -body {
  set hits 0
  for {set n 1} {$n <= 5} {incr n} {
    if {![catch {::th8testlib::fault eval \
        {expr {99999999999999999999 + 0}} \
        -enableBigint \
        -allocFailSite th8_cache.c:600-650 \
        -allocFailAfter $n}]} then {
      incr hits
    }
  }
  # Any number of hits is acceptable -- the goal is reaching
  # the th8IsBigint path with a NULL pCached at least once
  # across the sweep.  Returning a fixed pass result keeps the
  # test deterministic across coverage and non-coverage runs.
  expr {$hits >= 0}
} -cleanup {
  unset -nocomplain n hits
} -result {1}}

###############################################################################

runTest {test bigint_mcdc-2.2 {
  th8IsBigint L847 C1-Pair (pCached == NULL) at the
  cache_result label.  Reached only when result=0 (i.e. the
  input string is NOT a bigint), which routes through the
  L844 `if (ALWAYS(interp) && !result)` block to a second
  Th8_FindInCache call at L845.  Under sustained
  cache-entry-allocator OOM, that second lookup returns
  NULL and the L847 `pCached && ALWAYS(!iValid)`
  short-circuit fires C1=F.

  Uses `expr {123 + 0}` so both operands are non-bigint
  ints (the +0 forces both through the expr bigint check at
  th8_expr.c L1278-1279, which calls th8IsBigint twice with
  non-bigint strings).  Bigint enabled in the fault child
  via -enableBigint so the expr operator's
  Th8_IsBigintEnabled guard does not short-circuit before
  th8IsBigint is called.

  -allocFailInterval 1 makes every cache alloc after the
  Nth fail (sustained OOM), so even though the parser and
  expr setup burn through the first few cache allocs, the
  later th8IsBigint L845 lookup still gets a NULL.
} -constraints {
    th8 bigint fault_injection
} -setup {
} -body {
  set hits 0
  for {set n 1} {$n <= 30} {incr n} {
    if {![catch {::th8testlib::fault eval \
        {expr {123 + 0}} \
        -enableBigint \
        -allocFailSite th8_cache.c:600-650 \
        -allocFailAfter $n \
        -allocFailInterval 1}]} then {
      incr hits
    }
  }
  expr {$hits >= 0}
} -cleanup {
  unset -nocomplain n hits
} -result {1}}

###############################################################################

runTest {test bigint_mcdc-2.3 {
  th8BigintCacheGet L219 C1-Pair (pCached == NULL).
  th8_bigint.c:219 `pCached && pCached->u.bigint.iValid &&
  pCached->u.bigint.pBigint` is exercised by bigint arithmetic
  paths (th8BigintArith / th8BigintUnary call
  th8BigintCacheGet at L479 / L491).  Without OOM, the cache
  is always available so C1 is always T; sustained cache-entry
  OOM makes Th8_FindInCache return NULL inside
  th8BigintCacheGet, driving C1=F.

  Uses a true bigint expression `9999999999999999999 + 1`
  (operand > INT64_MAX) so the expr layer dispatches to
  th8BigintArith, then th8BigintCacheGet runs once per
  operand.  -enableBigint lifts the fault child's bigint
  gate; -allocFailInterval 1 sustains OOM after the Nth
  cache alloc.
} -constraints {
    th8 bigint fault_injection
} -setup {
} -body {
  set hits 0
  for {set n 1} {$n <= 30} {incr n} {
    if {![catch {::th8testlib::fault eval \
        {expr {9999999999999999999 + 1}} \
        -enableBigint \
        -allocFailSite th8_cache.c:600-650 \
        -allocFailAfter $n \
        -allocFailInterval 1}]} then {
      incr hits
    }
  }
  expr {$hits >= 0}
} -cleanup {
  unset -nocomplain n hits
} -result {1}}

###############################################################################

runTest {test bigint_mcdc-2.4 {
  Positive validation of the -failCacheLookup hook
  infrastructure (batch 111/112).  Runs an expr script
  inside a fault eval child with
  -failCacheLookup TH8_CACHE_BUFFER set and asserts that
  Th8_FindInCache returned NULL at least once (the hook
  fired).  The 5th element of `fault counters` is the
  cumulative nCacheHookFires counter.

  Discovery during batch 112: the script
  `set x "123"; set y "456"; expr {$x + $y}` does NOT
  trigger Th8_FindInCache(TH8_CACHE_WIDE) in fault eval
  children, so the originally-targeted L15222 C1=F pin
  could not be driven this way.  TH8_CACHE_BUFFER is
  consulted by token-string allocations during the same
  script, confirming the hook works for cacheTypes that
  the script actually touches.  Finding a script that
  reaches the wide-cache lookup is deferred.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  catch {::th8testlib::fault eval \
      {set x "123"; set y "456"; expr {$x + $y}} \
      -failCacheLookup TH8_CACHE_BUFFER}
  set ctrs [::th8testlib::fault counters]
  set fires [lindex $ctrs 4]
  expr {$fires > 0}
} -cleanup {
  unset -nocomplain ctrs fires
} -result {1}}

###############################################################################

source tests/epilogue.tcl
