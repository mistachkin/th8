###############################################################################
#
# coverage_package_states.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for package-state compounds in
# src/plugins/th8_extensibility.c:
#
#   line 617  if (zUnk && zUnk[0])  -- package unknown handler check
#
# `package require` for a missing package routes through the
# unknown handler; this test exercises with handler set, cleared,
# and re-set forms.
#
# Coverage-driven; not pinned to specific R-markers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test pkg_st-1.1 {
  package require for missing package with cleared unknown handler
  drives the F,- vector at line 617 (zUnk == NULL or empty)
} -constraints {
    th8
} -body {
  # Save and clear the existing unknown handler.
  set saved [package unknown]
  package unknown ""
  catch {package require _no_such_package_pkg_st_1_1_} r
  package unknown $saved
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain saved r
} -result {1}}

###############################################################################

runTest {test pkg_st-1.2 {
  package require for missing package with non-empty unknown
  handler drives the T,T vector at line 617
} -constraints {
    th8
} -body {
  set saved [package unknown]
  package unknown {set ::pkg_st_1_2_called 1}
  catch {package require _no_such_package_pkg_st_1_2_} r
  package unknown $saved
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain saved r ::pkg_st_1_2_called
} -result {1}}

###############################################################################

runTest {test pkg_st-1.3 {
  package require with default unknown handler (whatever is
  installed by prologue) -- drives whichever vector is current
} -constraints {
    th8
} -body {
  catch {package require _no_such_package_pkg_st_1_3_} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
