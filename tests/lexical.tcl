###############################################################################
#
# lexical.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for lexical structure (Section 5).
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
# Section 1 -- script structure
#
###############################################################################

runTest {test lexical-1.1 {
  R-20498-60370: script = commands separated by newlines
} -body {
  set a 1
  set b 2
  expr {$a + $b}
} -cleanup {
  unset -nocomplain a b
} -result {3}}

###############################################################################

runTest {test lexical-1.2 {
  R-20498-60370: script = commands separated by semicolons
} -body {
  set a 10; set b 20; expr {$a + $b}
} -cleanup {
  unset -nocomplain a b
} -result {30}}

###############################################################################

runTest {test lexical-1.3 {
  R-09662-29945: command = words separated by whitespace
} -setup {
} -body {
  set    x    hello
} -cleanup {
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test lexical-1.4 {
  R-29611-36088: first word is command name
} -body {
  string length "abc"
} -result {3}}

###############################################################################
#
# Section 2 -- substitution
#
###############################################################################

runTest {test lexical-2.1 {
  R-58077-09618: dollar triggers variable substitution
} -setup {
} -body {
  set myvar "world"
  set result "hello $myvar"
} -cleanup {
  unset -nocomplain result myvar
} -result {hello world}}

###############################################################################

runTest {test lexical-2.2 {
  R-01453-47388: bracket triggers command substitution
} -setup {
} -body {
  set result "len=[string length abc]"
} -cleanup {
  unset -nocomplain result
} -result {len=3}}

###############################################################################

runTest {test lexical-2.3 {
  R-60231-25699: backslash triggers backslash substitution
} -setup {
} -body {
  set result "tab:\there"
  string index $result 4
} -cleanup {
  unset -nocomplain result
} -result {	}}

###############################################################################

runTest {test lexical-2.4 {
  R-09092-04681: substitution performed exactly once
} -setup {
} -body {
  set x {$y}
  set y "inner"
  set x
} -cleanup {
  unset -nocomplain x
  unset -nocomplain y
} -result {$y}}

###############################################################################

runTest {test lexical-2.5 {
  R-25821-20104: double-quoted words undergo substitution
} -setup {
} -body {
  set v 42
  set result "value is $v"
} -cleanup {
  unset -nocomplain result v
} -result {value is 42}}

###############################################################################

runTest {test lexical-2.6 {
  R-20186-08766: braced words undergo no substitution
} -setup {
} -body {
  set v 42
  set result {value is $v}
} -cleanup {
  unset -nocomplain result v
} -result {value is $v}}

###############################################################################

runTest {test lexical-2.7 {
  R-33512-08832: braces nest
} -setup {
} -body {
  set result {outer {inner {deep}} end}
} -cleanup {
  unset -nocomplain result
} -result {outer {inner {deep}} end}}

###############################################################################

runTest {test lexical-2.8 {
  R-29551-28259: backslash + unlisted char = literal char
} -setup {
} -body {
  set result \q
} -cleanup {
  unset -nocomplain result
} -result {q}}

###############################################################################
#
# Section 3 -- comments
#
###############################################################################

runTest {test lexical-3.1 {
  R-61045-20811: hash at command position begins comment
} -setup {
} -body {
  set x 1
  # this is a comment
  set x
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test lexical-3.2 {
  R-23450-38612: hash in other positions has no special meaning
} -setup {
} -body {
  set x "hello#world"
} -cleanup {
  unset -nocomplain x
} -result {hello#world}}

###############################################################################

runTest {test lexical-3.3 {
  R-23450-38612: hash as argument is literal
} -body {
  string length "#"
} -result {1}}

###############################################################################

source tests/epilogue.tcl
