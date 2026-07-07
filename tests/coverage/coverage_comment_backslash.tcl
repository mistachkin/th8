###############################################################################
#
# coverage_comment_backslash.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the backslash-escape handler in the comment-parsing
# loop at th8_core.c:19473 -- `if (*z == '\\' && n > 1)`.
# Three sub-vectors:
#
#   (T,T): comment contains `\X` where X is any non-newline byte
#   (T,F): comment ends with a trailing `\` (n == 1 at this point)
#   (F,-): comment contains no backslash (covered by existing tests)
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

runTest {test cmtbslash-1.1 {
  Comment with embedded backslash-char drives th8_core.c:19473
  (T,T) vector -- the parser sees `\` followed by any non-
  newline byte and advances past the escape sequence.  The
  embedded script (`eval` body) is a comment line followed
  by a real command; if the comment scan misbehaves the
  command does not execute.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [eval "# alpha\\beta gamma\nlist 1 2 3"]
  lappend rcs [eval "# x\\y\\z\nlist a b"]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {{1 2 3} {a b}}}

###############################################################################

runTest {test cmtbslash-1.2 {
  Comment ending in trailing backslash drives th8_core.c:19473
  (T,F) vector -- the parser sees `\` as the LAST byte before
  end-of-input (n == 1), so the look-ahead skip is suppressed.
  The script just contains a comment with no trailing command;
  parsing should complete cleanly.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {eval "# trailing\\"} m]
  lappend rcs [catch {eval "# more text\\"} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 0}}

###############################################################################

source tests/epilogue.tcl
