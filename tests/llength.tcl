###############################################################################
#
# llength.tcl --
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
# Section 11 -- llength: Return the number of elements in a list
#
###############################################################################

runTest {test llength-11.1 {
  R-48562-48787: llength of empty list
} -body {
  llength {}
} -result {0}}

###############################################################################

runTest {test llength-11.2 {
  R-48562-48787: llength of single element
} -body {
  llength {hello}
} -result {1}}

###############################################################################

runTest {test llength-11.3 {
  R-48562-48787: llength of multiple elements
} -body {
  llength {a b c d e}
} -result {5}}

###############################################################################

runTest {test llength-11.4 {
  R-48562-48787: llength with nested list
} -body {
  llength {a {b c d} e}
} -result {3}}

###############################################################################

runTest {test llength-11.5 {
  R-48562-48787: llength with element containing spaces
} -setup {
} -body {
  set mylist [list "hello world" foo bar]
  llength $mylist
} -cleanup {
  unset -nocomplain mylist
} -result {3}}

###############################################################################

runTest {test llength-11.6 {
  R-48562-48787: llength with empty string element
} -setup {
} -body {
  set mylist [list "" a b]
  llength $mylist
} -cleanup {
  unset -nocomplain mylist
} -result {3}}

###############################################################################

runTest {test llength-11.7 {
  R-48562-48787: llength of list built by lappend
} -setup {
} -body {
  set mylist {}
  lappend mylist a
  lappend mylist b
  lappend mylist c
  llength $mylist
} -cleanup {
  unset -nocomplain mylist
} -result {3}}

###############################################################################

runTest {test llength-11.8 {
  R-48562-48787: llength of list from split
} -body {
  llength [split "a:b:c:d" :]
} -result {4}}

###############################################################################

runTest {test llength-11.9 {
  R-48562-48787: llength with numeric elements
} -body {
  llength {1 2 3 4 5 6 7 8 9 10}
} -result {10}}

###############################################################################

runTest {test llength-11.10 {
  R-48562-48787: llength of list constructed by list command
} -body {
  llength [list a b c d]
} -result {4}}

###############################################################################

runTest {test llength-11.11 {
  R-48562-48787: llength deeply nested list counts outer elements
} -body {
  llength {{a b} {c d} {e f}}
} -result {3}}

###############################################################################

runTest {test llength-11.12 {
  R-48562-48787: llength with special characters in elements
} -setup {
} -body {
  set mylist [list "a\tb" "c\nd" "e"]
  llength $mylist
} -cleanup {
  unset -nocomplain mylist
} -result {3}}

###############################################################################

runTest {test llength-11.err.1 {
  R-48562-48787: llength wrong # args (zero)
} -setup {
} -body {
  list [catch {llength} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test llength-11.err.2 {
  R-48562-48787: llength wrong # args (too many)
} -setup {
} -body {
  list [catch {llength {a b} extra} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
