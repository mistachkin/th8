###############################################################################
#
# coverage_flags_colon.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L597 -- the
# "colon outside braces in complex mode" check
# `if (c == AF_NAME_SEP && bComplex)`.  Existing tests cover
# C1=F (no colon) and (T,T) (colon in -complex mode); none drive
# C2=F (colon present in non-complex mode), which is a
# common-mistake error path -- the parser sees the colon, the
# compound short-circuits, and the subsequent
# `!th8AfIsIdentChar(':')` check fires with "invalid flag
# character".
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

runTest {test fl_colon-1.1 {
  [flags have "x:y" haveFlags] (no -complex flag) drives
  src/plugins/harpy/th8_attrflags.c L597 vector (T,F) --
  c==':' but bComplex==0.  The colon does not match the
  name-separator branch and falls through to
  `!th8AfIsIdentChar(':')` which errors with "invalid
  flag character".
} -constraints {
    th8
} -setup {
} -body {
  set rc [catch {flags have "x:y" "a"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test fl_colon-2.1 {
  [flags have "x|y" haveFlags] passes a '|' character
  (ASCII 124).  th8AfIsIdentChar('|') evaluates the 7-
  condition ident-class compound at src/plugins/harpy/
  th8_attrflags.c L143-146 with C1=T (124>='0'), C2=F
  (124>'9'), C3=T (124>='a'), C4=F (124>'z'), driving
  the C4-pair (T,F).  The function returns F so the
  outer flag parser errors with "invalid flag
  character".
} -constraints {
    th8
} -setup {
} -body {
  set rc [catch {flags have "x|y" "a"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test fl_colon-2.2 {
  [flags have "x^y" haveFlags] passes a '^' character
  (ASCII 94).  th8AfIsIdentChar('^') evaluates the
  compound at L143-146 with C1=T (94>='0'), C2=F
  (94>'9'), C3=F (94<'a'), C5=T (94>='A'), C6=F
  (94>'Z'), C7=F (94!='_'), driving the C6-pair (T,F).
  Result F, flag parser errors.
} -constraints {
    th8
} -setup {
} -body {
  set rc [catch {flags have "x^y" "a"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test fl_colon-2.3 {
  [flags have "_" haveFlags] -- the underscore
  character is the C7-pair driver.  Existing vector
  (T,F,F,-,T,F,T) was hit via colon (already covered)
  but the F-result variant requires a non-underscore
  in same position.  Underscore IS a valid ident char
  (C7=T) so this is the happy path -- the function
  returns T (1) and "_" is treated as a valid flag
  identifier.
} -constraints {
    th8
} -setup {
} -body {
  set r [flags have "_" "_"]
  set r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
