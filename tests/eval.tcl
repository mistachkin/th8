###############################################################################
#
# eval.tcl --
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
# Section 1 -- eval: Single argument
#
###############################################################################

runTest {test eval-1.1 {
  R-10747-51512: eval single script argument
} -setup {
} -body {
  set result [eval {expr {1 + 2}}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {3}}

###############################################################################

runTest {test eval-1.2 {
  R-10747-51512: eval single arg with set
} -setup {
} -body {
  eval {set x "hello"}
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test eval-1.3 {
  R-10747-51512: eval single arg with multiple commands
} -setup {
} -body {
  eval {
    set x 10
    set y 20
  }
  expr {$x + $y}
} -cleanup {
  unset -nocomplain x
  unset -nocomplain y
} -result {30}}

###############################################################################

runTest {test eval-1.4 {
  R-42027-41333: eval returns result of last command
} -setup {
} -body {
  set result [eval {
    expr {1 + 1}
    expr {2 + 2}
  }]
  set result
} -cleanup {
  unset -nocomplain result
} -result {4}}

###############################################################################
#
# Section 2 -- eval: Multi-arg concatenation
#
###############################################################################

runTest {test eval-2.1 {
  R-10747-51512: eval concatenates multiple args
} -setup {
} -body {
  set result [eval set x 42]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {42}}

###############################################################################

runTest {test eval-2.2 {
  R-10747-51512: eval concatenation forms valid command
} -setup {
} -body {
  set result [eval expr {1 + 2}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {3}}

###############################################################################

runTest {test eval-2.3 {
  R-10747-51512: eval with list to build command
} -setup {
} -body {
  set cmd [list set x "hello world"]
  set result [eval $cmd]
  set result
} -cleanup {
  unset -nocomplain cmd
  unset -nocomplain result
  unset -nocomplain x
} -result {hello world}}

###############################################################################
#
# Section 3 -- eval: Nested eval
#
###############################################################################

runTest {test eval-3.1 {
  R-42027-41333: nested eval
} -setup {
} -body {
  set result [eval {eval {expr {3 + 4}}}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {7}}

###############################################################################

runTest {test eval-3.2 {
  R-42027-41333: double nested eval
} -setup {
} -body {
  set result [eval {eval {eval {expr {5 * 2}}}}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {10}}

###############################################################################

runTest {test eval-3.3 {
  R-10747-51512: nested eval with variable passing
} -setup {
} -body {
  set x 100
  set result [eval {eval {set x}}]
  set result
} -cleanup {
  unset -nocomplain x
  unset -nocomplain result
} -result {100}}

###############################################################################
#
# Section 4 -- eval: Variable scope
#
###############################################################################

runTest {test eval-4.1 {
  R-10747-51512: eval executes in caller scope
} -setup {
} -body {
  eval {set x 42}
  set x
} -cleanup {
  unset -nocomplain x
} -result {42}}

###############################################################################

runTest {test eval-4.2 {
  R-42027-41333: eval can read caller variables
} -setup {
} -body {
  set x "from caller"
  set result [eval {set x}]
  set result
} -cleanup {
  unset -nocomplain x
  unset -nocomplain result
} -result {from caller}}

###############################################################################

runTest {test eval-4.3 {
  R-10747-51512: eval can modify caller variables
} -setup {
} -body {
  set x "before"
  eval {set x "after"}
  set x
} -cleanup {
  unset -nocomplain x
} -result {after}}

###############################################################################
#
# Section 5 -- eval: Error cases
#
###############################################################################

runTest {test eval-5.1 {
  R-42027-41333: eval propagates errors
} -setup {
} -body {
  set rc [catch {eval {error "eval error"}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1 {eval error}}}

###############################################################################

runTest {test eval-5.2 {
  R-42027-41333: eval with syntax error
} -setup {
} -body {
  set rc [catch {eval {set}} msg]
  set rc
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test eval-5.3 {
  R-10747-51512: eval with wrong # args
} -setup {
} -body {
  list [catch {eval} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test eval-5.4 {
  R-42027-41333: eval with empty string
} -setup {
} -body {
  set result [eval {}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {}}

###############################################################################
#
# Section 6 -- uplevel: absolute level specifiers
#
###############################################################################

runTest {test eval-6.1 {
  R-39968-16588: uplevel #0 accesses global scope
} -setup {
} -body {
  proc _ul_inner {} { uplevel #0 {set _ul_val global} }
  proc _ul_outer {} { _ul_inner }
  _ul_outer
  set _ul_val
} -cleanup {
  catch {rename _ul_inner ""}
  catch {rename _ul_outer ""}
  unset -nocomplain _ul_val
} -result {global}}

###############################################################################

runTest {test eval-6.2 {
  R-39968-16588: uplevel #1 accesses first call frame
} -setup {
} -body {
  proc _ul_inner {} { uplevel #1 {set _ul_val frame1} }
  proc _ul_outer {} {
    set _ul_val unset
    _ul_inner
    set _ul_val
  }
  _ul_outer
} -cleanup {
  catch {rename _ul_inner ""}
  catch {rename _ul_outer ""}
  unset -nocomplain _ul_val
} -result {frame1}}

###############################################################################
#
# Section 7 -- upvar: absolute level specifiers
#
###############################################################################

runTest {test eval-7.1 {
  R-51028-29323: upvar #0 links to global variable
} -setup {
} -body {
  proc _uv_inner {} {
    upvar #0 _uv_global local
    set local 42
  }
  _uv_inner
  set _uv_global
} -cleanup {
  catch {rename _uv_inner ""}
  unset -nocomplain _uv_global
} -result {42}}

###############################################################################

source tests/epilogue.tcl
