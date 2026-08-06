###############################################################################
#
# hooks.tcl --
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
# Section 1 -- preeval hook: allow mode
#
###############################################################################

runTest {test hooks-1.1 {
  preeval install in allow mode, eval succeeds
} -constraints {
    loadLib th8
} -body {
  th8testlib::preeval install
  set x [expr {1 + 2}]
  set x
} -cleanup {
  catch {th8testlib::preeval uninstall}
  unset -nocomplain x
} -result {3}}

###############################################################################

runTest {test hooks-1.2 {
  preeval count increments on each eval
} -constraints {
    loadLib th8
} -body {
  th8testlib::preeval install
  set before [th8testlib::preeval count]
  set x 1
  set after [th8testlib::preeval count]
  expr {$after > $before}
} -cleanup {
  catch {th8testlib::preeval uninstall}
  unset -nocomplain x after before
} -result {1}}

###############################################################################

runTest {test hooks-1.3 {
  preeval mode returns allow
} -constraints {
    loadLib th8
} -body {
  th8testlib::preeval install
  th8testlib::preeval mode
} -cleanup {
  catch {th8testlib::preeval uninstall}
} -result {allow}}

###############################################################################
#
# Section 2 -- preeval hook: deny mode
#
###############################################################################

runTest {test hooks-2.1 {
  preeval deny mode blocks script evaluation (C-level test)
} -constraints {
    loadLib th8
} -body {
  #
  # Use the C-level deny test command to safely test deny
  # mode.  This installs deny, attempts an eval, uninstalls,
  # and reports whether the eval was blocked -- all in C,
  # avoiding the problem where deny mode blocks the test
  # framework's own script evaluation.
  #
  th8testlib::preeval_denytest
} -result {denied}}

###############################################################################
#
# Section 3 -- preeval hook: uninstall
#
###############################################################################

runTest {test hooks-3.1 {
  preeval uninstall, eval succeeds without hook
} -constraints {
    loadLib th8
} -body {
  th8testlib::preeval install deny
  th8testlib::preeval uninstall
  set x [expr {2 + 3}]
  set x
} -cleanup {
  unset -nocomplain x
} -result {5}}

###############################################################################

runTest {test hooks-3.2 {
  preeval count resets to 0 after uninstall
} -constraints {
    loadLib th8
} -body {
  th8testlib::preeval install
  set x 1
  th8testlib::preeval uninstall
  th8testlib::preeval count
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################
#
# Section 4 -- preload hook: allow mode
#
###############################################################################

runTest {test hooks-4.1 {
  preload install in allow mode succeeds
} -constraints {
    loadLib th8
} -body {
  th8testlib::preload install
  th8testlib::preload mode
} -cleanup {
  catch {th8testlib::preload uninstall}
} -result {allow}}

###############################################################################

runTest {test hooks-4.2 {
  preload count starts at 0 after install
} -constraints {
    loadLib th8
} -body {
  th8testlib::preload install
  th8testlib::preload count
} -cleanup {
  catch {th8testlib::preload uninstall}
} -result {0}}

###############################################################################
#
# Section 5 -- preload hook: deny mode
#
###############################################################################

runTest {test hooks-5.1 {
  preload install in deny mode
} -constraints {
    loadLib th8
} -body {
  th8testlib::preload install deny
  th8testlib::preload mode
} -cleanup {
  catch {th8testlib::preload uninstall}
} -result {deny}}

###############################################################################

runTest {test hooks-5.2 {
  preload mode change from allow to deny
} -constraints {
    loadLib th8
} -body {
  th8testlib::preload install
  set before [th8testlib::preload mode]
  th8testlib::preload mode deny
  set after [th8testlib::preload mode]
  list $before $after
} -cleanup {
  catch {th8testlib::preload uninstall}
  unset -nocomplain before after
} -result {allow deny}}

###############################################################################
#
# Section 6 -- preload hook: uninstall
#
###############################################################################

runTest {test hooks-6.1 {
  preload uninstall succeeds
} -constraints {
    loadLib th8
} -setup {
} -body {
  th8testlib::preload install
  list [catch {th8testlib::preload uninstall} msg] [expr {$msg ne "error"}]
} -cleanup {
  unset -nocomplain msg
} -result {0 1}}

###############################################################################

runTest {test hooks-6.2 {
  preload count resets to 0 after uninstall
} -constraints {
    loadLib th8
} -body {
  th8testlib::preload install
  th8testlib::preload uninstall
  th8testlib::preload count
} -result {0}}

###############################################################################
#
# Section 7 -- cancel-unwind recovery
#
# Verifies that an interpreter recovers after cancel-unwind.
# Uses an isolated temporary interpreter created in C by the
# th8testlib::cancel_recover command.
#
###############################################################################

runTest {test hooks-7.1 {
  R-48881-39702 R-16133-17525: An interpreter SHALL be usable for
                 evaluation after a cancel-unwind operation completes; when
                 TH8_CANCEL_UNWIND fires, the outermost Th8_Eval restores
                 nEvalDepth to its entry value and clears the cancel state
                 so a subsequent evaluation succeeds.  cancel_recover arms
                 cancel-unwind, evaluates a script that fails, then
                 evaluates a second script that must succeed -- which can
                 only happen if the depth/cancel state was reset.
} -constraints {
    loadLib th8
} -body {
  th8testlib::cancel_recover
} -result {ok}}

###############################################################################

runTest {test hooks-7.2 {
  R-00559-15113: The -unwind option causes all call frames to unwind to the
                 top level, preventing catch from intercepting the
                 cancellation.
} -constraints {
    loadLib th8
} -body {
  th8testlib::catch_unwind
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
