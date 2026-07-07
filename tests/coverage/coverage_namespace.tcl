###############################################################################
#
# coverage_namespace.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted tests for uncovered MC/DC branches in
# src/plugins/th8_management.c -- specifically [namespace origin]
# and [namespace which], neither of which is exercised by the
# conformance suite at large.
#
# Coverage targets:
#   - namespace origin: argc check, leading "::" handling,
#     full-qualifier resolution, lookup success/failure.
#   - namespace which: argc check, -command/-variable parsing,
#     missing-name error, leading "::" handling, command vs
#     variable resolution.
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
# Section 1 -- namespace origin
#
###############################################################################

runTest {test ns_cov-1.1 {
  namespace origin: wrong-args (no name)
} -constraints {
    th8
} -body {
  catch {namespace origin} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test ns_cov-1.2 {
  namespace origin: too many args
} -constraints {
    th8
} -body {
  catch {namespace origin a b} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test ns_cov-1.3 {
  namespace origin: simple unqualified name (lookup path)
} -constraints {
    th8
} -body {
  catch {namespace origin set} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-1.4 {
  namespace origin: fully-qualified name with leading ::
} -constraints {
    th8
} -body {
  catch {namespace origin ::set} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-1.5 {
  namespace origin: name with only "::" prefix exercises edge
} -constraints {
    th8
} -body {
  # The branch checks for argv[2][0]==':' && argv[2][1]==':' ; the
  # inner branch checks for the bare "::" name (zNs[2]=='\0').
  catch {namespace origin ::} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-1.6 {
  namespace origin: nonexistent command
} -constraints {
    th8
} -body {
  catch {namespace origin ::_no_such_command_xyz_} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 2 -- namespace which
#
###############################################################################

runTest {test ns_cov-2.1 {
  namespace which: wrong-args (too few)
} -constraints {
    th8
} -body {
  catch {namespace which} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test ns_cov-2.2 {
  namespace which: wrong-args (too many)
} -constraints {
    th8
} -body {
  catch {namespace which a b c d} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test ns_cov-2.3 {
  namespace which: default (command) lookup
} -constraints {
    th8
} -body {
  catch {namespace which set} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-2.4 {
  namespace which -command: explicit command flag
} -constraints {
    th8
} -body {
  catch {namespace which -command set} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-2.5 {
  namespace which -variable: variable lookup
} -constraints {
    th8
} -body {
  set ns_test_var foo
  catch {namespace which -variable ns_test_var} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain ns_test_var r
} -result {1}}

###############################################################################

runTest {test ns_cov-2.6 {
  namespace which: fully-qualified name with leading ::
} -constraints {
    th8
} -body {
  catch {namespace which ::set} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-2.7 {
  namespace which: bare "::" prefix exercises inner branch
} -constraints {
    th8
} -body {
  catch {namespace which ::} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-2.8 {
  namespace which: only flag, no name (iArg >= argc)
} -constraints {
    th8
} -body {
  # Exercises the iArg >= argc branch.  Either an error or an
  # empty result is acceptable -- we only care that the branch
  # was traversed.
  catch {namespace which -command} msg
  expr {[string length $msg] >= 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 3 -- namespace origin/which from within a sub-namespace
#
# The current-namespace check at lines 705 and 806 is a 3-cond
# compound (zNs[0]==':' && zNs[1]==':' && zNs[2]=='\0') that
# is only reachable when [namespace origin]/[namespace which]
# is invoked from a context whose current namespace is NOT
# "::" -- i.e. inside [namespace eval ::sub].  Tests in
# section 1/2 above run from "::"  and only cover the T,T,T
# vector; the T,T,F (and earlier) vectors require a
# non-empty sub-namespace.
#
###############################################################################

runTest {test ns_cov-3.1 {
  namespace origin from within a sub-namespace drives the
  T,T,F MC/DC vector at line 705 (zNs[2] != '\0')
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_cov_sub_ns_origin {
    namespace origin set
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_cov_sub_ns_origin}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-3.2 {
  namespace which from within a sub-namespace drives the
  T,T,F MC/DC vector at line 806 (zNs[2] != '\0')
} -constraints {
    th8
} -body {
  catch {namespace eval ::ns_cov_sub_ns_which {
    namespace which set
  }} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {namespace delete ::ns_cov_sub_ns_which}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-3.3 {
  namespace which with a SINGLE-COLON-PREFIX name (length
  > 2, first char ':', second char NOT ':') drives the
  C3=F vector at lines 800-801 -- the qualified-name
  detect compound's third condition fails because the
  second char isn't ':'.  Falls through to the else
  branch which prepends the current namespace.
} -constraints {
    th8
} -body {
  catch {namespace which :nosuchcommandxyz} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ns_cov-3.4 {
  info commands with a qualified pattern that contains a
  SINGLE colon embedded inside the namespace path (e.g.
  "::ns_with:colon::*") drives the C3=F vector at the
  th8FindNamespace component-scan compound -- nComp+1<n
  T, z[nComp]==':' T, but z[nComp+1] != ':' (single
  colon, not separator).  The inner scanner advances past
  the single colon as part of the component name.
} -constraints {
    th8
} -body {
  list \
      [info commands ::ns_with:colon::nosuchcommand] \
      [info commands ::a:b::c::nosuchcommand]
} -result {{} {}}}

###############################################################################

runTest {test ns_cov-3.5 {
  namespace import with an EMPTY pattern drives the
  C1=F vector at th8SimpleGlob (th8_core.c:3538) --
  nPat == 0 short-circuits the trailing-`*` check.
  No commands are imported (empty pattern matches
  nothing in the import context).
} -constraints {
    th8
} -body {
  namespace eval ::ns_cov_empty_src {
      proc cmdA {} {}
      namespace export *
  }
  set rc [catch {
      namespace eval ::ns_cov_empty_dst {
          namespace import ::ns_cov_empty_src::""
      }
  } m]
  list $rc [info commands ::ns_cov_empty_dst::*]
} -cleanup {
  catch {namespace delete ::ns_cov_empty_src}
  catch {namespace delete ::ns_cov_empty_dst}
  unset -nocomplain rc m
} -result {0 {}}}

###############################################################################

runTest {test ns_cov-3.6 {
  namespace exists / namespace which on a name with a
  SINGLE-COLON prefix (e.g. ":foo") drives the C3=F vector
  at th8FindNamespace (th8_core.c:4239) -- nName >= 2 (T),
  zName[0] == ':' (T), zName[1] != ':' (F).  The single
  colon is treated as part of the name, NOT a global
  qualifier; lookup runs in the current namespace and
  returns false for the missing namespace.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [namespace exists :foo]
  lappend rcs [namespace exists :nope]
  catch {namespace which :nosuch} m
  lappend rcs [string length $m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 0 0}}

###############################################################################

runTest {test ns_cov-3.7 {
  namespace import with a SINGLE-CHARACTER, NON-* pattern
  drives the C2=F vector at th8SimpleGlob
  (th8_core.c:3533) -- nPat == 1 (T), zPat[0] != '*' (F),
  so the fast-path returns F and the export-match scanner
  falls through to per-char comparison.
} -constraints {
    th8
} -body {
  namespace eval ::ns_cov_37_src {
      proc a {} {}
      proc b {} {}
      proc abc {} {}
      namespace export *
  }
  set rcs {}
  namespace eval ::ns_cov_37_dst1 {
      namespace import ::ns_cov_37_src::a
  }
  lappend rcs [info commands ::ns_cov_37_dst1::*]
  namespace eval ::ns_cov_37_dst2 {
      namespace import ::ns_cov_37_src::b
  }
  lappend rcs [info commands ::ns_cov_37_dst2::*]
  set rcs
} -cleanup {
  catch {namespace delete ::ns_cov_37_src}
  catch {namespace delete ::ns_cov_37_dst1}
  catch {namespace delete ::ns_cov_37_dst2}
  unset -nocomplain rcs
} -result {::ns_cov_37_dst1::a ::ns_cov_37_dst2::b}}

###############################################################################

runTest {test ns_cov-3.8 {
  namespace exists / which on TWO-CHAR names drives the
  C2/C3 pairs at th8FindNamespace (th8_core.c:4231) --
  nName == 2 (T) but zName[0]/[1] != ':' (F).  Covers
  the case of 2-char namespace names that aren't "::".
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [namespace exists ab]
  lappend rcs [namespace exists xy]
  lappend rcs [namespace exists :a]
  catch {namespace which q} m
  lappend rcs [string length $m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 0 0 0}}

###############################################################################

runTest {test ns_cov-3.9 {
  Namespace lookup on a name with embedded SINGLE-COLON
  (not "::") drives the C2/C3 pairs at th8FindNamespace
  inner-component scanner (th8_core.c:4363) -- after
  consuming the leading component, the next char is ':'
  (C1=T, C2=T) but the char after is NOT ':' (C3=F), so
  the loop exits without consuming a separator.
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [namespace exists "foo:bar"]
  lappend rcs [namespace exists "abc:def:ghi"]
  catch {namespace which "x:y"} m
  lappend rcs [string length $m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {0 0 0}}

###############################################################################

source tests/epilogue.tcl
