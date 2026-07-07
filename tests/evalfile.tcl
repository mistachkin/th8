###############################################################################
#
# evalfile.tcl --
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
# Section 1 -- Th8_EvalFile: basic functionality
#
###############################################################################

runTest {test evalfile-1.1 {
  R-37343-58361: evalfile succeeds and sets a variable
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::evalfile tests/helpers/evalfile_test.tcl
  set ::_evalfile_test_result
} -cleanup {
  unset -nocomplain ::_evalfile_test_result
} -result {evalfile_ok}}

###############################################################################

runTest {test evalfile-1.2 {
  R-15788-25921: info script during evalfile returns the file name
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::evalfile tests/helpers/evalfile_script.tcl
  set ::_evalfile_script
} -cleanup {
  unset -nocomplain ::_evalfile_script
} -result {tests/helpers/evalfile_script.tcl}}

###############################################################################

runTest {test evalfile-1.3 {
  R-26048-05083: evalfile on nonexistent file returns error
} -constraints {
    loadLib th8
} -setup {
} -body {
  list [catch {th8testlib::evalfile nonexistent_file.tcl} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test evalfile-1.4 {
  R-26048-05083: evalfile with script error propagates the error
} -constraints {
    loadLib th8
} -setup {
} -body {
  list [catch {th8testlib::evalfile tests/helpers/evalfile_error.tcl} msg] \
      $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {intentional evalfile error}}}

###############################################################################
#
# Section 2 -- source: return value
#
###############################################################################

runTest {test evalfile-2.1 {
  R-31363-07661: source returns result of last command in file
} -constraints {
    th8
} -setup {
} -body {
  set result [th8testlib::evalfile tests/helpers/evalfile_test.tcl]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain ::_evalfile_test_result
} -result {evalfile_ok}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
