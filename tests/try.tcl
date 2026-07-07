###############################################################################
#
# try.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the [try] command (Section 12.16).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test try-1.1 {
  R-29919-42617: try evaluates body and preserves result
} -body {
  try { expr {6 * 7} }
} -result {42}}

###############################################################################

runTest {test try-1.2 {
  R-02591-65336: try without finally behaves like eval
} -setup {
} -body {
  try { set x "hello"; string length $x }
} -cleanup {
  unset -nocomplain x
} -result {5}}

###############################################################################

runTest {test try-1.3 {
  R-29919-42617: try preserves error from body
} -setup {
} -body {
  catch { try { error "body error" } } msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {body error}}

###############################################################################

runTest {test try-2.1 {
  R-51881-64256: finally runs after successful body
} -setup {
} -body {
  set log {}
  try {
    lappend log "body"
  } finally {
    lappend log "finally"
  }
  set log
} -cleanup {
  unset -nocomplain log
} -result {body finally}}

###############################################################################

runTest {test try-2.2 {
  R-51881-64256: finally runs after failed body
} -setup {
} -body {
  set log {}
  catch {
    try {
      lappend log "body"
      error "boom"
    } finally {
      lappend log "finally"
    }
  }
  set log
} -cleanup {
  unset -nocomplain log
} -result {body finally}}

###############################################################################

runTest {test try-2.3 {
  R-29352-51924: successful finally returns body result
} -setup {
} -body {
  try {
    set x "body result"
  } finally {
    set y "finally ran"
  }
} -cleanup {
  unset -nocomplain x y
} -result {body result}}

###############################################################################

runTest {test try-2.4 {
  R-29352-51924: successful finally preserves body return code
} -setup {
} -body {
  catch {
    try {
      error "body error"
    } finally {
      set y "cleanup ok"
    }
  } msg
  set msg
} -cleanup {
  unset -nocomplain msg y
} -result {body error}}

###############################################################################

runTest {test try-2.5 {
  R-60969-27299: failed finally overrides body result
} -setup {
} -body {
  catch {
    try {
      set x "body ok"
    } finally {
      error "finally error"
    }
  } msg
  set msg
} -cleanup {
  unset -nocomplain msg x
} -result {finally error}}

###############################################################################

runTest {test try-2.6 {
  R-60969-27299: failed finally overrides body error
} -setup {
} -body {
  catch {
    try {
      error "body error"
    } finally {
      error "finally error"
    }
  } msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {finally error}}

###############################################################################

runTest {test try-3.1 {
  R-29919-42617: try with break in body
} -setup {
} -body {
  set log {}
  for {set i 0} {$i < 5} {incr i} {
    try {
      if {$i == 2} then { break }
      lappend log $i
    } finally {
      lappend log "f$i"
    }
  }
  set log
} -cleanup {
  unset -nocomplain log i
} -result {0 f0 1 f1 f2}}

###############################################################################

runTest {test try-3.2 {
  R-29919-42617: try with continue in body
} -setup {
} -body {
  set log {}
  for {set i 0} {$i < 4} {incr i} {
    try {
      if {$i == 1} then { continue }
      lappend log $i
    } finally {
      lappend log "f$i"
    }
  }
  set log
} -cleanup {
  unset -nocomplain log i
} -result {0 f0 f1 2 f2 3 f3}}

###############################################################################

runTest {test try-3.3 {
  R-29919-42617: try with return in body
} -body {
  proc tryReturnTest {} {
    try {
      return "early"
    } finally {
      set ::_tryFinally "ran"
    }
    return "never"
  }
  list [tryReturnTest] $::_tryFinally
} -cleanup {
  catch {rename tryReturnTest ""}
  unset -nocomplain ::_tryFinally
} -result {early ran}}

###############################################################################

runTest {test try-4.1 {
  R-42434-29835: wrong number of args
} -setup {
} -body {
  catch {try} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {wrong # args:*}}

###############################################################################

runTest {test try-4.2 {
  R-42434-29835: bad keyword instead of finally
} -setup {
} -body {
  catch {try { set x 1 } badword { set y 2 }} msg
  set msg
} -cleanup {
  unset -nocomplain msg x y
} -match glob -result {expected "finally"*}}

###############################################################################

runTest {test try-5.1 {
  R-12502-54202: finally script can be canceled during execution
} -constraints {
    th8 interp_cancel
} -setup {
} -body {
  # Cancel issued DURING finally takes effect.
  catch {
    try {
      set x "body ok"
    } finally {
      interp cancel "veto from finally"
    }
  } msg
  expr {[string match "*veto*" $msg]}
} -cleanup {
  unset -nocomplain msg x
} -result {1}}

###############################################################################

runTest {test try-5.2 {
  R-32581-44432: pre-existing cancel does not prevent finally
} -constraints {
    th8 interp_cancel
} -setup {
} -body {
  # The body cancels, but finally still runs.
  set log {}
  catch {
    try {
      lappend log "body"
      interp cancel "body cancel"
    } finally {
      lappend log "finally"
    }
  }
  set log
} -cleanup {
  unset -nocomplain log
} -result {body finally}}

###############################################################################
#
# Section 6 -- R-marker coverage: exit suppresses finally, fresh alloc budget
#
###############################################################################

runTest {test try-6.1 {
  R-05041-46440: finally SHALL NOT be evaluated if exit flag is set
} -constraints {
    th8 test_only_exec
} -setup {
} -body {
  set rc [catch {
    test_only_exec \
        tests/helpers/exit_try_finally.tcl
  } msg]
  #
  # "finally_ran" must NOT appear -- exit suppresses finally.
  #
  list [expr {[string match "*before_exit*" $msg]}] \
      [expr {[string match "*finally_ran*" $msg]}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 0}}

###############################################################################

runTest {test try-6.2 {
  R-60341-22718: finally receives fresh memory allocation budget
} -constraints {
    th8 sandbox
} -setup {
} -body {
  #
  # The body exhausts most of the allocation budget by building
  # a large string.  The finally block then allocates its own
  # string.  If finally did NOT receive a fresh budget, the
  # allocation in finally would fail.
  #
  set r [::th8testlib::sandbox {
    try {
      string length [string repeat X 500000]
    } finally {
      set _cleanup [string repeat Y 1000]
    }
  }]
  list [sandboxRc $r] [expr {[sandboxResult $r] >= 0}]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

source tests/epilogue.tcl
