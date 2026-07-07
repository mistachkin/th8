###############################################################################
#
# coverage_afmapget_nocreate_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L393
# in `th8AfMapGet`:
#
#   if (bCreate && p->n < AF_MAX_KEYS) { ... }
#
# 2-cond AND.  Both in-tree callers (th8AttrFlagsChange and
# Th8_AttrFlagsHave) pass bCreate=1 so C1 stayed always T
# and C1-Pair was open at 50% MC/DC.  New
# ::th8testlib::afmapget_nocreate helper invokes the
# function via the th8_AfMapGet internal-stubs entry with
# an empty stack-allocated map and bCreate=0, driving
# C1=F and returning NULL.
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

runTest {test afmapget_nocreate-1.1 {
  th8_attrflags.c L393 (F,-) -- th8AfMapGet with empty
  map and bCreate=0 short-circuits at C1=F, returns NULL.
  Helper returns 1 (signalling NULL outcome).
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  th8testlib::afmapget_nocreate
} -cleanup {
} -result {1}}

###############################################################################

source tests/epilogue.tcl
