###############################################################################
#
# coverage_read_opts.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [read] option-parsing in
# src/plugins/th8_io.c, focusing on the missing C3-pair at:
#
#   :663  argl[1] == 10 && argv[1][0] == '-' && memcmp(...,"-nonewline",10) == 0
#                 ([read] -nonewline option detect)
#                 missing C3-pair: 10-char option that
#                 starts with '-' but is NOT "-nonewline"
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

runTest {test rdopts-1.1 {
  read with a 10-character option that starts with '-' but
  is NOT "-nonewline" drives the (T, T, F) vector at
  th8_io.c:663.  After the failed match, the parser falls
  through to the argc==3 form where the supposed numChars
  arg fails integer parsing -- so the test catches the
  resulting error.
} -constraints {
    th8
} -body {
  set rc [catch {read -otherspec channelnamearg} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
