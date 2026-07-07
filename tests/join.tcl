###############################################################################
#
# join.tcl --
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
# Section 9 -- join: Join list elements into a string
#
###############################################################################

runTest {test join-9.1 {
  R-09813-27218: default separator is single space
} -body {
  join {a b c d}
} -result {a b c d}}

###############################################################################

runTest {test join-9.2 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {a b c d} :
} -result {a:b:c:d}}

###############################################################################

runTest {test join-9.3 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {one two three} ,
} -result {one,two,three}}

###############################################################################

runTest {test join-9.4 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {a b c} ", "
} -result {a, b, c}}

###############################################################################

runTest {test join-9.5 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {} :
} -result {}}

###############################################################################

runTest {test join-9.6 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {hello} :
} -result {hello}}

###############################################################################

runTest {test join-9.7 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {a b c} ""
} -result {abc}}

###############################################################################

runTest {test join-9.8 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {hello world} -
} -result {hello-world}}

###############################################################################

runTest {test join-9.9 {
  R-07716-29030: join concatenates elements with separator
} -setup {
} -body {
  set original {a b c d e}
  set result [join [split [join $original :] :]]
  expr {$result eq $original}
} -cleanup {
  unset -nocomplain original
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test join-9.10 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {a b c} \t
} -result "a\tb\tc"}

###############################################################################

runTest {test join-9.11 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join {line1 line2 line3} \n
} -result "line1\nline2\nline3"}

###############################################################################

runTest {test join-9.12 {
  R-07716-29030: join concatenates elements with separator
} -body {
  join [list "hello world" foo bar] :
} -result {hello world:foo:bar}}

###############################################################################

runTest {test join-9.err.1 {
  R-07716-29030: join concatenates elements with separator
} -setup {
} -body {
  list [catch {join} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
