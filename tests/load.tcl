###############################################################################
#
# load.tcl --
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
# Section 1 -- load: error conditions (TH8-specific gate behavior)
#
###############################################################################

runTest {test load-1.1 {
  R-61098-59911: load gate blocks when not enabled
} -constraints {
    th8
} -setup {
} -body {
  #
  # This test only makes sense when the gate is disabled.
  # Since the shell enables loading, we test the error message
  # format by trying to load a nonexistent library.
  #
  list [catch {load nonexistent:Foo} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test load-1.2 {
  R-61098-59911: load with wrong # args
} -setup {
} -body {
  list [catch {load} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 2 -- unload: error conditions and option parsing
#
###############################################################################

runTest {test unload-2.1 {
  R-56356-31045: unload of not-loaded library is error
} -constraints {
    th8
} -setup {
} -body {
  list [catch {unload "notloaded:Foo"} msg] [string match "*not loaded*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test unload-2.2 {
  R-58288-53995: unload -nocomplain suppresses all errors
} -constraints {
    th8
} -body {
  unload -nocomplain "notloaded:Foo"
} -result {}}

###############################################################################

runTest {test unload-2.3 {
  R-03886-00091: unload wrong # args
} -setup {
} -body {
  list [catch {unload} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test unload-2.4 {
  R-03886-00091: unload -- allows names starting with dash
} -constraints {
    th8
} -setup {
} -body {
  list [catch {unload -- "-mylib:Foo"} msg] [string match "*not loaded*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test unload-2.5 {
  R-15174-22820: unload -keeplibrary is default
} -constraints {
    th8
} -setup {
} -body {
  # This verifies the option is accepted (no "bad option" error).
  catch {unload -keeplibrary "notloaded:Foo"} msg
  string match "*not loaded*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test unload-2.6 {
  R-50869-02312: unload -nokeeplibrary accepted
} -constraints {
    th8
} -setup {
} -body {
  catch {unload -nokeeplibrary "notloaded:Foo"} msg
  string match "*not loaded*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test unload-2.7 {
  R-58288-53995: unload -nocomplain -keeplibrary
} -constraints {
    th8
} -body {
  unload -nocomplain -keeplibrary "notloaded:Foo"
} -result {}}

###############################################################################

runTest {test unload-2.8 {
  R-58288-53995: unload -nocomplain -nokeeplibrary
} -constraints {
    th8
} -body {
  unload -nocomplain -nokeeplibrary "notloaded:Foo"
} -result {}}

###############################################################################
#
# Section 3 -- load/unload lifecycle with test shared library
#
###############################################################################

###############################################################################
#
# NOTE: Tests in sections 3-6 call [load] and [unload] directly
# (not testLoadLib/testUnloadLib) because they are testing the
# load/unload mechanism itself.  The prologue's pre-load is
# undone in -setup and restored in -cleanup.
#
###############################################################################

runTest {test load-3.1 {
  R-25864-47382: load registers commands from shared library
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 0]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {1970-01-01T00:00:00Z}}

###############################################################################

runTest {test load-3.2 {
  R-33726-54804: load returns empty string on success
} -constraints {
    loadLib th8
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  unload $::testlib_name
} -cleanup {
  detectLoadLib; testLoadLib
} -result {}}

###############################################################################

runTest {test load-3.3 {
  R-11211-50952: loaded library appears in tracking
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 86400]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {1970-01-02T00:00:00Z}}

###############################################################################

runTest {test load-3.4 {
  R-45616-08734: unload removes commands from interpreter
} -constraints {
    loadLib th8
} -setup {
  catch {unload $::loadtest_name}
} -body {
  # Use the independent Th8loadtest entry point so this test fully
  # owns the library's reference -- the harness's own "Th8test"
  # load is a separate (file,symbol) tracking entry and does not
  # keep ::__th8_loadtest_marker alive.
  load $::loadtest_name
  set before [catch {__th8_loadtest_marker}]
  unload $::loadtest_name
  set after [catch {__th8_loadtest_marker}]
  list $before $after
} -cleanup {
  catch {unload $::loadtest_name}
  unset -nocomplain before after
} -result {0 1}}

###############################################################################

runTest {test load-3.5 {
  R-21269-37512: unload returns empty string on success
} -constraints {
    loadLib th8
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  unload $::testlib_name
} -cleanup {
  detectLoadLib; testLoadLib
} -result {}}

###############################################################################

runTest {test load-3.6 {
  R-56356-31045: unload of already-unloaded library is error
} -constraints {
    loadLib th8
} -setup {
  catch {unload $::loadtest_name}
} -body {
  load $::loadtest_name
  unload $::loadtest_name
  list [catch {unload $::loadtest_name} msg] \
      [string match "*not loaded*" $msg]
} -cleanup {
  catch {unload $::loadtest_name}
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test load-3.7 {
  R-25864-47382: reload after unload restores commands
} -constraints {
    loadLib th8
} -setup {
  catch {unload $::loadtest_name}
} -body {
  load $::loadtest_name
  unload $::loadtest_name
  set gone [catch {__th8_loadtest_marker}]
  load $::loadtest_name
  set restored [__th8_loadtest_marker]
  unload $::loadtest_name
  list $gone $restored
} -cleanup {
  catch {unload $::loadtest_name}
  unset -nocomplain gone restored
} -result {1 ok}}

###############################################################################

runTest {test load-3.8 {
  R-50869-02312: unload -nokeeplibrary with TH8_UNLOAD_DANGEROUS
} -constraints {
    loadLib th8
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  unload -nokeeplibrary $::testlib_name
} -cleanup {
  detectLoadLib; testLoadLib
} -result {}}

###############################################################################

runTest {test load-3.9 {
  R-58288-53995: unload -nocomplain after already unloaded
} -constraints {
    loadLib th8
} -setup {
  catch {unload $::loadtest_name}
} -body {
  load $::loadtest_name
  unload $::loadtest_name
  unload -nocomplain $::loadtest_name
} -cleanup {
  catch {unload $::loadtest_name}
} -result {}}

###############################################################################
#
# Section 4 -- isotime command behavior
#
###############################################################################

runTest {test load-4.1 {
  R-25864-47382: isotime Unix epoch
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 0]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {1970-01-01T00:00:00Z}}

###############################################################################

runTest {test load-4.2 {
  R-25864-47382: isotime one day after epoch
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 86400]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {1970-01-02T00:00:00Z}}

###############################################################################

runTest {test load-4.3 {
  R-25864-47382: isotime Y2K
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 946684800]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {2000-01-01T00:00:00Z}}

###############################################################################

runTest {test load-4.4 {
  R-25864-47382: isotime leap year date (2024-02-29)
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 1709164800]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {2024-02-29T00:00:00Z}}

###############################################################################

runTest {test load-4.5 {
  R-25864-47382: isotime end of 2024
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 1735689599]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {2024-12-31T23:59:59Z}}

###############################################################################

runTest {test load-4.6 {
  R-25864-47382: isotime specific time with hours/minutes/seconds
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [th8testlib::isotime 1711036923]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {2024-03-21T16:02:03Z}}

###############################################################################

runTest {test load-4.7 {
  R-25864-47382: isotime wrong # args
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [list [catch {th8testlib::isotime} msg] $msg]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test load-4.8 {
  R-25864-47382: isotime non-integer argument
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [list [catch {th8testlib::isotime "notanumber"} msg] \
      [expr {$msg ne ""}]]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result msg
} -result {1 1}}

###############################################################################
#
# Section 5 -- load with explicit initProc (Section 18.4)
#
###############################################################################

runTest {test load-5.1 {
  R-41729-49555: load with explicit initProc names the initialization entry
                 point
} -constraints {
    loadLib
} -setup {
  testUnloadLib
} -body {
  load $::testlib_name
  set result [expr {[catch {th8testlib::isotime 0}] == 0}]
  unload $::testlib_name
  set result
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result
} -result {1}}

###############################################################################
#
# Section 6 -- unload -nokeeplibrary (Section 18.5)
#
###############################################################################

runTest {test load-6.1 {
  R-36679-29193: unload -nokeeplibrary errors unless TH8_UNLOAD_DANGEROUS is
                 enabled
} -constraints {
    th8 loadLib
} -setup {
  testUnloadLib
} -body {
  #
  # The shell enables TH8_UNLOAD_DANGEROUS, so this should
  # succeed.  The test verifies the option is accepted.
  #
  load $::testlib_name
  set result [catch {unload -nokeeplibrary $::testlib_name} msg]
  list $result [expr {$msg eq ""}]
} -cleanup {
  detectLoadLib; testLoadLib
  unset -nocomplain result msg
} -result {0 1}}

###############################################################################

source tests/epilogue.tcl
