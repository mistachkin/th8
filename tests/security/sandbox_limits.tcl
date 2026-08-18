###############################################################################
#
# sandbox_limits.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Red-team tests for sandbox resource limits: step limits, result size
# limits, memory allocation limits, and eval depth limits.
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
# Helper: extract the return code from sandbox result.
#
###############################################################################

runTest {test sandbox-limits-1.1 {
  R-42761-35236: step limit: infinite while loop is terminated
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {while {1} {}}]
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-limits-1.2 {
  R-42761-35236: step limit: step count near limit after exhaustion
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {while {1} {}}]
  # Step count should be at or just over 1,000,000
  expr {[sandboxSteps $r] >= 1000000}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-limits-1.3 {
  R-42761-35236: step limit: simple script stays well under limit
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {expr {2 + 2}}]
  list [sandboxRc $r] [sandboxResult $r] [expr {[sandboxSteps $r] < 1000}]
} -cleanup {
  unset -nocomplain r
} -result {0 4 1}}

###############################################################################

runTest {test sandbox-limits-2.1 {
  R-51616-58234: result limit: oversized string result is rejected
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {string repeat A 1100000}]
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-limits-2.2 {
  R-51616-58234: result limit: error message mentions string too long
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {string repeat A 1100000}]
  string match {*too long*} [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-limits-2.3 {
  R-51616-58234: result limit: string just under limit succeeds
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {string length [string repeat A 900000]}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 900000}}

###############################################################################

runTest {test sandbox-limits-3.1 {
  R-12425-26970: memory limit: allocation tracking reports reasonable usage
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r alloc
} -body {
  # Verify that sandbox tracks memory and stays under limit
  # for a moderate allocation.
  set r [::th8testlib::sandbox {
    string length [string repeat A 500000]
  }]
  set alloc [sandboxAllocCount $r]
  list [sandboxRc $r] [sandboxResult $r] [expr {$alloc > 0}]
} -cleanup {
  unset -nocomplain r alloc
} -result {0 500000 1}}

###############################################################################

runTest {test sandbox-limits-3.2 {
  R-12425-26970: memory limit: moderate allocation succeeds
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    string length [string repeat A 100000]
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 100000}}

###############################################################################

runTest {test sandbox-limits-3.3 {
  R-12425-26970: PEAK (high-water) allocation accounting (TH8K-021) -- the
  sandbox reports the maximum TRANSIENT memory a workload demanded, not only the
  residual after cleanup.  A workload that builds a large string and then frees
  it leaves a small current allocation but a large peak: the peak must be at
  least the transient's size AND strictly greater than the post-cleanup current
  count, proving the high-water mark is retained across the free.
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    set big [string repeat A 500000]
    string length $big
    set big ""
    return done
  }]
  list [sandboxRc $r] [sandboxResult $r] \
      [expr {[sandboxPeak $r] >= 500000 \
                 && [sandboxAllocCount $r] < [sandboxPeak $r]}]
} -cleanup {
  unset -nocomplain r
} -result {0 done 1}}

###############################################################################

runTest {test sandbox-limits-4.1 {
  R-24231-13066: eval depth: deeply nested proc recursion is caught
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    proc bomb {} { bomb }
    catch {bomb} msg
    expr {$msg ne ""}
  }]
  list [sandboxRc $r] [expr {[sandboxResult $r] != 0}]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-limits-4.2 {
  R-24231-13066: eval depth: nested eval chain is caught
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::sandbox {
    proc deep {n} {
      if {$n > 0} then { eval [list deep [expr {$n - 1}]] }
    }
    catch {deep 5000} msg
    expr {$msg ne ""}
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 1}}

###############################################################################

runTest {test sandbox-limits-5.1 {
  R-12425-26970: memory-limit allocation tracking is OVERFLOW-SAFE (TH8K-023).
  The per-interpreter allocation counter is incremented by the allocator's
  reported usable size after each successful allocation; that add must never
  wrap size_t -- a wrap would make the counter falsely small and silently
  collapse the memory ceiling.  ::th8testlib::alloc_account_overflow primes a
  child's counter to a few bytes below the size_t ceiling and performs a normal
  allocation whose usable size overflows the add: a correct implementation
  SATURATES the counter at the ceiling (poisoning further allocation) instead
  of wrapping to a small value.
} -constraints {
    th8 sandbox
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::alloc_account_overflow]
  # "saturated" is the correct outcome; tolerate "skip:alloc-failed" on a host
  # where the 64-byte probe allocation itself fails.
  expr {$r eq "saturated" || $r eq "skip:alloc-failed"}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
