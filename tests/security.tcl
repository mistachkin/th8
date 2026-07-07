###############################################################################
#
# security.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the ::th8_security system variable (Section 29).
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
# Section 1 -- th8_security: default values
#
###############################################################################

runTest {test security-1.1 {
  R-32092-29314: th8_security is an array at interpreter creation
} -constraints {
    th8
} -body {
  array exists ::th8_security
} -result {1}}

###############################################################################

runTest {test security-1.2a {
  R-32092-29314: th8_security(algorithmName) defaults to none
} -constraints {
    th8 crypto_disabled
} -body {
  set ::th8_security(algorithmName)
} -result {none}}

###############################################################################

runTest {test security-1.2b {
  R-32092-29314: th8_security(algorithmName) reflects the signing algorithm
                 when enabled
} -constraints {
    th8 crypto_enabled
} -body {
  set ::th8_security(algorithmName)
} -match regexp -result {^RSA-(?:16384|2048)$}}

###############################################################################

runTest {test security-1.3a {
  R-32092-29314: th8_security(policy) defaults to none
} -constraints {
    th8 crypto_disabled
} -body {
  set ::th8_security(policy)
} -result {none}}

###############################################################################

runTest {test security-1.3b {
  R-32092-29314: th8_security(policy) defaults to none
} -constraints {
    th8 crypto_enabled
} -body {
  set ::th8_security(policy)
} -result {signedOnly}}

###############################################################################

runTest {test security-1.4a {
  R-56520-28721: th8_security(publicKeyToken) defaults to none
} -constraints {
    th8 crypto_disabled
} -body {
  set ::th8_security(publicKeyToken)
} -result {none}}

###############################################################################

runTest {test security-1.4b {
  R-56520-28721: th8_security(publicKeyToken) defaults to none
} -constraints {
    th8 crypto_enabled
} -body {
  set ::th8_security(publicKeyToken)
} -match regexp -result {^26f17c3a1a544324|9920868842008fc9$}}

###############################################################################

runTest {test security-1.5a {
  R-56520-28721: th8_security(dataName) defaults to none
} -constraints {
    th8 crypto_disabled
} -body {
  set ::th8_security(dataName)
} -result {none}}

###############################################################################

runTest {test security-1.5b {
  R-56520-28721: th8_security(dataName) has current file name
} -constraints {
    th8 crypto_enabled
} -body {
  set ::th8_security(dataName)
} -result {tests/security.tcl}}

###############################################################################
#
# Section 2 -- th8_security: read-only protection
#
###############################################################################

runTest {test security-2.1 {
  R-50169-65270: set on system variable element returns error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {set ::th8_security(policy) foo} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {can't modify system variable *}}}

###############################################################################

runTest {test security-2.2 {
  R-50169-65270: unset on system variable returns error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {unset ::th8_security} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {can't modify system variable *}}}

###############################################################################

runTest {test security-2.3 {
  R-50169-65270: append on system variable element returns error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {append ::th8_security(algorithmName) "x"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {can't modify system variable *}}}

###############################################################################

runTest {test security-2.4 {
  R-50169-65270: incr on system variable element returns error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {incr ::th8_security(policy)} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {can't modify system variable *}}}

###############################################################################

runTest {test security-2.5 {
  set on system variable element (publicKeyToken) returns error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {set ::th8_security(publicKeyToken) "abc123"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {can't modify system variable *}}}

###############################################################################

runTest {test security-2.6 {
  unset on system variable element returns error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {unset ::th8_security(algorithmName)} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {can't modify system variable *}}}

###############################################################################
#
# Section 3 -- th8_security: introspection
#
###############################################################################

runTest {test security-3.1 {
  R-04287-59267: array names returns expected element names
} -constraints {
    th8
} -body {
  lsort [array names ::th8_security]
} -result {algorithmName dataName flags notAfter notBefore policy publicKeyToken}}

###############################################################################

runTest {test security-3.2 {
  R-04287-59267: array exists returns 1 for system variable
} -constraints {
    th8
} -body {
  array exists ::th8_security
} -result {1}}

###############################################################################

runTest {test security-3.3a {
  R-32092-29314: th8_security array contains all none values when disabled
} -constraints {
    th8 crypto_disabled
} -setup {
} -body {
  set pairs [array get ::th8_security]
  #
  # Sort by key for deterministic output.
  #
  set result {}
  foreach key [lsort [array names ::th8_security]] {
    lappend result $key [set ::th8_security($key)]
  }
  set result
} -cleanup {
  unset -nocomplain key
  unset -nocomplain pairs
  unset -nocomplain result
} -result {algorithmName none dataName none flags none notAfter none\
notBefore none policy none publicKeyToken none}}

###############################################################################

runTest {test security-3.3b {
  R-04287-59267: th8_security array reflects signing details when enabled
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set pairs [array get ::th8_security]
  #
  # Sort by key for deterministic output.
  #
  set result {}
  foreach key [lsort [array names ::th8_security]] {
    lappend result $key [set ::th8_security($key)]
  }
  set result
} -cleanup {
  unset -nocomplain key
  unset -nocomplain pairs
  unset -nocomplain result
} -match regexp -result {^algorithmName (?:RSA-16384|RSA-2048) dataName\
tests/security\.tcl flags (?:none|\d+) notAfter\
(?:none|\d{4}_\d{2}_\d{2}T\d{2}_\d{2}_\d{2}Z) notBefore\
(?:none|\d{4}_\d{2}_\d{2}T\d{2}_\d{2}_\d{2}Z) policy signedOnly publicKeyToken\
(?:26f17c3a1a544324|9920868842008fc9)$}}

###############################################################################

runTest {test security-3.4 {
  R-04287-59267: The info exists command SHALL return 1 for each th8_security
                 array element.
} -constraints {
    th8
} -body {
  list [info exists ::th8_security(algorithmName)] \
      [info exists ::th8_security(policy)] \
      [info exists ::th8_security(publicKeyToken)] \
      [info exists ::th8_security(dataName)]
} -result {1 1 1 1}}

###############################################################################

runTest {test security-3.5 {
  R-09588-64534: The info exists command SHALL return 1 for the th8_security
                 array base name.
} -constraints {
    th8
} -body {
  info exists ::th8_security
} -result {1}}

###############################################################################

runTest {test security-3.6 {
  R-04287-59267: The info exists command SHALL return 0 for a nonexistent
                 th8_security element.
} -constraints {
    th8
} -body {
  info exists ::th8_security(nosuchelement)
} -result {0}}

###############################################################################

source tests/epilogue.tcl
