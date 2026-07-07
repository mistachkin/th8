###############################################################################
#
# coverage_nspat_resolve.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c L4076 inside
# th8ResolveNsPattern:
#
#   if (!(nPat >= 2 && zPat[0] == ':' && zPat[1] == ':')) { ... }
#
# This guards the "relative pattern" branch.  Existing [info
# commands] tests drive (T,T,T) [absolute "::foo::*"] and (T,F,-)
# [relative "foo::*"].  The (T,T,F) vector -- single colon
# followed by non-colon, e.g. ":foo::*" -- has no coverage.
#
# th8ResolveNsPattern is reached because the pattern contains
# "::" somewhere (the bHasNsSep scan at L4068 succeeds).  With
# zPat[0]==':' but zPat[1]!=':', the L4076 NOT is true and the
# relative-resolution branch fires, treating ":foo" as a
# relative segment name.  The lookup will not match any real
# namespace, but the decision is exercised.
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

runTest {test nspat_resolve-1.1 {
  [info commands :foo::*] drives th8ResolveNsPattern L4076
  vector (T,T,F): nPat>=2, zPat[0]==':', zPat[1]!=':'.  The
  pattern is treated as relative and resolved against the
  current namespace, yielding no matches (the ":foo"
  prefix isn't a valid namespace name).
} -constraints {
    th8
} -setup {
} -body {
  set r [info commands :nspr_no_such::*]
  expr {[llength $r] == 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test nspat_resolve-1.2 {
  [info vars :foo::bar] -- variation that exercises the same
  th8ResolveNsPattern decision via [info vars] entry point.
} -constraints {
    th8
} -setup {
} -body {
  set r [info vars :nspr_vno::xyz]
  expr {[llength $r] == 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
