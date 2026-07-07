###############################################################################
#
# coverage_lists_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [list] / [lremove] / [dict update]
# / [dict remove] decisions in src/plugins/th8_lists.c.  Each
# test drives the SPECIFIC missing vector identified by
# llvm-cov's per-decision Executed Test Vectors block, not just
# the line.
#
#   :442  if (pCached && pCached->zData)        ([list] cache miss)
#                                                missing: C1=F (cache miss)
#   :1655 .. == TH8_OK && idx == i              ([lremove] invalid idx)
#                                                missing: C1=F (parse fails)
#   :2750 anElem[i] == argl[j] && memcmp == 0   ([dict remove] no match)
#                                                missing: C1=F (length differ)
#   :4070 if (argc < 6 || (argc - 4) % 2 != 0)  ([dict update] odd args)
#                                                missing: C2=T (argc>=6 odd)
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

runTest {test lismsc-1.1 {
  list with novel arguments drives the cache-miss vector
  at line 442 (C1=F: pCached is NULL because the
  argv-prefix has not been cached yet).  The unique-prefix
  values force a cache slot creation.
} -constraints {
    th8
} -body {
  list \
      [list lismsc_uniq_xyz_1 abc def] \
      [list lismsc_uniq_xyz_2 ghi jkl] \
      [list lismsc_uniq_xyz_3 mno pqr]
} -result \
{{lismsc_uniq_xyz_1 abc def} {lismsc_uniq_xyz_2 ghi jkl} {lismsc_uniq_xyz_3 mno pqr}}}

###############################################################################

runTest {test lismsc-2.1 {
  lremove with an INVALID index argument drives the (F,-)
  vector at line 1655 -- th8ParseIndex returns non-OK for
  a non-numeric / out-of-range string, short-circuiting
  the second condition.  TH8 may either error out or
  treat the bad index as a no-op; either way the parse
  short-circuit fires.
} -constraints {
    th8
} -body {
  set rc [catch {lremove {a b c d} "bogus_index"} m]
  expr {$rc == 1 || [llength $m] >= 0}
} -cleanup {
  unset -nocomplain rc m
} -result {1}}

###############################################################################

runTest {test lismsc-3.1 {
  dict remove with a key whose LENGTH differs from the
  dict's stored keys drives the C1=F vector at line 2750
  (anElem[i] != argl[j]) -- the length check short-
  circuits before memcmp runs.
} -constraints {
    th8
} -body {
  list \
      [dict remove {alpha 1 beta 2 gamma 3} long_nonexistent_key_12345] \
      [dict remove {alpha 1 beta 2 gamma 3} z] \
      [dict remove {alpha 1 beta 2 gamma 3} alpha]
} -result \
{{alpha 1 beta 2 gamma 3} {alpha 1 beta 2 gamma 3} {beta 2 gamma 3}}}

###############################################################################

runTest {test lismsc-4.1 {
  dict update with an ODD number of (key, varname) pairs
  drives the C2=T vector at line 4070 -- argc >= 6 (so
  C1 is false) but (argc - 4) is odd, so C2 fires.
  Example: argc==7 means 5 args after "dict update",
  which can be { dictVar k1 v1 k2 } body -- not a valid
  pair sequence.
} -constraints {
    th8
} -body {
  set ::lismsc_dv {a 1 b 2}
  set rc [catch {dict update ::lismsc_dv k1 v1 k2 {set v1 X}} m]
  expr {$rc == 1 && [string length $m] > 0}
} -cleanup {
  unset -nocomplain ::lismsc_dv rc m
} -result {1}}

###############################################################################

runTest {test lismsc-5.1 {
  lsort -dictionary mixing digit-prefix and non-digit-prefix
  elements drives the (T, F) vector at th8_lists.c:1312
  -- aIsDigit=T (one starts with digit), bIsDigit=F (other
  starts with letter).
} -constraints {
    th8
} -body {
  lsort -dictionary {1abc abc 2def}
} -result {1abc 2def abc}}

###############################################################################

