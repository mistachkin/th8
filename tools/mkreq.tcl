#!/usr/bin/env tclsh
###############################################################################
#
# mkreq.tcl --
#
#     Requirements mark tool for the Tcl Language Standard.
#
#     Reads a Markdown specification file, extracts normative requirement
#     text from lines beginning with the marker "^  " (caret, two spaces),
#     computes an MD5-based requirement identifier following the SQLite
#     algorithm, and rewrites the line with the identifier.
#
#     The identifier format is R-NNNNN-NNNNN where each group is the
#     decimal value (0..65535, zero-padded to 5 digits) of a 16-bit
#     slice of the MD5 hash of the normalized requirement text.
#
#     Long requirement text MAY span multiple consecutive marker lines,
#     all of which are concatenated (with whitespace normalised to single
#     spaces) before hashing.  Both forms below produce the same R-marker:
#
#         ^   Long requirement text that fits in one line.
#
#         ^   Long requirement text that
#         ^   fits in one line.
#
#     The same is true after processing: the `:   ` continuation form
#     accepted by --verify and --refresh allows the rendered text to wrap
#     across as many lines as needed without changing the computed
#     R-marker.
#
# Modes:
#
#     tclsh mkreq.tcl <file.md>
#         Process new requirements (^  lines) and write R-markers.
#         Consecutive ^  lines are coalesced into a single requirement.
#         The file is updated in-place.
#
#     tclsh mkreq.tcl --verify <file.md>
#         Check all existing R-markers against their requirement text.
#         Multi-line `:   ` continuations are concatenated before
#         hashing.  Reports mismatches to stdout.  Does NOT modify the
#         file.
#
#     tclsh mkreq.tcl --refresh <file.md>
#         Recompute all existing R-markers from the current requirement
#         text.  Multi-line `:   ` continuations are concatenated before
#         hashing.  Fixes stale markers caused by post-generation edits.
#         The file is updated in-place.
#
#     tclsh mkreq.tcl --check-tests <file.md> <path> ?<path>...?
#         Cross-check R-markers used in test scripts against the
#         markers defined in the standard.  Each <path> is either a
#         single .tcl file or a directory (recursively scanned for
#         *.tcl).  Test descriptions may span multiple lines (line
#         breaks at or before column 79); all lines between the
#         opening `{` after the test name and the matching closing
#         `}` are concatenated, whitespace-normalised, and the leading
#         R-marker prefix is extracted.  Reports orphan R-markers
#         (used in tests but not defined in the standard) and (with
#         --report-uncovered) standard R-markers that no test
#         references.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################


###############################################################################
#
# normalize --
#
#     Reduce TEXT to a canonical form for hashing: trim leading and
#     trailing whitespace, collapse internal whitespace to single
#     spaces, and strip Markdown inline formatting characters that do
#     not change the requirement's meaning.  This canonicalisation
#     guarantees the same R-marker is produced regardless of how the
#     source text wraps across lines.
#
###############################################################################

