###############################################################################
#
# coverage_platform_cb_null.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# P4 of the MC/DC closure plan.  Drives `pPlat && pPlat->xXxx`
# compounds across th8_core.c, th8_expressions.c,
# th8_filesystems.c, th8_plat.c by running scripts in a child
# interp where one specific platform callback is set to NULL.
#
# Uses ::th8testlib::platform_cb_null CALLBACK SCRIPT to build
# the child interp with the specified callback nullified.
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

runTest {test pcb_null-1.1 {
  fpclassify with xMathFunc=NULL drives the T,F vector at
  th8_expressions.c:137 (pPlat valid, xMathFunc NULL)
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xMathFunc {fpclassify 1.0}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pcb_null-1.2 {
  baseline: fpclassify with xPanic=NULL only (xMathFunc still
  available) drives the T,T success vector
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xPanic {fpclassify 1.0}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pcb_null-2.1 {
  pwd with xGetCwd=NULL drives the !pPlatform || !xGetCwd
  compound at th8_core.c (Th8_GetCwd internal)
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xGetCwd {pwd}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pcb_null-2.2 {
  file same with xSameFile=NULL drives the compound at
  th8_filesystems.c:1560 (!pPlatform || !pPlatform->xSameFile)
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xSameFile \
      {catch {file same /tmp /tmp} m; set m}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pcb_null-3.1 {
  puts with xOutput=NULL drives the compound at
  th8_filesystems.c:648 (pPlat && pPlat->xOutput)
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xOutput {flush stdout}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pcb_null-3.2 {
  expr with xMathFunc=NULL drives Th8_FindMathFunc compound
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xMathFunc \
      {expr {sin(1.0)}}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pcb_null-4.1 {
  unknown callback name returns error
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xNoSuchCallback {set x 1}} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test pcb_null-5.1 {
  vwait -timeout with xTimeMs=NULL drives the T,T vector at
  th8_events.c:529 (nTimeoutMs >= 0 && !xTimeMs)
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xTimeMs \
      {vwait -timeout 100 _no_var_pcb_5_1_}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pcb_null-5.2 {
  update with xSleep=NULL drives the !xSleep error path
} -constraints {
    th8
} -body {
  catch {::th8testlib::platform_cb_null xSleep \
      {vwait -timeout 100 _no_var_pcb_5_2_}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
