###############################################################################
#
# regex.tcl --
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
# Section 1 -- regexp: basic matching
#
###############################################################################

runTest {test regex-1.1 {
  R-10050-09986: regexp basic match returns 1
} -constraints {
    regexp
} -body {
  regexp {abc} "xabcy"
} -result {1}}

###############################################################################

runTest {test regex-1.2 {
  R-38092-61134: regexp no match returns 0
} -constraints {
    regexp
} -body {
  regexp {xyz} "abcdef"
} -result {0}}

###############################################################################

runTest {test regex-1.3 {
  R-10050-09986: regexp anchored match with ^
} -constraints {
    regexp
} -body {
  list [regexp {^abc} "abcdef"] [regexp {^abc} "xabc"]
} -result {1 0}}

###############################################################################

runTest {test regex-1.4 {
  R-10050-09986: regexp anchored match with $
} -constraints {
    regexp
} -body {
  list [regexp {def$} "abcdef"] [regexp {def$} "defx"]
} -result {1 0}}

###############################################################################

runTest {test regex-1.5 {
  R-10050-09986: regexp dot matches any character
} -constraints {
    regexp
} -body {
  regexp {a.c} "axc"
} -result {1}}

###############################################################################

runTest {test regex-1.6 {
  R-10050-09986: regexp star quantifier
} -constraints {
    regexp
} -body {
  regexp {ab*c} "ac"
} -result {1}}

###############################################################################

runTest {test regex-1.7 {
  R-10050-09986: regexp plus quantifier
} -constraints {
    regexp
} -body {
  list [regexp {ab+c} "abc"] [regexp {ab+c} "ac"]
} -result {1 0}}

###############################################################################

runTest {test regex-1.8 {
  R-10050-09986: regexp question quantifier
} -constraints {
    regexp
} -body {
  list [regexp {ab?c} "abc"] [regexp {ab?c} "ac"]
} -result {1 1}}

###############################################################################
#
# Section 2 -- regexp: capture variables
#
###############################################################################

runTest {test regex-2.1 {
  R-31950-35276: regexp capture whole match
} -constraints {
    regexp
} -setup {
} -body {
  regexp {[0-9]+} "abc123def" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {123}}

###############################################################################

runTest {test regex-2.2 {
  R-31950-35276: regexp capture subexpressions
} -constraints {
    regexp
} -setup {
} -body {
  regexp {([a-z]+)([0-9]+)} "abc123" all sub1 sub2
  list $all $sub1 $sub2
} -cleanup {
  unset -nocomplain all
  unset -nocomplain sub1
  unset -nocomplain sub2
} -result {abc123 abc 123}}

###############################################################################
#
# Section 3 -- regexp: options
#
###############################################################################

runTest {test regex-3.1 {
  R-05796-03008: regexp -nocase
} -constraints {
    regexp
} -body {
  regexp -nocase {abc} "ABC"
} -result {1}}

###############################################################################

runTest {test regex-3.2 {
  R-14943-11718: regexp -- ends options
} -constraints {
    regexp
} -body {
  regexp -- {-test} "a-testy"
} -result {1}}

###############################################################################
#
# Section 4 -- regsub: basic substitution
#
###############################################################################

runTest {test regex-4.1 {
  R-60287-61395: regsub basic substitution
} -constraints {
    regsub
} -setup {
} -body {
  regsub {[0-9]+} "abc123def" "NUM" result
  set result
} -cleanup {
  unset -nocomplain result
} -result {abcNUMdef}}

###############################################################################

runTest {test regex-4.2 {
  R-12785-54861: regsub -all replaces all occurrences
} -constraints {
    regsub
} -setup {
} -body {
  regsub -all {[0-9]+} "a1b2c3" "N" result
  set result
} -cleanup {
  unset -nocomplain result
} -result {aNbNcN}}

###############################################################################

runTest {test regex-4.3 {
  R-37530-51413: regsub -nocase
} -constraints {
    regsub
} -setup {
} -body {
  regsub -nocase {abc} "ABCdef" "xyz" result
  set result
} -cleanup {
  unset -nocomplain result
} -result {xyzdef}}

###############################################################################

runTest {test regex-4.4 {
  R-49932-20637: regsub backreference with &
} -constraints {
    regsub
} -setup {
} -body {
  regsub {[a-z]+} "hello world" {[&]} result
  set result
} -cleanup {
  unset -nocomplain result
} -result {[hello] world}}

###############################################################################

runTest {test regex-4.5 {
  R-60287-61395: regsub returns count of replacements
} -constraints {
    regsub
} -setup {
} -body {
  regsub -all {x} "xaxbxcx" "y" result
} -cleanup {
  unset -nocomplain result
} -result {4}}

###############################################################################
#
# Section 5 -- error cases
#
###############################################################################

runTest {test regex-5.1 {
  R-10050-09986: regexp wrong # args
} -constraints {
    regexp
} -setup {
} -body {
  list [catch {regexp} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test regex-5.2 {
  R-10050-09986: regexp invalid pattern
} -constraints {
    regexp
} -setup {
} -body {
  set pat "\[invalid"
  list [catch {regexp $pat "test"} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain pat
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
