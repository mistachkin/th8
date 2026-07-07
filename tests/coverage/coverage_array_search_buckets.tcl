###############################################################################
#
# coverage_array_search_buckets.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/th8_variables.c L1834 inside
# array_nextelement_command:
#
#   if (!p->pCursor && p->pArrayHash) {
#       th8ArraySearchSkipEmpty(...);
#   }
#
# The (T,T) vector fires when the current hash-bucket chain is
# exhausted -- pCur->pNext is NULL -- and the iterator must
# advance to the next non-empty bucket.  Existing arrayops-5.1
# uses only 3 elements which may collide into a single bucket,
# leaving the multi-bucket advance untested.
#
# This test inserts enough distinct keys (50) into an array that
# the hash distributes them across multiple buckets, forcing the
# enumeration to cross bucket boundaries at least once.
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

runTest {test arr_buckets-1.1 {
  array startsearch + repeated nextelement on a 50-element
  array drives th8_variables.c L1834 (T,T) -- pCursor exhausted
  in a non-final bucket, advance via th8ArraySearchSkipEmpty.
} -constraints {
    th8
} -setup {
} -body {
  for {set i 0} {$i < 50} {incr i} {
    set a(k$i) $i
  }
  set sid [array startsearch a]
  set names {}
  while {[array anymore a $sid]} {
    lappend names [array nextelement a $sid]
  }
  array donesearch a $sid
  list \
      [llength $names] \
      [expr {[llength [lsort -unique $names]] == 50}]
} -cleanup {
  unset -nocomplain a sid names i
} -result {50 1}}

###############################################################################

source tests/epilogue.tcl
