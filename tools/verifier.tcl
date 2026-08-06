#!/usr/bin/env tclsh
###############################################################################
#
# verifier.tcl --
#
#     Verify Harpy signatures on all script files (*.tcl, *.eagle, *.sh)
#     in the TH8 project tree.
#
#     For each script file that has a corresponding .b64sig signature
#     file, the TH8 shell's th8testlib::sig_hashes command is used to
#     independently compute the SHA-512 hash of the file and compare it
#     against the hash recovered from the RSA signature.
#
#     Files without a .b64sig are reported as unsigned.
#
# Usage:
#     tclsh tools/verifier.tcl ?-quiet? ?-dir PATH? ?-shell PATH?
#
#     -quiet    Only print failures and the summary (suppress OK lines).
#     -dir      Project root directory (default: directory containing
#               this script's parent).
#     -shell    Path to the TH8 shell binary (default: bin/th8sh).
#     -skip     Glob pattern for directories to skip (may be repeated).
#     -fix      Automatically re-sign test key files with production key.
#     -force    Re-sign test key files when they are not correctly signed.
#
# Exit code:
#     0   All signed files verified successfully.
#     1   One or more verification failures or errors.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

proc findScriptFiles {dir skipPatterns} {
  set results [list]
  foreach pattern {*.tcl *.th8 *.eagle} {
    foreach f [glob -nocomplain -directory $dir $pattern] {
      if {[string match "*.b64sig" $f]} then { continue }
      set skip 0
      foreach sp $skipPatterns {
        if {[string match $sp $f]} then { set skip 1; break }
      }
      if {!$skip} then { lappend results $f }
    }
  }
  foreach child [glob -nocomplain -directory $dir -types d *] {
    set tail [file tail $child]
    if {$tail eq "." || $tail eq ".."} then { continue }
    set skip 0
    foreach sp $skipPatterns {
      if {[string match $sp $child] ||
        [string match $sp $tail]} then { set skip 1; break }
    }
    if {!$skip} then {
      foreach f [findScriptFiles $child $skipPatterns] {
        lappend results $f
      }
    }
  }
  return $results
}

proc findSignatureFiles {dir skipPatterns} {
  set results [list]
  foreach pattern {*.b64sig} {
    foreach f [glob -nocomplain -directory $dir $pattern] {
      set skip 0
      foreach sp $skipPatterns {
        if {[string match $sp $f]} then {
          set skip 1
          break
        }
      }
      if {!$skip} then {
        set f [string range $f 0 end-7]; # remove ".b64sig"
        lappend results $f
      }
    }
  }
  foreach child [glob -nocomplain -directory $dir -types d *] {
    set tail [file tail $child]
    if {$tail eq "." || $tail eq ".."} then { continue }
    set skip 0
    foreach sp $skipPatterns {
      if {[string match $sp $child] || [string match $sp $tail]} then {
        set skip 1
        break
      }
    }
    if {!$skip} then {
      foreach f [findSignatureFiles $child $skipPatterns] {
        lappend results $f
      }
    }
  }
  return $results
}

proc verifyFile {shell projectDir scriptPath} {
  #
  # Construct a TH8 script that:
  #   1. Loads the test infrastructure (for testLoadLib).
  #   2. Loads the test shared library (for sig_hashes).
  #   3. Calls sig_hashes on the target file.
  #   4. Prints the match result.
  #
  # The relative path must be computed from the project root
  # because the TH8 shell resolves paths relative to its base
  # directory.
  #

  if {[string range $scriptPath 0 \
      [expr {[string length $projectDir]}]] eq \
      "${projectDir}/"} then {
    set relPath [string range $scriptPath \
        [expr {[string length $projectDir] + 1}] end]
  } else {
    set relPath $scriptPath
  }

  set evalScript [subst -nocommands {
    package require th8
    package require th8test
    package require th8test_load
    initializeTests
    detectLoadLib
    testLoadLib
    if {[llength [info commands ::th8testlib::sig_hashes]] == 0} then {
      puts "ERROR: sig_hashes command not available"
      exit
    }
    if {[catch {::th8testlib::sig_hashes {$relPath}} result]} then {
      puts "ERROR: \$result"
    } else {
      puts "RESULT: \$result"
    }
  }]

  set env_save [list]
  foreach var {TH8SH_NO_SCRIPT_SECURITY TH8SH_YES_TESTLIB} {
    if {[info exists ::env($var)]} then {
      lappend env_save $var $::env($var)
    } else {
      lappend env_save $var {}
    }
    set ::env($var) 1
  }

  set rc [catch {
    exec $shell -eval $evalScript
  } output]

  foreach {var val} $env_save {
    if {$val eq ""} then {
      catch {unset ::env($var)}
    } else {
      set ::env($var) $val
    }
  }

  if {$rc != 0} then {
    return [list error $output]
  }

  foreach line [split $output "\n"] {
    if {[string match "RESULT: *" $line]} then {
      set data [string range $line 8 end]
      set parts [split $data " "]
      if {[llength $parts] == 3} then {
        set match [lindex $parts 2]
        if {$match eq "1"} then {
          return [list ok ""]
        } else {
          return [list mismatch \
              "data=[lindex $parts 0] sig=[lindex $parts 1]"]
        }
      }
    }
    if {[string match "ERROR: *" $line]} then {
      return [list error [string range $line 7 end]]
    }
  }
  return [list error "unexpected output: $output"]
}

