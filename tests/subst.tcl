###############################################################################
#
# subst.tcl --
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
# Section 1 -- subst: argument validation
#
###############################################################################

runTest {test subst-1.1 {
  subst: wrong number of arguments (zero)
} -constraints {
    subst
} -setup {
} -body {
  list [catch {subst} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {wrong # args: *}}}

###############################################################################

runTest {test subst-1.2 {
  subst: bad option
} -constraints {
    subst
} -setup {
} -body {
  list [catch {subst -badopt "x"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {bad *}}}

###############################################################################
#
# Section 2 -- subst: basic substitutions
#
###############################################################################

runTest {test subst-2.1 {
  R-00961-13881: subst performs variable substitution
} -constraints {
    subst
} -setup {
  set x hello
} -body {
  subst {$x}
} -cleanup {
  unset x
} -result {hello}}

###############################################################################

runTest {test subst-2.2 {
  R-00961-13881: subst performs command substitution
} -constraints {
    subst
} -body {
  subst {[expr {1+2}]}
} -result {3}}

###############################################################################

runTest {test subst-2.3 {
  R-00961-13881: subst performs backslash substitution
} -constraints {
    subst
} -body {
  subst {a\tb}
} -result "a\tb"}

###############################################################################

runTest {test subst-2.4 {
  R-45467-07298: subst with all three substitution types
} -constraints {
    subst
} -setup {
  set name world
} -body {
  subst {Hello\t$name [expr {2+2}]}
} -cleanup {
  unset name
} -result "Hello\tworld 4"}

###############################################################################

runTest {test subst-2.5 {
  R-54337-10665: braces in subst argument are not special
  (unlike the parser, subst processes inside braces)
} -constraints {
    subst
} -setup {
  set a 44
} -body {
  subst {xyz {$a}}
} -cleanup {
  unset a
} -result {xyz {44}}}

###############################################################################

runTest {test subst-2.6 {
  R-00961-13881: subst with empty string
} -constraints {
    subst
} -body {
  subst {}
} -result {}}

###############################################################################

runTest {test subst-2.7 {
  R-00961-13881: subst with no special characters
} -constraints {
    subst
} -body {
  subst {plain text}
} -result {plain text}}

###############################################################################

runTest {test subst-2.8 {
  R-00961-13881: subst with .varname. syntax
} -constraints {
    subst
} -setup {
  set abc 123
} -body {
  subst {${abc}def}
} -cleanup {
  unset abc
} -result {123def}}

###############################################################################
#
# Section 3 -- subst: -no* flags (disable individual substitutions)
#
###############################################################################

runTest {test subst-3.1 {
  R-24221-26585: -nobackslashes disables backslash substitution
} -constraints {
    subst
} -body {
  subst -nobackslashes {a\nb}
} -result {a\nb}}

###############################################################################

runTest {test subst-3.2 {
  R-36255-16813: -nocommands disables command substitution
} -constraints {
    subst
} -setup {
  set x val
} -body {
  subst -nocommands {$x [expr {1}]}
} -cleanup {
  unset x
} -result {val [expr {1}]}}

###############################################################################

runTest {test subst-3.3 {
  R-29914-10062: -novariables disables variable substitution
} -constraints {
    subst
} -body {
  subst -novariables {$x [expr {1+2}]}
} -result {$x 3}}

###############################################################################

runTest {test subst-3.4 {
  R-24221-26585: all three -no flags at once
} -constraints {
    subst
} -body {
  subst -nobackslashes -nocommands -novariables \
      {$x [cmd] a\nb}
} -result {$x [cmd] a\nb}}

###############################################################################

runTest {test subst-3.5 {
  R-24221-26585: two -no flags
} -constraints {
    subst
} -setup {
  set v 42
} -body {
  subst -nobackslashes -nocommands {$v a\nb}
} -cleanup {
  unset v
} -result {42 a\nb}}

###############################################################################
#
# Section 4 -- subst: TIP #712 positive flags
#
###############################################################################

runTest {test subst-4.1 {
  TIP #712: -variables enables only variable substitution
} -constraints {
    subst tip712
} -setup {
  set x hello
} -body {
  subst -variables {$x [expr {1}] a\nb}
} -cleanup {
  unset x
} -result {hello [expr {1}] a\nb}}

###############################################################################

runTest {test subst-4.2 {
  TIP #712: -commands enables only command substitution
} -constraints {
    subst tip712
} -body {
  subst -commands {$x [expr {1+2}] a\nb}
} -result {$x 3 a\nb}}

###############################################################################

runTest {test subst-4.3 {
  TIP #712: -backslashes enables only backslash substitution
} -constraints {
    subst tip712
} -body {
  subst -backslashes {$x [cmd] a\nb}
} -result {$x [cmd] a
b}}

###############################################################################

runTest {test subst-4.4 {
  TIP #712: -commands -variables (two positive flags)
} -constraints {
    subst tip712
} -setup {
  set v 10
} -body {
  subst -commands -variables {$v=[expr {$v*2}] a\nb}
} -cleanup {
  unset v
} -result {10=20 a\nb}}

###############################################################################

runTest {test subst-4.5 {
  TIP #712: mixing positive and negative flags is an error
} -constraints {
    subst tip712
} -setup {
} -body {
  list [catch {subst -variables -nocommands {x}} msg] \
      [string match "*cannot mix*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 5 -- subst: break/continue/return in command substitutions
#
###############################################################################

runTest {test subst-5.1 {
  R-54301-42546: [break] stops substitution
} -constraints {
    subst
} -body {
  subst {abc,[break],def}
} -result {abc,}}

###############################################################################

runTest {test subst-5.2 {
  R-43408-18524: [continue] replaces command with empty string
} -constraints {
    subst
} -body {
  subst {abc,[continue],def}
} -result {abc,,def}}

###############################################################################

runTest {test subst-5.3 {
  R-39231-13955: [return val] uses the return value
} -constraints {
    subst
} -body {
  subst {abc,[return foo],def}
} -result {abc,foo,def}}

###############################################################################

runTest {test subst-5.4 {
  R-09985-04199: error propagates out of subst
} -constraints {
    subst
} -setup {
} -body {
  list [catch {subst {[error boom]}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 boom}}

###############################################################################

runTest {test subst-5.5 {
  R-54301-42546: [break] after variable substitution
} -constraints {
    subst
} -setup {
  set x hello
} -body {
  subst {$x,[break],world}
} -cleanup {
  unset x
} -result {hello,}}

###############################################################################

runTest {test subst-5.6 {
  R-43408-18524: multiple command substitutions with continue
} -constraints {
    subst
} -body {
  subst {[expr {1}],[continue],[expr {3}]}
} -result {1,,3}}

###############################################################################
#
# Section 6 -- subst: -- end-of-options marker
#
###############################################################################

runTest {test subst-6.1 {
  -- terminates option processing
} -constraints {
    th8 subst
} -body {
  subst -- {-nocommands}
} -result {-nocommands}}

###############################################################################

source tests/epilogue.tcl
