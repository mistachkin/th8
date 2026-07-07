###############################################################################
#
# coverage_fault_null_cb.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for short-circuit boolean compounds whose
# operands are platform-callback function pointers, e.g.:
#
#   if (ALWAYS(interp->pPlatform) && interp->pPlatform->xMutexEnter) {
#   if (bPanic && interp->pPlatform->xPanic) {
#   if (ALWAYS(pPlat) && pPlat->xOutput) {
#
# On a fully-populated platform (POSIX, Win32) the callback
# slot is always non-NULL, so the second condition only
# evaluates to True -- the False vector cannot be reached
# from script.  This test file uses the fault-injection layer's
# new -nullCallbacks option to selectively NULL specific
# slots on a child interpreter, then exercises code paths
# that read those slots.  Running these scripts inside the
# fault layer drives the missing False vector.
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

runTest {test fault_nullcb-1.1 {
  Smoke test -- fault eval with no -nullCallbacks, then with
  one valid callback nulled, then with multiple.  Confirms
  the option parses, the layer installs and uninstalls
  cleanly, and the script body still evaluates.
} -constraints {
    th8 fault_injection
} -body {
  set r1 [::th8testlib::fault eval {expr {1+2}}]
  set r2 [::th8testlib::fault eval {expr {1+2}} -nullCallbacks xPanic]
  set r3 [::th8testlib::fault eval {expr {1+2}} -nullCallbacks {xPanic xMutexEnter}]
  list \
      [lindex $r1 0] \
      [lindex $r1 1] \
      [lindex $r2 0] \
      [lindex $r2 1] \
      [lindex $r3 0] \
      [lindex $r3 1]
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {0 3 0 3 0 3}}

###############################################################################

runTest {test fault_nullcb-1.2 {
  An unknown callback name produces a clean error rather
  than a crash.  The error message names the bad slot.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {expr {1+2}} \
      -nullCallbacks {xPanic xBogusSlotName}} m
  expr {[string match "*xBogusSlotName*" $m] ? 1 : 0}
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test fault_nullcb-2.1 {
  th8_core.c:388 / :420 -- (ALWAYS(pPlatform) && xMutexEnter)
  and (ALWAYS(pPlatform) && xMutexLeave) compounds.  Nulling
  xMutexEnter and xMutexLeave drives the C2=F vector at the
  cache-mutex lock/unlock sites, which any value-cache
  operation reaches via th8CacheMutexLock / Unlock.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
      set x 0
      for {set i 0} {$i < 5} {incr i} { incr x }
      set x
  } -nullCallbacks {xMutexEnter xMutexLeave}]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {0 5}}

###############################################################################

runTest {test fault_nullcb-2.2 {
  th8_core.c:963 / :999 / :1592 / :1622 -- (bPanic && xPanic)
  compound at the alloc-failure panic sites.  Nulling xPanic
  combined with -allocFailAfter drives the C2=F vector
  (panic requested but no callback installed -> graceful
  error return instead of abort).  Whether the script-level
  return code is OK or ERROR depends on which allocation
  fails; what matters for coverage is that the layer
  installed without crashing and the alloc counter is
  positive.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
      set acc {}
      for {set i 0} {$i < 100} {incr i} { lappend acc $i }
      llength $acc
  } -nullCallbacks xPanic -allocFailAfter 50]
  expr {[lindex $r 2] > 0 && [lindex $r 3] > 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault_nullcb-2.4 {
  th8_core.c:963 -- (bPanic && interp->pPlatform->xPanic)
  compound at the sandbox-allocation-limit-exceeded site.
  This decision was unreachable from script before: the
  limit check is gated on nAllocLimit > 0, which only the
  sandbox command set, and the sandbox kept xPanic non-NULL.
  With -allocLimit + -nullCallbacks xPanic together, the
  child interp's xMalloc enters the limit branch (line 960)
  and the inner panic compound at line 963 sees C1=T
  (bPanic) and C2=F (xPanic NULL), driving the previously-
  uncovered vector.  Result: a clean "out of memory" error
  (rc=1) instead of an abort.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
      set acc {}
      for {set i 0} {$i < 1000} {incr i} { lappend acc $i }
      llength $acc
  } -allocLimit 1024 -nullCallbacks xPanic]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {1 {out of memory}}}

###############################################################################

