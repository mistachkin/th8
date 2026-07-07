###############################################################################
#
# evaluation.tcl --
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
# Section 1 -- basic evaluation
#
###############################################################################

runTest {test evaluation-1.1 {
  R-18921-24435: interpreter evaluates script by parsing into commands
} -setup {
} -body {
  set x 1
  set y 2
  set z [expr {$x + $y}]
} -cleanup {
  unset -nocomplain x
  unset -nocomplain y
  unset -nocomplain z
} -result {3}}

###############################################################################

runTest {test evaluation-1.2 {
  R-59159-45407: result of script = result of last command
} -setup {
} -body {
  set x 10
  set y 20
  expr {$x + $y}
} -cleanup {
  unset -nocomplain x
  unset -nocomplain y
} -result {30}}

###############################################################################

runTest {test evaluation-1.3 {
  R-34446-28679: every command produces return code + result
} -setup {
} -body {
  set rc [catch {set x 42} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  unset -nocomplain x
} -result {0 42}}

###############################################################################

runTest {test evaluation-1.4 {
  R-00216-15651: return code 0 = normal completion
} -setup {
} -body {
  set rc [catch {expr {1 + 1}}]
} -cleanup {
  unset -nocomplain rc
} -result {0}}

###############################################################################
#
# Section 2 -- control flow return codes
#
###############################################################################

runTest {test evaluation-2.1 {
  R-21109-56507: if evaluates at most one body
} -setup {
} -body {
  set count 0
  if {1} then {
    incr count
  } elseif {1} then {
    incr count
  }
  set count
} -cleanup {
  unset -nocomplain count
} -result {1}}

###############################################################################

runTest {test evaluation-2.2 {
  R-27348-35115: break terminates innermost loop
} -setup {
} -body {
  set result ""
  for {set i 0} {$i < 10} {incr i} {
    if {$i == 3} then {break}
    append result $i
  }
  set result
} -cleanup {
  unset -nocomplain i
  unset -nocomplain result
} -result {012}}

###############################################################################

runTest {test evaluation-2.3 {
  R-46740-18674: continue skips to next iteration
} -setup {
} -body {
  set result ""
  for {set i 0} {$i < 5} {incr i} {
    if {$i == 2} then {continue}
    append result $i
  }
  set result
} -cleanup {
  unset -nocomplain i
  unset -nocomplain result
} -result {0134}}

###############################################################################

runTest {test evaluation-2.4 {
  R-55092-57883: return causes proc to return with value
} -setup {
} -body {
  proc myproc {} {
    return "hello"
  }
  myproc
} -cleanup {
  catch {rename myproc ""}
} -result {hello}}

###############################################################################

runTest {test evaluation-2.5 {
  R-50620-47870: return default value is empty string
} -setup {
} -body {
  proc myproc {} {
    return
  }
  myproc
} -cleanup {
  catch {rename myproc ""}
} -result {}}

###############################################################################

runTest {test evaluation-2.6 {
  R-63972-27573: catch returns 2 for return
} -setup {
} -body {
  set rc [catch {return "val"}]
} -cleanup {
  unset -nocomplain rc
} -result {2}}

###############################################################################

runTest {test evaluation-2.7 {
  R-07360-42944: catch returns 3 for break
} -setup {
} -body {
  set rc [catch {break}]
} -cleanup {
  unset -nocomplain rc
} -result {3}}

###############################################################################

runTest {test evaluation-2.8 {
  R-44545-30306: catch returns 4 for continue
} -setup {
} -body {
  set rc [catch {continue}]
} -cleanup {
  unset -nocomplain rc
} -result {4}}

###############################################################################
#
# Section 3 -- variable system
#
###############################################################################

runTest {test evaluation-3.1 {
  R-27171-24066: variable created on first assignment
} -setup {
} -body {
  set newvar123 "created"
  info exists newvar123
} -cleanup {
  unset -nocomplain newvar123
} -result {1}}

###############################################################################

runTest {test evaluation-3.2 {
  R-07656-00984: reading nonexistent variable = error
} -setup {
} -body {
  list [catch {set nosuchvar_xyzzy} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test evaluation-3.3 {
  R-24298-16451: array = collection indexed by string keys
} -setup {
} -body {
  set arr(name) "Alice"
  set arr(age) "30"
  list $arr(name) $arr(age)
} -cleanup {
  unset -nocomplain arr
} -result {Alice 30}}

###############################################################################

runTest {test evaluation-3.4 {
  R-07480-05554: array element syntax varName(key)
} -setup {
} -body {
  set data(x) 10
  set data(y) 20
  expr {$data(x) + $data(y)}
} -cleanup {
  unset -nocomplain data
} -result {30}}

###############################################################################

runTest {test evaluation-3.5 {
  R-49650-33951: variable is scalar or array, never both
} -setup {
} -body {
  set v "scalar"
  list [catch {set v(key) "array"} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain v
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 4 -- eval loop: substitution, lookup, and invocation
#
###############################################################################

runTest {test evaluation-4.1 {
  R-27915-59612: interpreter performs substitutions on each word, looks up the
                 command name, and invokes with substituted arguments
} -setup {
} -body {
  set _ev3_x world
  set _ev3_result [string length "hello $_ev3_x"]
  set _ev3_result
} -cleanup {
  unset -nocomplain _ev3_x _ev3_result
} -result {11}}

###############################################################################

source tests/epilogue.tcl
