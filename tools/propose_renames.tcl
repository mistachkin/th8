#!/usr/bin/env tclsh
###############################################################################
#
# propose_renames.tcl --
#
#   For each orphan R-marker referenced in tests/, dump:
#     - file:line  testName  ORPHAN-MARKER
#     - the test's R-marker description (first 200 chars)
#     - top-3 candidate current markers from the standard / API spec /
#       extensions, ranked by token-overlap with the test description
#
#   Operator-driven: NEVER auto-applies a rename.  This is a work-list
#   for the human (or LLM) doing the manual sweep.
#
# Usage:
#     tclsh tools/propose_renames.tcl
#
###############################################################################

set DOC_FILES [list \
    docs/pending/tcl_language_standard_v1.md \
    docs/pending/th8_public_c_api_specification.md \
    docs/pending/th8_language_extensions.md]

set TESTS_DIR tests

# ----------------------------------------------------------------
# Tokenize a string into a sorted unique list of words for set-overlap.
# ----------------------------------------------------------------

set STOPWORDS [list a an and the of to is be by are as in on or for if it \
    via that this when which with shall must may not no all any its on \
    from at into so command be been being do does should]

proc tokenize {s} {
    global STOPWORDS
    set s [string tolower $s]
    regsub -all {[^a-z0-9_-]+} $s " " s
    set out [list]
    foreach t [split $s] {
        if {$t eq ""} then { continue }
        if {[string length $t] < 2} then { continue }
        if {[lsearch -exact $STOPWORDS $t] >= 0} then { continue }
        lappend out $t
    }
    return [lsort -unique $out]
}

# ----------------------------------------------------------------
# Read a doc file and return list of {marker text} for every R-marker
# block.  A block is `R-NNNNN-NNNNN` followed by one or more `:   ...`
# continuation lines.
# ----------------------------------------------------------------

proc read_doc_markers {path} {
    set fp [open $path r]
    set raw [read $fp]
    close $fp
    set lines [split $raw \n]
    set blocks [list]
    set n [llength $lines]
    for {set i 0} {$i < $n} {incr i} {
        set ln [lindex $lines $i]
        if {![regexp {^(R-[0-9]{5}-[0-9]{5})\s*$} $ln _ rid]} continue
        set parts [list]
        set j [expr {$i + 1}]
        while {$j < $n} {
            set nx [lindex $lines $j]
            if {[regexp {^:\s+(.*)$} $nx _ rest]} then {
                lappend parts $rest
                incr j
                continue
            }
            break
        }
        if {[llength $parts] > 0} then {
            lappend blocks [list $rid [join $parts " "]]
        }
    }
    return $blocks
}

# ----------------------------------------------------------------
# Main.
# ----------------------------------------------------------------

# Build a marker -> tokenset and marker -> text map across all docs.
array set MARKER_TEXT {}
array set MARKER_TOK  {}

# Buckets populated during the per-orphan loop.
set SUGGEST_MAP    [list]
set NEEDS_REVIEW   [list]
set NO_CANDIDATE   [list]

foreach doc $DOC_FILES {
    foreach b [read_doc_markers $doc] {
        lassign $b rid text
        set MARKER_TEXT($rid) $text
        set MARKER_TOK($rid)  [tokenize $text]
    }
}

# Run mkreq.tcl --check-tests and capture orphan rows.  --check-tests
# exits non-zero when orphans exist; catch and use the captured stdout
# either way.
set sh ""
catch {exec tclsh tools/mkreq.tcl --check-tests {*}$DOC_FILES $TESTS_DIR} sh
set orphan_rows [list]
foreach line [split $sh \n] {
    # Lines look like: "  /path/tests/foo.tcl:NN  test-name  R-NNNNN-NNNNN"
    if {[regexp {tests/([a-zA-Z0-9_./-]+):(\d+)\s+(\S+)\s+(R-\d{5}-\d{5})} \
            $line _ relPath lineNo testName marker]} then {
        lappend orphan_rows [list "tests/$relPath" $lineNo $testName $marker]
    }
}

# For each orphan, fetch the test's description, score against every
# current marker, print top 3 candidates.
# Regex constants (hoisted out of foreach bodies because Tcl's first-
# pass brace-counter does not honour backslash-escaped braces inside
# braced blocks).
set END_OF_DESC_RE "^\[\[:space:\]\]*\}\[\[:space:\]\]*-"
set MARKER_PREFIX_RE {^\s*R-\d{5}-\d{5}:\s*}

