###############################################################################
#
# coverage_null_guards.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Tests targeting MC/DC condition coverage on public-API entry NULL guards
# (the "if (!arg1 || !arg2) return TH8_ERROR;" pattern).  Each test
# exercises the three input combinations needed to satisfy MC/DC for a
# typical two-operand short-circuit OR guard:
#
#     Test 1: arg1 = NULL              (operand 1 short-circuits)
#     Test 2: arg1 valid, arg2 = NULL  (operand 1 false, operand 2 true)
#     Test 3: both valid               (success path)
#
# Each ::th8testlib::null_guard SUBCMD invocation runs all three calls
# in C and reports "ok" on the expected outcome triple, or a failure
# diagnostic.
#
# These are coverage-driven tests; R-markers are NOT used because the
# tests target implementation-internal MC/DC conditions, not normative
# requirements (see test_generation_failure_modes.md §0.2 for the
# coverage-vs-conformance distinction).
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
# Section 1 -- Public-API entry NULL guards
#
###############################################################################

#
# null_guard-1.1 must run before any other breakpoint-touching test
# in this file: it exercises the !interp->paBreakpoints branch of
# Th8_ClearBreakpoint, which is only reachable when no breakpoint
# has ever been registered on the interp (paBreakpoints is lazily
# allocated and stays non-NULL once initialized).
#
runTest {test null_guard-1.1 {
  Th8_ClearBreakpoint NULL guard MC/DC sweep (paBreakpoints initially NULL)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard clear_breakpoint
} -result {ok}}

###############################################################################

runTest {test null_guard-1.2 {
  Th8_QueueEvent NULL guard MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard queue_event
} -result {ok}}

###############################################################################

runTest {test null_guard-1.3 {
  Th8_IterateArraySearches NULL guard MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard iterate_array
} -result {ok}}

###############################################################################

runTest {test null_guard-1.4 {
  Th8_SetBreakpoint NULL/range guard MC/DC sweep (3 operands)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard set_breakpoint
} -result {ok}}

###############################################################################

runTest {test null_guard-1.5 {
  Th8_ListAppendBreakpoints NULL guard MC/DC sweep (3 operands)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard list_append_breakpoints
} -result {ok}}

###############################################################################

runTest {test null_guard-1.6 {
  Th8_ListAppendExpansions NULL guard MC/DC sweep (3 operands)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard list_append_expansions
} -result {ok}}

###############################################################################

runTest {test null_guard-1.7 {
  Th8_MergePlatformInterp NULL guard MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard merge_platform
} -result {ok}}

###############################################################################

runTest {test null_guard-1.8 {
  Th8_SetPlatformContext NULL guard MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard set_platform_ctx
} -result {ok}}

###############################################################################

runTest {test null_guard-1.9 {
  Th8_GetPlatformContext NULL guard MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard get_platform_ctx
} -result {ok}}

###############################################################################
#
# Section 2 -- Public-API entry boundary-value (size) guards
#
# Pattern: if (nByte == 0 || nByte > TH8_MX_ALLOC) ... return NULL;
# Each test exercises three sizes -- 0, TH8_MX_ALLOC+1, and a small
# valid size -- to satisfy MC/DC for both operands of the OR.
#
###############################################################################

runTest {test null_guard-2.1 {
  Th8_SafeAlloc boundary-value MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard alloc_safe
} -result {ok}}

###############################################################################

runTest {test null_guard-2.2 {
  Th8_SafeRealloc boundary-value MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard realloc_safe
} -result {ok}}

###############################################################################

runTest {test null_guard-2.3 {
  Th8_SafeAttemptRealloc boundary-value MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard realloc_safe_attempt
} -result {ok}}

###############################################################################
#
# Section 3 -- Mem* utility entry guards (in th8_plat.c)
#
###############################################################################

runTest {test null_guard-3.1 {
  Th8_Memcmp NULL-operand MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard memcmp
} -result {ok}}

###############################################################################

runTest {test null_guard-3.2 {
  Th8_Memcpy n==0 / NULL-operand MC/DC sweep (3 operands)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard memcpy
} -result {ok}}

###############################################################################

runTest {test null_guard-3.3 {
  Th8_Memset n==0 / NULL-operand MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard memset
} -result {ok}}

###############################################################################

runTest {test null_guard-3.4 {
  Th8_FindInCache NULL guard MC/DC sweep
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard find_in_cache
} -result {ok}}

###############################################################################

runTest {test null_guard-3.5 {
  Th8_EvalFile NULL guard MC/DC sweep (NULL paths only;
  success exercised by every [source] in the suite)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard eval_file
} -result {ok}}

###############################################################################

runTest {test null_guard-3.6 {
  Th8_FaultInstall (3-op) + Th8_FaultUninstall (2-op) MC/DC sweep
} -constraints {
    th8 fault_injection
} -body {
  ::th8testlib::null_guard fault
} -result {ok}}

###############################################################################

runTest {test null_guard-3.7 {
  Th8_HarpySigLoad NULL guard MC/DC sweep (3 operands)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard harpy_sig_load
} -result {ok}}

###############################################################################

runTest {test null_guard-3.8 {
  Th8_RsaKey* family NULL/private guards MC/DC sweep
  (PrivExp/Prime1/Prime2 + Token; full 3-input coverage)
} -constraints {
    th8 crypto_enabled test_key
} -body {
  ::th8testlib::null_guard rsa_getters
} -result {ok}}

###############################################################################

runTest {test null_guard-3.9 {
  Th8_PolicyPreloadKey NULL guard MC/DC sweep
} -constraints {
    th8 crypto_enabled test_key
} -body {
  ::th8testlib::null_guard policy_preload
} -result {ok}}

###############################################################################

runTest {test null_guard-3.10 {
  Th8_EvalFileAsData NULL guard MC/DC sweep (4 operands)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard eval_file_as_data
} -result {ok}}

###############################################################################

runTest {test null_guard-3.11 {
  Th8_PolicyFindKey NULL guard MC/DC partial sweep (operands 1, 3)
} -constraints {
    th8 crypto_enabled test_key
} -body {
  ::th8testlib::null_guard policy_find_key
} -result {ok}}

###############################################################################

runTest {test null_guard-3.12 {
  Th8_NsEval / Th8_Thaw / Th8_IsCanceled / Th8_ResetCancel
  single-arg NULL guards (operand-1 sweep)
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard misc_singlearg
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
