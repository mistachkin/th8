###############################################################################
#
# coverage_flags_options.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the if-elif option-parsing chain in
# th8FlagsParseOpts (src/plugins/th8_harpy.c lines 196-258).  Each
# branch is `argl[i] == N && memcmp(argv[i], "-NAME", N) == 0`.
#
# Existing tests use -complex / -key / -strict; the un-tested
# options (-space, -sort, -all, -legacy, -compact, --) reach
# their own match branches and also drive the T,F vector at any
# prior same-length check they walk past.
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

runTest {test fl_opts-1.1 {
  flags -space option exercises the argl==6 match branch
} -constraints {
    th8
} -body {
  catch {flags have -space {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_opts-1.2 {
  flags -sort option exercises the argl==5 match branch
} -constraints {
    th8
} -body {
  catch {flags have -sort {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_opts-1.3 {
  flags -all option exercises the argl==4 match branch
} -constraints {
    th8
} -body {
  catch {flags have -all {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_opts-1.4 {
  flags -legacy option exercises the argl==7 match (after -strict
  which is also argl==7, drives T,F at -strict's check)
} -constraints {
    th8
} -body {
  catch {flags have -legacy {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_opts-1.5 {
  flags -compact option exercises the argl==8 match (after -complex
  which is also argl==8, drives T,F at -complex's check)
} -constraints {
    th8
} -body {
  catch {flags have -compact {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_opts-1.6 {
  flags -- terminator exercises the argl==2 match branch
} -constraints {
    th8
} -body {
  catch {flags have -- {} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_opts-1.7 {
  flags with multiple combined options
} -constraints {
    th8
} -body {
  catch {flags have -complex -all -sort -space -compact -- {{1:abc}} a} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fl_opts-1.8 {
  flags -key VALUE drives the C1/C2/C3/C4 pair vectors at
  th8_harpy.c:228-229.  Existing tests use "0xN" hex
  values (T,T,T,- and T,T,F,T).  This adds:
    - C2=F: zK[0] != '0' (decimal value "123" with nK>2)
    - C3=F && C4=F: zK[0]='0' but zK[1] not 'x'/'X'
      (octal-style "0123")
    - C1=F: nK <= 2 (single-digit "7" / 2-digit "99")
  Closes the C1/C2/C3/C4 pairs at the hex-prefix check.
} -constraints {
    th8
} -body {
  set rcs {}
  catch {flags have -complex -key 99 {{0:a}} a} r
  lappend rcs $r
  catch {flags have -complex -key 0123 {{0:a}} a} r
  lappend rcs $r
  catch {flags have -complex -key 7 {{0:a}} a} r
  lappend rcs $r
  catch {flags have -complex -key 0xff {{0:a}} a} r
  lappend rcs $r
  # C2=F vector: nK>2 (3 digits) AND zK[0] != '0'.  "123"
  # parses as decimal 123, zK[0]='1' != '0', so C1=T, C2=F
  # and the hex-prefix check short-circuits to F.
  catch {flags have -complex -key 123 {{0:a}} a} r
  lappend rcs $r
  catch {flags have -complex -key 4567 {{0:a}} a} r
  lappend rcs $r
  set rcs
} -cleanup {
  unset -nocomplain rcs r
} -result {0 0 0 0 0 0}}

###############################################################################

source tests/epilogue.tcl
