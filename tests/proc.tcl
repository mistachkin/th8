###############################################################################
#
# proc.tcl --
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
# Section 1 -- proc: basic definition and invocation
#
###############################################################################

runTest {test proc-1.1 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc myProc {} {
    return "hello"
  }
  myProc
} -cleanup {
  catch {rename myProc ""}
} -result {hello}}

###############################################################################

runTest {test proc-1.2 {
  R-32995-09838: each element of argList names a formal parameter
} -setup {
} -body {
  proc myAdd {a b} {
    expr {$a + $b}
  }
  myAdd 3 4
} -cleanup {
  catch {rename myAdd ""}
} -result {7}}

###############################################################################

runTest {test proc-1.3 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc myVal {} {
    set x 10
    set y 20
    expr {$x + $y}
  }
  myVal
} -cleanup {
  catch {rename myVal ""}
} -result {30}}

###############################################################################
#
# Section 2 -- proc: default arguments
#
###############################################################################

runTest {test proc-2.1 {
  R-54322-24989: two-element list {name default} specifies default value
} -setup {
} -body {
  proc myGreet {{name "world"}} {
    return "hello $name"
  }
  myGreet
} -cleanup {
  catch {rename myGreet ""}
} -result {hello world}}

###############################################################################

runTest {test proc-2.2 {
  R-54322-24989: two-element list {name default} specifies default value
} -setup {
} -body {
  proc myGreet {{name "world"}} {
    return "hello $name"
  }
  myGreet "Tcl"
} -cleanup {
  catch {rename myGreet ""}
} -result {hello Tcl}}

###############################################################################

runTest {test proc-2.3 {
  R-54322-24989: two-element list {name default} specifies default value
} -setup {
} -body {
  proc myFunc {a {b 10}} {
    expr {$a + $b}
  }
  list [myFunc 5] [myFunc 5 20]
} -cleanup {
  catch {rename myFunc ""}
} -result {15 25}}

###############################################################################
#
# Section 3 -- proc: args parameter
#
###############################################################################

runTest {test proc-3.1 {
  R-54924-11430: special parameter name args collects remaining arguments
} -setup {
} -body {
  proc myList {args} {
    return $args
  }
  myList a b c
} -cleanup {
  catch {rename myList ""}
} -result {a b c}}

###############################################################################

runTest {test proc-3.2 {
  R-54924-11430: special parameter name args collects remaining arguments
} -setup {
} -body {
  proc myFunc {first args} {
    list $first $args
  }
  myFunc a b c d
} -cleanup {
  catch {rename myFunc ""}
} -result {a {b c d}}}

###############################################################################

runTest {test proc-3.3 {
  R-54924-11430: special parameter name args collects remaining arguments
} -setup {
} -body {
  proc myFunc {args} {
    llength $args
  }
  myFunc
} -cleanup {
  catch {rename myFunc ""}
} -result {0}}

###############################################################################
#
# Section 4 -- proc: wrong # args
#
###############################################################################

runTest {test proc-4.1 {
  R-51060-55401: too few or too many arguments produces error unless args
                 present
} -setup {
} -body {
  proc myFunc {a b} {
    expr {$a + $b}
  }
  list [catch {myFunc 1} msg] $msg
} -cleanup {
  catch {rename myFunc ""}
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test proc-4.2 {
  R-51060-55401: too few or too many arguments produces error unless args
                 present
} -setup {
} -body {
  proc myFunc {a} {
    return $a
  }
  list [catch {myFunc 1 2} msg] $msg
} -cleanup {
  catch {rename myFunc ""}
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 5 -- proc: return value
#
###############################################################################

runTest {test proc-5.1 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc myFunc {} {
    return 42
    return 99
  }
  myFunc
} -cleanup {
  catch {rename myFunc ""}
} -result {42}}

###############################################################################

runTest {test proc-5.2 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc myFunc {} {
    return ""
  }
  myFunc
} -cleanup {
  catch {rename myFunc ""}
} -result {}}

###############################################################################
#
# Section 6 -- proc: recursion
#
###############################################################################

runTest {test proc-6.1 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc factorial {n} {
    if {$n <= 1} then {
      return 1
    }
    expr {$n * [factorial [expr {$n - 1}]]}
  }
  factorial 5
} -cleanup {
  catch {rename factorial ""}
} -result {120}}

###############################################################################

runTest {test proc-6.2 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc fib {n} {
    if {$n <= 1} then {
      return $n
    }
    expr {[fib [expr {$n - 1}]] + [fib [expr {$n - 2}]]}
  }
  fib 8
} -cleanup {
  catch {rename fib ""}
} -result {21}}

###############################################################################
#
# Section 7 -- proc: nested calls
#
###############################################################################

runTest {test proc-7.1 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc double {x} {
    expr {$x * 2}
  }
  proc quadruple {x} {
    double [double $x]
  }
  quadruple 5
} -cleanup {
  catch {rename double ""}
  catch {rename quadruple ""}
} -result {20}}

###############################################################################
#
# Section 8 -- proc: rename
#
###############################################################################

runTest {test proc-8.1 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc myOld {} {
    return "hello"
  }
  rename myOld myNew
  myNew
} -cleanup {
  catch {rename myNew ""}
  catch {rename myOld ""}
} -result {hello}}

###############################################################################

runTest {test proc-8.2 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc myFunc {} {
    return "hello"
  }
  rename myFunc ""
  list [catch {myFunc} msg] $msg
} -cleanup {
  catch {rename myFunc ""}
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 9 -- proc: error cases
#
###############################################################################

runTest {test proc-9.1 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  list [catch {proc} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test proc-9.2 {
  R-33690-08330: proc creates new command, evaluates body in new local scope
} -setup {
} -body {
  proc badProc {} {
    error "deliberate error"
  }
  list [catch {badProc} msg] $msg
} -cleanup {
  catch {rename badProc ""}
  unset -nocomplain msg
} -result {1 {deliberate error}}}

###############################################################################
#
# Section 10 -- proc: implicit return value
#
###############################################################################

runTest {test proc-10.1 {
  R-19079-49744: proc returns result of last command when no explicit return
} -setup {
} -body {
  proc _implicit {} { set x 42 }
  set result [_implicit]
  set result
} -cleanup {
  catch {rename _implicit ""}
  unset -nocomplain result
} -result {42}}

###############################################################################

runTest {test proc-10.2 {
  R-19079-49744: proc returns empty string when body is empty
} -setup {
} -body {
  proc _empty {} {}
  set result [_empty]
  set result
} -cleanup {
  catch {rename _empty ""}
  unset -nocomplain result
} -result {}}

###############################################################################

source tests/epilogue.tcl
