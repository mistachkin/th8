###############################################################################
#
# coverage_crlf_translate.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_plat.c th8TranslateCrLfToLf
# L809:
#
#   if (z[r] == '\r' && r + 1 < n && z[r + 1] == '\n')
#
# The C2-Pair (bare CR at the final byte: r + 1 == n) and
# C3-Pair (CR followed by some byte that is NOT '\n') were
# both unreached because every existing source-able .th8
# file in the test suite uses Unix LF endings.  When the
# TH8_TRANSLATE_EOL flag is set on Th8_GetData, the contents
# pass through th8TranslateCrLfToLf, but with no bare CR
# bytes anywhere the inner if at L809 short-circuits at C1
# every time.
#
# Driver: source `tests/helpers/crlf_test.th8` which contains
# two bare CR bytes -- one between commands ("ok\r"more...)
# and one at the file's final byte.  The first drives
# (T,T,F) (CR followed by 'm', not LF) and the second drives
# (T,F,-) (CR at the last byte position).  The source attempt
# itself errors at the parser level (bare CR is interpreted
# as a command separator producing "wrong # args" for the
# trailing fragment) which the test catches and ignores --
# the goal is to exercise the translation pass, not produce
# a meaningful interpretation.
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

runTest {test crlftrans-1.1 {
  Sourcing a file with bare CR bytes (one mid-file, one at
  EOF) drives th8TranslateCrLfToLf L809 C2-pair (T,F,-)
  and C3-pair (T,T,F).  The source itself produces a parse
  error which we catch; the value of the test is the
  translation-pass coverage, not the script result.
} -constraints {
    th8
} -setup {
} -body {
  set rc [catch {source tests/helpers/crlf_test.th8}]
  expr {$rc == 1}
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain ::th8t_crlf_ok
} -result {1}}

###############################################################################

source tests/epilogue.tcl
