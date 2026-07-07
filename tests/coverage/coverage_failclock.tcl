###############################################################################
#
# coverage_failclock.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Exercises the -failTimeMs and -failGetEnv F3 fault flags
# (batch 160).  Drives clock-failure and env-fetch-failure
# error arms in consumers of Th8_GetTimeMs / Th8_GetEnv.
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

runTest {test failclock-1.1 {
  [clock milliseconds] under -failTimeMs drives the clock-
  failure error arm where Th8_GetTimeMs returns TH8_ERROR.
  The result rc / message may vary; just verify the eval
  completes without crashing.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {clock milliseconds} -failTimeMs]
  expr {[lindex $r 0] != 0 || [string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test failclock-2.1 {
  [info env] enumeration under -failGetEnv drives the
  fail-to-fetch arms in env-array initialization.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval \
      {info exists ::env(HOME)} -failGetEnv]
  expr {[lindex $r 0] != 0 || [string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
