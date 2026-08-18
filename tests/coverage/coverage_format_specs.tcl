###############################################################################
#
# coverage_format_specs.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [format] / [scan] format-specifier
# parsing compounds in src/plugins/th8_formatting.c:
#
#   :155  if (i < nFmt && zFmt[i] == '.')        (precision dot)
#   :169  && zFmt[i] <= '9'                      (digit in precision)
#   :871  && zFmt[iFmt] <= '9'                   (scan width digit)
#   :906  && zStr[iStr] <= '7'                   (scan octal digit)
#   :930  || zStr[iStr + 1] == 'X'               (scan hex prefix)
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

runTest {test fmtspec-1.1 {
  format "%.*d" with the .* precision specifier drives line
  155 (i < nFmt && zFmt[i] == '.') and the dot-then-* path,
  plus line 169 (zFmt[i] <= '9' for the precision-digit
  loop entry).  TH8's %d ignores precision but the dot/star
  parsing still runs; combined width "%5.*d" exercises both
  width-digit and precision-star paths together.
} -constraints {
    th8
} -body {
  list \
      [format "%.*d" 5 42] \
      [format "%5.*d" 3 42] \
      [format "%-5.*d" 3 42]
} -result {42 {   42} {42   }}}

###############################################################################

runTest {test fmtspec-1.2 {
  Multi-character width drives line 139 (zFmt[i] <= '9'
  in the width-digit loop) past the first iteration --
  single-digit widths only hit the loop once.
} -constraints {
    th8
} -body {
  list \
      [format "%12d" 42] \
      [format "%5d" 42] \
      [string length [format "%99s" abc]]
} -result {{          42} {   42} 99}}

###############################################################################

runTest {test fmtspec-2.1 {
  scan with hex prefix "0X" or "0x" -- line 930 (the
  X-or-x check at zStr[iStr + 1]).  Both upper and lower
  case in the same test exercise both halves of the OR.
} -constraints {
    th8
} -body {
  list \
      [scan "0X1A" "%x"] \
      [scan "0x1a" "%x"] \
      [scan "1A"   "%x"]
} -result {26 26 26}}

###############################################################################

runTest {test fmtspec-2.2 {
  scan with octal digits 0..7 -- line 906
  (zStr[iStr] >= '0' && zStr[iStr] <= '7').  An out-of-
  octal-range digit (8 or 9) closes the second condition's
  F vector when scan-octal stops early.
} -constraints {
    th8
} -body {
  list \
      [scan "0177" "%o"] \
      [scan "0789" "%o"] \
      [scan "0o7"  "%o"]
} -result {127 7 0}}

###############################################################################

runTest {test fmtspec-2.3 {
  scan honors a multi-character field width: %5d reads at most
  five input characters, so "123456" yields 12345 (Tcl 8.6 parity).
} -constraints {
    th8
} -body {
  list \
      [scan "12345"  "%5d"] \
      [scan "123456" "%5d"] \
      [scan "12"     "%5d"]
} -result {12345 12345 12}}

###############################################################################

runTest {test fmtspec-3.1 {
  format "%g" of values that produce only-zero fractional
  digits drives the C1-pair at th8_formatting.c:574 --
  the trailing-zero strip loop iterates fp all the way
  down to zFBuf, where C1 (fp > zFBuf) becomes F.
} -constraints {
    th8
} -body {
  list \
      [format "%g" 1.0] \
      [format "%g" 0.0] \
      [format "%g" 100.0] \
      [format "%.2f" 0.999]
} -result {1 0 100 1.00}}

###############################################################################

runTest {test fmtspec-3.2 {
  format "%+05d" of a positive integer drives the C4-pair
  at th8_formatting.c:704 (zNum[0] == '+') -- the
  zero-padding-with-sign branch encounters a '+' rather
  than a '-' as the leading char.
} -constraints {
    th8
} -body {
  list \
      [format "%+05d" 42] \
      [format "%05d" -42] \
      [format "%05d" 42] \
      [format "%+d" 7]
} -result {+0042 -0042 00042 +7}}

###############################################################################

runTest {test fmtspec-3.3 {
  scan in list mode returns one element per non-suppressed specifier,
  padding unmatched trailing specifiers with empty strings, except
  that an empty input yields the empty list (Tcl 8.6 parity).
} -constraints {
    th8
} -body {
  list \
      [scan "abc" "%s %s"] \
      [scan "1" "%d %d"] \
      [scan "" "%s"]
} -result {{abc {}} {1 {}} {}}}

###############################################################################

