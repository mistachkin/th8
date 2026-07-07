###############################################################################
#
# coverage_dict_with_oom.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the L4345 3-condition compound
# `if (aazLevel && aanLevel && anCountLvl)` in
# plugins/th8_lists.c::dict_with_command's nested rebuild
# path.  Only the success vector (T,T,T) is seen via the
# normal test suite; failing each of the three preceding
# TH8_ALLOC_MULs (L4338, L4340, L4342) in turn drives
# (F,-,-), (T,F,-), and (T,T,F) respectively.
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

runTest {test dict_with_oom-1.1 {
  OOM-fault each of the three TH8_ALLOC_MUL calls at
  th8_lists.c L4338, L4340, L4342 in turn.  Each failed
  alloc leaves one or more of {aazLevel, aanLevel,
  anCountLvl} NULL, exercising a different pair of the
  L4345 compound check.  catch wraps the eval so an
  "out of memory" failure is captured cleanly.
} -constraints {
    th8 fault_injection
} -body {
  set rcs {}
  foreach idx {1 2 3} {
    set rc [catch {::th8testlib::fault eval {
      set ::dw {k1 {k2 v}}
      catch {dict with ::dw k1 {}}
    } -allocFailSite th8_lists.c:4338-4343 -allocFailAfter $idx} r]
    lappend rcs [expr {$rc == 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r idx ::dw
} -result {1 1 1}}

###############################################################################

source tests/epilogue.tcl
