###############################################################################
#
# coverage_hash_order.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# TH8K-016: the insertion-order stamp that ordered hash iteration (dict for /
# dict keys / dict values) sorts by is drawn from a per-hash MONOTONIC LIFETIME
# counter -- it advances on every insert and is never rolled back on delete, so
# a long-lived churning dict (e.g. a work queue) could reach the counter's
# ceiling over time.  The counter is 64-bit, so that ceiling is unreachable in
# any real run; the guard that keeps the stamp DEFINED at the ceiling (and the
# documented tie semantics there) is therefore exercised deliberately, via
# ::th8testlib::hash_order_saturation, which primes a throwaway hash's counter to
# one below the 64-bit ceiling and inserts across the boundary.  It confirms the
# last pre-ceiling entry still orders before the saturated ones, that entries at
# the ceiling tie, and that the counter does not advance past it (no signed
# overflow).  This makes the boundary semantics explicit and covers the guard's
# saturating arm.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test coverage_hash_order-1.1 {
  R-44992-17786: insertion-order stamp is DEFINED at the 64-bit counter ceiling
  -- the last distinct entry still orders before entries inserted at the
  ceiling, those entries tie, and the lifetime counter saturates rather than
  overflowing (TH8K-016).
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::hash_order_saturation]
} -cleanup {
  unset -nocomplain r
} -result {ok}}

###############################################################################

source tests/epilogue.tcl
