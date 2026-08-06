###############################################################################
#
# varsystem.tcl --
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
# Section 1 -- Variable System (Section 8): scalar variables
#
###############################################################################

runTest {test varsystem-1.1 {
  R-45859-12418: scalar variable holds single string value
} -setup {
} -body {
  set x "hello world"
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello world}}

###############################################################################

runTest {test varsystem-1.2 {
  R-45859-12418: scalar variable overwrites previous value
} -setup {
} -body {
  set x "first"
  set x "second"
  set x
} -cleanup {
  unset -nocomplain x
} -result {second}}

###############################################################################
#
# Section 2 -- Variable System (Section 8): name resolution
#
###############################################################################

runTest {test varsystem-2.1 {
  R-08187-14580: unqualified name resolved in local scope
} -setup {
} -body {
  set ::x "global"
  proc localScopeProc {} {
    set x "local"
    return $x
  }
  list [localScopeProc] [set ::x]
} -cleanup {
  catch {rename localScopeProc ""}
  unset -nocomplain ::x
} -result {local global}}

###############################################################################

runTest {test varsystem-2.2 {
  R-29933-20236: :: prefix resolved in global namespace
} -setup {
} -body {
  set ::gvar "global value"
  proc globalPrefixProc {} {
    return $::gvar
  }
  globalPrefixProc
} -cleanup {
  catch {rename globalPrefixProc ""}
  unset -nocomplain ::gvar
} -result {global value}}

###############################################################################

runTest {test varsystem-2.3 {
  R-29933-20236: :: prefix write from proc sets global
} -setup {
} -body {
  proc globalWriteProc {} {
    set ::gwrite "written from proc"
  }
  globalWriteProc
  set ::gwrite
} -cleanup {
  catch {rename globalWriteProc ""}
  unset -nocomplain ::gwrite
} -result {written from proc}}

###############################################################################

runTest {test varsystem-2.4 {
  R-44157-24939: :: separators resolved by namespace path
} -setup {
} -body {
  namespace eval ::nsA {
    namespace eval nsB {
      variable deep "found"
    }
  }
  set ::nsA::nsB::deep
} -cleanup {
  catch {namespace delete ::nsA}
} -result {found}}

###############################################################################

runTest {test varsystem-2.5 {
  R-44157-24939: :: separators write through namespace path
} -setup {
} -body {
  namespace eval ::pathns {
    namespace eval child {}
  }
  set ::pathns::child::val "deep write"
  set ::pathns::child::val
} -cleanup {
  catch {namespace delete ::pathns}
} -result {deep write}}

###############################################################################
#
# Section 3 -- Variable Commands (Section 11): set
#
###############################################################################

runTest {test varsystem-3.1 {
  R-51616-42806: set in proc accesses locals unless declared
} -setup {
} -body {
  set ::x "global"
  proc setLocalProc {} {
    set x "local"
    return $x
  }
  set result [setLocalProc]
  list $result [set ::x]
} -cleanup {
  catch {rename setLocalProc ""}
  unset -nocomplain ::x
  unset -nocomplain result
} -result {local global}}

###############################################################################

runTest {test varsystem-3.2 {
  R-51616-42806: set in proc with global declaration accesses global
} -setup {
} -body {
  set ::x "original"
  proc setGlobalProc {} {
    global x
    set x "modified"
  }
  setGlobalProc
  set ::x
} -cleanup {
  catch {rename setGlobalProc ""}
  unset -nocomplain ::x
} -result {modified}}

###############################################################################

runTest {test varsystem-3.3 {
  R-27266-50469: set with :: resolves in specified namespace
} -setup {
} -body {
  namespace eval ::setns {
    variable val "initial"
  }
  proc setNsProc {} {
    set ::setns::val "updated"
  }
  setNsProc
  set ::setns::val
} -cleanup {
  catch {rename setNsProc ""}
  catch {namespace delete ::setns}
} -result {updated}}

###############################################################################

runTest {test varsystem-3.4 {
  R-27266-50469: set with :: reads from specified namespace
} -setup {
} -body {
  namespace eval ::readns {
    variable data "ns data"
  }
  proc readNsProc {} {
    set ::readns::data
  }
  readNsProc
} -cleanup {
  catch {rename readNsProc ""}
  catch {namespace delete ::readns}
} -result {ns data}}

###############################################################################
#
# Section 4 -- Variable Commands (Section 11): unset -nocomplain and --
#
###############################################################################

runTest {test varsystem-4.1 {
  R-22182-46738: unset -nocomplain suppresses error on nonexistent
} -body {
  unset -nocomplain nosuchvar
} -result {}}

###############################################################################

