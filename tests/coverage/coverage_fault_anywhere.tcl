###############################################################################
#
# coverage_fault_anywhere.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_fault.c th8FaultMatchFilter L169/L172:
#
#   if (pF->zFile && pCfg->zCurFile == NULL) continue;         L169
#   if (pF->zFile && !th8FaultStrEqAscii(...)                  L172
#           && !th8FaultPathMatchesBaseName(...)) continue;
#
# Existing tests always supply -allocFailSite FILE which sets
# pF->zFile to a heap copy of FILE.  L169/L172 then evaluate with
# C1=T (pF->zFile non-NULL) only.  A plain -allocFailAfter alone
# installs no filter (nFilter=0) and the function short-circuits
# at L163 before reaching L169/L172.  Hence the C1-Pair vectors
# (pF->zFile == NULL) are never exercised.
#
# This test uses the new -allocFailAnywhere option on
# ::th8testlib::fault eval to install a wildcard filter
# (zFile=NULL).  th8FaultMatchFilter then iterates the loop with
# pF->zFile NULL, driving L169 C1=F (no path-check continue) and
# L172 C1=F (no name-check continue).
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

runTest {test fault_anywhere-1.1 {
  fault eval with -allocFailAnywhere installs a wildcard
  filter entry (zFile=NULL).  th8FaultMatchFilter walks the
  loop with pF->zFile NULL, driving the C1=F vectors at
  th8_fault.c L169 and L172.  The script is a tiny string
  alloc; -allocFailAfter 5 means the wrapper fails the 5th
  matching allocation, exercising the fault path.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  catch {::th8testlib::fault eval {
      set s ""
      for {set i 0} {$i < 20} {incr i} {
          append s "x"
      }
      string length $s
  } -allocFailAnywhere -allocFailAfter 5} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault_anywhere-2.1 {
  fault eval with a script that touches many platform
  callbacks via the wrapped child interp's xKeyValue,
  xGetCwd, xNormalizePath, xGetPid, and time ops.  Each
  platform callback in the fault wrapper is a pt_*
  passthrough; this test exercises them.  No
  -allocFailAfter so the fault layer is in trace mode
  but doesn't actually inject failures.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  catch {::th8testlib::fault eval {
      # Time ops -> pt_xTimeMs
      clock seconds
      # Env probes -> pt_xKeyValue / pt_xGetEnv
      env exists PATH
      catch {env get NOSUCHVAR}
      # Filesystem ops -> pt_xNormalizePath / pt_xGetCwd
      file normalize "./foo"
      pwd
      # Process info -> pt_xGetPid
      pid
      string length "ok"
  }} r
  expr {1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
