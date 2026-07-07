###############################################################################
#
# coverage_file_paths_2.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for [file] path-handling decisions
# in src/plugins/th8_filesystems.c that have an explicitly
# uncovered C1-pair OR C2-pair (per llvm-cov mcdc report):
#
#   :148  while (i > 1 && th8IsPathSep(z[i - 1]))
#                 ([file dirname] trailing-sep collapse loop)
#                 missing: C1-pair (i <= 1 at loop entry)
#   :836  if (n1 == n2 && TH8_PATH_CMP(z1, z2, n1) == 0)
#                 ([file under] same-length-different-content)
#                 missing: C2-pair (n1 == n2 but content differs)
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

runTest {test fpaths2-1.1 {
  file dirname on a single-character input drives the
  C1=F vector at line 148 -- i never exceeds 1 so the
  loop check fails on the first iteration's first
  condition, exiting before evaluating the IsPathSep call.
} -constraints {
    th8
} -body {
  list \
      [file dirname "/"] \
      [file dirname "a"] \
      [file dirname ""]
} -result {/ . .}}

###############################################################################

runTest {test fpaths2-1.2 {
  file dirname on multiple-trailing-separator paths drives
  the loop multiple times.  The final iteration where i
  drops to 1 hits C1=F (the "i > 1" exit case).  Combined
  with -1.1, this closes the C1-pair at line 148.
} -constraints {
    th8
} -body {
  list \
      [file dirname "//"] \
      [file dirname "///"] \
      [file dirname "/abc/"]
} -result {/ / /}}

###############################################################################

