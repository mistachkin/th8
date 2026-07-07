###############################################################################
#
# unset.tcl --
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
# Section 1 -- unset: scalar variables
#
###############################################################################

runTest {test unset-1.1 {
  R-58795-26380: unset removes a scalar variable
} -setup {
  unset -nocomplain x
} -body {
  set x "hello"
  unset x
  info exists x
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test unset-1.2 {
  R-2500-0102: unset variable then access is error
} -setup {
  unset -nocomplain x
  unset -nocomplain msg
} -body {
  set x "hello"
  unset x
  list [catch {set x} msg] $msg
} -cleanup {
  unset -nocomplain x
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test unset-1.3 {
  R-2500-0103: unset multiple variables one at a time
} -setup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -body {
  set a 1
  set b 2
  set c 3
  unset a
  unset b
  unset c
  list [info exists a] [info exists b] [info exists c]
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -result {0 0 0}}

###############################################################################

runTest {test unset-1.4 {
  R-37085-57680: unset returns empty string
} -setup {
  unset -nocomplain x
} -body {
  set x "hello"
  unset x
} -cleanup {
  unset -nocomplain x
} -result {}}

###############################################################################
#
# Section 2 -- unset: array elements
#
###############################################################################

runTest {test unset-2.1 {
  R-33222-17245: unset array element
} -setup {
  unset -nocomplain arr
} -body {
  set arr(a) 1
  set arr(b) 2
  unset arr(a)
  list [info exists arr(a)] [info exists arr(b)]
} -cleanup {
  unset -nocomplain arr
} -result {0 1}}

###############################################################################

runTest {test unset-2.2 {
  R-2500-0202: unset entire array
} -setup {
  unset -nocomplain arr
} -body {
  set arr(a) 1
  set arr(b) 2
  unset arr
  info exists arr
} -cleanup {
  unset -nocomplain arr
} -result {0}}

###############################################################################

runTest {test unset-2.3 {
  R-2500-0203: unset last array element leaves array
} -setup {
  unset -nocomplain arr
} -body {
  set arr(a) 1
  unset arr(a)
  info exists arr(a)
} -cleanup {
  unset -nocomplain arr
} -result {0}}

###############################################################################

runTest {test unset-2.4 {
  R-2500-0204: unset array element then re-create
} -setup {
  unset -nocomplain arr
} -body {
  set arr(x) "old"
  unset arr(x)
  set arr(x) "new"
  set arr(x)
} -cleanup {
  unset -nocomplain arr
} -result {new}}

###############################################################################
#
# Section 3 -- unset: nonexistent variable (error)
#
###############################################################################

runTest {test unset-3.1 {
  R-41556-65059: unset nonexistent variable is error
} -setup {
  unset -nocomplain nosuchvar
  unset -nocomplain msg
} -body {
  list [catch {unset nosuchvar} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test unset-3.2 {
  R-2500-0302: unset nonexistent array element is error
} -setup {
  unset -nocomplain arr
  unset -nocomplain msg
} -body {
  set arr(a) 1
  list [catch {unset arr(nosuch)} msg] $msg
} -cleanup {
  unset -nocomplain arr
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 4 -- unset: in procedures
#
###############################################################################

runTest {test unset-4.1 {
  R-2500-0401: unset local variable in proc
} -setup {
  catch {rename myProc ""}
} -body {
  proc myProc {} {
    set x "local"
    unset x
    info exists x
  }
  myProc
} -cleanup {
  catch {rename myProc ""}
} -result {0}}

###############################################################################

runTest {test unset-4.2 {
  R-2500-0402: unset global link from proc removes global
} -setup {
  unset -nocomplain ::gvar
  catch {rename myProc ""}
} -body {
  set ::gvar "global"
  proc myProc {} {
    global gvar
    unset gvar
  }
  myProc
  info exists ::gvar
} -cleanup {
  unset -nocomplain ::gvar
  catch {rename myProc ""}
} -result {0}}

###############################################################################
#
# Section 5 -- unset: error cases
#
###############################################################################

runTest {test unset-5.1 {
  R-60700-26967: unset with no args is no-op
} -setup {
  unset -nocomplain msg
} -body {
  list [catch {unset} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {0 {}}}

###############################################################################

runTest {test unset-5.2 {
  R-58795-26380: unset of entire array removes all elements
} -setup {
  unset -nocomplain arr
} -body {
  set arr(a) 1
  set arr(b) 2
  set arr(c) 3
  unset arr
  list [info exists arr] \
      [info exists arr(a)] [info exists arr(b)] [info exists arr(c)]
} -cleanup {
  unset -nocomplain arr
} -result {0 0 0 0}}

###############################################################################

runTest {test unset-5.3 {
  R-58795-26380: unset multiple variables in one command
} -setup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -body {
  set a 1
  set b 2
  set c 3
  unset a b c
  list [info exists a] [info exists b] [info exists c]
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -result {0 0 0}}

###############################################################################

runTest {test unset-5.4 {
  R-37085-57680: unset returns empty string verified via capture
} -setup {
  unset -nocomplain x
  unset -nocomplain result
} -body {
  set x "hello"
  set result [unset x]
  list $result [string length $result]
} -cleanup {
  unset -nocomplain x
  unset -nocomplain result
} -result {{} 0}}

###############################################################################

runTest {test unset-5.5 {
  R-33222-17245: unset of array element leaves other elements intact
} -setup {
  unset -nocomplain arr
} -body {
  set arr(x) 10
  set arr(y) 20
  set arr(z) 30
  unset arr(y)
  list [info exists arr(x)] [set arr(x)] \
      [info exists arr(y)] [info exists arr(z)] [set arr(z)]
} -cleanup {
  unset -nocomplain arr
} -result {1 10 0 1 30}}

###############################################################################

runTest {test unset-5.6 {
  R-58795-26380: unset within procedure removes local variable
} -setup {
  catch {rename myUnsetProc ""}
} -body {
  proc myUnsetProc {} {
    set localvar "exists"
    set before [info exists localvar]
    unset localvar
    set after [info exists localvar]
    list $before $after
  }
  myUnsetProc
} -cleanup {
  catch {rename myUnsetProc ""}
} -result {1 0}}

###############################################################################
#
# Section 7 -- unset: -nocomplain and -- options
#
###############################################################################

runTest {test unset-7.1 {
  R-58795-26380: unset -nocomplain suppresses nonexistent var error
} -setup {
  unset -nocomplain nosuchvar
} -body {
  unset -nocomplain nosuchvar
} -result {}}

###############################################################################

runTest {test unset-7.2 {
  R-58795-26380: unset -nocomplain with existing var removes it
} -setup {
  unset -nocomplain x
} -body {
  set x "hello"
  unset -nocomplain x
  info exists x
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test unset-7.3 {
  R-58795-26380: unset -nocomplain with mix of existing and nonexistent
} -setup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -body {
  set a 1
  set c 3
  unset -nocomplain a b c
  list [info exists a] [info exists b] [info exists c]
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -result {0 0 0}}

###############################################################################

runTest {test unset-7.4 {
  R-58795-26380: unset -- allows var names starting with dash
} -setup {
  unset -nocomplain {-myvar}
} -body {
  set {-myvar} "dashed"
  unset -- {-myvar}
  info exists {-myvar}
} -cleanup {
  unset -nocomplain -- {-myvar}
} -result {0}}

###############################################################################

runTest {test unset-7.5 {
  R-60700-26967: unset -nocomplain with no var names is no-op
} -body {
  unset -nocomplain
} -result {}}

###############################################################################

source tests/epilogue.tcl
