###############################################################################
#
# split.tcl --
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
# Section 8 -- split: Split a string into a list
#
###############################################################################

runTest {test split-8.1 {
  R-34801-30956: split defaults to splitting on whitespace
} -body {
  split "hello world foo"
} -result {hello world foo}}

###############################################################################

runTest {test split-8.2 {
  R-18165-20346: split on custom delimiter character
} -body {
  split "a:b:c:d" :
} -result {a b c d}}

###############################################################################

runTest {test split-8.3 {
  R-1800-0003: split on comma delimiter
} -body {
  split "one,two,three" ,
} -result {one two three}}

###############################################################################

runTest {test split-8.4 {
  R-49049-40006: split with empty splitchars splits each character
} -body {
  split "abc" ""
} -result {a b c}}

###############################################################################

runTest {test split-8.5 {
  R-05599-50617: split with adjacent delimiters produces empty elements
} -body {
  split "a::b::c" :
} -result {a {} b {} c}}

###############################################################################

runTest {test split-8.6 {
  R-1800-0006: split with delimiter at start produces leading empty element
} -body {
  split ":a:b" :
} -result {{} a b}}

###############################################################################

runTest {test split-8.7 {
  R-1800-0007: split with delimiter at end produces trailing empty element
} -body {
  split "a:b:" :
} -result {a b {}}}

###############################################################################

runTest {test split-8.8 {
  R-22346-05431: split of empty string returns empty string
} -body {
  split "" :
} -result {}}

###############################################################################

runTest {test split-8.9 {
  R-1800-0009: split with no delimiter found returns original string
} -body {
  split "hello" :
} -result {hello}}

###############################################################################

runTest {test split-8.10 {
  R-1800-0010: split with multiple different delimiter characters
} -body {
  split "a.b:c.d" .:
} -result {a b c d}}

###############################################################################

runTest {test split-8.11 {
  R-1800-0011: split on tab character
} -body {
  split "a\tb\tc" \t
} -result {a b c}}

###############################################################################

runTest {test split-8.12 {
  R-1800-0012: split on newline character
} -body {
  split "line1\nline2\nline3" \n
} -result {line1 line2 line3}}

###############################################################################

runTest {test split-8.13 {
  R-1800-0013: split result element count matches expected
} -setup {
} -body {
  set result [split "a:b:c:d:e" :]
  llength $result
} -cleanup {
  unset -nocomplain result
} -result {5}}

###############################################################################

runTest {test split-8.err.1 {
  R-1800-0014: split with wrong number of args is error
} -setup {
} -body {
  list [catch {split} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