###############################################################################
#
# Main
#
###############################################################################

set fix 0
set force 0
set quiet 0
set projectDir [file normalize [file join [file dirname [info script]] ..]]
set shell ""
set skipPatterns [list]
set signTool reconfig.bat

for {set i 0} {$i < $argc} {incr i} {
  set arg [lindex $argv $i]
  switch -- $arg {
    -fix    { set fix 1 }
    -force  { set force  1 }
    -quiet  { set quiet 1 }
    -dir    { incr i; set projectDir [file normalize [lindex $argv $i]] }
    -shell  { incr i; set shell [lindex $argv $i] }
    -skip   { incr i; lappend skipPatterns [lindex $argv $i] }
    default {
      puts stderr "Unknown option: $arg"
      puts stderr "Usage: tclsh tools/verifier.tcl ?-quiet? ?-dir PATH?\
                   ?-shell PATH? ?-skip PATTERN? -fix"
          exit 1
    }
  }
}

if {$shell eq ""} then {
  if {$tcl_platform(platform) eq "windows"} then {
    set shell [file join $projectDir bin th8sh.exe]
  } else {
    set shell [file join $projectDir bin th8sh]
  }
}

if {![file executable $shell]} then {
  puts stderr "Error: TH8 shell not found or not executable: $shell"
  exit 1
}

lappend skipPatterns "*/fuzz/*" "*/bin/*"

set allFiles [list]

eval lappend allFiles [findScriptFiles $projectDir $skipPatterns]
eval lappend allFiles [findSignatureFiles $projectDir $skipPatterns]

set allFiles [lsort -unique $allFiles]

set nTotal    0
set nSigned   0
set nUnsigned 0
set nOk       0
set nFail     0
set nError    0

foreach f $allFiles {
  incr nTotal
  set sigFile "${f}.b64sig"

  if {![file exists $sigFile]} then {
    incr nUnsigned
    if {!$quiet} then {
      set rel $f
      if {[string range $f 0 \
          [expr {[string length $projectDir]}]] eq \
          "${projectDir}/"} then {
        set rel [string range $f \
            [expr {[string length $projectDir] + 1}] end]
      }
      puts "UNSIGNED  $rel"
    }
    continue
  }

  incr nSigned

  set result [verifyFile $shell $projectDir $f]
  set status [lindex $result 0]
  set detail [lindex $result 1]

  set rel $f
  if {[string range $f 0 \
      [expr {[string length $projectDir]}]] eq \
      "${projectDir}/"} then {
    set rel [string range $f \
        [expr {[string length $projectDir] + 1}] end]
  }

  switch $status {
    ok {
      incr nOk
      if {!$quiet} then {
        puts "OK        $rel"
      }
    }
    unsigned -
    mismatch -
    error {; # BUGBUG: Unsigned does not work due to [continue] above.
      if {$status eq "error"} then {
        incr nError
        puts "ERROR     $rel  ($detail)"
      } elseif {$status eq "mismatch"} then {
        incr nFail
        puts "FAIL      $rel  ($detail)"
      }

      if {![string match tampered_* [file tail $rel]]} then {
        if {$tcl_platform(platform) eq "windows"} then {
          if {$fix && ($force || $detail eq "no matching key")} then {
            exec -- $signTool [file nativename [file join $projectDir $rel]]
            puts "RE-SIGNED $rel"
          }
        }
      }
    }
  }
}

puts ""
puts "============================================"
puts "Signature Verification Summary"
puts "============================================"
puts "  Total files:    $nTotal"
puts "  Signed:         $nSigned"
puts "  Unsigned:       $nUnsigned"
puts "  Verified OK:    $nOk"
puts "  FAILED:         $nFail"
puts "  Errors:         $nError"
puts "============================================"

if {$nFail > 0 || $nError > 0} then {
  puts "OVERALL: FAILURE"
  exit 1
} else {
  puts "OVERALL: SUCCESS"
  exit 0
}
