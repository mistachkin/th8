###############################################################################
#
# regexp_switches.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for regexp command switches and option combinations that
# are not exercised by the main regex test files: -about, -indices
# combined with -inline, -start edge cases, error paths.
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
# Section 1 -- regexp -about (pattern complexity analysis)
#
###############################################################################

runTest {test regexp_switches-1.1 {
  -about returns metadata list for simple pattern
} -constraints {
    regexp th8
} -body {
  set info [regexp -about {abc}]
  # Returns pairs: nsub N nstates N nsubre N nlacons N matchall B backref B lookaround B
  expr {[llength $info] == 14}
} -cleanup {
  unset -nocomplain info
} -result {1}}

###############################################################################

runTest {test regexp_switches-1.2 {
  -about reports capture group count
} -constraints {
    regexp th8
} -body {
  set info [regexp -about {(a)(b)(c)}]
  set idx [lsearch $info nsub]
  lindex $info [expr {$idx + 1}]
} -cleanup {
  unset -nocomplain info idx
} -result {3}}

###############################################################################

runTest {test regexp_switches-1.3 {
  -about reports zero groups for non-capturing pattern
} -constraints {
    regexp th8
} -body {
  set info [regexp -about {abc}]
  set idx [lsearch $info nsub]
  lindex $info [expr {$idx + 1}]
} -cleanup {
  unset -nocomplain info idx
} -result {0}}

###############################################################################

runTest {test regexp_switches-1.4 {
  -about detects backreference
} -constraints {
    regexp th8
} -body {
  set info [regexp -about {(a)\1}]
  set idx [lsearch $info backref]
  lindex $info [expr {$idx + 1}]
} -cleanup {
  unset -nocomplain info idx
} -result {1}}

###############################################################################

runTest {test regexp_switches-1.5 {
  -about detects lookaround
} -constraints {
    regexp th8
} -body {
  set info [regexp -about {a(?=b)}]
  set idx [lsearch $info lookaround]
  lindex $info [expr {$idx + 1}]
} -cleanup {
  unset -nocomplain info idx
} -result {1}}

###############################################################################

runTest {test regexp_switches-1.6 {
  -about with -nocase flag
} -constraints {
    regexp th8
} -body {
  set info [regexp -about -nocase {abc}]
  expr {[llength $info] == 14}
} -cleanup {
  unset -nocomplain info
} -result {1}}

###############################################################################
#
# Section 2 -- regexp -indices combined with -inline
#
###############################################################################

runTest {test regexp_switches-2.1 {
  -indices -inline returns index pairs as list
} -constraints {
    regexp
} -body {
  regexp -indices -inline {b+} "aabbcc"
} -result {{2 3}}}

###############################################################################

runTest {test regexp_switches-2.2 {
  -indices -inline with capture groups
} -constraints {
    regexp
} -body {
  regexp -indices -inline {(a+)(b+)} "xaabby"
} -result {{1 4} {1 2} {3 4}}}

###############################################################################

runTest {test regexp_switches-2.3 {
  -indices -inline -all returns all match indices
} -constraints {
    regexp
} -body {
  regexp -indices -inline -all {[0-9]+} "a1b23c456"
} -result {{1 1} {3 4} {6 8}}}

###############################################################################
#
# Section 3 -- regexp -start edge cases
#
###############################################################################

runTest {test regexp_switches-3.1 {
  -start past end of string returns no match
} -constraints {
    regexp
} -body {
  regexp -start 100 {abc} "abc"
} -result {0}}

###############################################################################

runTest {test regexp_switches-3.2 {
  -start at exact match position
} -constraints {
    regexp
} -body {
  regexp -start 3 {def} "abcdef"
} -result {1}}

###############################################################################

runTest {test regexp_switches-3.3 {
  -start 0 matches from beginning
} -constraints {
    regexp
} -body {
  regexp -start 0 {^abc} "abc"
} -result {1}}

###############################################################################
#
# Section 4 -- regexp error paths
#
###############################################################################

runTest {test regexp_switches-4.1 {
  bad switch produces error
} -constraints {
    regexp
} -setup {
  unset -nocomplain msg
} -body {
  catch {regexp -badswitch {abc} "abc"} msg
  expr {[string match "*bad *" $msg] || [string match "*wrong*" $msg]}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test regexp_switches-4.2 {
  wrong number of args (no pattern)
} -constraints {
    regexp
} -body {
  catch {regexp} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test regexp_switches-4.3 {
  invalid regex pattern produces error
} -constraints {
    regexp
} -body {
  set pat "\[invalid"
  set rc [catch {regexp $pat "test"} msg]
  list $rc [expr {[string length $msg] > 0}]
} -cleanup {
  unset -nocomplain rc msg pat
} -result {1 1}}

###############################################################################

runTest {test regexp_switches-4.4 {
  -start without value
} -constraints {
    regexp
} -body {
  catch {regexp -start} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################
#
# Section 5 -- regexp with extra capture variables (subvar overflow)
#
###############################################################################

runTest {test regexp_switches-5.1 {
  more variables than capture groups sets empty
} -constraints {
    regexp
} -body {
  set a ""
  set b ""
  set c ""
  regexp {(x)} "x" all a b c
  list $all $a $b $c
} -cleanup {
  unset -nocomplain a b c all
} -result {x x {} {}}}

###############################################################################

runTest {test regexp_switches-5.2 {
  -all returns match count
} -constraints {
    regexp
} -body {
  regexp -all {[aeiou]} "hello world"
} -result {3}}

###############################################################################

runTest {test regexp_switches-5.3 {
  -all with capture variables updates on each match
} -constraints {
    regexp
} -setup {
  unset -nocomplain m
} -body {
  regexp -all -inline {[aeiou]} "hello world"
} -cleanup {
  unset -nocomplain m
} -result {e o o}}

###############################################################################

runTest {test regexp_switches-6.1 {
  regexp with EMPTY first argument drives src/plugins/
  regexp/th8_regex.c L1104 (C1=F) -- TH8_LEN(argl[iArg])
  > 0 is false.  The switches loop falls into the else
  branch (treats empty as non-switch start of pattern).
  Existing tests pass non-empty switches or non-empty
  patterns; the zero-length switch byte never exercises
  the (F,-) MC/DC vector.
} -constraints {
    regexp
} -body {
  set rc [catch {regexp {} "abc"} m]
  list $rc $m
} -cleanup {
  unset -nocomplain rc m
} -result {0 1}}

###############################################################################

runTest {test regexp_switches-6.2 {
  regexp with OPTIONAL capture group that does not match
  drives src/plugins/regexp/th8_regex.c L1284 (C2=F) --
  k <= re_nsub (C1=T) AND pmatch[k].rm_so >= 0 (C2=F:
  unmatched optional group has rm_so == -1).  Variable
  for the unmatched group gets set to the empty string.
} -constraints {
    regexp
} -setup {
  unset -nocomplain all g1 g2
} -body {
  # Pattern (x)?(y) -- group 1 is optional and won't match
  # when the input is just "y".
  set rc [regexp {(x)?(y)} "y" all g1 g2]
  list $rc $all $g1 $g2
} -cleanup {
  unset -nocomplain rc all g1 g2
} -result {1 y {} y}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
