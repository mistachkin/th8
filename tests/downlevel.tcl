###############################################################################
#
# downlevel.tcl --
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
# Section 1 -- basic downlevel: round-trip through uplevel
#
###############################################################################

runTest {test downlevel-1.1 {
  R-24561-62433: downlevel evaluates in pre-uplevel frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc innerProc {} {
    downlevel {set x}
  }
  proc outerProc {} {
    set x "from-outer"
    uplevel 1 innerProc
  }
  set x "from-global"
  outerProc
} -cleanup {
  catch {rename outerProc ""}
  catch {rename innerProc ""}
  unset -nocomplain x
} -result {from-outer}}

###############################################################################

runTest {test downlevel-1.2 {
  R-24561-62433: downlevel returns script result
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    uplevel 1 {downlevel {expr {6 * 7}}}
  }
  helper
} -cleanup {
  catch {rename helper ""}
} -result {42}}

###############################################################################

runTest {test downlevel-1.3 {
  R-24561-62433: downlevel can set variables in pre-uplevel frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc setter {} {
    downlevel {set dlresult "written-by-downlevel"}
  }
  proc driver {} {
    set dlresult "original"
    uplevel 1 setter
    set dlresult
  }
  driver
} -cleanup {
  catch {rename setter ""}
  catch {rename driver ""}
  unset -nocomplain dlresult driver
} -result {written-by-downlevel}}

###############################################################################

