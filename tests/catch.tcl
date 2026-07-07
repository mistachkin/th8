###############################################################################
#
# catch.tcl --
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
# Section 1 -- catch: Catching OK (return code 0)
#
###############################################################################

runTest {test catch-1.1 {
  R-56831-47631: catch returns 0 for successful command
} -setup {
} -body {
  set result [catch {expr {1 + 1}}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {0}}

###############################################################################

runTest {test catch-1.2 {
  R-56831-47631: catch returns 0 for set command
} -setup {
} -body {
  set result [catch {set x "hello"}]
  set result
} -cleanup {
  unset -nocomplain result
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test catch-1.3 {
  R-31903-30952: catch OK with varname captures result string
} -setup {
} -body {
  set rc [catch {expr {2 + 3}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {0 5}}

###############################################################################
#
# Section 2 -- catch: Catching errors (return code 1)
#
###############################################################################

runTest {test catch-2.1 {
  R-56831-47631: catch returns 1 for error as return code integer
} -setup {
} -body {
  set result [catch {error "test error"}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test catch-2.2 {
  R-31903-30952: catch error with varname stores error message in variable
} -setup {
} -body {
  set rc [catch {error "test error"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1 {test error}}}

###############################################################################

runTest {test catch-2.3 {
  R-56291-06757: catch undefined variable returns error code 1
} -setup {
} -body {
  set rc [catch {set nosuchvar} msg]
  set rc
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  unset -nocomplain nosuchvar
} -result {1}}

###############################################################################

runTest {test catch-2.4 {
  R-56291-06757: catch undefined command returns error code 1
} -setup {
} -body {
  set rc [catch {nosuchcommand} msg]
  set rc
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 3 -- catch: Catching break (return code 3)
#
###############################################################################

runTest {test catch-3.1 {
  R-28638-19737: catch returns 3 for break
} -setup {
} -body {
  set result [catch {break}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {3}}

###############################################################################

runTest {test catch-3.2 {
  R-28638-19737: catch break with varname stores result
} -setup {
} -body {
  set rc [catch {break} msg]
  set rc
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {3}}

###############################################################################
#
# Section 4 -- catch: Catching continue (return code 4)
#
###############################################################################

runTest {test catch-4.1 {
  R-27705-58825: catch returns 4 for continue
} -setup {
} -body {
  set result [catch {continue}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {4}}

###############################################################################

runTest {test catch-4.2 {
  R-27705-58825: catch continue with varname stores result
} -setup {
} -body {
  set rc [catch {continue} msg]
  set rc
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {4}}

###############################################################################
#
# Section 5 -- catch: Catching return (return code 2)
#
###############################################################################

runTest {test catch-5.1 {
  R-56831-47631: catch returns 2 for return as return code integer
} -setup {
} -body {
  set result [catch {return "value"}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {2}}

###############################################################################

runTest {test catch-5.2 {
  R-31903-30952: catch return with varname captures value in variable
} -setup {
} -body {
  set rc [catch {return "hello"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {2 hello}}

###############################################################################
#
# Section 6 -- catch: Nested catch
#
###############################################################################

runTest {test catch-6.1 {
  R-59093-15883: nested catch always returns TCL_OK for outer catch
} -setup {
} -body {
  set outer_rc [catch {
    set inner_rc [catch {error "inner error"} inner_msg]
    list $inner_rc $inner_msg
  }]
  set outer_rc
} -cleanup {
  unset -nocomplain outer_rc
  unset -nocomplain inner_rc
  unset -nocomplain inner_msg
} -result {0}}

###############################################################################

runTest {test catch-6.2 {
  R-31903-30952: nested catch captures inner error message in varname
} -setup {
} -body {
  catch {
    catch {error "inner error"} inner_msg
    set inner_msg
  } outer_msg
  set outer_msg
} -cleanup {
  unset -nocomplain outer_msg
  unset -nocomplain inner_msg
} -result {inner error}}

###############################################################################

runTest {test catch-6.3 {
  R-56831-47631: outer catch sees error not caught by inner catch
} -setup {
} -body {
  set rc [catch {
    catch {expr {1 + 1}}
    error "outer error"
  } msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1 {outer error}}}

###############################################################################
#
# Section 7 -- catch: Return value usage
#
###############################################################################

runTest {test catch-7.1 {
  R-59093-15883: catch with empty body always returns TCL_OK
} -setup {
} -body {
  set result [catch {}]
  set result
} -cleanup {
  unset -nocomplain result
} -result {0}}

###############################################################################

runTest {test catch-7.2 {
  R-59093-15883: catch result used in if condition always returns TCL_OK
} -setup {
} -body {
  if {[catch {error "err"}]} then {
    set result "caught"
  } else {
    set result "not caught"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {caught}}

###############################################################################

runTest {test catch-7.3 {
  R-56831-47631: catch with wrong # args produces error
} -setup {
} -body {
  list [catch {catch} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 8 -- catch: Additional Tcl 8.4 behavior tests
#
###############################################################################

runTest {test catch-8.1 {
  R-56831-47631: catch returns 2 for return command inside script
} -setup {
} -body {
  set rc [catch {return "from script"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {2 {from script}}}

###############################################################################

runTest {test catch-8.2 {
  R-56831-47631: catch returns 3 for break command inside script
} -setup {
} -body {
  set rc [catch {
    set x 1
    break
    set x 2
  } msg]
  set rc
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  unset -nocomplain x
} -result {3}}

###############################################################################

runTest {test catch-8.3 {
  R-56831-47631: catch returns 4 for continue command inside script
} -setup {
} -body {
  set rc [catch {
    set x 1
    continue
    set x 2
  } msg]
  set rc
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
  unset -nocomplain x
} -result {4}}

###############################################################################

runTest {test catch-8.4 {
  R-59093-15883: catch with nested errors, error in error handler
} -setup {
} -body {
  set outer_rc [catch {
    set inner_rc [catch {error "first error"} inner_msg]
    error "second error: $inner_msg"
  } outer_msg]
  list $outer_rc $outer_msg $inner_rc $inner_msg
} -cleanup {
  unset -nocomplain outer_rc
  unset -nocomplain outer_msg
  unset -nocomplain inner_rc
  unset -nocomplain inner_msg
} -result {1 {second error: first error} 1 {first error}}}

###############################################################################

runTest {test catch-8.5 {
  R-31903-30952: catch stores error message in variable
} -setup {
} -body {
  set rc [catch {error "detailed error message"} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {1 {detailed error message}}}

###############################################################################

runTest {test catch-8.6 {
  R-31903-30952: catch stores normal result in variable when script succeeds
} -setup {
} -body {
  set rc [catch {expr {6 * 7}} msg]
  list $rc $msg
} -cleanup {
  unset -nocomplain rc
  unset -nocomplain msg
} -result {0 42}}

###############################################################################

runTest {test catch-8.7 {
  R-56831-47631: catch with no variable name still returns the code
} -setup {
} -body {
  set rc_ok  [catch {expr {1 + 1}}]
  set rc_err [catch {error "err"}]
  set rc_ret [catch {return "val"}]
  set rc_brk [catch {break}]
  set rc_cnt [catch {continue}]
  list $rc_ok $rc_err $rc_ret $rc_brk $rc_cnt
} -cleanup {
  unset -nocomplain rc_ok
  unset -nocomplain rc_err
  unset -nocomplain rc_ret
  unset -nocomplain rc_brk
  unset -nocomplain rc_cnt
} -result {0 1 2 3 4}}

###############################################################################

source tests/epilogue.tcl
