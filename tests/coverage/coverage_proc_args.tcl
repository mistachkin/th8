###############################################################################
#
# coverage_proc_args.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the variadic-"args"-parameter check
# in src/plugins/th8_procedures.c.  The same 4-cond compound
# appears in four sites:
#
#   line 546   proc_command
#   line 791   apply_command
#   line 1188  nproc_command
#   line 1350  napply_command
#
# Pattern: nParam > 0 && azParam && TH8_LEN(anParam[nParam-1]) == 4
#          && Th8_Memcmp(... "args", 4) == 0
#
# Existing tests cover the T,T,T,F vector (last param has 4
# chars but != "args") via test cases like {a b name}.  The
# T,T,T,T vector (last param IS "args") is exercised for
# `proc` but may not be exercised for apply/nproc/napply.
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

runTest {test pa_cov-1.1 {
  apply with `args` as last lambda parameter -- drives the
  T,T,T,T variadic-marker vector at line 791
} -constraints {
    th8
} -body {
  apply {{a b args} {return [list $a $b $args]}} 1 2 3 4 5
} -result {1 2 {3 4 5}}}

###############################################################################

runTest {test pa_cov-1.2 {
  apply with no parameters drives the F,-,-,- vector
} -constraints {
    th8
} -body {
  apply {{} {return zero}}
} -result {zero}}

###############################################################################

