###############################################################################
#
# coverage_plugin_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_plugin.c.  Th8_RegisterPlugin and
# Th8_UnregisterPlugin each contain short-circuit guard compounds
# whose false vectors are never reached during ordinary interp
# startup -- valid arguments fall through every NULL guard, no
# duplicate registration occurs, and no plugin is ever
# unregistered.  This file delegates to the testlib helper
# ::th8testlib::null_guard plugin which performs the full sweep
# on a private child interpreter (so the test harness's plugin
# list is not perturbed).
#
# Drives the following decisions in src/th8_plugin.c:
#   line  63   th8PluginFind inner compound (C1=T,C2=T match).
#   line 117   Th8_RegisterPlugin 3-operand NULL guard.
#   line 127   Th8_RegisterPlugin duplicate-name branch.
#   line 135   Th8_RegisterPlugin probe-fail / 0-command OR.
#   line 158   Th8_RegisterPlugin second-call failure.
#   line 233   Th8_UnregisterPlugin 2-operand NULL guard.
#   line 247   Th8_UnregisterPlugin inline search compound
#              (T,T match and T,F same-length mismatch).
#   line 254   Th8_UnregisterPlugin "not found" branch.
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

runTest {test plugin_mcdc-1.1 {
  ::th8testlib::null_guard plugin runs the plugin-API sweep
  on a private child interpreter and returns "ok".  This test
  drives every short-circuit compound in src/th8_plugin.c that
  is not reached by ordinary interpreter startup.
} -constraints {
    th8
} -body {
  ::th8testlib::null_guard plugin
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
