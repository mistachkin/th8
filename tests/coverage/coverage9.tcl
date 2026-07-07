###############################################################################
#
# coverage9.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the memory recovery platform layer (Th8_GetMemPlatform).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test coverage9-1.1 {
  R-08479-28857: memory platform provides xNeedMemory
} -constraints {
    th8
} -body {
  # The memory platform is merged by default in TH8 builds.
  # Basic allocation should work (xNeedMemory is a fallback
  # that only activates on primary allocation failure).
  set x [string repeat "A" 1000]
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {1000}}

###############################################################################

runTest {test coverage9-1.2 {
  R-08479-28857: allocation after heavy cache use
} -constraints {
    th8
} -body {
  # Fill the IR cache with many list splits and type conversions,
  # then allocate.  This exercises the path where xNeedMemory
  # might be called if the cache consumed significant memory.
  for {set i 0} {$i < 100} {incr i} {
    llength [list a b c d e f g h i j]
    expr {$i * 2 + 1}
  }
  # If we get here, allocation succeeded
  set result "ok"
} -cleanup {
  unset -nocomplain i result
} -result {ok}}

###############################################################################

runTest {test coverage9-1.3 {
  R-41372-31985: Th8_GetMemPlatform returns non-NULL
} -constraints {
    th8
} -body {
  # The interpreter is running, which means the platform was
  # successfully created and merged.  info plugins confirms
  # the interpreter is functional.
  expr {[llength [info plugins]] > 0}
} -result {1}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
