#!/usr/bin/env tclsh
#
# bestline_vendor.tcl -- Prepare bestline for TH8.
#
# Copies pristine source from vendor/ to build/.  vendor/ is NEVER
# modified.  Unlike the linenoise vendor script, no patches are
# applied -- bestline's built-in bracketed paste, UTF-8 support,
# and multi-line editing are used as-is.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

set scriptDir [file dirname [file normalize [info script]]]
set topDir    [file dirname $scriptDir]
set vendorDir [file join $topDir externals bestline vendor]
set buildDir  [file join $topDir externals bestline build]

proc copyFile {src dst} {
  set fd [open $src r]; fconfigure $fd -translation binary
  set d [read $fd]; close $fd
  file mkdir [file dirname $dst]
  set fd [open $dst w]; fconfigure $fd -translation binary
  puts -nonewline $fd $d; close $fd
}

proc prepare {} {
  global vendorDir buildDir

  puts "Preparing bestline for TH8..."
  puts "  Vendor: $vendorDir"
  puts "  Build:  $buildDir"

  if {[file exists $buildDir]} then { file delete -force $buildDir }
  file mkdir $buildDir

  foreach f {bestline.c bestline.h} {
    copyFile [file join $vendorDir $f] [file join $buildDir $f]
    puts "  Copied: $f"
  }

  puts "Done.  Build files are in $buildDir/"
}

prepare
