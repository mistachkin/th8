###############################################################################
#
# safealloc.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the public safe-math and safe-allocation primitives:
#   Th8_SafeMul / Th8_SafeAdd
#   Th8_SafeAllocStrAdd  (TH8_ALLOC_STR_ADD)
#   Th8_SafeAllocStrMul  (TH8_ALLOC_STR_MUL)
#   Th8_SafeAllocMulAdd2 (TH8_ALLOC_MUL_ADD2)
#
# Each function is exercised on its overflow boundary and on a
# non-overflowing path.  Boundary inputs use symbolic tokens parsed
# by the testlib (max, max-1, half, halfp1, halfdiv2) since
# SIZE_MAX exceeds Tcl's signed wide-int range.
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
# Section 1 -- Th8_SafeMul
#
###############################################################################

runTest {test safealloc-1.1 {
  R-26323-64339: Th8_SafeMul overflow at SIZE_MAX/2 + 1 times 2
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safemul halfp1 2
} -result {overflow}}

###############################################################################

runTest {test safealloc-1.2 {
  R-26323-64339: Th8_SafeMul exact-fit boundary at SIZE_MAX/2 times 2
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safemul half 2
} -match glob -result {ok *}}

###############################################################################

runTest {test safealloc-1.3 {
  R-26323-64339: Th8_SafeMul times 0 produces 0 without overflow
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safemul max 0
} -result {ok 0}}

###############################################################################
#
# Section 2 -- Th8_SafeAdd
#
###############################################################################

runTest {test safealloc-2.1 {
  R-40857-48614: Th8_SafeAdd overflow at SIZE_MAX + 1
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeadd max 1
} -result {overflow}}

###############################################################################

runTest {test safealloc-2.2 {
  R-40857-48614: Th8_SafeAdd boundary at (SIZE_MAX - 1) + 1
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeadd max-1 1
} -match glob -result {ok *}}

###############################################################################
#
# Section 3 -- TH8_ALLOC_STR_ADD (Th8_SafeAllocStrAdd)
#
###############################################################################

runTest {test safealloc-3.1 {
  R-51492-41171: TH8_ALLOC_STR_ADD overflow on outer (k + n) step
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocstradd max 1
} -result {overflow}}

###############################################################################

runTest {test safealloc-3.2 {
  R-51492-41171: TH8_ALLOC_STR_ADD overflow on trailing (+ 1) step
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocstradd max-1 0
} -result {overflow}}

###############################################################################

runTest {test safealloc-3.3 {
  R-51492-41171: TH8_ALLOC_STR_ADD non-overflowing path returns ok
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocstradd 16 32
} -result {ok}}

###############################################################################
#
# Section 4 -- TH8_ALLOC_STR_MUL (Th8_SafeAllocStrMul)
#
###############################################################################

runTest {test safealloc-4.1 {
  R-41471-25493: TH8_ALLOC_STR_MUL overflow on (n * sz) step
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocstrmul max 4
} -result {overflow}}

###############################################################################

runTest {test safealloc-4.2 {
  R-41471-25493: TH8_ALLOC_STR_MUL overflow on trailing (+ sz) step
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocstrmul half 4
} -result {overflow}}

###############################################################################

runTest {test safealloc-4.3 {
  R-41471-25493: TH8_ALLOC_STR_MUL non-overflowing path returns ok
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocstrmul 100 4
} -result {ok}}

###############################################################################
#
# Section 5 -- TH8_ALLOC_MUL_ADD2 (Th8_SafeAllocMulAdd2)
#
###############################################################################

runTest {test safealloc-5.1 {
  R-55878-51290: TH8_ALLOC_MUL_ADD2 overflow on (a * b) step
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocmuladd2 max 2 0 0 0
} -result {overflow}}

###############################################################################

runTest {test safealloc-5.2 {
  R-55878-51290: TH8_ALLOC_MUL_ADD2 overflow on (c * d) step
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocmuladd2 1 1 max 2 0
} -result {overflow}}

###############################################################################

runTest {test safealloc-5.3 {
  R-55878-51290: TH8_ALLOC_MUL_ADD2 overflow on sum-of-products step
} -constraints {
    loadLib th8
} -body {
  # halfdiv2 = SIZE_MAX/4.  (halfdiv2 * 2) + (halfdiv2 * 2) =
  # SIZE_MAX/2 + SIZE_MAX/2 which fits, but
  # (halfdiv2 * 4) + (halfdiv2 * 4) overflows the sum.
  ::th8testlib::safeallocmuladd2 halfdiv2 4 halfdiv2 4 0
} -result {overflow}}

###############################################################################

runTest {test safealloc-5.4 {
  R-55878-51290: TH8_ALLOC_MUL_ADD2 overflow on trailing (+ e) step
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocmuladd2 1 max 0 0 1
} -result {overflow}}

###############################################################################

runTest {test safealloc-5.5 {
  R-55878-51290: TH8_ALLOC_MUL_ADD2 non-overflowing path returns ok
} -constraints {
    loadLib th8
} -body {
  ::th8testlib::safeallocmuladd2 100 8 100 8 256
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
