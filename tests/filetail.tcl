###############################################################################
#
# filetail.tcl --
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
# Section 1 -- file tail: basic path component extraction
#
###############################################################################

runTest {test filetail-1.1 {
  R-59685-03456: file tail multi-component path
} -body {
  file tail /a/b/c.txt
} -result {c.txt}}

###############################################################################

runTest {test filetail-1.2 {
  R-59685-03456: file tail two components
} -body {
  file tail /usr/bin
} -result {bin}}

###############################################################################

runTest {test filetail-1.3 {
  R-59685-03456: file tail root returns empty
} -body {
  file tail /
} -result {}}

###############################################################################

runTest {test filetail-1.4 {
  R-59685-03456: file tail bare name
} -body {
  file tail hello.txt
} -result {hello.txt}}

###############################################################################

runTest {test filetail-1.5 {
  R-59685-03456: file tail empty string
} -body {
  file tail ""
} -result {}}

###############################################################################

runTest {test filetail-1.6 {
  R-59685-03456: file tail relative path
} -body {
  file tail a/b/c
} -result {c}}

###############################################################################

runTest {test filetail-1.7 {
  R-59685-03456: file tail trailing separator
} -body {
  file tail /a/b/c/
} -result {c}}

###############################################################################

runTest {test filetail-1.8 {
  R-59685-03456: file tail and dirname complementary
} -body {
  set path "/usr/local/bin/th8sh"
  list [file dirname $path] [file tail $path]
} -cleanup {
  unset -nocomplain path
} -result {/usr/local/bin th8sh}}

###############################################################################

runTest {test filetail-1.9 {
  R-59685-03456: file tail wrong # args
} -setup {
} -body {
  list [catch {file tail} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
