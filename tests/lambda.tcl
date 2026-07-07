###############################################################################
#
# lambda.tcl --
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
# Section 1 -- apply: basic lambda application
#
###############################################################################

runTest {test lambda-1.1 {
  R-36957-50481: apply single argument
} -body {
  apply {x {expr {$x * 2}}} 5
} -result {10}}

###############################################################################

runTest {test lambda-1.2 {
  R-36957-50481: apply multiple arguments
} -body {
  apply {{a b} {expr {$a + $b}}} 3 4
} -result {7}}

###############################################################################

runTest {test lambda-1.3 {
  R-36957-50481: apply no arguments
} -body {
  apply {{} {expr {1 + 2}}}
} -result {3}}

###############################################################################

runTest {test lambda-1.4 {
  R-36957-50481: apply string result
} -body {
  apply {{name} {string toupper $name}} "hello"
} -result {HELLO}}

###############################################################################

runTest {test lambda-1.5 {
  R-36957-50481: apply body with multiple commands
} -body {
  apply {{x} {
      set y [expr {$x * 2}]
      set z [expr {$y + 1}]
      set z
  }} 10
} -result {21}}

###############################################################################

runTest {test lambda-1.6 {
  R-49466-26821: apply args variadic parameter
} -body {
  apply {{x args} {list $x $args}} hello a b c
} -result {hello {a b c}}}

###############################################################################

runTest {test lambda-1.7 {
  R-36957-50481: apply return from lambda
} -body {
  apply {{x} {
      if {$x > 0} then {return "positive"}
      return "non-positive"
  }} 5
} -result {positive}}

###############################################################################

runTest {test lambda-1.8 {
  R-36957-50481: apply lambda result is last command
} -body {
  apply {{} {
      set a 1
      set b 2
      expr {$a + $b}
  }}
} -result {3}}

###############################################################################
#
# Section 2 -- apply: error handling
#
###############################################################################