runTest {test downlevel-1.4 {
  R-24561-62433: downlevel reads variables from pre-uplevel frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc reader {} {
    downlevel {set secret}
  }
  proc driver {} {
    set secret "hidden-value"
    uplevel 1 reader
  }
  driver
} -cleanup {
  catch {rename reader ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {hidden-value}}

###############################################################################
#
# Section 2 -- downlevel error cases
#
###############################################################################

runTest {test downlevel-2.1 {
  R-24561-62433: downlevel with no active uplevel succeeds
} -constraints {
    downlevel
} -body {
  downlevel {expr {1 + 2}}
} -result {3}}

###############################################################################

runTest {test downlevel-2.2 {
  R-24561-62433: downlevel wrong # args
} -constraints {
    downlevel
} -setup {
} -body {
  list [catch {downlevel} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test downlevel-2.3 {
  R-24561-62433: downlevel wrong # args (too many)
} -constraints {
    downlevel
} -setup {
} -body {
  list [catch {downlevel {set x} {set y}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test downlevel-2.4 {
  R-24561-62433: downlevel propagates script errors
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    downlevel {error "boom"}
  }
  proc driver {} {
    uplevel 1 helper
  }
  list [catch {driver} msg] $msg
} -cleanup {
  catch {rename helper ""}
  catch {rename driver ""}
  unset -nocomplain driver msg
} -result {1 boom}}

###############################################################################
#
# Section 3 -- downlevel with multiple uplevel levels
#
###############################################################################

runTest {test downlevel-3.1 {
  R-24561-62433: downlevel from uplevel 1 returns to caller
} -constraints {
    downlevel
} -setup {
} -body {
  proc A {} {
    set localA "in-A"
    uplevel 1 B
  }
  proc B {} {
    downlevel {set localA}
  }
  A
} -cleanup {
  catch {rename A ""}
  catch {rename B ""}
  unset -nocomplain result
} -result {in-A}}

###############################################################################

runTest {test downlevel-3.2 {
  R-24561-62433: downlevel from uplevel #0 returns to proc frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    downlevel {info level}
  }
  proc worker {} {
    set localW "present"
    uplevel #0 helper
  }
  worker
} -cleanup {
  catch {rename worker ""}
  catch {rename helper ""}
} -result {1}}

###############################################################################

runTest {test downlevel-3.3 {
  R-24561-62433: nested uplevel/downlevel pairs
} -constraints {
    downlevel
} -setup {
} -body {
  proc inner {} {
    downlevel {set where}
  }
  proc middle {} {
    set where "middle-frame"
    uplevel 1 inner
  }
  proc outer {} {
    set where "outer-frame"
    middle
  }
  outer
} -cleanup {
  catch {rename outer ""}
  catch {rename middle ""}
  catch {rename inner ""}
} -result {middle-frame}}

###############################################################################

runTest {test downlevel-3.4 {
  R-24561-62433: downlevel accesses correct frame depth
} -constraints {
    downlevel
} -setup {
} -body {
  proc depthCheck {} {
    set myLevel [info level]
    uplevel 1 [list downlevel [list info level]]
  }
  depthCheck
} -cleanup {
  catch {rename depthCheck ""}
} -result {1}}

###############################################################################
#
# Section 4 -- downlevel with return codes
#
###############################################################################

runTest {test downlevel-4.1 {
  R-24561-62433: downlevel propagates return value
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    downlevel {return "done"}
  }
  proc driver {} {
    uplevel 1 helper
  }
  driver
} -cleanup {
  catch {rename helper ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {done}}

###############################################################################

runTest {test downlevel-4.2 {
  R-24561-62433: downlevel propagates break
} -constraints {
    downlevel
} -setup {
} -body {
  proc breaker {} {
    downlevel {break}
  }
  set result ""
  for {set i 0} {$i < 5} {incr i} {
    uplevel 0 breaker
    append result $i
  }
  set result
} -cleanup {
  catch {rename breaker ""}
  unset -nocomplain result
  unset -nocomplain i
} -result {}}

###############################################################################

runTest {test downlevel-4.3 {
  R-24561-62433: downlevel propagates continue
} -constraints {
    downlevel
} -setup {
} -body {
  proc skipper {} {
    downlevel {continue}
  }
  set result ""
  for {set i 0} {$i < 5} {incr i} {
    uplevel 0 skipper
    append result $i
  }
  set result
} -cleanup {
  catch {rename skipper ""}
  unset -nocomplain result
  unset -nocomplain i
} -result {}}

###############################################################################
#
# Section 5 -- downlevel with procs and variable scoping
#
###############################################################################

runTest {test downlevel-5.1 {
  R-24561-62433: downlevel can call procs in pre-uplevel frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc target {} {
    return "target-called"
  }
  proc helper {} {
    downlevel {target}
  }
  proc driver {} {
    uplevel 1 helper
  }
  driver
} -cleanup {
  catch {rename helper ""}
  catch {rename target ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {target-called}}

###############################################################################

runTest {test downlevel-5.2 {
  R-24561-62433: downlevel set persists in pre-uplevel frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    downlevel {set localvar "set-via-downlevel"}
  }
  proc driver {} {
    set localvar "original"
    uplevel 1 helper
    set localvar
  }
  driver
} -cleanup {
  catch {rename helper ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {set-via-downlevel}}

###############################################################################

runTest {test downlevel-5.3 {
  R-24561-62433: downlevel sees local vars of the frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc checker {} {
    downlevel {info exists localvar}
  }
  proc driver {} {
    set localvar "here"
    uplevel 1 checker
  }
  driver
} -cleanup {
  catch {rename checker ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {1}}

###############################################################################

runTest {test downlevel-5.4 {
  R-24561-62433: downlevel sees pre-uplevel frame locals
} -constraints {
    downlevel
} -setup {
} -body {
  proc checker {} {
    downlevel {info exists onlyInProc}
  }
  proc driver {} {
    set onlyInProc "secret"
    uplevel 1 checker
  }
  driver
} -cleanup {
  catch {rename checker ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {1}}

###############################################################################
#
# Section 6 -- downlevel with namespace eval
#
###############################################################################

runTest {test downlevel-6.1 {
  R-24561-62433: downlevel from uplevel inside namespace eval
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    downlevel {namespace current}
  }
  namespace eval ::dlns {
    uplevel 1 helper
  }
} -cleanup {
  catch {namespace delete ::dlns}
  catch {rename helper ""}
} -result {::dlns}}

###############################################################################

runTest {test downlevel-6.2 {
  R-24561-62433: downlevel accesses namespace variables via uplevel
} -constraints {
    downlevel
} -setup {
} -body {
  namespace eval ::dlns {
    variable data "ns-value"
    proc getter {} {
      downlevel {set ::dlns::data}
    }
  }
  proc driver {} {
    uplevel 1 ::dlns::getter
  }
  driver
} -cleanup {
  catch {namespace delete ::dlns}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {ns-value}}

###############################################################################

runTest {test downlevel-6.3 {
  R-24561-62433: downlevel from namespace proc via uplevel
} -constraints {
    downlevel
} -setup {
} -body {
  namespace eval ::dlns {
    proc setMarker {} {
      downlevel {set marker "ns-wrote-it"}
    }
  }
  proc driver {} {
    set marker "untouched"
    uplevel 1 ::dlns::setMarker
    set marker
  }
  driver
} -cleanup {
  catch {namespace delete ::dlns}
  catch {rename driver ""}
  unset -nocomplain marker driver
} -result {ns-wrote-it}}

###############################################################################

runTest {test downlevel-6.4 {
  R-24561-62433: downlevel in imported proc via uplevel
} -constraints {
    downlevel
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export getFrame
    proc getFrame {} {
      downlevel {info level}
    }
  }
  namespace eval ::dstns {
    namespace import ::srcns::getFrame
  }
  proc driver {} {
    uplevel 1 ::dstns::getFrame
  }
  driver
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {1}}

###############################################################################

runTest {test downlevel-6.5 {
  R-24561-62433: downlevel with namespace eval and variable
} -constraints {
    downlevel
} -setup {
} -body {
  namespace eval ::dlns {
    variable counter 0
    proc bumpDown {} {
      downlevel {incr ::dlns::counter}
    }
  }
  proc driver {} {
    uplevel 1 ::dlns::bumpDown
  }
  driver
  driver
  driver
  set ::dlns::counter
} -cleanup {
  catch {namespace delete ::dlns}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {3}}

###############################################################################
#
# Section 7 -- downlevel symmetry: uplevel then downlevel round-trips
#
###############################################################################

runTest {test downlevel-7.1 {
  R-24561-62433: uplevel+downlevel round-trip preserves scope
} -constraints {
    downlevel
} -setup {
} -body {
  proc inner {} {
    set innerVar "in-inner"
    uplevel 1 {downlevel {set innerVar}}
  }
  inner
} -cleanup {
  catch {rename inner ""}
} -result {in-inner}}

###############################################################################

runTest {test downlevel-7.2 {
  R-24561-62433: downlevel result becomes uplevel result
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    set myVal "computed"
    uplevel 1 [list downlevel [list set myVal]]
  }
  helper
} -cleanup {
  catch {rename helper ""}
} -result {computed}}

###############################################################################

runTest {test downlevel-7.3 {
  R-24561-62433: downlevel from global-level uplevel
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    set gval "from-proc"
    uplevel #0 {downlevel {set gval}}
  }
  helper
} -cleanup {
  catch {rename helper ""}
  unset -nocomplain gval
} -result {from-proc}}

###############################################################################
#
# Section 8 -- downlevel with complex scripts
#
###############################################################################

runTest {test downlevel-8.1 {
  R-24561-62433: downlevel with multi-command script
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    downlevel {
      set a 10
      set b 20
      expr {$a + $b}
    }
  }
  proc driver {} {
    set a 0
    set b 0
    uplevel 1 helper
  }
  driver
} -cleanup {
  catch {rename helper ""}
  catch {rename driver ""}
  unset -nocomplain a b driver
} -result {30}}

###############################################################################

runTest {test downlevel-8.2 {
  R-24561-62433: downlevel with list manipulation
} -constraints {
    downlevel
} -setup {
} -body {
  proc helper {} {
    downlevel {
      lappend mylist d e f
      set mylist
    }
  }
  proc driver {} {
    set mylist [list a b c]
    uplevel 1 helper
  }
  driver
} -cleanup {
  catch {rename helper ""}
  catch {rename driver ""}
  unset -nocomplain mylist driver
} -result {a b c d e f}}

###############################################################################

runTest {test downlevel-8.3 {
  R-24561-62433: downlevel with conditional logic
} -constraints {
    downlevel
} -setup {
} -body {
  proc decider {} {
    downlevel {
      if {$flag eq "yes"} then {
        set answer "approved"
      } else {
        set answer "denied"
      }
    }
  }
  proc driver {} {
    set flag "yes"
    uplevel 1 decider
  }
  driver
} -cleanup {
  catch {rename decider ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {approved}}

###############################################################################

runTest {test downlevel-8.4 {
  R-24561-62433: downlevel with loop in pre-uplevel frame
} -constraints {
    downlevel
} -setup {
} -body {
  proc summer {} {
    downlevel {
      set total 0
      foreach item $items {
        incr total $item
      }
      set total
    }
  }
  proc driver {} {
    set items [list 1 2 3 4 5]
    uplevel 1 summer
  }
  driver
} -cleanup {
  catch {rename summer ""}
  catch {rename driver ""}
  unset -nocomplain driver
} -result {15}}

###############################################################################

source tests/epilogue.tcl
