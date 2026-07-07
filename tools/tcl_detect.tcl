#!/usr/bin/env tclsh
###############################################################################
#
# tcl_detect.tcl --
#
#     Detect Tcl installation paths from a running tclsh.  Used by
#     the Makefile for auto-detection of TCL_INCLUDE, TCL_STUBS_LIB,
#     and TCL_LIB.
#
# Usage:
#     tclsh tools/tcl_detect.tcl prefix     → /usr/local, /opt/homebrew, etc.
#     tclsh tools/tcl_detect.tcl include    → directory containing tcl.h
#     tclsh tools/tcl_detect.tcl libdir     → directory containing libs
#     tclsh tools/tcl_detect.tcl stubs      → path to libtclstub*.a
#     tclsh tools/tcl_detect.tcl shlib      → path to libtcl*.so/dylib
#     tclsh tools/tcl_detect.tcl version    → e.g. 8.6
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

proc detect {what} {
  set ver [info tclversion]

  #
  # Try tcl::pkgconfig first (Tcl 8.5+).
  #
  set prefix ""
  set libdir ""
  set incdir ""

  catch {set prefix [::tcl::pkgconfig get installprefix,runtime]}
  if {$prefix eq ""} then {
    catch {set prefix [file dirname [file dirname [info nameofexecutable]]]}
  }

  catch {set libdir [::tcl::pkgconfig get libdir,runtime]}
  if {$libdir eq ""} then {
    set libdir [file join $prefix lib]
  }

  catch {set incdir [::tcl::pkgconfig get includedir,runtime]}
  if {$incdir eq ""} then {
    #
    # Search common include subdirectories.
    #
    foreach sub [list \
        include/tcl-tk \
        include/tcl$ver \
        include \
        ] {
      set try [file join $prefix $sub]
      if {[file exists [file join $try tcl.h]]} then {
        set incdir $try
        break
      }
    }
  }

  switch -exact -- $what {
    prefix  { return $prefix }
    include { return $incdir }
    libdir  { return $libdir }
    version { return $ver }
    stubs {
      foreach pat [list \
          libtclstub$ver.a \
          libtclstub[string map {. {}} $ver].a \
          libtclstub.a \
          ] {
        set try [file join $libdir $pat]
        if {[file exists $try]} then {
          return $try
        }
      }
      return ""
    }
    shlib {
      set ext [expr {$::tcl_platform(os) eq "Darwin" ? ".dylib" : ".so"}]
      foreach pat [list \
          libtcl$ver$ext \
          libtcl[string map {. {}} $ver]$ext \
          ] {
        set try [file join $libdir $pat]
        if {[file exists $try]} then {
          return $try
        }
      }
      return ""
    }
    default {
      puts stderr "Usage: tclsh tcl_detect.tcl prefix|include|libdir|stubs|shlib|version"
      exit 1
    }
  }
}

if {[llength $argv] != 1} then {
  puts stderr "Usage: tclsh tcl_detect.tcl prefix|include|libdir|stubs|shlib|version"
  exit 1
}

puts [detect [lindex $argv 0]]
