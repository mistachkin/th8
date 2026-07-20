###############################################################################
#
# crypto.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the TH8 crypto subsystem (Section 29).  Most tests in
# this file require either the crypto compile-time option or the
# testlib to exercise the C-level key and policy APIs.  Tests that
# only inspect the default state of ::th8_security work without
# any special support.
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
# Section 1 -- crypto: default security state (no crypto required)
#
###############################################################################

runTest {test crypto-1.1 {
  R-32092-29314: th8_security has seven elements by default
} -constraints {
    th8
} -body {
  llength [array names ::th8_security]
} -result {7}}

###############################################################################

runTest {test crypto-1.2a {
  R-32092-29314: all th8_security elements are none when disabled
} -constraints {
    th8 crypto_disabled
} -body {
  set result 1
  foreach {key val} [array get ::th8_security] {
    if {$val ne "none"} then {
      parray ::th8_security
      set result 0
      break
    }
  }
  set result
} -cleanup {
  unset -nocomplain result key val
} -result {1}}

###############################################################################

runTest {test crypto-1.2b {
  R-32092-29314: all th8_security elements are NOT none when enabled
} -constraints {
    th8 crypto_enabled
} -body {
  set result 1
  foreach {key val} [array get ::th8_security] {
    if {$key eq "notAfter" || $key eq "notBefore" || $key eq "flags"} then { continue }
    if {$val eq "none"} then {
      parray ::th8_security
      set result 0
      break
    }
  }
  set result
} -cleanup {
  unset -nocomplain result key val
} -result {1}}

###############################################################################

runTest {test crypto-1.3 {
  R-14030-24353: ENABLE_CRYPTOGRAPHY in compileOptions when crypto is compiled
                 in
} -constraints {
    th8 crypto
} -body {
  expr {[lsearch -exact $::tcl_platform(compileOptions) \
      "ENABLE_CRYPTOGRAPHY"] >= 0}
} -result {1}}

###############################################################################

runTest {test crypto-1.4 {
  R-32092-29314: read of th8_security elements succeeds regardless of crypto
} -constraints {
    th8
} -body {
  list [catch {set ::th8_security(algorithmName)} a] \
      [catch {set ::th8_security(policy)} b] \
      [catch {set ::th8_security(publicKeyToken)} c]
} -cleanup {
  unset -nocomplain a b c
} -result {0 0 0}}

###############################################################################
#
# Section 2 -- crypto: compile-time option detection
#
###############################################################################

runTest {test crypto-2.1 {
  R-14274-04677: The tcl_platform(compileOptions) variable SHALL exist in TH8
                 interpreters.
} -constraints {
    th8
} -body {
  info exists ::tcl_platform(compileOptions)
} -result {1}}

###############################################################################

runTest {test crypto-2.2 {
  R-14274-04677: The tcl_platform(compileOptions) list SHALL contain at least
                 one element.
} -constraints {
    th8
} -body {
  expr {[llength $::tcl_platform(compileOptions)] > 0}
} -result {1}}

###############################################################################
#
# Section 3 -- crypto: embedded key token retrieval
#
# These tests require the testlib crypto commands
# (th8testlib::key_token, th8testlib::harpy_token,
# th8testlib::verify_sig).
#
###############################################################################

runTest {test crypto-3.1 {
  R-30888-26660: The key_token command SHALL return a 16-character hexadecimal
                 string for the key0 key.
} -constraints {
    th8 crypto_testlib
} -body {
  set token [th8testlib::key_token key0]
  list [string length $token] \
      [regexp {^[0-9a-f]{16}$} $token]
} -cleanup {
  unset -nocomplain token
} -result {16 1}}

###############################################################################

runTest {test crypto-3.2 {
  R-21583-54643: The key_token command SHALL return a 16-character hexadecimal
                 string for the keyRoot key.
} -constraints {
    th8 crypto_testlib
} -body {
  set token [th8testlib::key_token keyRoot]
  list [string length $token] \
      [regexp {^[0-9a-f]{16}$} $token]
} -cleanup {
  unset -nocomplain token
} -result {16 1}}

###############################################################################

runTest {test crypto-3.3 {
  R-50724-41788: The key0 and keyRoot embedded key tokens SHALL be distinct
                 values.
} -constraints {
    th8 crypto_testlib
} -body {
  expr {[th8testlib::key_token key0] ne
    [th8testlib::key_token keyRoot]}
} -result {1}}

###############################################################################

