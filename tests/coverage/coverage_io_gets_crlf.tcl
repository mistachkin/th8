###############################################################################
#
# coverage_io_gets_crlf.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/th8_io.c L125
# (`nData > 0 && zData[nData - 1] == '\r'`).  The gets()
# helper strips trailing '\n' first, then checks for a
# preceding '\r' to handle CRLF line endings on Windows-
# style input.  Ordinary stdin tests feed bare-LF data, so
# the (T,T) vector is never executed.
#
# This coverage test feeds an explicit CRLF input via
# subprocess stdin and lets gets strip both bytes.
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

runTest {test io_gets_crlf-1.1 {
  [gets stdin] on a line terminated with "\r\n" strips both
  the '\n' (L123) and the preceding '\r' (L125 (T,T)).  The
  helper prints the cleaned line; we expect "hello" without
  any trailing CR.
} -constraints {
    gets test_only_exec
} -body {
  test_only_exec tests/helpers/gets_novar.tcl \
      << "hello\r\n"
} -result {hello}}

###############################################################################

source tests/epilogue.tcl