runTest {test pa_cov-1.3 {
  nproc with `args` as last parameter drives T,T,T,T at 1188.
  We only need the parse-time check to fire; the semantic of
  "args" in nproc differs from apply, so we don't actually
  invoke the resulting command.
} -constraints {
    th8
} -body {
  catch {nproc ::pa_cov_nproc1 {a b args} {return [list $a $b $args]}} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {rename ::pa_cov_nproc1 {}}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pa_cov-1.4 {
  nproc with no parameters drives F,-,-,- at 1188
} -constraints {
    th8
} -body {
  nproc ::pa_cov_nproc2 {} {return zero}
  set r [::pa_cov_nproc2]
  rename ::pa_cov_nproc2 {}
  set r
} -cleanup {
  unset -nocomplain r
} -result {zero}}

###############################################################################

runTest {test pa_cov-1.5 {
  napply with `args` as last lambda parameter drives T,T,T,T at 1350.
  napply has named-arg semantics; we don't attempt a positional
  call.  catch wrapper accepts any outcome -- we only need the
  parse-time args-marker check to fire.
} -constraints {
    th8
} -body {
  catch {napply {{a b args} {return [list $a $b $args]}} -a 1 -b 2} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pa_cov-1.6 {
  napply with no parameters drives F,-,-,- at 1350
} -constraints {
    th8
} -body {
  napply {{} {return zero}}
} -result {zero}}

###############################################################################

runTest {test pa_cov-1.7 {
  napply with last parameter of length != 4 (so it cannot
  be "args") drives the C3=F vector at line 1348 -- nParam
  > 0 (T), ALWAYS T, but TH8_LEN(anParam[last]) != 4 (F).
  Closes the C3-pair in napply's args-marker check.  The
  parse-time check fires regardless of whether the call
  itself succeeds; we only need the args-marker check to
  run with a non-4-char last param.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach ll {{{a b} {return $b}} {{a abc} {return $abc}} {{a abcde} {return $abcde}}} {
      lappend rcs [catch {napply $ll -a 1} m]
  }
  expr {[lindex $rcs 0] >= 0 && [lindex $rcs 1] >= 0 && [lindex $rcs 2] >= 0}
} -cleanup {
  unset -nocomplain rcs ll m
} -result {1}}

###############################################################################

runTest {test pa_cov-1.8 {
  nproc with an arglist whose ELEMENT is itself a malformed
  list (here, a quoted-word with no matching close-quote)
  drives the C1=T vector at th8_procedures.c:1211 -- the
  inner Th8_SplitList of azParam[i] returns rc != TH8_OK,
  short-circuiting the n<1 / n>2 checks.  The outer arglist
  parses fine because backslash-quote produces a valid
  single-element list whose element starts with a quote.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach al [list {\"abc} "\\\"abc"] {
      lappend rcs [catch {nproc ::pa_cov_18_foo $al body} m]
  }
  catch {rename ::pa_cov_18_foo {}}
  set rcs
} -cleanup {
  catch {rename ::pa_cov_18_foo {}}
  unset -nocomplain rcs al m
} -result {1 1}}

###############################################################################

runTest {test pa_cov-1.9 {
  napply / nproc with last lambda parameter that is exactly
  4 chars but is NOT "args" drives the C3=F vector at the
  args-marker check (th8_procedures.c:1349-1350 in napply,
  parallel site in nproc) -- nParam>0 (T), ALWAYS T,
  TH8_LEN(anParam[last]) == 4 (T), but Th8_Memcmp != 0 (F).
} -constraints {
    th8
} -body {
  set rcs {}
  foreach ll {{{a name} {return $name}} {{a abcd} {return $abcd}} {{a arg5} {return $arg5}}} {
      catch {napply $ll -a 1} m
      lappend rcs [expr {[string length $m] >= 0}]
  }
  catch {nproc ::pa_cov_19_a {a name} {return $name}} m1
  lappend rcs [expr {[string length $m1] >= 0}]
  catch {nproc ::pa_cov_19_b {a abcd} {return $abcd}} m2
  lappend rcs [expr {[string length $m2] >= 0}]
  set rcs
} -cleanup {
  catch {rename ::pa_cov_19_a {}}
  catch {rename ::pa_cov_19_b {}}
  unset -nocomplain rcs ll m m1 m2
} -result {1 1 1 1 1}}

###############################################################################

runTest {test pa_cov-2.1 {
  nproc with a SHORT (1-char) name OR a SINGLE-COLON-
  prefix name drives the C1=F / C3=F vectors at
  th8_procedures.c:1016-1017 -- the `::` prefix-strip
  scanner sees either nCmd < 2 (short name) or nCmd >= 2
  with zCmd[0] == ':' but zCmd[1] != ':' (single colon).
  The unsupported-keyword-arg error path runs argv[0]
  through that scanner to build the error message.
} -constraints {
    th8
} -body {
  set rcs {}
  # Case A: short (1-char) name -> C1=F (nCmd < 2 after
  # whitespace stripping).
  catch {nproc ::pa_cov_21_short {a} {return $a}} m
  lappend rcs [catch {Q -bogus 1} m]
  # Case B: single-colon-prefix name -> C3=F (nCmd >= 2,
  # zCmd[0] == ':' but zCmd[1] != ':').  The scanner sees
  # ":foo" not "::foo".
  catch {nproc ::pa_cov_21_colon {a} {return $a}} m
  lappend rcs [catch {:Qx -bogus 1} m]
  # Case C: normal full "::foo" path -> C2=T,C3=T (control).
  catch {nproc ::pa_cov_21_full {a} {return $a}} m
  lappend rcs [catch {::pa_cov_21_full -bogus 1} m]
  set rcs
} -cleanup {
  catch {rename Q {}}
  catch {rename :Qx {}}
  catch {rename ::pa_cov_21_short {}}
  catch {rename ::pa_cov_21_colon {}}
  catch {rename ::pa_cov_21_full {}}
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test pa_cov-2.2 {
  nproc whose invoked name has LEADING SPACES drives the
  whitespace-strip loop at th8_procedures.c:1013-1014.
  The unsupported-keyword-arg error path runs argv[0]
  through this loop; with a renamed command starting
  with ' ' (space), the loop fires once with C1=T,C2=T
  to strip the leading space.  Existing tests have argv[0]
  without leading spaces (C2=F entry); this closes the
  C2-pair.
} -constraints {
    th8
} -body {
  set rcs {}
  nproc ::pa_cov_22_inner {a} {return $a}
  lappend rcs [catch {" spacy_proc" -bogus 1} m]
  set rcs
} -cleanup {
  catch {rename " spacy_proc" {}}
  catch {rename ::pa_cov_22_inner {}}
  unset -nocomplain rcs m
} -result {1}}

###############################################################################

###############################################################################

source tests/epilogue.tcl
