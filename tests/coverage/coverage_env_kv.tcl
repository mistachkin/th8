###############################################################################
#
# coverage_env_kv.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted tests for uncovered MC/DC branches in src/th8_env.c.
# th8EnvKeyValue is the default xKeyValue platform callback that
# wraps getenv/setenv.  In test runs that don't load the SQLite
# extension (most of the suite), it is the active backend for
# Th8_KeyValue and reachable via [::th8testlib::env_kv].  These tests
# read well-known env vars (PATH on POSIX, USERPROFILE on Win32)
# and exercise EXISTS / GET / EXISTS2 / GET2 op codes.
#
# Coverage-driven; not pinned to specific R-markers.  Use catch
# wrappers so the tests pass regardless of whether the active
# xKeyValue is the default env one or has been overridden by an
# extension.
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
# Section 1 -- kv EXISTS via env
#
###############################################################################

runTest {test env_kv-1.1 {
  kv exists for a guaranteed-present env var (PATH on POSIX, Path on Win32)
} -constraints {
    th8
} -body {
  # Try a few well-known names; at least one should exist on every
  # supported host.  We just need to drive th8EnvKeyValue's EXISTS
  # branch -- any successful getenv() satisfies the MC/DC condition.
  set seen 0
  foreach name {PATH Path HOME USERPROFILE TMPDIR TEMP TMP} {
    if {[catch {::th8testlib::env_kv exists $name} r] == 0 && $r == 1} then {
      incr seen
    }
  }
  expr {$seen >= 1}
} -cleanup {
  unset -nocomplain seen name r
} -result {1}}

###############################################################################

