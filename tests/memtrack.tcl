###############################################################################
#
# memtrack.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Exercises the allocation-site memory tracker's test-only driver
# commands (src/test/th8_testlib.c, src/th8_memtrack.c):
#
#   ::th8testlib::test_malloc size        -- allocate + record + return addr
#   ::th8testlib::test_free ptr           -- ownership-checked free
#   ::th8testlib::test_memory_dump file   -- dump live set (TH8_MEM_DEBUG)
#
# These are build-agnostic: test_malloc / test_free exercise the real
# allocator and the test library's logical allocation list in ANY build,
# so the ownership guards below run in the normal suite.  The dump writes
# the tracker's live set only in a TH8_MEM_DEBUG build; in a normal build
# it reports that a debug build is required.  test_memory_dump-2.2 asserts
# the correct outcome for whichever build is running.
#
# See docs/internal/design_notes_memtrack.md.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- test_malloc / test_free and the logical allocation list
#
###############################################################################

runTest {test memtrack-1.1 {
  test_malloc allocates through the real funnel and returns the block's
  address as a 0x-prefixed hex string the test can hold and free.
} -constraints {
    loadLib th8
} -setup {
  set p [::th8testlib::test_malloc 128]
} -body {
  regexp {^0x[0-9a-fA-F]+$} $p
} -cleanup {
  ::th8testlib::test_free $p
  unset -nocomplain p
} -result {1}}

###############################################################################

runTest {test memtrack-1.2 {
  test_free of an address that was never returned by test_malloc (not in
  the logical allocation list) is rejected and frees nothing -- the
  command cannot be coerced into freeing a foreign pointer.
} -constraints {
    loadLib th8
} -body {
  catch {::th8testlib::test_free 0xdeadbeef} m
  set m
} -cleanup {
  unset -nocomplain m
} -result {test_free: pointer is not a live test allocation}}

###############################################################################

runTest {test memtrack-1.3 {
  Freeing the same live allocation twice: the first free succeeds and
  removes it from the logical list; the second is rejected (no double
  free).
} -constraints {
    loadLib th8
} -setup {
  set p [::th8testlib::test_malloc 64]
} -body {
  ::th8testlib::test_free $p
  catch {::th8testlib::test_free $p} m
  set m
} -cleanup {
  unset -nocomplain p m
} -result {test_free: pointer is not a live test allocation}}

###############################################################################

runTest {test memtrack-1.4 {
  test_malloc rejects a non-integer size argument.
} -constraints {
    loadLib th8
} -body {
  catch {::th8testlib::test_malloc abc} m
  set m
} -cleanup {
  unset -nocomplain m
} -result {test_malloc: size must be a non-negative integer}}

###############################################################################

runTest {test memtrack-1.5 {
  test_free rejects an argument that is not a hex address.
} -constraints {
    loadLib th8
} -body {
  catch {::th8testlib::test_free notaptr} m
  set m
} -cleanup {
  unset -nocomplain m
} -result {test_free: ptr must be a hex address}}

###############################################################################

runTest {test memtrack-1.6 {
  A hex address that parses but is not a live test allocation is still
  rejected (parsing succeeds, the ownership check fails).
} -constraints {
    loadLib th8
} -setup {
  set p [::th8testlib::test_malloc 32]
  ::th8testlib::test_free $p
} -body {
  # p is now freed / removed from the logical list.
  catch {::th8testlib::test_free $p} m
  set m
} -cleanup {
  unset -nocomplain p m
} -result {test_free: pointer is not a live test allocation}}

###############################################################################
#
# Section 2 -- test_memory_dump
#
###############################################################################

runTest {test memtrack-2.1 {
  test_memory_dump requires exactly one argument (the file name).
} -constraints {
    loadLib th8
} -body {
  list [catch {::th8testlib::test_memory_dump} m] [string match {wrong # args*} $m]
} -cleanup {
  unset -nocomplain m
} -result {1 1}}

###############################################################################

runTest {test memtrack-2.2 {
  A dump either succeeds with a numeric live-block count (TH8_MEM_DEBUG
  build), or reports that a TH8_MEM_DEBUG build is required (normal
  build), or -- on a re-run under a debug build -- reports the file
  already exists (create-exclusive).  Each is the correct outcome for
  its situation.
} -constraints {
    loadLib th8
} -body {
  set rc [catch {::th8testlib::test_memory_dump memtrack_dump.out} m]
  expr {($rc == 0 && [string is integer -strict $m]) ||
        ($rc == 1 && ([string match {*TH8_MEM_DEBUG*} $m] ||
                      [string match {*already exists*} $m]))}
} -cleanup {
  unset -nocomplain rc m
} -result {1}}

###############################################################################
#
# Section 3 -- test_memory_reset
#
###############################################################################

runTest {test memtrack-3.1 {
  test_memory_reset clears the tracker's bookkeeping and returns the
  (non-negative) number of live blocks it had been tracking.  In a normal
  build it is a no-op that returns 0.
} -constraints {
    loadLib th8
} -body {
  string is integer -strict [::th8testlib::test_memory_reset]
} -result {1}}

###############################################################################

runTest {test memtrack-3.2 {
  test_memory_reset takes no arguments.
} -constraints {
    loadLib th8
} -body {
  list [catch {::th8testlib::test_memory_reset extra} m] \
      [string match {wrong # args*} $m]
} -cleanup {
  unset -nocomplain m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
