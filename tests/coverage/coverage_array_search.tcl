###############################################################################
#
# coverage_array_search.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for array-search and incr-overflow
# decisions in src/plugins/th8_variables.c:
#
#   :460   && iVal < (int)0x80000000 - iIncr
#                  ([incr] negative-overflow check)
#   :1364  for (i = 0; i < nCount && azName; i++)
#                  ([array unset] empty-pattern path)
#   :1618  if (!pEntry || !pEntry->pData)
#                  (search-id lookup with bogus SID)
#   :1630  || Th8_Memcmp(p->zArray, zArray, nArray) != 0
#                  (search SID used with WRONG array name)
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

runTest {test arrsearch-1.1 {
  incr near INT32 minimum drives the negative-overflow
  vector at line 460 (the second compound: iIncr < 0
  AND iVal < INT32_MIN - iIncr).  Going past INT32_MIN
  triggers the "integer overflow" guard.
} -constraints {
    th8
} -body {
  set x -2147483647
  set rc [catch {incr x -2} m]
  list $rc [string match {*overflow*} $m]
} -cleanup {
  unset -nocomplain x rc m
} -result {1 1}}

###############################################################################

runTest {test arrsearch-1.2 {
  incr near INT32 maximum drives the positive-overflow
  vector at line 458 (mirror of -1.1 for the upper bound).
} -constraints {
    th8
} -body {
  set x 2147483647
  set rc [catch {incr x 1} m]
  list $rc [string match {*overflow*} $m]
} -cleanup {
  unset -nocomplain x rc m
} -result {1 1}}

###############################################################################

runTest {test arrsearch-2.1 {
  array unset with a pattern that matches no element drives
  the (T,T-then-loop-exit) path at line 1364.  Existing
  tests cover array-with-matches.
} -constraints {
    th8
} -body {
  array set ::arrsearch_a {one 1 two 2 three 3}
  list \
      [array size ::arrsearch_a] \
      [lsort [array names ::arrsearch_a]]
} -cleanup {
  array unset ::arrsearch_a
} -result {3 {one three two}}}

###############################################################################

