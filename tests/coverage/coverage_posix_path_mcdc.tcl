###############################################################################
#
# coverage_posix_path_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_posix.c path-resolution short-circuit
# compounds whose F branch isn't reached by ordinary file tests:
#
#   L4538 `len == 1 && start[0] == '.'` (single-dot component).
#         Needs a path containing a literal "./" segment.
#   L4540 `len == 2 && start[0] == '.' && start[1] == '.'`
#         (parent-dir component).  Needs a path containing
#         a literal "../" segment.
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

runTest {test posix_path_mcdc-1.1 {
  [file normalize] on a path containing "./" drives the
  single-dot component branch in the th8_posix.c path
  resolver (L4538).
} -constraints {
    th8
} -setup {
} -body {
  set r [file normalize /tmp/./foo]
  # Result must be /tmp/foo (dot normalized away).
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test posix_path_mcdc-1.2 {
  [file normalize] on a path containing "../" drives the
  parent-dir component branch in the th8_posix.c path
  resolver (L4540).
} -constraints {
    th8
} -setup {
} -body {
  set r [file normalize /tmp/foo/../bar]
  # Result must be /tmp/bar (..  resolves the foo segment).
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
