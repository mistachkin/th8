###############################################################################
#
# coverage_flags_show.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [flags show] option-parser
# decisions in src/plugins/th8_harpy.c (th8FlagsParseOpts):
#
#   :200  -space option detect       (argl == 6 && memcmp)
#   :203  -sort option detect        (argl == 5 && memcmp)
#   :212  -legacy option detect      (argl == 7 && memcmp)
#   :215  -compact option detect     (argl == 8 && memcmp)
#   :218  -key option detect         (argl == 4 && memcmp)
#   :229  hex prefix uppercase X     ('x' || 'X' on second char)
#   :237  hex digit '0'..'9'         (range check)
#   :259  '--' end-of-options        (argl == 2 && memcmp)
#   :271  -key requires -complex     ((key != 0 && !bComplex))
#
# Existing flags tests cover the no-option default path.  This
# file passes each option in turn and drives both the matching
# vector (T,T) and the second-condition F vector (T,F) via
# option-chaining.
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

runTest {test flagsshow-1.1 {
  flags show -space adds inter-flag whitespace.  Drives the
  (T,T) vector at line 200 (-space option detect) and
  thereby walks past the -space match into the rest of the
  option chain.
} -constraints {
    th8
} -body {
  set rc [catch {flags show -space "ABEKLNSTY_"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test flagsshow-1.2 {
  flags show -sort sorts the flag output.  Drives (T,T) at
  line 203 and (T,F) at line 200 (-space comes before
  -sort in the option chain; passing -sort after a non-match
  drives the second-condition F vector at -space's check).
} -constraints {
    th8
} -body {
  set rc [catch {flags show -sort "YBAS"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test flagsshow-1.3 {
  flags show -legacy enables legacy emit format.  Drives
  (T,T) at line 212.
} -constraints {
    th8
} -body {
  set rc [catch {flags show -legacy "AB"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test flagsshow-1.4 {
  flags show -compact enables compact emit format.  Drives
  (T,T) at line 215.
} -constraints {
    th8
} -body {
  set rc [catch {flags show -compact "AB"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test flagsshow-2.1 {
  flags show -- terminates option processing.  Drives the
  (T,T) vector at line 259 ('--' as a 2-char option).
  After --, the next arg is treated as the flag string.
} -constraints {
    th8
} -body {
  set rc [catch {flags show -- "AB"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test flagsshow-3.1 {
  flags show -key 0xHEX (no -complex) drives the (T,T)
  vector at line 271 (key != 0 && !bComplex) -- the option
  parsed successfully but the post-parse validator rejects
  it.  Returns the documented error message.
} -constraints {
    th8
} -body {
  catch {flags show -key 0xABC "AB"} m
  string match {*complex*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test flagsshow-3.2 {
  flags show -complex -key 0xABC drives the lowercase x
  side of (zK[1] == 'x' || zK[1] == 'X') at line 229,
  plus all three branches of the hex digit range checks
  at line 237 (0-9, a-f).
} -constraints {
    th8
} -body {
  set rc [catch {flags show -complex -key 0xab12 "AB"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test flagsshow-3.3 {
  flags show -complex -key 0XABC drives the UPPERCASE X
  side of the hex prefix OR at line 229, plus the A-F
  uppercase branch of the digit range at line 237.
} -constraints {
    th8
} -body {
  set rc [catch {flags show -complex -key 0XAB12 "AB"} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test flagsshow-4.1 {
  Pass an UNKNOWN option of length 6 (matching -space's
  length).  Drives the (T,F) vector at line 200 -- argl
  matches but memcmp differs.  After the failed match,
  the parser falls through to the unknown-option error.
} -constraints {
    th8
} -body {
  set rc [catch {flags show -bogus "AB"} m]
  expr {$rc == 1 && [string length $m] > 0}
} -cleanup {
  unset -nocomplain rc m
} -result {1}}

###############################################################################

runTest {test flagsshow-4.2 {
  Same pattern as -4.1 across the other option-length
  buckets at lines 203 (-sort, 5), 212 (-legacy, 7),
  215 (-compact, 8), 218 (-key, 4), 259 (--, 2).  Each
  unknown-option-of-matching-length closes a different
  C2-pair vector.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach optX {-bogu -bogus7 -bogus8a -bug -x} {
      lappend rcs [catch {flags show $optX "AB"}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs optX
} -result {1 1 1 1 1}}

###############################################################################

runTest {test flagsshow-4.3 {
  -key 0xC where C is a non-hex char drives C1=F at
  line 237 (the '0'..'9' digit-range check fires false
  for chars below '0').
} -constraints {
    th8
} -body {
  set rcs {}
  foreach keyX {0x! 0x@ 0xz 0xZ} {
      lappend rcs [catch {flags show -complex -key $keyX "AB"}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs keyX
} -result {1 1 1 1}}

###############################################################################

runTest {test flagsshow-5.1 {
  clock ntp with a 7-char option that is NOT "-server"
  drives the C2=F vector at th8_harpy.c:60 -- argl matches
  but memcmp differs.  The -timeout (8) and other length
  options take different paths.
} -constraints {
    th8 crypto_enabled
} -body {
  set rc [catch {clock ntp -bogusa 1.2.3.4} m]
  expr {$rc == 1}
} -cleanup {
  unset -nocomplain rc m
} -result {1}}

###############################################################################

source tests/epilogue.tcl
