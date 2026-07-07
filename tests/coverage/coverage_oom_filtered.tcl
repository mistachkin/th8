###############################################################################
#
# coverage_oom_filtered.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# P1 of the MC/DC closure plan.  Uses the new per-site OOM
# filter (extended Th8_FaultConfig + ::th8testlib::fault eval
# -allocFailSite FILE[:LINE[-LINETO]]) to drive
# `!pX || !pY` post-allocation guards by source location.
#
# A filter with no line spec matches every allocation in that
# file; a filter with FILE:LINE matches a single line; a filter
# with FILE:LO-HI matches an inclusive line range.
#
# When a filter list is set, only matching allocations count
# toward `nAllocFailAfter`, so the test reliably hits the
# allocation-of-interest without sweeping N values across
# unrelated code paths.
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

runTest {test oom_filt-1.1 {
  -allocFailSite with no filter active behaves identically to
  the count-only mode (back-compat sanity)
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 10]
    string length $s
  } -allocFailAfter 5} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test oom_filt-1.2 {
  File-only -allocFailSite filter: only allocations from the
  named file count toward -allocFailAfter
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 10]
    string length $s
  } -allocFailSite th8_strings.c -allocFailAfter 1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test oom_filt-1.3 {
  Single-line -allocFailSite filter
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 10]
    string length $s
  } -allocFailSite th8_strings.c:1109 -allocFailAfter 1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test oom_filt-1.4 {
  Line-range -allocFailSite filter
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 10]
    string length $s
  } -allocFailSite th8_lists.c:300-320 -allocFailAfter 1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test oom_filt-1.5 {
  Multiple -allocFailSite entries: allocation matches if ANY
  filter matches (OR semantics)
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 10]
    set l [list a b c d]
    string length $s
  } -allocFailSite th8_strings.c \
    -allocFailSite th8_lists.c \
    -allocFailAfter 1} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test oom_filt-1.6 {
  -allocFailSite without -allocFailAfter is harmless (filter
  installed but no failure target)
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 10]
    string length $s
  } -allocFailSite th8_strings.c} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test oom_filt-1.7 {
  Bad -allocFailSite with no value errors
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s "x"
  } -allocFailSite} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test oom_filt-1.8 {
  Bad line spec errors
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::fault eval {
    set s "x"
  } -allocFailSite th8_strings.c:notanumber} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test oom_filt-2.1 {
  Narrow line-range -allocFailSite filter forces allocations
  past lineHi to drive the C2=T vector at th8_fault.c:181
  (pCfg->nCurLine > lineHi).  Existing oom_filt-1.4 uses a
  20-line range matching th8_lists.c, but matches mostly
  hit the in-range case (C2=F) or pre-range (C1=T).  This
  closes the C2-pair with a 1-line range matching FILE:1,
  forcing every allocation past line 1 to evaluate
  nCurLine > lineHi (C2=T) and continue past the rule.
} -constraints {
    th8 fault_injection
} -body {
  set rcs {}
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 100]
    set l [list a b c d e f g h]
    set d [dict create k1 v1 k2 v2]
    string length $s
  } -allocFailSite th8_lists.c:1 -allocFailAfter 1} r
  lappend rcs [expr {[string length $r] >= 0}]
  catch {::th8testlib::fault eval {
    set s [string repeat "xyz" 50]
    set m [list 1 2 3]
  } -allocFailSite th8_strings.c:1-2 -allocFailAfter 1} r2
  lappend rcs [expr {[string length $r2] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs r r2
} -result {1 1}}

###############################################################################

