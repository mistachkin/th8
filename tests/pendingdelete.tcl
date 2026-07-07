###############################################################################
#
# pendingdelete.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for the pending-delete queue: deferred deletion of commands and
# namespaces when the deletion occurs during script evaluation.
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
# Section 1 -- Command self-delete during dispatch
#
###############################################################################

runTest {test pendingdelete-1.1 {
  R-44541-15485: command that renames itself to empty during dispatch
} -body {
  proc _selfdelete {} {
    rename _selfdelete ""
    return "survived"
  }
  _selfdelete
} -result {survived}}

###############################################################################

runTest {test pendingdelete-1.2 {
  command gone after self-delete returns
} -body {
  proc _selfdelete2 {} {
    rename _selfdelete2 ""
    return "ok"
  }
  set r [_selfdelete2]
  list $r [info commands _selfdelete2]
} -cleanup {
  unset -nocomplain r
} -result {ok {}}}

###############################################################################

runTest {test pendingdelete-1.3 {
  command deletes another command during dispatch
} -body {
  proc _target {} { return "target" }
  proc _deleter {} {
    rename _target ""
    return "deleted"
  }
  set r [_deleter]
  list $r [info commands _target]
} -cleanup {
  catch {rename _deleter ""}
  unset -nocomplain r
} -result {deleted {}}}

###############################################################################

runTest {test pendingdelete-1.4 {
  chained self-delete: command A deletes command B which deletes itself
} -body {
  proc _chainB {} {
    rename _chainB ""
    return "B"
  }
  proc _chainA {} {
    set r [_chainB]
    rename _chainA ""
    return "A+$r"
  }
  set r [_chainA]
  list $r [info commands _chainA] [info commands _chainB]
} -cleanup {
  unset -nocomplain r
} -result {A+B {} {}}}

###############################################################################
#
# Section 2 -- Coroutine auto-delete (pending queue path)
#
###############################################################################

runTest {test pendingdelete-2.1 {
  coroutine command is auto-deleted when body completes
} -constraints {
    coroutine
} -body {
  proc _coro_body {} {
    yield "hello"
    return "done"
  }
  coroutine _coro _coro_body
  set r1 [_coro]
  list $r1 [info commands _coro]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _coro ""}
  unset -nocomplain r1
} -result {done {}}}

###############################################################################

runTest {test pendingdelete-2.2 {
  coroutine auto-delete does not corrupt interpreter state
} -constraints {
    coroutine
} -body {
  proc _coro_body {} {
    yield "a"
    yield "b"
    return "c"
  }
  coroutine _c _coro_body
  set r1 [_c]
  set r2 [_c]
  list $r1 $r2 [info commands _c]
} -cleanup {
  catch {rename _coro_body ""}
  catch {rename _c ""}
  unset -nocomplain r1 r2
} -result {b c {}}}

###############################################################################

runTest {test pendingdelete-2.3 {
  multiple coroutines auto-delete independently
} -constraints {
    coroutine
} -body {
  proc _body1 {} { yield "x"; return "1" }
  proc _body2 {} { yield "y"; return "2" }
  coroutine _c1 _body1
  coroutine _c2 _body2
  set r1 [_c1]
  set r2 [_c2]
  list $r1 $r2 [info commands _c1] [info commands _c2]
} -cleanup {
  catch {rename _body1 ""}
  catch {rename _body2 ""}
  catch {rename _c1 ""}
  catch {rename _c2 ""}
  unset -nocomplain r1 r2
} -result {1 2 {} {}}}

###############################################################################
#
# Section 3 -- Namespace delete during eval
#
###############################################################################

runTest {test pendingdelete-3.1 {
  R-45968-58474: namespace delete from within namespace eval
} -body {
  namespace eval ::_testns {
    proc hello {} { return "hello" }
  }
  namespace eval ::_testns {
    namespace delete ::_testns
  }
  info commands ::_testns::*
} -result {}}

###############################################################################

runTest {test pendingdelete-3.2 {
  namespace delete removes commands from that namespace
} -body {
  namespace eval ::_testns2 {
    proc foo {} { return "foo" }
    proc bar {} { return "bar" }
  }
  set before [lsort [info commands ::_testns2::*]]
  namespace delete ::_testns2
  set after [info commands ::_testns2::*]
  list $before $after
} -cleanup {
  catch {namespace delete ::_testns2}
  unset -nocomplain before after
} -result {{::_testns2::bar ::_testns2::foo} {}}}

###############################################################################

runTest {test pendingdelete-3.3 {
  namespace delete during command dispatch within the namespace
} -body {
  namespace eval ::_testns3 {
    proc selfDestruct {} {
      namespace delete ::_testns3
      return "boom"
    }
  }
  set r [::_testns3::selfDestruct]
  list $r [info commands ::_testns3::*]
} -cleanup {
  catch {namespace delete ::_testns3}
  unset -nocomplain r
} -result {boom {}}}

###############################################################################

runTest {test pendingdelete-3.4 {
  nested namespace delete: child ns deleted from parent
} -body {
  namespace eval ::_outer {
    namespace eval inner {
      proc greet {} { return "hi" }
    }
    proc killChild {} {
      namespace delete ::_outer::inner
      return "killed"
    }
  }
  set r [::_outer::killChild]
  list $r [info commands ::_outer::inner::*]
} -cleanup {
  catch {namespace delete ::_outer}
  unset -nocomplain r
} -result {killed {}}}

###############################################################################
#
# Section 4 -- Edge cases
#
###############################################################################

runTest {test pendingdelete-4.1 {
  delete nonexistent command returns error
} -body {
  catch {rename _no_such_cmd ""} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {no such command: _no_such_cmd}}

###############################################################################

runTest {test pendingdelete-4.2 {
  delete and recreate command in same eval
} -body {
  proc _recreate {} { return "v1" }
  set r1 [_recreate]
  rename _recreate ""
  proc _recreate {} { return "v2" }
  set r2 [_recreate]
  list $r1 $r2
} -cleanup {
  catch {rename _recreate ""}
  unset -nocomplain r1 r2
} -result {v1 v2}}

###############################################################################

runTest {test pendingdelete-4.3 {
  proc defined inside self-deleting proc works
} -body {
  proc _outer_del {} {
    proc _inner_new {} { return "inner" }
    rename _outer_del ""
    return [_inner_new]
  }
  set r [_outer_del]
  list $r [info commands _outer_del] [info commands _inner_new]
} -cleanup {
  catch {rename _inner_new ""}
  unset -nocomplain r
} -result {inner {} _inner_new}}

###############################################################################

runTest {test pendingdelete-4.4 {
  namespace delete then create same name in same eval
} -body {
  namespace eval ::_recrens {
    proc x {} { return "old" }
  }
  namespace delete ::_recrens
  namespace eval ::_recrens {
    proc x {} { return "new" }
  }
  set r [::_recrens::x]
  namespace delete ::_recrens
  set r
} -cleanup {
  catch {namespace delete ::_recrens}
  unset -nocomplain r
} -result {new}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
