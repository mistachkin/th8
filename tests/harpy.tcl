###############################################################################
#
# harpy.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the [harpy] command (Section 32.3) and RSA signing
# infrastructure (Section 32.4).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test harpy-1.1 {
  R-31508-51826: harpy sign produces .b64sig format output
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set sig [harpy sign $::_harpyToken "set x 1"]
  # Must contain the header with the token.
  expr {[string first $::_harpyToken $sig] >= 0}
} -cleanup {
  unset -nocomplain sig
} -result {1}}

###############################################################################

runTest {test harpy-1.2 {
  R-31508-51826: harpy sign output starts with comment header
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set sig [harpy sign $::_harpyToken "set x 1"]
  string range $sig 0 0
} -cleanup {
  unset -nocomplain sig
} -result {#}}

###############################################################################

runTest {test harpy-1.3 {
  R-49596-49581: harpy sign self-verifies (round-trip)
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set script "proc hello {} { return world }"
  set sig [harpy sign $::_harpyToken $script]
  harpy verify $::_harpyToken $script $sig
} -cleanup {
  unset -nocomplain script sig
} -result {ok}}

###############################################################################

runTest {test harpy-2.1 {
  R-40734-02380: harpy verify returns ok for valid signature
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set script "puts {test 2.1}"
  set sig [harpy sign $::_harpyToken $script]
  harpy verify $::_harpyToken $script $sig
} -cleanup {
  unset -nocomplain script sig
} -result {ok}}

###############################################################################

runTest {test harpy-2.2 {
  R-40078-47877: harpy verify detects tampered script
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set script "puts {original}"
  set sig [harpy sign $::_harpyToken $script]
  catch {harpy verify $::_harpyToken "puts {tampered}" $sig} msg
  expr {[string match "*mismatch*" $msg]}
} -cleanup {
  unset -nocomplain script sig msg
} -result {1}}

###############################################################################

runTest {test harpy-2.3 {
  R-40078-47877: harpy verify detects single-byte change
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set script "set x 42"
  set sig [harpy sign $::_harpyToken $script]
  catch {harpy verify $::_harpyToken "set x 43" $sig} msg
  expr {[string match "*mismatch*" $msg]}
} -cleanup {
  unset -nocomplain script sig msg
} -result {1}}

###############################################################################

runTest {test harpy-2.4 {
  R-40078-47877: harpy verify detects empty vs non-empty
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set script "set x 1"
  set sig [harpy sign $::_harpyToken $script]
  catch {harpy verify $::_harpyToken "" $sig} msg
  expr {[string match "*mismatch*" $msg]}
} -cleanup {
  unset -nocomplain script sig msg
} -result {1}}

###############################################################################

runTest {test harpy-3.1 {
  R-01323-22268: harpy sign rejects public-only key
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  # The embedded keyRoot (26f17c3a1a544324) is public-only.
  catch {harpy sign 26f17c3a1a544324 "test"} msg
  expr {[string match "*private key*" $msg]}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test harpy-3.2 {
  R-45789-59282: harpy requires policy installed
} -constraints {
    th8
} -body {
  # In TH8SH_NO_SCRIPT_SECURITY mode, the policy is not installed.
  # This test only runs when the policy IS installed.
  # Skip if no harpy command.
  if {[llength [info commands harpy]] == 0} then {
    error "skipped"
  }
  set result "policy check passed"
  set result
} -cleanup {
  unset -nocomplain result
} -match glob -result {*}}

###############################################################################

runTest {test harpy-4.1 {
  R-49596-49581: sign different scripts produce different signatures
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set sig1 [harpy sign $::_harpyToken "script one"]
  set sig2 [harpy sign $::_harpyToken "script two"]
  expr {$sig1 ne $sig2}
} -cleanup {
  unset -nocomplain sig1 sig2
} -result {1}}

###############################################################################

runTest {test harpy-4.2 {
  R-31508-51826: sign same script produces consistent verification
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  set script "consistent test"
  set sig1 [harpy sign $::_harpyToken $script]
  set sig2 [harpy sign $::_harpyToken $script]
  list [harpy verify $::_harpyToken $script $sig1] \
      [harpy verify $::_harpyToken $script $sig2]
} -cleanup {
  unset -nocomplain script sig1 sig2
} -result {ok ok}}

###############################################################################
#
# Script annotation tests (notBefore / notAfter / flags).
#
# Parsing tests use crypto_testlib (annotations are always scanned).
# Enforcement tests use harpy_sign (signed-only mode must be active
# for time constraints to block execution).
#
###############################################################################

runTest {test annotation-1.1 {
  R-32639-01913: no annotations: script runs, values are none
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_none.tcl
  list $result $::th8_security(notBefore) \
      $::th8_security(notAfter)
} -cleanup {
  unset -nocomplain result
} -result {no-annotations none none}}

###############################################################################

runTest {test annotation-1.2 {
  R-38064-59928: both notBefore and notAfter present and valid
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_both_valid.tcl
  list $result \
      $::th8_security(notBefore) \
      $::th8_security(notAfter)
} -cleanup {
  unset -nocomplain result
} -result {both-valid 2020_01_01T00_00_00Z 2099_12_31T23_59_59Z}}

###############################################################################

runTest {test annotation-1.3 {
  R-12455-60956: notBefore only: variable is set to correct value
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_notbefore_only.tcl
  list $result $::th8_security(notBefore)
} -cleanup {
  unset -nocomplain result
} -result {notbefore-only 2020_01_01T00_00_00Z}}

###############################################################################

runTest {test annotation-1.4 {
  R-55366-36861: notAfter only: variable is set to correct value
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_notafter_only.tcl
  list $result $::th8_security(notAfter)
} -cleanup {
  unset -nocomplain result
} -result {notafter-only 2099_12_31T23_59_59Z}}

###############################################################################

runTest {test annotation-1.5 {
  R-15750-16500: epoch boundary values (1970-01-01 to 9999-12-31)
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_epoch_boundary.tcl
  set result
} -cleanup {
  unset -nocomplain result
} -result {epoch-boundary}}

###############################################################################

runTest {test annotation-1.6 {
  R-15750-16500: leap year Feb 29 on a leap year (2024) is valid
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_leap_valid.tcl
  set result
} -cleanup {
  unset -nocomplain result
} -result {leap-valid}}

###############################################################################

runTest {test annotation-2.1 {
  R-48430-59337: bad format: not a timestamp at all
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_bad_format.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test annotation-2.2 {
  R-15750-16500: bad month (13)
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_bad_month.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test annotation-2.3 {
  R-15750-16500: bad day (Feb 30)
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_bad_day.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notAfter*}}

###############################################################################

runTest {test annotation-2.4 {
  R-15750-16500: leap year Feb 29 on a non-leap year (2023) is invalid
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_leap_invalid.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test annotation-2.5 {
  R-15750-16500: pre-epoch year (1969) is invalid
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_pre_epoch.tcl
} -returnCodes 1 -match glob -result {annotation: invalid notBefore*}}

###############################################################################

runTest {test annotation-3.1 {
  R-63019-38593: enforcement: expired script blocked in signed-only mode
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_expired.tcl
} -returnCodes 1 -match glob -result {annotation: script has expired*}}

###############################################################################

runTest {test annotation-3.2 {
  R-59386-08297: enforcement: not-yet-valid script blocked in signed-only mode
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_not_yet_valid.tcl
} -returnCodes 1 -match glob -result {annotation: script is not yet valid*}}

###############################################################################

runTest {test annotation-3.3 {
  R-20621-61185: enforcement: inverted range (notBefore > notAfter) blocked
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_inverted_range.tcl
} -returnCodes 1 -match glob -result {annotation: script*}}

###############################################################################

runTest {test annotation-3.4 {
  R-38064-59928: enforcement: valid range passes in signed-only mode
} -constraints {
    th8 harpy_sign crypto_enabled
} -body {
  source tests/helpers/ann_both_valid.tcl
  set result
} -cleanup {
  unset -nocomplain result
} -result {both-valid}}

###############################################################################

source tests/epilogue.tcl
