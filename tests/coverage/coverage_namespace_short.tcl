###############################################################################
#
# coverage_namespace_short.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the C1=F vector at th8_core.c:4230 in th8FindNamespace --
# `if (nName >= 2 && zName[0] == ':' && zName[1] == ':')`.  The
# existing tests cover the (T,F,-), (T,T,F), and (T,T,T) vectors;
# C1=F means the input is shorter than 2 bytes (empty or single-
# colon).  Both forms are safe to query via `namespace exists`
# which returns 0 for any non-existent namespace.
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

runTest {test nsshort-1.1 {
  `namespace exists ""` drives th8FindNamespace with
  nName=0 -- the (F,-,-) vector at th8_core.c:4230.
  TH8 maps the empty name to the current namespace
  (matches Tcl 8.x semantics); result is 1.
} -constraints {
    th8
} -body {
  namespace exists ""
} -result {1}}

###############################################################################

runTest {test nsshort-1.2 {
  `namespace exists ":"` drives th8FindNamespace with
  nName=1 -- also a (F,-,-) vector (nName < 2).
  Single-colon is not a recognized namespace prefix
  (only "::" is); returns 0.
} -constraints {
    th8
} -body {
  namespace exists ":"
} -result {0}}

###############################################################################

source tests/epilogue.tcl
