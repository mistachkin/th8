###############################################################################
#
# load.tcl --
#
# Tcl Language Standard
# Test Suite Infrastructure Package File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::th8test {
  #
  # detectLoadLib --
  #
  # Detect the test shared library and set the "loadLib" constraint.
  #
  proc detectLoadLib {} {
    set ::testlib_name ""
    #
    # Independent throwaway load target (TH8 only): the same testlib
    # file under the "Th8loadtest" symbol.  Because TH8 tracks loads
    # by (file-identity, symbol), this is a SEPARATE refcounted entry
    # from the harness's "Th8test" load, so [load]/[unload] tests can
    # fully control its lifetime (load -> ::__th8_loadtest_marker
    # appears, unload -> it disappears) without the harness's own
    # reference keeping the testlib's commands alive.  Left empty in
    # the non-TH8 (native Tcl) case.
    #
    set ::loadtest_name ""

    #
    # Detect the test shared library by probing for it.
    # If the prologue already loaded it via
    #   catch {package require th8testlib}
    # we still need to identify the file path so that
    # testLoadLib/testUnloadLib can use it.
    #
    # IMPORTANT: do NOT unload the library here.  The prologue
    # loaded it and the epilogue will handle cleanup.  Unloading
    # during detection removes persistent global commands like
    # ::__test_only_exec that other tests depend on.
    #

    if {[testConstraint th8]} then {
      foreach rootName {libth8test th8test} {
        foreach extension {.dylib .so .dll} {
          set fileName [file join bin ${rootName}${extension}]

          if {[file exists $fileName]} then {
            set ::testlib_name [file normalize $fileName]:Th8test
            set ::loadtest_name [file normalize $fileName]:Th8loadtest
            tputs stdout "---- set testlib to: $::testlib_name\n"
            break
          }
        }

        if {[info exists ::testlib_name] && \
            [string length $::testlib_name] > 0} then {
          break
        }
      }
    } else {
      foreach rootName {libtclth8test tclth8test} {
        foreach extension {.dylib .so .dll} {
          set fileName [file join bin ${rootName}${extension}]

          if {[file exists $fileName]} then {
            set ::testlib_name [file normalize $fileName]
            tputs stdout "---- set testlib to: $::testlib_name\n"
            break
          }
        }

        if {[info exists ::testlib_name] && \
            [string length $::testlib_name] > 0} then {
          break
        }
      }
    }

    set result [testConstraint loadLib [expr {
      [info exists ::testlib_name] && [string length $::testlib_name] > 0
    }]]

    if {[string length $result] == 0} then {set result <none>}
    tputs stdout "---- detect testlib result: $result\n"

    return $result
  }

  #
  # haveLoadLib --
  #
  # Checks if the test shared library is loaded.
  #
  proc haveLoadLib {} {
    if {[info exists ::testlib_name] && \
        [string length $::testlib_name] > 0} then {
      set fileNameOnly(1) [file tail $::testlib_name]

      #
      # [info loaded] returns a list of {name refCount} sublists,
      # one per loaded library.  Extract the name from each entry
      # and compare its file tail to the testlib name.
      #
      foreach entry [info loaded] {
        set fileNameOnly(2) [file tail [lindex $entry 0]]

        if {$fileNameOnly(2) eq $fileNameOnly(1)} then {
          return 1
        }
      }
    }

    return 0
  }

  #
  # testLoadLib --
  #
  # Load the test shared library, abstracting syntax differences
  # between TH8 (single arg with colon) and native Tcl (two args).
  #
  proc testLoadLib { {force true} } {
    if {[testConstraint loadLib]} then {
      if {$force || ![haveLoadLib]} then {
        if {[testConstraint th8]} then {
          set result [load $::testlib_name]
        } else {
          set result [load $::testlib_name Tclth8test]
        }

        if {[string length $result] == 0} then {set result <none>}
        tputs stdout "---- testlib load result: $result\n"

        return $result
      } else {
        return already_loaded
      }
    } else {
      return unavailable
    }
  }

  #
  # testUnloadLib --
  #
  # Unload the test shared library.
  #
  proc testUnloadLib { args } {
    if {[haveLoadLib]} then {
      if {[testConstraint th8]} then {
        set result [eval unload $args [list $::testlib_name]]
      } else {
        set result [eval unload $args [list $::testlib_name Tclth8test]]
      }

      if {[string length $result] == 0} then {set result <none>}
      tputs stdout "---- testlib unload result: $result\n"

      return $result
    } else {
      return already_unloaded
    }
  }

  namespace export detectLoadLib haveLoadLib testLoadLib testUnloadLib
  namespace eval :: {namespace import -force ::th8test::*}

  package provide th8test_load 1.0
}

