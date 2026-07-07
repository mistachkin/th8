###############################################################################
#
# variable.tcl --
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
# Section 1 -- global: access global variables from procs
#
###############################################################################

runTest {test variable-1.1 {
  R-64783-55603: global reads global variable in proc
} -setup {
  set ::gvar "global value"
} -body {
  proc readGlobal {} {
    global gvar
    return $gvar
  }
  readGlobal
} -cleanup {
  catch {rename readGlobal ""}
  unset -nocomplain ::gvar
} -result {global value}}

###############################################################################

runTest {test variable-1.2 {
  R-2200-0102: global writes global variable from proc
} -setup {
} -body {
  proc writeGlobal {} {
    global gvar
    set gvar "set from proc"
  }
  writeGlobal
  set ::gvar
} -cleanup {
  catch {rename writeGlobal ""}
  unset -nocomplain ::gvar
} -result {set from proc}}

###############################################################################

runTest {test variable-1.3 {
  R-2200-0103: global multiple variables
} -setup {
  set ::ga 1
  set ::gb 2
} -body {
  proc multiGlobal {} {
    global ga gb
    expr {$ga + $gb}
  }
  multiGlobal
} -cleanup {
  catch {rename multiGlobal ""}
  unset -nocomplain ::ga
  unset -nocomplain ::gb
} -result {3}}

###############################################################################
#
# Section 2 -- upvar: link to variables in other frames
#
###############################################################################

runTest {test variable-2.1 {
  R-24299-23982: upvar links variable for read and write
} -setup {
} -body {
  proc addOne {varName} {
    upvar 1 $varName v
    set v [expr {$v + 1}]
  }
  set x 5
  addOne x
} -cleanup {
  catch {rename addOne ""}
  unset -nocomplain x
} -result {6}}

###############################################################################

runTest {test variable-2.2 {
  R-17744-44957: upvar links variable for write from caller
} -setup {
} -body {
  proc setVar {varName value} {
    upvar 1 $varName v
    set v $value
  }
  set x "before"
  setVar x "after"
  set x
} -cleanup {
  catch {rename setVar ""}
  unset -nocomplain x
} -result {after}}

###############################################################################

runTest {test variable-2.3 {
  R-2200-0203: upvar links array element
} -setup {
} -body {
  proc setElem {varName value} {
    upvar 1 $varName v
    set v $value
  }
  set arr(key) "old"
  setElem arr(key) "new"
  set arr(key)
} -cleanup {
  catch {rename setElem ""}
  unset -nocomplain arr
} -result {new}}

###############################################################################

runTest {test variable-2.4 {
  R-35760-03398: upvar creates new variable in caller
} -setup {
} -body {
  proc createVar {varName value} {
    upvar 1 $varName v
    set v $value
  }
  createVar newvar "created"
  set newvar
} -cleanup {
  catch {rename createVar ""}
  unset -nocomplain newvar
} -result {created}}

###############################################################################
#
# Section 3 -- uplevel: evaluate script in caller context
#
###############################################################################

runTest {test variable-3.1 {
  R-2200-0301: uplevel evaluates in caller scope
} -setup {
} -body {
  proc evalUp {script} {
    uplevel 1 $script
  }
  set x 10
  evalUp {set x [expr {$x + 5}]}
  set x
} -cleanup {
  catch {rename evalUp ""}
  unset -nocomplain x
} -result {15}}

###############################################################################

runTest {test variable-3.2 {
  R-2200-0302: uplevel returns script result
} -setup {
} -body {
  proc evalUp {script} {
    uplevel 1 $script
  }
  evalUp {expr {3 + 4}}
} -cleanup {
  catch {rename evalUp ""}
} -result {7}}

###############################################################################

runTest {test variable-3.3 {
  R-2200-0303: uplevel #0 evaluates at global scope
} -setup {
} -body {
  proc evalGlobal {script} {
    uplevel #0 $script
  }
  evalGlobal {set gtest "from global"}
  set ::gtest
} -cleanup {
  catch {rename evalGlobal ""}
  unset -nocomplain ::gtest
} -result {from global}}

###############################################################################