proc normalize {text} {
  set text [string trim $text]
  regsub -all {\s+} $text { } text
  #
  # Strip Markdown inline formatting for hash stability.
  #
  regsub -all {`} $text {} text
  regsub -all {\*\*} $text {} text
  regsub -all {\*} $text {} text
  return $text
}


###############################################################################
#
# compute_md5 --
#
#     Run the platform's MD5 utility on the given text.  macOS exposes
#     `md5 -q -s STRING`; Linux uses `printf | md5sum`.  Returns the
#     32-character hex digest.
#
###############################################################################

proc compute_md5 {text} {
  if {![catch {exec md5 -q -s $text} hash]} then {
    return [string trim $hash]
  }
  if {![catch {exec printf "%s" $text | md5sum} result]} then {
    return [lindex [split $result] 0]
  }
  error "No MD5 tool found (tried md5, md5sum)"
}


###############################################################################
#
# compute_r --
#
#     Compute the R-marker for TEXT.  The marker is derived from the
#     first 8 hex digits of the MD5 of the normalized text, split into
#     two 16-bit big-endian groups.
#
###############################################################################

proc compute_r {text} {
  set norm [normalize $text]
  set hash [compute_md5 $norm]

  #
  # First 4 hex digits -> group 1, next 4 -> group 2.
  #
  scan [string range $hash 0 3] %x a
  scan [string range $hash 4 7] %x b
  return [format "R-%05d-%05d" $a $b]
}


###############################################################################
#
# read_lines --
#
#     Read FNAME and return the list of lines (no trailing newline on
#     each).  Empty trailing element from split is dropped.
#
###############################################################################

proc read_lines {fname} {
  set fd [open $fname r]
  set raw [read $fd]
  close $fd
  set lines [split $raw \n]
  if {[lindex $lines end] eq ""} then {
    set lines [lrange $lines 0 end-1]
  }
  return $lines
}


###############################################################################
#
# write_lines --
#
#     Write LINES to FNAME with `\n` separators and a trailing newline.
#
###############################################################################

proc write_lines {fname lines} {
  set fd [open $fname w]
  puts -nonewline $fd [join $lines \n]
  puts -nonewline $fd \n
  close $fd
}


###############################################################################
#
# mode_process --
#
#     Default mode: scan for runs of `^  ` lines, coalesce each run into
#     a single requirement (concatenated with single-space joins),
#     compute the R-marker, and rewrite the run as the R-marker line
#     followed by one `:   ` continuation line per original input line.
#
#     This preserves the author's chosen line-break positions while
#     ensuring the R-marker hashes the full concatenated text.
#
###############################################################################

proc mode_process {fname} {
  set lines [read_lines $fname]
  set out {}
  set reqs {}
  set lineNo 0

  set n [llength $lines]
  set i 0
  while {$i < $n} {
    set line [lindex $lines $i]
    incr lineNo

    if {[regexp {^(\s*)\^\s\s(.+)$} $line -> indent firstText]} then {
      #
      # Start of a new requirement.  Walk forward, collecting
      # every consecutive `^   ` line (with the same indent) as
      # a continuation of this same requirement.
      #
      set runStart $lineNo
      set parts [list $firstText]
      set rawLines [list $firstText]
      set j [expr {$i + 1}]
      while {$j < $n} {
        set nextLine [lindex $lines $j]
        if {[regexp [format {^%s\^\s\s(.+)$} $indent] $nextLine \
            -> nextText]} then {
          lappend parts $nextText
          lappend rawLines $nextText
          incr j
          continue
        }
        break
      }
      set joined [join $parts " "]
      set rid [compute_r $joined]
      #
      # Emit: R-marker line, then one `:   ` continuation per
      # original `^   ` line, preserving the author's line
      # breaks.
      #
      lappend out "${indent}${rid}"
      foreach part $rawLines {
        lappend out "${indent}:   ${part}"
      }
      lappend reqs [list $rid $runStart $joined]
      set i $j
      continue
    }
    lappend out $line
    incr i
  }

  write_lines $fname $out

  puts "=== Requirements Cross-Reference ==="
  puts [format "%-16s  %5s  %s" "Requirement" "Line" \
      "Text (first 60 chars)"]
  puts [string repeat - 80]
  foreach r $reqs {
    set rid [lindex $r 0]
    set ln  [lindex $r 1]
    set txt [lindex $r 2]
    if {[string length $txt] > 60} then {
      set txt "[string range $txt 0 56]..."
    }
    puts [format "%-16s  %5d  %s" $rid $ln $txt]
  }
  puts ""
  puts "Total requirements: [llength $reqs]"
}


###############################################################################
#
# scan_standard --
#
#     Walk the lines of FNAME and yield a list of {Rmarker text
#     startLine} triples for every R-NNNNN-NNNNN block, concatenating
#     consecutive `:   ` continuation lines into the text.  Used by
#     --verify, --refresh, and --check-tests.
#
###############################################################################

proc scan_standard {lines} {
  set blocks {}
  set n [llength $lines]
  for {set i 0} {$i < $n} {incr i} {
    set line [lindex $lines $i]
    if {![regexp {^(R-\d{5}-\d{5})$} $line -> rid]} continue

    #
    # Read every consecutive `:   ` continuation.  An empty line
    # or any non-`:   ` line ends the block.
    #
    set parts {}
    set j [expr {$i + 1}]
    while {$j < $n} {
      set next [lindex $lines $j]
      if {[regexp {^:\s+(.*)$} $next -> partText]} then {
        lappend parts $partText
        incr j
        continue
      }
      break
    }
    if {[llength $parts] == 0} then { continue }
    set joined [join $parts " "]
    lappend blocks [list $rid $joined [expr {$i + 1}] $j]
  }
  return $blocks
}


###############################################################################
#
# mode_verify --
#
#     Read-only mode: check every R-marker block in the file against
#     the hash of its (possibly multi-line) text.  Reports mismatches
#     and a summary; exits non-zero on any mismatch.
#
###############################################################################

proc mode_verify {fname} {
  set lines [read_lines $fname]
  set blocks [scan_standard $lines]

  set nOk 0
  set nBad 0

  foreach b $blocks {
    lassign $b rid text startLine _endLine
    set computed [compute_r $text]
    if {$computed ne $rid} then {
      puts "MISMATCH line $startLine:"
      puts "  found:    $rid"
      puts "  expected: $computed"
      set txt $text
      if {[string length $txt] > 70} then {
        set txt "[string range $txt 0 66]..."
      }
      puts "  text:     $txt"
      puts ""
      incr nBad
    } else {
      incr nOk
    }
  }

  puts "Verified: $nOk OK, $nBad mismatches"
  if {$nBad > 0} then {
    puts ""
    puts "Run with --refresh to fix stale markers."
    exit 1
  }
}


###############################################################################
#
# mode_refresh --
#
#     Recompute every R-marker from its (possibly multi-line) text and
#     rewrite the file in place.  Also processes any newly-introduced
#     `^   ` lines (delegated to mode_process semantics).
#
###############################################################################

proc mode_refresh {fname} {
  set lines [read_lines $fname]
  set out {}
  set nFixed 0
  set renames {}
  set n [llength $lines]
  set i 0
  set lineNo 0

  while {$i < $n} {
    set line [lindex $lines $i]
    incr lineNo
    set blockLine $lineNo

    if {[regexp {^(R-\d{5}-\d{5})$} $line -> rid]} then {
      #
      # Read the `:   ` continuation block.
      #
      set parts {}
      set rawLines {}
      set j [expr {$i + 1}]
      while {$j < $n} {
        set next [lindex $lines $j]
        if {[regexp {^:\s+(.*)$} $next -> partText]} then {
          lappend parts $partText
          lappend rawLines $next
          incr j
          continue
        }
        break
      }
      if {[llength $parts] > 0} then {
        set joined [join $parts " "]
        set computed [compute_r $joined]
        lappend out $computed
        foreach raw $rawLines { lappend out $raw }
        if {$computed ne $rid} then {
          incr nFixed
          #
          # Record the rename so any stale test references can be
          # found by grep.  Reported on stdout AFTER the file is
          # rewritten so the summary is the last thing printed.
          #
          lappend renames [list $blockLine $rid $computed $joined]
        }
        # Account for the extra lines consumed (rawLines).
        set lineNo [expr {$lineNo + [llength $rawLines]}]
        set i $j
        continue
      }
    }
    lappend out $line
    incr i
  }

  #
  # Second pass: any newly-introduced `^   ` runs become R-markers.
  #
  set out2 {}
  set nNew 0
  set m [llength $out]
  set i 0
  while {$i < $m} {
    set line [lindex $out $i]
    if {[regexp {^(\s*)\^\s\s(.+)$} $line -> indent firstText]} then {
      set parts [list $firstText]
      set rawLines [list $firstText]
      set j [expr {$i + 1}]
      while {$j < $m} {
        set nextLine [lindex $out $j]
        if {[regexp [format {^%s\^\s\s(.+)$} $indent] $nextLine \
            -> nextText]} then {
          lappend parts $nextText
          lappend rawLines $nextText
          incr j
          continue
        }
        break
      }
      set joined [join $parts " "]
      set rid [compute_r $joined]
      lappend out2 "${indent}${rid}"
      foreach part $rawLines {
        lappend out2 "${indent}:   ${part}"
      }
      incr nNew
      set i $j
      continue
    }
    lappend out2 $line
    incr i
  }

  write_lines $fname $out2

  #
  # Print the rename audit BEFORE the summary so a long list does
  # not push the summary off-screen, and so the operator can pipe
  # to less / a file if they want.
  #
  if {[llength $renames] > 0} then {
    puts ""
    puts "=== R-marker renames ==="
    puts "  These markers were recomputed because their text changed."
    puts "  Grep tests for the OLD marker and update each reference"
    puts "  to the NEW marker.  Failure to do so produces orphan"
    puts "  markers that mkreq.tcl --check-tests will report."
    puts ""
    foreach r $renames {
      lassign $r ln old new text
      set t $text
      if {[string length $t] > 60} then {
        set t "[string range $t 0 56]..."
      }
      puts [format "  line %5d: %s -> %s  %s" $ln $old $new $t]
    }
    puts ""
  }
  puts "Refreshed: $nFixed markers updated, $nNew new markers added"
}


###############################################################################
#
# scan_test_file --
#
#     Walk a single test script and yield a list of {testName Rmarker
#     descText startLine} tuples for every `runTest {test ... { ... }
#     ...}` invocation that contains an R-marker in its description.
#
#     Multi-line descriptions are supported: every line between the
#     `{` immediately following `test NAME ` and the closing `}` that
#     precedes a `-OPTION` clause is treated as part of the description.
#     Whitespace is normalised before extracting the R-marker prefix.
#
###############################################################################

proc scan_test_file {fname} {
  set lines [read_lines $fname]
  set tests {}
  set n [llength $lines]
  set i 0

  while {$i < $n} {
    set line [lindex $lines $i]
    #
    # Match the start of a test invocation.  Two cases:
    #   1. runTest \{test NAME \{DESC\} (entire description
    #      contained on this line).
    #   2. runTest \{test NAME \{       (description starts on
    #      the next line, ends at a later \}).
    #
    # The regex is built with format() to keep literal braces
    # away from Tcl's brace-balance parser.
    #
    set re [format {^\s*runTest\s+%ctest\s+(\S+)\s+%c(.*)$} 123 123]
    if {![regexp $re $line -> testName rest]} then {
      incr i
      continue
    }
    set startLine [expr {$i + 1}]
    #
    # Collect description lines until we find the closing brace.
    # The closing brace is a single \} at the start of a line
    # followed by whitespace and a -OPTION keyword (or the
    # description was on the same line and balanced).
    #
    set descParts {}
    if {[string trim $rest] ne ""} then {
      #
      # Same-line description: rest starts with the description
      # text and may end with a close-brace followed by an
      # optional -OPTION.  Walk braces to find the matching
      # close (descriptions rarely contain literal braces).
      #
      set depth 1
      set k 0
      set len [string length $rest]
      set descEnd -1
      while {$k < $len} {
        set ch [string index $rest $k]
        if {$ch eq "\\"} then {
          incr k 2
          continue
        }
        if {$ch eq [format %c 123]} then { incr depth }
        if {$ch eq [format %c 125]} then {
          incr depth -1
          if {$depth == 0} then { set descEnd $k; break }
        }
        incr k
      }
      if {$descEnd >= 0} then {
        lappend descParts [string range $rest 0 [expr {$descEnd - 1}]]
        set i [expr {$i + 1}]
        analyze_test_desc $testName $descParts $startLine tests
        continue
      }

      #
      # Description didn't close on this line -- it spans onto
      # following lines.  Keep what we have and walk forward.
      #
      lappend descParts $rest
    }

    #
    # Multi-line description: read forward until we find a
    # close-brace at the start of a line followed by -OPTION,
    # or a close-brace on its own.
    #
    set j [expr {$i + 1}]
    set closeRe [format {^\s*%c\s*(-\S+|$)} 125]
    while {$j < $n} {
      set jline [lindex $lines $j]
      if {[regexp $closeRe $jline]} then {
        break
      }
      lappend descParts $jline
      incr j
    }
    analyze_test_desc $testName $descParts $startLine tests
    set i [expr {$j + 1}]
  }
  return $tests
}


###############################################################################
#
# analyze_test_desc --
#
#     Inspect DESCPARTS (the raw description-block lines) for an R-
#     marker and record a test-tuple if one is found.  Multi-line
#     descriptions are flattened with single-space joins before the
#     R-marker is extracted.
#
###############################################################################

proc analyze_test_desc {testName descParts startLine resultsVar} {
  upvar 1 $resultsVar tests
  set joined [join $descParts " "]
  regsub -all -- {\s+} $joined { } joined
  set joined [string trim $joined]
  #
  # Tests may cite one or more R-markers as a space-separated list
  # before the colon, e.g.
  #
  #   R-45917-21828 R-65214-62481: binary format d Inf round-trips
  #
  # Capture every R-marker in that prefix; emit one tuple per
  # marker so each is credited against the standard's covered set.
  #
  if {[regexp -- {^((?:R-\d{5}-\d{5}\s*)+):\s*(.*)$} $joined -> prefix descText]} then {
    set descText [string trim $descText]
    foreach rid [regexp -all -inline -- {R-\d{5}-\d{5}} $prefix] {
      lappend tests [list $testName $rid $descText $startLine]
    }
  }
}


###############################################################################
#
# discover_test_files --
#
#     Expand each PATH (file or directory) into a flat list of *.tcl
#     files.  Directories are walked recursively; files are taken
#     verbatim.
#
###############################################################################

proc discover_test_files {paths} {
  set found {}
  foreach p $paths {
    if {[file isfile $p]} then {
      lappend found [file normalize $p]
      continue
    }
    if {[file isdirectory $p]} then {
      set walk [list $p]
      while {[llength $walk] > 0} {
        set d [lindex $walk 0]
        set walk [lrange $walk 1 end]
        foreach f [glob -nocomplain -directory $d *.tcl] {
          lappend found [file normalize $f]
        }
        foreach sub [glob -nocomplain -directory $d -types d *] {
          lappend walk $sub
        }
      }
      continue
    }
    puts stderr "warning: $p is not a file or directory; skipping"
  }
  return [lsort -unique $found]
}


###############################################################################
#
# orphan_suggest --
#
#     Given an orphan R-marker RID (e.g. "R-45561-34695") that is NOT
#     present in the standard, find the most plausible candidate(s)
#     among the known standard R-markers in CANDIDATES (a list of R-id
#     strings).  Returns up to MAXSUGG suggestions, lowest Hamming
#     distance over the 10 digits first, ties broken by lexical order.
#
#     A Hamming-distance heuristic catches the typical orphan source:
#     a single transposed or mistyped digit in a hand-edited test
#     citation.  Distances above 4 are filtered out as too noisy to be
#     useful suggestions.
#
###############################################################################

proc orphan_suggest {rid candidates {maxSugg 3}} {
  if {![regexp -- {^R-(\d{5})-(\d{5})$} $rid -> a b]} then { return {} }
  set ridDigits "$a$b"
  set scored {}
  foreach c $candidates {
    if {![regexp -- {^R-(\d{5})-(\d{5})$} $c -> ca cb]} then { continue }
    set cDigits "$ca$cb"
    set d 0
    for {set i 0} {$i < 10} {incr i} {
      if {[string index $ridDigits $i] ne [string index $cDigits $i]} then {
        incr d
      }
    }
    if {$d <= 4} then { lappend scored [list $d $c] }
  }
  set scored [lsort -index 0 -integer $scored]
  set out {}
  foreach s $scored {
    lappend out [lindex $s 1]
    if {[llength $out] >= $maxSugg} then { break }
  }
  return $out
}


###############################################################################
#
# mode_check_tests --
#
#     Cross-check R-markers used in test scripts against the markers
#     defined in the standard.  Each test path may be a file or a
#     directory; directories are scanned recursively for *.tcl.
#
#     Reports two failure classes:
#       1. Orphan R-markers -- referenced by a test but not defined in
#          the standard.  This is a hard error (exits non-zero).
#          For each orphan, mkreq suggests up to three near-miss
#          candidates from the standard, ranked by Hamming distance
#          over the 10 digits.  A single-digit miss is almost always
#          a typo in the test's citation.
#       2. Uncovered R-markers -- defined in the standard but never
#          referenced by any test.  Reported only when --report-uncovered
#          is set on the command line.
#
###############################################################################

proc mode_check_tests {fnames testPaths reportUncovered} {
  array set standardSet {}
  foreach fname $fnames {
    set lines [read_lines $fname]
    set blocks [scan_standard $lines]
    foreach b $blocks {
      set rid [lindex $b 0]
      set standardSet($rid) [lindex $b 1]
    }
  }
  # Re-create $blocks as the union (used below for "uncovered" report).
  set blocks {}
  foreach {rid txt} [array get standardSet] {
    lappend blocks [list $rid $txt]
  }

  set files [discover_test_files $testPaths]
  if {[llength $files] == 0} then {
    puts stderr "no test files found under: $testPaths"
    exit 2
  }

  set nTests 0
  set nUntagged 0
  set nMatched 0
  set orphans {}
  array set covered {}

  foreach f $files {
    set tests [scan_test_file $f]
    foreach t $tests {
      lassign $t testName rid descText startLine
      incr nTests
      if {[info exists standardSet($rid)]} then {
        incr nMatched
        set covered($rid) 1
        continue
      }
      lappend orphans [list $f $startLine $testName $rid]
    }
  }

  set nOrphans [llength $orphans]
  set nCovered [array size covered]
  set nStandard [array size standardSet]
  set nUncovered [expr {$nStandard - $nCovered}]

  puts "=== R-marker Test Cross-Reference ==="
  puts "  Standard R-markers:  $nStandard"
  puts "  Test invocations:    $nTests"
  puts "  Test R-markers ok:   $nMatched"
  puts "  Orphan R-markers:    $nOrphans"
  puts "  Covered standard:    $nCovered"
  puts "  Uncovered standard:  $nUncovered"
  puts ""

  if {$nOrphans > 0} then {
    set standardRids [array names standardSet]
    puts "ORPHAN R-markers (referenced in tests, missing from standard):"
    foreach o $orphans {
      lassign $o file lineNo testName rid
      puts "  $file:$lineNo  $testName  $rid"
      set hints [orphan_suggest $rid $standardRids 3]
      if {[llength $hints] > 0} then {
        puts "        did you mean: [join $hints {, }]"
      }
    }
    puts ""
  }

  if {$reportUncovered && $nUncovered > 0} then {
    puts "UNCOVERED R-markers (defined in standard, no test reference):"
    foreach b $blocks {
      set rid [lindex $b 0]
      if {[info exists covered($rid)]} then { continue }
      set txt [lindex $b 1]
      if {[string length $txt] > 60} then {
        set txt "[string range $txt 0 56]..."
      }
      puts "  $rid  $txt"
    }
    puts ""
  }

  if {$nOrphans > 0} then { exit 1 }
}


###############################################################################
#
# main / entry point --
#
###############################################################################

proc main {argv} {
  set mode "process"
  set files {}
  set reportUncovered 0

  foreach arg $argv {
    switch -exact -- $arg {
      "--verify"           { set mode "verify" }
      "--refresh"          { set mode "refresh" }
      "--check-tests"      { set mode "check_tests" }
      "--report-uncovered" { set reportUncovered 1 }
      default              { lappend files $arg }
    }
  }

  if {$mode eq "check_tests"} then {
    if {[llength $files] < 2} then {
      puts stderr "Usage: tclsh mkreq.tcl --check-tests <file.md> ?<file.md>...?\
                    <test-path> ?<test-path>...?"
          exit 2
    }
    # Split files into .md (standard sources) and the rest (test paths).
    set fnames {}
    set testPaths {}
    foreach f $files {
      if {[string match -nocase *.md $f]} then {
        lappend fnames $f
      } else {
        lappend testPaths $f
      }
    }
    if {[llength $fnames] == 0 || [llength $testPaths] == 0} then {
      puts stderr "Usage: tclsh mkreq.tcl --check-tests <file.md> ?<file.md>...?\
                    <test-path> ?<test-path>...?"
      exit 2
    }
    mode_check_tests $fnames $testPaths $reportUncovered
    return
  }

  if {[llength $files] != 1} then {
    puts stderr "Usage: tclsh mkreq.tcl ?--verify|--refresh? <file.md>"
    puts stderr "       tclsh mkreq.tcl --check-tests <file.md>\
                <test-path> ?<test-path>...?"
        exit 1
  }
  set fname [lindex $files 0]

  switch -exact -- $mode {
    "process" { mode_process $fname }
    "verify"  { mode_verify $fname }
    "refresh" { mode_refresh $fname }
  }
}


main $argv
