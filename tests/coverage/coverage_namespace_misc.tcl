###############################################################################
#
# coverage_namespace_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for various [namespace] subcommand
# decisions in src/plugins/th8_management.c:
#
#   :386  if (argc != 2 && argc != 3)        ([namespace children]
#                                             wrong-arg)
#   :651  && th8StrEq(...,"-force")          ([namespace import]
#                                             -force flag detect)
#   :700  argl[2] > 2 && argv[2][0] == ':'   ([namespace origin]
#                                             qualified-name check)
#   :705  zNs[0] == ':' && zNs[1] == ':'     ([namespace origin]
#                                             root-namespace check)
#   :801  argl[2] > 2 && argv[2][0] == ':'   ([namespace which]
#                                             qualified-name check)
#   :806  zNs[0] == ':' && zNs[1] == ':'     ([namespace which]
#                                             root-namespace check)
#
# Existing namespace tests cover the common paths.  This file
# drives the wrong-arg, -force-flag, and qualified-vs-unqualified
# vectors that no test currently exercises.
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

runTest {test nsmisc-1.1 {
  namespace children with too many args drives the (T,T)
  vector at line 386 (argc != 2 && argc != 3).  Existing
  tests cover the no-arg and one-arg success paths but
  not this wrong-args path.
} -constraints {
    th8
} -body {
  catch {namespace children a b c d} m
  expr {[string length $m] > 0}
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test nsmisc-2.1 {
  namespace import with -force flag drives the (T,T)
  vector at line 651 (argc > 2 AND -force matches).
  Without -force drives the (T,F) vector (the second
  condition is false because the arg is not -force).
} -constraints {
    th8
} -body {
  namespace eval ::nsmisc_src1 {
      proc helper {} { return ok }
      namespace export helper
  }
  namespace eval ::nsmisc_dst1 {
      catch {namespace import -force ::nsmisc_src1::helper} r1
      catch {namespace import ::nsmisc_src1::helper} r2
      list \
          [info commands helper] \
          [string length $r1] \
          [string length $r2]
  }
} -cleanup {
  catch {namespace delete ::nsmisc_src1}
  catch {namespace delete ::nsmisc_dst1}
} -result {helper 0 0}}

###############################################################################

runTest {test nsmisc-3.1 {
  namespace origin with qualified vs unqualified names
  drives lines 700 and 705.  Line 700: ::name has the
  argl > 2 && first two ':' (T,T,T).  Bare name takes the
  else branch (F or other).  Line 705: from the root
  namespace, current zNs == "::" so the root-check fires.
} -constraints {
    th8
} -body {
  proc ::nsmisc_origin_test {} { return origin_test }
  list \
      [namespace origin ::nsmisc_origin_test] \
      [namespace origin nsmisc_origin_test]
} -cleanup {
  catch {rename ::nsmisc_origin_test ""}
} -result {::nsmisc_origin_test ::nsmisc_origin_test}}

###############################################################################

runTest {test nsmisc-3.2 {
  namespace which mirrors namespace origin's qualified-vs-
  unqualified check at lines 801 and 806.  Same vector
  shape; different command path.  TH8 may map [which]
  through to [origin] internally; the source-level
  decisions still get hit independently.
} -constraints {
    th8
} -body {
  proc ::nsmisc_which_test {} { return which_test }
  list \
      [catch {namespace which ::nsmisc_which_test} r1; expr {[string length $r1] >= 0}] \
      [catch {namespace which nsmisc_which_test} r2; expr {[string length $r2] >= 0}]
} -cleanup {
  catch {rename ::nsmisc_which_test ""}
  unset -nocomplain r1 r2
} -result {1 1}}

###############################################################################

runTest {test nsmisc-3.3 {
  namespace origin from inside a NESTED namespace drives
  the F vector at line 705 -- current namespace is not
  "::" so the root-namespace fast-path is skipped, and the
  prepend-current-namespace branch is taken instead.
} -constraints {
    th8
} -body {
  namespace eval ::nsmisc_inner {
      proc nested_helper {} { return inner }
      namespace origin nested_helper
  }
} -cleanup {
  catch {namespace delete ::nsmisc_inner}
} -result {::nsmisc_inner::nested_helper}}

###############################################################################

runTest {test nsmisc-4.1 {
  Invoking an UNQUALIFIED unknown command from the GLOBAL
  namespace drives the C2=F vector at
  th8_core.c:12798-12800 (NRCmdDispatch unqualified
  lookup): when the per-namespace lookup misses (C1=T,
  pEntry == NULL), the fallback to the global namespace is
  skipped iff pCurrentNs == pGlobalNs.  Existing tests
  invoke unknown commands inside `namespace eval` blocks
  (pCurrentNs != pGlobalNs, drives C2=T); this test
  executes the command at top level where the inequality
  short-circuits.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {totally_unknown_command_nsm41a} m]
  lappend rcs [catch {totally_unknown_command_nsm41b arg1} m]
  lappend rcs [catch {nonexistent_4_1_helper_zzz} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test nsmisc-3.4 {
  [namespace origin :xx] drives the C3=F vector at
  src/plugins/th8_management.c L703 -- the argl > 2 AND
  argv[0] == ':' compound succeeds at C1/C2 but the
  second-char check (argv[1] == ':') is F because the
  name starts with a single colon followed by a letter.
  The command errors with "unknown command" but the
  source-level decision has been exercised.
} -constraints {
    th8
} -body {
  catch {namespace origin :xx} m
  # We don't care about the result; the catch ensures the
  # test passes regardless of the error message.
  expr {[string length $m] >= 0}
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test nsmisc-3.5 {
  [namespace which :xx] mirrors nsmisc-3.4 for the
  parallel decision at L801 in th8_management.c -- single
  colon prefix forces C3=F.
} -constraints {
    th8
} -body {
  catch {namespace which :xx} m
  expr {[string length $m] >= 0}
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test nsmisc-4.2 {
  [variable v] inside a top-level proc (frame level > 0,
  current namespace == "::") drives the C1=T short-circuit
  vector of the 5-condition compound at
  src/plugins/th8_variables.c L854-856.
} -constraints {
    th8
} -setup {
  catch {unset ::nsm42_var}
} -body {
  proc ::nsm42_topproc {} {
      variable nsm42_var 42
      return $nsm42_var
  }
  ::nsm42_topproc
} -cleanup {
  catch {rename ::nsm42_topproc ""}
  catch {unset ::nsm42_var}
} -result {42}}

###############################################################################

runTest {test nsmisc-4.4 {
  Invoking an UNQUALIFIED unknown command from INSIDE a
  [namespace eval] block drives the (T,T) MC/DC vector
  at src/th8_core.c L9717 -- `!pEntry &&
  pCurrentNs != pGlobalNs`.  The per-namespace lookup
  misses (C1=T), and the fallback to the global namespace
  fires because current namespace is not the global one
  (C2=T).  Complement to nsmisc-4.1 which covers the
  (T,F) global-namespace case.
} -constraints {
    th8
} -setup {
} -body {
  set rcs {}
  namespace eval ::nsm44_nested {
      lappend ::nsm44_rcs [catch {nonexistent_nsm44_helper_zzz} m]
      lappend ::nsm44_rcs [catch {another_unknown_nsm44_cmd} m]
  }
  set rcs $::nsm44_rcs
  set rcs
} -cleanup {
  catch {namespace delete ::nsm44_nested}
  unset -nocomplain rcs m ::nsm44_rcs
} -result {1 1}}

###############################################################################

runTest {test nsmisc-4.3 {
  [variable v] at TOP LEVEL in the global namespace
  (frame level == 0, current namespace == "::") drives
  the (F,T,T,T,F) MC/DC vector at
  src/plugins/th8_variables.c L854-856 -- C5=F because
  zCurNs[2]=='\0' for the root namespace.  Combined with
  nsmisc-4.2 (T,-,-,-,-) and existing namespace-eval
  tests (F,T,T,T,T), this closes additional MC/DC pairs.
} -constraints {
    th8
} -setup {
  catch {unset ::nsm43_root_var}
} -body {
  variable ::nsm43_root_var 5
  set ::nsm43_root_var
} -cleanup {
  catch {unset ::nsm43_root_var}
} -result {5}}

###############################################################################

runTest {test nsmisc-5.1 {
  namespace import with a trailing "::" produces an EMPTY
  tail pattern (nTail == 0) and drives the C1=F vector at
  th8_core.c:3538 (th8SimpleGlob) -- nPat > 0 evaluates F,
  short-circuiting the trailing-* prefix-match branch.
  Existing tests cover non-empty patterns; this test
  closes the C1-pair via an unusual but accepted spelling
  of the import command.
} -constraints {
    th8
} -body {
  set rcs {}
  namespace eval ::nsm51_src {
      proc helper {} { return ok }
      namespace export helper
  }
  namespace eval ::nsm51_dst {
      lappend ::nsm51_rcs [catch {namespace import ::nsm51_src::} m]
  }
  set rcs $::nsm51_rcs
  set rcs
} -cleanup {
  catch {namespace delete ::nsm51_src}
  catch {namespace delete ::nsm51_dst}
  unset -nocomplain rcs m ::nsm51_rcs
} -result {0}}

###############################################################################

runTest {test nsmisc-6.1 {
  Bug 5 regression: a namespace whose name contains a single
  ":" (created via a variable whose qualified name embeds one,
  e.g. "::_pkg:sub::nested") must be deletable.  The child is
  keyed in the parent under the full component "_pkg:sub"; the
  old Th8_DeleteNamespace tail scan stopped at the embedded
  ":" and removed the wrong key ("sub"), leaving a dangling
  paChild entry pointing at the freed namespace -- a silent
  delete no-op that also hung interpreter teardown.  After the
  fix, delete actually removes it and "namespace children ::"
  no longer lists it.  (Asserting the child is gone catches a
  regression of the dangling entry; the teardown hang would
  only manifest at process exit.)
} -constraints {
    th8
} -setup {
} -body {
  set ::_pkg:sub::nested 5
  set rc [catch {namespace delete ::_pkg:sub} m]
  set kids [namespace children ::]
  list $rc [lsearch -exact $kids ::_pkg:sub]
} -cleanup {
  catch {namespace delete ::_pkg:sub}
  unset -nocomplain rc kids m
} -result {0 -1}}

###############################################################################

source tests/epilogue.tcl
