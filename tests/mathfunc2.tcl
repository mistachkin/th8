###############################################################################
#
# mathfunc2.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the pi() and random() math functions (Section 9.2.1).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test mathfunc2-1.1 {
  R-64737-26899: pi() returns Pi to maximum double precision
} -body {
  expr {pi()}
} -result {3.141592653589793}}

###############################################################################

runTest {test mathfunc2-1.2 {
  R-07930-26550: pi() value is in the correct range
} -body {
  expr {pi() > 3.14159265358979 && pi() < 3.14159265358980}
} -result {1}}

###############################################################################

runTest {test mathfunc2-1.3 {
  R-64737-26899: pi() takes no arguments
} -setup {
} -body {
  # Verify it works with no args in an expression context.
  set v [expr {pi()}]
  string length $v
} -cleanup {
  unset -nocomplain v
} -match regexp -result {[0-9]+}}

###############################################################################

runTest {test mathfunc2-1.4 {
  R-07930-26550: pi() usable in arithmetic
} -body {
  # Area of unit circle: pi * r^2 where r=1.
  expr {pi() * 1.0 * 1.0}
} -result {3.141592653589793}}

###############################################################################

runTest {test mathfunc2-1.5 {
  R-64737-26899: pi() is listed by info functions
} -body {
  expr {[lsearch [info functions] "pi"] >= 0}
} -result {1}}

###############################################################################

runTest {test mathfunc2-2.1 {
  R-56249-19402: random() returns a 64-bit integer
} -setup {
} -body {
  set v [expr {random()}]
  string is wideinteger $v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################

runTest {test mathfunc2-2.2 {
  R-36032-44824: successive random() calls return different values
} -body {
  set a [expr {random()}]
  set b [expr {random()}]
  set c [expr {random()}]
  # All three should be different (probability of collision
  # for 64-bit values is negligible).
  expr {$a ne $b && $b ne $c && $a ne $c}
} -cleanup {
  unset -nocomplain a b c
} -result {1}}

###############################################################################

runTest {test mathfunc2-2.3 {
  R-56249-19402: random() is listed by info functions
} -body {
  expr {[lsearch [info functions] "random"] >= 0}
} -result {1}}

###############################################################################

runTest {test mathfunc2-2.4 {
  R-22028-56793: random() is distinct from rand()
} -body {
  # rand() returns a double in [0,1); random() returns a wide int.
  set r [expr {rand()}]
  set q [expr {random()}]
  list [expr {$r >= 0.0 && $r < 1.0}] [string is wideinteger $q]
} -cleanup {
  unset -nocomplain r q
} -result {1 1}}

###############################################################################

runTest {test mathfunc2-2.5 {
  R-56249-19402: random() can produce negative values
} -body {
  # Run enough trials to get at least one negative (50% chance per trial).
  set found 0
  for {set i 0} {$i < 100} {incr i} {
    if {[expr {random()}] < 0} then {
      set found 1
      break
    }
  }
  set found
} -cleanup {
  unset -nocomplain found i
} -result {1}}

###############################################################################
#
# Section 3 -- epsilon()
#
###############################################################################

runTest {test mathfunc2-3.1 {
  R-30069-08069: epsilon() returns the IEEE 754 double-precision
  machine epsilon
} -body {
  expr {epsilon()}
} -result {2.220446049250313e-16}}

###############################################################################

runTest {test mathfunc2-3.2 {
  R-30069-08069: epsilon() is the smallest positive value where
  1.0 + epsilon is distinguishable from 1.0
} -body {
  expr {1.0 + epsilon() != 1.0}
} -result {1}}

###############################################################################

runTest {test mathfunc2-3.3 {
  R-06656-00761: epsilon() returns a positive value
} -body {
  expr {epsilon() > 0.0}
} -result {1}}

###############################################################################

runTest {test mathfunc2-3.4 {
  R-06656-00761: successive epsilon() calls return identical values
  within a single interpreter lifetime
} -body {
  expr {epsilon() == epsilon()}
} -result {1}}

###############################################################################
#
# Section 4 -- bool() math function corner cases
#
# The [expr bool(x)] math function delegates to the same boolean
# coercion as `if`/`while`/etc.  These tests verify the wide-int,
# bigint, double, and keyword paths through that delegation.
#
###############################################################################

runTest {test mathfunc2-4.1 {
  R-04038-64345: bool of literal 0 is 0
} -body {
  expr bool(0)
} -result {0}}

###############################################################################

runTest {test mathfunc2-4.2 {
  R-04038-64345: bool of literal 1 is 1
} -body {
  expr bool(1)
} -result {1}}

###############################################################################

runTest {test mathfunc2-4.3 {
  R-04038-64345: bool of int32 nonzero is 1
} -body {
  expr bool(42)
} -result {1}}

###############################################################################

runTest {test mathfunc2-4.4 {
  R-04038-64345: bool of value in int32-int64 range is 1 (wide-int path)
} -body {
  expr bool(5000000000)
} -result {1}}

###############################################################################

runTest {test mathfunc2-4.5 {
  R-04038-64345: bool of value exceeding int64 is 1 (bigint path)
} -constraints {
  bigint
} -body {
  expr bool(2348923847623847623847623847623487623)
} -result {1}}

###############################################################################

runTest {test mathfunc2-4.6 {
  R-04038-64345: bool of nonzero double is 1
} -body {
  expr bool(1.0)
} -result {1}}

###############################################################################

runTest {test mathfunc2-4.7 {
  R-04038-64345: bool of zero double is 0
} -body {
  expr bool(0.0)
} -result {0}}

###############################################################################

runTest {test mathfunc2-4.8 {
  R-02588-48107: bool of keyword "true" is 1
} -body {
  expr {bool("true")}
} -result {1}}

###############################################################################

runTest {test mathfunc2-4.9 {
  R-00488-11343: bool of keyword "false" is 0
} -body {
  expr {bool("false")}
} -result {0}}

###############################################################################

runTest {test mathfunc2-4.10 {
  R-04038-64345: bool of non-numeric, non-keyword string raises an error
} -setup {
} -body {
  list [catch {expr {bool("hello")}} msg] [string match {*expected boolean*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
