###############################################################################
#
# coverage_import_noexport.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_core.c th8ImportCallback L3703:
#
#   if (pSrcNs->zExport && pSrcNs->nExport > 0) { ... }
#
# Existing namespace-import tests always export commands first
# (`namespace export NAME`) so the import callback sees
# pSrcNs->zExport set with nExport > 0 and only the (T,T)
# vector ever fires.  An import attempt against a namespace
# that has NO exports drives C1=F (zExport is NULL) for every
# command in the source namespace.
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

runTest {test impnoexp-1.1 {
  namespace import on a source namespace with no exports
  drives th8ImportCallback L3703 C1=F vector
  (zExport==NULL).  The import succeeds silently --
  zero commands match -- and the namespace is cleaned
  up at the end of the test.
} -constraints {
    th8
} -setup {
} -body {
  namespace eval ::imp_src_xyz {
      proc foo {} { return foo-result }
      proc bar {} { return bar-result }
      # Deliberately NO namespace export -- zExport stays NULL.
  }
  catch {namespace import ::imp_src_xyz::*} r
  set r
} -cleanup {
  unset -nocomplain r
  catch {namespace delete ::imp_src_xyz}
} -result {}}

###############################################################################

runTest {test impnoexp-1.2 {
  namespace import on a source namespace whose zExport
  buffer is allocated but length-zero -- drives the C2=F
  vector at th8ImportCallback L3703 (zExport non-NULL,
  nExport == 0).  Reachable by calling [namespace export]
  with an empty pattern: Th8_StringAppend allocates the
  buffer (so zExport != NULL) but nApp == 0, leaving
  nExport at zero.  Closes the C2-Pair that the original
  no-export test (impnoexp-1.1, zExport == NULL, C1=F)
  cannot reach.
} -constraints {
    th8
} -setup {
} -body {
  namespace eval ::imp_src_xyz2 {
      proc foo {} { return foo-result }
      namespace export ""
  }
  catch {namespace import ::imp_src_xyz2::*} r
  set r
} -cleanup {
  unset -nocomplain r
  catch {namespace delete ::imp_src_xyz2}
} -result {}}

###############################################################################

source tests/epilogue.tcl
