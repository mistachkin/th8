###############################################################################
#
# coverage_policy_eval_pre_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L1410
# in `th8PolicyEvalPre`:
#
#   if (!zName || nName == 0) { ... return TH8_ERROR; }
#
# Mirror of L1100 in th8PolicyVerifyData (closed in batch
# #20).  Pre-test report: 0% MC/DC -- only (F,F) observed
# because normal Th8_EvalFile callers always supply a
# non-empty file path.  The new
# ::th8testlib::policyevalpre helper invokes Th8_Eval
# directly with NULL or empty zName on a signed-only
# child interp, routing through th8PolicyCallback to
# th8PolicyEvalPre.
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

runTest {test policyevalpre-1.1 {
  th8_policy.c L1410 (T,-) -- Th8_Eval with zName=NULL
  on signed-only child triggers C1=T short-circuit.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyevalpre null]
  list [lindex $r 0] [string match "*no origin name*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test policyevalpre-1.2 {
  th8_policy.c L1410 (F,T) -- Th8_Eval with zName non-NULL
  but nName=0 triggers C2=T.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyevalpre empty]
  list [lindex $r 0] [string match "*no origin name*" [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test policyevalpre-1.3 {
  th8_policy.c L1410 (F,F) baseline -- Th8_Eval with a
  valid name falls through L1410, hits L1430
  th8PolicyVerifyData which errors on the missing
  signature file (no .b64sig for "y").  Required for
  the C1-Pair and C2-Pair MC/DC independence proofs.
} -constraints {
    th8 crypto_enabled
} -setup {
} -body {
  set r [th8testlib::policyevalpre named]
  lindex $r 0
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
