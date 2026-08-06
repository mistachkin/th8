###############################################################################
#
# mimalloc_vendor.tcl --
#
#     Populate externals/mimalloc/build/ from the pristine upstream
#     externals/mimalloc/vendor/ tree, overlaying any whole-file patches
#     from externals/mimalloc/patches/ (each patch mirrors its
#     vendor-relative path).  The TH8 build compiles mimalloc from build/
#     so that vendor/ stays canonical upstream -- the same vendoring
#     model libtommath uses (see tools/tommath_amalg.tcl).
#
#     mimalloc is linked STATICALLY into libth8, so correctness fixes to
#     it belong in a tracked patch here, never a Valgrind suppression.
#
#     Usage:  tclsh tools/mimalloc_vendor.tcl
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set vendorDir externals/mimalloc/vendor
set buildDir  externals/mimalloc/build
set patchDir  externals/mimalloc/patches

if {![file exists [file join $vendorDir include mimalloc.h]]} then {
  puts stderr "mimalloc not found in $vendorDir"
  exit 1
}

#
# copyTree -- recursively copy every file under src into dst, preserving
# the directory structure.
#
proc copyTree {src dst} {
  file mkdir $dst
  foreach f [lsort [glob -nocomplain -directory $src *]] {
    set child [file join $dst [file tail $f]]
    if {[file isdirectory $f]} then {
      copyTree $f $child
    } else {
      file copy -force $f $child
    }
  }
}

#
# overlayPatches -- recursively overlay whole-file .c/.h patches onto the
# build/ tree at their mirrored relative path.  Non-source files (e.g.
# README.md) and unified-diff files (first line "---"/"diff") are skipped,
# so only complete replacement files take effect.
#
proc overlayPatches {src dst} {
  foreach f [lsort [glob -nocomplain -directory $src *]] {
    set tail [file tail $f]
    set child [file join $dst $tail]
    if {[file isdirectory $f]} then {
      overlayPatches $f $child
    } elseif {[string match "*.c" $tail] || [string match "*.h" $tail]} then {
      set fd [open $f r]
      set first [gets $fd]
      close $fd
      if {![string match "---*" $first] && ![string match "diff*" $first]} then {
        file mkdir [file dirname $child]
        file copy -force $f $child
        puts "patched: $child"
      }
    }
  }
}

if {[file exists $buildDir]} then {
  file delete -force $buildDir
}

#
# Only the compiled inputs are copied: src/ (static.c #includes every
# other .c) and include/ (the -I search root).  test/, docs/, cmake/,
# etc. are not part of the TH8 build and stay only in vendor/.
#
foreach sub {src include} {
  copyTree [file join $vendorDir $sub] [file join $buildDir $sub]
}

if {[file isdirectory $patchDir]} then {
  overlayPatches $patchDir $buildDir
}

puts "mimalloc build/ populated from vendor/ + patches/"
