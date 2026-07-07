###############################################################################
#
# coverage_info_level_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for src/th8_core.c::Th8_GetFrameObjv
# bad-level error compounds.  The function backs [info level
# N], [info frame N], and the [uplevel] level decoder.
# Existing conformance tests in tests/info.tcl cover the
# success path (info level 0 / inside-proc levels) which only
# drives the (F,F) vector of the L16627 compound `nTarget <=
# 0 || nTarget > nCurrent`.  Negative-by-far and positive-by-
# far arguments close the remaining (T,-) and (F,T) pairs.
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

runTest {test info_level_mcdc-1.1 {
  [info level -999] at global scope drives the L16627 C1=T
  vector: nCurrent=0, iLevel=-999, nTarget = 0 + (-999) =
  -999 which is <= 0.  Expected: TH8_ERROR "bad level".
} -constraints {
    th8
} -body {
  set rc [catch {info level -999} m]
  list $rc $m
} -cleanup {
  unset -nocomplain rc m
} -result {1 {bad level}}}

###############################################################################

runTest {test info_level_mcdc-1.2 {
  [info level 999] from inside a 2-deep proc stack drives
  the L16627 (F, T) vector: nTarget=999, nCurrent=2, C1=F
  (999 > 0), C2=T (999 > 2).  Expected: TH8_ERROR
  "bad level".
} -constraints {
    th8
} -body {
  proc fnInner {} {
    return [catch {info level 999} m]
  }
  proc fnOuter {} {
    return [fnInner]
  }
  fnOuter
} -cleanup {
  catch {rename fnInner ""}
  catch {rename fnOuter ""}
} -result {1}}

###############################################################################

source tests/epilogue.tcl
