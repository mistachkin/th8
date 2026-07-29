###############################################################################
#
# arrayops.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- array set
#
###############################################################################

runTest {test arrayops-1.1 {
  R-20793-16099: array set bulk-assigns from name-value list
} -setup {
} -body {
  array set arr {x 10 y 20 z 30}
  list $arr(x) $arr(y) $arr(z)
} -cleanup {
  unset -nocomplain arr
} -result {10 20 30}}

###############################################################################

runTest {test arrayops-1.2 {
  R-60323-35069: array set odd list is error
} -setup {
} -body {
  list [catch {array set arr {a 1 b}} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain arr
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test arrayops-1.3 {
  R-28939-42058: array set empty list creates empty array
} -setup {
} -body {
  array set arr {}
  array exists arr
} -cleanup {
  unset -nocomplain arr
} -result {1}}

###############################################################################

runTest {test arrayops-1.4 {
  R-20793-16099: array set overwrites existing elements
} -setup {
} -body {
  array set arr {k1 old}
  array set arr {k1 new k2 val2}
  list $arr(k1) $arr(k2)
} -cleanup {
  unset -nocomplain arr
} -result {new val2}}

###############################################################################
#
# Section 2 -- array get
#
###############################################################################

runTest {test arrayops-2.1 {
  R-65179-47163: array get returns name-value pairs
} -setup {
} -body {
  set arr(a) 1
  set arr(b) 2
  set result [array get arr]
  expr {[llength $result] == 4}
} -cleanup {
  unset -nocomplain arr
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test arrayops-2.2 {
  R-09466-52892: array get with pattern filters elements
} -setup {
} -body {
  set arr(foo) 1
  set arr(bar) 2
  set arr(foz) 3
  array get arr "f*"
} -cleanup {
  unset -nocomplain arr
} -match glob -result {*fo*}}

###############################################################################

runTest {test arrayops-2.3 {
  R-65179-47163: array get on nonexistent returns empty
} -setup {
} -body {
  array get nosucharray
} -result {}}

###############################################################################
#
# Section 3 -- array size
#
###############################################################################

runTest {test arrayops-3.1 {
  R-12247-30649: array size returns element count
} -setup {
} -body {
  set arr(a) 1
  set arr(b) 2
  set arr(c) 3
  array size arr
} -cleanup {
  unset -nocomplain arr
} -result {3}}

###############################################################################

runTest {test arrayops-3.2 {
  R-27362-34391: array size returns 0 for non-array
} -setup {
} -body {
  array size notarr
} -result {0}}

###############################################################################

runTest {test arrayops-3.3 {
  R-12247-30649: array size after adding elements
} -setup {
} -body {
  array set arr {a 1 b 2}
  set before [array size arr]
  set arr(c) 3
  set after [array size arr]
  list $before $after
} -cleanup {
  unset -nocomplain arr
  unset -nocomplain before after
} -result {2 3}}

###############################################################################
#
# Section 4 -- array statistics
#
###############################################################################

runTest {test arrayops-4.1 {
  R-07244-26691: array statistics on a populated array reports the entry count on the first line
} -setup {
} -body {
  array set arr {apple red banana yellow cherry red}
  lindex [split [array statistics arr] "\n"] 0
} -cleanup {
  unset -nocomplain arr
} -match glob -result {3 entries*}}

###############################################################################

runTest {test arrayops-4.2 {
  R-07244-26691: array statistics on an empty array reports zero entries and zero bytes
} -constraints {th8} -setup {
} -body {
  array set arr {}
  set lines [split [array statistics arr] "\n"]
  list \
      [lindex $lines 0] \
      [lindex $lines 1] \
      [lindex $lines 2]
} -cleanup {
  unset -nocomplain arr lines
} -result {{0 entries} {total element name bytes: 0} {total element value bytes: 0}}}

###############################################################################

runTest {test arrayops-4.3 {
  R-07244-26691: array statistics correctly counts element-name and element-value byte totals
} -constraints {th8} -setup {
} -body {
  array set arr {a 11 bb 222 ccc 3333}
  set lines [split [array statistics arr] "\n"]
  # name bytes: 1+2+3 = 6; value bytes: 2+3+4 = 9
  list \
      [lindex $lines 1] \
      [lindex $lines 2]
} -cleanup {
  unset -nocomplain arr lines
} -result {{total element name bytes: 6} {total element value bytes: 9}}}

###############################################################################

runTest {test arrayops-4.4 {
  R-00505-09565: array statistics on a nonexistent array raises a script error
} -setup {
} -body {
  list [catch {array statistics arr} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {"arr" isn't an array}}}

###############################################################################

runTest {test arrayops-4.5 {
  R-45874-35574: array statistics with too few or too many arguments raises wrong-# args
} -setup {
} -body {
  list \
      [catch {array statistics} msg] [string match {*wrong # args*} $msg] \
      [catch {array statistics a b} msg] [string match {*wrong # args*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1 1 1}}

###############################################################################
#
# Section 5 -- array search-iteration API
# (startsearch, nextelement, anymore, donesearch)
#
###############################################################################

runTest {test arrayops-5.1 {
  R-31734-18707: array startsearch returns a non-empty search id; full enumerate yields all element names exactly once
} -setup {
} -body {
  array set a {x 1 y 2 z 3}
  set sid [array startsearch a]
  set names {}
  while {[array anymore a $sid]} {
    lappend names [array nextelement a $sid]
  }
  array donesearch a $sid
  list \
      [expr {[string length $sid] > 0}] \
      [lsort $names]
} -cleanup {
  unset -nocomplain a sid names
} -result {1 {x y z}}}

###############################################################################

runTest {test arrayops-5.2 {
  R-38073-02154: array anymore returns 0 once the search is exhausted; nextelement returns the empty string at exhaustion
} -setup {
} -body {
  array set a {only one}
  set sid [array startsearch a]
  set first [array nextelement a $sid]
  set anymore [array anymore a $sid]
  set next [array nextelement a $sid]
  array donesearch a $sid
  list $first $anymore $next
} -cleanup {
  unset -nocomplain a sid first anymore next
} -result {only 0 {}}}

###############################################################################

runTest {test arrayops-5.3 {
  R-04772-16495: array startsearch on an empty array yields a usable search id with anymore = 0
} -setup {
} -body {
  array set a {}
  set sid [array startsearch a]
  set anymore [array anymore a $sid]
  array donesearch a $sid
  list [expr {[string length $sid] > 0}] $anymore
} -cleanup {
  unset -nocomplain a sid anymore
} -result {1 0}}

###############################################################################

runTest {test arrayops-5.4 {
  R-11358-43827: array startsearch on a nonexistent array raises a script error
} -setup {
} -body {
  list [catch {array startsearch nope} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {"nope" isn't an array}}}

###############################################################################

runTest {test arrayops-5.5 {
  R-40419-26190: array donesearch invalidates the search id; subsequent nextelement / anymore / donesearch on the same id raise "couldn't find search"
} -setup {
} -body {
  array set a {only one}
  set sid [array startsearch a]
  array donesearch a $sid
  list \
      [catch {array nextelement a $sid} msg] [string match {*couldn't find search*} $msg] \
      [catch {array anymore     a $sid} msg] [string match {*couldn't find search*} $msg] \
      [catch {array donesearch  a $sid} msg] [string match {*couldn't find search*} $msg]
} -cleanup {
  unset -nocomplain a sid msg
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test arrayops-5.6 {
  R-55410-62072: array unset during iteration invalidates the search; subsequent calls raise "couldn't find search"
} -constraints {th8} -body {
  array set a {p 10 q 20}
  set sid [array startsearch a]
  array unset a
  list [catch {array nextelement a $sid} msg] [string match {*couldn't find search*} $msg]
} -cleanup {
  unset -nocomplain a sid msg
} -result {1 1}}

###############################################################################

runTest {test arrayops-5.6a {
  R-55410-62072: unset and same-name re-create after
  startsearch still invalidates the search; no SID survives
  the allocator handing the same hash address back for the
  new array.
} -setup {
} -body {
  array set a {p 10 q 20}
  set sid [array startsearch a]
  array set a {x 99}
  set rc [catch {array nextelement a $sid} msg]
  list $rc [string match {*couldn't find search*} $msg]
} -cleanup {
  unset -nocomplain a sid msg rc
} -result {1 1}}

###############################################################################

runTest {test arrayops-5.7 {
  R-31734-18707: adding an element after startsearch invalidates the search
} -setup {
} -body {
  array set a {a 1 b 2}
  set sid [array startsearch a]
  set a(c) 3
  set rc [catch {array nextelement a $sid} msg]
  set match [string match {*couldn't find search*} $msg]
  catch {array donesearch a $sid}
  list $rc $match
} -cleanup {
  unset -nocomplain a sid msg rc match
} -result {1 1}}

###############################################################################

runTest {test arrayops-5.7a {
  R-31734-18707: removing an element after startsearch invalidates the search
} -body {
  array set a {a 1 b 2 c 3}
  set sid [array startsearch a]
  unset a(b)
  set rc [catch {array anymore a $sid} msg]
  set match [string match {*couldn't find search*} $msg]
  catch {array donesearch a $sid}
  list $rc $match
} -cleanup {
  unset -nocomplain a sid msg rc match
} -result {1 1}}

###############################################################################

runTest {test arrayops-5.7b {
  R-31734-18707: modifying the value of an existing element after startsearch invalidates the search
} -constraints {th8} -setup {
} -body {
  array set a {a 1 b 2 c 3}
  set sid [array startsearch a]
  set a(b) 99
  set rc [catch {array nextelement a $sid} msg]
  set match [string match {*couldn't find search*} $msg]
  catch {array donesearch a $sid}
  list $rc $match
} -cleanup {
  unset -nocomplain a sid msg rc match
} -result {1 1}}

###############################################################################

runTest {test arrayops-5.7c {
  R-31734-18707: read-only access ([array names], [info exists]) does NOT invalidate the search
} -setup {
} -body {
  array set a {a 1 b 2 c 3}
  set sid [array startsearch a]
  array names a
  info exists a(b)
  set names {}
  while {[array anymore a $sid]} {
    lappend names [array nextelement a $sid]
  }
  array donesearch a $sid
  lsort $names
} -cleanup {
  unset -nocomplain a sid names
} -result {a b c}}

###############################################################################

runTest {test arrayops-5.8 {
  R-15876-06247: search-id paired with a different array name than the one it was started for is rejected as if unknown
} -constraints {th8} -setup {
} -body {
  array set a {one 1}
  array set b {two 2}
  set sid [array startsearch a]
  set rc [catch {array nextelement b $sid} msg]
  array donesearch a $sid
  list $rc [string match {*couldn't find search*} $msg]
} -cleanup {
  unset -nocomplain a b sid rc msg
} -result {1 1}}

###############################################################################

runTest {test arrayops-5.9 {
  R-52780-31332: multiple concurrent searches on the same array each have their own independent cursor
} -setup {
} -body {
  array set a {alpha 1 beta 2 gamma 3}
  set s1 [array startsearch a]
  set s2 [array startsearch a]
  set v1 [array nextelement a $s1]
  set v2 [array nextelement a $s2]
  array donesearch a $s1
  array donesearch a $s2
  # Both are at the same cursor position, so they return the same first element.
  expr {$v1 eq $v2}
} -cleanup {
  unset -nocomplain a s1 s2 v1 v2
} -result {1}}

###############################################################################

runTest {test arrayops-5.10 {
  R-15876-06247: forged search-id (string that resembles the format but never started) is rejected
} -setup {
} -body {
  array set a {x 1}
  list [catch {array nextelement a "s-99999-a"} msg] [string match {*couldn't find search*} $msg]
} -cleanup {
  unset -nocomplain a msg
} -result {1 1}}

###############################################################################

runTest {test arrayops-5.11 {
  R-45874-35574: startsearch / nextelement / anymore / donesearch all raise wrong-# args on bad arity
} -setup {
} -body {
  array set a {x 1}
  list \
      [catch {array startsearch}    msg] [string match {*wrong # args*} $msg] \
      [catch {array startsearch a b}  msg] [string match {*wrong # args*} $msg] \
      [catch {array nextelement a}    msg] [string match {*wrong # args*} $msg] \
      [catch {array anymore a}        msg] [string match {*wrong # args*} $msg] \
      [catch {array donesearch a}     msg] [string match {*wrong # args*} $msg]
} -cleanup {
  unset -nocomplain a msg
} -result {1 1 1 1 1 1 1 1 1 1}}

###############################################################################
#
# Section 6 -- ::th8testlib::array_searches: pending-search introspection
#
###############################################################################

runTest {test arrayops-6.1 {
  Test-only command [::th8testlib::array_searches]: empty when no
  pending searches.  Not part of the language standard; exercises
  Th8_IterateArraySearches via th8_testlib for leak detection.
} -constraints {array_searches} -body {
  ::th8testlib::array_searches
} -result {}}

###############################################################################

runTest {test arrayops-6.2 {
  [::th8testlib::array_searches] returns flat list of
  {arrayName searchId} pairs, sorted by arrayName then searchId.
} -constraints {array_searches} -setup {
} -body {
  array set a {x 1 y 2}
  array set b {p 1}
  set s1 [array startsearch a]
  set s2 [array startsearch a]
  set s3 [array startsearch b]
  set result [::th8testlib::array_searches]
  array donesearch a $s1
  array donesearch a $s2
  array donesearch b $s3
  list [llength $result] \
       [lindex $result 0 0] [lindex $result 1 0] [lindex $result 2 0] \
       [expr {[lindex $result 0 1] eq $s1}] \
       [expr {[lindex $result 1 1] eq $s2}] \
       [expr {[lindex $result 2 1] eq $s3}]
} -cleanup {
  unset -nocomplain a b s1 s2 s3 result
} -result {3 a a b 1 1 1}}

###############################################################################

runTest {test arrayops-6.3 {
  [::th8testlib::array_searches] accepts optional glob pattern
  matched against arrayName.
} -constraints {array_searches} -setup {
} -body {
  array set a {x 1}
  array set b {p 1}
  set s1 [array startsearch a]
  set s2 [array startsearch b]
  set onlyA [::th8testlib::array_searches a*]
  array donesearch a $s1
  array donesearch b $s2
  list [llength $onlyA] \
       [lindex $onlyA 0 0] \
       [expr {[lindex $onlyA 0 1] eq $s1}]
} -cleanup {
  unset -nocomplain a b s1 s2 onlyA
} -result {1 a 1}}

###############################################################################

runTest {test arrayops-6.4 {
  [::th8testlib::array_searches] returns empty list when pattern
  matches no pending-search arrays.
} -constraints {array_searches} -setup {
} -body {
  array set a {x 1}
  set sid [array startsearch a]
  set out [::th8testlib::array_searches zzz*]
  array donesearch a $sid
  set out
} -cleanup {
  unset -nocomplain a sid out
} -result {}}

###############################################################################

source tests/epilogue.tcl
