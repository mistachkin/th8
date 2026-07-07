###############################################################################
#
# coverage_embedded_key_faults_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for the three identical lazy-init guards in
# src/plugins/harpy/th8_policy.c:
#
#   Th8_GetPublicKeyZero  -- L2091  if (!zData || nData == 0) return NULL;
#   Th8_GetPublicKeyRoot  -- L2190  if (!zData || nData == 0) return NULL;
#   Th8_GetPublicKeyTest  -- L2295  if (!zData || nData == 0) return NULL;
#
# Each decision has 2 conditions (C1: !zData, C2: nData == 0).
# In normal operation Th8_GetEmbeddedKey0/Root/Test always return
# a valid blob with a non-zero size, so the only MC/DC vector
# observed is (F, F) (fall-through past the guard); both C1 and
# C2 are reported as not independence-pair-covered.
#
# Each test below uses the C-side driver
# `th8testlib::drivekeyfault KEY MODE` which performs the full
# cycle (cache reset, fault install, getpublickey call, fault
# uninstall) in C so the lazy-init body re-enters with the
# fault active.  Modes:
#   1 -- force zData=NULL (drives C1=T, propagates "cannot load
#        embedded ..." error from the *Token wrapper)
#   2 -- force nData=0    (drives C1=F, C2=T, same propagation)
#
# Closes 6 MC/DC conditions (two per decision).
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

runTest {test embedkey-1.1 {
  th8_policy.c L2091 Th8_GetPublicKeyZero (T,-) -- zData==NULL
  via drivekeyfault zero 1.  The driver clears the cache,
  installs a fault forcing zData to NULL, calls
  Th8_GetPublicKeyZeroToken, captures the "cannot load
  embedded key0" error from the wrapper, and uninstalls the
  fault.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {th8testlib::drivekeyfault zero 1} msg]
  list $rc [string match "*cannot load embedded key0*" $msg]
} -cleanup {
  th8testlib::resetkeycaches
  catch {th8testlib::getpublickeytoken zero}
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test embedkey-1.2 {
  th8_policy.c L2091 Th8_GetPublicKeyZero (F,T) -- nData==0
  via drivekeyfault zero 2.  Drives the C2 condition with
  the embedded blob pointer preserved but the size forced
  to 0.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {th8testlib::drivekeyfault zero 2} msg]
  list $rc [string match "*cannot load embedded key0*" $msg]
} -cleanup {
  th8testlib::resetkeycaches
  catch {th8testlib::getpublickeytoken zero}
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test embedkey-2.1 {
  th8_policy.c L2190 Th8_GetPublicKeyRoot (T,-) -- zData==NULL
  via drivekeyfault root 1.  Mirrors embedkey-1.1 for keyRoot.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {th8testlib::drivekeyfault root 1} msg]
  list $rc [string match "*cannot load embedded keyRoot*" $msg]
} -cleanup {
  th8testlib::resetkeycaches
  catch {th8testlib::getpublickeytoken root}
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test embedkey-2.2 {
  th8_policy.c L2190 Th8_GetPublicKeyRoot (F,T) -- nData==0
  via drivekeyfault root 2.  Mirrors embedkey-1.2 for keyRoot.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set rc [catch {th8testlib::drivekeyfault root 2} msg]
  list $rc [string match "*cannot load embedded keyRoot*" $msg]
} -cleanup {
  th8testlib::resetkeycaches
  catch {th8testlib::getpublickeytoken root}
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test embedkey-3.1 {
  th8_policy.c L2295 Th8_GetPublicKeyTest (T,-) -- zData==NULL
  via drivekeyfault test 1.  Mirrors embedkey-1.1 for the
  test key.  Only runs when TH8_ENABLE_TEST_KEY is compiled
  in (the test_key constraint).
} -constraints {
    th8 crypto_enabled test_key
} -setup {
} -body {
  set rc [catch {th8testlib::drivekeyfault test 1} msg]
  list $rc [string match "*cannot load embedded test key*" $msg]
} -cleanup {
  th8testlib::resetkeycaches
  catch {th8testlib::getpublickeytoken test}
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

runTest {test embedkey-3.2 {
  th8_policy.c L2295 Th8_GetPublicKeyTest (F,T) -- nData==0
  via drivekeyfault test 2.  Mirrors embedkey-1.2 for the
  test key.
} -constraints {
    th8 crypto_enabled test_key
} -setup {
} -body {
  set rc [catch {th8testlib::drivekeyfault test 2} msg]
  list $rc [string match "*cannot load embedded test key*" $msg]
} -cleanup {
  th8testlib::resetkeycaches
  catch {th8testlib::getpublickeytoken test}
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
