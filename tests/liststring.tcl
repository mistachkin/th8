###############################################################################
#
# liststring.tcl --
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
# Section 1 -- List Commands (Section 14)
#
###############################################################################

runTest {test liststring-1.1 {
  R-05534-35124: empty list equals empty string
} -body {
  expr {[list] eq ""}
} -result {1}}

###############################################################################

runTest {test liststring-1.2 {
  R-05534-35124: empty list string length is zero
} -body {
  string length [list]
} -result {0}}

###############################################################################

runTest {test liststring-1.3 {
  R-45345-25968: list commands parse respecting braces
} -setup {
} -body {
  set result [lindex {{a b} c d} 0]
  set result
} -cleanup {
  unset -nocomplain result
} -result {a b}}

###############################################################################

runTest {test liststring-1.4 {
  R-45345-25968: list commands parse respecting quotes
} -setup {
} -body {
  set mylist [list "hello world" foo bar]
  set result [lindex $mylist 0]
  set result
} -cleanup {
  unset -nocomplain mylist result
} -result {hello world}}

###############################################################################

runTest {test liststring-1.5 {
  R-32094-10782: lindex with no index returns list unchanged
} -body {
  lindex {a b c}
} -result {a b c}}

###############################################################################

runTest {test liststring-1.6 {
  R-32094-10782: lindex with no index on single element
} -body {
  lindex hello
} -result {hello}}

###############################################################################

runTest {test liststring-1.7 {
  R-44569-25869: lrange clamps first < 0 to 0
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist -3 2
} -cleanup {
  unset -nocomplain mylist
} -result {a b c}}

###############################################################################

runTest {test liststring-1.8 {
  R-44569-25869: lrange with first -1 clamps to 0
} -setup {
} -body {
  set mylist {x y z}
  lrange $mylist -1 1
} -cleanup {
  unset -nocomplain mylist
} -result {x y}}

###############################################################################

runTest {test liststring-1.9 {
  R-35094-63625: lrange clamps last beyond end
} -setup {
} -body {
  set mylist {a b c}
  lrange $mylist 1 100
} -cleanup {
  unset -nocomplain mylist
} -result {b c}}

###############################################################################

runTest {test liststring-1.10 {
  R-35094-63625: lrange with last far beyond end
} -setup {
} -body {
  set mylist {p q r s}
  lrange $mylist 2 999
} -cleanup {
  unset -nocomplain mylist
} -result {r s}}

###############################################################################

runTest {test liststring-1.11 {
  R-19062-07944: lreplace more replacements increases length
} -setup {
} -body {
  set mylist {a b c}
  set result [lreplace $mylist 1 1 X Y Z]
  list $result [llength $result]
} -cleanup {
  unset -nocomplain mylist result
} -result {{a X Y Z c} 5}}

###############################################################################

runTest {test liststring-1.12 {
  R-19062-07944: lreplace inserting at position increases length
} -setup {
} -body {
  set mylist {a b}
  set result [lreplace $mylist 0 0 W X Y]
  list $result [llength $result]
} -cleanup {
  unset -nocomplain mylist result
} -result {{W X Y b} 4}}

###############################################################################

runTest {test liststring-1.13 {
  R-28645-27103: lreplace fewer replacements decreases length
} -setup {
} -body {
  set mylist {a b c d e}
  set result [lreplace $mylist 1 3 X]
  list $result [llength $result]
} -cleanup {
  unset -nocomplain mylist result
} -result {{a X e} 3}}

###############################################################################

runTest {test liststring-1.14 {
  R-28645-27103: lreplace with no replacements decreases length
} -setup {
} -body {
  set mylist {a b c d e}
  set result [lreplace $mylist 1 3]
  list $result [llength $result]
} -cleanup {
  unset -nocomplain mylist result
} -result {{a e} 2}}

###############################################################################

runTest {test liststring-1.15 {
  R-65407-60482: lsearch uses string match rules (glob)
} -body {
  lsearch {apple banana cherry} b*
} -result {1}}

###############################################################################

runTest {test liststring-1.16 {
  R-65407-60482: lsearch glob with question mark
} -body {
  lsearch {cat bat hat} ?at
} -result {0}}

###############################################################################

runTest {test liststring-1.17 {
  R-65407-60482: lsearch glob with bracket range
} -body {
  lsearch {123 abc xyz} {[a-z]*}
} -result {1}}

###############################################################################

runTest {test liststring-1.19 {
  R-56935-37465: lsort -index sort is stable -- equal-key elements
                 preserve their input order
} -setup {
} -body {
  #
  # Build a list of pairs whose first element is the sort key
  # and whose second element is a tag distinguishing otherwise-
  # equal-key elements.  A stable sort MUST keep tag-a before
  # tag-b for each repeated key (3 appears twice with tags a,b;
  # 1 appears twice with tags a,b).  An unstable sort is free
  # to reorder equal-key elements and would fail this test.
  # An input where all elements are byte-identical (e.g.
  # {c c c} or {3 1 2 3 1}) cannot distinguish stable from
  # unstable -- this paired-tag form is the canonical shape
  # for stability verification.
  #
  set input {{3 a} {1 a} {2 a} {3 b} {1 b}}
  set result [lsort -integer -index 0 $input]
  set result
} -cleanup {
  unset -nocomplain input result
} -result {{1 a} {1 b} {2 a} {3 a} {3 b}}}

