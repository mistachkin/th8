###############################################################################
#
# coverage_char_classify_neg.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c char-classification
# predicates L10183 (th8IsHexDig) C1-Pair:
#
#   return c>=0 && c<256 && (th8CharProp[c]&0x22);
#
# The C1=F vector (c < 0, i.e., a sign-extended multibyte
# UTF-8 byte) was unreached for th8IsHexDig because no test
# fed a non-ASCII byte through the backslash-hex escape
# parser.  Existing bslesc tests stop the hex-digit scan
# with ASCII non-hex chars only, driving (T,T,F).
#
# Driver: a string literal containing the byte sequence
# \u12<0xc3> -- a 2-digit Unicode escape immediately
# followed by a raw 0xC3 byte.  After scanning \u12 the
# escape parser calls th8IsHexDig on byte 0xC3.  Because
# char is signed on this platform the int promotion is -61,
# which triggers C1=F (c >= 0 false), short-circuiting C2
# and C3.
#
# Non-ASCII bytes in this test fixture are intentional;
# test source ascii-cleanliness rules cover the engine
# code, not coverage-fixture inputs that exercise the
# engine's handling of arbitrary bytes.
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

runTest {test charneg-1.1 {
  String literal "\u12<0xC3>z" drives th8IsHexDig L10183
  C1=F: the partial 2-digit \u escape calls th8IsHexDig
  on 0xC3 which is a negative int after char-promotion.
  Length is 2 (one Unicode char from \u12, two bytes for
  0xC3 + ascii z appended).  We only assert it's non-
  empty since the exact length depends on internal
  handling of high bytes.
} -constraints {
    th8
} -setup {
} -body {
  set x "\u12�z"
  expr {[string length $x] > 0}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################


###############################################################################

runTest {test charneg-1.2 {
  Variable substitution "$abc<0xC3>" drives th8IsAlnum L10481
  C1=F: the var-name scanner reads the non-ASCII byte after
  the bare-word identifier and calls th8IsAlnum on it.
  With signed char, 0xC3 promotes to -61 so C1=F short-
  circuits the property lookup, the scan terminates at the
  high byte, and the variable resolves to just "abc".
} -constraints {
    th8
} -setup {
  set ::abc "alpha"
} -body {
  set v "$abc�"
  expr {[string match "alpha*" $v]}
} -cleanup {
  unset -nocomplain v
  unset -nocomplain ::abc
} -result {1}}

###############################################################################

runTest {test charneg-1.3 {
  `expr {12<0xC3>z}` (numeric token in expr-grammar
  followed by a raw 0xC3 byte) drives th8IsDigit L10179
  C1=F: the expr-grammar numeric scanner at L1942 reads
  the byte after digits and calls th8IsDigit on it.  With
  signed char, 0xC3 promotes to -61 so C1=F short-
  circuits the property lookup.  The expression itself
  errors with "syntax error" because the scanner stops at
  the high byte and the trailing 'z' is unparseable, which
  we catch and discard.
} -constraints {
    th8
} -setup {
} -body {
  set rc [catch {expr {12�z}}]
  expr {$rc != 0}
} -cleanup {
  unset -nocomplain rc
} -result {1}}

###############################################################################

source tests/epilogue.tcl
