###############################################################################
#
# coverage_loop_polls.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# TH8K-009 loop-audit: verify that the cancellation / step-limit polls
# added to attacker-controlled command loops actually fire.  Each test
# runs a valid command whose loop iterates over its (literal) input
# inside ::th8testlib::sandbox twice: once with a LOW explicit step
# limit and once with the default (large) limit.  ::th8testlib::sandbox
# returns {returnCode result stepCount allocBytes}.  A per-iteration
# Th8_Ready poll makes the command trip the low limit (returnCode 1)
# while the same command completes normally under the default limit
# (returnCode 0).  Building the input as a brace/literal costs ~no
# steps, so the trip happens inside the command's own loop.
#
# This also exercises the loop's error-cleanup path (free the partial
# output on a poll trip); the suite's per-file leak check validates it.
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

# A 40-element literal list / dict-ish payload, cheap to parse.
set ::LP_LIST {a b c d e f g h i j k l m n o p q r s t\
               u v w x y z a b c d e f g h i j k l m n}

###############################################################################

proc lpTrips {script} {
  # Returns "1 0": low-limit run trips (rc 1), default-limit run ok (rc 0).
  set lo [::th8testlib::sandbox $script 6]
  set hi [::th8testlib::sandbox $script]
  return [list [lindex $lo 0] [lindex $hi 0]]
}

###############################################################################

runTest {test looppoll-1.1 {
  lreverse in-loop poll fires under a low step limit (TH8K-009)
} -constraints {th8} -body {
  lpTrips "lreverse {$::LP_LIST}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.2 {
  join in-loop poll fires
} -constraints {th8} -body {
  lpTrips "join {$::LP_LIST} -"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.3 {
  lrange in-loop poll fires
} -constraints {th8} -body {
  lpTrips "lrange {$::LP_LIST} 0 end"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.4 {
  dict keys in-loop poll fires
} -constraints {th8} -body {
  lpTrips "dict keys {a 1 b 2 c 3 d 4 e 5 f 6 g 7 h 8 i 9 j 10 k 11 l 12}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.5 {
  dict values in-loop poll fires
} -constraints {th8} -body {
  lpTrips "dict values {a 1 b 2 c 3 d 4 e 5 f 6 g 7 h 8 i 9 j 10 k 11 l 12}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.6 {
  dict merge in-loop poll fires
} -constraints {th8} -body {
  lpTrips "dict merge {a 1 b 2 c 3 d 4 e 5 f 6} {g 7 h 8 i 9 j 10 k 11 l 12}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.7 {
  string totitle in-loop poll fires
} -constraints {th8} -body {
  lpTrips "string totitle abcdefghijklmnopqrstuvwxyzabcdefghij"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.8 {
  string reverse in-loop poll fires
} -constraints {th8} -body {
  lpTrips "string reverse abcdefghijklmnopqrstuvwxyzabcdefghij"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.9 {
  binary format list-pack in-loop poll fires
} -constraints {th8} -body {
  lpTrips "binary format c* {1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.10 {
  array set in-loop poll fires
} -constraints {th8} -body {
  lpTrips "array set _a {a 1 b 2 c 3 d 4 e 5 f 6 g 7 h 8 i 9 j 10 k 11 l 12}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.11 {
  lsort merge sort is interruptible (TH8K-009): the in-sort poll fires under a
  low step limit, so an adversarial sort cannot run unbounded.
} -constraints {th8} -body {
  lpTrips "lsort {$::LP_LIST}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.12 {
  split (character mode) in-loop poll fires, and cancellation is propagated as
  an error rather than masked by returning the partial list (TH8K-009).
} -constraints {th8} -body {
  lpTrips "split {$::LP_LIST} {}"
} -result {1 0}}

###############################################################################

runTest {test looppoll-1.13 {
  dict for is interruptible via its per-iteration body poll (TH8K-009).  NOTE:
  [dict for] iterates a th8DictSplit'd list, NOT Th8_HashIterateOrdered -- TH8
  dicts are list-backed and no production command drives the ordered-hash walk,
  so this case exercises the loop-body Th8_Ready checkpoint, not the ordered-hash
  merge sort.  (Th8_HashIterateOrdered's own pollable merge sort is a public-API
  mechanism with no script-reachable caller; it is driven directly by the
  testlib ordered-hash coverage driver, not from script.)
} -constraints {th8} -body {
  lpTrips "dict for {k v} {a 1 b 2 c 3 d 4 e 5 f 6 g 7 h 8 i 9 j 10 k 11 l 12}\
      {}"
} -result {1 0}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
