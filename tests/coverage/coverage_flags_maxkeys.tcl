###############################################################################
#
# coverage_flags_maxkeys.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/harpy/th8_attrflags.c L395
# inside th8AfMapGet:
#
#   if (bCreate && p->n < AF_MAX_KEYS) { ... }
#
# AF_MAX_KEYS == TH8_AF_MAX_KEYS == 16.  Existing tests never
# exceed 4-5 keys per complex flag dict so the second-condition
# C2-pair (`p->n < AF_MAX_KEYS`) is uncovered: only (T,T) was
# hit, never (T,F).  bCreate=0 is intrinsic dead because the
# only call sites (L617, L1104) always pass bCreate=1.
#
# This test passes a 17-key complex flag dict so the parser's
# 17th th8AfMapGet call hits L395 with p->n == AF_MAX_KEYS
# (already full), driving (T,F).  Th8AfMapGet returns NULL,
# the upstream code at L620 errors with "flags: too many
# keys", closing the C2-pair.
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

runTest {test fl_maxkeys-1.1 {
  17-key complex flag dict exceeds AF_MAX_KEYS (16), driving
  th8_attrflags.c L395 (T,F) -- th8AfMapGet hits the bounded
  array, returns NULL, parser errors with "too many keys".
} -constraints {
    th8
} -setup {
} -body {
  set spec ""
  foreach k {1 2 3 4 5 6 7 8 9 a b c d e f 10 11} {
    append spec "\{$k:x\}"
  }
  catch {flags have -complex $spec "x"} r
  expr {[string match "*too many*" $r]}
} -cleanup {
  unset -nocomplain spec r k
} -result {1}}

###############################################################################

source tests/epilogue.tcl