foreach row $orphan_rows {
    lassign $row file lineNo testName marker
    set fp [open $file r]
    set raw [read $fp]
    close $fp
    set tlines [split $raw \n]
    set start [expr {$lineNo - 1}]
    set descParts [list]
    set k $start
    set max [llength $tlines]
    while {$k < $max && $k < $start + 8} {
        set tl [lindex $tlines $k]
        if {$k > $start && [regexp $END_OF_DESC_RE $tl]} then { break }
        if {$k == $start} then {
            regsub $MARKER_PREFIX_RE $tl "" tl
        }
        lappend descParts [string trim $tl]
        incr k
    }
    set desc [string trim [join $descParts " "]]
    set descTok [tokenize $desc]

    # Score every current marker.
    set scored [list]
    foreach rid [array names MARKER_TOK] {
        set common 0
        foreach t $descTok {
            if {[lsearch -sorted $MARKER_TOK($rid) $t] >= 0} then { incr common }
        }
        if {$common >= 2} then {
            lappend scored [list $common $rid]
        }
    }
    set scored [lsort -decreasing -integer -index 0 $scored]

    puts "----"
    puts "$file:$lineNo  $testName  $marker"
    puts "  DESC: [string range $desc 0 200]"
    if {[llength $scored] == 0} then {
        puts "  (no candidates with token-overlap >= 2)"
        lappend NO_CANDIDATE [list $marker $testName $desc]
    } else {
        set top [lindex $scored 0]
        lassign $top topScore topRid
        set runner ""
        if {[llength $scored] > 1} then {
            lassign [lindex $scored 1] runnerScore _
            set runner $runnerScore
        }
        foreach c [lrange $scored 0 2] {
            lassign $c sc rid
            set t $MARKER_TEXT($rid)
            puts "  #$sc $rid: [string range $t 0 200]"
        }
        # Confidence rule: top score >= 4 AND top score > runner-up by >= 2,
        # OR runner-up doesn't exist.  Tightest cases get auto-suggested;
        # everything else goes in the manual-review pile.
        if {$topScore >= 4 && ($runner eq "" || $topScore - $runner >= 2)} then {
            lappend SUGGEST_MAP [list $marker $topRid $testName]
        } else {
            lappend NEEDS_REVIEW [list $marker $topRid $testName $topScore $runner]
        }
    }
}

# Emit summary at end.
puts ""
puts "=== Summary ==="
puts "Auto-suggested mappings (high confidence): [llength $SUGGEST_MAP]"
puts "Needs review (ambiguous):                  [llength $NEEDS_REVIEW]"
puts "No candidates (manual investigation):      [llength $NO_CANDIDATE]"

# Write the auto-suggested mapping to a file.
set fp [open /tmp/orphan_map.txt w]
puts $fp "# Auto-generated by propose_renames.tcl"
puts $fp "# Format: OLD-marker NEW-marker  # test-name"
puts $fp ""
# Deduplicate (same orphan may be referenced by many tests).
array set seen {}
foreach entry $SUGGEST_MAP {
    lassign $entry old new test
    if {[info exists seen($old)]} then {
        if {$seen($old) ne $new} then {
            puts $fp "# CONFLICT: $old previously mapped to $seen($old), now $new ($test)"
        }
        continue
    }
    set seen($old) $new
    puts $fp "$old $new  # $test"
}
close $fp
puts ""
puts "Auto-suggested mapping written to /tmp/orphan_map.txt"

# Also write the "needs review" candidates (top pick + alt) to a
# review file so the operator can look at each, accept or override.
set fp [open /tmp/orphan_review.txt w]
puts $fp "# Operator review needed for these orphans."
puts $fp "# Format: OLD top-score top-marker top-text-snippet"
puts $fp "#         (next 2 alternatives indented)"
puts $fp ""
foreach entry $NEEDS_REVIEW {
    lassign $entry old top test topScore runnerScore
    puts $fp "$old (test=$test, top=$topScore, runner-up=$runnerScore)"
    puts $fp "  TOP:    $top  $MARKER_TEXT($top)"
    puts $fp ""
}
close $fp
puts "Review file written to /tmp/orphan_review.txt ([llength $NEEDS_REVIEW] entries)"

# And the no-candidate list.
set fp [open /tmp/orphan_no_candidate.txt w]
puts $fp "# Orphans with no doc R-marker that token-overlaps with the test"
puts $fp "# description.  Genuine manual investigation needed."
puts $fp ""
foreach entry $NO_CANDIDATE {
    lassign $entry old test desc
    puts $fp "$old  test=$test"
    puts $fp "  DESC: $desc"
    puts $fp ""
}
close $fp
puts "No-candidate file written to /tmp/orphan_no_candidate.txt ([llength $NO_CANDIDATE] entries)"