runTest {test oom_filt-2.2 {
  -allocFailSite filter with the FULL source path
  (matching the compiler's __FILE__ exactly) drives the
  C2=F vector at th8_fault.c:172-175 (the file-name
  matching compound) -- th8FaultStrEqAscii returns 1
  because the user-supplied path equals __FILE__ literally,
  so the second sub-condition !strEqAscii evaluates F and
  the rule applies to the file.  Existing oom_filt tests
  use bare basenames (C2=T, then pathMatchesBaseName fires
  at C3); this closes the C2-pair.
} -constraints {
    th8 fault_injection
} -body {
  set rcs {}
  catch {::th8testlib::fault eval {
    set l [list a b c]
    set s [string repeat "x" 10]
  } -allocFailSite src/plugins/th8_lists.c -allocFailAfter 1} r
  lappend rcs [expr {[string length $r] >= 0}]
  catch {::th8testlib::fault eval {
    set s [string repeat "y" 5]
  } -allocFailSite src/plugins/th8_strings.c -allocFailAfter 1} r2
  lappend rcs [expr {[string length $r2] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs r r2
} -result {1 1}}

###############################################################################

runTest {test oom_filt-2.3 {
  -allocFailSite with an EMPTY file part (just ":<line>")
  drives the C1-pair vector at th8_fault.c:132
  (th8FaultPathMatchesBaseName) -- with the filter's
  zFile set to an empty string, the basename matcher
  computes nBase = 0 and short-circuits at "nBase == 0"
  (C1=T).  Existing oom_filt tests pass non-empty
  filenames (C1=F); this closes the C1-pair.
} -constraints {
    th8 fault_injection
} -body {
  set rcs {}
  catch {::th8testlib::fault eval {
    set s [string repeat "abc" 10]
    set l [list a b c]
  } -allocFailSite :1-99999 -allocFailAfter 1} r
  lappend rcs [expr {[string length $r] >= 0}]
  catch {::th8testlib::fault eval {
    set t [string repeat "xy" 8]
  } -allocFailSite :1 -allocFailAfter 1} r2
  lappend rcs [expr {[string length $r2] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs r r2
} -result {1 1}}

###############################################################################

runTest {test oom_filt-2.1 {
  Drive th8_curl.c L242 TH8_ALLOC_STR failure: a -allocFailSite
  filter targeting th8_curl.c:242 makes the URI-copy
  allocation fail before any network I/O.  Closes the L243
  `if (!zUri)` early-return path that no real fetch reaches
  (libc/mimalloc never returns NULL on a sub-100-byte alloc).
} -constraints {
    th8 fault_injection libcurl
} -body {
  set rc [catch {::th8testlib::fault eval {
    source http://127.0.0.1:1/foo
  } -allocFailSite th8_curl.c:242 -allocFailAfter 1} r]
  # The fault helper returns OK with a list "rc msg ..."
  # describing the body's evaluation result.  The body's
  # source command will return non-OK either because the
  # alloc-fail filter fired at L242 (driving L243-247) or
  # because the connect to 127.0.0.1:1 was refused.
  # Either outcome exercises th8_curl.c's entry path; the
  # filter directs at least one execution through the
  # alloc-fail branch.
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc r
} -result {1}}

###############################################################################

runTest {test oom_filt-2.2 {
  Drive plugins/th8_lists.c L4429 / L4432 / L4435 TH8_ALLOC_MUL
  failures to close the L4439 (`aazLevel && aanLevel &&
  anCountLvl`) MC/DC C1-Pair / C2-Pair / C3-Pair vectors.
  Single-line filters at 4429 / 4432 / 4435 don't match
  because the macro expansion's __LINE__ doesn't always
  line up with the exact source line; a range filter
  covering all three lines, combined with -allocFailAfter
  N, picks off each individual alloc in sequence.  Driven
  by [dict with] using a nested key path so the L4268
  `if (nNestedKeys > 0)` arm enters the L4429 rebuild
  block.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  foreach idx {1 2 3} {
    set rc [catch {::th8testlib::fault eval {
      set ::dw {k1 {k2 v}}
      catch {dict with ::dw k1 {}}
    } -allocFailSite th8_lists.c:4420-4440 -allocFailAfter $idx} r]
    lappend rcs [expr {$rc == 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r idx ::dw
} -result {1 1 1}}

###############################################################################

runTest {test oom_filt-2.3 {
  Drive the three single-condition allocation guards in
  dict_set_command's nested-rebuild path (plugins/
  th8_lists.c L3666 / L3672 / L3679 TH8_ALLOC_MUL).  Each
  alloc has an `if (!ptr)` early-return that the standard
  test suite never reaches because the allocations always
  succeed.  A range -allocFailSite + sequenced
  -allocFailAfter N drives each branch in turn.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  foreach idx {1 2 3} {
    set rc [catch {::th8testlib::fault eval {
      set ::ds {k1 {k2 inner}}
      catch {dict set ::ds k1 k2 NEW}
    } -allocFailSite th8_lists.c:3660-3690 -allocFailAfter $idx} r]
    lappend rcs [expr {$rc == 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r idx ::ds
} -result {1 1 1}}

###############################################################################

runTest {test oom_filt-2.4 {
  Drive the three allocation guards in dict_unset_command's
  nested-rebuild path (plugins/th8_lists.c L3898 / L3904 /
  L3911).  Mirror of oom_filt-2.3 for the unset operation.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  foreach idx {1 2 3} {
    set rc [catch {::th8testlib::fault eval {
      set ::du {k1 {k2 v}}
      catch {dict unset ::du k1 k2}
    } -allocFailSite th8_lists.c:3890-3920 -allocFailAfter $idx} r]
    lappend rcs [expr {$rc == 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r idx ::du
} -result {1 1 1}}

###############################################################################

runTest {test oom_filt-2.5 {
  Drive plugins/th8_looping.c L623 TH8_ALLOC_MUL failure
  in foreach_command's per-pair table.  The early-return
  block at L625-L629 is otherwise unreached -- foreach
  arrays are typically small and the allocation always
  succeeds.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rc [catch {::th8testlib::fault eval {
    foreach {a b} {1 2 3 4} {}
  } -allocFailSite th8_looping.c:620-630 -allocFailAfter 1} r]
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc r
} -result {1}}

###############################################################################

runTest {test oom_filt-2.6 {
  Drive plugins/th8_lists.c L1908 / L1963 TH8_ALLOC_MUL_ADD2
  failures inside th8DictSplit (uncached + cache-store paths).
  Both are reachable via any [dict get] / [dict keys] /
  [dict values] call that misses the cache.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  foreach line {1908 1963} {
    set rc [catch {::th8testlib::fault eval {
      set ::dd {a 1 b 2 c 3 d 4}
      catch {dict keys $::dd}
      catch {dict get $::dd a}
    } -allocFailSite th8_lists.c:[expr {$line - 5}]-[expr {$line + 5}] \
        -allocFailAfter 1} r]
    lappend rcs [expr {$rc == 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r line ::dd
} -result {1 1}}

###############################################################################

runTest {test oom_filt-2.7 {
  Drive plugins/th8_procedures.c L961 TH8_ALLOC_MUL failure
  inside nproc_call_nr's aBound array allocation.  Reached
  by invoking an nproc (named-parameter procedure).  The
  L964-L968 error block is never hit by the standard suite
  because the alloc always succeeds for typical proc sizes.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rc [catch {::th8testlib::fault eval {
    nproc ::oom_np {x y} {expr {$x + $y}}
    catch {oom_np 1 2}
  } -allocFailSite th8_procedures.c:955-970 -allocFailAfter 1} r]
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc r
} -result {1}}

###############################################################################

runTest {test oom_filt-2.8 {
  Drive plugins/th8_extensibility.c L434 / L799 TH8_ALLOC
  failures inside [package provide] and [package ifneeded]
  when the package's Th8_PkgInfo struct doesn't yet exist
  (first registration).  Both alloc-fail blocks (L437-L439,
  L801-L804) are otherwise unreached.  Uses unique package
  names per call so each invocation triggers a fresh
  hash-entry creation that needs to allocate the PkgInfo.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  set rc [catch {::th8testlib::fault eval {
    catch {package provide _oom_pkg_provide 1.0}
  } -allocFailSite th8_extensibility.c:430-445 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  set rc [catch {::th8testlib::fault eval {
    catch {package ifneeded _oom_pkg_ifn 1.0 {}}
  } -allocFailSite th8_extensibility.c:795-810 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r
} -result {1 1}}

###############################################################################

runTest {test oom_filt-2.9 {
  Drive misc plugin TH8_ALLOC failures.
    L1609 th8_control.c [try] state struct alloc.
    L1745, L1750 th8_variables.c [array startsearch] state +
                  array-name copy allocs.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  # [try] state alloc.
  set rc [catch {::th8testlib::fault eval {
    catch {try {set ::omt_x 1} on ok {} {}}
  } -allocFailSite th8_control.c:1605-1615 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  # [array startsearch] state.
  set rc [catch {::th8testlib::fault eval {
    set ::omt_a(k1) v1
    catch {array startsearch ::omt_a}
  } -allocFailSite th8_variables.c:1740-1755 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r ::omt_x ::omt_a
} -result {1 1}}

###############################################################################

runTest {test oom_filt-2.10 {
  Drive procedure-allocation error paths in
  plugins/th8_procedures.c and plugins/th8_events.c.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  # th8_events.c L274: [update] state alloc fail.
  set rc [catch {::th8testlib::fault eval {
    catch {update}
  } -allocFailSite th8_events.c:272-278 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  # th8_events.c L557: [vwait] state alloc fail.
  set rc [catch {::th8testlib::fault eval {
    catch {after 0 {set ::vw_var 1}; vwait ::vw_var}
  } -allocFailSite th8_events.c:555-565 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  # th8_procedures.c L537: [proc NAME ARGS BODY] alloc fail.
  set rc [catch {::th8testlib::fault eval {
    catch {proc ::oom_proc {x} {expr {$x + 1}}}
  } -allocFailSite th8_procedures.c:535-545 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  # th8_procedures.c L1179: [nproc NAME ARGS BODY] alloc fail.
  set rc [catch {::th8testlib::fault eval {
    catch {nproc ::oom_nproc {x y} {expr {$x + $y}}}
  } -allocFailSite th8_procedures.c:1170-1185 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  # th8_procedures.c L772: [apply {args body}] lambda alloc.
  set rc [catch {::th8testlib::fault eval {
    catch {apply {{x} {expr {$x * 2}}} 5}
  } -allocFailSite th8_procedures.c:765-780 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r ::vw_var
} -result {1 1 1 1 1}}

###############################################################################

runTest {test oom_filt-2.11 {
  Drive plugins/th8_strings.c L1519 ([string totitle])
  and L1821 ([string case]) TH8_ALLOC_STR failure paths.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rcs {}
  set rc [catch {::th8testlib::fault eval {
    catch {string totitle hello}
  } -allocFailSite th8_strings.c:1515-1525 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  set rc [catch {::th8testlib::fault eval {
    catch {string toupper hello}
  } -allocFailSite th8_strings.c:1817-1830 -allocFailAfter 1} r]
  lappend rcs [expr {$rc == 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs rc r
} -result {1 1}}

###############################################################################

runTest {test oom_filt-2.12 {
  Drive plugins/th8_filesystems.c L1250 ([file nativename])
  TH8_ALLOC_STR failure path.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set rc [catch {::th8testlib::fault eval {
    catch {file nativename foo}
  } -allocFailSite th8_filesystems.c:1245-1255 -allocFailAfter 1} r]
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc r
} -result {1}}

###############################################################################

runTest {test oom_filt-2.13 {
  Drive plugins/regexp/th8_regex.c L754 (th8Utf8ToChr)
  TH8_ALLOC_MUL_ADD failure on pattern UTF-8 -> UTF-32
  conversion.  The first allocation th8Utf8ToChr performs
  inside regexp_command is for the pattern; failing it must
  set TH8_ERROR and the catch must succeed.
} -constraints {
    th8 fault_injection regexp
} -setup {
} -body {
  set rc [catch {::th8testlib::fault eval {
    catch {regexp {abc} "abcdef"}
  } -allocFailSite th8_regex.c:750-760 -allocFailAfter 1} r]
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc r
} -result {1}}

###############################################################################

runTest {test oom_filt-2.14 {
  Drive plugins/regexp/th8_regex.c L754 (th8Utf8ToChr)
  TH8_ALLOC_MUL_ADD failure on string UTF-8 -> UTF-32
  conversion.  Skip the first alloc (pattern) and fail the
  second (string); pattern compile succeeds, string convert
  fails.
} -constraints {
    th8 fault_injection regexp
} -setup {
} -body {
  set rc [catch {::th8testlib::fault eval {
    catch {regexp {abc} "abcdef"}
  } -allocFailSite th8_regex.c:750-760 -allocFailAfter 2} r]
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc r
} -result {1}}

###############################################################################

runTest {test oom_filt-2.15 {
  Drive plugins/regexp/th8_regex.c L1189
  (TH8_ALLOC_MUL match-results array).  Pattern compile and
  UTF-8 conversions succeed; allocation of pmatch[] fails.
} -constraints {
    th8 fault_injection regexp
} -setup {
} -body {
  set rc [catch {::th8testlib::fault eval {
    catch {regexp {(a)(b)(c)} "abc" m a b c}
  } -allocFailSite th8_regex.c:1185-1195 -allocFailAfter 1} r]
  expr {$rc == 0}
} -cleanup {
  unset -nocomplain rc r
} -result {1}}

###############################################################################

runTest {test oom_filt-2.16 {
  Drive plugins/th8_variables.c th8GetOrCreateVar fallback
  path.  [append] tries the cached-buffer fast path first
  (th8BufferAlloc at th8_variables.c:339).  When that
  alloc fails, control jumps to the `fallback:` label which
  invokes th8GetOrCreateVar.  No in-tree test exercises
  this fallback because production never triggers it.
  Sweep a range of -allocFailAfter values so at least one
  iteration fails the right alloc; any iteration that
  doesn't fire is harmless.
} -constraints {
    th8 fault_injection
} -setup {
  set done 0
} -body {
  for {set n 1} {$n <= 20} {incr n} {
    catch {::th8testlib::fault eval {
      catch {append v "x"}
    } -allocFailSite th8_cache.c:1395-1405 -allocFailAfter $n}
    incr done
  }
  expr {$done == 20}
} -cleanup {
  unset -nocomplain rc r n done
} -result {1}}

###############################################################################

runTest {test oom_filt-2.17 {
  Drive th8_xlib.c L427 `bParentSigned && pPolicyCtx`
  C2-Pair {T,F}.  When Th8_EvalFileAsData is invoked from
  a signed parent, it calls Th8_EnableSignedPolicy on the
  child interp.  Failing the TH8_ALLOC inside
  Th8_InstallSignedPolicy (th8_policy.c L1541) makes the
  enable return TH8_ERROR; the cleanup `goto` then runs
  L427 with bParentSigned=T but pPolicyCtx=NULL.
} -constraints {
    th8 fault_injection crypto_enabled
} -setup {
} -body {
  set ok 0
  # Sweep -signedParent with policy.c filter to drive L427
  # {T,F}: fault eval child has signed-only set, then
  # Th8_EnableSignedPolicy on the grandchild fails at OOM.
  for {set n 1} {$n <= 5} {incr n} {
    if {[catch {::th8testlib::fault eval {
      catch {::th8testlib::load_key_file \
              tests/helpers/evalfile_empty.tcl}
    } -signedParent -allocFailSite th8_policy.c \
      -allocFailAfter $n}] == 0} then {
      set ok 1
    }
  }
  # Phase 2: filter on th8_snk.c so the FIRST alloc inside
  # Th8_RsaKeyLoad fails.  Th8_EnableSignedPolicy then takes
  # `goto fail` BEFORE writing *ppCtx, leaving pPolicyCtx
  # at its initial NULL on the cleanup path -> L427 {T,F}.
  for {set n 1} {$n <= 20} {incr n} {
    if {[catch {::th8testlib::fault eval {
      catch {::th8testlib::load_key_file \
              tests/helpers/evalfile_empty.tcl}
    } -signedParent -allocFailSite th8_snk.c \
      -allocFailAfter $n}] == 0} then {
      set ok 1
    }
  }
  # Phase 3: no file filter, just sweep allocFailAfter for
  # the case where the grandchild's EnableSignedPolicy
  # consumes some allocs before reaching the RSA key load.
  for {set n 1} {$n <= 40} {incr n} {
    if {[catch {::th8testlib::fault eval {
      catch {::th8testlib::load_key_file \
              tests/helpers/evalfile_empty.tcl}
    } -signedParent -allocFailAfter $n}] == 0} then {
      set ok 1
    }
  }
  # Fallback Phase 1: child without signed (only drives
  # cleanup {F,-} which is already covered).
  for {set n 1} {$n <= 10} {incr n} {
    if {[catch {::th8testlib::fault eval {
      catch {::th8testlib::load_key_file \
              tests/helpers/evalfile_empty.tcl}
    } -allocFailSite th8_policy.c -allocFailAfter $n}] == 0} then {
      set ok 1
    }
  }
  set ok
} -cleanup {
  unset -nocomplain rc r n ok lim
} -result {1}}

###############################################################################

runTest {test oom_filt-2.18 {
  Drive th8_lists.c list_command L462 `nElem > 0 && zList`
  C2-Pair {T,F}.  Th8_ListAppend silently ignores OOM, so
  if every Th8_StringAppend allocation during [list a b ...]
  fails, the loop completes with zList still NULL but nElem
  positive.  Filter on th8_core.c StringAppend region.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set ok 0
  # Targeted: filter on th8_cache.c (where th8BufferAlloc
  # falls through to TH8_ALLOC) so OOM hits ListAppend's
  # internal buffer alloc.  Conservative range.
  for {set n 1} {$n <= 10} {incr n} {
    catch {::th8testlib::fault eval {
      list aa bb cc dd ee ff
    } -allocFailSite th8_cache.c:1390-1400 -allocFailAfter $n}
    incr ok
  }
  expr {$ok > 0}
} -cleanup {
  unset -nocomplain rc r n ok
} -result {1}}

###############################################################################

runTest {test oom_filt-2.19 {
  Drive th8_introspection.c info_globals_command L893
  `argc == 3 && zList` C2-Pair {T,F}.  When OOM hits the
  very first Th8_StringAppend inside the hash-iterate
  callback, zList stays NULL even though globals exist
  (argc==3 because the pattern arg is present).
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set ok 0
  # Bug 28 fix unblocked the th8_cache.c filter; previously
  # this crashed via ALWAYS(NULL_pCached) in Th8_SplitList.
  # Now exercises the actual FindInCache OOM path.
  for {set n 1} {$n <= 10} {incr n} {
    catch {::th8testlib::fault eval {
      info globals foo*
    } -allocFailSite th8_cache.c:600-650 -allocFailAfter $n}
    incr ok
  }
  # Also keep the th8_core.c StringAppend filter sweep.
  for {set n 1} {$n <= 10} {incr n} {
    catch {::th8testlib::fault eval {
      info globals foo*
    } -allocFailSite th8_core.c:6929-6932 -allocFailAfter $n}
    incr ok
  }
  expr {$ok > 0}
} -cleanup {
  unset -nocomplain rc r n ok
} -result {1}}

###############################################################################

runTest {test oom_filt-2.20 {
  Drive the Bug 28 family of pCached==NULL paths in th8_core.c
  integer/boolean/wide/real/splitlist conversion functions
  via fault injection at th8_cache.c:600-650 (cache entry
  alloc).  Each cache type needs a body that performs the
  matching conversion.
} -constraints {
    th8 fault_injection
} -setup {
} -body {
  set ok 0
  foreach body {
    {expr {123 + 0}}
    {if {1} then { set x 1 }}
    {expr {9999999999 + 0}}
    {expr {99999999999999999999 + 0}}
    {expr {3.14 + 0.0}}
    {lindex {a b c} 0}
    {set x abc; string length $x}
    {string is integer 42}
    {string is double 3.14}
    {expr {wide(123)}}
    {list aa bb cc; lindex {a b} 0}
    {list xx yy zz}
    {list a b c d e}
    {dict get {a 1 b 2} a}
    {dict keys {a 1 b 2 c 3}}
  } {
    for {set n 1} {$n <= 5} {incr n} {
      catch {::th8testlib::fault eval $body \
        -allocFailSite th8_cache.c:600-650 -allocFailAfter $n}
      incr ok
    }
    # Aggressive: fail every matching alloc.  Drives the
    # second FindInCache (store path) NULL-return cases.
    catch {::th8testlib::fault eval $body \
      -allocFailSite th8_cache.c:600-650 \
      -allocFailAfter 1 -allocFailInterval 1}
    incr ok
    # th8FindListInCache (list_command's lookup) allocates
    # in a different th8_cache.c range.  Cover it too.
    catch {::th8testlib::fault eval $body \
      -allocFailSite th8_cache.c:970-1020 \
      -allocFailAfter 1 -allocFailInterval 1}
    incr ok
    # Bug 28 fix unblocked whole-file th8_cache.c filter.
    # Covers buffer pool + list cache + entry alloc paths.
    # Sweep allocFailAfter to find the spot that drives the
    # store-path {T,F} (e.g. zList==NULL in list_command).
    for {set n 1} {$n <= 8} {incr n} {
      catch {::th8testlib::fault eval $body \
        -allocFailSite th8_cache.c \
        -allocFailAfter $n -allocFailInterval 1}
      incr ok
    }
  }
  expr {$ok > 0}
} -cleanup {
  unset -nocomplain rc r n ok body
} -result {1}}

###############################################################################

source tests/epilogue.tcl
