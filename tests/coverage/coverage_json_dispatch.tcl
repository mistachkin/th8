###############################################################################
#
# coverage_json_dispatch.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for [json] subcommand dispatch decisions in
# src/sqlite3/th8_sqlite3.c:
#
#   L1927  argl[1] == 5 && memcmp("error", 5)
#   L1955  argl[1] == 4 && memcmp("keys",  4)
#   L1974  argl[1] == 6 && memcmp("values"/"pretty", 6)
#
# Each compound is `length-match && content-match`.  Existing tests
# cover the length-mismatch (C1=F) and full-match (T,T) paths.
# The C2-pair (length matches, content does not) fires when an
# unknown subcommand happens to be the same byte length as a known
# one: those calls must traverse the length-equal-but-content-
# mismatched branch before erroring out at the dispatch fallthrough.
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

runTest {test json_dispatch-1.1 {
  Unknown json subcommand of length 5 (same as "error")
  drives src/sqlite3/th8_sqlite3.c L1927 C2-pair.
  Existing tests use either the real "error" subcommand
  (T,T) or shorter / longer unknown names (length-mismatch,
  C1=F); a 5-byte unknown reaches the memcmp and gets the
  (T,F) vector.
} -constraints {
    th8 json
} -setup {
} -body {
  set rc [catch {json abcde "{}"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test json_dispatch-1.2 {
  Unknown json subcommand of length 4 (same as "keys")
  drives src/sqlite3/th8_sqlite3.c L1955 C2-pair via a
  4-byte name that is not "keys".
} -constraints {
    th8 json
} -setup {
} -body {
  set rc [catch {json abcd "{}"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test json_dispatch-1.3 {
  Unknown json subcommand of length 6 (same as "values"
  and "pretty") drives src/sqlite3/th8_sqlite3.c L1974
  C2-pair.  A single 6-byte unknown name traverses BOTH
  the "pretty" check at L1941 and the "values" check at
  L1974 with the same (T,F) vector.
} -constraints {
    th8 json
} -setup {
} -body {
  set rc [catch {json abcdef "{}"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
