###############################################################################
#
# package.tcl --
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
# Section 1 -- package scan
#
###############################################################################

runTest {test package-1.1 {
  R-27404-11181: package scan returns empty string
} -constraints {
    package_scan
} -body {
  package scan
} -result {}}

###############################################################################

runTest {test package-1.2 {
  R-27404-11181: package scan does not leak previous result
} -constraints {
    package_scan
} -setup {
} -body {
  set x "sentinel"
  set x [package scan]
  set x
} -cleanup {
  unset -nocomplain x
} -result {}}

###############################################################################
#
# Section 2 -- package ifneeded result clearing
#
###############################################################################

runTest {test package-2.1 {
  R-11081-25091: package ifneeded register form clears result
} -constraints {
    package_ifneeded
} -body {
  package ifneeded _test_pkg_2_1 1.0 {puts loaded}
  package ifneeded _test_pkg_2_1 1.0
} -cleanup {
  package forget _test_pkg_2_1
} -result {puts loaded}}

###############################################################################

runTest {test package-2.2 {
  R-11081-25091: package ifneeded register form returns empty string
} -constraints {
    package_ifneeded
} -setup {
} -body {
  set x "sentinel"
  set x [package ifneeded _test_pkg_2_2 1.0 {puts loaded}]
  set x
} -cleanup {
  package forget _test_pkg_2_2
  unset -nocomplain x
} -result {}}

###############################################################################
#
# Section 3 -- package provide / require / names
#
###############################################################################

runTest {test package-3.1 {
  R-49109-54019: package ifneeded registers a script for a version
} -constraints {
    package_ifneeded package_require package_forget
} -body {
  package ifneeded _test_pkg_3_1 2.0 {package provide _test_pkg_3_1 2.0}
  package require _test_pkg_3_1
} -cleanup {
  package forget _test_pkg_3_1
} -result {2.0}}

###############################################################################

runTest {test package-3.2 {
  R-11675-43758: package names returns list of known packages
} -constraints {
    package
} -body {
  expr {[lsearch [package names] TH8] >= 0}
} -result {1}}

###############################################################################

runTest {test package-3.3 {
  R-16506-57090: package forget removes package information
} -constraints {
    package_ifneeded package_require package_forget
} -body {
  package ifneeded _test_pkg_3_3 1.0 {package provide _test_pkg_3_3 1.0}
  package require _test_pkg_3_3
  package forget _test_pkg_3_3
  expr {[lsearch [package names] _test_pkg_3_3] >= 0}
} -result {0}}

###############################################################################
#
# Section 4 -- package vcompare / vsatisfies
#
###############################################################################

runTest {test package-4.1 {
  R-17088-65425: package vcompare returns -1, 0, or 1
} -constraints {
    package_vcompare
} -body {
  list [package vcompare 1.0 2.0] \
      [package vcompare 2.0 2.0] \
      [package vcompare 3.0 2.0]
} -result {-1 0 1}}

###############################################################################

runTest {test package-4.2 {
  R-61177-25697: package vsatisfies returns boolean
} -constraints {
    package_vsatisfies
} -body {
  list [package vsatisfies 2.3 2.0] \
      [package vsatisfies 1.9 2.0] \
      [package vsatisfies 3.0 2.0]
} -result {1 0 0}}

###############################################################################

source tests/epilogue.tcl
