###############################################################################
#
# coverage_invalid_platform.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# TH8K-001: Th8_CreateInterp must REJECT (return NULL for) an invalid platform
# table, not merely survive one.  ::th8testlib::invalid_platform_rejected
# constructs each of the ten invalid platforms th8ValidatePlatform guards
# against -- a NULL platform, a wrong nVersion, and each of the eight mandatory
# callbacks (xMalloc/xRealloc/xFree/xMemorySize/xMemcpy/xMemmove/xMemset/
# xMemcmp) nulled in turn -- and returns {nTested nRejected}.  Every case must
# be rejected; a regression that admitted a live interpreter for an invalid
# platform is caught here (the pre-existing plat_wrappers drive only deleted a
# non-NULL child without asserting NULL).
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

runTest {test invalid_platform-1.1 {
  Th8_CreateInterp rejects (returns NULL for) all ten invalid platform tables:
  NULL platform, wrong nVersion, and each of the eight mandatory callbacks
  nulled in turn.  {nTested nRejected} must be {10 10} (TH8K-001).
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::invalid_platform_rejected]
} -cleanup {
  unset -nocomplain r
} -result {10 10}}

###############################################################################

source tests/epilogue.tcl
