#!/usr/bin/env tclsh
#
# scan_unset_placement.tcl --
#
#     Scan .tcl test files and flag cleanup-class commands
#     (unset, array unset, catch ns/file delete, file delete
#     -force, catch close, catch rename) that appear inside a
#     runTest -body or -setup block.  Per the
#     feedback_unset_in_cleanup memory: these belong in
#     -cleanup, unless the cleanup command IS the test subject.
#
#     Subject-tests (where the cleanup-class command is the line
#     under test) are whitelisted via `tests/.unset_whitelist`.
#     Each whitelist entry is a `[string match]` glob over test
#     names, plus a free-form reason.  Subject-tests are NOT
#     listed in the scanner output unless `--show-whitelisted`
#     is passed.
#
#     Usage:
#       tclsh tools/scan_unset_placement.tcl [--show-whitelisted]
#       tclsh tools/scan_unset_placement.tcl --list-whitelist
#
#     With no flag: report only NON-whitelisted hits (real
#     placement violations).  Exit 1 if any are found.
#
#     --show-whitelisted: also list the whitelisted hits, marked
#     with `[whitelisted: <reason>]`.  Exit code still reflects
#     non-whitelisted findings only.
#
#     --list-whitelist: dump the parsed whitelist and exit.
#

# ----------------------------------------------------------------------
# Argument parsing.
# ----------------------------------------------------------------------

set showWhitelisted 0
set listWhitelist 0
set roots [list]
foreach arg $argv {
    switch -- $arg {
        "--show-whitelisted" { set showWhitelisted 1 }
        "--list-whitelist"   { set listWhitelist 1 }
        default              { lappend roots $arg }
    }
}
if {[llength $roots] == 0} then {
    lappend roots "tests"
}

# ----------------------------------------------------------------------
# Whitelist loader.
#
# Returns a list of {glob reason} pairs.  Comments / blanks ignored.
# ----------------------------------------------------------------------

proc load_whitelist { path } {
    set entries [list]
    if {![file exists $path]} then { return $entries }
    set fh [open $path r]
    while {[gets $fh line] >= 0} {
        set t [string trim $line]
        if {$t eq ""} then { continue }
        if {[string index $t 0] eq "#"} then { continue }
        # Split into <glob> [whitespace] <reason-rest-of-line>.
        if {![regexp -- {^(\S+)\s+(.*)$} $t -> g r]} then { continue }
        lappend entries [list $g [string trim $r]]
    }
    close $fh
    return $entries
}

proc whitelist_reason { entries testname } {
    foreach e $entries {
        set g [lindex $e 0]
        set r [lindex $e 1]
        if {[string match $g $testname]} then { return $r }
    }
    return ""
}

set whitelist [load_whitelist tests/.unset_whitelist]

if {$listWhitelist} then {
    puts "Loaded [llength $whitelist] whitelist entries from tests/.unset_whitelist:"
    foreach e $whitelist {
        puts "  [lindex $e 0]\t-- [lindex $e 1]"
    }
    exit 0
}

# ----------------------------------------------------------------------
# Scanner.
# ----------------------------------------------------------------------

set OB [format %c 123]
set CB [format %c 125]

# Cleanup-class prefixes, applied to a trimmed line.  Each entry
# is {tag literal-prefix}.  Built without raw braces in the
# script source to keep the Tcl parser happy.
set patterns [list]
lappend patterns [list "unset" "unset "]
lappend patterns [list "unset" "unset"]
lappend patterns [list "array-unset" "array unset "]
lappend patterns [list "ns-delete" "catch ${OB}namespace delete"]
lappend patterns [list "file-delete" "catch ${OB}file delete"]
lappend patterns [list "file-delete-force" "file delete -force"]
lappend patterns [list "close-catch" "catch ${OB}close"]
lappend patterns [list "rename-restore" "catch ${OB}rename"]

# Section-opener literals.
set bodyOpen "-body ${OB}"
set setupOpen "-setup ${OB}"
set cleanupOpen "-cleanup ${OB}"
set testOpen "runTest ${OB}"

# Collect the .tcl files under each requested root.  The scanner
# walks each root non-recursively except for `tests` itself, where
# we include `tests/coverage/*` as well (the historical default).
set files [list]
foreach root $roots {
    if {[file isdirectory $root]} then {
        foreach pat [list "$root/*.tcl" "$root/coverage/*.tcl"] {
            foreach f [glob -nocomplain $pat] {
                lappend files $f
            }
        }
    } else {
        lappend files $root
    }
}

set totalReal 0
set totalWhitelisted 0
foreach f [lsort -unique $files] {
    if {[catch {open $f r} fh]} then { continue }
    set in_body 0
    set in_setup 0
    set test_name ""
    set hits [list]
    set lineno 0
    while {[gets $fh line] >= 0} {
        incr lineno
        set t [string trim $line]
        if {[string first $testOpen $line] == 0} then {
            if {[regexp -- "test (\\S+) " $line -> tn]} then { set test_name $tn }
            set in_body 0
            set in_setup 0
            continue
        }
        if {[string first $bodyOpen $line] >= 0} then {
            set in_body 1
            set in_setup 0
            continue
        }
        if {[string first $setupOpen $line] >= 0} then {
            set in_setup 1
            set in_body 0
            continue
        }
        if {[string first $cleanupOpen $line] >= 0} then {
            set in_body 0
            set in_setup 0
            continue
        }
        if {[string first "-result " $line] >= 0
                || [string first "-match " $line] >= 0
                || [string first "-returnCodes " $line] >= 0} then {
            set in_body 0
            set in_setup 0
            continue
        }
        if {!$in_body && !$in_setup} then { continue }
        set tag ""
        foreach p $patterns {
            set pTag [lindex $p 0]
            set pPre [lindex $p 1]
            if {$pTag eq "unset" && $pPre eq "unset" && $t ne "unset"} then {
                continue
            }
            if {[string first $pPre $t] == 0} then {
                set tag $pTag
                break
            }
        }
        if {$tag eq ""} then { continue }
        set section "body"
        if {$in_setup} then { set section "setup" }
        set reason [whitelist_reason $whitelist $test_name]
        lappend hits [list $lineno $test_name $section $tag $t $reason]
    }
    close $fh
    set realHits [list]
    set whHits [list]
    foreach h $hits {
        if {[lindex $h 5] eq ""} then {
            lappend realHits $h
        } else {
            lappend whHits $h
        }
    }
    incr totalReal [llength $realHits]
    incr totalWhitelisted [llength $whHits]
    if {[llength $realHits] > 0 || ($showWhitelisted && [llength $whHits] > 0)} then {
        puts "=== $f"
        foreach h $realHits {
            puts "  L[lindex $h 0] \[[lindex $h 2] in [lindex $h 1]\] ([lindex $h 3]) [lindex $h 4]"
        }
        if {$showWhitelisted} then {
            foreach h $whHits {
                puts "  L[lindex $h 0] \[[lindex $h 2] in [lindex $h 1]\] ([lindex $h 3]) [lindex $h 4]   \[whitelisted: [lindex $h 5]\]"
            }
        }
    }
}

# ----------------------------------------------------------------------
# Summary + exit status.
# ----------------------------------------------------------------------

puts ""
if {$totalReal == 0} then {
    puts "OK: no misplaced cleanup-class commands found (${totalWhitelisted} whitelisted hits suppressed)."
    exit 0
} else {
    puts "Found $totalReal real placement violations (plus ${totalWhitelisted} whitelisted subject-test hits)."
    exit 1
}
