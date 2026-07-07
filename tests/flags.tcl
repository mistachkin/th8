###############################################################################
#
# flags.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the [flags] command (Harpy-compatible attribute flags).
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
# Section 1 -- flags have: basic
#
###############################################################################

runTest {test flags-1.1 {
  R-17475-11102: flags have returns 1 when flag is present
} -constraints {
    th8 flags
} -body {
  flags have abc a
} -result {1}}

###############################################################################

runTest {test flags-1.2 {
  R-17475-11102: flags have returns 0 when flag is absent
} -constraints {
    th8 flags
} -body {
  flags have abc d
} -result {0}}

###############################################################################

runTest {test flags-1.3 {
  R-17475-11102: flags have -all returns 1 when all flags are present
} -constraints {
    th8 flags
} -body {
  flags have -all abc abc
} -result {1}}

###############################################################################

runTest {test flags-1.4 {
  R-17475-11102: flags have -all returns 0 when any flag is missing
} -constraints {
    th8 flags
} -body {
  flags have -all abc abcd
} -result {0}}

###############################################################################

runTest {test flags-1.5 {
  R-17475-11102: flags have with empty haveFlags returns 1
} -constraints {
    th8 flags
} -body {
  flags have abc {}
} -result {1}}

###############################################################################
#
# Section 2 -- flags change: operators
#
###############################################################################

runTest {test flags-2.1 {
  R-19403-45740: flags change +X adds a flag
} -constraints {
    th8 flags
} -body {
  flags change -sort abc +x
} -result {abcx}}

###############################################################################

runTest {test flags-2.2 {
  R-19403-45740: flags change -X removes a flag
} -constraints {
    th8 flags
} -body {
  flags change abc -b
} -result {ac}}

###############################################################################

runTest {test flags-2.3 {
  R-19403-45740: flags change =X clears and sets
} -constraints {
    th8 flags
} -body {
  flags change abcdef =xyz
} -result {xyz}}

###############################################################################

runTest {test flags-2.4 {
  R-19403-45740: flags change with mixed operators
} -constraints {
    th8 flags
} -body {
  flags change -sort abc +xy-b
} -result {acxy}}

###############################################################################
#
# Section 3 -- flags: complex (keyed) format
#
###############################################################################

runTest {test flags-3.1 {
  R-47520-46390: flags have -complex with keyed flags
} -constraints {
    th8 flags
} -body {
  list [flags have -complex -key 0x1 {{1:abc}} a] \
      [flags have -complex -key 0x1 {{1:abc}} d]
} -result {1 0}}

###############################################################################

runTest {test flags-3.2 {
  R-19403-45740: flags change -complex with keyed flags
} -constraints {
    th8 flags
} -body {
  flags change -complex -sort -key 0xFF {{FF:abc}} +xyz
} -result {{FF:abcxyz}}}

###############################################################################

runTest {test flags-3.3 {
  R-17475-11102: flags have default key returns 0 for keyed-only flags
} -constraints {
    th8 flags
} -body {
  flags have -complex {{1:abc}} a
} -result {0}}

###############################################################################

runTest {test flags-3.4 {
  R-47520-46390: flags have -complex with mixed default and keyed flags
} -constraints {
    th8 flags
} -setup {
} -body {
  set f "simple{26f17c3a1a544324:keyed}"
  list [flags have -complex $f s] \
      [flags have -complex $f k] \
      [flags have -complex -key 0x26f17c3a1a544324 $f k]
} -cleanup {
  unset -nocomplain f
} -result {1 0 1}}

###############################################################################
#
# Section 4 -- flags: wildcards
#
###############################################################################

runTest {test flags-4.1 {
  R-41140-31025: flags change with wildcard star adds all alphanumeric
} -constraints {
    th8 flags
} -body {
  string length [flags change {} +*]
} -result {62}}

###############################################################################

runTest {test flags-4.2 {
  R-41140-31025: flags change with wildcard hash adds all digits
} -constraints {
    th8 flags
} -body {
  flags change {} +#
} -result {0123456789}}

###############################################################################

runTest {test flags-4.3 {
  R-41140-31025: flags change with wildcard removes via star
} -constraints {
    th8 flags
} -body {
  flags change abcxyz -*
} -result {}}

###############################################################################

runTest {test flags-4.4 {
  R-41140-31025: flags have with wildcard star checks all alphanumeric
} -constraints {
    th8 flags
} -setup {
} -body {
  set all [flags change {} +*]
  list [flags have -all $all *] \
      [flags have -all abc *]
} -cleanup {
  unset -nocomplain all
} -result {1 0}}

###############################################################################
#
# Section 5 -- flags: error cases
#
###############################################################################

runTest {test flags-5.1 {
  R-47520-46390: flags have rejects plus sign without -complex
} -constraints {
    th8 flags
} -body {
  catch {flags have ab+cd a} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {flags: invalid flag character}}

###############################################################################

runTest {test flags-5.2 {
  R-47520-46390: flags have -complex rejects empty key name
} -constraints {
    th8 flags
} -setup {
} -body {
  set input [format "%c:abc%c" 123 125]
  set rc [catch [list flags have -complex $input a] msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain input
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1 {flags: ':' without key name}}}

###############################################################################

runTest {test flags-5.3 {
  R-47520-46390: flags have -complex rejects invalid key character
} -constraints {
    th8 flags
} -body {
  set input [format "%co:abc%c" 123 125]
  set rc [catch [list flags have -complex $input a] msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain input msg rc
} -result {1 {flags: invalid key character}}}

###############################################################################
#
# Section 6 -- flags: option combinations
#
###############################################################################

runTest {test flags-6.1 {
  R-47520-46390: flags have -strict rejects invalid flag characters
} -constraints {
    th8 flags
} -body {
  flags have -strict abc {a+b}
} -result {0}}

###############################################################################

runTest {test flags-6.2 {
  R-19403-45740: flags change -sort produces sorted output
} -constraints {
    th8 flags
} -body {
  flags change -sort zyx +abc
} -result {abcxyz}}

###############################################################################

runTest {test flags-6.3 {
  R-47520-46390: flags have -complex with legacy 16-digit hex key
} -constraints {
    th8 flags
} -body {
  flags have -complex -key 0x9559f6017247e3e2 \
      {{9559f6017247e3e2abc}} a
} -result {1}}

###############################################################################

source tests/epilogue.tcl
