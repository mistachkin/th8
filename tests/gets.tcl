###############################################################################
#
# gets.tcl --
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
# Section 1 -- gets: argument validation
#
###############################################################################

runTest {test gets-1.1 {
  gets: wrong number of arguments (zero)
} -constraints {
    gets
} -setup {
} -body {
  list [catch {gets} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {wrong # args: *}}}

###############################################################################

runTest {test gets-1.2 {
  gets: wrong number of arguments (too many)
} -constraints {
    gets
} -setup {
} -body {
  list [catch {gets stdin x y} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {wrong # args: *}}}

###############################################################################

runTest {test gets-1.3 {
  gets: invalid channel name
} -constraints {
    gets
} -body {
  gets badchannel
} -returnCodes 1 -match glob -result {*badchannel*}}

###############################################################################

runTest {test gets-1.4 {
  gets: only stdin channel is supported
} -constraints {
    gets not_eagle
} -body {
  gets stdout
} -returnCodes 1 -match glob -result {*stdout*}}

###############################################################################
#
# Section 2 -- gets: reading from stdin via subprocess
#
# These tests pipe data into a subprocess so that gets can read from
# stdin.  They require the exec command (available in Tcl, not TH8).
# Each test uses a helper script in tests/helpers/ and feeds stdin
# data via exec ... << data.
#
###############################################################################

runTest {test gets-2.1 {
  R-07337-52920: gets reads one line, excluding the newline R-62412-53550:
                 without a variable name, gets returns the line
} -constraints {
    gets test_only_exec not_eagle
} -body {
  test_only_exec tests/helpers/gets_novar.tcl \
      << "hello world\n"
} -result {hello world}}

###############################################################################

runTest {test gets-2.2 {
  R-12363-41715: with a variable, gets stores the line and returns the
                 character count
} -constraints {
    gets test_only_exec not_eagle
} -body {
  test_only_exec tests/helpers/gets_var.tcl \
      << "abc\n"
} -result {3 abc}}

###############################################################################

runTest {test gets-2.3 {
  R-07337-52920: gets strips trailing newline from the line
} -constraints {
    gets test_only_exec not_eagle
} -body {
  test_only_exec tests/helpers/gets_novar.tcl \
      << "hello\n"
} -result {hello}}

###############################################################################

runTest {test gets-2.4 {
  R-62412-53550: gets returns empty string on empty input line
} -constraints {
    gets test_only_exec not_eagle
} -body {
  test_only_exec tests/helpers/gets_novar.tcl \
      << "\n"
} -result {}}

###############################################################################

runTest {test gets-2.5 {
  R-12363-41715: gets with variable returns -1 on EOF
} -constraints {
    th8 gets test_only_exec close
} -body {
  test_only_exec tests/helpers/gets_eof.tcl
} -result {-1}}

###############################################################################
#
# Section 3 -- puts -nonewline
#
# The puts command is tested in coverage2.tcl for basic functionality.
# This section covers the -nonewline option (R-06032-53760) which
# requires subprocess capture to verify output content.
#
###############################################################################

runTest {test gets-3.1 {
  R-06032-53760: puts -nonewline suppresses the trailing newline
} -constraints {
    puts test_only_exec not_eagle
} -body {
  test_only_exec tests/helpers/puts_nonewline.tcl
} -result {helloworld}}

###############################################################################

runTest {test gets-3.2 {
  R-24090-61568: puts (without -nonewline) appends newline R-06032-53760:
                 contrast with -nonewline variant
} -constraints {
    puts test_only_exec not_eagle
} -body {
  #
  # Two puts without -nonewline produce two lines.
  # Captured by exec, the trailing newline is stripped
  # but the intermediate one remains.
  #
  test_only_exec tests/helpers/puts_newline.tcl
} -result {A
B}}

###############################################################################
#
# Section 4 -- flush: result clearing
#
###############################################################################

runTest {test gets-4.1 {
  R-39389-57537: flush stdout returns empty string
} -constraints {
    flush
} -body {
  flush stdout
} -result {}}

###############################################################################

runTest {test gets-4.2 {
  R-39389-57537: flush does not leak previous result
} -constraints {
    flush
} -setup {
} -body {
  set x "sentinel"
  set x [flush stdout]
  set x
} -cleanup {
  unset -nocomplain x
} -result {}}

###############################################################################

source tests/epilogue.tcl
