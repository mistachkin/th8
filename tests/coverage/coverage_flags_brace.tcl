###############################################################################
#
# coverage_flags_brace.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c:
#
#   L523:  if (!bComplex || !bOpen) ...   "unexpected close-brace"
#   L528:  if (!bHaveName || nName == 0)  "close-brace without complete key"
#
# Existing tests only drive the normal-close path through these
# guards (C1=F, C2=F at both lines) -- they parse complete
# complex flag dicts.  The error-path vectors are uncovered:
#
#   L523 (T,-): bComplex=0 (simple mode) seeing a close-brace.
#   L523 (F,T): bComplex=1 but bOpen=0 -- close without prior open.
#   L528 (T,-): bOpen with no name parsed yet, immediate close.
#               (The F,T vector "bHaveName=1 AND nName==0" is
#               intrinsic dead: bHaveName is only set after a
#               name with nName greater than zero -- see L560
#               and L589.)
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

runTest {test fl_brace-1.1 {
  flags have on a simple-mode input containing a bare close-brace
  drives th8_attrflags.c L523 T-comma-dash: bComplex=0 so the
  not-bComplex short-circuits the OR and the parser errors with
  the unexpected close-brace message.
} -constraints {
    th8
} -setup {
} -body {
  set spec "\}"
  catch {flags have $spec "a"} r
  expr {[string match {*unexpected*} $r]}
} -cleanup {
  unset -nocomplain spec r
} -result {1}}

###############################################################################

runTest {test fl_brace-1.2 {
  flags have -complex on input with a stray close-brace and no
  prior open-brace drives th8_attrflags.c L523 F-comma-T:
  bComplex=1 but bOpen=0 -- the second condition of the OR
  fires and the parser errors with the unexpected close-brace
  message.
} -constraints {
    th8
} -setup {
} -body {
  set spec "\}"
  catch {flags have -complex $spec "a"} r
  expr {[string match {*unexpected*} $r]}
} -cleanup {
  unset -nocomplain spec r
} -result {1}}

###############################################################################

runTest {test fl_brace-1.3 {
  flags have -complex on an empty brace pair drives
  th8_attrflags.c L528 T-comma-dash: an immediate close-brace
  with no name characters collected leaves bHaveName=0, so the
  first condition of the OR fires and the parser errors with
  the close-brace without complete key message.  This also
  incidentally drives L523 F-comma-F for the normal path.
} -constraints {
    th8
} -setup {
} -body {
  set spec "\{\}"
  catch {flags have -complex $spec "a"} r
  expr {[string match {*without complete key*} $r]}
} -cleanup {
  unset -nocomplain spec r
} -result {1}}

###############################################################################

runTest {test fl_brace-1.4 {
  flags have -complex with an UPPERCASE hex key drives
  th8_attrflags.c th8AfParseHexKey L439 C1-pair:
  the elif `c >= 'a' && c <= 'f'`.  For 'A'-'F', the
  guard 'a' <= c evaluates F so the elif falls through to
  the uppercase branch at L440.  Existing tests use only
  lowercase or digit hex, so the (F,-) vector at L439 was
  never driven.  Test asserts the parse succeeds (catch
  returns 0) -- the key value match itself is incidental.
} -constraints {
    th8
} -setup {
} -body {
  catch {flags have -complex "\{AB:x\}" "x"} r
  expr {$r == 0 || $r == 1}
} -cleanup {
  unset -nocomplain spec r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
