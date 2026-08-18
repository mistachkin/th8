#!/usr/bin/env tclsh
#
# check_polls.tcl --
#
#     TH8K-009 loop-audit enforcement.  Verifies that every function
#     listed in tools/data/poll_required.tsv -- the reviewed set of
#     command functions that contain an attacker-controlled loop and
#     were given a cancellation/step-limit poll -- still contains a
#     `Th8_Ready(` call in its body.  If a future edit removes the poll
#     (or renames the function), the gate fails, so the loop-audit
#     result cannot silently regress.
#
#     This is the enforcement half of the TH8K-009 audit; the full
#     reviewed matrix of every loop and its disposition lives in
#     docs/internal/loop_audit.md.
#
# Usage:
#     tclsh tools/check_polls.tcl
#
# Exit code: 0 if every required function polls; 1 on any violation or
#     malformed input.
#
###############################################################################

set root [file dirname [file dirname [file normalize [info script]]]]
set tsv [file join $root tools data poll_required.tsv]

if {![file exists $tsv]} {
    puts stderr "check_polls: missing $tsv"
    exit 1
}

# Shared helpers (strip_literals, extract_body).
source [file join $root tools check_polls_common.tcl]

# ---------------------------------------------------------------------------
# analyze_polls --
#     Given a function body, return {hasReady inLoop badMask maxMask}:
#       hasReady -> 1 if the body calls Th8_Ready( at all.
#       inLoop   -> 1 if at least one Th8_Ready( sits inside a for/while
#                   scope (a poll that is not inside any loop guards
#                   nothing -- TH8K-009 wants the poll IN the loop, not
#                   merely somewhere in the function).
#       badMask  -> 1 if a chunked-poll gate ("(x & 0xNNN) == 0") uses an
#                   interval mask greater than 0xFFF, i.e. it polls less
#                   often than the documented 4096-iteration maximum.
#       maxMask  -> the largest such mask seen (for diagnostics), or 0.
#     Brace-depth tracking with a per-scope loop/block tag; a `{` that
#     follows a for/while keyword opens a "loop" scope.  This is a
#     heuristic (it does not parse strings/comments), sufficient for the
#     tight command functions in the reviewed set.
# ---------------------------------------------------------------------------
proc analyze_polls {body} {
    set scope {}
    set pendingLoop 0
    set hasReady 0
    set inLoop 0
    set badMask 0
    set maxMask 0
    foreach line [split $body \n] {
        if {[regexp {(^|[^A-Za-z0-9_])(for|while)[[:space:]]*\(} $line]} {
            set pendingLoop 1
        }
        # Chunked-poll interval gate: "& 0xNNN ... == 0".
        if {[regexp {&[[:space:]]*0x([0-9a-fA-F]+)[^0-9a-fA-F]*==[[:space:]]*0} \
                $line -> hex]} {
            set m 0
            scan $hex %x m
            if {$m > $maxMask} {set maxMask $m}
            if {$m > 0xFFF} {set badMask 1}
        }
        # Walk the (literal-stripped) line char by char so a Th8_Ready( is
        # tested against the scope AT ITS POSITION -- before the closing braces
        # later on the same line pop the enclosing loop.
        set clean [strip_literals $line]
        set L [string length $clean]
        for {set c 0} {$c < $L} {incr c} {
            set ch [string index $clean $c]
            if {$ch eq "\{"} {
                if {$pendingLoop} {
                    lappend scope loop
                    set pendingLoop 0
                } else {
                    lappend scope block
                }
            } elseif {$ch eq "\}"} {
                if {[llength $scope] > 0} {
                    set scope [lrange $scope 0 end-1]
                }
            } elseif {$ch eq "T" &&
                [string range $clean $c [expr {$c + 9}]] eq "Th8_Ready("} {
                set hasReady 1
                if {[lsearch -exact $scope loop] >= 0} {
                    set inLoop 1
                }
            }
        }
    }
    return [list $hasReady $inLoop $badMask $maxMask]
}

set violations 0
set checked 0

set fd [open $tsv r]
set data [read $fd]
close $fd

# Cache file contents so a file with many required functions is read once.
array set filecache {}

foreach row [split $data \n] {
    set row [string trim $row]
    if {$row eq "" || [string index $row 0] eq "#"} {
        continue
    }
    set fields [split $row \t]
    if {[llength $fields] < 2} {
        puts stderr "check_polls: malformed row (need file<TAB>function): $row"
        set violations 1
        continue
    }
    set relfile [string trim [lindex $fields 0]]
    set func [string trim [lindex $fields 1]]
    set path [file join $root $relfile]

    if {![info exists filecache($path)]} {
        if {![file exists $path]} {
            puts stderr "check_polls: FAIL $relfile: file not found"
            incr violations
            set filecache($path) ""
            continue
        }
        set f [open $path r]
        set filecache($path) [read $f]
        close $f
    }
    set text $filecache($path)
    if {$text eq ""} {
        continue
    }

    set body [extract_body $text $func]
    incr checked
    if {$body eq ""} {
        puts stderr "check_polls: FAIL $relfile: function '$func' not found\
            (renamed or removed?)"
        incr violations
        continue
    }
    lassign [analyze_polls $body] hasReady inLoop badMask maxMask
    if {!$hasReady} {
        puts stderr "check_polls: FAIL $relfile: '$func' has an\
            attacker-controlled loop but no Th8_Ready poll (TH8K-009 regression)"
        incr violations
    } elseif {!$inLoop} {
        puts stderr "check_polls: FAIL $relfile: '$func' calls Th8_Ready but\
            NOT inside any for/while loop -- a poll outside the loop it is\
            meant to bound does not make the loop interruptible (TH8K-009)"
        incr violations
    } elseif {$badMask} {
        puts stderr "check_polls: FAIL $relfile: '$func' chunked poll uses\
            interval mask [format 0x%X $maxMask] (> 0xFFF); the documented\
            maximum interval is 4096 iterations (TH8K-009)"
        incr violations
    }
}

if {$violations > 0} {
    puts stderr "check_polls: $violations violation(s)"
    exit 1
}
puts "check_polls: OK -- all $checked poll-required functions poll Th8_Ready."
exit 0