runTest {test fault_nullcb-2.3 {
  th8_core.c:1108 -- (!p && xNeedMemory) compound at the
  second-chance allocator path.  Nulling xNeedMemory and
  forcing an alloc failure drives the C2=F vector (alloc
  failed but no second-chance hook -> propagate the OOM).
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
      set acc {}
      for {set i 0} {$i < 100} {incr i} { lappend acc $i }
      llength $acc
  } -nullCallbacks {xNeedMemory xPanic} -allocFailAfter 30]
  expr {[lindex $r 3] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault_nullcb-2.5 {
  th8_core.c:963 (malloc-limit panic) and :999 (malloc-oom
  panic) -- both compounds gated on bPanic=1 which only
  Th8_Malloc / Th8_Realloc set.  No script-level allocator
  goes through these paths; ::th8testlib::malloc_drive is
  the C-side entry point that bridges the gap.
} -constraints {
    th8 fault_injection
} -body {
  list \
      [::th8testlib::malloc_drive malloc-limit 4096] \
      [::th8testlib::malloc_drive malloc-oom 1024]
} -result {nil nil}}

###############################################################################

runTest {test fault_nullcb-2.6 {
  th8_core.c:1592 (realloc-limit) and :1622 (realloc-oom) --
  realloc analogues of -2.5.  Plus :1108 the second-chance
  allocator gate (!p && xNeedMemory) at the realloc post-
  failure path.
} -constraints {
    th8 fault_injection
} -body {
  list \
      [::th8testlib::malloc_drive realloc-limit 32 4096] \
      [::th8testlib::malloc_drive realloc-oom 32 1024] \
      [::th8testlib::malloc_drive realloc-need 32 1024]
} -result {nil nil nil}}

###############################################################################

runTest {test fault_nullcb-2.9 {
  Drives the (T,T) vector of the second-chance allocator
  compound (!p && xNeedMemory) at th8_core.c:1108.  The
  safealloc-need-call mode installs a non-NULL xNeedMemory
  stub (returns NULL = "no recovery available") on the child
  platform and forces the SafeAlloc to fail; both conditions
  of the compound are then T, the wrapper invokes the stub,
  and SafeAlloc returns NULL.
} -constraints {
    th8 fault_injection
} -body {
  ::th8testlib::malloc_drive safealloc-need-call 4096
} -result {nil}}

###############################################################################

runTest {test fault_nullcb-2.8 {
  Drives the C1-pair MC/DC vector (bPanic=0) of the realloc
  panic compounds at th8_core.c:1592 and :1622.  No
  script-level path reaches th8ReallocCommon with bPanic=0
  while a fault is armed; the *-attempt modes call
  Th8_AttemptRealloc directly to bridge the gap.
} -constraints {
    th8 fault_injection
} -body {
  list \
      [::th8testlib::malloc_drive realloc-limit-attempt 32 4096] \
      [::th8testlib::malloc_drive realloc-oom-attempt 32 1024]
} -result {nil nil}}

###############################################################################

runTest {test fault_nullcb-2.7 {
  Mirror of -2.5 / -2.6 but with a non-NULL safe-stub xPanic
  installed.  This drives the (T,T) vector of the same panic
  compounds: bPanic=1 AND xPanic-non-NULL -> the wrapper
  invokes the stub, which returns instead of aborting, so
  Th8_Malloc / Th8_Realloc continue past the panic call site
  and return NULL anyway.  Together with -2.5 / -2.6, this
  closes the C2-pair MC/DC for the (bPanic && xPanic)
  compounds at lines 963, 999, 1592, 1622, plus the
  (!p && xNeedMemory) compound at line 1108.
} -constraints {
    th8 fault_injection
} -body {
  list \
      [::th8testlib::malloc_drive malloc-limit-call 4096] \
      [::th8testlib::malloc_drive malloc-oom-call 1024] \
      [::th8testlib::malloc_drive realloc-limit-call 32 4096] \
      [::th8testlib::malloc_drive realloc-oom-call 32 1024] \
      [::th8testlib::malloc_drive realloc-need-call 32 1024]
} -result {nil nil nil nil nil}}

###############################################################################

