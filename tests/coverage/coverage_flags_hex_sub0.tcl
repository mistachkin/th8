###############################################################################
#
# coverage_flags_hex_sub0.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L111-113
# th8AfIsHexDigit -- a triple-OR predicate with 6 conditions
# across three char-range tests:
#
#   return (c >= '0' && c <= '9')          // C1, C2
#       || (c >= 'a' && c <= 'f')          // C3, C4
#       || (c >= 'A' && c <= 'F');         // C5, C6
#
# Existing tests pass chars from each hex range plus chars
# strictly ABOVE 'F' / 'f' / '9' (drives the C2/C4/C6 lower-
# bound-passed-but-upper-bound-failed vectors).  No existing
# test passes a char strictly BELOW '0' (0x30), which is the
# only way to drive C1=F (which then short-circuits, evaluates
# C3 with F, then C5 with F -- closing all three lower-bound
# pairs in a single vector).
#
# The driver is `flags change -complex "{!:flag}" +z`, which
# routes through Th8_AttrFlagsParse's complex-mode brace key
# scanner at L565.  The first key char ('!', 0x21) fails the
# IsHexDigit check, yielding the "flags: invalid key character"
# error -- exactly the path that exposes the (F,-,F,-,F,-)
# vector to the MC/DC instrumentation.
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

runTest {test fl_hex_sub0-1.1 {
  flags change -complex with a sub-'0' key char ('!' = 0x21)
  drives th8AfIsHexDigit's C1=F, C3=F, C5=F vector at L111-113.
  Closes C1-Pair / C3-Pair / C5-Pair simultaneously.
} -constraints {
    th8
} -body {
  catch {flags change -complex "\{!:flag\}" +z} msg
  string match "*invalid key character*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test fl_hex_sub0-1.2 {
  flags have -complex with sub-'0' key char (space = 0x20) --
  alternate driver via the have-flags command path.  Confirms
  the IsHexDigit path is reached regardless of which
  Th8_AttrFlagsParse entry point is used.
} -constraints {
    th8
} -body {
  catch {flags have -complex "\{ :x\}" a} msg
  string match "*invalid*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test fl_hex_sub0-1.3 {
  flags change -complex with a between-'9'-and-'A' key char
  (';' = 0x3b) drives C3=F (c < 'a') AND C5=F (c < 'A') at
  th8AfIsHexDigit L112/L113 -- the (T,F,F,-,F,-) vector.
  Combined with the existing (T,F,F,-,T,T) and other
  T-prefix vectors, closes C5-Pair (was: only C5=T vectors
  recorded among C1=T,C2=F,C3=F prefixes).
} -constraints {
    th8
} -body {
  catch {flags change -complex "\{;:flag\}" +z} msg
  string match "*invalid key character*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test fl_hex_sub0-1.4 {
  flags change -complex with a between-'F'-and-'a' key char
  ('G' = 0x47) drives C5=T,C6=F at L113 with C1=T,C2=F,C3=F --
  the (T,F,F,-,T,F) vector.  Closes C6-Pair (was: only C6=T
  vectors recorded).
} -constraints {
    th8
} -body {
  catch {flags change -complex "\{G:flag\}" +z} msg
  string match "*invalid key character*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test fl_hex_sub0-1.5 {
  flags change -complex with a between-'F'-and-'a' key char
  that is NOT a hex digit and falls in the C3-Pair gap.
  Use '?' = 0x3f -- gives (T,F,F,-,F,-), and combined with
  existing (T,F,T,T,-,-) [vector 4] satisfies the C3-Pair
  unique-cause requirement when paired with a complementary
  T-flipping vector recorded elsewhere.  Belt-and-braces.
} -constraints {
    th8
} -body {
  catch {flags change -complex "\{?:flag\}" +z} msg
  string match "*invalid key character*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

source tests/epilogue.tcl
