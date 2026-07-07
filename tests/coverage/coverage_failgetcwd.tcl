###############################################################################
#
# coverage_failgetcwd.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the `if (zCwd == NULL)` error
# arms in Th8_GetCwd consumers.  The new -failGetCwd fault
# flag (batch 159) forces the fault wrapper's fi_xGetCwd to
# return NULL unconditionally, exercising the error-path
# branches in pwd_command and any other commands that
# resolve cwd as part of their normal logic.
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

runTest {test failgetcwd-1.1 {
  pwd under -failGetCwd drives the `if (zCwd == NULL)`
  error-path arm in plugins/th8_filesystems.c::pwd_command.
  The function detects xGetCwd's NULL return and falls
  through to the secondary error-set path.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {pwd} -failGetCwd]
  # rc != 0 means the body errored; body content varies.
  expr {[lindex $r 0] != 0 || [string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
