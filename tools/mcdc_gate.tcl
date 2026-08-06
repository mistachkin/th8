###############################################################################
#
# mcdc_gate.tcl --
#
# RTM 1.0 MC/DC "reachable-denominator" gate.  See
# docs/internal/mcdc_reachable_criterion.md for the design.
#
# Computes, over every measured MC/DC decision:
#
#     gate_pct = covered / (total - waived)
#
# where `waived` is the missed condition-pairs in decisions formally
# classified as intrinsically-undrivable in the waiver file, and
# requires gate_pct >= the threshold with ZERO stale waivers and no
# un-waived debt below the threshold.
#
# A waiver's ANCHOR is (enclosing-function-name + decision source
# text) -- edit-stable, unlike a line number.  The tool derives the
# same anchor from llvm-cov's decision span so waivers match across
# edits; a waiver whose anchor no longer maps to a <100% decision is
# STALE and fails the gate (so a test that closes a decision, or code
# that removes it, forces the waiver's removal).
#
# Usage:
#     tclsh tools/mcdc_gate.tcl \
#         --profdata bin/mcdc.profdata \
#         --bin bin/th8sh --object bin/libth8.dylib \
#         --waivers tools/data/mcdc_waivers.tsv \
#         [--threshold 95.0] [--srcdir src] [--verbose]
#
# Exit: 0 if the gate passes; 1 otherwise.
#
# DATA SOURCES.  The authoritative denominator (total/covered/missed) comes
# from `llvm-cov report -show-mcdc-summary` -- merged across instances and
# EXCLUDING constant-folded (NEVER/ALWAYS-wrapped) conditions, identical to
# `make mcdc-report`.  The uncovered-decision WORKLIST comes from `llvm-cov
# show`, which IS folding-aware per-decision: a folded condition prints
# "C<N>-Pair: constant folded" (not "not covered") and the decision's own %
# excludes it.  We count only real "not covered" pairs and merge a decision's
# multiple coverage instances (union of covered/folded condition indices), so
# the worklist's missed sums to the authoritative missed.  (`llvm-cov export`
# is NOT usable here -- its mcdc_records give no folded/covered distinction,
# only raw bools, so folded and truly-uncovered are indistinguishable.)
#
# KNOWN CAVEAT: the anchor is (function + decision-text), so two IDENTICAL
# decision expressions in the SAME function share one anchor and merge.  When
# such twins have differing coverage, the uncovered one is masked, leaving the
# worklist missed a few short of authoritative (a small negative drift, shown
# as a NOTE).  It does NOT affect PASS/FAIL (that uses the authoritative
# denominator); it only means those few conditions cannot be individually
# waived -- they stay as implicit debt.  Disambiguate by wrapping one twin or
# refactoring if a masked condition must be classified.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set ::opt(profdata) bin/mcdc.profdata
set ::opt(bin)      bin/th8sh
set ::opt(object)   bin/libth8.dylib
set ::opt(waivers)  tools/data/mcdc_waivers.tsv
set ::opt(threshold) 95.0
set ::opt(srcdir)   src
set ::opt(verbose)  0
set ::opt(dumpdebt) 0
set ::opt(llvmcov)  "xcrun llvm-cov"

set ::VALID_CLASSES {
    correlated-conditions defensive-omit-guard overflow-guard
    startup-cached-callback environmental network win32-only
    synchronization-invasive reserved-value-reject
    external-oom-unreachable
}

proc usage {msg} {
    puts stderr "mcdc_gate: $msg"
    puts stderr "usage: tclsh tools/mcdc_gate.tcl --profdata F --bin F\
        --object F --waivers F ?--threshold N? ?--srcdir D? ?--verbose?"
    exit 2
}

for {set i 0} {$i < [llength $argv]} {incr i} {
    set a [lindex $argv $i]
    switch -exact -- $a {
        --profdata - --bin - --object - --waivers - --threshold - --srcdir {
            incr i
            set ::opt([string range $a 2 end]) [lindex $argv $i]
        }
        --verbose { set ::opt(verbose) 1 }
        --dump-debt { set ::opt(dumpdebt) 1 }
        default { usage "unknown argument: $a" }
    }
}

