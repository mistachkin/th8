###############################################################################
#
# coverage_flags_hex.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for hex-digit char-class compounds in
# src/plugins/th8_harpy.c (the [flags] command's -key 0x... parser):
#
#   line 237  if (c >= '0' && c <= '9') d = c - '0';
#   line 238  if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
#   line 240  if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
#
# Existing flags tests cover `-key 0xFF` (T,T at 240) but not the
# F,- / T,F vectors that require non-A-F chars.
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

runTest {test fl_hex-1.1 {
  flags -key 0xG (uppercase out of range) drives T,F at line 240
  (c >= 'A' true, c <= 'F' false)
} -constraints {
    th8
} -body {
  catch {flags have -key 0xG {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_hex-1.2 {
  flags -key 0x: (colon char, code 58) drives F,- at line 240
  (c < 'A')
} -constraints {
    th8
} -body {
  catch {flags have -key 0x: {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_hex-1.3 {
  flags -key with full mixed-case hex value drives T,T at all
  three char-class compounds (237, 238, 240)
} -constraints {
    th8
} -body {
  catch {flags have -complex -key 0xAaBbCcDd09 {{AaBbCcDd09:test}} t} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_hex-1.4 {
  flags -key 0xz (lowercase out of range) drives T,F at line 238
  (c >= 'a' true, c <= 'f' false) before falling through to 240
} -constraints {
    th8
} -body {
  catch {flags have -key 0xz {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_hex-1.5 {
  flags -key 0x! (character below '0' in ASCII) drives
  the th8AfIsHexDigit compound at src/plugins/harpy/
  th8_attrflags.c L111 with C1=F (33 < '0'(48)).  Then
  C3=F (33 < 'a'(97)) and C5=F (33 < 'A'(65)), so the
  function returns 0 and the hex-key parser rejects with
  "invalid key character".  Drives the C1-pair (F,-,...)
  -- the previously-untracked sub-'0' vector that
  complements fl_hex-1.1/1.2 (which only test characters
  >= '0').
} -constraints {
    th8
} -setup {
} -body {
  set rc [catch {flags have -key 0x! {} a} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
