###############################################################################
#
# coverage_flag_set_highbit_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for the (T,_)->(F,_) C1-Pair vectors at:
#
#   src/plugins/harpy/th8_attrflags.c
#     L243: th8AfFlagSetAdd
#       if ((unsigned char)c < 128 && !p->present[c]) { ... }
#     L278: th8AfFlagSetRemove
#       if ((unsigned char)c < 128 && p->present[c]) { ... }
#
# 2-condition AND each.  Pre-test report shows C2-Pair
# covered (the th8AfIsIdentChar-filtered ASCII chars produce
# both (T,T) and (T,F)) but C1-Pair was open because every
# script-level caller went through th8AfIsIdentChar which
# rejects c >= 128 before invoking these helpers.
#
# The new ::th8testlib::flagsethighbit helper invokes them
# directly with c = 0xC3 (195) through the
# th8_AfFlagSetAdd / th8_AfFlagSetRemove internal-stubs
# entries, driving C1=F.
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

runTest {test flagsethighbit-1.1 {
  th8_attrflags.c L243 (F,-) -- th8AfFlagSetAdd with
  c=0xC3 short-circuits at C1=F.  Verifies the present[]
  slot for that char remains 0 (body did not execute).
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  th8testlib::flagsethighbit add
} -cleanup {
} -result {0}}

###############################################################################

runTest {test flagsethighbit-1.2 {
  th8_attrflags.c L278 (F,-) -- th8AfFlagSetRemove with
  c=0xC3 short-circuits at C1=F.  Same expectation.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  th8testlib::flagsethighbit remove
} -cleanup {
} -result {0}}

###############################################################################

source tests/epilogue.tcl