runTest {test varsystem-4.2 {
  R-22182-46738: unset -nocomplain removes existing variable
} -body {
  set x "exists"
  unset -nocomplain x
  info exists x
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test varsystem-4.3 {
  R-22182-46738: unset -nocomplain with mix of existing and nonexistent
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

runTest {test varsystem-4.4 {
  R-44601-21802: unset -- allows variable names starting with dash
} -body {
  set {-dashvar} "dashed"
  unset -- {-dashvar}
  info exists {-dashvar}
} -cleanup {
  unset -nocomplain -- {-dashvar}
} -result {0}}

###############################################################################

runTest {test varsystem-4.5 {
  R-44601-21802: unset -- with normal variable name
} -body {
  set x "hello"
  unset -- x
  info exists x
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################
#
# Section 5 -- Variable Commands (Section 11): unset stops on first error
#
###############################################################################

runTest {test varsystem-5.1 {
  R-41523-35137: unset stops on first error
} -setup {
} -body {
  set a 1
  set c 3
  list [catch {unset a b c} msg] \
      [info exists a] [info exists b] [info exists c]
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
  unset -nocomplain msg
} -result {1 0 0 1}}

###############################################################################

runTest {test varsystem-5.2 {
  R-41523-35137: unset processes vars left-to-right before error
} -setup {
} -body {
  set x 10
  set y 20
  list [catch {unset x nosuchvar y} msg] [info exists x] [info exists y]
} -cleanup {
  unset -nocomplain x
  unset -nocomplain y
  unset -nocomplain msg
} -result {1 0 1}}

###############################################################################
#
# Section 6 -- Variable Commands (Section 11): unset on array name
#
###############################################################################

runTest {test varsystem-6.1 {
  R-18157-17712: unset on array name removes entire array
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

runTest {test varsystem-6.2 {
  R-18157-17712: unset on array name with many elements
} -setup {
} -body {
  for {set i 0} {$i < 10} {incr i} {
    set arr($i) $i
  }
  unset arr
  info exists arr
} -cleanup {
  unset -nocomplain arr
  unset -nocomplain i
} -result {0}}

###############################################################################
#
# Section 7 -- Variable Commands (Section 11): append
#
###############################################################################

runTest {test varsystem-7.1 {
  R-23915-20716: append concatenates multiple values
} -setup {
} -body {
  set x "start"
  append x "A" "B" "C"
  set x
} -cleanup {
  unset -nocomplain x
} -result {startABC}}

###############################################################################

runTest {test varsystem-7.2 {
  R-23915-20716: append concatenates multiple values to new variable
} -setup {
} -body {
  append x "one" "two" "three"
  set x
} -cleanup {
  unset -nocomplain x
} -result {onetwothree}}

###############################################################################

runTest {test varsystem-7.3 {
  R-23915-20716: append concatenates values preserving order
} -setup {
} -body {
  set x ""
  append x "a" "b" "c" "d" "e"
  set x
} -cleanup {
  unset -nocomplain x
} -result {abcde}}

###############################################################################
#
# Section 8 -- Variable Commands (Section 11): incr
#
###############################################################################

runTest {test varsystem-8.1 {
  R-08027-26429: incr accepts negative increment
} -setup {
} -body {
  set x 10
  incr x -3
  set x
} -cleanup {
  unset -nocomplain x
} -result {7}}

###############################################################################

runTest {test varsystem-8.2 {
  R-08027-26429: incr accepts negative increment to go below zero
} -setup {
} -body {
  set x 5
  incr x -10
  set x
} -cleanup {
  unset -nocomplain x
} -result {-5}}

###############################################################################

runTest {test varsystem-8.3 {
  R-08027-26429: incr accepts negative increment of -1
} -setup {
} -body {
  set x 100
  incr x -1
} -cleanup {
  unset -nocomplain x
} -result {99}}

###############################################################################

runTest {test varsystem-8.4 {
  R-29967-61212: incr with 0 validates integer
} -setup {
} -body {
  set x "not_an_integer"
  list [catch {incr x 0} msg] $msg
} -cleanup {
  unset -nocomplain x
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test varsystem-8.5 {
  R-29967-61212: incr with 0 on valid integer succeeds
} -setup {
} -body {
  set x 42
  incr x 0
} -cleanup {
  unset -nocomplain x
} -result {42}}

###############################################################################
#
# Section 9 -- Variable Commands (Section 11): array exists and array names
#
###############################################################################

runTest {test varsystem-9.1 {
  R-02108-25225: array exists returns 1 for arrays
} -setup {
} -body {
  set arr(x) 1
  array exists arr
} -cleanup {
  unset -nocomplain arr
} -result {1}}

###############################################################################

runTest {test varsystem-9.2 {
  R-02108-25225: array exists returns 0 for scalars
} -setup {
} -body {
  set x "scalar"
  array exists x
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test varsystem-9.3 {
  R-02108-25225: array exists returns 0 for nonexistent
} -setup {
} -body {
  array exists nosuchvar
} -result {0}}

###############################################################################

runTest {test varsystem-9.4 {
  R-34461-28486: array names returns element names
} -setup {
} -body {
  set arr(alpha) 1
  set arr(beta) 2
  set arr(gamma) 3
  lsort [array names arr]
} -cleanup {
  unset -nocomplain arr
} -result {alpha beta gamma}}

###############################################################################

runTest {test varsystem-9.5 {
  R-34461-28486: array names on empty array returns empty list
} -setup {
} -body {
  array set arr {}
  array names arr
} -cleanup {
  unset -nocomplain arr
} -constraints {array_set} -result {}}

###############################################################################
#
# Section 10 -- Variable Commands (Section 11): global
#
###############################################################################

runTest {test varsystem-10.1 {
  R-38546-20769: global link read
} -setup {
  set ::gdata "global read test"
} -body {
  proc globalReadProc {} {
    global gdata
    return $gdata
  }
  globalReadProc
} -cleanup {
  catch {rename globalReadProc ""}
  unset -nocomplain ::gdata
} -result {global read test}}

###############################################################################

runTest {test varsystem-10.2 {
  R-38546-20769: global link write
} -setup {
} -body {
  proc globalWriteProc {} {
    global gdata
    set gdata "written via global"
  }
  globalWriteProc
  set ::gdata
} -cleanup {
  catch {rename globalWriteProc ""}
  unset -nocomplain ::gdata
} -result {written via global}}

###############################################################################

runTest {test varsystem-10.3 {
  R-38546-20769: global link read and write round-trip
} -setup {
  set ::counter 0
} -body {
  proc globalRWProc {} {
    global counter
    set old $counter
    incr counter
    return $old
  }
  list [globalRWProc] [globalRWProc] [globalRWProc] [set ::counter]
} -cleanup {
  catch {rename globalRWProc ""}
  unset -nocomplain ::counter
} -result {0 1 2 3}}

###############################################################################
#
# Section 11 -- Variable Commands (Section 11): upvar
#
###############################################################################

runTest {test varsystem-11.1 {
  R-51028-29323: upvar #0 links to global
} -setup {
  set ::glink "global via upvar"
} -body {
  proc upvarGlobalProc {} {
    upvar #0 glink local
    return $local
  }
  upvarGlobalProc
} -cleanup {
  catch {rename upvarGlobalProc ""}
  unset -nocomplain ::glink
} -result {global via upvar}}

###############################################################################

runTest {test varsystem-11.2 {
  R-51028-29323: upvar #0 write modifies global
} -setup {
  set ::glink "before"
} -body {
  proc upvarGlobalWriteProc {} {
    upvar #0 glink local
    set local "after"
  }
  upvarGlobalWriteProc
  set ::glink
} -cleanup {
  catch {rename upvarGlobalWriteProc ""}
  unset -nocomplain ::glink
} -result {after}}

###############################################################################

runTest {test varsystem-11.3 {
  R-20965-07472: upvar returns empty string
} -setup {
  set x 1
} -body {
  proc upvarReturnProc {} {
    upvar 1 x local
  }
  upvarReturnProc
} -cleanup {
  catch {rename upvarReturnProc ""}
  unset -nocomplain x
} -result {}}

###############################################################################

runTest {test varsystem-11.4 {
  R-20965-07472: upvar return value captured is empty
} -setup {
  set x 1
} -body {
  proc upvarCaptureProc {} {
    set result [upvar 1 x local]
    list $result [string length $result]
  }
  upvarCaptureProc
} -cleanup {
  catch {rename upvarCaptureProc ""}
  unset -nocomplain x
} -result {{} 0}}

###############################################################################

runTest {test varsystem-11.5 {
  R-43175-24686: upvar target created on first write
} -setup {
} -body {
  proc upvarCreateProc {} {
    upvar 1 newvar local
    set before [info exists local]
    set local "created"
    set after [info exists local]
    list $before $after
  }
  set result [upvarCreateProc]
  list $result [set newvar]
} -cleanup {
  catch {rename upvarCreateProc ""}
  unset -nocomplain newvar
  unset -nocomplain result
} -result {{0 1} created}}

###############################################################################

runTest {test varsystem-11.6 {
  R-43175-24686: upvar target does not exist until written
} -setup {
} -body {
  proc upvarNoWriteProc {} {
    upvar 1 phantom local
    info exists phantom
  }
  upvarNoWriteProc
} -cleanup {
  catch {rename upvarNoWriteProc ""}
  unset -nocomplain phantom
} -result {0}}

###############################################################################

source tests/epilogue.tcl
