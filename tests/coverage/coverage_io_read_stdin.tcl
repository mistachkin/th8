###############################################################################
#
# coverage_io_read_stdin.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/th8_io.c read_command stdin
# path.  The plain conformance suite only exercises [read]
# against registered temp-file channels; the bulk-read-from-
# stdin codepath (lines 762-803) is therefore never hit and
# every decision in it shows 0% MC/DC.
#
#   L774  `rc != TH8_OK || !zLine || nLine == 0`  (read loop
#         break condition)
#   L783  `numChars >= 0 && (th8_int64_t)nAll >= numChars`
#         (per-iter character limit)
#   L790  `numChars >= 0 && nAll > (size_t)numChars`
#         (post-loop truncation)
#   L795  `bNoNewline && nAll > 0 && zAll[nAll - 1] == '\n'`
#         (-nonewline trailing-newline strip)
#
# All three forms of [read] are exercised via subprocess
# stdin pipes -- the only way to make Th8_Input return real
# data from the conformance suite.
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

runTest {test io_read_stdin-1.1 {
  [read stdin] reads ALL of stdin until EOF (Th8_Input
  returns rc != TH8_OK), exercising the L774 (T,*,*) break
  vector.  No numChars limit so L783 stays at (F,-) and
  L790 stays at (F,-).
} -constraints {
    test_only_exec
} -body {
  test_only_exec tests/helpers/read_stdin_all.tcl \
      << "alpha beta gamma"
} -result {alpha beta gamma}}

###############################################################################

runTest {test io_read_stdin-1.2 {
  [read stdin 3] caps the read at 3 characters, driving
  L783 (T,T) -- per-iter limit reached -- and L790 (T,T)
  -- post-loop truncation.
} -constraints {
    test_only_exec
} -body {
  test_only_exec tests/helpers/read_stdin_n.tcl \
      << "hello world\n"
} -result {hel}}

###############################################################################

runTest {test io_read_stdin-1.2b {
  [read stdin 100] with only a few bytes of input drives
  the C2=F vectors at src/plugins/th8_io.c L783 and L790
  -- numChars >= 0 (T) but nAll < numChars (F).  The
  read loop hits EOF before reaching the limit and the
  post-loop truncation is skipped.
} -constraints {
    test_only_exec
} -body {
  test_only_exec tests/helpers/read_stdin_n100.tcl \
      << "hi"
} -result {hi}}

###############################################################################

runTest {test io_read_stdin-1.3 {
  [read -nonewline stdin] reads all and strips a trailing
  '\n', driving L795 (T,T,T) -- bNoNewline AND non-empty
  AND trailing '\n'.
} -constraints {
    test_only_exec
} -body {
  test_only_exec tests/helpers/read_stdin_nonewline.tcl \
      << "abc\n"
} -result {abc}}

###############################################################################

runTest {test io_read_stdin-1.3b {
  [read -nonewline stdin] with EMPTY stdin drives src/
  plugins/th8_io.c L795 C2-pair vector (T, F, -) --
  bNoNewline is T but nAll is 0, so the trailing-newline
  strip is skipped without entering the body.
} -constraints {
    test_only_exec
} -body {
  test_only_exec tests/helpers/read_stdin_nonewline.tcl \
      << ""
} -result {}}

###############################################################################

runTest {test io_read_stdin-1.3c {
  [read -nonewline stdin] with content that does NOT end
  in a newline drives src/plugins/th8_io.c L795 C3-pair
  vector (T, T, F) -- bNoNewline=T, nAll>0=T, but the
  last byte is not '\n' so the strip branch falls
  through and the raw content (without truncation) is
  returned.
} -constraints {
    test_only_exec
} -body {
  test_only_exec tests/helpers/read_stdin_nonewline.tcl \
      << "abc"
} -result {abc}}

###############################################################################

source tests/epilogue.tcl
