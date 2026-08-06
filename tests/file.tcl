###############################################################################
#
# file.tcl --
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
# Section 1 -- file dirname
#
###############################################################################

runTest {test file-1.1 {
  R-40969-00092: file dirname returns directory portion
} -body {
  file dirname "/a/b/c"
} -result {/a/b}}

###############################################################################

runTest {test file-1.2 {
  R-40969-00092: file dirname returns directory portion
} -body {
  file dirname "a/b/c"
} -result {a/b}}

###############################################################################

runTest {test file-1.3 {
  R-40969-00092: file dirname returns directory portion
} -body {
  file dirname "/a"
} -result {/}}

###############################################################################

runTest {test file-1.4 {
  R-18250-49096: root directory returns root itself
} -constraints {not_eagle} -body {
  file dirname "/"
} -result {/}}

###############################################################################

runTest {test file-1.5 {
  R-55000-49041: no separator returns "."
} -body {
  file dirname "hello"
} -result {.}}

###############################################################################

runTest {test file-1.6 {
  R-55000-49041: no separator returns "."
} -body {
  file dirname ""
} -result {.}}

###############################################################################

runTest {test file-1.7 {
  R-40969-00092: file dirname returns directory portion
} -body {
  file dirname "/a/b/c/"
} -result {/a/b}}

###############################################################################

runTest {test file-1.8 {
  R-40969-00092: file dirname returns directory portion
} -body {
  file dirname "/a/b///"
} -result {/a}}

###############################################################################

runTest {test file-1.err.1 {
  R-40969-00092: file dirname returns directory portion
} -setup {
} -body {
  list [catch {file dirname} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 2 -- file join
#
###############################################################################

runTest {test file-2.1 {
  R-50124-31522: file join combines with forward slash
} -body {
  file join a b
} -result {a/b}}

###############################################################################

runTest {test file-2.2 {
  R-50124-31522: file join combines with forward slash
} -body {
  file join a b c
} -result {a/b/c}}

###############################################################################

runTest {test file-2.3 {
  R-50124-31522: file join combines with forward slash
} -body {
  file join /usr local bin
} -result {/usr/local/bin}}

###############################################################################

runTest {test file-2.4 {
  R-50437-19510: absolute component resets result
} -constraints {not_eagle} -body {
  file join a /b c
} -result {/b/c}}

###############################################################################

runTest {test file-2.5 {
  R-50124-31522: file join combines with forward slash
} -body {
  file join foo
} -result {foo}}

###############################################################################

runTest {test file-2.6 {
  R-50124-31522: file join combines with forward slash
} -body {
  file join "a/" b
} -result {a/b}}

###############################################################################

runTest {test file-2.7 {
  R-37984-15130: empty components skipped
} -body {
  file join a "" b
} -result {a/b}}

###############################################################################

runTest {test file-2.err.1 {
  R-50124-31522: file join combines with forward slash
} -setup {
} -body {
  list [catch {file join} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 3 -- file split
#
###############################################################################

runTest {test file-3.1 {
  R-64401-01441: file split returns list of components
} -body {
  file split "/usr/local/bin"
} -result {/ usr local bin}}

###############################################################################

runTest {test file-3.2 {
  R-64401-01441: file split returns list of components
} -body {
  file split "a/b/c"
} -result {a b c}}

###############################################################################

runTest {test file-3.3 {
  R-22232-42067: leading separator becomes first element
} -body {
  file split "/"
} -result {/}}

###############################################################################

runTest {test file-3.4 {
  R-64401-01441: file split returns list of components
} -body {
  file split "hello"
} -result {hello}}

###############################################################################

runTest {test file-3.5 {
  R-64401-01441: file split returns list of components
} -body {
  file split ""
} -result {}}

###############################################################################

runTest {test file-3.6 {
  R-64401-01441: file split returns list of components
} -body {
  file split "a/b/"
} -result {a b}}

###############################################################################

runTest {test file-3.7 {
  R-46268-59137: consecutive separators collapsed
} -body {
  file split "a//b///c"
} -result {a b c}}

###############################################################################

runTest {test file-3.err.1 {
  R-64401-01441: file split returns list of components
} -setup {
} -body {
  list [catch {file split} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 4 -- file: error cases
#
###############################################################################

runTest {test file-4.err.1 {
  R-40969-00092: file dirname returns directory portion
} -setup {
} -body {
  list [catch {file} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test file-4.err.2 {
  R-40969-00092: file dirname returns directory portion
} -setup {
} -body {
  list [catch {file nosuchsub foo} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 5 -- file exists
#
###############################################################################

runTest {test file-5.1 {
  R-51814-55378: file exists returns 1 for existing file
} -body {
  file exists tests/prologue.tcl
} -result {1}}

###############################################################################

runTest {test file-5.2 {
  R-51814-55378: file exists returns 0 for nonexistent file
} -body {
  file exists tests/nonexistent_file.tcl
} -result {0}}

###############################################################################

runTest {test file-5.3 {
  R-51814-55378: file exists returns 1 for source directory file
} -body {
  file exists src/th8.h
} -result {1}}

###############################################################################

runTest {test file-5.4 {
  R-51814-55378: file exists returns 0 for nonexistent absolute path
} -body {
  file exists /no/such/path/anywhere
} -result {0}}

###############################################################################

runTest {test file-5.5 {
  R-51814-55378: file exists returns 1 for the test file itself
} -body {
  file exists tests/file.tcl
} -result {1}}

###############################################################################

runTest {test file-5.6 {
  R-33853-30033: file exists with empty string returns 0
} -body {
  file exists ""
} -result {0}}

###############################################################################

runTest {test file-5.err.1 {
  R-51814-55378: file exists wrong # args
} -setup {
} -body {
  list [catch {file exists} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

source tests/epilogue.tcl
