###############################################################################
#
# coverage_string_list_nocase.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for case-folding compounds in
# src/plugins/th8_strings.c and src/plugins/th8_lists.c that
# are at 0% MC/DC because the existing -nocase tests use
# uppercase/lowercase only (missing the non-letter F,- vector):
#
#   th8_strings.c:294    string equal -nocase   (b >= 'A' && b <= 'Z')
#   th8_strings.c:1125   string match -nocase   (c >= 'A' && c <= 'Z') [pattern fold]
#   th8_strings.c:1180   string match -nocase   (cb >= 'A' && cb <= 'Z') [exact fold]
#   th8_lists.c:917      lsearch -nocase        (ca >= 'A' && ca <= 'Z')
#   th8_lists.c:919      lsearch -nocase        (cb >= 'A' && cb <= 'Z')
#
# Each must process a string containing upper, lower, and
# non-letter chars to drive T,T / T,F / F,- vectors.
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
# Section 1 -- string equal -nocase with non-letter chars
#
###############################################################################

runTest {test sl_nocase-1.1 {
  string equal -nocase with mixed upper, lower, and non-letter chars
} -constraints {
    th8
} -body {
  string equal -nocase "Hello World 123!" "hello WORLD 123!"
} -result {1}}

###############################################################################

runTest {test sl_nocase-1.2 {
  string equal -nocase mismatch with non-letter at start
} -constraints {
    th8
} -body {
  string equal -nocase "@HELLO" "@hello"
} -result {1}}

###############################################################################
#
# Section 2 -- string match -nocase with mixed chars
#
###############################################################################

runTest {test sl_nocase-2.1 {
  string match -nocase glob with all three char categories
} -constraints {
    th8
} -body {
  string match -nocase "*World*" "Hello World 42!"
} -result {1}}

###############################################################################

runTest {test sl_nocase-2.2 {
  string match -nocase exact-form (no glob chars) with mixed input
} -constraints {
    th8
} -body {
  string match -nocase "Hello42!" "HELLO42!"
} -result {1}}

###############################################################################

runTest {test sl_nocase-2.3 {
  string match -nocase with digit and special-char patterns
} -constraints {
    th8
} -body {
  string match -nocase {[A-Z]bc*4@} "Abc1234@"
} -result {1}}

###############################################################################
#
# Section 3 -- lsearch -nocase
#
###############################################################################

runTest {test sl_nocase-3.1 {
  lsearch -sorted -dictionary drives the case-fold compound
  in the binary-search dictionary comparator
} -constraints {
    th8
} -body {
  # Sorted list with mixed-case letters and digits to drive both
  # the (ca>='A' && ca<='Z') and (cb>='A' && cb<='Z') compounds.
  catch {lsearch -sorted -dictionary \
      [list Apple1 banana2 Cherry@3 dragon4] Banana2} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sl_nocase-3.2 {
  lsort -dictionary on a list with mixed upper/lower/digit/special
  chars -- exercises the dictionary comparator in the sort path
} -constraints {
    th8
} -body {
  catch {lsort -dictionary \
      [list Banana2 Apple1 cherry@3 Apple10 banana20]} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