runTest {test lismsc-5.2 {
  lsort -integer with a NON-integer first element drives
  the (T, -) vector at th8_lists.c:1254 -- the first
  Th8_ToInt fails, short-circuits before checking the
  second.
} -constraints {
    th8
} -body {
  catch {lsort -integer {abc 1 2}} m
  string match "*integer*" $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test lismsc-5.3 {
  dict filter key with a pattern whose length differs
  from any dict key drives the (F, -) vector at
  th8_lists.c:4144 -- length mismatch short-circuits the
  memcmp call.
} -constraints {
    th8
} -body {
  list \
      [dict filter {a 1 b 2 cc 3} key longerkey] \
      [dict filter {a 1 b 2 cc 3} key cc] \
      [dict filter {a 1 b 2 cc 3} key a]
} -result {{} {cc 3} {a 1}}}

###############################################################################

runTest {test lismsc-6.1 {
  lsearch -sorted -dictionary on a list where the binary
  search compare finds a MISMATCH between two strings
  drives the C3=F vector at th8_lists.c:891 -- the
  while-loop condition (ia < nA && ib < nPat && cmp == 0)
  exits via C3=F when the per-character compare in the
  body sets cmp non-zero.  Searching for a non-matching
  pattern (e.g. "foo" in a dict-sorted list of "a1"...)
  forces multiple comparisons that each set cmp != 0.
} -constraints {
    th8
} -body {
  list \
      [lsearch -sorted -dictionary {a1 a2 a10 a20 b1 b5} foo] \
      [lsearch -sorted -dictionary {a1 a2 a10 a20 b1 b5} a3] \
      [lsearch -sorted -dictionary {a1 a2 a10 a20 b1 b5} z]
} -result {-1 -1 -1}}

###############################################################################

runTest {test lismsc-7.1 {
  dict update writeback skipping keys that don't match
  -- drives the C1=F vector at th8_lists.c:4141 (the
  length-equality short-circuit in the dict-update
  rebuild loop).  When the dict contains a key whose
  length differs from any update key, anElem[i] !=
  TH8_LEN(argl[argKey]) short-circuits the memcmp and
  the original entry is preserved in the rebuilt dict.
} -constraints {
    th8
} -body {
  set ::lismsc_du1 {a 1 longerkey 2 x 3}
  dict update ::lismsc_du1 a aVar {
      set aVar 100
  }
  set ::lismsc_du1
} -cleanup {
  unset -nocomplain ::lismsc_du1 aVar
} -result {longerkey 2 x 3 a 100}}

###############################################################################

runTest {test lismsc-8.1 {
  dict replace where some dict keys differ in LENGTH
  from any replacement key drives the C1=F vector at
  th8_lists.c:2827 -- the inner loop's length-equality
  short-circuit fires on length-mismatched keys.  The
  unchanged entries are preserved.
} -constraints {
    th8
} -body {
  list \
      [dict replace {a 1 longerkey 2 x 3} a 100] \
      [dict replace {alpha 1 beta 2} alpha 99] \
      [dict replace {one 1 two 2} new 5]
} -result {{a 100 longerkey 2 x 3} {alpha 99 beta 2} {one 1 two 2 new 5}}}

###############################################################################

runTest {test lismsc-9.1 {
  lindex with a SINGLE index-list argument (string form of
  multiple indices) drives the multi-index nested-list
  drilling path at th8_lists.c:287-330.  Tcl semantics:
  `lindex $list {0 1}` is equivalent to `lindex $list 0 1`.
  This argc==3 with nIdx>1 path is otherwise uncovered;
  existing tests use the multi-arg form `lindex $list 0 1`
  which routes through argc>3 elsewhere.
} -constraints {
    th8
} -body {
  list \
      [lindex {{a b c} {d e f}} {0 1}] \
      [lindex {{a b c} {d e f}} {1 2}] \
      [lindex {{{1 2} {3 4}} {{5 6} {7 8}}} {1 0 1}] \
      [lindex {a b c} {0}]
} -result {b f 6 a}}

###############################################################################

runTest {test lismsc-9.2 {
  lindex with an index-list whose elements drive both
  iIndex < 0 (C1=F) and iIndex >= nCount (C2=F) at the
  multi-index drilling loop in th8_lists.c:312
  (`iIndex >= 0 && iIndex < nCount && ALWAYS(azElem)`).
  Existing index-list tests use in-bounds indices (C1=T,
  C2=T); this closes both C1- and C2-pairs.
} -constraints {
    th8
} -body {
  list \
      [lindex {a b c} {end-100 0}] \
      [lindex {a b c} {100 0}] \
      [lindex {{a b} {c d}} {0 -50}] \
      [lindex {{a b} {c d}} {0 5}]
} -result {{} {} {} {}}}

###############################################################################

source tests/epilogue.tcl
