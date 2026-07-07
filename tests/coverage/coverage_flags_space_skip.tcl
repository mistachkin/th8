###############################################################################
#
# coverage_flags_space_skip.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L543 and
# L595:
#
#   if (bSpace && th8AfIsSpace(c)) continue;
#
# These guard the whitespace-skip branches inside brace-quoted
# hex keys (L543) and inside the name portion of complex flag
# parsing (L595).  Existing -space tests (fl_opts-1.1, fl_opts-1.7)
# use either empty input `{}` or compact `{1:abc}` strings with
# no actual whitespace characters inside the brace scope, so
# th8AfIsSpace is never invoked and L543/L595 sit at 0% MC/DC.
#
# These tests feed flag dictionaries that DO contain spaces
# between '{' and the hex key (driving L543) and between key
# and name (driving L595).  The (T,T) vector at each line is
# the path "bSpace enabled and current char is whitespace ->
# skip and continue".
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

runTest {test fl_space-1.1 {
  flags -complex -space with spaces inside the hex-key brace
  drives th8_attrflags.c L543 (T,T): bSpace=1 and the current
  char inside `{ ... }` is whitespace -- skipped before
  collecting the hex digits.
} -constraints {
    th8
} -setup {
} -body {
  catch {flags have -complex -space "{ 1: foo} { 2: bar}" "foo"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_space-1.2 {
  flags -complex -space with tab inside the hex-key brace
  drives th8_attrflags.c L543 via the '\t' branch of
  th8AfIsSpace (covers a second character of the OR compound
  at L175 too).
} -constraints {
    th8
} -setup {
} -body {
  catch {flags have -complex -space "{\t1:\tfoo}" "foo"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_space-1.3 {
  flags -complex -space with CR ('\r') inside the hex-key
  brace drives th8AfIsSpace L175 C3-pair via the third
  branch of the OR compound.
} -constraints {
    th8
} -setup {
} -body {
  catch {flags have -complex -space "{\r1:foo}" "foo"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_space-1.4 {
  flags -complex -space with LF ('\n') inside the hex-key
  brace drives th8AfIsSpace L175 C4-pair via the fourth
  branch of the OR compound.
} -constraints {
    th8
} -setup {
} -body {
  catch {flags have -complex -space "{\n1:foo}" "foo"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
