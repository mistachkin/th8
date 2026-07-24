###############################################################################
#
# coroutine.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for coroutine and yield commands.  All tests are designed
# to pass in both Tcl 8.6+ and TH8.
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
# Section 1 -- Basic coroutine creation and yield
#
###############################################################################

runTest {test coroutine-1.1 {
  coroutine creates a command that resumes execution
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    yield "hello"
    return "done"
  }
  coroutine _coro _coro_body
  set result [_coro]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {done}}

###############################################################################

runTest {test coroutine-1.2 {
  initial yield value is returned from coroutine command
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    yield "initial"
    return "final"
  }
  set result [coroutine _coro _coro_body]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {initial}}

###############################################################################

runTest {test coroutine-1.3 {
  yield with no value returns empty string
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    yield
    return "done"
  }
  set result [coroutine _coro _coro_body]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {}}

###############################################################################
#
# Section 2 -- Multiple yields in sequence
#
###############################################################################

runTest {test coroutine-2.1 {
  three yields in sequence produce correct values
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    yield "first"
    yield "second"
    return "third"
  }
  coroutine _coro _coro_body
  set r1 [_coro]
  set r2 [_coro]
  list $r1 $r2
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain r1 r2 r3
} -result {second third}}

###############################################################################

runTest {test coroutine-2.2 {
  generator pattern: yield inside while loop
} -constraints {
    coroutine
} -setup {
} -body {
  proc _counter {} {
    set i 0
    while {1} {
      yield $i
      incr i
    }
  }
  coroutine _coro _counter
  set r1 [_coro]
  set r2 [_coro]
  set r3 [_coro]
  list $r1 $r2 $r3
} -cleanup {
  catch {rename _counter ""}
  catch {rename _coro ""}
  unset -nocomplain r1 r2 r3
} -result {1 2 3}}

###############################################################################

runTest {test coroutine-2.3 {
  generator with foreach loop
} -constraints {
    coroutine
} -setup {
} -body {
  proc _letters {} {
    foreach c {a b c d e} {
      yield $c
    }
    return "done"
  }
  coroutine _coro _letters
  set results [list]
  lappend results [_coro]
  lappend results [_coro]
  lappend results [_coro]
  lappend results [_coro]
  lappend results [_coro]
  set results
} -cleanup {
  catch {rename _letters ""}
  catch {rename _coro ""}
  unset -nocomplain results
} -result {b c d e done}}

###############################################################################
#
# Section 3 -- Resume value passing: set x [yield val]
#
###############################################################################

runTest {test coroutine-3.1 {
  resume value is returned from yield expression
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    set x [yield "ready"]
    return "got: $x"
  }
  coroutine _coro _coro_body
  set result [_coro "hello"]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {got: hello}}

###############################################################################

runTest {test coroutine-3.2 {
  resume value empty string when no argument
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    set x [yield "ready"]
    return "got: \[$x\]"
  }
  coroutine _coro _coro_body
  set result [_coro]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {got: []}}

###############################################################################

runTest {test coroutine-3.3 {
  multiple yield-resume exchanges
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    set a [yield "first"]
    set b [yield "second: $a"]
    return "third: $b"
  }
  coroutine _coro _coro_body
  set r1 [_coro "alpha"]
  set r2 [_coro "beta"]
  list $r1 $r2
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain r1 r2 r3
} -result {{second: alpha} {third: beta}}}

###############################################################################

runTest {test coroutine-3.4 {
  counter with resume value as increment
} -constraints {
    coroutine
} -setup {
} -body {
  proc _counter {start} {
    set i $start
    while {1} {
      set delta [yield $i]
      if {$delta eq ""} then { set delta 1 }
      incr i $delta
    }
  }
  coroutine _coro _counter 10
  set r1 [_coro]
  set r2 [_coro]
  set r3 [_coro 5]
  set r4 [_coro]
  list $r1 $r2 $r3 $r4
} -cleanup {
  catch {rename _counter ""}
  catch {rename _coro ""}
  unset -nocomplain r1 r2 r3 r4
} -result {11 12 17 18}}

###############################################################################
#
# Section 4 -- Multiple concurrent coroutines
#
###############################################################################