###############################################################################

runTest {test liststring-1.20 {
  R-25333-10244: split splitChars is a character set
} -body {
  split "a.b:c.d" .:
} -result {a b c d}}

###############################################################################

runTest {test liststring-1.21 {
  R-25333-10244: split each char in splitChars is a separate delimiter
} -body {
  split "x,y;z" ",;"
} -result {x y z}}

###############################################################################

runTest {test liststring-1.22 {
  R-38622-32405: split leading delimiter produces leading empty element
} -setup {
} -body {
  set result [split ":a:b" :]
  list [lindex $result 0] [llength $result]
} -cleanup {
  unset -nocomplain result
} -result {{} 3}}

###############################################################################

runTest {test liststring-1.23 {
  R-38622-32405: split leading delimiter is empty first element
} -body {
  split ",hello,world" ,
} -result {{} hello world}}

###############################################################################

runTest {test liststring-1.24 {
  R-01699-22478: split trailing delimiter produces trailing empty element
} -setup {
} -body {
  set result [split "a:b:" :]
  list [lindex $result end] [llength $result]
} -cleanup {
  unset -nocomplain result
} -result {{} 3}}

###############################################################################

runTest {test liststring-1.25 {
  R-01699-22478: split trailing delimiter is empty last element
} -body {
  split "hello,world," ,
} -result {hello world {}}}

###############################################################################

runTest {test liststring-1.26 {
  R-50429-33312: list is whitespace-separated elements
} -setup {
} -body {
  set mylist "alpha beta gamma"
  llength $mylist
} -cleanup {
  unset -nocomplain mylist
} -result {3}}

###############################################################################

runTest {test liststring-1.27 {
  R-50429-33312: list elements separated by spaces are distinct
} -setup {
} -body {
  set mylist "x y z"
  list [lindex $mylist 0] [lindex $mylist 1] [lindex $mylist 2]
} -cleanup {
  unset -nocomplain mylist
} -result {x y z}}

###############################################################################

runTest {test liststring-1.28 {
  R-23074-38273: elements with special chars enclosed in braces
} -setup {
} -body {
  set mylist {{hello world} foo bar}
  list [lindex $mylist 0] [llength $mylist]
} -cleanup {
  unset -nocomplain mylist
} -result {{hello world} 3}}

###############################################################################

runTest {test liststring-1.29 {
  R-23074-38273: braces protect spaces in list elements
} -setup {
} -body {
  set mylist {{a b c} {d e} f}
  list [lindex $mylist 0] [lindex $mylist 1] [lindex $mylist 2]
} -cleanup {
  unset -nocomplain mylist
} -result {{a b c} {d e} f}}

###############################################################################
#
# Section 2 -- String Commands (Section 15)
#
###############################################################################

runTest {test liststring-2.1 {
  R-45728-58943: string match must match entire string
} -body {
  list [string match "hello" "hello"] [string match "hello" "hello world"]
} -result {1 0}}

###############################################################################

runTest {test liststring-2.2 {
  R-45728-58943: string match partial pattern does not match full string
} -body {
  string match "hel" "hello"
} -result {0}}

###############################################################################

runTest {test liststring-2.3 {
  R-45728-58943: string match with glob must cover entire string
} -body {
  list [string match "hel*" "hello"] [string match "*llo" "hello"] \
      [string match "*ell*" "hello"]
} -result {1 1 1}}

###############################################################################

runTest {test liststring-2.4 {
  R-19250-20590: string match backslash matches literal star
} -body {
  string match {\*} "*"
} -result {1}}

###############################################################################

runTest {test liststring-2.5 {
  R-19250-20590: string match backslash star does not match other strings
} -body {
  string match {\*} "hello"
} -result {0}}

###############################################################################

runTest {test liststring-2.6 {
  R-19250-20590: string match backslash question mark matches literal
} -body {
  list [string match {\?} "?"] [string match {\?} "x"]
} -result {1 0}}

###############################################################################

runTest {test liststring-2.7 {
  R-19250-20590: string match backslash in pattern matches literal char
} -body {
  string match {hello\*world} "hello*world"
} -result {1}}

###############################################################################

runTest {test liststring-2.8 {
  R-01692-49711: string map scans left-to-right
} -body {
  string map {ab AB a X} "abc"
} -result {ABc}}

###############################################################################

runTest {test liststring-2.9 {
  R-01692-49711: string map left-to-right first match wins
} -body {
  string map {abc ABC ab XY} "abcdef"
} -result {ABCdef}}

###############################################################################

runTest {test liststring-2.10 {
  R-01692-49711: string map processes replacement without rescanning
} -setup {
} -body {
  set result [string map {a b b c} "aab"]
  set result
} -cleanup {
  unset -nocomplain result
} -result {bbc}}

###############################################################################

runTest {test liststring-2.11 {
  R-43508-65179: string map empty mapping returns unchanged
} -body {
  string map {} "hello world"
} -result {hello world}}

###############################################################################

runTest {test liststring-2.12 {
  R-43508-65179: string map empty mapping preserves any string
} -body {
  string map {} "special chars: \t\n\{"
} -result "special chars: \t\n\{"}

###############################################################################

source tests/epilogue.tcl
