###############################################################################
#
# coverage_info_procs_mixed.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c L18762-18763 inside the hash
# iteration callback used by Th8_ListAppendCommandsMatching:
#
#   if (pCmd && (pCmd->xProc == xMatch1
#         || (xMatch2 && pCmd->xProc == xMatch2))) {
#
# The compound has four conditions:
#   C1: pCmd != NULL
#   C2: pCmd->xProc == xMatch1  (the [proc] dispatch fn)
#   C3: xMatch2 != NULL          (the [nproc] dispatch fn)
#   C4: pCmd->xProc == xMatch2
#
# This callback is invoked by Th8_ListAppendCommandsMatching, which
# is called from info_procs_command at L810/L838 -- the
# UNQUALIFIED-pattern branch.  When the pattern resolves to a
# specific namespace (e.g. `info procs ::ns::*`), the qualified
# branch at L796 uses a different callback (th8InfoCmdCallback)
# and bypasses L18762 entirely.
#
# To drive (T,F,T,T) -- entry's xProc matches xMatch2 (the nproc
# dispatch) -- the test creates a global [proc] and a global
# [nproc], then enumerates via the unqualified `info procs
# glob_ipmixed_*` pattern.  The iterator visits both global
# entries plus every other global command; the nproc entry
# closes the previously-uncovered C4-pair.
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

runTest {test ipmixed-1.1 {
  Unqualified [info procs] pattern + a global nproc command
  drives the C4-pair at src/th8_core.c L18763.  Existing
  global-procs are dispatched by th8ProcCall1 (xMatch1, hits
  C2=T and short-circuits), so the C4=T variant requires an
  entry whose xProc is th8NprocCall1 (xMatch2).
} -constraints {
    th8
} -setup {
} -body {
  proc ::glob_ipmixed_p {} {return p}
  nproc ::glob_ipmixed_n {x} {return n}
  set r [lsort [info procs glob_ipmixed_*]]
  set r
} -cleanup {
  catch {rename ::glob_ipmixed_p {}}
  catch {rename ::glob_ipmixed_n {}}
  unset -nocomplain r
} -result {glob_ipmixed_n glob_ipmixed_p}}

###############################################################################

source tests/epilogue.tcl