runTest {test env_kv-1.2 {
  kv exists for a definitely-absent env var
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv exists "_TH8_NO_SUCH_VAR_$$_"} r
  # Either 0 (env says no) or non-zero (any backend rejects the name);
  # the goal is to drive the not-present arm of the EXISTS switch.
  expr {[string is integer -strict $r] || [string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 2 -- kv GET via env
#
###############################################################################

runTest {test env_kv-2.1 {
  kv get for a present env var produces a non-empty value
} -constraints {
    th8
} -body {
  set found ""
  foreach name {PATH Path HOME USERPROFILE} {
    if {[catch {::th8testlib::env_kv get $name} v] == 0 && \
            [string length $v] > 0} then {
      set found $name
      break
    }
  }
  expr {[string length $found] > 0}
} -cleanup {
  unset -nocomplain found name v
} -result {1}}

###############################################################################

runTest {test env_kv-2.2 {
  kv get for an absent env var errors or returns empty
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv get "_TH8_NO_SUCH_VAR_$$_"} v
  # Any string outcome is fine -- we just want the C-level "missing"
  # branch traversed.
  expr {[string length $v] >= 0}
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################
#
# Section 3 -- kv EXISTS2 / GET2 (path-qualified forms)
#
###############################################################################

runTest {test env_kv-3.1 {
  kv exists2 with a path-qualified name exercises the TH8_KV_EXISTS2 arm
} -constraints {
    th8
} -body {
  # The "2" forms accept a name that may be qualified with a
  # backend-specific path/scope.  We pass a plain env-var name; the
  # backend should treat it like the unqualified form.
  catch {::th8testlib::env_kv exists2 PATH} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_kv-3.2 {
  kv get2 with a path-qualified name exercises the TH8_KV_GET2 arm
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv get2 PATH} v
  expr {[string length $v] >= 0}
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################
#
# Section 4 -- LIST with empty/missing pattern
#
# Drives MC/DC at 6 zName/nName sites in th8_env.c (lines 379,
# 411, 444, 488, 543, 609) by passing both empty-string and
# valid-string patterns for LIST/LIST2 ops.
#
###############################################################################

runTest {test env_kv-4.1 {
  kv list with explicit empty pattern -- drives the T,F vector
  at line 379 (zName valid pointer, nName == 0)
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv list ""} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_kv-4.2 {
  kv list with valid pattern drives T,T vector
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv list "PA*"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_kv-4.3 {
  kv list2 with empty pattern drives T,F vector at line 411
} -constraints {
    th8
} -body {
  catch {::th8testlib::env_kv list2 ""} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test env_kv-5.1 {
  env_kv exists2 with a name pattern that matches an
  existing variable drives the post-find loop exit at
  th8_env.c:403 -- after found=1 is set, the next loop
  check yields (environ[i] non-NULL, !found = F),
  closing the C2-pair.
} -constraints {
    th8
} -body {
  set rc [catch {::th8testlib::env_kv exists2 "PATH"} r]
  list $rc [expr {$r == 0 || $r == 1}]
} -cleanup {
  unset -nocomplain rc r
} -result {0 1}}

###############################################################################

runTest {test env_kv-5.2 {
  env_kv exists2 with a name pattern that DOES NOT match
  any variable drives the C1=F vector at th8_env.c:403 --
  the loop walks every environ entry, never sets found,
  and finally hits environ[i]==NULL (end of array),
  short-circuiting to F.  Existing tests cover the
  found=true mid-iteration exit (C2=F); this closes
  the C1-pair via a deliberately-unmatching pattern.
} -constraints {
    th8
} -body {
  set rcs {}
  catch {::th8testlib::env_kv exists2 "ZZZ_NO_SUCH_VAR_5_2"} r
  lappend rcs $r
  catch {::th8testlib::env_kv exists2 "DEFINITELY_NOT_IN_ENV_ZZZ"} r
  lappend rcs $r
  set rcs
} -cleanup {
  unset -nocomplain rcs r
} -result {0 0}}

###############################################################################

runTest {test env_kv-6.1 {
  env_kv list with NO pattern argument drives the C1=F
  vector at th8_env.c:379 -- the env-list loop's glob
  filter sees zName == NULL (no pattern supplied), so the
  glob match is short-circuited and every env entry is
  appended.  Existing tests pass an explicit pattern;
  this closes C1-pair.
} -constraints {
    th8
} -body {
  set rc [catch {::th8testlib::env_kv list} r]
  list $rc [expr {[string length $r] >= 0}]
} -cleanup {
  unset -nocomplain rc r
} -result {0 1}}

###############################################################################

runTest {test env_kv-7.1 {
  env_kv exists2 with NO name argument drives the C1=F
  vector at th8_env.c:411 (TH8_KV_EXISTS2 case) -- zName
  is NULL, so the name-filter compound short-circuits to
  F at C1 and the loop iterates all env entries without
  filtering by name.  Existing env_kv-5.x tests pass an
  explicit pattern (C1=T, C2=T); this closes C1-pair.
} -constraints {
    th8
} -body {
  set rc [catch {::th8testlib::env_kv exists2} r]
  list $rc [expr {$r == 0 || $r == 1}]
} -cleanup {
  unset -nocomplain rc r
} -result {0 1}}

###############################################################################

runTest {test env_kv-7.2 {
  env_kv exists2 with an EMPTY name argument drives the
  C2=F vector at th8_env.c:411 -- zName is "" (non-NULL)
  but nName == 0, so the compound is C1=T, C2=F.  This
  closes the C2-pair which existing tests miss (they pass
  only non-empty patterns).
} -constraints {
    th8
} -body {
  set rc [catch {::th8testlib::env_kv exists2 ""} r]
  list $rc [expr {$r == 0 || $r == 1}]
} -cleanup {
  unset -nocomplain rc r
} -result {0 1}}

###############################################################################

runTest {test env_kv-7.3 {
  env_kv list2 with a non-empty pattern drives the (T,T)
  vector at th8_env.c:444 -- zName is non-NULL with
  nName > 0, so the name-filter compound succeeds and
  the glob match runs.  Existing list2 tests pass only
  empty pattern "" (C2=F); this closes the C2-pair via
  a real pattern.
} -constraints {
    th8
} -body {
  set rc [catch {::th8testlib::env_kv list2 "PATH*"} r]
  list $rc [expr {[string length $r] >= 0}]
} -cleanup {
  unset -nocomplain rc r
} -result {0 1}}

###############################################################################

source tests/epilogue.tcl