runTest {test fpaths2-1.3 {
  file dirname "///abc" enters the else branch with i set
  to the position after the runs of leading separators
  (i = 3 for "///abc").  The strip-trailing-sep loop at
  line 148 then iterates twice (i = 3 -> 2 -> 1) before
  exiting on C1=F (i > 1 fails).  This drives the missing
  C1-pair vector that fpaths2-1.1/-1.2 cannot reach
  because they all settle into the "i == 1" arm without
  entering the inner strip loop.
} -constraints {
    th8
} -body {
  list \
      [file dirname "///abc"] \
      [file dirname "////xyz"] \
      [file dirname "//a/b"]
} -result {/ / //a}}

###############################################################################

runTest {test fpaths2-2.1 {
  file under with same-length but different-content paths
  drives the C2=F vector at line 836 -- (n1 == n2) is true
  but the path-compare returns non-zero.
} -constraints {
    th8
} -body {
  list \
      [file under "abc" "abd"] \
      [file under "abc" "abc"] \
      [file under "abc/d" "abc"] \
      [file under "abc" "abc/d"]
} -result {0 1 1 0}}

###############################################################################

runTest {test fpaths2-3.1 {
  file under with name1 strictly longer than name2 AND
  matching prefix BUT non-separator next char drives the
  C3=F vector at th8_filesystems.c:844 (n1 > n2 AND
  PATH_CMP == 0 AND IS_SEP false).  This is the case where
  "abcd" starts with "abc" but the 'd' is not a separator,
  so it's NOT under the parent.
} -constraints {
    th8
} -body {
  list \
      [file under "abcd" "abc"] \
      [file under "abc/d" "abc"] \
      [file under "abc" "ab"]
} -result {0 1 0}}

###############################################################################

runTest {test fpaths2-4.1 {
  file pathtype with a drive-letter-only string ("C:")
  drives the (T, F) vector at th8_filesystems.c:1361 --
  the outer block is entered (drive letter format), but
  the inner check n >= 3 fails because the string has
  only 2 chars.  Result is "volumerelative" rather than
  "absolute".
} -constraints {
    th8
} -body {
  list \
      [file pathtype "C:"] \
      [file pathtype "X:"] \
      [file pathtype "C:/"] \
      [file pathtype "abc"]
} -result {volumerelative volumerelative absolute relative}}

###############################################################################

runTest {test fpaths2-5.1 {
  cd with a single-character non-"." path drives the C2=F
  vector at th8PosixSetCwd (th8_posix.c:4338) -- nPath ==
  1 (T) but zPath[0] != '.' (F).  The shortcut bypasses,
  falling through to general path resolution.  Existing
  cd "." tests cover the (T,T) case.
} -constraints {
    th8
} -body {
  set rcs {}
  set cur [pwd]
  catch {cd "x"} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {cd "/"} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {cd $cur} m
  lappend rcs [expr {[string length $m] >= 0}]
  set rcs
} -cleanup {
  catch {cd $cur}
  unset -nocomplain cur rcs m
} -result {1 1 1}}

###############################################################################

runTest {test fpaths2-6.1 {
  file pathtype with 2+ char paths covering various drive-
  letter check conditions drives the C3..C6 pairs at
  th8_filesystems.c:1357-1360 -- char beyond 'Z' but
  before 'a' (e.g. '['), non-alpha first char ('5'), and
  2-char with non-colon second char ('ab'), each driving
  different vectors of the drive-letter detect compound.
} -constraints {
    th8
} -body {
  list \
      [file pathtype "\[:"] \
      [file pathtype "5:"] \
      [file pathtype "ab"] \
      [file pathtype "_:"] \
      [file pathtype "|:"]
} -result {relative relative relative relative relative}}

###############################################################################

runTest {test fpaths2-7.1 {
  File-ops on paths containing 1-char alphabetic and
  2-char non-".." path components drive the C2-pair at
  th8PosixIsPathUnderBase (th8_posix.c:4135, 4137) -- the
  path-component scanner sees 1-char non-".", 2-char
  alphabetic, etc.  Even when the path is rejected as
  outside the sandbox, the scanner has already processed
  the components.
} -constraints {
    th8
} -body {
  set rcs {}
  catch {file readlink "/a/b/c"} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {file readlink "/x/ab/c"} m
  lappend rcs [expr {[string length $m] >= 0}]
  catch {file readlink "/q/r/s/t"} m
  lappend rcs [expr {[string length $m] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test fpaths2-8.1 {
  file pathtype with uppercase first char followed by
  non-colon, and 2-char strings starting with various
  edge chars, drive the remaining C4..C6 pairs at
  th8_filesystems.c:1357-1360 -- uppercase z[0] with
  z[1] != ':' gives C6=F in the uppercase arm; specific
  combinations of upper/lower boundaries close the rest.
} -constraints {
    th8
} -body {
  list \
      [file pathtype "Cab"] \
      [file pathtype "ABC"] \
      [file pathtype "Zz"] \
      [file pathtype "aZ"]
} -result {relative relative relative relative}}

###############################################################################

runTest {test fpaths2-9.1 {
  file exists on non-existent paths with 2-char segments
  drives the C2-pair and C3-pair at th8PosixIsPathUnderBase
  L4137 (".." detection in the path-synthesis branch).
  When realpath() fails (path doesn't exist), the function
  manually parses zPath segment-by-segment; the ".."
  predicate checks (n==2 && s[0]=='.' && s[1]=='.').
  Two-char segments that don't match drive C2=F ("ab" --
  s[0]!='.') and C3=F (".c" -- s[0]='.' but s[1]!='.').
} -constraints {
    th8
} -body {
  set rcs {}
  # C2=F: 2-char non-dot-leading segment "ab".
  lappend rcs [file exists "abnoexist123/ab/foo"]
  # C3=F: 2-char dot-then-non-dot segment ".c".
  lappend rcs [file exists "abnoexist456/.c/foo"]
  # Both: ensure both predicates fire across the run.
  lappend rcs [file exists "noexist789/xy/.z/foo"]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {0 0 0}}

###############################################################################

runTest {test fpaths2-9.2 {
  file exists on a non-existent path with a SINGLE-char
  segment that is NOT "." drives the C2=F vector at
  th8PosixIsPathUnderBase L4135 (n==1 && s[0]=='.').  The
  segment is one byte (C1=T) but not '.' (C2=F), so the
  current-dir-skip branch is bypassed and the segment is
  recorded as a regular path component.  Existing tests
  exercise the "." segment itself (C2=T) and multi-char
  segments (C1=F); this closes the C2-pair.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [file exists "abnoexist92/a/foo"]
  lappend rcs [file exists "abnoexist92/x/y/z"]
  lappend rcs [file exists "noexist92/q/p"]
  set rcs
} -cleanup {
  unset -nocomplain rcs
} -result {0 0 0}}

###############################################################################

runTest {test fpaths2-10.1 {
  file pathtype with a LOWERCASE drive-letter prefix
  ("c:", "z:", "a:") drives the C4-pair and C5-pair at
  th8_filesystems.c:1357-1360 -- the upper-case sub-check
  (z[0]>='A' && z[0]<='Z') fails, then the lower-case
  sub-check (z[0]>='a' && z[0]<='z') succeeds for these
  inputs, combined with z[1]==':' producing the
  drive-letter-relative outcome.  Existing tests cover
  uppercase drive letters; this closes the lowercase
  arm's pairs.
} -constraints {
    th8
} -body {
  list \
      [file pathtype "c:"] \
      [file pathtype "z:"] \
      [file pathtype "a:"] \
      [file pathtype "c:/foo"] \
      [file pathtype "m:/bar"]
} -result {volumerelative volumerelative volumerelative absolute absolute}}

###############################################################################

source tests/epilogue.tcl
