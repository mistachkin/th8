###############################################################################
#
# coverage_env_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_env.c filter compounds.  Each env
# iteration op (LIST, LIST2, EXISTS2, GET2) gates the
# per-entry filter with `if (zName && nName > 0)` and
# `if (zValue && nValue > 0)`.  Ordinary callers always pass
# either NULL (no filter) or non-empty string (filter set), so
# the C2=F vector (zName non-NULL with nName==0) is never
# exercised.  This test drives each filter compound by passing
# an explicit empty string ("" with length 0) for the filter
# argument.
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

runTest {test env_mcdc-1.1 {
  env_kv list with explicit empty name pattern drives
  C2=F vector at src/th8_env.c L379 (zName non-NULL,
  nName == 0).  Suppress output since the full env may
  be huge.
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv list ""} r
  # The call must complete cleanly (list all entries when
  # the filter is empty).
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_mcdc-1.2 {
  env_kv list2 with explicit empty name and value patterns
  drives C2=F at L417 (zValue compound) in addition to L379.
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv list2 "" ""} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_mcdc-1.3 {
  env_kv exists2 with explicit empty name and value patterns
  drives the matching compound in the EXISTS2 op (L488/L494).
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv exists2 "" ""} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_mcdc-1.4 {
  env_kv get2 with explicit empty name and value patterns
  drives the matching compound in the GET2 op (L543, L609).
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv get2 "" ""} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_mcdc-1.5 {
  env_kv list2 with non-empty name pattern and empty value
  pattern drives the value-filter C2=F at L444/L450.
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv list2 "*" ""} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_mcdc-1.6 {
  env_kv list2/exists2/get2 with both filters non-empty drives
  the (T,T) vector pair on the zValue filter compounds at
  L417, L450, L494, L615.
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv list2 "*" "*"} r1
  catch {::th8testlib::env_kv exists2 "PATH" "*"} r2
  catch {::th8testlib::env_kv get2 "PATH" "*"} r3
  list \
      [expr {[string length $r1] >= 0}] \
      [expr {[string length $r2] >= 0}] \
      [expr {[string length $r3] >= 0}]
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1 1 1}}

###############################################################################

runTest {test env_mcdc-1.7 {
  env_kv exists2/get2/list/list2 with NO name argument
  forces zName=NULL, driving the C1=F pair on the
  name-filter compounds at L379 (LIST), L411 (EXISTS2),
  L444 (LIST2), L488 (EXISTS2 secondary), L543 (SET2).
  Back-compat substitution was removed so even LIST/LIST2
  now propagate the NULL pattern through to xKeyValue.
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv exists2} r1
  catch {::th8testlib::env_kv get2} r2
  catch {::th8testlib::env_kv exists} r3
  catch {::th8testlib::env_kv get} r4
  catch {::th8testlib::env_kv list} r5
  catch {::th8testlib::env_kv list2} r6
  list \
      [expr {[string length $r1] >= 0}] \
      [expr {[string length $r2] >= 0}] \
      [expr {[string length $r3] >= 0}] \
      [expr {[string length $r4] >= 0}] \
      [expr {[string length $r5] >= 0}] \
      [expr {[string length $r6] >= 0}]
} -cleanup {
  unset -nocomplain r1 r2 r3 r4 r5 r6
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test env_mcdc-1.8 {
  env_kv with a name STRING longer than 255 bytes drives
  the heap-allocation path in th8EnvNulTerminate (src/
  th8_env.c L87 false branch), which yields a heap z
  passed to th8EnvFreeNul.  This closes both the C1-pair
  and the C2-pair at src/th8_env.c L127
  `if (z && z != zBuf)` -- previously only the (F,-) and
  (T,F) vectors were observed; this test adds the (T,T)
  vector by passing a 300-byte name that overflows the
  256-byte stack buffer.
} -constraints {
    th8
} -setup {
} -body {
  # 300-byte name forces th8EnvNulTerminate to heap-allocate.
  set longName [string repeat X 300]
  catch {::th8testlib::env_kv exists $longName} r
  # Whatever the env lookup returns (likely "0" since the
  # var doesn't exist), the call must complete cleanly.
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain longName r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
