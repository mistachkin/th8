###############################################################################
#
# for.tcl --
#
# Tcl Language Standard
# Conformance Test File
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
# Section 1 -- for: Basic loop operation
#
###############################################################################

runTest {test for-1.1 {
  R-40199-07933: for evaluates init, tests condition, evaluates body and incr
} -setup {
} -body {
  set result 0
  for {set i 0} {$i < 5} {incr i} {
    incr result
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {5}}

###############################################################################

runTest {test for-1.2 {
  R-40199-07933: for loop variable is accessible after loop
} -setup {
} -body {
  for {set i 0} {$i < 3} {incr i} {}
  set i
} -cleanup {
  unset -nocomplain i
} -result {3}}

###############################################################################

runTest {test for-1.3 {
  R-40199-07933: for loop with decrement evaluates body and incr
} -setup {
} -body {
  set result ""
  for {set i 3} {$i > 0} {incr i -1} {
    append result $i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {321}}

###############################################################################

runTest {test for-1.4 {
  R-40199-07933: for body not executed when test is initially false
} -setup {
} -body {
  set result "unchanged"
  for {set i 10} {$i < 5} {incr i} {
    set result "changed"
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {unchanged}}

###############################################################################
#
# Section 2 -- for: break
#
###############################################################################

runTest {test for-2.1 {
  R-20728-60242: break within for body terminates loop
} -setup {
} -body {
  set result ""
  for {set i 0} {$i < 10} {incr i} {
    if {$i == 3} then {
      break
    }
    append result $i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {012}}

###############################################################################

runTest {test for-2.2 {
  R-20728-60242: break does not execute next incr step
} -setup {
} -body {
  for {set i 0} {$i < 10} {incr i} {
    if {$i == 5} then {
      break
    }
  }
  set i
} -cleanup {
  unset -nocomplain i
} -result {5}}

###############################################################################
#
# Section 3 -- for: continue
#
###############################################################################

runTest {test for-3.1 {
  R-27601-55226: continue within for body skips to next incr evaluation
} -setup {
} -body {
  set result ""
  for {set i 0} {$i < 5} {incr i} {
    if {$i == 2} then {
      continue
    }
    append result $i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {0134}}

###############################################################################

runTest {test for-3.2 {
  R-27601-55226: continue still runs incr and re-evaluates test
} -setup {
} -body {
  for {set i 0} {$i < 5} {incr i} {
    continue
  }
  set i
} -cleanup {
  unset -nocomplain i
} -result {5}}

###############################################################################
#
# Section 4 -- for: empty body
#
###############################################################################

runTest {test for-4.1 {
  R-40199-07933: for with empty body still evaluates test and incr
} -setup {
} -body {
  for {set i 0} {$i < 3} {incr i} {}
  set i
} -cleanup {
  unset -nocomplain i
} -result {3}}

###############################################################################
#
# Section 5 -- for: nested loops
#
###############################################################################

runTest {test for-5.1 {
  R-40199-07933: nested for loops each evaluate init, test, body, incr
} -setup {
} -body {
  set result 0
  for {set i 0} {$i < 3} {incr i} {
    for {set j 0} {$j < 4} {incr j} {
      incr result
    }
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
  unset -nocomplain j
} -result {12}}

###############################################################################

runTest {test for-5.2 {
  R-20728-60242: break in inner for loop does not exit outer loop
} -setup {
} -body {
  set result 0
  for {set i 0} {$i < 3} {incr i} {
    for {set j 0} {$j < 10} {incr j} {
      if {$j == 2} then {
        break
      }
      incr result
    }
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
  unset -nocomplain j
} -result {6}}

###############################################################################
#
# Section 6 -- for: variable scope
#
###############################################################################

runTest {test for-6.1 {
  R-40199-07933: for loop variable visible at caller scope
} -setup {
} -body {
  for {set i 0} {$i < 3} {incr i} {}
  set result $i
} -cleanup {
  unset -nocomplain i
  unset -nocomplain result
} -result {3}}

###############################################################################

runTest {test for-6.2 {
  R-40199-07933: for loop body modifies caller variables
} -setup {
} -body {
  set x 0
  for {set i 0} {$i < 5} {incr i} {
    incr x 2
  }
  set x
} -cleanup {
  unset -nocomplain x
  unset -nocomplain i
} -result {10}}

###############################################################################
#
# Section 7 -- for: error cases
#
###############################################################################

runTest {test for-7.1 {
  R-40199-07933: for with wrong # args produces error
} -setup {
} -body {
  list [catch {for {set i 0}} msg] $msg
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain i
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test for-7.2 {
  R-40199-07933: for with error in test expression propagates error
} -setup {
} -body {
  list [catch {for {set i 0} {$i < nosuchvar} {incr i} {}} msg] $msg
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain i
} -match glob -result {1 *}}

###############################################################################

runTest {test for-7.3 {
  R-61272-28849: for returns empty string on normal completion
} -setup {
} -body {
  set result [for {set i 0} {$i < 3} {incr i} {}]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {}}

###############################################################################
#
# Section 8 -- for: break clears result
#
###############################################################################

runTest {test for-8.1 {
  R-61603-53175: break within for clears result to empty string
} -setup {
} -body {
  for {set i 0} {$i < 10} {incr i} {
    break
  }
} -cleanup {
  unset -nocomplain i
} -result {}}

###############################################################################

runTest {test for-8.2 {
  R-61603-53175: break within for does not leak body result
} -setup {
} -body {
  set x [for {set i 0} {$i < 10} {incr i} {
    set dummy "leaked"
    break
  }]
  set x
} -cleanup {
  unset -nocomplain x
  unset -nocomplain i
  unset -nocomplain dummy
} -result {}}

###############################################################################

source tests/epilogue.tcl