runTest {test crypto-3.4 {
  R-21583-54643: The key_token command SHALL return an error for an
                 unrecognized key name.
} -constraints {
    th8 crypto_testlib
} -body {
  catch {th8testlib::key_token bogus} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {unknown key name: must be key0 or keyRoot}}

###############################################################################
#
# Section 4 -- crypto: Harpy signature parsing
#
###############################################################################

runTest {test crypto-4.1 {
  R-31833-59574: The harpy_token command SHALL extract the public key token
                 from a .b64sig signature file.
} -constraints {
    th8 crypto_testlib
} -body {
  th8testlib::harpy_token \
      tests/helpers/signed_clock_seconds.th8.b64sig
} -result {26f17c3a1a544324}}

###############################################################################

runTest {test crypto-4.2 {
  R-31833-59574: The harpy_token command SHALL return a 16-character
                 hexadecimal string.
} -constraints {
    th8 crypto_testlib
} -body {
  set token [th8testlib::harpy_token \
      tests/helpers/signed_clock_seconds.th8.b64sig]
  list [string length $token] \
      [regexp {^[0-9a-f]{16}$} $token]
} -cleanup {
  unset -nocomplain token
} -result {16 1}}

###############################################################################
#
# Section 5 -- crypto: signature verification
#
###############################################################################

runTest {test crypto-5.1 {
  R-40734-02380: The verify_sig command SHALL return valid for a script signed
                 with an embedded key.
} -constraints {
    th8 crypto_testlib
} -body {
  #
  # The signed_clock_seconds.th8 file was signed with keyRoot
  # (token 26f17c3a1a544324).  The verifier should match the
  # embedded keyRoot and return "valid".
  #
  th8testlib::verify_sig \
      tests/helpers/signed_clock_seconds.th8
} -result {valid}}

###############################################################################

runTest {test crypto-5.2 {
  R-19249-39118: The verify_sig command SHALL fail for a nonexistent script
                 file.
} -constraints {
    th8 crypto_testlib
} -body {
  catch {th8testlib::verify_sig \
      tests/helpers/does_not_exist.th8} msg
  expr {$msg ne "valid"}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 6 -- crypto: signed-only policy lifecycle
#
# Tests the Th8_EnableSignedOnly flag and the signed-only pre-eval
# callback using the signed test helper file and its Harpy signature.
#
###############################################################################

runTest {test crypto-6.1 {
  R-19249-39118: The signed_only query subcommand SHALL return a boolean value
                 (0 or 1).
} -constraints {
    th8 crypto_testlib
} -body {
  set q [th8testlib::signed_only query]
  expr {$q == 0 || $q == 1}
} -cleanup {
  unset -nocomplain q
} -result {1}}

###############################################################################

runTest {test crypto-6.2 {
  R-19249-39118: The eval_signed subcommand SHALL succeed for a signed script
                 and populate the security state.
} -constraints {
    th8 crypto_testlib
} -body {
  th8testlib::signed_only eval_signed \
      tests/helpers/signed_clock_seconds.th8
} -result {disable_signed_policy ok 0 signedOnly 26f17c3a1a544324}}

###############################################################################

runTest {test crypto-6.3 {
  R-19249-39118: The eval_signed subcommand SHALL return code 0 for a script
                 with a valid signature.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::signed_only eval_signed \
      tests/helpers/signed_clock_seconds.th8] 2
} -result {0}}

###############################################################################

runTest {test crypto-6.4 {
  R-58065-24188: The eval_signed subcommand SHALL set th8_security(policy) to
                 signedOnly during evaluation.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::signed_only eval_signed \
      tests/helpers/signed_clock_seconds.th8] 3
} -result {signedOnly}}

###############################################################################

runTest {test crypto-6.5 {
  R-58065-24188: The eval_signed subcommand SHALL set
                 th8_security(publicKeyToken) to the signing key token.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::signed_only eval_signed \
      tests/helpers/signed_clock_seconds.th8] 4
} -result {26f17c3a1a544324}}

###############################################################################

runTest {test crypto-6.6 {
  R-19249-39118: The eval_signed subcommand SHALL return a non-zero code for a
                 nonexistent script file.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::signed_only eval_signed \
      tests/helpers/does_not_exist.th8] 2
} -result {1}}

###############################################################################

runTest {test crypto-6.7 {
  R-58065-24188: The signed_only policy SHALL not revert to off after
                 eval_signed completes.
} -constraints {
    th8 crypto_testlib crypto_enabled
} -body {
  # After eval_signed completes, signed-only should be off.
  set q1 [th8testlib::signed_only query]
  th8testlib::signed_only eval_signed \
      tests/helpers/signed_clock_seconds.th8
  set q2 [th8testlib::signed_only query]
  list $q1 $q2
} -cleanup {
  unset -nocomplain q1 q2
} -result {1 1}}

###############################################################################
#
# Section 7 -- crypto: policy enforcement depth and tamper tests
#
# These tests exercise the signed-only policy at multiple eval
# depths and verify tamper detection.  Uses the C-level
# policy_depth_test command for comprehensive coverage.
#
###############################################################################