# The Makefile's $(S) is "src/" (trailing slash); strip it so the
# relative-path computation (strlen(srcdir)+1) is correct.
set ::opt(srcdir) [string trimright $::opt(srcdir) /]

# --- source-file list: mirror the Makefile's MCDC_SOURCES ------------------

proc mcdc_sources {srcdir} {
    set pats [list \
        [file join $srcdir th8_*.c] \
        [file join $srcdir th8StubInit.c] \
        [file join $srcdir plugins th8_*.c] \
        [file join $srcdir plugins crypto th8_*.c] \
        [file join $srcdir plugins harpy th8_*.c]]
    set exclude [list th8sh.c th8_shell.c th8_win32.c th8_mimalloc.c]
    set out [list]
    foreach p $pats {
        foreach f [lsort [glob -nocomplain $p]] {
            if {[lsearch -exact $exclude [file tail $f]] >= 0} continue
            lappend out $f
        }
    }
    return $out
}

# --- enclosing-function detection ------------------------------------------
#
# The C style in this tree puts a function definition's name flush at
# column 0 immediately followed by "(" (the return type is on the line
# above).  The enclosing function of a decision at line L is the last
# such definition at or before L.

proc build_func_index {srclines} {
    set idx [list]
    set n [llength $srclines]
    for {set i 0} {$i < $n} {incr i} {
        set line [lindex $srclines $i]
        if {[regexp {^([A-Za-z_][A-Za-z0-9_]*)\(} $line -> name]} {
            lappend idx [expr {$i + 1}] $name
        }
    }
    return $idx
}

proc func_at {funcidx lineno} {
    set cur "<file-scope>"
    foreach {fl name} $funcidx {
        if {$fl <= $lineno} { set cur $name } else { break }
    }
    return $cur
}

# --- decision source-text extraction + normalization -----------------------

proc decision_text {srclines sl sc el ec} {
    # 1-based line numbers; columns 1-based inclusive start, exclusive-ish
    # end as llvm-cov reports the span.  Extract and collapse whitespace.
    set out ""
    for {set l $sl} {$l <= $el} {incr l} {
        set line [lindex $srclines [expr {$l - 1}]]
        set a 0
        set b [string length $line]
        if {$l == $sl} { set a [expr {$sc - 1}] }
        if {$l == $el} { set b [expr {$ec - 1}] }
        if {$a < 0} { set a 0 }
        append out " " [string range $line $a [expr {$b - 1}]]
    }
    regsub -all {\s+} $out " " out
    return [string trim $out]
}

# --- authoritative lib-wide totals -----------------------------------------
#
# The reachable-denominator MUST use the SAME accounting as `make
# mcdc-report` -- `llvm-cov report -show-mcdc-summary`, which MERGES
# multiple coverage instances of a decision and EXCLUDES constant-folded
# (NEVER/ALWAYS-wrapped) conditions.  Summing `llvm-cov show`'s per-
# instance regions instead over-counts both (folded + duplicated).  So
# take (total, missed) from the report summary's TOTAL row, and use
# `show` only to enumerate + anchor the uncovered decisions (merged by
# anchor to drop the per-instance false-negatives).

proc authoritative_totals {srcs} {
    # `llvm-cov report -show-mcdc-summary` merges instances AND excludes
    # constant-folded conditions -- its TOTAL row is the same accounting as
    # `make mcdc-report`.  Trailing MC/DC columns: total, uncovered, pct%.
    set rc [catch {
        exec {*}$::opt(llvmcov) report -instr-profile=$::opt(profdata) \
            -show-mcdc-summary $::opt(bin) -object $::opt(object) {*}$srcs 2>/dev/null
    } raw]
    foreach ln [split $raw "\n"] {
        if {[regexp {^TOTAL} $ln]} {
            set f [regexp -all -inline {[^ \t]+} $ln]
            set n [llength $f]
            set total [lindex $f [expr {$n - 3}]]
            set missed [lindex $f [expr {$n - 2}]]
            if {[string is integer -strict $total] && [string is integer -strict $missed]} {
                return [list $total $missed]
            }
        }
    }
    puts stderr "mcdc_gate: could not parse llvm-cov report TOTAL row"
    exit 2
}

# Parse `llvm-cov show -show-mcdc` for ONE source file.  llvm-cov is
# folding-aware here: a folded condition prints "C<N>-Pair: constant
# folded" (NOT "not covered") and the decision's own % excludes it.  We
# return every decision INSTANCE (a decision may recur across coverage
# instances) as {func text nconds covered folded}, where covered/folded
# are lists of condition indices; main merges instances by anchor.

proc parse_file {srcpath} {
    set fh [open $srcpath r]
    set srclines [split [read $fh] "\n"]
    close $fh
    set funcidx [build_func_index $srclines]

    set rc [catch {
        exec {*}$::opt(llvmcov) show -instr-profile=$::opt(profdata) \
            -show-mcdc $::opt(bin) -object $::opt(object) $srcpath 2>/dev/null
    } raw]

    set decisions [list]
    set sl 0; set sc 0; set el 0; set ec 0
    set nconds 0; set covered [list]; set folded [list]; set inregion 0
    foreach ln [split $raw "\n"] {
        if {[regexp {MC/DC Decision Region \((\d+):(\d+)\) to \((\d+):(\d+)\)} \
                $ln -> sl sc el ec]} {
            set inregion 1; set nconds 0; set covered [list]; set folded [list]
            continue
        }
        if {!$inregion} continue
        if {[regexp {Number of Conditions:\s*(\d+)} $ln -> n]} { set nconds $n }
        if {[regexp {C(\d+)-Pair: constant folded} $ln -> ci]} {
            lappend folded $ci
        } elseif {[regexp {C(\d+)-Pair: covered} $ln -> ci]} {
            lappend covered $ci
        }
        if {[regexp {Coverage for Decision:} $ln]} {
            set text [decision_text $srclines $sl $sc $el $ec]
            set func [func_at $funcidx $sl]
            lappend decisions [dict create func $func text $text \
                nconds $nconds covered $covered folded $folded]
            set inregion 0
        }
    }
    return $decisions
}

# --- load waivers -----------------------------------------------------------
#
# TSV: file <TAB> function <TAB> decision-text <TAB> class <TAB> rationale
# Blank lines and lines beginning with '#' are ignored.

proc load_waivers {path} {
    set waivers [dict create]
    if {![file exists $path]} { return $waivers }
    set fh [open $path r]
    set lno 0
    foreach line [split [read $fh] "\n"] {
        incr lno
        if {[string trim $line] eq "" || [string index [string trimleft $line] 0] eq "#"} continue
        set cols [split $line "\t"]
        if {[llength $cols] < 5} {
            puts stderr "mcdc_gate: waiver line $lno: expected 5 tab-separated fields"
            exit 2
        }
        lassign $cols file func text class rationale
        if {[lsearch -exact $::VALID_CLASSES $class] < 0} {
            puts stderr "mcdc_gate: waiver line $lno: unknown class '$class'"
            exit 2
        }
        set key [waiver_key $file $func $text]
        dict set waivers $key [dict create class $class rationale $rationale used 0 line $lno]
    }
    close $fh
    return $waivers
}

proc waiver_key {file func text} {
    return "$file $func [string trim $text]"
}

# --- main -------------------------------------------------------------------

set waivers [load_waivers $::opt(waivers)]
set srcs [mcdc_sources $::opt(srcdir)]

# authoritative denominator (merged + folding-excluded), == make mcdc-report
lassign [authoritative_totals $srcs] total covered_missed

# collect decision instances per file, then merge by anchor: a condition is
# covered if covered in ANY instance, folded if folded in any instance;
# merged-missed = conditions that are neither.
set merged [dict create]   ;# anchor -> {file func text nconds covered folded}
foreach src $srcs {
    set relfile [string range $src [expr {[string length $::opt(srcdir)] + 1}] end]
    foreach d [parse_file $src] {
        set key [waiver_key $relfile [dict get $d func] [dict get $d text]]
        if {[dict exists $merged $key]} {
            foreach fld {covered folded} {
                set cur [dict get $merged $key $fld]
                foreach ci [dict get $d $fld] {
                    if {[lsearch -exact $cur $ci] < 0} { lappend cur $ci }
                }
                dict set merged $key $fld $cur
            }
        } else {
            dict set merged $key [dict create file $relfile \
                func [dict get $d func] text [dict get $d text] \
                nconds [dict get $d nconds] covered [dict get $d covered] \
                folded [dict get $d folded]]
        }
    }
}

set waived 0
set debt [list]         ;# uncovered decisions with no waiver
set stale [list]        ;# waivers matching no uncovered decision
set worklist_missed 0   ;# sum of merged missed (cross-check vs authoritative)

dict for {key m} $merged {
    set naccounted [expr {[llength [dict get $m covered]] + [llength [dict get $m folded]]}]
    set nmissed [expr {[dict get $m nconds] - $naccounted}]
    if {$nmissed <= 0} continue    ;# fully covered/folded once merged
    incr worklist_missed $nmissed
    if {[dict exists $waivers $key]} {
        dict set waivers $key used 1
        incr waived $nmissed
    } else {
        set pct [format "%.0f" [expr {100.0 * [llength [dict get $m covered]] \
            / ([dict get $m nconds] - [llength [dict get $m folded]] + 1e-9)}]]
        lappend debt [dict create file [dict get $m file] func [dict get $m func] \
            text [dict get $m text] nconds [dict get $m nconds] \
            nmissed $nmissed pct $pct]
    }
}

# stale waivers: declared but never matched an uncovered decision
dict for {k v} $waivers {
    if {![dict get $v used]} { lappend stale [list $k $v] }
}

if {$::opt(dumpdebt)} {
    foreach d [lsort -command {apply {{a b} {string compare "[dict get $a file] [dict get $a func]" "[dict get $b file] [dict get $b func]"}}} $debt] {
        puts [format "%s\t%s\t%s\t%s%%" [dict get $d file] [dict get $d func] \
            [dict get $d text] [dict get $d pct]]
    }
    exit 0
}

set covered [expr {$total - $covered_missed}]
set reachable [expr {$total - $waived}]
set gate_pct [expr {$reachable > 0 ? 100.0 * $covered / $reachable : 100.0}]
set flat_pct [expr {$total > 0 ? 100.0 * $covered / $total : 100.0}]

puts "== MC/DC reachable-denominator gate =="
puts [format "  total conditions      : %d  (authoritative, mcdc-report accounting)" $total]
puts [format "  covered               : %d" $covered]
puts [format "  missed (all)          : %d" $covered_missed]
puts [format "  waived (intrinsic)    : %d" $waived]
puts [format "  reachable denominator : %d" $reachable]
puts [format "  flat MC/DC            : %.2f%%" $flat_pct]
puts [format "  reachable MC/DC       : %.2f%%   (threshold %.1f%%)" $gate_pct $::opt(threshold)]
puts [format "  un-waived debt        : %d decisions / %d condition-pairs" \
    [llength $debt] [expr {$covered_missed - $waived}]]
puts [format "  stale waivers         : %d" [llength $stale]]
if {$worklist_missed != $covered_missed} {
    set drift [expr {$worklist_missed - $covered_missed}]
    puts [format "  NOTE: worklist missed %d vs authoritative %d (%+d) -- %s" \
        $worklist_missed $covered_missed $drift \
        [expr {$drift < 0 ? "identical-text decision twins share one anchor and\
            merge; masked twins stay as implicit debt (PASS/FAIL unaffected)." \
            : "unexpected positive drift -- investigate."}]]
}

if {$::opt(verbose) || [llength $debt] > 0} {
    puts "\n-- un-waived debt (drive or classify) --"
    foreach d [lsort -command {apply {{a b} {string compare [dict get $a file] [dict get $b file]}}} $debt] {
        puts [format "  %-28s %-26s %s%%  %s" [dict get $d file] \
            [dict get $d func] [dict get $d pct] [dict get $d text]]
    }
}
if {[llength $stale] > 0} {
    puts "\n-- STALE waivers (remove: decision is now covered or gone) --"
    foreach s $stale {
        lassign $s k v
        set parts [split $k " "]
        puts [format "  %-28s %-26s %s" [lindex $parts 0] [lindex $parts 1] [lindex $parts 2]]
    }
}

set fail 0
if {$gate_pct + 1e-9 < $::opt(threshold)} { set fail 1 }
if {[llength $stale] > 0} { set fail 1 }

puts ""
if {$fail} {
    puts "mcdc_gate: FAIL"
    exit 1
} else {
    puts "mcdc_gate: PASS"
    exit 0
}