runTest {test lambda-2.1 {
  R-36957-50481: apply wrong # args (too few)
} -setup {
} -body {
  list [catch {apply} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test lambda-2.2 {
  R-49466-26821: apply wrong # args for lambda (too many args)
} -setup {
} -body {
  list [catch {apply {x {set x}} 1 2 3} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test lambda-2.3 {
  R-49466-26821: apply wrong # args for lambda (too few args)
} -setup {
} -body {
  list [catch {apply {{a b} {expr {$a + $b}}} 1} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test lambda-2.4 {
  R-36957-50481: apply error in body propagates
} -setup {
} -body {
  list [catch {apply {{} {error "lambda error"}} } msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {lambda error}}}

###############################################################################

runTest {test lambda-2.5 {
  R-36957-50481: apply malformed lambda (not a list)
} -setup {
} -body {
  list [catch {apply "not-a-lambda"} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 3 -- apply: variable scoping
#
###############################################################################

runTest {test lambda-3.1 {
  R-36957-50481: apply lambda has own local scope
} -setup {
} -body {
  set x "outer"
  apply {{} {set x "inner"}}
  set x
} -cleanup {
  unset -nocomplain x
} -result {outer}}

###############################################################################

runTest {test lambda-3.2 {
  R-36957-50481: apply lambda can access globals via ::
} -setup {
} -body {
  set ::gvar "global"
  apply {{} {set ::gvar}}
} -cleanup {
  unset -nocomplain ::gvar
} -result {global}}

###############################################################################

runTest {test lambda-3.3 {
  R-36957-50481: apply lambda can use upvar
} -setup {
} -body {
  set x 10
  apply {{} {upvar 1 x local; set local [expr {$local + 5}]}}
  set x
} -cleanup {
  unset -nocomplain x
} -result {15}}

###############################################################################
#
# Section 4 -- apply: functional programming patterns
#
###############################################################################

runTest {test lambda-4.1 {
  R-36957-50481: apply map-like pattern with foreach
} -setup {
} -body {
  set result [list]
  foreach item {1 2 3 4 5} {
    lappend result [apply {x {expr {$x * $x}}} $item]
  }
  set result
} -cleanup {
  unset -nocomplain result item
} -result {1 4 9 16 25}}

###############################################################################

runTest {test lambda-4.2 {
  R-36957-50481: apply closure-like with upvar
} -body {
  set total 0
  foreach n {1 2 3 4 5} {
    apply {{val} {
        upvar 1 total t
        set t [expr {$t + $val}]
    }} $n
  }
  set total
} -cleanup {
  unset -nocomplain n total
} -result {15}}

###############################################################################

runTest {test lambda-4.3 {
  R-36957-50481: apply nested apply calls
} -body {
  apply {{x} {
      apply {{y} {expr {$y + 100}}} [expr {$x * 2}]
  }} 5
} -result {110}}

###############################################################################
#
# Section 5 -- nproc: named argument procedures
#
###############################################################################

runTest {test lambda-5.1 {
  R-64857-12453: nproc basic named args
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {x y} {expr {$x + $y}}
  ::nptest x 3 y 4
} -cleanup {
  catch {rename ::nptest ""}
} -result {7}}

###############################################################################

runTest {test lambda-5.2 {
  R-64857-12453: nproc with default values
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {x {y 10}} {expr {$x + $y}}
  list [::nptest x 5] [::nptest x 5 y 20]
} -cleanup {
  catch {rename ::nptest ""}
} -result {15 25}}

###############################################################################

runTest {test lambda-5.3 {
  R-56848-64742: nproc -name default parameter form
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {{-name hello} {-value 42}} {list ${-name} ${-value}}
  ::nptest
} -cleanup {
  catch {rename ::nptest ""}
} -result {hello 42}}

###############################################################################

runTest {test lambda-5.4 {
  R-22328-03699: nproc named args as -name value pairs
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {{-name hello} {-value 42}} {list ${-name} ${-value}}
  ::nptest -name world -value 99
} -cleanup {
  catch {rename ::nptest ""}
} -result {world 99}}

###############################################################################

runTest {test lambda-5.5 {
  R-21440-63588: nproc named args can be reordered
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {{-x 0} {-y 0}} {list ${-x} ${-y}}
  ::nptest -y 20 -x 10
} -cleanup {
  catch {rename ::nptest ""}
} -result {10 20}}

###############################################################################

runTest {test lambda-5.6 {
  R-22328-03699: nproc mixed positional and named
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {a {-opt default}} {list $a ${-opt}}
  list [::nptest a hello] [::nptest a hello -opt world]
} -cleanup {
  catch {rename ::nptest ""}
} -result {{hello default} {hello world}}}

###############################################################################

runTest {test lambda-5.7 {
  R-22328-03699: nproc partial named args
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {{-a 1} {-b 2} {-c 3}} {list ${-a} ${-b} ${-c}}
  ::nptest -b 20
} -cleanup {
  catch {rename ::nptest ""}
} -result {1 20 3}}

###############################################################################

runTest {test lambda-5.8 {
  R-21440-63588: nproc three named args in reverse order
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {{-a 0} {-b 0} {-c 0}} {list ${-a} ${-b} ${-c}}
  ::nptest -c 30 -b 20 -a 10
} -cleanup {
  catch {rename ::nptest ""}
} -result {10 20 30}}

###############################################################################

runTest {test lambda-5.9 {
  R-21440-63588: nproc named args in scrambled order
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {{-x 0} {-y 0} {-z 0} {-w 0}} {list ${-x} ${-y} ${-z} ${-w}}
  ::nptest -w 4 -y 2 -x 1 -z 3
} -cleanup {
  catch {rename ::nptest ""}
} -result {1 2 3 4}}

###############################################################################
#
# Section 6 -- nproc: error handling
#
###############################################################################

runTest {test lambda-6.1 {
  R-64857-12453: nproc wrong # args to define
} -constraints {
    nproc
} -setup {
} -body {
  list [catch {nproc} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test lambda-6.2 {
  R-64857-12453: nproc missing required positional arg
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {x y} {expr {$x + $y}}
  list [catch {::nptest 1} msg] [expr {$msg ne ""}]
} -cleanup {
  catch {rename ::nptest ""}
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test lambda-6.3 {
  R-22328-03699: nproc unknown named arg error
} -constraints {
    nproc
} -setup {
} -body {
  nproc ::nptest {{-x 1}} {set x}
  ::nptest -z 99
} -cleanup {
  catch {rename ::nptest ""}
} -returnCodes {1} -result {procedure "nptest" unsupported argument named "-z"}}

###############################################################################
#
# Section 7 -- napply: named-argument lambda
#
###############################################################################

runTest {test lambda-7.1 {
  R-29574-18521: napply basic lambda
} -constraints {
    napply
} -body {
  napply {{x y} {expr {$x + $y}}} x 3 y 4
} -result {7}}

###############################################################################

runTest {test lambda-7.2 {
  R-29574-18521: napply with multiple args
} -constraints {
    napply
} -body {
  napply {{a b c} {list $a $b $c}} a x b y c z
} -result {x y z}}

###############################################################################

runTest {test lambda-7.3 {
  R-29574-18521: napply returns body result
} -constraints {
    napply
} -body {
  napply {{x} {
      set y [expr {$x * 3}]
      expr {$y + 1}
  }} x 10
} -result {31}}

###############################################################################

runTest {test lambda-7.4 {
  R-29574-18521: napply error in body propagates
} -constraints {
    napply
} -setup {
} -body {
  list [catch {napply {{} {error "napply error"}}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {napply error}}}

###############################################################################

runTest {test lambda-7.5 {
  R-29574-18521: napply wrong # args
} -constraints {
    napply
} -setup {
} -body {
  list [catch {napply} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test lambda-7.6 {
  R-21440-63588: napply named args in reverse order
} -constraints {
    napply
} -body {
  napply {{x y z} {list $x $y $z}} z 3 y 2 x 1
} -result {1 2 3}}

###############################################################################

runTest {test lambda-7.7 {
  R-21440-63588: napply named args in scrambled order
} -constraints {
    napply
} -body {
  napply {{a b c d} {list $a $b $c $d}} c C a A d D b B
} -result {A B C D}}

###############################################################################

source tests/epilogue.tcl
