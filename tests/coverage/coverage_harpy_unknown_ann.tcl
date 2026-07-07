###############################################################################
#
# coverage_harpy_unknown_ann.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_policy.c L580 and
# L598 -- the annotation-prefix length+memcmp guards for
# `notAfter:` and `flags:`:
#
#   } else if (nContent > 9
#             && Th8_Memcmp(interp, zContent, "notAfter:", 9) == 0) {
#   ...
#   } else if (nContent > 6
#             && Th8_Memcmp(interp, zContent, "flags:", 6) == 0) {
#
# Existing tests provide `<<notBefore:...>>`, `<<notAfter:...>>`,
# and `<<flags:...>>` annotations -- all of which match a known
# prefix at C2=T.  The C2=F vector (length OK but prefix wrong)
# and the C1=F vector (content too short) have no coverage:
# the test corpus never feeds unrecognized annotations because
# they have no observable effect on script execution.
#
# Two helpers here inject unrecognized annotations:
#   ann_short_unknown -- `<<x>>` content is 1 char so
#       nContent>9 / nContent>6 are F at both L580/L598
#       (closes the C1=F vector at both).
#   ann_long_unknown  -- 47-char content not matching any
#       known prefix so nContent is long enough but the
#       memcmp comparisons fire and miss
#       (closes the C2=F vector at both).
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

runTest {test harpy_uann-1.1 {
  Short unknown annotation `<<x>>` drives th8_policy.c L580
  C1=F (nContent <= 9) and L598 C1=F (nContent <= 6).  The
  prefix length-check short-circuits each branch; the
  annotation is silently ignored.
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  source tests/helpers/ann_short_unknown.tcl
  expr {[string length $result] > 0}
} -cleanup {
  unset -nocomplain r result
} -result {1}}

###############################################################################

runTest {test harpy_uann-1.2 {
  Long unknown annotation drives th8_policy.c L580 C2=F
  (nContent>9 T but memcmp != "notAfter:") and L598 C2=F
  (nContent>6 T but memcmp != "flags:").  The prefix
  comparisons run but neither matches; the annotation is
  silently ignored.
} -constraints {
    th8 harpy_sign crypto_enabled
} -setup {
} -body {
  source tests/helpers/ann_long_unknown.tcl
  expr {[string length $result] > 0}
} -cleanup {
  unset -nocomplain r result
} -result {1}}

###############################################################################

source tests/epilogue.tcl
