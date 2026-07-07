###############################################################################
#
# coverage_bug22_math_null.tcl --
#
# Tcl Language Standard
# Regression Test File for Bug 22
#
# Bug 22 was: math functions registered via Th8_CreateMathFunc and
# obtained via Th8_FindMathFunc could be called from direct-API
# callers with NULL operands.  Per-op procs (th8MathAbs et al.) used
# to deref zArg1 unconditionally -- SIGSEGV.  The partial fix added
# `if (!z1 || ...) return TH8_ERROR;` guards to the per-op procs.
# The shared dispatchers (th8MathTranscendental, th8MathClassify)
# kept the softer `if (z1 && ...)` form which avoided the crash but
# silently computed sin(0)/cos(0)/etc on a missing operand.
#
# 2026-06-07: full fix landed -- ctx encoding now packs the function's
# arity alongside the opcode (`(arity << 16) | opcode`), the shared
# dispatchers decode arity at entry and return TH8_ERROR when a
# required operand is NULL.  fpclassify (the only direct callback
# also receiving an opcode in ctx) gained an explicit NULL check at
# its entry.
#
# This test exercises the partial-fix probe loop in
# `::th8testlib::plat_wrappers` (which invokes 22 math procs with
# NULL operands and records any that don't return TH8_ERROR) and
# asserts the post-fix regression counter is zero.
#
# Coverage-driven; pinned to Bug 22 entry in incomplete.md.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test bug22-math-null-1.1 {
  After the 2026-06-07 full Bug 22 fix, every math proc reached
  via Th8_FindMathFunc returns TH8_ERROR when a required operand
  is NULL.  The probe loop inside ::th8testlib::plat_wrappers
  invokes 22 named math procs and increments
  th8test_bug22NullOpRegressions for any that returns OK with a
  NULL z1 (arity >= 1) or NULL z2 (arity == 2).  Post-fix the
  counter MUST be 0.
} -constraints {
    th8
} -body {
  ::th8testlib::plat_wrappers
  ::th8testlib::bug22_null_op_count
} -result {0}}

###############################################################################

source tests/epilogue.tcl
