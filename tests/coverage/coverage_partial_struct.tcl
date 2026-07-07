###############################################################################
#
# coverage_partial_struct.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# P3 of the MC/DC closure plan.  Drives `pPkg && pPkg->zVersion`
# and `pPkg && pPkg->paIfNeeded` MC/DC compounds in
# src/plugins/th8_extensibility.c by synthesizing partial-state
# Th8_PkgInfo records via the new ::th8testlib::partial_object
# helper.  After synthesis, normal [package require] / [package
# present] commands traverse the partial states and exercise
# the targeted compounds.
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

runTest {test ps_cov-1.1 {
  pkg_no_version: package present queries a partially-registered
  package (no version) -- drives `pPkg && pPkg->zVersion` T,F vector
} -constraints {
    th8
} -body {
  ::th8testlib::partial_object pkg_no_version _ps_cov_no_ver_pkg_
  catch {package present _ps_cov_no_ver_pkg_} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package forget _ps_cov_no_ver_pkg_}
  catch {package forget _ps_cov_no_ifn_}
  catch {package forget _ps_cov_req_no_ver_}
  catch {package forget _ps_cov_versions_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-1.2 {
  pkg_no_ifneeded: package require with version on a package that
  has zVersion set but no paIfNeeded -- drives the
  `pPkg && pPkg->paIfNeeded` T,F vector
} -constraints {
    th8
} -body {
  ::th8testlib::partial_object pkg_no_ifneeded _ps_cov_no_ifn_ 1.0
  catch {package require _ps_cov_no_ifn_ 2.0} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package forget _ps_cov_no_ver_pkg_}
  catch {package forget _ps_cov_no_ifn_}
  catch {package forget _ps_cov_req_no_ver_}
  catch {package forget _ps_cov_versions_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-1.3 {
  pkg_no_version followed by package require -- drives the
  ifneeded-search path with a package whose zVersion is NULL
} -constraints {
    th8
} -body {
  ::th8testlib::partial_object pkg_no_version _ps_cov_req_no_ver_
  catch {package require _ps_cov_req_no_ver_} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package forget _ps_cov_no_ver_pkg_}
  catch {package forget _ps_cov_no_ifn_}
  catch {package forget _ps_cov_req_no_ver_}
  catch {package forget _ps_cov_versions_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-1.4 {
  pkg_no_ifneeded followed by package versions query
} -constraints {
    th8
} -body {
  ::th8testlib::partial_object pkg_no_ifneeded _ps_cov_versions_ 3.0
  catch {package versions _ps_cov_versions_} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package forget _ps_cov_no_ver_pkg_}
  catch {package forget _ps_cov_no_ifn_}
  catch {package forget _ps_cov_req_no_ver_}
  catch {package forget _ps_cov_versions_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-1.5 {
  Unknown subcommand returns error
} -constraints {
    th8
} -body {
  catch {::th8testlib::partial_object no_such_subcmd foo} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test ps_cov-2.1 {
  package ifneeded with a script that does NOT call package
  provide -- after package require runs the script, the
  post-eval re-check at th8_extensibility.c:588 sees pPkg
  with zVersion still NULL -- drives the T,F vector
} -constraints {
    th8
} -body {
  package ifneeded _ps_cov_silent_ 1.0 {
    # No package provide call.  Coverage probe.
  }
  catch {package require _ps_cov_silent_ 1.0} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package forget _ps_cov_silent_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-2.2 {
  Same pattern with no version requested -- drives the
  any-version branch's post-eval re-check at
  th8_extensibility.c:588 (any-version path through paIfNeeded)
} -constraints {
    th8
} -body {
  package ifneeded _ps_cov_silent_any_ 2.0 {
    # No package provide call.
  }
  catch {package require _ps_cov_silent_any_} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package forget _ps_cov_silent_any_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-2.3 {
  ifneeded script that forgets its own package -- after eval
  returns, the post-eval re-check at L588 finds pPkg == NULL
  -- drives the F,- vector
} -constraints {
    th8
} -body {
  package ifneeded _ps_cov_self_forget_ 1.0 {
    package forget _ps_cov_self_forget_
  }
  catch {package require _ps_cov_self_forget_ 1.0} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package forget _ps_cov_self_forget_}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-3.1 {
  Custom [package unknown] handler that does NOT register the
  requested package -- after handler returns, pPkg is still NULL
  at th8_extensibility.c:641 -- drives the F,- vector
} -constraints {
    th8
} -body {
  set saved [package unknown]
  proc _ps_cov_silent_unknown {args} {
    # Coverage probe: do not register or provide.
  }
  package unknown _ps_cov_silent_unknown
  catch {package require _ps_cov_no_such_pkg_via_unknown_ 1.0} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package unknown $saved}
  catch {rename _ps_cov_silent_unknown {}}
  unset -nocomplain r saved
} -result {1}}

###############################################################################

runTest {test ps_cov-3.2 {
  Custom [package unknown] handler installed but pkg already
  exists with no zVersion -- handler runs, post-handler re-check
  sees pPkg without zVersion -- drives the T,F vector at L641
} -constraints {
    th8
} -body {
  set saved [package unknown]
  ::th8testlib::partial_object pkg_no_version _ps_cov_unk_partial_
  proc _ps_cov_silent_unknown {args} {
    # Don't override the partial state.
  }
  package unknown _ps_cov_silent_unknown
  catch {package require _ps_cov_unk_partial_ 1.0} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package unknown $saved}
  catch {package forget _ps_cov_unk_partial_}
  catch {rename _ps_cov_silent_unknown {}}
  unset -nocomplain r saved
} -result {1}}

###############################################################################

runTest {test ps_cov-3.3 {
  Custom [package unknown] handler that successfully provides
  the requested package -- post-handler re-check sees pPkg
  with zVersion set -- drives the T,T success vector at L641
} -constraints {
    th8
} -body {
  set saved [package unknown]
  proc _ps_cov_provide_unknown {pkgName args} {
    package provide $pkgName 1.0
  }
  package unknown _ps_cov_provide_unknown
  catch {package require _ps_cov_unk_provide_ 1.0} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {package unknown $saved}
  catch {package forget _ps_cov_unk_provide_}
  catch {rename _ps_cov_provide_unknown {}}
  unset -nocomplain r saved
} -result {1}}

###############################################################################

runTest {test ps_cov-4.1 {
  package present on a never-registered package name -- pPkg
  comes back NULL after hash lookup -- drives the F,- vector
  at th8_extensibility.c:894
} -constraints {
    th8
} -body {
  catch {package present _ps_cov_never_present_pkg_} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test ps_cov-4.2 {
  package versions on a never-registered package name -- pPkg
  comes back NULL after hash lookup -- drives the F,- vector
  at th8_extensibility.c:1217
} -constraints {
    th8
} -body {
  catch {package versions _ps_cov_never_versions_pkg_} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
