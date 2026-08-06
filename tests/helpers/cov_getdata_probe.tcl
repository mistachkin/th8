###############################################################################
#
# cov_getdata_probe.tcl --
#
# Minimal signed file sourced by coverage_posix_fault_errno.tcl to
# exercise th8PosixGetData's read loop (the errno==EINTR retry arm)
# WITHOUT side effects.  Sourcing it drives the xGetData open/fstat/
# read path but executes nothing -- the body is intentionally empty --
# so a successful source cannot mutate interpreter/load/test state
# (unlike re-sourcing prologue.tcl, which re-runs initializeTests /
# detectLoadLib and marks the enclosing test DIRTY).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################
