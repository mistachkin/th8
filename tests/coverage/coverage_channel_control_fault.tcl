###############################################################################
#
# coverage_channel_control_fault.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the pt_xChannelControl WRITE and OPEN arms in
# th8_fault.c:508 and :511 via the dedicated testlib helper
# ::th8testlib::pt_chanctl_fault.  The helper installs the
# fault layer on the calling interp with bFailChannelOpen
# and bFailChannelWrite set, then calls pPlat->xChannelControl
# with op=OPEN and op=WRITE so the fault interceptor fires.
# Without this path the entire pt_xChannelControl function
# has zero execution count (no in-tree caller invokes the
# WRITE fallback or OPEN via xChannelControl).
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

runTest {test chanctl_fault-1.1 {
  Drive pt_xChannelControl OPEN and WRITE arms via the
  dedicated ::th8testlib::pt_chanctl_fault helper.  The
  helper installs the fault layer, invokes both ops, then
  uninstalls cleanly.  Returns "ok" on success.
} -constraints {
    th8 fault_injection
} -body {
  ::th8testlib::pt_chanctl_fault
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
