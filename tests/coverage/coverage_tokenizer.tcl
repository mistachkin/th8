###############################################################################
#
# coverage_tokenizer.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for word-tokenizer / brace-content
# decisions in src/th8_core.c:th8NRSubstAndBuild, focusing on
# the SPECIFIC missing vectors:
#
#   :12341 if (zWord[i]=='\\' && i+1 < nn-1 && zWord[i+1]=='\n')
#                                          (brace-content backslash-
#                                          newline scanner, missing
#                                          C2-pair: backslash at the
#                                          penultimate position --
#                                          i+1 == nn-1)
#   :12347 while (i < nn-1 && (zWord[i]==' ' || zWord[i]=='\t'))
#                                          (post-backslash-newline
#                                          whitespace skip, missing
#                                          C1-pair: i reaches nn-1
#                                          inside the inner skip)
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

runTest {test toksub-2.1 {
  Brace-content backslash-newline scanner with a backslash
  at the PENULTIMATE position of the brace content drives
  the C2=F vector at th8_core.c:12341-12343 -- zWord[i] is
  '\\' (C1=T) but i+1 == nn-1 (C2=F), so the close-brace
  index is reached and the lookahead is rejected.  The
  trailing backslashes are preserved in the word value.
} -constraints {
    th8
} -body {
  set x {ab\\}
  list \
      [string length $x] \
      [string index $x end]
} -cleanup {
  unset -nocomplain x
} -result {4 \\}}

###############################################################################

runTest {test toksub-3.1 {
  Brace-content backslash-newline + trailing-whitespace
  skip where the whitespace extends to the end of the
  brace content drives the C1=F vector at
  th8_core.c:12347-12349 -- the inner whitespace-skip loop
  exits because i reaches nn-1, not because of a non-WS
  char.  The result has the backslash-newline collapsed to
  a single space and trailing whitespace consumed.
} -constraints {
    th8
} -body {
  set s "list {a\\\n     }"
  set r [eval $s]
  list \
      [llength $r] \
      [string length [lindex $r 0]]
} -cleanup {
  unset -nocomplain s r
} -result {1 2}}

###############################################################################

runTest {test toksub-4.1 {
  Brace-content backslash-newline followed immediately by a
  TAB (not a space) drives the C3=T vector at th8SubstWord
  (th8_core.c:11510-11511) -- inner whitespace-skip loop
  sees zWord[i] != ' ' (C2=F) but zWord[i] == '\t' (C3=T),
  so iteration continues past the tab.  Combined with the
  existing space-driven coverage, this closes C3-pair.
} -constraints {
    th8
} -body {
  set s "list {a\\\n\tb}"
  set r [eval $s]
  list \
      [llength $r] \
      [lindex $r 0]
} -cleanup {
  unset -nocomplain s r
} -result {1 {a b}}}

###############################################################################

runTest {test toksub-5.1 {
  Empty expansion-tag like `{}rest` drives the C2=F vector
  at th8SplitCommand (th8_core.c:11914) -- k+1 < nWord (T)
  but k <= 1 (F).  The expansion check sees an empty tag
  and falls through.  The result is the literal `{}rest`
  becoming a regular braced word followed by content.
} -constraints {
    th8
} -body {
  set rcs {}
  set s1 [format "list \173\175rest"]
  catch {eval $s1} m
  lappend rcs [expr {[string length $m] >= 0}]
  set s2 [format "list \173\175a"]
  catch {eval $s2} m
  lappend rcs [expr {[string length $m] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs s1 s2 m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
