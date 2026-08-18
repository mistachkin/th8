#!/usr/bin/env tclsh
#
# discover_loops.tcl --
#
#     TH8K-009 reachable-loop DISCOVERY.  Complements check_polls.tcl (which
#     enforces that the reviewed set in poll_required.tsv still polls): this
#     tool SWEEPS the attacker-reachable command surface and reports every
#     function that contains a for/while loop but is NOT accounted for -- i.e.
#     neither in poll_required.tsv (polled) nor in the BOUNDED/SAFE allowlist
#     (tools/data/loop_bounded.tsv, a function<TAB>reason list of loops whose
#     iteration count is bounded by a small constant or by already-counted
#     work).  A non-empty "UNACCOUNTED" list is a prompt to review each loop and
#     either add a poll (-> poll_required.tsv) or record its bound (->
#     loop_bounded.tsv).  Advisory by default; pass -strict to exit non-zero on
#     any unaccounted function (for CI once the inventory is complete).
#
#     "Command surface" = the *_command / *GetCommands functions and the
#     enumerated parse/iterate helpers in the files listed below -- the
#     functions an untrusted script can drive with attacker-sized input.
#
# Usage:
#     tclsh tools/discover_loops.tcl [-strict]
#
###############################################################################

set root [file dirname [file dirname [file normalize [info script]]]]
set strict [expr {[lindex $argv 0] eq "-strict"}]

# Files whose command functions form the attacker-reachable surface.
set files {
    src/plugins/th8_lists.c src/plugins/th8_strings.c
    src/plugins/th8_binary.c src/plugins/th8_formatting.c
    src/plugins/th8_looping.c src/plugins/th8_control.c
    src/plugins/th8_variables.c src/plugins/th8_introspection.c
    src/plugins/th8_extensibility.c src/plugins/th8_filesystems.c
    src/plugins/th8_io.c src/plugins/th8_management.c
    src/th8_hash.c
}

# Load the two disposition sets keyed on function name.
proc load_names {path} {
    set names {}
    if {![file exists $path]} {return $names}
    set fd [open $path r]
    foreach row [split [read $fd] \n] {
        set row [string trim $row]
        if {$row eq "" || [string index $row 0] eq "#"} {continue}
        set f [split $row \t]
        if {[llength $f] >= 2} {lappend names [string trim [lindex $f 1]]}
    }
    close $fd
    return $names
}
set polled [load_names [file join $root tools data poll_required.tsv]]
set bounded [load_names [file join $root tools data loop_bounded.tsv]]

# Reuse the robust body extractor + literal stripping from check_polls.tcl.
source [file join $root tools check_polls_common.tcl]

set unaccounted {}
set total 0
foreach rel $files {
    set path [file join $root $rel]
    if {![file exists $path]} {continue}
    set fd [open $path r]
    set text [read $fd]
    close $fd
    # Enumerate column-0 function definitions in this file.
    foreach line [split $text \n] {
        if {![regexp {^([A-Za-z_][A-Za-z0-9_]*)\(} $line -> name]} {continue}
        # Only the command surface: *_command, *GetCommands, and the
        # named parse/iterate helpers.
        if {![string match *_command $name]
            && ![string match *GetCommands $name]
            && ![regexp {^(th8Next|th8Parse|Th8_HashIterate|th8.*Sort|(th8|Th8_).*Split)} \
                    $name]} {
            continue
        }
        set body [extract_body $text $name]
        if {$body eq ""} {continue}
        if {![regexp {(^|[^A-Za-z0-9_])(for|while)[[:space:]]*\(} $body]} {
            continue ;# no loop
        }
        incr total
        if {[lsearch -exact $polled $name] >= 0} {continue}
        if {[lsearch -exact $bounded $name] >= 0} {continue}
        lappend unaccounted "$rel\t$name"
    }
}

puts "discover_loops: $total command-surface functions with loops;\
    [llength $unaccounted] UNACCOUNTED (not polled, not bounded/safe)."
foreach u [lsort -unique $unaccounted] {
    puts "  UNACCOUNTED\t$u"
}
if {$strict && [llength $unaccounted] > 0} {exit 1}
exit 0
