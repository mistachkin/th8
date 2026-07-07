###############################################################################
#
# coverage_package_versions.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [package] subcommand decisions in
# src/plugins/th8_extensibility.c:
#
#   :836  if (pVer && pVer->pData)         ([package ifneeded] query)
#   :1035 while (i < n && z[i] >= '0' && z[i] <= '9')
#                                          (version digit loop)
#   :1147 if (nVer < 1 || nReq < 1 || aVer[0] != aReq[0])
#                                          ([package vsatisfies] guard)
#
# Existing package tests cover the common compatible-version
# vector but leave the empty-version, empty-required, and
# major-mismatch vectors uncovered, plus the non-digit version
# component path.
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

runTest {test pkgver-1.1 {
  package vsatisfies with empty version drives the (T,-,-)
  vector at th8_extensibility.c:1147 -- nVer < 1 short-
  circuits because the parsed version array has 0 entries.
} -constraints {
    th8
} -body {
  package vsatisfies "" "1.0"
} -result {0}}

###############################################################################

runTest {test pkgver-1.2 {
  package vsatisfies with empty requirement drives the
  (F,T,-) vector at line 1147 -- the parse of the version
  succeeds but the requirement is empty.
} -constraints {
    th8
} -body {
  package vsatisfies "1.0" ""
} -result {0}}

###############################################################################

runTest {test pkgver-1.3 {
  package vsatisfies with major-version mismatch drives
  the (F,F,T) vector at line 1147 -- both versions parse
  but the leading components differ.
} -constraints {
    th8
} -body {
  list \
      [package vsatisfies "1.0" "2.0"] \
      [package vsatisfies "2.5" "1.5"] \
      [package vsatisfies "1.5" "1.5"]
} -result {0 0 1}}

###############################################################################

runTest {test pkgver-1.4 {
  Version parse with non-digit character at the start of
  a component drives the F vector at line 1035 (the
  digit-range second-condition check exits the loop).
  TH8 stops parsing at the non-digit and returns the
  digits already consumed as a single component.
} -constraints {
    th8
} -body {
  list \
      [package vsatisfies "1.5a" "1.5"] \
      [package vsatisfies "1-extra" "1.0"] \
      [package vsatisfies "1.5.0" "1.5"]
} -result {1 1 1}}

###############################################################################

runTest {test pkgver-2.1 {
  package ifneeded query for a never-registered version
  drives the (F,-) vector at line 836 (pVer is NULL so
  the second condition is not evaluated).  TH8 returns
  an empty string for unknown ifneeded queries.
} -constraints {
    th8
} -body {
  list \
      [package ifneeded NeverRegisteredPkg42 1.0] \
      [package ifneeded NeverRegisteredPkg42 99.99]
} -cleanup {
  catch {package forget NeverRegisteredPkg42}
} -result {{} {}}}

###############################################################################

source tests/epilogue.tcl
