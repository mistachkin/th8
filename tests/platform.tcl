###############################################################################
#
# platform.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for platform callbacks, initialization lifecycle, UTF-8
# validation, and base path detection (Sections 5, 29).
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
# Section 1 -- UTF-8 validation (Section 5.1)
#
###############################################################################

runTest {test platform-1.1 {
  R-49723-00529: valid UTF-8 passes validation (returns 0 = TH8_OK)
} -constraints {
    loadLib th8
} -body {
  th8testlib::utf8validate "hello"
} -result {0}}

###############################################################################

runTest {test platform-1.2 {
  R-49723-00529: invalid UTF-8 lead byte is rejected (returns 1 = TH8_ERROR)
} -constraints {
    loadLib th8
} -body {
  th8testlib::utf8validate "\xFE\xFF"
} -result {1}}

###############################################################################

runTest {test platform-1.3 {
  R-49723-00529: truncated UTF-8 sequence is rejected
} -constraints {
    loadLib th8
} -body {
  #
  # 0xC3 is the start of a 2-byte UTF-8 sequence but
  # without the continuation byte.
  #
  th8testlib::utf8validate "\xC3"
} -result {1}}

###############################################################################

runTest {test platform-1.4 {
  R-49723-00529: empty string is valid UTF-8
} -constraints {
    loadLib th8
} -body {
  th8testlib::utf8validate ""
} -result {0}}

###############################################################################

runTest {test platform-1.5 {
  R-49723-00529: a UTF-16 surrogate codepoint encoded as UTF-8 is rejected
} -constraints {
    loadLib th8
} -body {
  #
  # 0xED 0xA0 0x80 is the (ill-formed) three-byte UTF-8 encoding of
  # U+D800, the first high surrogate.  Surrogates are valid only inside
  # UTF-16 and MUST NOT appear in UTF-8; accepting them enables
  # surrogate-smuggling (WTF-8 / CESU-8) attacks.  Exercises the
  # `cp >= 0xD800 && cp <= 0xDFFF` reject path.
  #
  th8testlib::utf8validate "\xED\xA0\x80"
} -result {1}}

###############################################################################

runTest {test platform-1.6 {
  R-49723-00529: the codepoint just past the surrogate range (U+E000) is valid
} -constraints {
    loadLib th8
} -body {
  #
  # 0xEE 0x80 0x80 encodes U+E000, the first codepoint above the
  # surrogate block.  It satisfies `cp >= 0xD800` but not
  # `cp <= 0xDFFF`, so the surrogate guard falls through and the byte
  # sequence validates -- the upper boundary of the reject range.
  #
  th8testlib::utf8validate "\xEE\x80\x80"
} -result {0}}

###############################################################################
#
# Section 2 -- Platform initialization lifecycle (Section 29)
#
###############################################################################

runTest {test platform-2.1 {
  R-09811-18829: POSIX platform provides xInitialize callback
} -constraints {
    loadLib th8
} -body {
  th8testlib::test_lifecycle has_init
} -result {1}}

###############################################################################

runTest {test platform-2.2 {
  R-38069-19424: POSIX platform provides xFinalize callback
} -constraints {
    loadLib th8
} -body {
  th8testlib::test_lifecycle has_finalize
} -result {1}}

###############################################################################

runTest {test platform-2.3 {
  R-63795-60847: cd fails when xSetCwd callback is absent (verifying error
                 propagation path)
} -constraints {
    loadLib nulleval
} -setup {
} -body {
  list [catch {th8testlib::nulleval {cd .}} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 3 -- Base path detection (Section 29.3)
#
###############################################################################

runTest {test platform-3.1 {
  R-49340-37967: base path resolves to dot in TH8 (the shell's base path is the
                 current directory)
} -constraints {
    th8
} -body {
  pwd
} -result {.}}

###############################################################################

source tests/epilogue.tcl
