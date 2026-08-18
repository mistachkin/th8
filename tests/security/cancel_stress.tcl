###############################################################################
#
# cancel_stress.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Concurrent-cancellation stress for the lock-free cross-thread cancel request
# (TH8K-008).  Th8_CancelEval is callable from any thread; a foreign thread
# publishes a coherent (message, length, flags) request under a bounded CAS
# try-spinlock, which the owner adopts at its next poll.  The earlier
# single-joined-canceller test (apicontract-10.2) did not exercise simultaneous
# publishers or publisher-vs-owner races; ::th8testlib::cancel_stress does:
# several foreign threads cancel one child interpreter at once (each with a
# distinct static message) while the owner races them by adopting
# (Th8_IsCanceled) and clearing (Th8_ResetCancel).  Every observed cancellation
# message must be a COHERENT known message -- a torn pointer/length (the pre-fix
# race) would yield a wrong length or garbage.  Also runnable under
# ThreadSanitizer via `make tsan-cancel`.  th8-constrained: driven by the TH8
# C-API cross-thread cancellation path via testlib.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test cancel_stress-1.1 {
  TH8K-008: concurrent multi-publisher cross-thread cancellation is race-free.
  ::th8testlib::cancel_stress spawns several foreign threads that cancel one
  child interpreter simultaneously (each with a distinct static message) while
  the owning thread races them by adopting and clearing the cancellation.  It
  returns "ok" when every observed cancellation reported a COHERENT known
  message (no torn pointer/length) and at least one cancellation was observed,
  or a "skip:..." token where threads or thread ids are unavailable.
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::cancel_stress]
  expr {$r eq "ok" || [string match skip:* $r]}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