runTest {test fmtspec-3.4 {
  format "%.*s" with NEGATIVE precision drives the (T,F,-)
  vector at th8_formatting.c:354 -- havePrec=T, prec < 0,
  so the truncation compound short-circuits at C2 and
  the full string is emitted.
} -constraints {
    th8
} -body {
  list \
      [format "%.*s" -1 "abc"] \
      [format "%.*s" -5 "hello"] \
      [format "%.2s" "abcdef"]
} -result {abc hello ab}}

###############################################################################

runTest {test fmtspec-4.1 {
  format "%-" / "%+0" (a percent with only flags and no
  conversion-spec body) drives the C1=F vector at
  th8_formatting.c:138 -- the width-digit loop entry sees
  i >= nFmt because the format string is exhausted right
  after the flags.  The parser then errors via the
  bad-format path.  ("%" alone is treated as a literal
  by the outer loop and does not enter this branch.)
} -constraints {
    th8
} -body {
  set rcs {}
  foreach fs {"%-" "%+" "%0"} {
      lappend rcs [catch {format $fs} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs fs m
} -result {1 1 1}}

###############################################################################

runTest {test fmtspec-4.2 {
  format "%.!d" with a precision-followed-by-char-below-'0'
  drives the C2=F vector at th8_formatting.c:168 -- the
  precision-digit loop sees zFmt[i] < '0' (the '!' is
  ASCII 33, below '0').  prec stays 0 and the parser
  rejects the spec on the conversion-char read.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach fs {"%.!d" "%.\"d" "%.#d"} {
      lappend rcs [catch {format $fs 1} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs fs m
} -result {1 1 1}}

###############################################################################

runTest {test fmtspec-4.3 {
  format "%g" of a NaN value drives the C1=T vector at
  th8_formatting.c:415 -- rVal != rVal is true (NaN
  property), short-circuiting the (rVal != 0 && doubles
  to itself) compound.  Emits "NaN" literally.
} -constraints {
    th8
} -body {
  set inf [expr {1e308 * 1e308}]
  set nan [expr {$inf - $inf}]
  list \
      [format "%g" $nan] \
      [format "%e" $nan] \
      [format "%f" $nan]
} -cleanup {
  unset -nocomplain inf nan
} -result {NaN NaN NaN}}

###############################################################################

runTest {test fmtspec-5.1 {
  scan with a LITERAL character in the format that fails
  to match the input character drives the C2=F vector at
  th8_formatting.c:849 -- iStr < nStr (T) but zStr[iStr]
  != zFmt[iFmt] (the literal char doesn't match).  This
  causes scan to return early with no captures.
} -constraints {
    th8
} -body {
  list \
      [scan "abc" "X%d"] \
      [scan "1xx" "1y%d"] \
      [scan "abc" "abZ"]
} -result {{} {} {}}}

###############################################################################

runTest {test fmtspec-6.1 {
  format "%u" of various integers exercises the unsigned-
  conversion branch at th8_formatting.c:229 onward,
  which has no other coverage source.  Drives the case
  body and the Th8_ToWideInt success path.
} -constraints {
    th8
} -body {
  list \
      [format "%u" 0] \
      [format "%u" 42] \
      [format "%u" 12345] \
      [format "%5u" 7]
} -result {0 42 12345 {    7}}}

###############################################################################

runTest {test fmtspec-6.2 {
  format "%.0f" with a fractional part BELOW 0.5 drives
  the C2=F vector at th8_formatting.c:589 -- nPrec == 0
  (T) but fPart < 0.5 (F), so the integer round-up
  branch is skipped.  The integer part is emitted as-is.
} -constraints {
    th8
} -body {
  list \
      [format "%.0f" 1.4] \
      [format "%.0f" 2.49] \
      [format "%.0f" 0.1] \
      [format "%.0f" 7.0]
} -result {1 2 0 7}}

###############################################################################

runTest {test fmtspec-7.1 {
  format "%05s" "" drives th8_formatting.c L713 C2-Pair
  (T,F,-,-) -- flagZero is set, nNum is 0 (empty string),
  so the && short-circuits at C2 and the zero-padded-with-
  sign branch is skipped, falling through to plain right-
  justify zero-padding.  Existing tests only exercise %0Nd
  with non-empty number strings (nNum > 0); empty-string
  %0Ns is the missing vector.
} -constraints {
    th8
} -body {
  list \
      [format "<%05s>" ""] \
      [format "<%03s>" ""] \
      [format "<%05s>" abc] \
      [format "<%05s>" x]
} -result {<00000> <000> <00abc> <0000x>}}

###############################################################################

source tests/epilogue.tcl
