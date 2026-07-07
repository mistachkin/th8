###############################################################################
#
# coverage_lsort_dict.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for lsort -dictionary digit and case
# compounds at src/plugins/th8_lists.c:
#
#   :891  while (ia < nA && ib < nPat && cmp == 0) { ... }
#   :903  && azElem[mid][ia] >= '0' && azElem[mid][ia] <= '9') {
#   :910  && zPat[ib] >= '0' && zPat[ib] <= '9') {
#   :917  if (ca >= 'A' && ca <= 'Z') ca += ('a' - 'A');
#   :1306 while (ia < nA && ib < nB && cmp == 0) { ... }
#   :1317 && zA[ia] >= '0' && zA[ia] <= '9') {
#   :1322 && zB[ib] >= '0' && zB[ib] <= '9') {
#
# These implement the "natural" sort order: digit substrings
# compare numerically; ASCII case is folded.  Existing lsort
# coverage already drives the no-digit / no-case path.  The
# tests here add inputs that are equal-up-to-fold to drive the
# fold compounds, and digit substrings of differing length to
# drive the digit-state-machine compounds.
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

runTest {test lsort_dict-1.1 {
  lsort -dictionary on a list mixing case drives the
  ca/cb >= 'A' && <= 'Z' case-fold compounds (lists.c:917
  for binary-search bisect path; mirror in linear-compare
  path).  All three vectors of each cb/ca compound are hit
  across the comparisons triggered by the sort.
} -constraints {
    th8
} -body {
  lsort -dictionary {Banana apple Cherry banana APPLE cherry}
} -result {apple APPLE Banana banana Cherry cherry}}

###############################################################################

runTest {test lsort_dict-1.2 {
  lsort -dictionary on numeric-suffix items drives the
  digit-substring state machine (lists.c:903 / :910 / :1317
  / :1322).  The inputs differ only inside the digit run,
  which forces the state machine through both the "both
  sides are digits" branch and the "left is digit, right
  is letter" cross-over.
} -constraints {
    th8
} -body {
  lsort -dictionary {item10 item2 item1 item20 item3}
} -result {item1 item2 item3 item10 item20}}

###############################################################################

runTest {test lsort_dict-1.3 {
  lsort -dictionary on items with embedded multi-segment
  digit runs drives the loop-exit at end of one operand
  before the other (lists.c:891 / :1306, ia < nA short-
  circuits or ib < nB short-circuits).
} -constraints {
    th8
} -body {
  lsort -dictionary {a a1 a2 a a10}
} -result {a a a1 a2 a10}}

###############################################################################

runTest {test lsort_dict-1.4 {
  lsort -dictionary with only-digit elements drives the
  pure-numeric comparison path; verifies the (cmp == 0)
  loop-continue condition gets exercised when leading
  digits agree.
} -constraints {
    th8
} -body {
  lsort -dictionary {100 20 3 1000 4}
} -result {3 4 20 100 1000}}

###############################################################################

runTest {test lsort_dict-1.5 {
  lsort -dictionary with elements containing digits
  followed immediately by a character whose ASCII value
  is BELOW '0' (e.g. '.', '-', ' ', '!', '/') drives the
  C2=F vector at the digit-accumulator loops at lines
  1316-1317 and 1321-1322 -- the loop body advances ia/
  ib while characters are digits and exits when the next
  character is < '0'.  Existing tests only exit via
  end-of-string (C1=F) or via char > '9' (C3=F).
} -constraints {
    th8
} -body {
  list \
      [lsort -dictionary {1.5 1.10 2.1}] \
      [lsort -dictionary {a1-b a2-b a10-b}] \
      [lsort -dictionary {x1!y x2!y x10!y}]
} -result {{1.5 1.10 2.1} {a1-b a2-b a10-b} {x1!y x2!y x10!y}}}

###############################################################################

runTest {test lsort_dict-1.6 {
  lsearch -sorted -dictionary on a list whose mid-element
  contains a digit run followed by a char BELOW '0'
  drives the C2=F vector at the digit accumulator loops
  at th8_lists.c:901-903 and 908-910 -- the lsearch
  binary-search comparator's digit-accumulator hits the
  '.', '-', etc. and exits.  Distinct compound from
  lsort's L1316/L1321 (which lsort_dict-1.5 already
  closed).
} -constraints {
    th8
} -body {
  list \
      [lsearch -sorted -dictionary {a1.5 a2.5 a10.5} a2.5] \
      [lsearch -sorted -dictionary {x1!y x2!y x10!y} x2!y] \
      [lsearch -sorted -dictionary {a1-b a2-b a10-b} a2-b]
} -result {1 1 1}}

###############################################################################

runTest {test lsort_dict-1.7 {
  lsearch -sorted -dictionary on a list whose mid-element
  contains a digit run followed by a char ABOVE '9'
  (e.g. 'a'-'z') drives the C3=F vector at the digit
  accumulator loops at th8_lists.c:901-903 and 908-910 --
  the digit-accumulator exits because the next char
  exceeds '9'.  Combined with -1.6, closes both ends of
  the digit-range exit conditions.
} -constraints {
    th8
} -body {
  list \
      [lsearch -sorted -dictionary {a1z a2z a10z} a2z] \
      [lsearch -sorted -dictionary {x1abc x2abc x10abc} x2abc] \
      [lsearch -sorted -dictionary {b1q b2q b10q} b2q]
} -result {1 1 1}}

###############################################################################

runTest {test lsort_dict-1.8 {
  lsearch -sorted -dictionary with UPPERCASE elements
  drives the C2=T (T,T) vector at the case-fold compound
  th8_lists.c:917 (ca >= 'A' && ca <= 'Z' folds 'A'-'Z'
  to lowercase before compare).  Existing lsearch tests
  use lowercase elements, leaving (T,F) covered only.
} -constraints {
    th8
} -body {
  list \
      [lsearch -sorted -dictionary {ABC DEF XYZ} DEF] \
      [lsearch -sorted -dictionary {AAA BBB CCC} BBB]
} -result {1 1}}

###############################################################################

runTest {test lsort_dict-1.9 {
  lsearch -sorted -dictionary with elements whose first
  char is BELOW 'A' (digit or punct) drives the C1=F
  vector at th8_lists.c:917 -- ca < 'A' short-circuits
  the case-fold check.
} -constraints {
    th8
} -body {
  list \
      [lsearch -sorted -dictionary {1abc 2def 3ghi} 2def] \
      [lsearch -sorted -dictionary {!a !b !c} !b]
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
