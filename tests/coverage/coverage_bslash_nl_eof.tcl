###############################################################################
#
# coverage_bslash_nl_eof.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c th8NextSpace L10249-10251:
#
#   while (i < nInput
#           && (zInput[i] == ' '
#               || zInput[i] == '\t')) {
#       i++;
#   }
#
# The C1-Pair (i >= nInput) was unreached: every existing
# script ended with at least one byte (\n / `}` / etc.) after
# the backslash-newline continuation, so after `i += 2` the
# inner while always found at least one more byte to inspect.
#
# Driver: an `eval` of a string that ends EXACTLY at the
# backslash-newline -- no trailing whitespace, no closing
# brace.  After th8NextSpace consumes the `\<LF>` it advances
# i to nInput and the inner whitespace-skip while-loop
# immediately fails C1 (i < nInput).  The script body
# itself ("set ::r 42") evaluates normally.
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

runTest {test bslnleof-1.1 {
  Eval of a script string ending with backslash-newline at
  EOF drives th8NextSpace L10249 C1=F: after `i += 2`, i
  equals nInput so the trailing-space skip-loop fails the
  bounds check before evaluating either C2 (== ' ') or C3
  (== '\t').
} -constraints {
    th8
} -setup {
} -body {
  set s "set ::r 99\\\n"
  eval $s
  set ::r
} -cleanup {
  unset -nocomplain ::r
  unset -nocomplain s
} -result {99}}

###############################################################################

source tests/epilogue.tcl
