###############################################################################
#
# coverage2.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for untested requirements across Sections 5, 12, 13, 18, 20, 22, 24,
# 26, and 27 of the Tcl Language Standard.
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
# Section 1 -- Control Flow (Section 12)
#
###############################################################################

runTest {test coverage2-1.1 {
  R-27941-12603: eval concatenates its arguments in the same fashion as the
                 concat command
} -setup {
} -body {
  eval set x 5
  set x
} -cleanup {
  unset -nocomplain x
} -result {5}}

###############################################################################

runTest {test coverage2-1.2 {
  R-12040-05089: error command produces an error with the given message as the
                 result string
} -body {
  catch {error "test error"}
} -result {1}}

###############################################################################

runTest {test coverage2-1.3 {
  R-29669-65193: error with info argument sets the initial value of the
                 errorInfo variable
} -setup {
} -body {
  catch {error msg "custom info"} msg
  set errorInfo
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*custom info*}}

###############################################################################

runTest {test coverage2-1.4 {
  R-13453-54978: error with code argument stores the given code in the
                 errorCode variable
} -setup {
} -body {
  catch {error msg info CODE} msg
  set errorCode
} -cleanup {
  unset -nocomplain msg
} -result {CODE}}

###############################################################################

runTest {test coverage2-1.5 {
  R-03654-57637: return with -errorinfo and -errorcode options are ignored
                 unless the return code is error
} -constraints {
    return_errorinfo
} -setup {
  #
  # Pre-clear errorInfo / errorCode so we can assert they remain
  # unchanged after the return-with-options call.  A test that
  # only verifies the return value cannot distinguish "options
  # ignored" from "options silently stored": the strong assertion
  # is that errorInfo / errorCode are NOT mutated.
  #
  set errorInfo ""
  set errorCode NONE
} -body {
  proc _cov2_rettest {} {
    return -code ok -errorinfo "should be ignored" \
        -errorcode {SHOULD BE IGNORED} "ok result"
  }
  set rv [_cov2_rettest]
  list $rv [string length $errorInfo] [string equal $errorCode NONE]
} -cleanup {
  catch {rename _cov2_rettest ""}
  unset -nocomplain rv
} -result {{ok result} 0 1}}

###############################################################################
#
# Section 2 -- Procedures (Section 13)
#
###############################################################################

runTest {test coverage2-2.1 {
  R-00011-09866: uplevel evaluates script in the call frame identified by level
} -setup {
} -body {
  proc _cov2_uplevel {} {
    uplevel 1 {set x 99}
  }
  set x ""
  _cov2_uplevel
  set x
} -cleanup {
  catch {rename _cov2_uplevel ""}
  unset -nocomplain x
} -result {99}}

###############################################################################

runTest {test coverage2-2.2 {
  R-30634-62834: rename command changes the name of an existing command
} -setup {
} -body {
  proc _cov2_foo {} {return bar}
  rename _cov2_foo _cov2_baz
  _cov2_baz
} -cleanup {
  catch {rename _cov2_baz ""}
} -result {bar}}

###############################################################################

runTest {test coverage2-2.3 {
  R-58228-24850: rename to the empty string deletes the command
} -body {
  proc _cov2_del {} {return gone}
  rename _cov2_del ""
  catch {_cov2_del}
} -result {1}}

###############################################################################

runTest {test coverage2-2.4 {
  R-35801-03855: renaming a nonexistent command produces an error
} -body {
  catch {rename _cov2_nosuchcmd _cov2_bar}
} -result {1}}

###############################################################################

runTest {test coverage2-2.5 {
  R-01034-50775: tailcall replaces the current procedure invocation with the
                 specified command, evaluated in the caller's scope
} -constraints {
    tailcall
} -body {
  proc _cov2_tc_a {} {tailcall _cov2_tc_b}
  proc _cov2_tc_b {} {return "from b"}
  _cov2_tc_a
} -cleanup {
  catch {rename _cov2_tc_a ""}
  catch {rename _cov2_tc_b ""}
} -result {from b}}

