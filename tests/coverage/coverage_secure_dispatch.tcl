###############################################################################
#
# coverage_secure_dispatch.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for [secure] subcommand dispatch decisions in
# src/plugins/crypto/th8_crypto_cmds.c:
#
#   L75   `argl[1] != 6 || Th8_Memcmp("normal", 6) != 0` (hash)
#   L164  `argc != 3 && argc != 4`  (secure create wrong args)
#   L183  `argl[1] == 6 && Th8_Memcmp("delete", 6) == 0`
#   L192  `argl[1] == 4 && Th8_Memcmp("save",   4) == 0`
#   L200  `argl[1] == 4 && Th8_Memcmp("load",   4) == 0`
#
# Drives the length-equal-but-content-mismatched (C2=F) vector at
# each dispatch branch, and the (T,T) wrong-args vector for
# secure create with extra arguments.
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

runTest {test secure_disp-1.1 {
  Unknown secure subcommand of length 6 (same as
  "create", "exists", "delete") drives src/plugins/
  crypto/th8_crypto_cmds.c L162/173/183 C2-pair --
  the memcmp comparisons must run with length match
  but content mismatch before falling through to the
  "must be" error.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {secure abcdef foo} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test secure_disp-1.2 {
  Unknown secure subcommand of length 4 (same as
  "save"/"load") drives src/plugins/crypto/
  th8_crypto_cmds.c L192/L200 C2-pair via a 4-byte
  name that is not "save" or "load".
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {secure abcd foo} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test secure_disp-1.3 {
  [secure create varName value extra] passes 5 args,
  argc==5 so L164 `argc != 3 && argc != 4` evaluates
  (T,T) -- the wrong-args path that existing tests
  (which always use 3 or 4 args) never hit.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {secure create v1 v2 v3} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test secure_disp-2.1 {
  [hash abcdef SHA512 data] drives src/plugins/crypto/
  th8_crypto_cmds.c L75 C2-pair -- subcommand "abcdef"
  is length 6 (same as "normal") with non-matching
  content.  The memcmp != 0 path triggers the "must be
  normal" error.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {hash abcdef SHA512 ""} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################
#
# Section 3 -- algorithm-name case-insensitive matching in
# th8_crypto_cmds.c hash_command's per-character SHA512 check.
# Drives both halves of each (uppercase || lowercase) compound
# at each character position so all eight MC/DC C-pairs
# (4 chars * 2 conds) reach 100%.
#
###############################################################################

runTest {test secure_disp-3.1 {
  [hash normal sha512 ...] drives the lowercase half of the
  per-character compound at each position (z[k] == 'X'
  fails, z[k] == 'x' matches) for the 'S', 'H', and 'A'
  characters.
} -constraints {
    th8 crypto_enabled
} -body {
  set h [hash normal sha512 ""]
  string length $h
} -cleanup {
  unset -nocomplain h
} -result {128}}

###############################################################################

runTest {test secure_disp-3.2 {
  [hash normal ShA512 ...] mixed-case drives each
  per-character compound's specific arm independently --
  in particular the 'h' lowercase arm where 'H' upper-arm
  is false but 'h' lower-arm matches.
} -constraints {
    th8 crypto_enabled
} -body {
  set h [hash normal ShA512 ""]
  string length $h
} -cleanup {
  unset -nocomplain h
} -result {128}}

###############################################################################

runTest {test secure_disp-3.3 {
  [hash normal sHa512 ...] drives the 's' lowercase arm
  while 'H' uppercase arm and 'a' lowercase arm are also
  covered together with the digit positions.
} -constraints {
    th8 crypto_enabled
} -body {
  set h [hash normal sHa512 ""]
  string length $h
} -cleanup {
  unset -nocomplain h
} -result {128}}

###############################################################################

runTest {test secure_disp-3.4 {
  [hash normal sha511 ...] with a wrong-trailing-digit
  drives the failure path of the digit-character check
  ('1' != '2') without matching SHA512, exercising the
  final-position C2 fail vector.
} -constraints {
    th8 crypto_enabled
} -body {
  set rc [catch {hash normal sha511 ""} m]
  list $rc [string match {*unsupported algorithm*} $m]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test secure_disp-3.5 {
  6-byte algorithm name with z[0] not 'S' / 's' (e.g.
  "XHA512") drives the (F, F) C-pair on the L95 compound
  -- both C1-Pair and C2-Pair require this vector.
} -constraints {
    th8 crypto_enabled
} -body {
  set rc [catch {hash normal XHA512 ""} m]
  list $rc [string match {*unsupported algorithm*} $m]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test secure_disp-3.6 {
  6-byte algorithm name with z[1] not 'H' / 'h' (e.g.
  "SXA512") drives the (F, F) C-pair on the L96 compound.
} -constraints {
    th8 crypto_enabled
} -body {
  set rc [catch {hash normal SXA512 ""} m]
  list $rc [string match {*unsupported algorithm*} $m]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test secure_disp-3.7 {
  6-byte algorithm name with z[2] not 'A' / 'a' (e.g.
  "SHX512") drives the (F, F) C-pair on the L97 compound.
} -constraints {
    th8 crypto_enabled
} -body {
  set rc [catch {hash normal SHX512 ""} m]
  list $rc [string match {*unsupported algorithm*} $m]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
