###############################################################################
#
# coverage_flags_have_wildcards_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for the four under-covered switch cases in
# src/plugins/harpy/th8_attrflags.c:
#
#   th8AfWildcardHaveAll  -- cases '#', '!', '$', '@'  (L891-908)
#   th8AfWildcardHaveAny  -- cases '#', '!', '$', '@'  (L950-967)
#
# th8AfIsWildcard recognises five wildcard characters
# (`* # ! $ @`); the `flags have` command path drives them via
# Th8_AttrFlagsHave -> th8AfWildcardHaveAll (when -all is set)
# or th8AfWildcardHaveAny (default).  The existing flags-4.x
# tests in tests/flags.tcl only exercise the `*` wildcard
# through `flags have`, so the other four cases in BOTH
# functions show count 0 in the mcdc-uncovered output.
#
# Wildcard semantics:
#   '*' -- all alphanumerics  (already covered by flags-4.x)
#   '#' -- digits 0-9
#   '!' -- letters (A-Z, a-z)
#   '$' -- uppercase only (A-Z)
#   '@' -- lowercase only (a-z)
#
# For each of the four uncovered wildcards we drive both
# branches (bAll=0 via plain `flags have`, bAll=1 via
# `flags have -all`).  In each case the flag set contains
# the entire wildcard class so the test returns 1 in both
# any/all modes -- this is the simplest stimulus that
# guarantees the switch case body executes its full inner
# loop without short-circuiting on the very first iteration.
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

runTest {test flags_have_wc-1.1 {
  th8_attrflags.c L892 th8AfWildcardHaveAny case '#' --
  flags have with '#' wildcard (digits) on a flag set that
  contains every digit returns 1.
} -constraints {
    th8 flags
} -setup {
  set digits [flags change {} +#]
} -body {
  flags have $digits #
} -cleanup {
  unset -nocomplain digits
} -result {1}}

###############################################################################

runTest {test flags_have_wc-1.2 {
  th8_attrflags.c L883 th8AfWildcardHaveAll case '#' --
  flags have -all with '#' on a digit-only flag set returns
  1 (every digit is present so the inner loop runs to
  completion).
} -constraints {
    th8 flags
} -setup {
  set digits [flags change {} +#]
} -body {
  flags have -all $digits #
} -cleanup {
  unset -nocomplain digits
} -result {1}}

###############################################################################

runTest {test flags_have_wc-2.1 {
  th8_attrflags.c L895 th8AfWildcardHaveAny case '!' --
  flags have with '!' wildcard (all letters) on a flag set
  containing every letter returns 1.
} -constraints {
    th8 flags
} -setup {
  set letters [flags change {} {+!}]
} -body {
  flags have $letters {!}
} -cleanup {
  unset -nocomplain letters
} -result {1}}

###############################################################################

runTest {test flags_have_wc-2.2 {
  th8_attrflags.c L895 th8AfWildcardHaveAll case '!' --
  flags have -all with '!' wildcard returns 1 on an all-
  letters flag set.
} -constraints {
    th8 flags
} -setup {
  set letters [flags change {} {+!}]
} -body {
  flags have -all $letters {!}
} -cleanup {
  unset -nocomplain letters
} -result {1}}

###############################################################################

runTest {test flags_have_wc-3.1 {
  th8_attrflags.c L901 th8AfWildcardHaveAny case '$' --
  flags have with '$' wildcard (uppercase) on a flag set
  containing every uppercase letter returns 1.
} -constraints {
    th8 flags
} -setup {
  set upper [flags change {} {+$}]
} -body {
  flags have $upper {$}
} -cleanup {
  unset -nocomplain upper
} -result {1}}

###############################################################################

runTest {test flags_have_wc-3.2 {
  th8_attrflags.c L901 th8AfWildcardHaveAll case '$' --
  flags have -all with '$' on an uppercase-only flag set
  returns 1.
} -constraints {
    th8 flags
} -setup {
  set upper [flags change {} {+$}]
} -body {
  flags have -all $upper {$}
} -cleanup {
  unset -nocomplain upper
} -result {1}}

###############################################################################

runTest {test flags_have_wc-4.1 {
  th8_attrflags.c L905 th8AfWildcardHaveAny case '@' --
  flags have with '@' wildcard (lowercase) on a flag set
  containing every lowercase letter returns 1.
} -constraints {
    th8 flags
} -setup {
  set lower [flags change {} +@]
} -body {
  flags have $lower @
} -cleanup {
  unset -nocomplain lower
} -result {1}}

###############################################################################

runTest {test flags_have_wc-4.2 {
  th8_attrflags.c L905 th8AfWildcardHaveAll case '@' --
  flags have -all with '@' on a lowercase-only flag set
  returns 1.
} -constraints {
    th8 flags
} -setup {
  set lower [flags change {} +@]
} -body {
  flags have -all $lower @
} -cleanup {
  unset -nocomplain lower
} -result {1}}

###############################################################################

source tests/epilogue.tcl