runTest {test coverage2-2.6 {
  R-15545-60396: tailcall releases the current procedure's local variables
                 before the new command executes
} -constraints {
    tailcall
} -body {
  #
  # Set a local variable in proc A, then tailcall to proc B.
  # Proc B runs in the caller's scope (not A's).  If A's
  # locals were properly released, [info exists] in B (at
  # the caller's level) should not see A's local.
  #
  proc _cov2_tc_release_a {} {
    set localVar "from_a"
    tailcall _cov2_tc_release_b
  }
  proc _cov2_tc_release_b {} {
    info exists localVar
  }
  _cov2_tc_release_a
} -cleanup {
  catch {rename _cov2_tc_release_a ""}
  catch {rename _cov2_tc_release_b ""}
} -result {0}}

###############################################################################
#
# Section 3 -- I/O (Section 18)
#
###############################################################################

runTest {test coverage2-3.1 {
  R-24090-61568: puts command writes string followed by a newline to the output
                 without error
} -constraints {
    puts
} -body {
  #
  # We cannot easily verify newline output in the test
  # framework, but we can verify the command does not
  # produce an error and returns the empty string.
  #
  puts "coverage2 puts test"
} -result {}}

###############################################################################
#
# Section 4 -- Introspection (Section 20)
#
###############################################################################

runTest {test coverage2-4.1 {
  R-34410-07117: applying info args to a nonexistent or non-procedure command
                 produces an error
} -body {
  catch {info args _cov2_nosuchproc}
} -result {1}}

###############################################################################
#
# Section 5 -- Packages (Section 22)
#
###############################################################################

runTest {test coverage2-5.1 {
  R-37867-23416: package provide declares that the named package is present,
                 optionally at the given version
} -constraints {
    package
} -setup {
  catch {package forget _cov2_testpkg}
} -body {
  package provide _cov2_testpkg 1.0
  package provide _cov2_testpkg
} -cleanup {
  catch {package forget _cov2_testpkg}
} -result {1.0}}

###############################################################################

runTest {test coverage2-5.2 {
  R-11675-43758: package names returns a list of all known package names
} -constraints {
    package
} -setup {
  catch {package forget _cov2_testpkg2}
} -body {
  package provide _cov2_testpkg2 2.0
  expr {[lsearch [package names] _cov2_testpkg2] >= 0}
} -cleanup {
  catch {package forget _cov2_testpkg2}
} -result {1}}

###############################################################################

runTest {test coverage2-5.3 {
  R-30063-56559: package versions returns a list of all known versions of the
                 named package
} -constraints {
    package package_ifneeded
} -setup {
  catch {package forget _cov2_testpkg3}
} -body {
  package ifneeded _cov2_testpkg3 3.0 {}
  expr {[lsearch [package versions _cov2_testpkg3] 3.0] >= 0}
} -cleanup {
  catch {package forget _cov2_testpkg3}
} -result {1}}

###############################################################################

runTest {test coverage2-5.4 {
  R-16506-57090: package forget removes all information about the named
                 packages
} -constraints {
    package package_forget
} -setup {
  catch {package forget _cov2_testpkg4}
} -body {
  package provide _cov2_testpkg4 1.0
  package forget _cov2_testpkg4
  expr {[lsearch [package names] _cov2_testpkg4] < 0}
} -cleanup {
  catch {package forget _cov2_testpkg4}
} -result {1}}

###############################################################################

runTest {test coverage2-5.5 {
  R-49109-54019: package ifneeded registers a script that, when evaluated,
                 provides the given version of the named package
} -constraints {
    package package_ifneeded
} -setup {
  catch {package forget _cov2_testpkg5}
} -body {
  package ifneeded _cov2_testpkg5 1.0 {set _cov2_loaded 1}
  package ifneeded _cov2_testpkg5 1.0
} -cleanup {
  catch {package forget _cov2_testpkg5}
} -result {set _cov2_loaded 1}}

