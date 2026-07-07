###############################################################################
#
# controlflow.tcl --
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
# Section 1 -- foreach: multiple varlist-list pairs
#
###############################################################################

runTest {test controlflow-1.1 {
  R-55480-36320: foreach with multiple varlist-list pairs
} -setup {
} -body {
  set result ""
  foreach {a b} {1 2 3 4} {x y} {A B C D} {
    append result "($a,$b,$x,$y) "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain x
  unset -nocomplain y
} -result {(1,2,A,B) (3,4,C,D) }}

###############################################################################

runTest {test controlflow-1.2 {
  R-55480-36320: foreach with multiple pairs, uneven lengths
} -setup {
} -body {
  set result ""
  foreach a {1 2 3} x {A B} {
    append result "($a,$x) "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain a
  unset -nocomplain x
} -result {(1,A) (2,B) (3,) }}

###############################################################################

runTest {test controlflow-1.3 {
  R-55480-36320: foreach with multiple pairs, second list longer
} -setup {
} -body {
  set result ""
  foreach a {1 2} x {A B C} {
    append result "($a,$x) "
  }
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain a
  unset -nocomplain x
} -result {(1,A) (2,B) (,C) }}

###############################################################################
#
# Section 2 -- switch: -- ends options
#
###############################################################################

runTest {test controlflow-2.1 {
  R-25901-40000: switch -- ends options, allows dash in string
} -setup {
} -body {
  set result [switch -- "-value" {
    -value  {set x "matched -value"}
    default {set x "default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched -value}}

###############################################################################

runTest {test controlflow-2.2 {
  R-25901-40000: switch -- prevents -glob as option
} -setup {
} -body {
  set result [switch -- "-glob" {
    -glob   {set x "matched -glob literally"}
    default {set x "default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched -glob literally}}

###############################################################################

runTest {test controlflow-2.3 {
  R-25901-40000: switch -- prevents -exact as option
} -setup {
} -body {
  set result [switch -- "-exact" {
    -exact  {set x "matched -exact literally"}
    default {set x "default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched -exact literally}}

###############################################################################
#
# Section 3 -- switch: default matches anything
#
###############################################################################

runTest {test controlflow-3.1 {
  R-42835-45040: switch default matches anything
} -setup {
} -body {
  set result [switch "no-match-here" {
    alpha   {set x "alpha"}
    beta    {set x "beta"}
    default {set x "caught by default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {caught by default}}

###############################################################################

runTest {test controlflow-3.2 {
  R-42835-45040: switch default matches any unmatched value
} -setup {
} -body {
  set result [switch "completely random string 12345" {
    one     {set x "one"}
    two     {set x "two"}
    three   {set x "three"}
    default {set x "default branch"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {default branch}}

###############################################################################

runTest {test controlflow-3.3 {
  R-42835-45040: switch default not reached when match exists
} -setup {
} -body {
  set result [switch "two" {
    one     {set x "one"}
    two     {set x "two"}
    default {set x "default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {two}}

###############################################################################
#
# Section 4 -- return -code
#
###############################################################################

runTest {test controlflow-4.1 {
  R-34816-21014: return -code 0 produces ok return code
} -setup {
} -body {
  proc testReturnCode0 {} {
    return -code 0 "normal value"
  }
  set rc [catch {testReturnCode0} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  catch {rename testReturnCode0 ""}
} -result {0 {normal value}}}

###############################################################################

runTest {test controlflow-4.2 {
  R-34816-21014: return -code 1 produces error return code
} -setup {
} -body {
  proc testReturnCode1 {} {
    return -code 1 "error message"
  }
  set rc [catch {testReturnCode1} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  catch {rename testReturnCode1 ""}
} -result {1 {error message}}}

###############################################################################

runTest {test controlflow-4.3 {
  R-34816-21014: return -code 0 value behaves like normal return
} -setup {
} -body {
  proc testReturnNormal {} {
    return -code 0 "hello"
  }
  set val [testReturnNormal]
  set val
} -cleanup {
  unset -nocomplain val
  catch {rename testReturnNormal ""}
} -result {hello}}

###############################################################################
#
# Section 5 -- return -code error
#
###############################################################################

runTest {test controlflow-5.1 {
  R-35585-00247: return -code error msg, catch sees code 1
} -setup {
} -body {
  proc testReturnError {} {
    return -code error "something went wrong"
  }
  set rc [catch {testReturnError} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  catch {rename testReturnError ""}
} -result {1 {something went wrong}}}

###############################################################################

runTest {test controlflow-5.2 {
  R-35585-00247: return -code error behaves like error command
} -setup {
} -body {
  proc testRetErr {} {
    return -code error "oops"
  }
  proc testError {} {
    error "oops"
  }
  set rc1 [catch {testRetErr} msg1]
  set rc2 [catch {testError} msg2]
  list [expr {$rc1 == $rc2}] [expr {$msg1 eq $msg2}]
} -cleanup {
  unset -nocomplain rc1
  unset -nocomplain msg1
  unset -nocomplain rc2
  unset -nocomplain msg2
  catch {rename testRetErr ""}
  catch {rename testError ""}
} -result {1 1}}

###############################################################################
#
# Section 6 -- return code 2 = return from procedure
#
###############################################################################

runTest {test controlflow-6.1 {
  R-26834-28099: return code 2 = return from procedure
} -setup {
} -body {
  set rc [catch {return "from proc"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {2 {from proc}}}

###############################################################################

runTest {test controlflow-6.2 {
  R-26834-28099: return in proc yields code 2 to catch at that level
} -setup {
} -body {
  proc testReturn {} {
    return "my value"
  }
  set rc [catch {testReturn} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  catch {rename testReturn ""}
} -result {0 {my value}}}

###############################################################################

runTest {test controlflow-6.3 {
  R-26834-28099: return code 2 intercepted by catch in script context
} -setup {
} -body {
  set rc [catch {return "intercepted"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {2 intercepted}}

###############################################################################
#
# Section 7 -- source: records script name for info script
#
###############################################################################

runTest {test controlflow-7.1 {
  R-61728-51322: source records script name for info script
} -setup {
} -body {
  source tests/helpers/info_script.tcl
  string match *info_script.tcl $::_cf_test_script
} -cleanup {
  unset -nocomplain ::_cf_test_script
} -result {1}}

###############################################################################

runTest {test controlflow-7.2 {
  R-61728-51322: info script inside sourced file returns sourced name
} -setup {
} -body {
  source tests/helpers/info_script.tcl
  string match *info_script.tcl $::_cf_test_script
} -cleanup {
  unset -nocomplain ::_cf_test_script
} -result {1}}

###############################################################################
#
# Section 8 -- source: nested source maintains name stack
#
###############################################################################

runTest {test controlflow-8.1 {
  R-05497-60890: nested source maintains name stack
} -setup {
} -body {
  source tests/helpers/info_script_outer.tcl
  list \
      [string match *info_script_outer.tcl $::_cf_before_inner] \
      [string match *info_script_inner.tcl $::_cf_inner_script] \
      [string match *info_script_outer.tcl $::_cf_after_inner]
} -cleanup {
  unset -nocomplain ::_cf_before_inner
  unset -nocomplain ::_cf_inner_script
  unset -nocomplain ::_cf_after_inner
} -result {1 1 1}}

###############################################################################

runTest {test controlflow-8.2 {
  R-05497-60890: after nested source returns, info script returns outer name
} -setup {
} -body {
  source tests/helpers/info_script_outer.tcl
  string match *info_script_outer.tcl $::_cf_after_inner
} -cleanup {
  unset -nocomplain ::_cf_before_inner
  unset -nocomplain ::_cf_inner_script
  unset -nocomplain ::_cf_after_inner
} -result {1}}

###############################################################################

source tests/epilogue.tcl
