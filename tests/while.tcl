###############################################################################
#
# while.tcl --
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
# Section 1 -- while: Basic loop operation
#
###############################################################################

runTest {test while-1.1 {
  R-01450-54795: while repeatedly evaluates test and executes body while true
} -setup {
} -body {
  set result 0
  set i 0
  while {$i < 5} {
    incr result
    incr i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {5}}

###############################################################################

runTest {test while-1.2 {
  R-01450-54795: while with string accumulation in body
} -setup {
} -body {
  set result ""
  set i 0
  while {$i < 4} {
    append result $i
    incr i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {0123}}

###############################################################################

runTest {test while-1.3 {
  R-01450-54795: while loop variable accessible after loop
} -setup {
} -body {
  set i 0
  while {$i < 7} {
    incr i
  }
  set i
} -cleanup {
  unset -nocomplain i
} -result {7}}

###############################################################################
#
# Section 2 -- while: false on first test
#
###############################################################################

runTest {test while-2.1 {
  R-01450-54795: while body not executed when condition initially false
} -setup {
} -body {
  set result "unchanged"
  while {0} {
    set result "changed"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {unchanged}}

###############################################################################

runTest {test while-2.2 {
  R-01450-54795: while with false expression condition does not execute body
} -setup {
} -body {
  set result "unchanged"
  set x 10
  while {$x < 5} {
    set result "changed"
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {unchanged}}

###############################################################################
#
# Section 3 -- while: break
#
###############################################################################

runTest {test while-3.1 {
  R-27074-56801: break within while body terminates loop
} -setup {
} -body {
  set result ""
  set i 0
  while {$i < 10} {
    if {$i == 4} then {
      break
    }
    append result $i
    incr i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {0123}}

###############################################################################

runTest {test while-3.2 {
  R-27074-56801: break from infinite while loop terminates loop
} -setup {
} -body {
  set count 0
  while {1} {
    incr count
    if {$count >= 3} then {
      break
    }
  }
  set count
} -cleanup {
  unset -nocomplain count
} -result {3}}

###############################################################################
#
# Section 4 -- while: continue
#
###############################################################################

runTest {test while-4.1 {
  R-13074-61812: continue skips rest of while body to next test evaluation
} -setup {
} -body {
  set result ""
  set i 0
  while {$i < 6} {
    incr i
    if {$i == 3} then {
      continue
    }
    append result $i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {12456}}

###############################################################################

runTest {test while-4.2 {
  R-13074-61812: continue re-evaluates while test condition
} -setup {
} -body {
  set count 0
  set i 0
  while {$i < 5} {
    incr i
    incr count
    if {$i == 3} then {
      continue
    }
  }
  set count
} -cleanup {
  unset -nocomplain count
  unset -nocomplain i
} -result {5}}

###############################################################################
#
# Section 5 -- while: nested while
#
###############################################################################

runTest {test while-5.1 {
  R-01450-54795: nested while loops each evaluate test and body
} -setup {
} -body {
  set result 0
  set i 0
  while {$i < 3} {
    set j 0
    while {$j < 4} {
      incr result
      incr j
    }
    incr i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
  unset -nocomplain j
} -result {12}}

###############################################################################

runTest {test while-5.2 {
  R-27074-56801: break in inner while does not exit outer loop
} -setup {
} -body {
  set result 0
  set i 0
  while {$i < 3} {
    set j 0
    while {$j < 10} {
      if {$j == 2} then {
        break
      }
      incr result
      incr j
    }
    incr i
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
  unset -nocomplain j
} -result {6}}

###############################################################################
#
# Section 6 -- while: return value
#
###############################################################################

runTest {test while-6.1 {
  R-53705-09312: while returns empty string
} -setup {
} -body {
  set i 0
  set result [while {$i < 3} {incr i}]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain i
} -result {}}

###############################################################################
#
# Section 7 -- while: error cases
#
###############################################################################

runTest {test while-7.1 {
  R-01450-54795: while with wrong # args produces error
} -setup {
} -body {
  list [catch {while} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test while-7.2 {
  R-01450-54795: while with non-boolean condition produces error
} -setup {
} -body {
  list [catch {while {notabool} {break}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test while-7.3 {
  R-56291-06757: while with error in body returns error code 1
} -setup {
} -body {
  list [catch {while {1} {error "test error"}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {test error}}}

###############################################################################
#
# Section 8 -- while: break clears result
#
###############################################################################

runTest {test while-8.1 {
  R-63526-16514: break within while clears result to empty string
} -body {
  while {1} {
    break
  }
} -result {}}

###############################################################################

runTest {test while-8.2 {
  R-63526-16514: break within while does not leak body result
} -setup {
} -body {
  set x [while {1} {
    set dummy "leaked"
    break
  }]
  set x
} -cleanup {
  unset -nocomplain x
  unset -nocomplain dummy
} -result {}}

###############################################################################

source tests/epilogue.tcl
