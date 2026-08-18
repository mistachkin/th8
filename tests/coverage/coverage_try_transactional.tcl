###############################################################################
#
# coverage_try_transactional.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Probative checks for the [try]/[finally] command's transactional and
# allocation-accounting properties.  ::th8testlib::try_transactional runs four
# checks in throwaway child interpreters and returns {acct noleak cancelfree
# oom}; the correct result is {1 1 1 1}:
#
#   acct (TH8K-023/-018): memory a finally block leaves LIVE is charged to the
#     interpreter after the block completes -- the fresh finally budget must not
#     let the counter reset silently bypass Th8_SetAllocLimit.  Pre-fix the
#     counter was restored to the pre-finally value, hiding the live bytes, so
#     acct==0.
#   noleak (TH8K-020): a [try] with NO finally clause makes no copy of its
#     result (it already sits in the interpreter); pre-fix it copied and leaked
#     the copy, so noleak==0.
#   cancelfree (TH8K-008): when a finally block's cancellation supersedes a
#     saved cancellation, the saved OWNED message (moved out of the interpreter
#     by th8SaveCancel) is freed; pre-fix it leaked, so cancelfree==0.
#   oom (TH8K-020): under a one-shot allocation fault swept across the [try]
#     evaluation, a fired fault never yields success with a silently lost
#     result; pre-fix the unchecked result copy could leave a NULL/empty result
#     reported as success, so oom==0.
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

runTest {test try_transactional-1.1 {
  [try]/[finally] is transactional and accounts finally's live allocations:
  finally residue is charged (acct==1), a no-finally try does not leak its
  result copy (noleak==1), a superseded owned cancel message is freed
  (cancelfree==1), and a one-shot OOM during the try never loses the result
  silently (oom==1).  The result must be {1 1 1 1}.
} -constraints {
    loadLib th8
} -setup {
  unset -nocomplain r
} -body {
  set r [::th8testlib::try_transactional]
} -cleanup {
  unset -nocomplain r
} -result {1 1 1 1}}

###############################################################################

source tests/epilogue.tcl
