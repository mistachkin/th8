#!/usr/bin/env tclsh
###############################################################################
#
# check_buildconfig.tcl --
#
# TH8K-029 build-configuration hermeticity guard.  Detects (and rejects) an
# incompatible object set: object files left in the build directory by a
# PREVIOUS build configuration, which -- if linked against freshly-built
# objects or a regenerated stub table -- produce a mixed, incoherent library.
# The concrete failure was toggling ENABLE_TEST_KEY without `make clean`,
# leaving the stub table's test-key slots NULL while the core still exported
# the functions, so a coverage test called a NULL stub.
#
# Cross-platform (invoked identically from Makefile and Makefile.msc via
# tclsh) so the compare logic cannot diverge between the POSIX shell and
# cmd.exe.
#
# Usage:
#   tclsh tools/check_buildconfig.tcl <stampFile> <objDir> <configSig>
#
# Writes <configSig> to <stampFile>.  If <stampFile> already holds a DIFFERENT
# signature AND <objDir> contains object files (*.o or *.obj), it prints a
# diagnostic and exits 1.  `make clean` / `fresh` remove <objDir> (and the
# stamp), so this never fires on the documented clean-first workflow -- only on
# an in-place configuration switch.  Exit 0 otherwise.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

proc main {args} {
  if {[llength $args] != 3} then {
    puts stderr "usage: check_buildconfig.tcl <stampFile> <objDir> <configSig>"
    exit 2
  }
  lassign $args stampFile objDir sig

  #
  # Read the previously-stamped signature, if any.
  #

  set old ""
  set havePrev 0
  if {[file exists $stampFile]} then {
    set f [open $stampFile r]
    set old [read $f]
    close $f
    set havePrev 1
  }

  #
  # Compare (and later store) whitespace-normalized signatures so a trailing
  # newline or incidental spacing difference is not mistaken for a config
  # change.  What matters is the set of flags, not their surrounding
  # whitespace.
  #

  set oldN [string trim $old]
  set sigN [string trim $sig]

  #
  # A config change is only a hazard when stale objects from the previous
  # configuration are actually present; on a fresh (clean) build directory we
  # simply record the new signature.
  #

  if {$havePrev && $oldN ne $sigN} then {
    set objs [concat \
        [glob -nocomplain -directory $objDir -- *.o] \
        [glob -nocomplain -directory $objDir -- *.obj]]
    if {[llength $objs] > 0} then {
      puts stderr "*** ERROR (TH8K-029): incompatible object set in $objDir."
      puts stderr "***   The build configuration changed but object files from"
      puts stderr "***   the previous configuration are still present; linking"
      puts stderr "***   them would produce a mixed library (e.g. a stub table"
      puts stderr "***   out of sync with the core -- the ENABLE_TEST_KEY"
      puts stderr "***   NULL-stub bug)."
      puts stderr "***   previous config: [string trim $old]"
      puts stderr "***   current  config: [string trim $sig]"
      puts stderr "***   Run \"make clean\" (or \"make ... fresh\") before"
      puts stderr "***   switching the build configuration."
      exit 1
    }
  }

  #
  # Record (or refresh) the current signature.  Only rewrite when it changed,
  # to avoid touching the stamp's mtime on every no-op build.
  #

  if {!$havePrev || $oldN ne $sigN} then {
    set f [open $stampFile w]
    puts -nonewline $f $sigN
    close $f
  }
  exit 0
}

main {*}$argv
