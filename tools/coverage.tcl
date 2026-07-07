#!/usr/bin/env tclsh
###############################################################################
#
# coverage.tcl --
#
#     Analyze gcov output files (.gcov) and report uncovered branches
#     and lines.  Produces a summary per file and a detailed listing
#     of uncovered branch sites with surrounding source context.
#
# Usage:
#     tclsh tools/coverage.tcl *.gcov
#     tclsh tools/coverage.tcl -detail *.gcov
#     tclsh tools/coverage.tcl -summary *.gcov
#
# Options:
#     -detail    Show source context for each uncovered branch (default).
#     -summary   Show per-file summary only.
#     -context N Number of context lines around uncovered branches (default 2).
#     -min-pct N Only report files with branch coverage below N% (default 100).
#     -skip PAT  Skip files matching glob pattern (e.g., "regex_*").
#
# Input:
#     gcov files produced by: gcov -b -o bin/ src/th8_core.c
#
# gcov branch annotation format:
#     branch  N taken K       -- branch N was taken K times
#     branch  N never executed -- branch N was never executed
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

proc parseGcov {fileName} {
  set fd [open $fileName r]
  set lines [split [read $fd] \n]
  close $fd

  set sourceFile ""
  set totalLines 0
  set coveredLines 0
  set uncoveredLines 0
  set totalBranches 0
  set takenBranches 0
  set neverBranches 0
  set branchSites {}  ;# list of {lineNo srcText branchInfo}

  set prevSrcLine ""
  set prevLineNo 0

  foreach line $lines {
    #
    # Source line format:
    #   COUNT:  LINENO:SOURCE
    # where COUNT is a number, "#####" (not executed), or "-" (non-code)
    #

    if {[regexp {^\s*([#0-9-]+):\s*(\d+):(.*)} $line -> count lineNo src]} then {
      set prevSrcLine $src
      set prevLineNo $lineNo

      if {$lineNo == 0} then {
        #
        # Line 0 contains metadata like "Source:filename"
        #

        if {[regexp {^Source:(.+)} $src -> sf]} then {
          set sourceFile $sf
        }
        continue
      }

      if {$count ne "-"} then {
        incr totalLines
        if {$count eq "#####" || $count eq "=====" } then {
          incr uncoveredLines
        } else {
          incr coveredLines
        }
      }
    }

    #
    # Branch annotation (follows the source line it belongs to).
    #

    if {[regexp {^branch\s+(\d+)\s+(.*)} $line -> branchId info]} then {
      incr totalBranches

      if {[string match "never executed" $info]} then {
        incr neverBranches
        lappend branchSites [list $prevLineNo $prevSrcLine \
            "branch $branchId never executed"]
      } elseif {[regexp {taken (\d+)} $info -> count]} then {
        if {$count == 0} then {
          incr neverBranches
          lappend branchSites [list $prevLineNo $prevSrcLine \
              "branch $branchId taken 0"]
        } else {
          incr takenBranches
        }
      } else {
        incr takenBranches
      }
    }
  }

  return [list \
      sourceFile $sourceFile \
      totalLines $totalLines \
      coveredLines $coveredLines \
      uncoveredLines $uncoveredLines \
      totalBranches $totalBranches \
      takenBranches $takenBranches \
      neverBranches $neverBranches \
      branchSites $branchSites \
      allLines $lines]
}


proc printSummary {results} {
  puts "=== TH8 Code Coverage Summary ==="
  puts ""
  puts [format "%-35s %6s %6s %5s   %6s %6s %5s" \
      "File" "Lines" "Cover" "L%" "Branch" "Taken" "B%"]
  puts [string repeat - 85]

  set grandTotalLines 0
  set grandCoveredLines 0
  set grandTotalBranches 0
  set grandTakenBranches 0

  foreach r $results {
    array set a $r
    set linePct 0
    set branchPct 0

    if {$a(totalLines) > 0} then {
      set linePct [expr {100.0 * $a(coveredLines) / $a(totalLines)}]
    }
    if {$a(totalBranches) > 0} then {
      set branchPct [expr {100.0 * $a(takenBranches) / $a(totalBranches)}]
    }

    set shortName [file tail $a(sourceFile)]
    puts [format "%-35s %6d %6d %4.1f%%   %6d %6d %4.1f%%" \
        $shortName \
        $a(totalLines) $a(coveredLines) $linePct \
        $a(totalBranches) $a(takenBranches) $branchPct]

    incr grandTotalLines $a(totalLines)
    incr grandCoveredLines $a(coveredLines)
    incr grandTotalBranches $a(totalBranches)
    incr grandTakenBranches $a(takenBranches)

    array unset a
  }

  puts [string repeat - 85]

  set grandLinePct 0
  set grandBranchPct 0
  if {$grandTotalLines > 0} then {
    set grandLinePct [expr {100.0 * $grandCoveredLines / $grandTotalLines}]
  }
  if {$grandTotalBranches > 0} then {
    set grandBranchPct [expr {100.0 * $grandTakenBranches / $grandTotalBranches}]
  }

  puts [format "%-35s %6d %6d %4.1f%%   %6d %6d %4.1f%%" \
      "TOTAL" \
      $grandTotalLines $grandCoveredLines $grandLinePct \
      $grandTotalBranches $grandTakenBranches $grandBranchPct]
  puts ""
}


proc printBranchDetail {results contextLines minPct skipPat} {
  foreach r $results {
    array set a $r

    set branchPct 100.0
    if {$a(totalBranches) > 0} then {
      set branchPct [expr {100.0 * $a(takenBranches) / $a(totalBranches)}]
    }

    set shortName [file tail $a(sourceFile)]

    if {$skipPat ne "" && [string match $skipPat $shortName]} then {
      array unset a
      continue
    }

    if {$branchPct >= $minPct} then {
      array unset a
      continue
    }

    if {[llength $a(branchSites)] == 0} then {
      array unset a
      continue
    }

    puts "=== $shortName (branch coverage: [format %.1f $branchPct]%) ==="
    puts ""

    #
    # Build a line-number-indexed source map from the gcov file.
    #

    array set srcMap {}
    foreach gline $a(allLines) {
      if {[regexp {^\s*([#0-9-]+):\s*(\d+):(.*)} $gline -> cnt lno src]} then {
        if {$lno > 0} then {
          set srcMap($lno) $src
        }
      }
    }

    #
    # Group branch sites by line number (multiple branches on one line).
    #

    array set sitesByLine {}
    foreach site $a(branchSites) {
      set lno [lindex $site 0]
      set info [lindex $site 2]
      lappend sitesByLine($lno) $info
    }

    foreach lno [lsort -integer [array names sitesByLine]] {
      puts "  Line $lno:"

      #
      # Print context lines.
      #

      for {set i [expr {$lno - $contextLines}]} \
          {$i <= $lno + $contextLines} {incr i} {
        if {$i < 1} then { continue }
        if {[info exists srcMap($i)]} then {
          set marker "  "
          if {$i == $lno} then { set marker ">>" }
          puts [format "    %s %5d: %s" $marker $i $srcMap($i)]
        }
      }

      foreach info $sitesByLine($lno) {
        puts "    ** $info"
      }
      puts ""
    }

    array unset a
    array unset srcMap
    array unset sitesByLine
  }
}


proc main {argv} {
  set mode "detail"
  set contextLines 2
  set minPct 100.0
  set skipPat ""
  set files {}

  set i 0
  while {$i < [llength $argv]} {
    set arg [lindex $argv $i]
    switch -exact -- $arg {
      -detail  { set mode "detail" }
      -summary { set mode "summary" }
      -context {
        incr i
        set contextLines [lindex $argv $i]
      }
      -min-pct {
        incr i
        set minPct [lindex $argv $i]
      }
      -skip {
        incr i
        set skipPat [lindex $argv $i]
      }
      default {
        lappend files $arg
      }
    }
    incr i
  }

  if {[llength $files] == 0} then {
    puts stderr "Usage: tclsh coverage.tcl \[options\] file.gcov ..."
    puts stderr ""
    puts stderr "Options:"
    puts stderr "  -detail        Show uncovered branches with context (default)"
    puts stderr "  -summary       Per-file summary only"
    puts stderr "  -context N     Context lines (default 2)"
    puts stderr "  -min-pct N     Only report files below N% branch coverage"
    puts stderr "  -skip PAT      Skip files matching glob pattern"
    exit 1
  }

  set results {}
  foreach f $files {
    if {![file exists $f]} then {
      puts stderr "Warning: $f not found, skipping."
      continue
    }
    lappend results [parseGcov $f]
  }

  if {[llength $results] == 0} then {
    puts stderr "No .gcov files found."
    exit 1
  }

  printSummary $results

  if {$mode eq "detail"} then {
    printBranchDetail $results $contextLines $minPct $skipPat
  }
}

main $argv
