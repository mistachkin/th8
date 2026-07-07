###############################################################################
#
# list.tcl --
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
# Section 1 -- list: Construct a proper Tcl list
#
###############################################################################

runTest {test list-1.1 {
  R-12427-49975: list with no arguments returns empty list
} -body {
  list
} -result {}}

###############################################################################

runTest {test list-1.2 {
  R-11387-53455: list with single element
} -body {
  list hello
} -result {hello}}

###############################################################################

runTest {test list-1.3 {
  R-05434-27940: list with multiple elements
} -body {
  list a b c d
} -result {a b c d}}

###############################################################################

runTest {test list-1.4 {
  R-05434-27940: list with element containing spaces
} -body {
  list "hello world" foo
} -result {{hello world} foo}}

###############################################################################

runTest {test list-1.5 {
  R-11387-53455: list with empty string element
} -body {
  list "" b c
} -result {{} b c}}

###############################################################################

runTest {test list-1.6 {
  R-05434-27940: list with special characters (braces)
} -body {
  list "\{" "\}"
} -result "\\{ \\}"}

###############################################################################

runTest {test list-1.7 {
  R-05434-27940: list with backslash characters
} -body {
  list "a\\b" "c\\d"
} -result {{a\b} {c\d}}}

###############################################################################

runTest {test list-1.8 {
  R-11387-53455: list with nested list element
} -setup {
} -body {
  set inner [list a b c]
  list $inner d e
} -cleanup {
  unset -nocomplain inner
} -result {{a b c} d e}}

###############################################################################

runTest {test list-1.9 {
  R-11387-53455: list preserves element count
} -setup {
} -body {
  set result [list a b c d e]
  llength $result
} -cleanup {
  unset -nocomplain result
} -result {5}}

###############################################################################

runTest {test list-1.10 {
  R-05434-27940: list with numeric elements
} -body {
  list 1 2 3 4 5
} -result {1 2 3 4 5}}

###############################################################################

runTest {test list-1.11 {
  R-05434-27940: list with tab and newline in element
} -body {
  list "a\tb" "c\nd"
} -match glob -result "*a*b*c*d*"}

###############################################################################

runTest {test list-1.12 {
  R-11387-53455: list result is valid for llength
} -setup {
} -body {
  set x [list "hello world" foo bar]
  llength $x
} -cleanup {
  unset -nocomplain x
} -result {3}}

###############################################################################

source tests/epilogue.tcl