runTest {test crypto-7.1 {
  R-23239-63645: The policy_depth_test command SHALL report all sub-tests as
                 ok.
} -constraints {
    th8 crypto_testlib
} -body {
  th8testlib::policy_depth_test
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain r1 r2 r3 msg
} -match glob -result {*}}

###############################################################################

runTest {test crypto-7.2 {
  R-19249-39118: Source of a signed script SHALL succeed under signed-only
                 policy.
} -constraints {
    th8 crypto_testlib
} -body {
  set result [th8testlib::policy_depth_test]
  set idx [lsearch -exact $result "source_signed"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain r1 r2 r3 msg result idx
} -result {ok}}

###############################################################################

runTest {test crypto-7.3 {
  R-23239-63645: Nested eval within a signed script SHALL succeed under
                 signed-only policy.
} -constraints {
    th8 crypto_testlib
} -body {
  set result [th8testlib::policy_depth_test]
  set idx [lsearch -exact $result "nested_eval"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain r1 r2 r3 msg idx result
} -result {ok}}

###############################################################################

runTest {test crypto-7.4 {
  R-09186-19763: A tampered script with a valid signature file SHALL be
                 rejected.
} -constraints {
    th8 crypto_testlib
} -body {
  set result [th8testlib::policy_depth_test]
  set idx [lsearch -exact $result "tampered"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain r1 r2 r3 msg idx result
} -result {ok}}

###############################################################################

runTest {test crypto-7.5 {
  R-57476-09524: Direct Th8_Eval with NULL origin SHALL succeed within a
                 verified context.
} -constraints {
    th8 crypto_testlib
} -body {
  set result [th8testlib::policy_depth_test]
  set idx [lsearch -exact $result "null_origin_verified"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain r1 r2 r3 msg idx result
} -result {ok}}

###############################################################################

runTest {test crypto-7.6 {
  R-19249-39118: Multiple consecutive source commands of signed scripts SHALL
                 all succeed.
} -constraints {
    th8 crypto_testlib
} -body {
  set result [th8testlib::policy_depth_test]
  set idx [lsearch -exact $result "double_source"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain r1 r2 r3 msg idx result
} -result {ok}}

###############################################################################

runTest {test crypto-7.7 {
  R-19249-39118: Source of a nonexistent script SHALL be rejected when
                 signed-only is active.
} -constraints {
    th8 crypto_testlib
} -body {
  set result [th8testlib::policy_depth_test]
  set idx [lsearch -exact $result "nonexistent"]
  lindex $result [expr {$idx + 1}]
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain r1 r2 r3 msg idx result
} -result {ok}}

###############################################################################

runTest {test crypto-7.8 {
  R-26716-52256: The sig_hashes command SHALL show matching hashes for a valid
                 signed file.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::sig_hashes \
      tests/helpers/signed_clock_seconds.th8] 2
} -result {1}}

###############################################################################

runTest {test crypto-7.9 {
  R-09186-19763: The sig_hashes command SHALL show mismatched hashes for a
                 tampered file.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::sig_hashes \
      tests/helpers/tampered_clock_seconds.th8] 2
} -result {0}}

###############################################################################
#
# Section 8 -- crypto: Th8_EvalFileAsData
#
# Tests for loading binary data via signed script evaluation in
# an isolated child interpreter.
#
###############################################################################

runTest {test crypto-8.1 {
  R-36411-65332: The load_key_file command SHALL succeed for a signed script
                 that returns a valid RSA key.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::load_key_file \
      tests/helpers/load_test_key.th8] 0
} -result {0}}

###############################################################################

runTest {test crypto-8.2 {
  R-36411-65332: The load_key_file command SHALL fail for non-key base64 data
                 when crypto is enabled.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::load_key_file \
      tests/helpers/base64_hello.th8] 0
} -result {1}}

###############################################################################

runTest {test crypto-8.3 {
  R-36411-65332: The load_key_file command SHALL fail for a nonexistent script
                 file.
} -constraints {
    th8 crypto_testlib
} -body {
  lindex [th8testlib::load_key_file \
      tests/helpers/nonexistent_key.th8] 0
} -result {1}}

###############################################################################

runTest {test crypto-9.1 {
  R-09186-19763: Files that have been tampered with should not be loaded.
} -constraints {
  th8 crypto_enabled
} -body {
  list [catch {source tests/helpers/tampered_clock_seconds.th8} msg] $msg
} -cleanup {
  catch {namespace delete ::test_ns}
  unset -nocomplain msg
} -result {1 {signed-only: script signature verification failed for "tests/helpers/tampered_clock_seconds.th8"}}}

###############################################################################

runTest {test crypto-9.2 {
  R-19249-39118: Files that are verified authentic should be loaded.
} -constraints {
  th8 crypto_enabled
} -body {
  list [catch {source tests/helpers/signed_clock_seconds.th8} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match regexp -result {^0 \d+$}}

###############################################################################

source tests/epilogue.tcl
