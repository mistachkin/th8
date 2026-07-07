###############################################################################
#
# pwdcd.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the [pwd] and [cd] commands.
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
# Section 1 -- pwd: current working directory
#
###############################################################################

runTest {test pwdcd-1.1 {
  R-58870-35364: pwd returns current working directory
} -setup {
} -body {
  set result [pwd]
  expr {[string length $result] > 0}
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test pwdcd-1.2 {
  R-58870-35364: pwd returns dot in base directory
} -constraints {
    th8
} -body {
  pwd
} -result {.}}

###############################################################################

runTest {test pwdcd-1.3 {
  R-58870-35364: pwd wrong # args
} -setup {
} -body {
  list [catch {pwd extra} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test pwdcd-1.4 {
  R-61298-50331: dot resolves to base path in file normalize
} -constraints {
    th8
} -body {
  file normalize .
} -result {.}}

###############################################################################

runTest {test pwdcd-1.5 {
  R-19004-61741: file normalize returns relative for paths under base
} -constraints {
    th8
} -setup {
} -body {
  set n [file normalize "./tests"]
  string match "./tests*" $n
} -cleanup {
  unset -nocomplain n
} -result {1}}

###############################################################################

runTest {test pwdcd-1.6 {
  R-13194-38737: xGetCwd returns dot at base directory
} -constraints {
    th8
} -body {
  pwd
} -result {.}}

###############################################################################
#
# Section 2 -- cd: change directory
#
###############################################################################

runTest {test pwdcd-2.1 {
  R-20968-52147: cd to dot succeeds
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  cd .
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd
} -result {}}

###############################################################################

runTest {test pwdcd-2.2 {
  R-57714-55951: cd with no argument defaults to dot
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  cd
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd
} -result {}}

###############################################################################

runTest {test pwdcd-2.3 {
  R-04719-20198: cd to non-dot path rejected
} -constraints {
    th8
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  list [catch {cd /tmp} msg] \
      [string match "*permission denied*" $msg]
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd msg
} -result {1 1}}

###############################################################################

runTest {test pwdcd-2.4 {
  R-20968-52147: cd wrong # args
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  list [catch {cd a b} msg] [expr {$msg ne ""}]
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd msg
} -result {1 1}}

###############################################################################

runTest {test pwdcd-2.5 {
  R-20968-52147: pwd after cd dot is still dot
} -constraints {
    th8
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  cd .
  pwd
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd
} -result {.}}

###############################################################################

runTest {test pwdcd-2.6 {
  R-04719-20198: cd to parent rejected
} -constraints {
    th8
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  list [catch {cd ..} msg] \
      [string match "*permission denied*" $msg]
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd msg
} -result {1 1}}

###############################################################################

runTest {test pwdcd-2.7 {
  R-04719-20198: cd to absolute path rejected
} -constraints {
    th8
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  list [catch {cd /usr/local} msg] \
      [string match "*permission denied*" $msg]
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd msg
} -result {1 1}}

###############################################################################
#
# Section 3 -- callback-unavailable scenarios
#
# These tests verify the error behavior specified by R-04236-17465,
# R-41249-16138, and R-19594-01472 when platform callbacks are NULL.
# They require the th8Eval command (to evaluate in a restricted
# interpreter without file system callbacks).
#
###############################################################################

runTest {test pwdcd-3.1 {
  R-04236-17465: pwd errors when xGetCwd callback unavailable
} -constraints {
    loadLib nulleval
} -setup {
} -body {
  list [catch {th8testlib::nulleval {pwd}} msg] \
      [string match "*access to file system unavailable*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test pwdcd-3.2 {
  R-41249-16138: cd errors when xSetCwd callback unavailable
} -constraints {
    loadLib nulleval
} -setup {
} -body {
  list [catch {th8testlib::nulleval {cd .}} msg] \
      [string match "*access to file system unavailable*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test pwdcd-3.3 {
  R-19594-01472: file normalize returns path unchanged when xNormalizePath
                 callback unavailable R-10315-21713: no normalization callback
                 returns argument unchanged
} -constraints {
    loadLib nulleval
} -setup {
} -body {
  set result [th8testlib::nulleval {file normalize "./test"}]
  string equal $result "./test"
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test pwdcd-3.4 {
  R-03414-21845: file exists returns 0 when xDataExists callback unavailable
} -constraints {
    loadLib nulleval
} -setup {
} -body {
  set result [th8testlib::nulleval {file exists anything}]
} -cleanup {
  unset -nocomplain result
} -result {0}}

###############################################################################
#
# Section 4 -- xSetCwd / xGetCwd security model (Section 29.4)
#
###############################################################################

runTest {test pwdcd-4.1 {
  R-00624-49023: cd accepts dot (base directory)
} -constraints {
    th8
} -setup {
  if {[isTcl] || [isEagle]} then {set _saved_cwd [pwd]}
} -body {
  cd .
} -cleanup {
  if {[isTcl] || [isEagle]} then {cd $_saved_cwd}
  unset -nocomplain _saved_cwd
} -result {}}

###############################################################################

runTest {test pwdcd-4.2 {
  R-00624-49023: cd rejects paths outside base directory
} -constraints {
    th8
} -setup {
} -body {
  list [catch {cd /tmp} msg] \
      [string match "*permission denied*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test pwdcd-4.3 {
  R-64118-16729: pwd returns dot at base directory
} -constraints {
    th8
} -body {
  pwd
} -result {.}}

###############################################################################

runTest {test pwdcd-4.4 {
  R-35192-33230: cd with no xSetCwd callback is silently skipped
} -constraints {
    loadLib nulleval
} -setup {
} -body {
  #
  # In the null interp, xSetCwd is NULL. The cd command
  # should error (no callback available).
  #
  list [catch {th8testlib::nulleval {cd .}} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
