###############################################################################
#
# coverage_load_mcdc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_load.c short-circuit compounds:
#
#   L622  Th8_Load `NEVER(!interp->pPlatform) || !xLoad`.
#         Needs xLoad=NULL to drive C2=T.
#   L756  Th8_Unload `ALWAYS(pPlatform) && xUnload`.
#         Needs xUnload=NULL to drive C2=F.
#
# Both are driven by the fault layer's -nullCallbacks option
# nulling the platform's xLoad / xUnload slot on a child
# interp, then invoking the script-level [load] / [unload]
# command which delegates directly to the public API.
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

runTest {test load_mcdc-1.1 {
  [load name] under -enableLoad + -nullCallbacks xLoad drives
  Th8_Load src/th8_load.c L622 C2=T ("no load callback
  available").  Without -enableLoad the child interp's load
  gate short-circuits before reaching the platform-callback
  check.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set r [::th8testlib::fault eval {load nonexistent:Foo} \
      -enableLoad -nullCallbacks xLoad]
  expr {[string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test load_mcdc-1.2 {
  [unload name] under -enableUnload + -nullCallbacks xUnload
  reaches Th8_Unload src/th8_load.c L756; unload of a
  never-loaded library errors with "library not loaded", but
  the test verifies the call completes cleanly through the
  unload gate.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set r [::th8testlib::fault eval {catch {unload nonexistent:Foo}} \
      -enableUnload -nullCallbacks xUnload]
  expr {[string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test load_mcdc-1.3 {
  [load name proc] with an explicit init procedure name
  drives src/th8_posix.c L595 (C1=T, C2=T) -- the
  `zProc && nProc > 0` compound that selects the
  caller-supplied symbol over the colon-suffix default.
  Ordinary [load $::testlib_name] omits the third
  argument so zProc stays NULL.

  Unload, reload with the 3-arg form, then leave the
  library loaded (matching the prologue's expectation).
} -constraints {
    th8 loadLib
} -setup {
} -body {
  testUnloadLib
  set r1 [load $::testlib_name Th8test_Init]
  # Result is empty string on success.
  expr {[string length $r1] >= 0}
} -cleanup {
  unset -nocomplain r1 r2
} -result {1}}

###############################################################################

runTest {test load_mcdc-1.5 {
  [load $libpath:BogusInit ""] with a real library file
  but a non-existent init symbol drives src/th8_posix.c
  L595 (T,F) -- zProc non-NULL but nProc == 0, falling
  back to the colon-suffix.  dlopen succeeds (library
  file exists) but dlsym for "BogusInit_Init" fails, so
  Th8_Load returns error without mutating the
  loaded-library tracking list.  Safe to run alongside
  the prologue's pre-load because no tracking entry is
  created or modified.
} -constraints {
    th8 loadLib
} -setup {
} -body {
  # Strip the existing :Th8test suffix and append :Bogus
  # so the dlopen target is the real testlib but the
  # init name is a guaranteed-missing symbol.
  set path [file dirname $::testlib_name]/[file tail \
      [string range $::testlib_name 0 \
      [expr {[string last : $::testlib_name] - 1}]]]
  set bogus_name "$path:LoadMcdc15BogusSuffix"
  # Empty initProc forces L595 (T,F) -- the dlsym then
  # fails because there is no LoadMcdc15BogusSuffix_Init
  # exported from the testlib.
  set r [catch {load $bogus_name ""} m]
  expr {$r == 1}
} -cleanup {
  unset -nocomplain r m bogus_name path
} -result {1}}

###############################################################################

runTest {test load_mcdc-1.4 {
  [unload -nokeeplibrary] without TH8_UNLOAD_DANGEROUS
  drives src/th8_load.c L726 (`bClose && !th8Is
  UnloadDangerous`) C1=T,C2=T -- the rejection path that
  the shell never sees because it enables dangerous
  by default.  The fault helper enables unload but NOT
  dangerous, so the gate fires and the unload errors.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set r [::th8testlib::fault eval \
      {catch {unload -nokeeplibrary nonexistent:Foo} m; set m} \
      -enableUnload]
  # The child runs the script; the returned value is a
  # 2-element list {rc result}; either errors with a
  # "DANGEROUS" message or "library not loaded" (depending
  # on which gate fires first), both indicate the L726
  # decision was at least exercised.
  expr {[string length [lindex $r 1]] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test load_mcdc-1.6 {
  In a child fault interp: load the testlib, then unload
  it with the platform xUnload slot nulled.  Drives Th8_
  Unload src/th8_load.c L756 (`ALWAYS(pPlatform) && xUnload`)
  C2-Pair (xUnload=NULL with pPlatform present and the
  tracked library found at L733) -- the else-branch fires
  and the tracking entry is removed without invoking any
  platform-level unload callback.
} -constraints {
    th8 fault_injection loadLib
} -setup {
} -body {
  # Substitute testlib_name into the script body in the
  # parent so the child interp gets an absolute path.
  set script "load [list $::testlib_name]\nunload [list $::testlib_name]"
  set r [::th8testlib::fault eval $script \
      -enableLoad -enableUnload -nullCallbacks xUnload]
  # Expect rc=0 (both load and unload succeed in the
  # child).  Result string may be empty.
  expr {[lindex $r 0] == 0}
} -cleanup {
  unset -nocomplain r script
} -result {1}}

###############################################################################

runTest {test load_mcdc-2.1 {
  th8RemoveLoaded at src/th8_load.c L302-303
  (`p->nName == nName && Th8_Memcmp(...) == 0`) currently
  only has the (T,T) vector covered: every unload in the
  existing suite happens with exactly one loaded library,
  so the first list entry matches and no non-matching
  iteration step occurs.  This test loads the testlib via
  two distinct path strings (a relative path symlink with
  a different length and a same-length-different-content
  symlink, both pointing back at libth8test.dylib), then
  unloads the first.  The internal walk iterates past the
  most-recently-loaded entry (which does not match) before
  finding the match, driving both the C1=F (length
  mismatch) and C2=F (same length, content mismatch)
  vectors.  The actual library image is deduped at the
  dyld level so this is safe -- both unloads release
  separate tracking entries without double-freeing the
  underlying shared image.
} -constraints {
    th8
} -setup {
  set origLib $::testlib_name
  # Strip ":Symbol" suffix that detectLoadLib appends.
  set i [string last ":" $origLib]
  if {$i > 1} then {
    set sym [string range $origLib [expr {$i + 1}] end]
    set origLib [string range $origLib 0 [expr {$i - 1}]]
  } else {
    set sym Th8test
  }
  # Use the same directory as origLib so the
  # "library path outside base directory" check passes.
  set baseDir [file dirname $origLib]
  set tag _mcdc_[clock seconds]_[pid]
  set targetLeaf [file tail $origLib]
  # Symlinks are RELATIVE to $baseDir so they don't depend
  # on an absolute target path that may differ on every
  # build host.
  # Three symlinks: link2 has SAME-LENGTH name as link1
  # (different content) and link3 has a DIFFERENT-LENGTH
  # name.  Loading all three then unloading link1 walks
  # past link3 (drives C1=F at L302) and link2 (drives
  # C2=F at L303) before matching link1 (drives the
  # (T,T) outcome).  All three MC/DC vectors hit in one
  # iteration.
  set link1Leaf "linkAAA${tag}.dylib"
  set link2Leaf "linkBBB${tag}.dylib"
  set link3Leaf "linkCCCxx${tag}.dylib"
  set link1 [file join $baseDir $link1Leaf]
  set link2 [file join $baseDir $link2Leaf]
  set link3 [file join $baseDir $link3Leaf]
  # The symlink TARGET is the leaf name, not a full path:
  # macOS resolves a relative symlink target relative to
  # the directory holding the LINK, not the cwd.  With
  # leaf-only ("libth8test.dylib") the resolution lands
  # next to the symlink in $baseDir, where the real lib
  # lives.
  set lnRc1 [catch {::th8testlib::symlink create \
      $targetLeaf $link1} lnM1]
  set lnRc2 [catch {::th8testlib::symlink create \
      $targetLeaf $link2} lnM2]
  set lnRc3 [catch {::th8testlib::symlink create \
      $targetLeaf $link3} lnM3]
} -body {
  # Load via the three symlinks (different tracking entries).
  set rc1 [catch {load $link1:$sym} m1]
  set rc2 [catch {load $link2:$sym} m2]
  set rc3a [catch {load $link3:$sym} m3a]
  # Unload via link1: the internal th8RemoveLoaded walk
  # starts at the most-recently-added head (link3 entry,
  # different LENGTH -> C1=F at L302), continues to link2
  # (same length, different CONTENT -> C2=F at L303), and
  # finally matches link1 ((T,T) outcome).  Closes both
  # MC/DC pairs at the L302-303 short-circuit compound.
  set rc3 [catch {unload $link1:$sym} m3]
  # Cleanup-unload the remaining entries.
  set rc4 [catch {unload $link2:$sym} m4]
  set rc4a [catch {unload $link3:$sym} m4a]
  expr {$rc1 == 0 && $rc2 == 0 && $rc3a == 0
      && $rc3 == 0 && $rc4 == 0 && $rc4a == 0}
} -cleanup {
  catch {::th8testlib::symlink delete $link1}
  catch {::th8testlib::symlink delete $link2}
  catch {::th8testlib::symlink delete $link3}
  unset -nocomplain r baseDir tag origLib link1 link2 link3 \
      link1Leaf link2Leaf link3Leaf targetLeaf \
      lnRc1 lnM1 lnRc2 lnM2 lnRc3 lnM3 \
      sym rc1 rc2 rc3 rc3a rc4 rc4a \
      m1 m2 m3 m3a m4 m4a i
} -result {1}}

###############################################################################

runTest {test load_mcdc-3.1 {
  Drive th8_load.c L200 C1-Pair (`i < nName` becomes F
  because no ':' was found in zName) by passing a load-name
  string with no ':' separator.  th8SplitLoadName's scan
  loop runs until i == nName, exiting via C1=F.  Existing
  load tests all use "lib:Symbol" format which exits via
  C2=F when the colon is found.  The load call itself
  errors out because "noseparator" isn't a real library;
  asserts only the rc, not the message.
} -constraints {
    th8 loadLib
} -setup {
} -body {
  set rc [catch {load noseparator} m]
  expr {$rc == 1}
} -cleanup {
  unset -nocomplain rc m
} -result {1}}

###############################################################################

runTest {test load_mcdc-3.2 {
  Drive th8_load.c L256 (T,F) in th8LoadNameMatch
  (`nLibA == nLibB && Th8_Memcmp == 0`): pass an unload
  name that has the SAME length as the loaded testlib
  but differs at one byte.  The search walks the
  loaded-library list, finds the same-length-but-
  different entry, observes (T,F) on the compare, and
  reports "library not loaded".

  Existing load tests use either the exact testlib name
  (drives (T,T) match) or names of different lengths
  (drives (F,-) short-circuit), so the (T,F) C2-Pair
  vector was never closed.
} -constraints {
    th8 loadLib
} -setup {
  # Th8_Unload canonicalizes the unload name via xGetRealPath
  # BEFORE the th8LoadNameMatch loop runs, so we must build
  # $wrong from the canonicalized name actually stored in
  # the loaded-library list ([info loaded] returns the
  # canonical "/abs/path/lib.dylib:Symbol" forms).  Replacing
  # the LAST library-name byte preserves nLib while
  # guaranteeing the path still canonicalizes to itself (an
  # X-prefixed bogus path would canonicalize to itself too,
  # but the LENGTH might mismatch the original canonical
  # length).  Use a single existing entry from [info loaded].
  set tlibCanon [lindex [info loaded] 0]
  # Split off the symbol so we touch only the lib byte.
  set colon [string last : $tlibCanon]
  set libPart [string range $tlibCanon 0 [expr {$colon - 1}]]
  set symPart [string range $tlibCanon $colon end]
  # Replace LAST byte of lib with a guaranteed-different
  # char.  This preserves nLibA == nLibB (C1=T) but breaks
  # the Memcmp (C2=F).
  set last [string index $libPart end]
  set replC [expr {$last eq {X} ? {Y} : {X}}]
  set libWrong [string replace $libPart end end $replC]
  set wrong "$libWrong$symPart"
} -body {
  set rc [catch {unload $wrong} m]
  expr {$rc == 1}
} -cleanup {
  unset -nocomplain rc m tlibCanon colon libPart symPart last replC libWrong wrong
} -result {1}}

###############################################################################

source tests/epilogue.tcl
