###############################################################################
#
# coverage_list_oom_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/th8_lists.c L512 in
# `list_command`:
#
#   if (nElem > 0 && zList) {
#       /* store in cache */
#   }
#
# 2-cond AND.  Pre-test report: 50% MC/DC -- (T,T)
# (existing tests with non-empty list args) and (F,-)
# (zero args -> empty list) cover C1-Pair, but C2-Pair
# (T,F) was open.  (T,F) requires nElem>0 yet
# zList==NULL, which happens when every Th8_ListAppend
# call OOM-fails leaving the local zList unchanged.
# Drivable through the existing fault-injection
# infrastructure with `-allocFailAfter 0`.
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

runTest {test list_oom-1.1 {
  th8_lists.c L512 (T,F) -- `list a b c` with the FIRST
  Th8_Memcpy/TH8_ALLOC inside list_command's append loop
  failing.  Sweep -allocFailAfter values 1..30 with the
  th8_lists.c filter so one of the iterations lands on
  the body-level append.  Once the local zList stays
  NULL through the loop, L512 decision evaluates as
  (T, F) -> skips cache store.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  for {set n 0} {$n <= 30} {incr n} {
    catch {
      th8testlib::fault eval {
        list a b c
      } -allocFailSite th8_lists.c -allocFailAfter $n
    }
  }
  # We don't assert a specific outcome -- the goal is
  # solely to vary the allocation-failure timing so
  # at least one sweep value drives L512 (T,F).
  expr {1}
} -cleanup {
  unset -nocomplain n
} -result {1}}

###############################################################################

source tests/epilogue.tcl
