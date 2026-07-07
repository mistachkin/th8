###############################################################################
#
# coverage_ns_eval_multi.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Drives the multi-script-arg concat path in namespace_eval_command
# (th8_management.c:283-304).  Single-arg form goes through
# Th8_NsEval directly; multi-arg form concatenates with " " between
# the script args and then invokes Th8_NsEval on the result.
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

runTest {test ns_eval_multi-1.1 {
  namespace eval with two body args concatenates them with
  a space separator; explicit `;` terminator in each body
  makes the joined string a valid two-command script.
  Uses procs (runtime-enabled) instead of variables.
} -constraints {
    th8
} -body {
  namespace eval ::th8test_nsemulti1 \
      "proc f1 {} {return 7};" "proc f2 {} {return 5};"
  list [::th8test_nsemulti1::f1] [::th8test_nsemulti1::f2]
} -cleanup {
  namespace delete ::th8test_nsemulti1
} -result {7 5}}

###############################################################################

runTest {test ns_eval_multi-1.2 {
  namespace eval with three body args concatenates all three
  with " " separators between them.
} -constraints {
    th8
} -body {
  namespace eval ::th8test_nsemulti2 \
      "proc a {} {return 1};" \
      "proc b {} {return 2};" \
      "proc c {} {return 3};"
  list [::th8test_nsemulti2::a] \
       [::th8test_nsemulti2::b] \
       [::th8test_nsemulti2::c]
} -cleanup {
  namespace delete ::th8test_nsemulti2
} -result {1 2 3}}

###############################################################################

source tests/epilogue.tcl
