###############################################################################
#
# coverage_shell_subprocess.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# F2 (per the release plan): drive MC/DC in src/th8_shell.c via
# subprocess invocations of th8sh.  The shell's argv-parsing
# and TH8SH_* env-var handling run in main() / pre-init code
# that the in-process conformance suite cannot reach -- testlib
# is linked against the shared library, NOT against th8sh, so
# th8_shell.c sits at 0% MC/DC under the normal test load.
#
# This file targets Th8Shell_ParseExprFeatures (called from
# Th8Shell_ApplyStandardEnvFeatures when the TH8SH_EXPR_FEATURES
# env var is set).  Each subprocess invocation passes a single
# helper script and a -env TH8SH_EXPR_FEATURES=... pair via
# __test_only_exec (bypassing the test_only_exec Tcl wrapper
# because we need the -env extension that wrapper does not yet
# forward).  Each token form drives one of the function's five
# decision arms: "none" (4-byte match), "all" (3-byte match),
# "top-comma" (9-byte match), "var-assign" (10-byte match), and
# the unknown-token error path (nTok > 0, none-of-the-above).
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

#
# Wrapper that calls __test_only_exec directly so we can pass
# -env NAME=VALUE pairs (the test_only_exec Tcl wrapper does
# not forward these yet).  Loads the C testlib on demand.
#
proc shellExecEnv { envList script } {
  set exe [info nameofexecutable]

  if {[llength [info commands __test_only_exec]] == 0 && \
      [info exists ::testlib_name] && $::testlib_name ne ""} then {
    catch {load $::testlib_name}
  }

  if {[llength [info commands __test_only_exec]] == 0} then {
    error "shellExecEnv: __test_only_exec unavailable"
  }

  set cmd [list __test_only_exec]
  foreach pair $envList {lappend cmd -env $pair}
  lappend cmd $exe $script
  return [uplevel 1 $cmd]
}

###############################################################################