runTest {test variable-3.4 {
  R-2200-0304: uplevel 2 skips one frame
} -setup {
} -body {
  proc inner {} {
    uplevel 2 {set result "from inner"}
  }
  proc outer {} {
    inner
  }
  outer
  set result
} -cleanup {
  catch {rename inner ""}
  catch {rename outer ""}
  unset -nocomplain result
} -result {from inner}}

###############################################################################
#
# Section 4 -- variable: declare namespace variables
#
###############################################################################

runTest {test variable-4.1 {
  R-51486-54706: variable in namespace eval
} -setup {
} -body {
  namespace eval ::testns {
    variable myvar "hello"
  }
  set ::testns::myvar
} -cleanup {
  catch {namespace delete ::testns}
} -result {hello}}

###############################################################################

runTest {test variable-4.2 {
  R-30531-28192: variable in namespace proc via qualified name
} -setup {
} -body {
  namespace eval ::testns {
    variable counter 0
    proc increment {} {
      incr ::testns::counter
    }
  }
  ::testns::increment
  ::testns::increment
  ::testns::increment
  set ::testns::counter
} -cleanup {
  catch {namespace delete ::testns}
} -result {3}}

###############################################################################

runTest {test variable-4.3 {
  R-63785-46872: variable with initial value
} -setup {
} -body {
  namespace eval ::testns {
    variable x 10
    variable y 20
  }
  expr {$::testns::x + $::testns::y}
} -cleanup {
  catch {namespace delete ::testns}
} -result {30}}

###############################################################################
#
# Section 5 -- error cases
#
###############################################################################

