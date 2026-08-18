###############################################################################
#
# lsort.tcl --
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
# Section 6 -- lsort: Sort a list
#
###############################################################################

runTest {test lsort-6.1 {
  R-30185-38802: lsort default (ascii ascending)
} -body {
  lsort {banana apple cherry date}
} -result {apple banana cherry date}}

###############################################################################

runTest {test lsort-6.2 {
  R-12088-02535: lsort -ascii explicit
} -body {
  lsort -ascii {banana apple cherry date}
} -result {apple banana cherry date}}

###############################################################################

runTest {test lsort-6.3 {
  R-53425-17573: lsort -integer
} -body {
  lsort -integer {30 5 100 12 1}
} -result {1 5 12 30 100}}

###############################################################################

runTest {test lsort-6.4 {
  R-09875-38623: lsort -real
} -body {
  lsort -real {3.14 1.0 2.71 0.5}
} -result {0.5 1.0 2.71 3.14}}

###############################################################################

runTest {test lsort-6.5 {
  R-40622-36745: lsort -increasing (default)
} -body {
  lsort -increasing {c a b}
} -result {a b c}}

###############################################################################

runTest {test lsort-6.6 {
  R-39269-21617: lsort -decreasing
} -body {
  lsort -decreasing {c a b}
} -result {c b a}}

###############################################################################

runTest {test lsort-6.7 {
  R-39269-21617: lsort -integer -decreasing
} -body {
  lsort -integer -decreasing {30 5 100 12 1}
} -result {100 30 12 5 1}}

###############################################################################

runTest {test lsort-6.8 {
  R-53256-47651: lsort -unique removes duplicates
} -body {
  lsort -unique {b a c b a d c}
} -result {a b c d}}

###############################################################################

runTest {test lsort-6.9 {
  R-30185-38802: lsort empty list
} -body {
  lsort {}
} -result {}}

###############################################################################

runTest {test lsort-6.10 {
  R-30185-38802: lsort single element
} -body {
  lsort {hello}
} -result {hello}}

###############################################################################

runTest {test lsort-6.11 {
  R-30185-38802: lsort already sorted
} -body {
  lsort {a b c d e}
} -result {a b c d e}}

###############################################################################

runTest {test lsort-6.12 {
  R-30185-38802: lsort reverse sorted input
} -body {
  lsort {e d c b a}
} -result {a b c d e}}

###############################################################################

runTest {test lsort-6.13 {
  R-53256-47651: lsort -integer -unique
} -body {
  lsort -integer -unique {3 1 2 3 1 4}
} -result {1 2 3 4}}

###############################################################################

runTest {test lsort-6.14 {
  R-12088-02535: lsort ascii sorts numbers lexicographically
} -body {
  lsort {100 20 3}
} -result {100 20 3}}

###############################################################################
#
# Section 7 -- lsort: -dictionary, -command, -index
#
###############################################################################

runTest {test lsort-7.1 {
  R-12054-28168: lsort -dictionary sorts case-insensitively with numbers
                 compared numerically
} -setup {
} -body {
  set result [lsort -dictionary {a10 a2 a1 A3}]
} -cleanup {
  unset -nocomplain result
} -result {a1 a2 A3 a10}}

###############################################################################

runTest {test lsort-7.2 {
  R-21407-05967: lsort -command uses custom comparison script
} -setup {
} -body {
  set result [lsort -command {apply {{a b} {expr {$a - $b}}}} {3 1 4 1 5}]
} -cleanup {
  unset -nocomplain result
} -result {1 1 3 4 5}}

###############################################################################

runTest {test lsort-7.3 {
  R-34299-16437: lsort -index sorts by sub-element
} -setup {
} -body {
  set result [lsort -index 1 {{a 2} {b 1} {c 3}}]
} -cleanup {
  unset -nocomplain result
} -result {{b 1} {a 2} {c 3}}}

###############################################################################

runTest {test lsort-7.4 {
  R-34299-16437: lsort -index combined with -integer
} -setup {
} -body {
  set result [lsort -integer -index 1 {{x 30} {y 10} {z 20}}]
} -cleanup {
  unset -nocomplain result
} -result {{y 10} {z 20} {x 30}}}

