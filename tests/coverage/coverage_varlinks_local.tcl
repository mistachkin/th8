###############################################################################
#
# coverage_varlinks_local.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_vars.c L2050:
#   `if (pVar && pVar->nRef > 1)` inside th8AppendLinkedHashKeys.
#
# Existing infovarlinks-1.2 / 1.3 (tests/newcmds.tcl) only ever put
# ONE variable in the proc's frame -- the linked one -- so the
# hash iterator never sees an entry with nRef == 1.  That leaves
# the C2-pair (T,T) hit but (T,F) untouched.
#
# Drive (T,F) by creating a proc that has BOTH a local variable
# (nRef==1) AND a linked variable (nRef>1), then call
# `info varlinks` inside it.  The result still only contains the
# linked variable -- the local one is filtered by the L2050
# compound.  Both vectors fire on the same iteration.
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

runTest {test varlinks_local-1.1 {
  proc with both a local variable and an upvar-linked variable:
  [info varlinks] must return only the linked variable.  The
  iterator visits both entries; the L2050 compound drives (T,T)
  for the linked entry and (T,F) for each local entry.
} -constraints {
    th8
} -setup {
} -body {
  set ::vll_outer 1
  proc _vll_mixed {} {
    upvar 1 ::vll_outer linked
    set local_a 1
    set local_b "two"
    info varlinks
  }
  set r [_vll_mixed]
  rename _vll_mixed {}
  set r
} -cleanup {
  unset -nocomplain ::vll_outer r
} -result {linked}}

###############################################################################

runTest {test varlinks_local-1.2 {
  proc with [global]-linked variable plus locals: [info varlinks]
  returns only the global-linked variable.  Same C2-pair coverage
  via the `global` linkage flavor (instead of upvar).
} -constraints {
    th8
} -setup {
} -body {
  set ::vll_g 7
  proc _vll_g {} {
    global vll_g
    set scratch_x 1
    set scratch_y 2
    info varlinks
  }
  set r [_vll_g]
  rename _vll_g {}
  set r
} -cleanup {
  unset -nocomplain ::vll_g r
} -result {vll_g}}

###############################################################################

source tests/epilogue.tcl