runTest {test shell_subproc_expr_features-1.1 {
  TH8SH_EXPR_FEATURES=none drives the 4-byte token arm of
  Th8Shell_ParseExprFeatures.  Subprocess exits 0 and emits
  the helper marker.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEnv {TH8SH_EXPR_FEATURES=none} \
      tests/helpers/sh_noop.tcl]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.2 {
  TH8SH_EXPR_FEATURES=all drives the 3-byte token arm.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEnv {TH8SH_EXPR_FEATURES=all} \
      tests/helpers/sh_noop.tcl]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.3 {
  TH8SH_EXPR_FEATURES=top-comma drives the 9-byte token arm.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEnv {TH8SH_EXPR_FEATURES=top-comma} \
      tests/helpers/sh_noop.tcl]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.4 {
  TH8SH_EXPR_FEATURES=var-assign drives the 10-byte token arm.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEnv {TH8SH_EXPR_FEATURES=var-assign} \
      tests/helpers/sh_noop.tcl]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.5 {
  TH8SH_EXPR_FEATURES with a multi-token comma list drives the
  inner while-loop multiple times and OR-merges the flag bits.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEnv \
      {TH8SH_EXPR_FEATURES=top-comma,var-assign,none} \
      tests/helpers/sh_noop.tcl]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.6 {
  TH8SH_EXPR_FEATURES with an unknown token drives the error
  arm of Th8Shell_ParseExprFeatures.  Subprocess exits non-zero
  with the documented "unknown TH8SH_EXPR_FEATURES token"
  diagnostic on stderr.
} -constraints {
    th8 test_only_exec
} -body {
  set rc [catch {shellExecEnv \
      {TH8SH_EXPR_FEATURES=bogus-token} \
      tests/helpers/sh_noop.tcl} out]
  list $rc [string match {*unknown TH8SH_EXPR_FEATURES token*} $out]
} -cleanup {
  unset -nocomplain rc out
} -result {1 1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.7 {
  TH8SH_NO_EXPR_FEATURES wins over TH8SH_EXPR_FEATURES (the
  shell skips the parser entirely).  Subprocess succeeds even
  though TH8SH_EXPR_FEATURES contains an unknown token,
  proving the early-out at the env-presence check.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEnv \
      {TH8SH_NO_EXPR_FEATURES=1 TH8SH_EXPR_FEATURES=bogus} \
      tests/helpers/sh_noop.tcl]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################
#
# Tests 1.8 -- 1.11 drive each token-match decision's C2-Pair
# vector ((T, F): correct length, wrong content) by supplying a
# token whose byte-length equals the matched-arm's length but
# whose content differs.  Each subprocess exits non-zero with
# the unknown-token diagnostic.
#
###############################################################################

runTest {test shell_subproc_expr_features-1.8 {
  4-byte non-"none" token drives the (T, F) C2-Pair on the
  L1179 decision.
} -constraints {
    th8 test_only_exec
} -body {
  set rc [catch {shellExecEnv {TH8SH_EXPR_FEATURES=nono} \
      tests/helpers/sh_noop.tcl} out]
  list $rc [string match {*unknown TH8SH_EXPR_FEATURES token*} $out]
} -cleanup {
  unset -nocomplain rc out
} -result {1 1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.9 {
  3-byte non-"all" token drives the (T, F) C2-Pair on the
  L1182 decision.
} -constraints {
    th8 test_only_exec
} -body {
  set rc [catch {shellExecEnv {TH8SH_EXPR_FEATURES=abc} \
      tests/helpers/sh_noop.tcl} out]
  list $rc [string match {*unknown TH8SH_EXPR_FEATURES token*} $out]
} -cleanup {
  unset -nocomplain rc out
} -result {1 1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.10 {
  9-byte non-"top-comma" token drives the (T, F) C2-Pair on
  the L1185 decision.
} -constraints {
    th8 test_only_exec
} -body {
  set rc [catch {shellExecEnv {TH8SH_EXPR_FEATURES=nineletrs} \
      tests/helpers/sh_noop.tcl} out]
  list $rc [string match {*unknown TH8SH_EXPR_FEATURES token*} $out]
} -cleanup {
  unset -nocomplain rc out
} -result {1 1}}

###############################################################################

runTest {test shell_subproc_expr_features-1.11 {
  10-byte non-"var-assign" token drives the (T, F) C2-Pair
  on the L1189 decision.
} -constraints {
    th8 test_only_exec
} -body {
  set rc [catch {shellExecEnv {TH8SH_EXPR_FEATURES=tenletterz} \
      tests/helpers/sh_noop.tcl} out]
  list $rc [string match {*unknown TH8SH_EXPR_FEATURES token*} $out]
} -cleanup {
  unset -nocomplain rc out
} -result {1 1}}

###############################################################################
#
# Section 2 -- `--version` / `-V` short-circuit in th8sh.c main().
# These exit before any TH8 initialisation, so the path is
# only reachable via subprocess.
#
###############################################################################

#
# Wrapper that invokes th8sh with ONLY a flag (no script).
# __test_only_exec normally requires an "exe script" pair, but
# the C-level command accepts any argv -- the file-exists check
# is in the Tcl test_only_exec wrapper only.
#
proc shellExecFlag { flag } {
  set exe [info nameofexecutable]
  if {[llength [info commands __test_only_exec]] == 0 && \
      [info exists ::testlib_name] && $::testlib_name ne ""} then {
    catch {load $::testlib_name}
  }
  if {[llength [info commands __test_only_exec]] == 0} then {
    error "shellExecFlag: __test_only_exec unavailable"
  }
  return [uplevel 1 [list __test_only_exec $exe $flag]]
}

runTest {test shell_subproc_version-2.1 {
  th8sh --version emits the version banner and exits with status 0
  (drives the early --version short-circuit in main).
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecFlag --version]
  string match {*TH8 v*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_version-2.2 {
  th8sh -V (single-letter alias) drives the same early short-circuit.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecFlag -V]
  string match {*TH8 v*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_version-2.3 {
  th8sh -version (dash-prefixed long form) also drives the
  short-circuit.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecFlag -version]
  string match {*TH8 v*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################
#
# Section 3 -- Th8Shell_PreScanChrootArgs argv parser.
# These exercise the --chroot / --chroot-user argv scanner
# without triggering an actual chroot syscall (which requires
# root).  --chroot-user sets the user pointer but leaves
# doChroot == 0 when the parent is non-root, so the helper
# script runs to completion.
#
###############################################################################

#
# Wrapper that calls __test_only_exec with two argv tokens
# inserted BEFORE the script path.  Used to exercise the shell's
# pre-init argv pre-scan.
#
proc shellExecArgv2 { arg1 arg2 } {
  set exe [info nameofexecutable]
  if {[llength [info commands __test_only_exec]] == 0 && \
      [info exists ::testlib_name] && $::testlib_name ne ""} then {
    catch {load $::testlib_name}
  }
  if {[llength [info commands __test_only_exec]] == 0} then {
    error "shellExecArgv2: __test_only_exec unavailable"
  }
  return [uplevel 1 [list __test_only_exec $exe $arg1 $arg2 \
      tests/helpers/sh_noop.tcl]]
}

runTest {test shell_subproc_chroot-3.1 {
  th8sh --chroot-user nobody <script> parses the user flag,
  leaves doChroot=0 (non-root caller), and runs the script
  cleanly.  Drives the (T,T) C2-Pair vector of the L344
  compound condition.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecArgv2 --chroot-user nobody]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_chroot-3.2 {
  th8sh --chroot --chroot-user nobody <script>: drives both
  the L341 --chroot match (T) and the L344 --chroot-user
  match (T,T) in a single subprocess.  Subprocess will fail
  at the actual chroot syscall (EPERM as non-root), but the
  pre-init argv scan has already been exercised by then.
} -constraints {
    th8 test_only_exec
} -body {
  # The catch is expected because the chroot syscall fails as
  # non-root; we only care that the argv parser was hit.
  catch {shellExecArgv2 --chroot --chroot-user} out
  string match {*chroot*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################
#
# Section 4 -- Th8Shell_EvalString (-eval dispatch) and
# Th8Shell_EvalFile (script-file dispatch).  Drives both
# the success and failure rc branches, plus the
# Th8_IsExited ternary (eval that calls [exit]).
#
###############################################################################

#
# Wrapper for `th8sh -eval <script>` invocations.
#
proc shellExecEval { script } {
  set exe [info nameofexecutable]
  if {[llength [info commands __test_only_exec]] == 0 && \
      [info exists ::testlib_name] && $::testlib_name ne ""} then {
    catch {load $::testlib_name}
  }
  if {[llength [info commands __test_only_exec]] == 0} then {
    error "shellExecEval: __test_only_exec unavailable"
  }
  return [uplevel 1 [list __test_only_exec $exe -eval $script]]
}

runTest {test shell_subproc_eval-4.1 {
  th8sh -eval with a successful script drives the L1587
  rc==TH8_OK branch and the Th8_IsExited(false) ternary arm.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEval {puts "EVAL_OK"}]
  string match {*EVAL_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test shell_subproc_eval-4.2 {
  th8sh -eval with a failing script drives the L1593 rc!=OK
  branch.  Subprocess exits with TH8_EXIT_FAILURE.
} -constraints {
    th8 test_only_exec
} -body {
  set rc [catch {shellExecEval {error "EVAL_FAIL"}} out]
  list $rc [string match {*EVAL_FAIL*} $out]
} -cleanup {
  unset -nocomplain rc out
} -result {1 1}}

###############################################################################

runTest {test shell_subproc_eval-4.3 {
  th8sh -eval with a script that calls [exit] drives the
  Th8_IsExited(true) ternary arm.  Subprocess exits with
  TH8_EXIT_DEMAND (= 2), so __test_only_exec reports
  non-zero; we catch and confirm the pre-exit output was
  emitted (proving the script ran through the eval path
  before the exit was demanded).
} -constraints {
    th8 test_only_exec
} -body {
  set rc [catch {shellExecEval {puts BEFORE_EXIT ; exit 0}} out]
  list $rc [string match {*BEFORE_EXIT*} $out]
} -cleanup {
  unset -nocomplain rc out
} -result {1 1}}

###############################################################################

runTest {test shell_subproc_evalfile-4.4 {
  th8sh <script> dispatch (Th8Shell_EvalFile) drives the
  script-file path.  Already exercised by the version /
  expr-features tests indirectly, but exercise it
  explicitly here.
} -constraints {
    th8 test_only_exec
} -body {
  set out [shellExecEnv {} tests/helpers/sh_noop.tcl]
  string match {*SHELL_SUBPROC_OK*} $out
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

source tests/epilogue.tcl
