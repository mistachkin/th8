###############################################################################
#
# foreach.tcl --
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
# Section 1 -- foreach: Basic iteration
#
###############################################################################

runTest {test foreach-1.1 {
  R-62928-07465: basic iteration over list
} -setup {
} -body {
  set result ""
  foreach x {a b c d} {
    append result $x
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {abcd}}

###############################################################################

runTest {test foreach-1.2 {
  R-34479-61035: loop variable retains last value
} -setup {
} -body {
  foreach x {1 2 3} {}
  set x
} -cleanup {
  unset -nocomplain x
} -result {3}}

###############################################################################

runTest {test foreach-1.3 {
  R-62928-07465: foreach with single element list
} -setup {
} -body {
  set result ""
  foreach x {only} {
    append result $x
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {only}}

###############################################################################

runTest {test foreach-1.4 {
  R-62928-07465: foreach accumulates values
} -setup {
} -body {
  set result 0
  foreach x {1 2 3 4 5} {
    set result [expr {$result + $x}]
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {15}}

###############################################################################
#
# Section 2 -- foreach: Multiple variables
#
###############################################################################

runTest {test foreach-2.1 {
  R-62928-07465: two variables per iteration
} -setup {
} -body {
  set result ""
  foreach {a b} {1 2 3 4 5 6} {
    append result "$a$b "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain a
  unset -nocomplain b
} -result {12 34 56 }}

###############################################################################

runTest {test foreach-2.2 {
  R-62928-07465: three variables per iteration
} -setup {
} -body {
  set result ""
  foreach {a b c} {1 2 3 4 5 6} {
    append result "$a$b$c "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -result {123 456 }}

###############################################################################
#
# Section 3 -- foreach: break
#
###############################################################################

runTest {test foreach-3.1 {
  R-33606-19387: break exits foreach loop
} -setup {
} -body {
  set result ""
  foreach x {a b c d e} {
    if {$x eq "c"} then {
      break
    }
    append result $x
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {ab}}

###############################################################################

runTest {test foreach-3.2 {
  R-33606-19387: break sets loop variable to current value
} -setup {
} -body {
  foreach x {10 20 30 40} {
    if {$x == 30} then {
      break
    }
  }
  set x
} -cleanup {
  unset -nocomplain x
} -result {30}}

###############################################################################
#
# Section 4 -- foreach: continue
#
###############################################################################

runTest {test foreach-4.1 {
  R-15926-59145: continue skips rest of body
} -setup {
} -body {
  set result ""
  foreach x {a b c d e} {
    if {$x eq "c"} then {
      continue
    }
    append result $x
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {abde}}

###############################################################################

runTest {test foreach-4.2 {
  R-15926-59145: continue proceeds to next element
} -setup {
} -body {
  set count 0
  foreach x {1 2 3 4 5} {
    incr count
    continue
  }
  set count
} -cleanup {
  unset -nocomplain count
  unset -nocomplain x
} -result {5}}

###############################################################################
#
# Section 5 -- foreach: empty list
#
###############################################################################

runTest {test foreach-5.1 {
  R-62928-07465: empty list does not execute body
} -setup {
} -body {
  set result "unchanged"
  foreach x {} {
    set result "changed"
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {unchanged}}

###############################################################################

runTest {test foreach-5.2 {
  R-02310-09257: foreach returns empty string on empty list
} -setup {
} -body {
  set result [foreach x {} {set x}]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {}}

###############################################################################
#
# Section 6 -- foreach: partial last group
#
###############################################################################

runTest {test foreach-6.1 {
  R-16096-42004: partial group fills missing vars with empty string
} -setup {
} -body {
  set result ""
  foreach {a b} {1 2 3} {
    append result "($a,$b) "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain a
  unset -nocomplain b
} -result {(1,2) (3,) }}

###############################################################################

runTest {test foreach-6.2 {
  R-16096-42004: partial group with three vars fills missing with empty string
} -setup {
} -body {
  set result ""
  foreach {a b c} {1 2 3 4} {
    append result "($a,$b,$c) "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -result {(1,2,3) (4,,) }}

###############################################################################
#
# Section 7 -- foreach: return value and error cases
#
###############################################################################

runTest {test foreach-7.1 {
  R-02310-09257: foreach returns empty string
} -setup {
} -body {
  set result [foreach x {1 2 3} {set x}]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {}}

###############################################################################

runTest {test foreach-7.2 {
  R-62928-07465: foreach with wrong # args
} -setup {
} -body {
  list [catch {foreach} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test foreach-7.3 {
  R-62928-07465: foreach with error in body
} -setup {
} -body {
  list [catch {foreach x {1 2 3} {error "oops"}} msg] $msg
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain x
} -result {1 oops}}

###############################################################################
#
# Section 8 -- foreach: Additional Tcl 8.4 behavior tests
#
###############################################################################

runTest {test foreach-8.1 {
  R-62928-07465: single variable over empty list, body never executes
} -setup {
} -body {
  set result "untouched"
  foreach x {} {
    set result "touched"
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {untouched}}

###############################################################################

runTest {test foreach-8.2 {
  R-62928-07465: two variables over list of 6 gives 3 iterations
} -setup {
} -body {
  set result ""
  set count 0
  foreach {a b} {10 20 30 40 50 60} {
    incr count
    append result "($a,$b) "
  }
  list $count $result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain count
  unset -nocomplain a
  unset -nocomplain b
} -result {3 {(10,20) (30,40) (50,60) }}}

###############################################################################

runTest {test foreach-8.3 {
  R-15926-59145: continue skips to next iteration but loop completes
} -setup {
} -body {
  set result ""
  set count 0
  foreach x {1 2 3 4 5} {
    incr count
    if {$x == 2 || $x == 4} then {
      continue
    }
    append result $x
  }
  list $count $result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain count
  unset -nocomplain x
} -result {5 135}}

###############################################################################

runTest {test foreach-8.4 {
  R-62928-07465: modifying list variable does not affect iteration
} -setup {
} -body {
  set mylist {a b c d}
  set result ""
  foreach x $mylist {
    append result $x
    set mylist {z z z z z z}
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain mylist
  unset -nocomplain x
} -result {abcd}}

###############################################################################

runTest {test foreach-8.5 {
  R-62928-07465: values containing spaces are properly parsed
} -setup {
} -body {
  set result ""
  foreach x {"hello world" "foo bar" "a b c"} {
    append result "($x) "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {(hello world) (foo bar) (a b c) }}

###############################################################################

runTest {test foreach-8.6 {
  R-62928-07465: nested lists as values
} -setup {
} -body {
  set result ""
  foreach x {{1 2} {3 4} {5 6}} {
    append result "([lindex $x 0]+[lindex $x 1]) "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {(1+2) (3+4) (5+6) }}

###############################################################################
#
# Section 8 -- foreach: break clears result
#
###############################################################################

runTest {test foreach-8.1 {
  R-03611-50931: break within foreach clears result to empty string
} -setup {
} -body {
  foreach i {1 2 3} {
    break
  }
} -cleanup {
  unset -nocomplain i
} -result {}}

###############################################################################

runTest {test foreach-8.2 {
  R-03611-50931: break within foreach does not leak body result
} -setup {
} -body {
  set x [foreach i {1 2 3} {
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
