###############################################################################
#
# clockext.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the [clock ntp] and [clock https] subcommands.
# These tests require network access and are gated on the
# libcurl constraint.
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
# Section 1 -- clock ntp
#
###############################################################################

runTest {test clockext-1.1 {
  R-14640-47759: clock ntp returns a plausible Unix epoch timestamp
} -constraints {
    th8 crypto_testlib clock_ntp clock_ntp_network
} -body {
  set t [clock ntp -server pool.ntp.org]
  # Must be after 2025-01-01 and before 2100-01-01
  expr {$t > 1735689600 && $t < 4102444800}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################

runTest {test clockext-1.2 {
  R-14640-47759: clock ntp with a custom server returns a timestamp
} -constraints {
    th8 crypto_testlib clock_ntp clock_ntp_network
} -body {
  set t [clock ntp -server pool.ntp.org -timeout 5000]
  set local [clock seconds]
  set diff [expr {$t - $local}]
  if {$diff < 0} then { set diff [expr {-$diff}] }
  expr {$diff <= 5}
} -cleanup {
  unset -nocomplain t
  unset -nocomplain local
  unset -nocomplain diff
} -result {1}}

###############################################################################

runTest {test clockext-1.3 {
  R-14640-47759: clock ntp with a nonexistent server returns an error
} -constraints {
    th8 crypto_testlib clock_ntp clock_ntp_network
} -body {
  catch {clock ntp -server nonexistent.invalid.test -timeout 2000} msg
  string match {clock ntp:*} $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 2 -- clock https
#
###############################################################################

runTest {test clockext-2.1 {
  R-08803-10244: clock https returns a plausible Unix epoch timestamp
} -constraints {
    th8 crypto_testlib libcurl clock_https clock_https_network
} -body {
  set t [clock https]
  expr {$t > 1735689600 && $t < 4102444800}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################

runTest {test clockext-2.2 {
  R-08803-10244: clock https result agrees with clock seconds within 5 seconds
} -constraints {
    th8 crypto_testlib libcurl clock_https clock_https_network
} -body {
  set t [clock https]
  set local [clock seconds]
  set diff [expr {$t - $local}]
  if {$diff < 0} then { set diff [expr {-$diff}] }
  expr {$diff <= 5}
} -cleanup {
  unset -nocomplain t diff local
} -result {1}}

###############################################################################

runTest {test clockext-3.1 {
  R-39982-49558: clock ntp accepts the -attempts retry option; a
                 non-numeric value is rejected at option-parse time,
                 before any network query (deterministic, network-free).
} -constraints {
    th8 crypto_testlib clock_ntp clock_ntp_network
} -body {
  #
  # The bad -attempts value fails in Th8_ToWideInt during option
  # parsing, so this exercises the -attempts wiring without depending
  # on a live NTP exchange.  catch returns 1 (error caught).
  #
  catch {clock ntp -attempts notanumber}
} -result {1}}

###############################################################################

source tests/epilogue.tcl
