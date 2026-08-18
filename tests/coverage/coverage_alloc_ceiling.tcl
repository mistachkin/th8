###############################################################################
#
# coverage_alloc_ceiling.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# TH8K-023: the per-interpreter memory limit (Th8_SetAllocLimit) is a real
# ceiling that the second-chance xNeedMemory path cannot punch through, and a
# legitimate cache-clear recovery still works.
# ::th8testlib::alloc_ceiling runs two checks in child interpreters and returns
# {bypass over recover}: bypass==0 (a 1 MiB request against a 4 KiB headroom is
# rejected, not satisfied past the limit by the second chance), over==0 (the
# accounting never crosses the ceiling), and recover==1 (with no headroom, the
# second chance clears the IR cache and the retry through the one limit-checked
# core now fits and succeeds).  Pre-fix the built-in xNeedMemory retried a raw
# xMalloc with no limit check, so bypass/over were 1; the result must be
# {0 0 1}.
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

runTest {test alloc_ceiling-1.1 {
  The per-interpreter memory limit is a hard ceiling the xNeedMemory second
  chance cannot bypass (bypass==0, over==0), and a cache-clear recovery still
  satisfies a now-fitting request (recover==1).  The result must be {0 0 1}
  (TH8K-023).
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::alloc_ceiling]
} -cleanup {
  unset -nocomplain r
} -result {0 0 1}}

###############################################################################

source tests/epilogue.tcl