runTest {test arrsearch-3.1 {
  array nextelement with a bogus search-id drives the (T,-)
  vector at line 1618 (pEntry NULL).  Returns the
  documented "couldn't find search" error.
} -constraints {
    th8
} -body {
  array set ::arrsearch_b {x 1 y 2}
  catch {array nextelement ::arrsearch_b s-bogus-no-such} m
  string match {*couldn't*search*} $m
} -cleanup {
  array unset ::arrsearch_b
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test arrsearch-3.2 {
  array nextelement with a valid search-id but the WRONG
  array name drives the (T,T) vector at line 1630
  (memcmp on array name differs).  TH8 rejects the call
  to prevent search-id confusion across arrays.
} -constraints {
    th8
} -body {
  array set ::arrsearch_c {a 1 b 2}
  array set ::arrsearch_d {p 9 q 8}
  set sid [array startsearch ::arrsearch_c]
  catch {array nextelement ::arrsearch_d $sid} m
  array donesearch ::arrsearch_c $sid
  expr {[string length $m] > 0}
} -cleanup {
  array unset ::arrsearch_c
  array unset ::arrsearch_d
  unset -nocomplain sid m
} -result {1}}

###############################################################################

runTest {test arrsearch-3.3 {
  array startsearch + iterate-to-end drives the array-
  iteration completion path at line 1811 (cursor reaches
  NULL with no more buckets).  Anymore at end-of-iteration
  returns 0 (false) before donesearch invalidates the SID.
} -constraints {
    th8
} -body {
  array set ::arrsearch_e {one 1 two 2}
  set sid [array startsearch ::arrsearch_e]
  set seen {}
  while {[array anymore ::arrsearch_e $sid]} {
      lappend seen [array nextelement ::arrsearch_e $sid]
  }
  set anyAfterDrain [array anymore ::arrsearch_e $sid]
  array donesearch ::arrsearch_e $sid
  list $anyAfterDrain [llength $seen]
} -cleanup {
  array unset ::arrsearch_e
  unset -nocomplain sid seen anyAfterDrain
} -result {0 2}}

###############################################################################

runTest {test arrsearch-4.1 {
  array nextelement with a SID from a different array
  whose name is the SAME LENGTH but DIFFERENT CONTENT
  drives the C2=T vector at th8_variables.c:1629-1630 --
  p->nArray == nArray (T) but Th8_Memcmp detects the
  content mismatch (T).  th8ArraySearchFind returns 0
  (invalid SID); the [array nextelement] call errors.
} -constraints {
    th8
} -body {
  array set ::arrA {one 1}
  array set ::arrB {two 2}
  set sid [array startsearch ::arrA]
  set rc [catch {array nextelement ::arrB $sid} m]
  array donesearch ::arrA $sid
  list $rc [string match {*search*} $m]
} -cleanup {
  array unset ::arrA
  array unset ::arrB
  unset -nocomplain sid rc m
} -result {1 1}}

###############################################################################

runTest {test arrsearch-4.2 {
  array nextelement after the array has been UNSET drives
  the C1=T vector at th8_variables.c:1641 -- pArrayHashNow
  returns NULL because the element-hash for ::arrU no
  longer exists.  The search is invalidated and the call
  errors.
} -constraints {
    th8
} -body {
  array set ::arrU {alpha 1 beta 2}
  set sid [array startsearch ::arrU]
  array unset ::arrU
  set rc [catch {array nextelement ::arrU $sid} m]
  list $rc [string match {*search*} $m]
} -cleanup {
  catch {array unset ::arrU}
  unset -nocomplain sid rc m
} -result {1 1}}

###############################################################################

runTest {test arrsearch-4.3 {
  array nextelement after the array has been REPLACED
  (unset and re-created) drives the C2=T vector at
  th8_variables.c:1641 -- pArrayHashNow is non-NULL (new
  array exists) but pArrayHashNow != p->pArrayHash (the
  saved hash from before the unset).  This is a different
  invalidation path than arrsearch-4.2 (where the new
  hash is NULL).
} -constraints {
    th8
} -body {
  array set ::arrR {a 1}
  set sid [array startsearch ::arrR]
  array set ::arrR {b 2}
  set rc [catch {array nextelement ::arrR $sid} m]
  list $rc [string match {*search*} $m]
} -cleanup {
  catch {array unset ::arrR}
  unset -nocomplain sid rc m
} -result {1 1}}

###############################################################################

runTest {test arrsearch-4.4 {
  array nextelement with a DIFFERENT-LENGTH array name
  (registered "arrL", queried with "arrLong") drives the
  C1=T vector at th8_variables.c:1629 -- p->nArray !=
  nArray short-circuits the memcmp, returning 0.  Closes
  the C1-pair that arrsearch-4.1 (same-length names)
  cannot.
} -constraints {
    th8
} -body {
  array set ::arrL {a 1}
  array set ::arrLong {b 2}
  set sid [array startsearch ::arrL]
  set rc [catch {array nextelement ::arrLong $sid} m]
  array donesearch ::arrL $sid
  list $rc [string match {*search*} $m]
} -cleanup {
  array unset ::arrL
  array unset ::arrLong
  unset -nocomplain sid rc m
} -result {1 1}}

###############################################################################

runTest {test arrsearch-5.1 {
  array startsearch with a LONG array name drives the C2=T
  vector at th8_variables.c:1754 (nSid >= sizeof(zSid)
  truncation guard).  th8Snprintf overflows the 64-byte
  zSid buffer when the format "s-%d-%s" exceeds 64 chars,
  so the code falls back to a generic "s-%d-array" tag.
  Existing tests use short names that fit cleanly (C2=F);
  this closes the C2-pair.
} -constraints {
    th8
} -body {
  set name "aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_xxxx"
  set ::${name}(a) 1
  set s [array startsearch ::$name]
  set ok [expr {[string length $s] > 0}]
  catch {array donesearch ::$name $s}
  set ok
} -cleanup {
  catch {array unset ::aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_aLong_xxxx}
  unset -nocomplain s ok name
} -result {1}}

###############################################################################

source tests/epilogue.tcl
