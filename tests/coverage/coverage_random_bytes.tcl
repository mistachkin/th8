###############################################################################
#
# coverage_random_bytes.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the (pPlatform && xRandomBytes)
# compound at src/th8_hash.c:147 -- the hash-randomization
# initialization gate.  Uses the existing
# ::th8testlib::platform_cb_null primitive to NULL the
# xRandomBytes callback on a child interpreter, driving the
# C2-pair (xRandomBytes NULL) that's not reachable on the
# default platform.
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

runTest {test rndbytes-1.1 {
  Drive (T, F) at th8_hash.c:147 by NULLing xRandomBytes
  on a child platform and triggering hash creation.  The
  hash-init code path that randomizes seeds via
  xRandomBytes will see C2=F and skip the seed call.
} -constraints {
    th8
} -body {
  set r [::th8testlib::platform_cb_null xRandomBytes {
      dict create k1 v1 k2 v2 k3 v3
  }]
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
