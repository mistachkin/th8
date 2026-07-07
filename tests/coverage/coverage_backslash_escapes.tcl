###############################################################################
#
# coverage_backslash_escapes.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for backslash-escape parsing decisions
# in src/th8_core.c (th8NextEscape):
#
#   :10319  while (i < nInput && i < 6  && th8IsHexDig(zInput[i]))
#                  (\u 1-4 hex digit accumulator, missing C3-pair:
#                   non-hex char encountered before max digits)
#   :10328  while (i < nInput && i < 10 && th8IsHexDig(zInput[i]))
#                  (\U 1-8 hex digit accumulator, missing C3-pair:
#                   non-hex char encountered before max digits)
#
# Also closes the analogous \x and octal compounds at lower
# coverage densities.
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

runTest {test bslesc-1.1 {
  \u escape with FEWER than 4 hex digits followed by a
  non-hex character drives the C3=F vector at line
  10319 -- the digit accumulator exits because the next
  character is not a hex digit, before reaching the
  max-digit cap of 4.
} -constraints {
    th8
} -body {
  list \
      "\u12Z" \
      "\u3X" \
      "\uABz"
} -result [list "Z" "X" "«z"]}

###############################################################################

runTest {test bslesc-1.2 {
  \U escape with FEWER than 8 hex digits followed by a
  non-hex character drives the C3=F vector at line
  10328 -- the digit accumulator exits because the next
  character is not a hex digit, before reaching the
  max-digit cap of 8.
} -constraints {
    th8
} -body {
  list \
      "\U41g" \
      "\U6162Y" \
      "\U7Az"
} -result [list "Ag" "慢Y" "zz"]}

###############################################################################

runTest {test bslesc-2.1 {
  Braced string with a backslash-newline immediately
  before the closing brace drives the C1=F vector at the
  whitespace-skip loop in th8NRSubstAndBuild (lines
  12347-12349).  After consuming the backslash-newline
  and i += 2, the next iteration sees i == nn - 1 so
  the loop never enters body.  The substitution result
  is a single space (the backslash-newline collapse).
} -constraints {
    th8
} -body {
  set s {\
}
  list [string length $s] [string index $s 0]
} -cleanup {
  unset -nocomplain s
} -result {1 { }}}

###############################################################################

runTest {test bslesc-3.1 {
  Octal escape (\ooo) with FEWER than 3 octal digits
  followed by a non-octal character drives the C3=F
  vector at th8_core.c:10349-10350 -- the digit
  accumulator exits because the next character isn't an
  octal digit, before reaching the max of 3 digits.  We
  verify the parsed value via [scan ... %c] rather than
  string equality (which would be ambiguous for
  non-printable bytes).
} -constraints {
    th8
} -body {
  set s1 "\7x"
  set s2 "\01a"
  set s3 "\12z"
  list [string length $s1] [scan [string index $s1 0] %c] \
       [string length $s2] [scan [string index $s2 0] %c] \
       [string length $s3] [scan [string index $s3 0] %c]
} -cleanup {
  unset -nocomplain s1 s2 s3
} -result {2 7 2 1 2 10}}

###############################################################################

runTest {test bslesc-4.1 {
  \U escape with 8 (MAX) hex digits followed by another
  hex character drives the C2=F vector at
  th8_core.c:10328 -- after consuming 8 hex digits the
  loop reaches i == 10, the i < 10 condition becomes F
  and the loop exits via C2 even though the next char is
  hex.  The parsed value uses only the first 8 digits and
  the trailing chars remain literal.
} -constraints {
    th8
} -body {
  list \
      "\U00000041abcd" \
      "\U00000042def" \
      "\U00000043f"
} -result [list "Aabcd" "Bdef" "Cf"]}

###############################################################################

runTest {test bslesc-5.1 {
  Word starting with empty braces "{}rest" is rejected
  as "extra characters after close-brace" per Tcl 8.6.
  Drives the empty-tag arm at the expansion-tag scanner
  in th8SplitCommand -- when k == 1 (close-brace
  immediately after open-brace) and k + 1 < nWord, the
  arm now returns TH8_ERROR rather than letting the word
  fall through as literal text.  Closes Bug 13.
} -constraints {
    th8
} -body {
  set rcs {}
  set s1 "list \173\175a \173\175b \173\175c"
  lappend rcs [catch {eval $s1} m1]
  lappend rcs [string match {*extra characters*} $m1]
  set s2 "set vv \173\175x"
  lappend rcs [catch {eval $s2} m2]
  lappend rcs [info exists vv]
  set rcs
} -cleanup {
  unset -nocomplain rcs s1 s2 m1 m2 vv
} -result {1 1 1 0}}

###############################################################################

runTest {test bs-cov-trail-1.1 {
  Quoted word containing a backslash-newline followed by
  whitespace that runs up to the closing quote drives the
  C1=F (i < nn-1) vector at th8_core.c:12417 inside
  th8NRSubstAndBuild's word-build phase.  The inner
  whitespace-consumption loop after a backslash-newline
  substitution must run to i == nn-1 (end of word interior)
  rather than stopping early on a non-whitespace byte.
  Existing tests stop on non-whitespace (C2=T or C3=T);
  this closes the C1-Pair by ending the loop on EOS.

  We build the script at runtime via [format %c 10] for the
  embedded newline byte so the runTest body-brace parser
  does not consume the backslash-newline before our [eval]
  ever sees it.
} -constraints {
    th8
} -body {
  set script "set x \{abc \\[format %c 10]   \}"
  eval $script
  string length $x
} -cleanup {
  unset -nocomplain script x
} -result {5}}

###############################################################################

source tests/epilogue.tcl
