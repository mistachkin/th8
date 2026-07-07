###############################################################################
#
# switch.tcl --
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
# Section 1 -- switch: Exact match
#
###############################################################################

runTest {test switch-1.1 {
  R-10596-01795: exact match first pattern
} -setup {
} -body {
  set result [switch abc {
    abc {set x "matched abc"}
    def {set x "matched def"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched abc}}

###############################################################################

runTest {test switch-1.2 {
  R-10596-01795: exact match second pattern
} -setup {
} -body {
  set result [switch def {
    abc {set x "matched abc"}
    def {set x "matched def"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched def}}

###############################################################################

runTest {test switch-1.3 {
  R-38225-33858: exact match with variable
} -setup {
} -body {
  set val "two"
  set result [switch $val {
    one {expr {1}}
    two {expr {2}}
    three {expr {3}}
  }]
  set result
} -cleanup {
  unset -nocomplain val
  unset -nocomplain result
} -result {2}}

###############################################################################
#
# Section 2 -- switch: default
#
###############################################################################

runTest {test switch-2.1 {
  R-38225-33858: default when no pattern matches
} -setup {
} -body {
  set result [switch xyz {
    abc {set x "abc"}
    def {set x "def"}
    default {set x "default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {default}}

###############################################################################

runTest {test switch-2.2 {
  R-38225-33858: default not reached when match exists
} -setup {
} -body {
  set result [switch abc {
    abc {set x "abc"}
    default {set x "default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {abc}}

###############################################################################
#
# Section 3 -- switch: -glob
#
###############################################################################

runTest {test switch-3.1 {
  R-29227-22697: -glob matching with wildcard
} -constraints {
    switch_glob
} -setup {
} -body {
  set result [switch -glob "hello world" {
    hello* {set x "matched"}
    default {set x "no match"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched}}

###############################################################################

runTest {test switch-3.2 {
  R-29227-22697: -glob matching with question mark
} -constraints {
    switch_glob
} -setup {
} -body {
  set result [switch -glob "cat" {
    c?t {set x "matched"}
    default {set x "no match"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched}}

###############################################################################

runTest {test switch-3.3 {
  R-29227-22697: -glob matching with bracket range
} -constraints {
    switch_glob
} -setup {
} -body {
  set result [switch -glob "bat" {
    "b\[aeiou\]t" {set x "matched"}
    default {set x "no match"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched}}

###############################################################################
#
# Section 4 -- switch: fall-through with -
#
###############################################################################

runTest {test switch-4.1 {
  R-39721-05226: fall-through with dash
} -setup {
} -body {
  set result [switch abc {
    abc -
    def {set x "matched abc or def"}
    ghi {set x "matched ghi"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched abc or def}}

###############################################################################

runTest {test switch-4.2 {
  R-39721-05226: fall-through chain
} -setup {
} -body {
  set result [switch bbb {
    aaa -
    bbb -
    ccc {set x "matched one of three"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched one of three}}

###############################################################################
#
# Section 5 -- switch: brace-list syntax (pattern-body pairs)
#
###############################################################################

runTest {test switch-5.1 {
  R-38225-33858: brace-list syntax
} -setup {
} -body {
  set result [switch "b" {
    a {expr {1}}
    b {expr {2}}
    c {expr {3}}
  }]
  set result
} -cleanup {
  unset -nocomplain result
} -result {2}}

###############################################################################

runTest {test switch-5.2 {
  R-38225-33858: brace-list syntax with default
} -setup {
} -body {
  set result [switch "z" {
    a {expr {1}}
    b {expr {2}}
    default {expr {0}}
  }]
  set result
} -cleanup {
  unset -nocomplain result
} -result {0}}

###############################################################################
#
# Section 6 -- switch: no match
#
###############################################################################

runTest {test switch-6.1 {
  R-22775-57830: no match and no default returns empty
} -setup {
} -body {
  set result [switch xyz abc {set x "abc"} def {set x "def"}]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {}}

###############################################################################

runTest {test switch-6.2 {
  R-60822-28178: no match with -exact flag
} -setup {
} -body {
  set result [switch -exact "xyz" abc {set x "abc"} def {set x "def"}]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {}}

###############################################################################
#
# Section 7 -- switch: error cases
#
###############################################################################

runTest {test switch-7.1 {
  R-38225-33858: switch with wrong # args
} -setup {
} -body {
  list [catch {switch} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test switch-7.2 {
  R-38225-33858: switch with odd number of pattern-body pairs
} -setup {
} -body {
  list [catch {switch abc {a}} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test switch-7.3 {
  R-38225-33858: switch with error in body
} -setup {
} -body {
  set rc [catch {switch abc {
      abc {error "body error"}
  }} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1 {body error}}}

###############################################################################
#
# Section 8 -- switch: Additional Tcl 8.4 behavior tests
#
###############################################################################

runTest {test switch-8.1 {
  R-38225-33858: default pattern matches any string
} -setup {
} -body {
  set result [switch "anything at all" {
    foo {set x "foo"}
    bar {set x "bar"}
    default {set x "caught by default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {caught by default}}

###############################################################################

runTest {test switch-8.2 {
  R-29227-22697: -glob with multiple wildcard patterns
} -constraints {
    switch_glob
} -setup {
} -body {
  set result [switch -glob "test.tcl" {
    *.txt {set x "text file"}
    *.tcl {set x "tcl file"}
    *.c   {set x "c file"}
    default {set x "unknown"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {tcl file}}

###############################################################################

runTest {test switch-8.3 {
  R-39721-05226: fall-through with dash across multiple patterns
} -setup {
} -body {
  set result [switch "wed" {
    mon -
    tue -
    wed -
    thu -
    fri {set x "weekday"}
    sat -
    sun {set x "weekend"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {weekday}}

###############################################################################

runTest {test switch-8.4 {
  R-22775-57830: no matching pattern returns empty string
} -setup {
} -body {
  set result [switch "nomatch" {
    alpha {set x "alpha"}
    beta  {set x "beta"}
    gamma {set x "gamma"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {}}

###############################################################################

runTest {test switch-8.5 {
  R-38225-33858: braced pattern list form as single argument
} -setup {
} -body {
  set result [switch "second" {
    first  {expr {1}}
    second {expr {2}}
    third  {expr {3}}
  }]
  set result
} -cleanup {
  unset -nocomplain result
} -result {2}}

###############################################################################

runTest {test switch-8.6 {
  R-38225-33858: first matching pattern wins, second match ignored
} -constraints {
    switch_glob
} -setup {
} -body {
  set result [switch -glob "abc" {
    a*  {set x "first match"}
    abc {set x "second match"}
    a?c {set x "third match"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {first match}}

###############################################################################

runTest {test switch-8.7 {
  R-38225-33858: -- ends options when string starts with dash
} -setup {
} -body {
  set result [switch -- "-glob" {
    -glob   {set x "matched -glob"}
    -exact  {set x "matched -exact"}
    default {set x "default"}
  }]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {matched -glob}}

###############################################################################

source tests/epilogue.tcl
