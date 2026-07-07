###############################################################################
#
# coverage_plat_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_plat.c.  Drives the platform-callback
# compounds at the public-API layer (Th8_GetCwd, Th8_SetCwd,
# Th8_GetEnv, Th8_KeyValue, Th8_Sleep, ...) whose C2=F vector is
# never reached from script because the higher-level plugin
# command checks the platform slot first and bails out before
# calling the public API.
#
# The companion testlib helper ::th8testlib::null_guard plat
# installs the fault layer (with -nullCallbacks for the relevant
# slots) on a private child interpreter and then calls each
# public API from C, hitting the slot=NULL branch at the
# plat-layer decision.  It also drives the public-API NULL-arg
# guards on Th8_SetPlatformContext, Th8_GetPlatformContext, and
# Th8_EmitTrace.
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

runTest {test plat_mcdc-1.1 {
  ::th8testlib::null_guard plat exercises the platform-layer
  NULL-callback and NULL-arg compounds in src/th8_plat.c on
  a private child interpreter and returns "ok".
} -constraints {
    th8 fault_injection
} -body {
  ::th8testlib::null_guard plat
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
