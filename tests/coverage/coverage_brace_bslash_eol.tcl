###############################################################################
#
# coverage_brace_bslash_eol.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c L12417 -- the post
# backslash-newline whitespace-consumption loop inside the
# brace-word substitution branch:
#
#   while (i .lt. nn - 1
#         && (pBuild->zWord[i] == ' '
#         || pBuild->zWord[i] == '\t')) {
#       i++;
#   }
#
# Existing brace-word tests put backslash-newline in the MIDDLE
# of a brace-quoted word, so after consuming the backslash-
# newline (i+=2) the inner while loop has at least one trailing
# byte to test for space/tab.  The C1-Pair vector (F,-,-),
# where i has advanced exactly to nn-1 (the closing brace)
# immediately after the backslash-newline consumption, is not
# driven.
#
# The eval-driven input builds a 5-char input word with bytes
# open-brace, x, backslash, newline, close-brace.  Inside the
# loop the backslash-newline is detected at i=2, the inner
# while-loop runs with i=4 vs nn-1=4 (false) -- closes the
# C1-Pair.
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

runTest {test bslash_eol-1.1 {
  brace-word with backslash-newline as the LAST two bytes
  inside the braces drives th8_core.c L12417 C1-Pair.
} -constraints {
    th8
} -body {
  eval "set s {x\\\n}"
  list [string length $s] [scan [string index $s 1] %c]
} -cleanup {
  unset -nocomplain s
} -result {2 32}}

###############################################################################

runTest {test bslash_eol-1.2 {
  brace-word with longer prefix and trailing backslash-newline.
} -constraints {
    th8
} -body {
  eval "set s {hello\\\n}"
  string length $s
} -cleanup {
  unset -nocomplain s
} -result {6}}

###############################################################################

source tests/epilogue.tcl
