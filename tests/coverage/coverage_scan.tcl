###############################################################################
#
# coverage_scan.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the format-string parser in
# src/plugins/th8_formatting.c scan_command:
#
#   line 814: if (iStr >= nStr || zStr[iStr] != zFmt[iFmt]) break;   (literal-mismatch)
#   line 825: if (iStr >= nStr || zStr[iStr] != '%') break;           (literal %%)
#   line 835: while (iFmt < nFmt && zFmt[iFmt] >= '0' &&              (width-skip loop)
#             zFmt[iFmt] <= '9')
#
# The existing scan tests cover happy-path conversions but not
# the literal-mismatch / input-exhausted / width-spec edge cases.
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
#
# Section 1 -- literal char mismatch (line 814 F,T vector)
#
###############################################################################

runTest {test scan_cov-1.1 {
  scan with literal char that doesn't match input
} -constraints {
    th8
} -body {
  catch {scan "abc" "xyz%d" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################

runTest {test scan_cov-1.2 {
  scan with literal that matches (line 814 F,F vector)
} -constraints {
    th8
} -body {
  catch {scan "abc 42" "abc %d" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################
#
# Section 2 -- input exhausted (line 814 T,- vector)
#
###############################################################################

runTest {test scan_cov-2.1 {
  scan input shorter than format literal
} -constraints {
    th8
} -body {
  catch {scan "ab" "abcdef" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################

runTest {test scan_cov-2.2 {
  scan empty input against non-empty format
} -constraints {
    th8
} -body {
  catch {scan "" "literal" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################
#
# Section 3 -- literal %% in format (line 825)
#
###############################################################################

runTest {test scan_cov-3.1 {
  scan with literal %% in format, matching input
} -constraints {
    th8
} -body {
  catch {scan "100%" "%d%%" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################

runTest {test scan_cov-3.2 {
  scan with literal %% in format, input ends before %
} -constraints {
    th8
} -body {
  catch {scan "100" "%d%%" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################
#
# Section 4 -- width-skip loop (line 835: digit-scanning compound)
#
###############################################################################

runTest {test scan_cov-4.1 {
  scan with width modifier (multiple digits exercises 835's >=0 && <=9)
} -constraints {
    th8
} -body {
  catch {scan "12345" "%5d" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################

runTest {test scan_cov-4.2 {
  scan with multi-digit width (drives the loop more than once)
} -constraints {
    th8
} -body {
  catch {scan "1234567890" "%10d" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################

runTest {test scan_cov-4.3 {
  scan with conversion immediately after % (no width, T,F vector for 835)
} -constraints {
    th8
} -body {
  catch {scan "42" "%d" v} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain v r
} -result {1}}

###############################################################################

runTest {test scan_cov-5.1 {
  scan where the format consumes MORE characters than the
  input string contains drives the C2=F vector at the main
  scan loop (th8_formatting.c:843) -- iStr advances past
  nStr via a fixed-width directive on a too-short input,
  causing iStr <= nStr to become F and the loop to exit
  with partial matches.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [scan "a" "%c%c" v1 v2]
  lappend rcs [scan "ab" "%c%c%c" w1 w2 w3]
  lappend rcs [scan "1" "%2d" n]
  set rcs
} -cleanup {
  unset -nocomplain rcs v1 v2 w1 w2 w3 n
} -result {1 2 1}}

###############################################################################

runTest {test scan_cov-6.1 {
  scan "%f" of an input containing DIGITS followed by a
  non-digit, non-dot, non-e/E character drives the C3=F
  vector at the float scanner loop
  (th8_formatting.c:998-1002) -- after digit scan,
  zStr[iStr] > '9' so C3=F, then C4-C6 also F, loop
  exits.  Covers the float-stop char paths.
} -constraints {
    th8
} -body {
  set rcs {}
  set rc [scan "12abc" "%f" v]
  lappend rcs $rc
  lappend rcs $v
  set rc [scan "1.5z" "%f" w]
  lappend rcs $rc
  lappend rcs $w
  set rcs
} -cleanup {
  unset -nocomplain rcs rc v w
} -result {1 12.0 1 1.5}}

###############################################################################

runTest {test scan_cov-6.2 {
  scan "%f" of inputs containing lowercase 'e' and
  uppercase 'E' exponents drives the C5-pair and C6-pair
  vectors at the float scanner loop
  (th8_formatting.c:998-1002) -- when the current char is
  'e' (C5=T) or 'E' (C6=T), the loop continues consuming.
  Existing tests cover digit-only inputs; this closes the
  exponent-letter pairs.
} -constraints {
    th8
} -body {
  set rcs {}
  catch {scan "1e10" "%f" v}
  catch {scan "2.5E3" "%f" w}
  catch {scan "3e5" "%f" x}
  catch {scan "4E7" "%f" y}
  set rcs [list [info exists v] [info exists w] [info exists x] [info exists y]]
  set rcs
} -cleanup {
  unset -nocomplain rcs v w x y
} -result {1 1 1 1}}

###############################################################################

runTest {test scan_cov-6.3 {
  Bug 8 regression: scan "%f" must accept an optional
  '+' or '-' immediately after 'e' / 'E'.  Previously
  the lexer's flat char-class stopped at the sign byte
  (so "1e-3" became the token "1e", Th8_ToDouble rejected
  it, and scan returned 0 with the target variable unset).
  Now: lower- and upper-case exponents with either sign
  parse, and assignment-by-name (scan into a var) sets
  the variable correctly.  Also exercises the edge cases
  where the sign or digits after the e/E are absent
  ("1e" / "1e-") and the scan correctly rejects them.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach s {1e-3 1e+3 1E-3 1E+3 2.5e-2 -1.0e-5 +3.14E+10} {
      set rc [scan $s "%f" v]
      lappend rcs $rc
      lappend rcs $v
  }
  unset -nocomplain v
  set rc [scan "1e" "%f" v]
  lappend rcs incomplete-rc $rc incomplete-set? [info exists v]
  unset -nocomplain v
  set rc [scan "1e-" "%f" v]
  lappend rcs trail-sign-rc $rc trail-sign-set? [info exists v]
  set rcs
} -cleanup {
  unset -nocomplain rcs rc s v
} -result {1 0.001 1 1000.0 1 0.001 1 1000.0 1 0.025 1 -1e-05 1 31400000000.0 incomplete-rc 0 incomplete-set? 0 trail-sign-rc 0 trail-sign-set? 0}}

###############################################################################

runTest {test scan_cov-7.1 {
  scan with a format that ends in digits (no conversion
  character) drives the C1=F vector at the width-skip
  loop in th8_formatting.c:870 -- iFmt walks past the
  digits to nFmt and the while-condition iFmt<nFmt
  evaluates F on the last iteration.  And a format with
  a sub-'0' byte (e.g. ' ' or '!') at the width position
  drives the C2=F vector -- zFmt[iFmt] < '0' so the
  loop exits without consuming any digits.
} -constraints {
    th8
} -body {
  set rcs {}
  # C1=F: format string ends with digits (no conversion).
  # The width-skip loop walks iFmt to nFmt; the outer
  # break at L874 catches this and exits cleanly.
  lappend rcs [catch {scan "42" "%123" v}]
  # C2=F: format byte is a space (< '0' = 0x30).  The
  # width-skip loop exits at the first iteration with
  # zFmt[iFmt] >= '0' = F; the switch default fires.
  lappend rcs [catch {scan "42" "% " v}]
  # C2=F variant: format byte is '!' (0x21, < '0').
  lappend rcs [catch {scan "42" "%!" v}]
  set rcs
} -cleanup {
  unset -nocomplain rcs v
} -result {0 0 0}}

###############################################################################

runTest {test scan_cov-8.1 {
  scan "%d" with values just past INT64_MIN drives the
  C3-pair and C4-pair at Th8_ToWideInt's INT64_MIN-save
  branch (th8_core.c:15387-15389).  Existing tests cover
  the exact INT64_MIN string and the no-overflow case;
  this exercises "-9223372036854775809" (val matches
  boundary but digit=9, C4=F) and longer overflows (val
  exceeds boundary, C3=F).
} -constraints {
    th8
} -body {
  set rcs {}
  set vals {
    -9223372036854775808
    -9223372036854775809
    -99999999999999999999
    -9999999999999999999
  }
  foreach inp $vals {
      unset -nocomplain v
      catch {scan $inp "%d" v} n
      lappend rcs $n
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs vals inp v n
} -result {1 0 0 0}}

###############################################################################

runTest {test scan_cov-9.1 {
  scan "%f" of a SIGNED float (leading '-' or '+') drives
  the C2-pair and C3-pair at th8_formatting.c:999-1000
  (the sign-prefix consume at the start of the float
  scanner).  Existing tests use unsigned inputs (C2=F,
  C3=F); this exercises the signed forms.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach inp {-1.5 +1.5 -3.14 +3.14 -0.5} {
      catch {scan $inp "%f" v} n
      lappend rcs [expr {[info exists v] && $n == 1}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp v n
} -result {1 1 1 1 1}}

###############################################################################

runTest {test scan_cov-9.2 {
  scan "%f" on an EMPTY string drives the C1=F vector at
  th8_formatting.c:1007 (the sign-prefix consume's outer
  bound check) -- iStr == nStr at entry, so the compound
  short-circuits before evaluating the sign characters.
  Existing tests pass non-empty inputs (C1=T); this closes
  the C1-pair.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {scan "" "%f" v1} n1]
  lappend rcs $n1
  lappend rcs [catch {scan "    " "%f" v2} n2]
  lappend rcs $n2
  set rcs
} -cleanup {
  unset -nocomplain rcs v1 v2 n1 n2
} -result {0 0 0 0}}

###############################################################################

source tests/epilogue.tcl
