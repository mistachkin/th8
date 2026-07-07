###############################################################################
#
# fuzzing.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Regression tests for crash inputs discovered by fuzz testing
# (AFL++, libFuzzer).  Each test feeds a known-bad binary input
# into the corresponding TH8 subsystem via th8testlib::fuzz and
# verifies that it does not crash or corrupt memory.
#
# Crash data files live in tests/fuzzing/<subType>/ as .bin files.
# The test automatically discovers all .bin files in each subtype
# directory via th8testlib::glob.
#
# To add new crash inputs:
#   1. Copy the crash file to tests/fuzzing/<subType>/fuzz-data-NNN.bin
#   2. Re-run this test file.
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
# Iterate over each fuzz subtype directory and create tests
# from all .bin files found.
#
###############################################################################

apply [list [list] {
  if {![testConstraint glob]} then {return}
  if {![testConstraint fuzz]} then {return}

  foreach subType [list list eval expr harpy snk format] {
    set directory [file join tests fuzzing $subType]
    if {![file exists $directory]} then {continue}

    set fileNames [lsort [th8testlib::glob [file join $directory *.bin]]]
    set index 0

    foreach fileName $fileNames {
      incr index

      runTest [list test [appendArgs fuzz- $subType - $index .0] \
          [appendArgs \
              " crash regression: " $subType " " [file tail $fileName]] \
          -constraints [list th8 fuzz] -body [list th8testlib::fuzz \
          $subType -file $fileName] -result {}]
    }
  }
}]

###############################################################################

source tests/epilogue.tcl
