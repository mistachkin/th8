###############################################################################
#
# coverage_proc_unsupported.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for plugins/th8_procedures.c L1004
# `nCmd >= 2 && zCmd[0] == ':' && zCmd[1] == ':'` (the
# leading-"::"-skip in the "unsupported argument" error
# message used by nproc).  Existing tests in tests/lambda.tcl
# only drive (T,T,T) (proc names starting with "::") and
# (T,T,F) via "nptest" (single-cond is not '::').  This test
# drives the (F,-,-) C1=F vector via a 1-char proc name.
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

runTest {test proc_unsup-1.1 {
  Single-char nproc name with an unsupported argument
  drives the L1004 C1=F vector (nCmd < 2 means leading
  "::" check is skipped, error message uses raw name).
} -constraints {
    th8 nproc
} -body {
  nproc x {{-a 1}} {set a}
  catch {x -unknown 1} m
  string match {*procedure*\"x\"*} $m
} -cleanup {
  catch {rename x ""}
  unset -nocomplain m
} -result {1}}

###############################################################################

source tests/epilogue.tcl
