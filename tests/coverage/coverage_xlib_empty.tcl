###############################################################################
#
# coverage_xlib_empty.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_xlib.c L400 Th8_EvalFileAsData:
# `NEVER(!zResult) || nResult == 0`.  Under
# TH8_OMIT_AUXILIARY_SAFETY_CHECKS the NEVER folds to
# constant false, leaving `nResult == 0` as the only live
# condition.  Most callers of Th8_EvalFileAsData pass
# scripts whose result is non-empty (a base64 blob), so the
# C2 true vector ({C,T}: empty result) is not driven by
# normal flows.
#
# tests/helpers/evalfile_empty.tcl returns the empty string;
# routing it through ::th8testlib::load_key_file drives the
# {C,T} vector and produces the expected error code (1).
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

runTest {test xlib_empty-1.1 {
  Drive th8_xlib.c L400's C2-Pair ({C,T}: empty result).
  th8testlib::load_key_file runs the signed helper that
  returns "" -- Th8_EvalFileAsData sees nResult==0 and
  errors out cleanly, returning 1 (failure).
} -constraints {
    th8 loadLib
} -body {
  ::th8testlib::load_key_file tests/helpers/evalfile_empty.tcl
} -result {1}}

###############################################################################

source tests/epilogue.tcl
