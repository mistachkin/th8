###############################################################################
#
# coverage6.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests targeting uncovered branches in the TH8 core: escape
# sequences (\x, \u, \U, \octal, \a..\v), multi-byte UTF-8
# handling, and backslash-space substitution.
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
# Section 1 -- Hex escape \xNN
#
###############################################################################

runTest {test coverage6-1.1 {hex escape single digit} -body {
    set x \x41
    set x
} -cleanup {
  unset -nocomplain x
} -result {A}}

###############################################################################

runTest {test coverage6-1.2 {hex escape two digits} -body {
    set x \x7A
    set x
} -cleanup {
  unset -nocomplain x
} -result {z}}

###############################################################################

runTest {test coverage6-1.3 {hex escape uppercase} -body {
    set x \x4F
    set x
} -cleanup {
  unset -nocomplain x
} -result {O}}

###############################################################################

runTest {test coverage6-1.4 {hex escape zero} -body {
    string length \x00
} -result {1}}

###############################################################################
#
# Section 2 -- Unicode escape \uNNNN
#
###############################################################################

runTest {test coverage6-2.1 {unicode escape BMP character} -body {
    set x \u00E9
    set x
} -cleanup {
  unset -nocomplain x
} -result "\u00e9"}

###############################################################################

runTest {test coverage6-2.2 {unicode escape ASCII range} -body {
    set x \u0041
    set x
} -cleanup {
  unset -nocomplain x
} -result {A}}

###############################################################################

runTest {test coverage6-2.3 {unicode escape 3-byte UTF-8} -body {
    string length \u4e16
} -result {1}}

###############################################################################
#
# Section 3 -- Wide unicode escape \UNNNNNNNN
#
###############################################################################

runTest {test coverage6-3.1 {wide unicode escape emoji} -constraints {
    escapeU
} -body {
  string length \U0001F600
} -result {1}}

###############################################################################

runTest {test coverage6-3.2 {wide unicode escape BMP} -constraints {
    escapeU
} -body {
  set x \U00000041
  set x
} -cleanup {
  unset -nocomplain x
} -result {A}}

###############################################################################

runTest {test coverage6-3.3 {wide unicode escape surrogate replaced} -constraints {
    escapeU
} -body {
  # Surrogate codepoints (D800-DFFF) should be replaced with U+FFFD
  string length \UD800
} -result {1}}

###############################################################################
#
# Section 4 -- Octal escape \NNN
#
###############################################################################

runTest {test coverage6-4.1 {octal escape basic} -body {
    set x \101
    set x
} -cleanup {
  unset -nocomplain x
} -result {A}}

###############################################################################

runTest {test coverage6-4.2 {octal escape zero} -body {
    string length \000
} -result {1}}

###############################################################################

runTest {test coverage6-4.3 {octal escape max single byte} -body {
    set x \177
    set x
} -cleanup {
  unset -nocomplain x
} -result "\x7f"}

###############################################################################
#
# Section 5 -- Named escapes
#
###############################################################################

runTest {test coverage6-5.1 {bell escape} -body {
    string length \a
} -result {1}}

###############################################################################

runTest {test coverage6-5.2 {backspace escape} -body {
    string length \b
} -result {1}}

###############################################################################

runTest {test coverage6-5.3 {form feed escape} -body {
    string length \f
} -result {1}}

###############################################################################

runTest {test coverage6-5.4 {carriage return escape} -body {
    string length \r
} -result {1}}

###############################################################################

runTest {test coverage6-5.5 {vertical tab escape} -body {
    string length \v
} -result {1}}

###############################################################################

runTest {test coverage6-5.6 {backslash-newline collapses to space} -body {
    set x "a\
b"
    set x
} -cleanup {
  unset -nocomplain x
} -result {a b}}

###############################################################################
#
# Section 6 -- Multi-byte UTF-8 string operations
#
###############################################################################

runTest {test coverage6-6.1 {string length with 2-byte UTF-8} -body {
    string length "\u00e9"
} -result {1}}

###############################################################################

runTest {test coverage6-6.2 {string length with 3-byte UTF-8} -body {
    string length "\u4e16"
} -result {1}}

###############################################################################

runTest {test coverage6-6.3 {string index on multi-byte string} -body {
    string index "\u00e9X" 1
} -result {X}}

###############################################################################

runTest {test coverage6-6.4 {string range with multi-byte} -body {
    string range "A\u00e9B" 1 1
} -result "\u00e9"}

###############################################################################
#
# Section 7 -- Binary literals in expressions
#
###############################################################################

runTest {test coverage6-7.1 {binary literal 0b101} -body {
    expr {0b101}
} -result {5}}

###############################################################################

runTest {test coverage6-7.2 {binary literal 0b0} -body {
    expr {0b0}
} -result {0}}

###############################################################################

runTest {test coverage6-7.3 {binary literal 0b11111111} -body {
    expr {0b11111111}
} -result {255}}

###############################################################################
#
# Section 8 -- Error paths and edge cases
#
###############################################################################

runTest {test coverage6-8.1 {deeply nested proc calls} -setup {
    unset -nocomplain result
} -body {
  proc _recur {n} {
    if {$n <= 0} then { return "done" }
    _recur [expr {$n - 1}]
  }
  set result [_recur 50]
  rename _recur ""
  set result
} -cleanup {
  catch {rename _recur ""}
  unset -nocomplain result
} -result {done}}

###############################################################################

runTest {test coverage6-8.2 {namespace qualified variable access} -body {
    namespace eval ::_cov6ns {
        variable x hello
    }
    set result $::_cov6ns::x
    namespace delete ::_cov6ns
    set result
} -cleanup {
  catch {namespace delete ::_cov6ns}
  unset -nocomplain result
} -result {hello}}

###############################################################################

runTest {test coverage6-8.3 {unknown command handler catches missing cmd} -constraints {th8} -body {
    proc unknown {args} {
        return "caught"
    }
    set result [_nonexistent_6_8_3]
    rename unknown ""
    set result
} -cleanup {
  catch {rename unknown ""}
  unset -nocomplain result
} -result {caught}}

###############################################################################

runTest {test coverage6-8.4 {string map with empty pattern} -body {
    string map {{} X} "hello"
} -result {hello}}

###############################################################################

runTest {test coverage6-8.5 {catch returns error code} -setup {
    unset -nocomplain msg
} -body {
  catch {error "boom"} msg
  list [catch {error "boom"}] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 boom}}

###############################################################################

runTest {test coverage6-8.6 {info exists on nonexistent var} -body {
    info exists _no_such_var_6_8_6
} -result {0}}

###############################################################################

runTest {test coverage6-8.7 {lsort with duplicates} -body {
    lsort {c a b a c}
} -result {a a b c c}}

###############################################################################

runTest {test coverage6-8.8 {lsearch not found} -body {
    lsearch {a b c} d
} -result {-1}}

###############################################################################

runTest {test coverage6-8.9 {string first not found} -body {
    string first "xyz" "hello world"
} -result {-1}}

###############################################################################

runTest {test coverage6-8.10 {string last basic} -body {
    string last "l" "hello"
} -result {3}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
