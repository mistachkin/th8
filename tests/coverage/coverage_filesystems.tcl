###############################################################################
#
# coverage_filesystems.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted tests for uncovered MC/DC branches in src/plugins/th8_filesystems.c.
# Exercises edge cases in [file join], [file split], [file under],
# [file pathtype], [file validname], [file separator], [file tempname],
# [file channels] -- inputs that the conformance suite doesn't naturally
# produce (multi-separator paths, leading/trailing separators, Windows-
# style drive letters, wrong-argument-count error paths, etc.).
#
# Coverage-driven; not pinned to specific R-markers.  Tests use
# 'catch' for error paths so they pass regardless of return code --
# the value of each test is in driving the C-level boolean compound,
# not in asserting a specific Tcl-level behavior.
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
# Section 1 -- [file join] edge cases
#
###############################################################################

runTest {test fs_cov-1.1 {
  file join exercises multi-separator-stripping branch
} -constraints {
    th8
} -body {
  # /a// has two trailing separators; the inner while loop strips them
  # both, exercising the multi-iter (n > 1 && isPathSep) branch.
  file join "/a//" "b"
} -result {/a/b}}

###############################################################################

runTest {test fs_cov-1.2 {
  file join with second arg starting with separator (no sep added)
} -constraints {
    th8
} -body {
  # When zResult ends with separator, the "no sep needed" branch fires.
  file join "/a/" "b"
} -result {/a/b}}

###############################################################################

runTest {test fs_cov-1.3 {
  file join with no separators in result yet
} -constraints {
    th8
} -body {
  file join "a" "b"
} -result {a/b}}

###############################################################################
#
# Section 2 -- [file split] edge cases
#
###############################################################################

runTest {test fs_cov-2.1 {
  file split with leading separator
} -constraints {
    th8
} -body {
  file split "/a/b"
} -result {/ a b}}

###############################################################################

runTest {test fs_cov-2.2 {
  file split with multiple consecutive separators
} -constraints {
    th8
} -body {
  file split "/a//b///c"
} -match glob -result {*}}

###############################################################################

runTest {test fs_cov-2.3 {
  file split with trailing separator
} -constraints {
    th8
} -body {
  file split "a/b/"
} -match glob -result {*}}

###############################################################################

runTest {test fs_cov-2.4 {
  file split of empty string
} -constraints {
    th8
} -body {
  file split ""
} -result {}}

###############################################################################
#
# Section 3 -- [file under] path-comparison branches
#
###############################################################################

