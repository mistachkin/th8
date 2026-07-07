###############################################################################
#
# coverage_flags_wildcards.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L788
# `th8AfIsWildcard`: the 5-way OR compound recognizes '*', '#',
# '!', '$', '@' as wildcard expansion characters.  Existing tests
# cover '*' (C1=T) and '#' (C2=T); these tests drive '!', '$', '@'
# which expand to letters-only, uppercase-only, and lowercase-only
# respectively.
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

runTest {test fl_wild-1.1 {
  flags change {} +! adds all letters (A-Z, a-z).  Drives
  the C3=T vector at L788.  Result length = 52 (26 + 26).
} -constraints {
    th8
} -body {
  string length [flags change {} +!]
} -result {52}}

###############################################################################

runTest {test fl_wild-1.2 {
  flags change {} +$ adds uppercase letters (A-Z).
  Drives the C4=T vector at L788.  Result length = 26.
} -constraints {
    th8
} -body {
  string length [flags change {} +$]
} -result {26}}

###############################################################################

runTest {test fl_wild-1.3 {
  flags change {} +@ adds lowercase letters (a-z).
  Drives the C5=T vector at L788.  Result length = 26.
} -constraints {
    th8
} -body {
  string length [flags change {} +@]
} -result {26}}

###############################################################################

runTest {test fl_wild-2.1 {
  flags have -strict "a" "*" drives src/plugins/harpy/
  th8_attrflags.c L1024 C2-pair (T,F) -- inside the
  strict-validation loop, the haveFlags character '*' is
  not an ident char (C1=T) BUT is a wildcard (C2=F via
  !isWildcard==F), so the if body is skipped instead of
  rejecting.  Non-empty flags ensure pFs is allocated and
  the validation loop is reached (empty flags fast-paths
  out at L1019 returning 0 before the loop).
} -constraints {
    th8
} -body {
  # Result is 1 because the default `have` semantics
  # match when ANY expanded wildcard char is present
  # ("a" is in the alphanumeric expansion of "*").
  flags have -strict "a" "*"
} -result {1}}

###############################################################################

source tests/epilogue.tcl