###############################################################################

runTest {test lsort-7.5 {
  R-12054-28168: lsort -dictionary combined with -index
} -setup {
} -body {
  set result [lsort -dictionary -index 0 {{B10 x} {a2 y} {A1 z}}]
} -cleanup {
  unset -nocomplain result
} -result {{A1 z} {a2 y} {B10 x}}}

###############################################################################
#
# Section 8 -- lsearch: -sorted option
#
###############################################################################

runTest {test lsort-8.1 {
  R-36657-11143: lsearch -sorted performs binary search
} -setup {
} -body {
  set result [lsearch -sorted {alpha bravo charlie delta} charlie]
} -cleanup {
  unset -nocomplain result
} -result {2}}

###############################################################################

runTest {test lsort-8.2 {
  R-36657-11143: lsearch -sorted returns -1 on miss
} -body {
  lsearch -sorted {a b c d} z
} -result {-1}}

###############################################################################

runTest {test lsort-8.3 {
  R-36657-11143: lsearch -sorted -integer
} -body {
  lsearch -sorted -integer {10 20 30 40 50} 30
} -result {2}}

###############################################################################

runTest {test lsort-8.4 {
  R-36657-11143: lsearch -sorted -inline
} -body {
  lsearch -sorted -inline {apple banana cherry} banana
} -result {banana}}

###############################################################################
#
# Section 9 -- lsort: TH8K-017 robustness regressions
#
###############################################################################

runTest {test lsort-9.1 {
  R-12054-28168: lsort -dictionary compares digit runs by magnitude
                 without integer overflow (runs wider than a 32-bit int
                 must still order correctly)
} -body {
  #
  # "4294967296" (2^32) and "10000000000" (10^10) both exceed the
  # signed 32-bit range that the old num=num*10+digit accumulator
  # overflowed, which produced wrong orderings.  Magnitude order is
  # 5 < 4294967296 < 10000000000.
  #
  lsort -dictionary {a10000000000 a5 a4294967296}
} -result {a5 a4294967296 a10000000000}}

###############################################################################

runTest {test lsort-9.2 {
  R-12054-28168: lsort -dictionary numerically-equal runs order by
                 length (leading zeros as tiebreak)
} -body {
  lsort -dictionary {x007 x7 x07}
} -result {x7 x07 x007}}

###############################################################################

runTest {test lsort-9.2.1 {
  R-12054-28168: lsort -dictionary all-zero digit runs (every digit is
                 a leading zero) order by length
} -body {
  lsort -dictionary {x0 x000 x00}
} -result {x0 x00 x000}}

###############################################################################

runTest {test lsort-9.3 {
  R-34299-16437: lsort -index reports an error (rather than silently
                 ignoring the split) when a list element is not a
                 well-formed sub-list -- unmatched open quote
} -body {
  catch {lsort -index 0 [list {a b} "\""]}
} -result {1}}

###############################################################################

runTest {test lsort-9.4 {
  R-34299-16437: lsort -index reports an error on a malformed sub-list
                 -- unmatched open brace
} -body {
  catch {lsort -index 0 {{a b} \{}}
} -result {1}}

###############################################################################

runTest {test lsort-9.5 {
  R-53425-17573: lsort -integer sorts a large reversed list correctly
                 (exercises the O(n log n) merge across width levels)
} -setup {
  set input {}
  for {set i 200} {$i > 0} {incr i -1} {
    lappend input $i
  }
  set want {}
  for {set i 1} {$i <= 200} {incr i} {
    lappend want $i
  }
} -body {
  expr {[lsort -integer $input] eq $want}
} -cleanup {
  unset -nocomplain input want i
} -result {1}}

###############################################################################

runTest {test lsort-9.6 {
  R-39269-21617: lsort -decreasing with a -command comparator applies
                 the direction flip safely and stably
} -body {
  lsort -decreasing -command {apply {{a b} {expr {$a - $b}}}} {3 1 2 3 1}
} -result {3 3 2 1 1}}

###############################################################################

source tests/epilogue.tcl
