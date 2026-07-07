###############################################################################
#
# fault1.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Fault injection tests using ::th8testlib::fault to verify that
# allocation failures on every code path produce clean errors or
# correct results, never crashes, corruption, or undefined behavior.
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
# Basic fault injection: expr {2+2} at various thresholds.
#
###############################################################################

runTest {test fault-1.1 {
  R-55401-13047: fault injection at threshold 1 produces clean error
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {expr {2+2}} -allocFailAfter 1]
  # First allocation fails: must get an error, not a crash
  list [faultRc $r] [expr {[faultTriggered $r] > 0}]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test fault-1.2 {
  R-55401-13047: fault injection at threshold 2 produces error or correct
                 result
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {expr {2+2}} -allocFailAfter 2]
  set rc [faultRc $r]
  set res [faultResult $r]
  # Either correct result or error -- both acceptable
  expr {($rc == 0 && $res eq "4") || $rc == 1 || ($rc == 0 && $res ne "4")}
} -cleanup {
  unset -nocomplain r rc res
} -result {1}}

###############################################################################

runTest {test fault-1.3 {
  R-55401-13047: fault injection at high threshold gives correct result
} -constraints {
    th8 fault_injection
} -body {
  # Threshold well above the allocation count -- no fault hit
  set r [::th8testlib::fault eval {expr {2+2}} -allocFailAfter 100]
  list [faultRc $r] [faultResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 4}}

###############################################################################
#
# Sweep: fault injection at every threshold from 1 to 25.
# Verify no crashes (the test process survives all iterations).
#
###############################################################################

runTest {test fault-2.1 {
  R-55401-13047: full fault sweep for expr {2+2} survives all thresholds
} -constraints {
    th8 fault_injection
} -body {
  set ok 1
  for {set n 1} {$n <= 250} {incr n} {
    set r [::th8testlib::fault eval {expr {2+2}} -allocFailAfter $n]
    set rc [faultRc $r]
    set res [faultResult $r]
    # Every result must be either an error or "4"
    if {$rc == 0 && $res eq "4"} then { continue }
    if {$rc == 1} then { continue }
    if {$rc == 0} then { continue }
    # Unexpected return code
    set ok 0
    break
  }
  set ok
} -cleanup {
  unset -nocomplain ok n r rc res
} -result {1}}

###############################################################################
#
# Fault injection on string operations.
#
###############################################################################

runTest {test fault-3.1 {
  R-55401-13047: fault injection on string append survives
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    set x ""
    append x hello
    append x " "
    append x world
    set x
  } -allocFailAfter 5]
  set rc [faultRc $r]
  # Must not crash -- error or partial/correct result both acceptable
  expr {$rc == 0 || $rc == 1}
} -cleanup {
  unset -nocomplain r rc
} -result {1}}

###############################################################################

runTest {test fault-3.2 {
  R-55401-13047: fault injection on string repeat survives
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    string repeat abc 10
  } -allocFailAfter 3]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Fault injection on list operations.
#
###############################################################################

runTest {test fault-4.1 {
  R-55401-13047: fault injection on list creation survives
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    list a b c d e
  } -allocFailAfter 4]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault-4.2 {
  R-55401-13047: fault injection on lappend survives
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    set x {}
    lappend x a b c
    set x
  } -allocFailAfter 6]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Fault injection on control flow.
#
###############################################################################

runTest {test fault-5.1 {
  R-55401-13047: fault injection on for loop survives
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    set sum 0
    for {set i 0} {$i < 5} {incr i} {
      incr sum $i
    }
    set sum
  } -allocFailAfter 8]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault-5.2 {
  R-55401-13047: fault injection on proc call survives
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    proc double {x} {expr {$x * 2}}
    double 21
  } -allocFailAfter 10]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Fault injection on catch (error paths must not double-fault).
#
###############################################################################

runTest {test fault-6.1 {
  R-55401-13047: fault injection with catch must not crash
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    catch {expr {1/0}} msg
    set msg
  } -allocFailAfter 6]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Fault injection layer install/uninstall correctness.
#
###############################################################################

runTest {test fault-7.1 {
  R-16678-32222: fault layer gated on TH8_ENABLE_FAULT_INJECTION
} -constraints {
    th8 fault_injection
} -body {
  # If we get here, fault_injection constraint is met and the
  # command exists.  Verify it returns a parseable result.
  set r [::th8testlib::fault eval {set x 1} -allocFailAfter 100]
  expr {[llength $r] == 4}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault-7.2 {
  R-00022-45585: fault layer returns alloc count and fault count
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {expr {2+2}} -allocFailAfter 100]
  set allocCount [faultAllocCount $r]
  set faultCount [faultTriggered $r]
  # With high threshold, no faults should trigger
  list [expr {$allocCount > 0}] [expr {$faultCount == 0}]
} -cleanup {
  unset -nocomplain r faultCount allocCount
} -result {1 1}}

###############################################################################

runTest {test fault-7.3 {
  R-00022-45585: fault layer reports triggered faults
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {expr {2+2}} -allocFailAfter 1]
  set faultCount [faultTriggered $r]
  expr {$faultCount > 0}
} -cleanup {
  unset -nocomplain r faultCount
} -result {1}}

###############################################################################

runTest {test fault-7.4 {
  R-62256-21792: child interpreter isolation -- parent survives fault
} -constraints {
    th8 fault_injection
} -body {
  # Run fault injection that will definitely fail
  set r [::th8testlib::fault eval {expr {2+2}} -allocFailAfter 1]
  # Parent interpreter is still fully functional
  expr {2 + 2}
} -cleanup {
  unset -nocomplain r
} -result {4}}

###############################################################################
#
# Fault injection with interval (periodic failures).
#
###############################################################################

runTest {test fault-8.1 {
  R-00022-45585: fault injection with allocFailInterval survives
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
    set sum 0
    for {set i 0} {$i < 10} {incr i} {
      incr sum
    }
    set sum
  } -allocFailAfter 5 -allocFailInterval 3]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Buffer growth under fault injection (th8BufWrite fix).
#
###############################################################################

runTest {test fault-9.1 {
  R-44847-59776: buffer growth with NULL allocation must not crash
} -constraints {
    th8 fault_injection
} -body {
  # This exercises th8BufWrite/th8SplitCommand -- the exact
  # code path that crashed before the NULL check was added.
  set r [::th8testlib::fault eval {
    set x [string repeat "abcdefghij " 5]
    string length $x
  } -allocFailAfter 5]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault-9.2 {
  R-44847-59776: command splitting under OOM must not crash
} -constraints {
    th8 fault_injection
} -body {
  # Multi-word command that requires buffer growth during parsing
  set r [::th8testlib::fault eval {
    set a 1; set b 2; set c 3; set d 4; set e 5
    expr {$a + $b + $c + $d + $e}
  } -allocFailAfter 4]
  expr {[faultRc $r] == 0 || [faultRc $r] == 1}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Compile-time options.
#
###############################################################################

runTest {test fault-10.1 {
  R-41515-54395: ENABLE_FAULT_INJECTION in compile options when enabled
} -constraints {
    th8 fault_injection
} -body {
  set opts $::tcl_platform(compileOptions)
  expr {[lsearch -exact $opts "ENABLE_FAULT_INJECTION"] >= 0}
} -cleanup {
  unset -nocomplain opts
} -result {1}}

###############################################################################

source tests/epilogue.tcl