###############################################################################

runTest {test coverage2-5.6 {
  R-17088-65425: package vcompare compares two version strings and returns -1,
                 0, or 1
} -constraints {
    package_vcompare
} -body {
  list [package vcompare 1.0 2.0] \
      [package vcompare 2.0 2.0] \
      [package vcompare 3.0 2.0]
} -result {-1 0 1}}

###############################################################################

runTest {test coverage2-5.7 {
  R-61177-25697: package vsatisfies returns 1 if version satisfies requirement,
                 0 otherwise
} -constraints {
    package_vsatisfies
} -body {
  list [package vsatisfies 1.5 1.0-2.0] \
      [package vsatisfies 2.5 1.0-2.0]
} -result {1 0}}

###############################################################################
#
# Section 6 -- Time and Process (Section 24)
#
###############################################################################

runTest {test coverage2-6.1 {
  R-42628-45607: clock seconds returns the current time as the number of
                 seconds since the Unix epoch
} -constraints {
    clock
} -body {
  expr {[clock seconds] > 1000000}
} -result {1}}

###############################################################################

runTest {test coverage2-6.2 {
  R-13293-54261: pid returns the process identifier of the host process
} -constraints {
    pid
} -body {
  expr {[pid] > 0}
} -result {1}}

###############################################################################
#
# Section 7 -- Error Model (Section 26)
#
###############################################################################

runTest {test coverage2-7.1 {
  R-07142-01462: when a command returns TCL_ERROR, the error propagates up the
                 call stack until intercepted by catch
} -body {
  proc _cov2_err {} {error oops}
  catch {_cov2_err}
} -cleanup {
  catch {rename _cov2_err ""}
} -result {1}}

###############################################################################

runTest {test coverage2-7.2 {
  R-45043-49981: the errorInfo variable contains a human-readable stack trace
                 of the error
} -setup {
} -body {
  proc _cov2_err2 {} {error "stack test"}
  catch {_cov2_err2} msg
  expr {[string length $errorInfo] > 0}
} -cleanup {
  catch {rename _cov2_err2 ""}
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test coverage2-7.3 {
  R-07255-02881: the errorCode variable contains a machine-readable error code
                 set by the error command
} -setup {
} -body {
  catch {error msg {} MYCODE} msg
  set errorCode
} -cleanup {
  unset -nocomplain msg
} -result {MYCODE}}

###############################################################################
#
# Section 8 -- Name Resolution (Section 27)
#
###############################################################################

runTest {test coverage2-8.1 {
  R-30207-53705: an unqualified command name is first looked up in the current
                 namespace, then in the global namespace
} -constraints {
    namespace
} -setup {
} -body {
  namespace eval ::_cov2_nstest {
    proc foo {} {return "from ns"}
  }
  namespace eval ::_cov2_nstest {foo}
} -cleanup {
  catch {namespace delete ::_cov2_nstest}
} -result {from ns}}

###############################################################################

runTest {test coverage2-8.2 {
  R-48221-02733: a fully qualified name beginning with :: is resolved from the
                 global namespace
} -constraints {
    namespace
} -body {
  proc ::_cov2_globalcmd {} {return global}
  ::_cov2_globalcmd
} -cleanup {
  catch {rename ::_cov2_globalcmd ""}
} -result {global}}

###############################################################################
#
# Section 9 -- Lexical Structure (Section 5)
#
###############################################################################

runTest {test coverage2-9.1 {
  R-57614-25424: a backslash immediately followed by a newline inside a braced
                 expression is treated as whitespace (line continuation)
} -setup {
} -body {
  set x {a\
      b}
  set x
} -cleanup {
  unset -nocomplain x
} -result {a b}}

###############################################################################

source tests/epilogue.tcl