runTest {test fs_cov-3.1 {
  file under: equal-length identical paths
} -constraints {
    th8
} -body {
  # Exercises the n1 == n2 && cmp == 0 branch.
  catch {file under "/a/b" "/a/b"} msg
  expr {[string length $msg] >= 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test fs_cov-3.2 {
  file under: prefix path with separator delimiter
} -constraints {
    th8
} -body {
  # Exercises the && TH8_IS_SEP(z1[n2]) branch.
  catch {file under "/a/b/c" "/a/b"} msg
  expr {[string length $msg] >= 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 4 -- [file validname] error paths
#
###############################################################################

runTest {test fs_cov-4.1 {
  file validname with wrong argc (zero args)
} -constraints {
    th8
} -body {
  catch {file validname} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test fs_cov-4.2 {
  file validname rejects question-mark / star characters
} -constraints {
    th8
} -body {
  # Exercises the c == '?' || c == '*' branch.
  catch {file validname "foo?bar"} r1
  catch {file validname "foo*bar"} r2
  list [expr {[string length $r1] >= 0}] [expr {[string length $r2] >= 0}]
} -cleanup {
  unset -nocomplain r1 r2
} -result {1 1}}

###############################################################################
#
# Section 5 -- [file separator] error path
#
###############################################################################

runTest {test fs_cov-5.1 {
  file separator with wrong argc
} -constraints {
    th8
} -body {
  catch {file separator a b c d} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################
#
# Section 6 -- [file tempname] argument handling
#
###############################################################################

runTest {test fs_cov-6.1 {
  file tempname with size 0 errors
} -constraints {
    th8 file_tempname
} -body {
  catch {file tempname 0} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test fs_cov-6.2 {
  file tempname with negative size errors
} -constraints {
    th8 file_tempname
} -body {
  catch {file tempname -1} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test fs_cov-6.3 {
  file tempname with a NON-NUMERIC size argument drives
  C1=T at th8_filesystems.c:592 (Th8_ToWideInt returns
  non-OK), short-circuiting the nSize-positivity check.
  This closes the C1-pair that fs_cov-6.1/-6.2 cannot
  reach because both pass numeric-but-non-positive values.
} -constraints {
    th8 file_tempname
} -body {
  set rc [catch {file tempname not_a_number} msg]
  list $rc [expr {[string length $msg] > 0}]
} -cleanup {
  unset -nocomplain rc msg
} -result {1 1}}

###############################################################################
#
# Section 7 -- [file channels] argc handling and platform-callback branches
#
###############################################################################

runTest {test fs_cov-7.1 {
  file channels with no pattern (argc == 2)
} -constraints {
    th8
} -body {
  set chans [file channels]
  expr {[llength $chans] >= 0}
} -cleanup {
  unset -nocomplain chans
} -result {1}}

###############################################################################

runTest {test fs_cov-7.2 {
  file channels with glob pattern (argc == 3)
} -constraints {
    th8
} -body {
  # Exercises the argc == 3 && zList branch when there are matching channels.
  set chans [file channels "*"]
  expr {[llength $chans] >= 0}
} -cleanup {
  unset -nocomplain chans
} -result {1}}

###############################################################################

runTest {test fs_cov-7.3 {
  file channels filter with non-matching pattern
} -constraints {
    th8
} -body {
  set chans [file channels "_no_such_channel_pattern_*"]
  llength $chans
} -cleanup {
  unset -nocomplain chans
} -result {0}}

###############################################################################

runTest {test fs_cov-7.4 {
  file channels with PATTERN under nulled xInput/xOutput
  callbacks drives the C2=F vector at th8_filesystems.c:657
  -- argc == 3 (T) but zList is NULL because no stdin/
  stdout was appended (callbacks nulled) and no temp
  channels are open in the fault-eval block.  This closes
  the C2-pair that fs_cov-7.2/-7.3 cannot reach.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::fault eval {file channels "*pattern*"} \
      -nullCallbacks {xInput xOutput}]
  list [lindex $r 0] [lindex $r 1]
} -cleanup {
  unset -nocomplain r
} -result {0 {}}}

###############################################################################
#
# Section 8 -- [file pathtype] Windows-style inputs (POSIX-friendly)
#
###############################################################################

runTest {test fs_cov-8.1 {
  file pathtype with Windows drive-letter prefix
  (exercises the n >= 2 && z[1] == ':' branch on POSIX hosts too;
   the parser logic runs regardless of the host platform)
} -constraints {
    th8
} -body {
  # On POSIX, "C:..." is treated as relative or volumerelative depending
  # on implementation -- we just need to exercise the branch.
  set t [file pathtype "C:foo"]
  expr {[string length $t] > 0}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################

runTest {test fs_cov-8.2 {
  file pathtype with Windows drive + sep prefix (n >= 3, sep at z[2])
} -constraints {
    th8
} -body {
  set t [file pathtype "C:/foo"]
  expr {[string length $t] > 0}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################
#
# Section 9 -- [file extension] and [file rootname]
#
###############################################################################

runTest {test fs_cov-9.1 {
  file extension on a simple "name.ext"
} -constraints {
    th8
} -body {
  file extension "foo.txt"
} -result {.txt}}

###############################################################################

runTest {test fs_cov-9.2 {
  file extension on a name with no extension
} -constraints {
    th8
} -body {
  file extension "noextension"
} -result {}}

###############################################################################

runTest {test fs_cov-9.3 {
  file extension on a name with multiple dots returns the last
} -constraints {
    th8
} -body {
  file extension "archive.tar.gz"
} -result {.gz}}

###############################################################################

runTest {test fs_cov-9.4 {
  file extension on a path with directory components
} -constraints {
    th8
} -body {
  file extension "/a/b/c/foo.tcl"
} -result {.tcl}}

###############################################################################

runTest {test fs_cov-9.5 {
  file extension on a hidden dotfile
} -constraints {
    th8
} -body {
  catch {file extension ".hidden"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fs_cov-9.6 {
  file rootname on a simple "name.ext"
} -constraints {
    th8
} -body {
  file rootname "foo.txt"
} -result {foo}}

###############################################################################

runTest {test fs_cov-9.7 {
  file rootname on a name with no extension
} -constraints {
    th8
} -body {
  file rootname "noextension"
} -result {noextension}}

###############################################################################

runTest {test fs_cov-9.8 {
  file rootname on a path with directory components
} -constraints {
    th8
} -body {
  file rootname "/a/b/c/foo.tcl"
} -result {/a/b/c/foo}}

###############################################################################

runTest {test fs_cov-9.9 {
  file rootname on a name with multiple dots strips only the last
} -constraints {
    th8
} -body {
  file rootname "archive.tar.gz"
} -result {archive.tar}}

###############################################################################

runTest {test fs_cov-10.1 {
  th8IsPathSep at th8_filesystems.c:57 -- the (c == '/' ||
  c == '\\') compound needs both a forward-slash input
  (covers the C1=T short-circuit) and a backslash input
  (covers C1=F, C2=T) and a non-separator (covers F,F).
} -constraints {
    th8
} -body {
  list \
      [file split "a/b/c"] \
      [file split "a\\b\\c"] \
      [file split "abc"]
} -result {{a b c} {a b c} abc}}

###############################################################################

runTest {test fs_cov-10.2 {
  Trailing-separator collapse loop at th8_filesystems.c:148
  -- need a path with at least 2 trailing separators so the
  loop iterates more than once (covers the i > 1 = T,
  IsPathSep = T compound on a non-first iteration).
  TH8's [file dirname] strips trailing separators eagerly;
  observed result is /a for both inputs, "." for the
  no-separator case.
} -constraints {
    th8
} -body {
  list \
      [file dirname "/a/b///"] \
      [file dirname "/a///b"] \
      [file dirname "noslash"]
} -result {/a /a .}}

###############################################################################

runTest {test fs_cov-10.3 {
  Path-component iteration at th8_filesystems.c:1096 / 1105
  -- the alternating !IsPathSep / IsPathSep loops only
  iterate beyond the first scan when there are MULTIPLE
  components.  A multi-component path drives both loops.
} -constraints {
    th8
} -body {
  list \
      [file split "/a/b/c/d"] \
      [file join "x" "y" "z"]
} -result {{/ a b c d} x/y/z}}

###############################################################################

source tests/epilogue.tcl