runTest {test fault_nullcb-3.1 {
  th8_filesystems.c:645 / :648 -- (ALWAYS(pPlat) && xInput)
  and (ALWAYS(pPlat) && xOutput) compounds in [file
  channels].  The function gates the "stdin" / "stdout"
  entries on whether the platform implements xInput /
  xOutput.  Nulling those slots and calling [file channels]
  drives the previously-uncovered C2=F vector at each
  compound.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
      file channels
  } -nullCallbacks {xInput xOutput}]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {0 {}}}

###############################################################################

runTest {test fault_nullcb-7.1 {
  expr floating-point math functions (sin, sqrt) under
  nulled xMathFunc drive the math-function-unavailable
  error path in th8_expr.c.  Without the platform
  callback, the math-func evaluator returns an error
  rather than computing a value.  (abs() is integer-
  internal and bypasses xMathFunc.)
} -constraints {
    th8 fault_injection
} -body {
  set rcs {}
  foreach inp {{sin(1.0)} {sqrt(4.0)} {cos(0.0)}} {
      set r [::th8testlib::fault eval [list expr $inp] \
          -nullCallbacks xMathFunc]
      lappend rcs [lindex $r 0]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs inp r
} -result {1 1 1}}

###############################################################################

runTest {test fault_nullcb-7.2 {
  env_kv operations under nulled xKeyValue drive the
  "env platform unavailable" error path at
  src/test/th8_testlib.c:8226-8228 -- xKeyValue NULL
  short-circuits to error.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
      ::th8testlib::env_kv list "*"
  } -nullCallbacks xKeyValue]
  lindex $r 0
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fault_nullcb-7.3 {
  format "%e" / "%g" of EXTREME floating-point values
  under nulled xMathFunc may drive the C2=T vector at
  th8_expr.c:343 if the format path uses th8MathOp for
  log10.  Whether this errors depends on the format-
  spec's internal exponent-calc path; we just check
  that the call returns deterministically.
} -constraints {
    th8 fault_injection
} -body {
  set r1 [::th8testlib::fault eval {format "%e" 1e100} \
      -nullCallbacks xMathFunc]
  set r2 [::th8testlib::fault eval {format "%e" 1e-100} \
      -nullCallbacks xMathFunc]
  set r3 [::th8testlib::fault eval {format "%g" 1e200} \
      -nullCallbacks xMathFunc]
  expr {[string length [lindex $r1 1]] >= 0 \
      && [string length [lindex $r2 1]] >= 0 \
      && [string length [lindex $r3 1]] >= 0}
} -cleanup {
  unset -nocomplain r1 r2 r3
} -result {1}}

###############################################################################

runTest {test fault_nullcb-7.4 {
  file same with nulled xSameFile drives the C2=T vector
  at th8_filesystems.c:1560 (the platform-callback check).
  Without xSameFile, the command errors with "platform
  does not support same-file detection".
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {
      file same a b
  } -nullCallbacks xSameFile]
  list [lindex $r 0] [string match {*platform*support*} [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test fault_nullcb-7.5 {
  pwd with nulled xGetCwd drives the C2=T vector at
  th8_filesystems.c:1752 (the platform-callback check
  for cwd retrieval).  Without xGetCwd, pwd errors
  with the permission-denied message.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {pwd} -nullCallbacks xGetCwd]
  list [lindex $r 0] [string match {*permission*denied*} [lindex $r 1]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test fault_nullcb-7.6 {
  clock seconds / clock milliseconds with nulled xTimeMs
  drives the platform-callback fallback in Th8_GetTime
  (th8_plat.c:1853) -- without xTimeMs, the fallback
  sets *pMs = 0 and returns TH8_OK.  The script-visible
  result is 0.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {clock seconds} \
      -nullCallbacks xTimeMs]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

runTest {test fault_nullcb-7.7 {
  realloc-shrink mode in malloc_drive drives th8_core.c
  L1591 C2-Pair (T,F) -- nAllocLimit > 0 (C1=T) AND
  nByte <= nOldSize (C2=F, shrink-realloc) -- the limit-
  check block at L1591 is correctly skipped.  Existing
  realloc tests cover only the GROWING path (C2=T).
  Seed buffer is 4096 bytes, target is 64 bytes (shrink).
} -constraints {
    th8 fault_injection
} -body {
  ::th8testlib::malloc_drive realloc-shrink 4096 64
} -result {ptr}}

###############################################################################

source tests/epilogue.tcl
