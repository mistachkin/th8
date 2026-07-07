###############################################################################
#
# coverage_flags_have_null_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L1025
# in Th8_AttrFlagsHave:
#
#   if (!zHave || nHave == 0) return 1; /* have-none */
#
# 2-condition OR.  Pre-test report: C2-Pair covered (the
# `flags have FLAGS ""` shape produces (F,T) plus the
# normal (F,F)), but C1-Pair (T,_) was open because the
# `flags have FLAGS HAVE` command always passes a Tcl
# string for HAVE -- never a C NULL pointer.  The new
# `::th8testlib::flagshavennull` helper invokes
# Th8_AttrFlagsHave directly with zHave=NULL.
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

runTest {test flagshavennull-1.1 {
  th8_attrflags.c L1025 (T,-) -- Th8_AttrFlagsHave called
  with zHave=NULL drives the C1=T short-circuit and returns
  1 (have-none vacuous success).
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  th8testlib::flagshavennull
} -cleanup {
} -result {1}}

###############################################################################

source tests/epilogue.tcl
