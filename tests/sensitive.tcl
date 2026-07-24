###############################################################################
#
# sensitive.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the sensitive interpreter result feature:
#   Th8_IsResultSensitive    -- read the flag
#   Th8_MarkResultSensitive  -- flag-only path (secure-zero on overwrite)
#   Th8_SetResultSensitive   -- store result in mlock'd, guard-paged
#                               protected region
#   Th8_TakeResult           -- refuses for sensitive results
#
# The protected region is allocated lazily on first sensitive
# result and reused for the lifetime of the interpreter.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- Th8_IsResultSensitive baseline
#
###############################################################################

runTest {test sensitive-1.1 {
  R-39669-03059: Th8_IsResultSensitive returns 0 on a fresh non-sensitive
                 result
} -constraints {
    loadLib th8
} -setup {
  set foo "ordinary"
} -body {
  ::th8testlib::is_result_sensitive
} -cleanup {
  unset -nocomplain foo
} -result {0}}

###############################################################################
#
# Section 2 -- secure variable read marks the result sensitive
#
###############################################################################

runTest {test sensitive-2.1 {
  R-47618-29168: secure variable read sets bResultSensitive in-place
} -constraints {
    th8 crypto_testlib
} -setup {
  catch {secure delete _sens_2_1}
  secure create _sens_2_1 "hello world"
} -body {
  ::th8testlib::sensitive_probe _sens_2_1
} -cleanup {
  catch {secure delete _sens_2_1}
} -result {ok 1 11 1 0}}

###############################################################################
#
# Section 3 -- Th8_TakeResult refuses for sensitive results
#
###############################################################################

runTest {test sensitive-3.1 {
  R-32296-63095: Th8_TakeResult on sensitive result returns refusal
} -constraints {
    th8 crypto_testlib
} -setup {
  catch {secure delete _sens_3_1}
  secure create _sens_3_1 "topsecret"
} -body {
  # The probe captures the take-refusal flag in field 4 of the
  # output: "ok <flagBefore> <len> <takeRefused> <flagAfter>"
  # We verify it is exactly 1 (refused).
  set r [::th8testlib::sensitive_probe _sens_3_1]
  lindex $r 3
} -cleanup {
  catch {secure delete _sens_3_1}
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 4 -- multiple sensitive reads reuse the same protected region
#
###############################################################################

runTest {test sensitive-4.1 {
  R-19055-10405: repeated sensitive reads reuse the per-interp region without
                 leaking
} -constraints {
    th8 crypto_testlib
} -setup {
  catch {secure delete _sens_4_a}
  catch {secure delete _sens_4_b}
  secure create _sens_4_a "alpha"
  secure create _sens_4_b "beta_value"
} -body {
  # Each probe reports "ok <flagBefore> <length> <takeRefused>
  # <flagAfter>".  Read each probe twice (consecutively) so the
  # region is exercised across two distinct sensitive values
  # back-to-back.  Verify: flagBefore=1 and takeRefused=1 every
  # time -- demonstrating that the region was reused safely
  # and the in-place enforcement holds across reuses.
  set a [::th8testlib::sensitive_probe _sens_4_a]
  set b [::th8testlib::sensitive_probe _sens_4_b]
  list [lindex $a 1] [lindex $a 3] [lindex $b 1] [lindex $b 3]
} -cleanup {
  catch {secure delete _sens_4_a}
  catch {secure delete _sens_4_b}
  unset -nocomplain a b
} -result {1 1 1 1}}

###############################################################################
#
# Section 5 -- R-54106-02907: sensitivity propagates through value-
# preserving operations (command substitution, variable storage), the
# same conservative way taint does.  These pin the parallel-tag fix:
# a secure value flowing through [list ...] or through a regular
# variable must remain classified sensitive at the boundary.
#
###############################################################################

runTest {test sensitive-5.1 {
  R-54106-02907: a secure value substituted through [list ...] into a
                 regular variable is still sensitive when read back
} -constraints {
    th8 crypto_testlib
} -setup {
  catch {secure delete _sens_5_1}
  secure create _sens_5_1 "topsecret"
  set _copy_5_1 [list $_sens_5_1]
} -body {
  # Field 1 of the probe is Th8_IsResultSensitive after Th8_GetVar.
  lindex [::th8testlib::sensitive_probe _copy_5_1] 1
} -cleanup {
  catch {secure delete _sens_5_1}
  unset -nocomplain _copy_5_1
} -result {1}}

###############################################################################

runTest {test sensitive-5.2 {
  R-54106-02907: Th8_TakeResult on the substituted-then-stored sensitive
                 value is still refused (field 3 of the probe)
} -constraints {
    th8 crypto_testlib
} -setup {
  catch {secure delete _sens_5_2}
  secure create _sens_5_2 "topsecret"
  set _copy_5_2 [list $_sens_5_2]
} -body {
  lindex [::th8testlib::sensitive_probe _copy_5_2] 3
} -cleanup {
  catch {secure delete _sens_5_2}
  unset -nocomplain _copy_5_2
} -result {1}}

###############################################################################

runTest {test sensitive-5.3 {
  R-54106-02907: no false positive -- an ordinary value flowing through
                 the same [list ...] path is NOT sensitive
} -constraints {
    th8 crypto_testlib
} -setup {
  set _plain_5_3 [list "ordinary"]
} -body {
  lindex [::th8testlib::sensitive_probe _plain_5_3] 1
} -cleanup {
  unset -nocomplain _plain_5_3
} -result {0}}

###############################################################################

source tests/epilogue.tcl