runTest {test variable-5.1 {
  R-2200-0501: upvar with bad level is error
} -setup {
} -body {
  list [catch {upvar 99 x y} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test variable-5.2 {
  R-2200-0502: uplevel with bad level is error
} -setup {
} -body {
  list [catch {uplevel 99 {set x 1}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test variable-5.3 {
  R-2200-0503: global at top level has no effect but is not error
} -setup {
} -body {
  global somevar
} -cleanup {
  unset -nocomplain somevar
} -result {}}

###############################################################################
#
# Section 6 -- variable: inside namespace procs (link to namespace variable)
#
###############################################################################

runTest {test variable-6.1 {
  R-51486-54706: variable inside namespace proc reads namespace var
} -setup {
} -body {
  namespace eval ::nsvar {
    variable myval "from namespace"
    proc getVal {} {
      variable myval
      return $myval
    }
  }
  ::nsvar::getVal
} -cleanup {
  catch {namespace delete ::nsvar}
} -result {from namespace}}

###############################################################################

runTest {test variable-6.2 {
  R-30531-28192: variable inside namespace proc with incr modifies namespace
                 var
} -setup {
} -body {
  namespace eval ::nsvar {
    variable counter 0
    proc bump {} {
      variable counter
      incr counter
    }
  }
  ::nsvar::bump
  ::nsvar::bump
  ::nsvar::bump
  set ::nsvar::counter
} -cleanup {
  catch {namespace delete ::nsvar}
} -result {3}}

###############################################################################

runTest {test variable-6.3 {
  R-30531-28192: variable inside imported namespace proc still works
} -setup {
} -body {
  namespace eval ::srcns {
    variable data "shared"
    namespace export readData
    proc readData {} {
      variable data
      return $data
    }
  }
  namespace eval ::dstns {
    namespace import ::srcns::readData
  }
  ::dstns::readData
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
} -result {shared}}

###############################################################################

runTest {test variable-6.4 {
  R-51486-54706: variable with dynamic names works
} -setup {
} -body {
  namespace eval ::dynns {
    variable c_alpha 10
    variable c_beta 20
    proc getCounter {name} {
      variable c_$name
      return [set c_$name]
    }
  }
  list [::dynns::getCounter alpha] [::dynns::getCounter beta]
} -cleanup {
  catch {namespace delete ::dynns}
} -result {10 20}}

###############################################################################

runTest {test variable-6.5 {
  R-30531-28192: multiple variable declarations in one proc all work
} -setup {
} -body {
  namespace eval ::multins {
    variable x 1
    variable y 2
    variable z 3
    proc sum {} {
      variable x
      variable y
      variable z
      expr {$x + $y + $z}
    }
  }
  ::multins::sum
} -cleanup {
  catch {namespace delete ::multins}
} -result {6}}

###############################################################################

runTest {test variable-6.6 {
  R-30531-28192: variable inside nested proc calls preserves correct namespace
} -setup {
} -body {
  namespace eval ::outerns {
    variable val "outer"
    proc getVal {} {
      variable val
      return $val
    }
    namespace eval inner {
      variable val "inner"
      proc getVal {} {
        variable val
        return $val
      }
    }
  }
  list [::outerns::getVal] [::outerns::inner::getVal]
} -cleanup {
  catch {namespace delete ::outerns}
} -result {outer inner}}

###############################################################################

runTest {test variable-6.7 {
  R-30531-28192: variable inside namespace proc allows set to modify namespace
                 var
} -setup {
} -body {
  namespace eval ::setns {
    variable config "default"
    proc setConfig {val} {
      variable config
      set config $val
    }
    proc getConfig {} {
      variable config
      return $config
    }
  }
  ::setns::setConfig "custom"
  ::setns::getConfig
} -cleanup {
  catch {namespace delete ::setns}
} -result {custom}}

###############################################################################

runTest {test variable-6.8 {
  R-51486-54706: variable with initial value inside proc is ignored if already
                 set
} -setup {
} -body {
  namespace eval ::initns {
    variable count 100
    proc getCount {} {
      variable count
      return $count
    }
  }
  ::initns::getCount
} -cleanup {
  catch {namespace delete ::initns}
} -result {100}}

###############################################################################
#
# Section 7 -- variable: declaration without initialization
#
# Tcl 8.4 semantics: [variable foo] without a value declares the
# name but does NOT create/initialize the variable.  [info exists]
# must return 0 until the variable is explicitly assigned.
#
###############################################################################

runTest {test variable-7.1 {
  variable without value: info exists returns 0 inside proc
} -constraints {
    namespace
} -setup {
} -body {
  namespace eval ::vartest7 {
    proc check {} {
      variable undeclared
      info exists undeclared
    }
  }
  ::vartest7::check
} -cleanup {
  catch {namespace delete ::vartest7}
} -result {0}}

###############################################################################

runTest {test variable-7.2 {
  variable with value: info exists returns 1
} -constraints {
    namespace
} -setup {
} -body {
  namespace eval ::vartest7b {
    variable initialized hello
    proc check {} {
      variable initialized
      list [info exists initialized] $initialized
    }
  }
  ::vartest7b::check
} -cleanup {
  catch {namespace delete ::vartest7b}
} -result {1 hello}}

###############################################################################

runTest {test variable-7.3 {
  variable without value: assignment makes it exist
} -constraints {
    namespace
} -setup {
} -body {
  namespace eval ::vartest7c {
    proc check {} {
      variable deferred
      set before [info exists deferred]
      set deferred "now set"
      set after [info exists deferred]
      list $before $after $deferred
    }
  }
  ::vartest7c::check
} -cleanup {
  catch {namespace delete ::vartest7c}
} -result {0 1 {now set}}}

###############################################################################

runTest {test variable-7.4 {
  variable without value: does not pollute namespace
} -constraints {
    namespace
} -setup {
} -body {
  namespace eval ::vartest7d {
    proc declare {} {
      variable phantom
    }
  }
  ::vartest7d::declare
  info exists ::vartest7d::phantom
} -cleanup {
  catch {namespace delete ::vartest7d}
} -result {0}}

###############################################################################
#
# Section 8 -- uplevel: default level
#
###############################################################################

runTest {test variable-8.1 {
  R-17399-64558: for uplevel, the default level is 1 (caller's frame)
} -setup {
} -body {
  proc _v8_inner {} {
    uplevel {set _v8_result "from_caller"}
  }
  proc _v8_outer {} {
    set _v8_result "initial"
    _v8_inner
    set _v8_result
  }
  _v8_outer
} -cleanup {
  catch {rename _v8_inner ""}
  catch {rename _v8_outer ""}
  unset -nocomplain _v8_result
} -result {from_caller}}

###############################################################################

source tests/epilogue.tcl
