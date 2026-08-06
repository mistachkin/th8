###############################################################################
#
# datamodel.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the data model (Section 6).
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
# Section 1 -- everything is a string
#
###############################################################################

runTest {test datamodel-1.1 {
  R-52995-63998: every value is a string
} -setup {
} -body {
  set x 42
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {2}}

###############################################################################

runTest {test datamodel-1.2 {
  R-16342-40509: no distinct types at language level
} -setup {
} -body {
  set x 42
  append x " hello"
  set x
} -cleanup {
  unset -nocomplain x
} -result {42 hello}}

###############################################################################
#
# Section 2 -- integer interpretation
#
###############################################################################

runTest {test datamodel-2.1 {
  R-33487-59232: decimal integer interpretation
} -body {
  expr {123 + 0}
} -result {123}}

###############################################################################

runTest {test datamodel-2.2 {
  R-33487-59232: negative integer
} -body {
  expr {-42 + 0}
} -result {-42}}

###############################################################################

runTest {test datamodel-2.3 {
  R-33487-59232: integer with leading whitespace
} -body {
  expr {  99 + 0}
} -result {99}}

###############################################################################

runTest {test datamodel-2.4 {
  R-08901-48972: hexadecimal prefix 0x
} -body {
  format %d [expr {0xFF}]
} -result {255}}

###############################################################################

runTest {test datamodel-2.5 {
  R-08901-48972: hexadecimal prefix 0X
} -body {
  format %d [expr {0X1A}]
} -result {26}}

###############################################################################

runTest {test datamodel-2.6 {
  R-18737-56517: octal prefix 0o
} -body {
  format %d [expr {0o77}]
} -result {63}}

###############################################################################

runTest {test datamodel-2.7 {
  R-44656-63369: binary prefix 0b
} -body {
  format %d [expr {0b1010}]
} -result {10}}

###############################################################################
#
# Section 3 -- floating-point interpretation
#
###############################################################################

runTest {test datamodel-3.1 {
  R-03620-44716: decimal point = floating-point
} -match regexp -body {
    expr {1.5 + 0.0}
} -result {^1\.5(0*)$}}

###############################################################################

runTest {test datamodel-3.2 {
  R-03620-44716: exponent = floating-point
} -match regexp -body {
    expr {1e2 + 0.0}
} -result {^100(?:\.0(0*))?$}}

###############################################################################
#
# Section 4 -- boolean interpretation
#
###############################################################################

runTest {test datamodel-4.1 {
  R-00488-11343: boolean false values
} -body {
  set results [list]
  foreach val {0 false no off} {
    if {$val} then {
      lappend results true
    } else {
      lappend results false
    }
  }
  set results
} -cleanup {
  unset -nocomplain results val
} -result {false false false false}}

###############################################################################

runTest {test datamodel-4.2 {
  R-02588-48107: boolean true values
} -body {
  set results [list]
  foreach val {1 true yes on} {
    if {$val} then {
      lappend results true
    } else {
      lappend results false
    }
  }
  set results
} -cleanup {
  unset -nocomplain results val
} -result {true true true true}}

###############################################################################

runTest {test datamodel-4.3 {
  R-00488-11343: boolean false case-insensitive
} -body {
  set results [list]
  foreach val {FALSE False No OFF} {
    if {$val} then {
      lappend results true
    } else {
      lappend results false
    }
  }
  set results
} -cleanup {
  unset -nocomplain results val
} -result {false false false false}}

###############################################################################

runTest {test datamodel-4.4 {
  R-02588-48107: boolean true case-insensitive
} -body {
  set results [list]
  foreach val {TRUE True Yes ON} {
    if {$val} then {
      lappend results true
    } else {
      lappend results false
    }
  }
  set results
} -cleanup {
  unset -nocomplain results val
} -result {true true true true}}

###############################################################################

source tests/epilogue.tcl