runTest {test coroutine-4.1 {
  two coroutines interleave correctly
} -constraints {
    coroutine
} -setup {
} -body {
  proc _letters {} {
    foreach c {a b c} { yield $c }
    return "x"
  }
  proc _numbers {} {
    foreach n {1 2 3} { yield $n }
    return "0"
  }
  coroutine _L _letters
  coroutine _N _numbers
  set results [list]
  lappend results [_L] [_N] [_L] [_N] [_L] [_N]
  set results
} -cleanup {
  catch {rename _letters ""}
  catch {rename _numbers ""}
  catch {rename _L ""}
  catch {rename _N ""}
  unset -nocomplain results
} -result {b 2 c 3 x 0}}

###############################################################################
#
# Section 5 -- Coroutine completion and error handling
#
###############################################################################

runTest {test coroutine-5.1 {
  calling completed coroutine produces error
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    return "done"
  }
  coroutine _coro _coro_body
  catch {_coro} result
  set result
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -match glob -result {*invalid*}}

###############################################################################

runTest {test coroutine-5.2 {
  yield outside coroutine produces error
} -constraints {
    coroutine
} -setup {
} -body {
  set rc [catch {yield "oops"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc msg
} -match glob -result {1 *}}

###############################################################################

runTest {test coroutine-5.3 {
  coroutine body error propagates to creation point
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    error "bad stuff"
  }
  set rc [catch {coroutine _coro _coro_body} msg]
  list $rc $msg
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain rc msg
} -result {1 {bad stuff}}}

###############################################################################
#
# Section 6 -- Coroutine with command arguments
#
###############################################################################

runTest {test coroutine-6.1 {
  coroutine passes arguments to body command
} -constraints {
    coroutine
} -setup {
} -body {
  proc _adder {a b} {
    yield [expr {$a + $b}]
    return "done"
  }
  set result [coroutine _coro _adder 3 4]
} -cleanup {
  catch {rename _adder ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {7}}

###############################################################################
#
# Section 7 -- Yield inside nested proc calls
#
###############################################################################

runTest {test coroutine-7.1 {
  yield from a proc called inside a coroutine body
} -constraints {
    coroutine
} -setup {
} -body {
  proc _inner {} {
    yield "from inner"
  }
  proc _outer {} {
    _inner
    return "outer done"
  }
  coroutine _coro _outer
  set r1 [_coro]
  list $r1
} -cleanup {
  catch {rename _inner ""}
  catch {rename _outer ""}
  catch {rename _coro ""}
  unset -nocomplain r1 r2
} -result {{outer done}}}

###############################################################################

runTest {test coroutine-7.2 {
  yield value from nested call with resume value
} -constraints {
    coroutine
} -setup {
} -body {
  proc _yieldtwice {} {
    set a [yield "y1"]
    set b [yield "y2: $a"]
    return "$a $b"
  }
  proc _wrapper {} {
    set result [_yieldtwice]
    return "wrapper: $result"
  }
  coroutine _coro _wrapper
  set r1 [_coro "A"]
  set r2 [_coro "B"]
  list $r1 $r2
} -cleanup {
  catch {rename _yieldtwice ""}
  catch {rename _wrapper ""}
  catch {rename _coro ""}
  unset -nocomplain result r1 r2
} -result {{y2: A} {wrapper: A B}}}

###############################################################################
#
# Section 8 -- R-marker coverage: coroutine creation and command lifecycle
#
###############################################################################

runTest {test coroutine-8.1 {
  R-02272-62636: coroutine creates a new coroutine that evaluates cmdPrefix
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    yield "evaluated"
    return "done"
  }
  set result [coroutine _coro _coro_body]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {evaluated}}

###############################################################################

runTest {test coroutine-8.2 {
  R-00874-30590: coroutine name becomes a command that resumes it
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    yield "paused"
    return "resumed"
  }
  set r1 [coroutine _coro _coro_body]
  #
  # _coro is now a command; invoking it resumes the coroutine.
  #
  set r2 [_coro]
  list $r1 $r2
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain r1 r2
} -result {paused resumed}}

###############################################################################

runTest {test coroutine-8.3 {
  R-48842-57962: the coroutine command is auto-deleted from its namespace
                 after the body returns
} -constraints {
    coroutine
} -setup {
} -body {
  coroutine _coro apply {{} { yield "first"; return "done" }}
  _coro
  set exists [llength [info commands _coro]]
  set exists
} -cleanup {
  catch {rename _coro ""}
  unset -nocomplain exists
} -result {0}}

###############################################################################
#
# Section 9 -- R-marker coverage: yield semantics
#
###############################################################################

runTest {test coroutine-9.1 {
  R-23592-30941: yield suspends coroutine and returns value to caller
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    yield "suspended"
    return "completed"
  }
  #
  # The coroutine command returns the value passed to yield.
  #
  set result [coroutine _coro _coro_body]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {suspended}}

###############################################################################

runTest {test coroutine-9.2 {
  R-50081-23615: yield is an error outside a coroutine body
} -constraints {
    coroutine
} -setup {
} -body {
  set rc [catch {yield "outside"} msg]
  list $rc [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test coroutine-9.3 {
  R-33215-21255: when resumed, yield returns the value passed by resume
} -constraints {
    coroutine
} -setup {
} -body {
  proc _coro_body {} {
    set v [yield "ready"]
    return "received: $v"
  }
  coroutine _coro _coro_body
  set result [_coro "payload"]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain result
} -result {received: payload}}

###############################################################################

runTest {test coroutine-10.1 {
  R-34122-15052: the coroutine command raises a script error if a
  command with the given name already exists
} -constraints {
    coroutine
} -setup {
  proc _coro_dummy {} { return "ok" }
  proc _coro_body  {} { yield "ready"; return "done" }
} -body {
  list [catch {coroutine _coro_dummy _coro_body} msg] \
       [expr {[string length $msg] > 0}]
} -cleanup {
  catch {rename _coro_dummy ""}
  catch {rename _coro_body  ""}
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 11 -- R-18099-47953: nested coroutines (one coroutine body
# creates and resumes another).  These pin the pYieldingCoro save/
# restore fix (Bug 69): resuming an inner coroutine must not terminate
# the enclosing one, which must still be able to yield afterward.
#
###############################################################################

runTest {test coroutine-11.1 {
  R-18099-47953: outer coroutine (no yield) creates, resumes, and
  drives an inner coroutine to completion; its result is the nested
  computation
} -constraints {
    coroutine
} -body {
  coroutine _outer apply {{} {
    set v [coroutine _inner apply {{} { yield fromInner; return innerDone }}]
    set r [_inner]
    return "$v/$r"
  }}
} -cleanup {
  catch {rename _outer ""}
  catch {rename _inner ""}
} -result {fromInner/innerDone}}

###############################################################################

runTest {test coroutine-11.2 {
  R-18099-47953: outer coroutine yields after resuming an inner
  coroutine that itself yields (inner still suspended)
} -constraints {
    coroutine
} -body {
  set first [coroutine _outer apply {{} {
    set v [coroutine _inner apply {{} { yield i1; yield i2; return iDone }}]
    set r1 [_inner]
    yield "mid:$v/$r1"
    set r2 [_inner]
    return "done:$r2"
  }}]
  set second [_outer]
  list $first $second
} -cleanup {
  catch {rename _outer ""}
  catch {rename _inner ""}
  unset -nocomplain first second
} -result {mid:i1/i2 done:iDone}}

###############################################################################

runTest {test coroutine-11.3 {
  R-18099-47953: outer coroutine yields AFTER the inner coroutine has
  COMPLETED -- the enclosing coroutine's yield prompt must be restored
  when the inner one is deleted (the load-bearing Bug 69 case)
} -constraints {
    coroutine
} -body {
  set a [coroutine _outer apply {{} {
    set v [coroutine _inner apply {{} { yield iB; return dB }}]
    set r [_inner]
    yield "mid:$v/$r"
    return final
  }}]
  set b [_outer]
  list $a $b
} -cleanup {
  catch {rename _outer ""}
  catch {rename _inner ""}
  unset -nocomplain a b
} -result {mid:iB/dB final}}

###############################################################################

runTest {test coroutine-11.4 {
  R-18099-47953: outer coroutine yields BEFORE creating the inner
  coroutine, then creates and drives it on the next resume
} -constraints {
    coroutine
} -body {
  set a [coroutine _outer apply {{} {
    yield started
    set v [coroutine _inner apply {{} { yield iB; return dB }}]
    set r [_inner]
    return "done:$v/$r"
  }}]
  set b [_outer]
  list $a $b
} -cleanup {
  catch {rename _outer ""}
  catch {rename _inner ""}
  unset -nocomplain a b
} -result {started done:iB/dB}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
