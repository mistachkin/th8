#!/usr/bin/env tclsh
#
# check_amal.tcl -- verify that the amalgamation source list in tools/mkamal.tcl
# stays in sync with the TH8 sources actually present in the tree.
#
# Rationale:
#   The amalgamation (bin/th8.c, built by tools/mkamal.tcl) is produced by
#   concatenating an EXPLICIT, hand-maintained list of source files.  When a new
#   core/plugin/platform source is added to the tree (and to CORE_OBJ in the
#   makefiles) but NOT to mkamal.tcl's list, the amalgamation silently omits it
#   -- so the normal object build stays green while `make amalgamation-shell`
#   fails to link (missing symbols) or, worse, links a subtly incomplete library.
#   This is exactly how th8_memtrack.c and th8_unwind.c broke the amalgamation
#   on 2026-07-27.
#
#   The reference is the FILESYSTEM: every TH8-authored .c under src/ (core,
#   plugins, and platform layers) MUST be referenced by mkamal.tcl, except for a
#   small, documented set of units that are deliberately compiled OUTSIDE the
#   library amalgamation (the shell drivers and the stubs-consumer library).
#   The check is static (no compiler, no build) so it runs cheaply in `audit`.
#
# Usage:   tclsh tools/check_amal.tcl
# Exit:    0 = list in sync;  1 = one or more sources missing/stale (printed).
#
# See docs/internal/incomplete.md (Bug 70 / amalgamation drift) and FINDINGS.md.

set ROOT [file normalize [file join [file dirname [info script]] ..]]
cd $ROOT

# ---------------------------------------------------------------------------
# Sources that are TH8-authored but deliberately NOT part of the library
# amalgamation.  Keep this list tiny and each entry justified: anything not
# listed here is REQUIRED to appear in mkamal.tcl, so an unjustified addition
# will (correctly) fail the build until it is either amalgamated or excused.
# ---------------------------------------------------------------------------
set EXCLUDE {
    src/th8sh.c        "shell driver -- compiled separately even in amal mode"
    src/th8_shell.c    "interactive shell REPL -- part of th8sh, not the library"
    src/th8StubLib.c   "stubs-consumer library linked INTO extensions, not the core"
    src/th8_fossil.c   "Fossil-integration build unit, not the standalone amalgamation"
}

proc read_file {path} {
    if {[catch {open $path r} fp]} { return "" }
    set txt [read $fp]
    close $fp
    return $txt
}

# ---------------------------------------------------------------------------
# Enumerate every TH8-authored .c source in the tree (core + plugins + the
# harpy/crypto/regexp plugin subdirs).  Paths are returned repo-relative and
# normalised with forward slashes so they compare against mkamal's spelling.
# ---------------------------------------------------------------------------
proc tree_sources {} {
    set pats {
        src/th8*.c
        src/plugins/th8*.c
        src/plugins/*/th8*.c
    }
    set out {}
    foreach pat $pats {
        foreach f [glob -nocomplain -- $pat] {
            lappend out [string map {\\ /} $f]
        }
    }
    return [lsort -unique $out]
}

# ---------------------------------------------------------------------------
# Every "src/....c" path referenced by mkamal.tcl (any list entry, whether
# static or conditionally lappend'ed -- a whole-file scan sees them all).
# ---------------------------------------------------------------------------
proc mkamal_sources {} {
    set txt [read_file tools/mkamal.tcl]
    set out {}
    foreach {_ p} [regexp -all -inline {"(src/[A-Za-z0-9_/]+\.c)"} $txt] {
        lappend out $p
    }
    return [lsort -unique $out]
}

# ---------------------------------------------------------------------------
set excluded [dict create]
foreach {path why} $EXCLUDE { dict set excluded $path $why }

set tree   [tree_sources]
set listed [mkamal_sources]

set missing {} ;# on disk, required, but absent from mkamal.tcl
foreach f $tree {
    if {[dict exists $excluded $f]} { continue }
    if {[lsearch -exact $listed $f] < 0} { lappend missing $f }
}

set stale {} ;# referenced by mkamal.tcl but no longer on disk
foreach f $listed {
    if {![file exists $f]} { lappend stale $f }
}

# An exclusion for a file that no longer exists is dead weight -- surface it so
# the list cannot rot silently.
set deadexcl {}
foreach {path why} $EXCLUDE {
    if {![file exists $path]} { lappend deadexcl $path }
}

set nbad [expr {[llength $missing] + [llength $stale] + [llength $deadexcl]}]
if {$nbad} {
    if {[llength $missing]} {
        puts "check_amal: sources MISSING from tools/mkamal.tcl:"
        foreach f $missing { puts "    $f" }
        puts "  -> add each to the sourceFiles list in tools/mkamal.tcl, OR, if it"
        puts "     is intentionally excluded, add it (with a reason) to EXCLUDE in"
        puts "     tools/check_amal.tcl."
    }
    if {[llength $stale]} {
        puts "check_amal: mkamal.tcl references sources that no longer exist:"
        foreach f $stale { puts "    $f" }
    }
    if {[llength $deadexcl]} {
        puts "check_amal: EXCLUDE lists sources that no longer exist:"
        foreach f $deadexcl { puts "    $f" }
    }
    puts "\ncheck_amal: FAIL -- amalgamation source list out of sync ($nbad problem(s))."
    exit 1
}
puts "check_amal: OK -- every TH8 source is amalgamated or explicitly excused."
exit 0
