#!/usr/bin/env tclsh
###############################################################################
#
# apply_orphan_map.tcl --
#
#   Apply a list of "OLD-marker NEW-marker" rename pairs to every
#   test script under tests/.  Each line of the mapping file is
#   "R-NNNNN-NNNNN R-NNNNN-NNNNN"; comments start with "#" and blank
#   lines are skipped.  Only literal R-marker tokens are rewritten;
#   surrounding text is left intact.
#
# Usage:
#     tclsh tools/apply_orphan_map.tcl <map-file>
#
# Reports each rewrite as it goes; lists files modified at the end.
#
###############################################################################

if {[llength $argv] != 1} then {
    puts stderr "Usage: tclsh apply_orphan_map.tcl <map-file>"
    exit 1
}

set mapFile [lindex $argv 0]
set fp [open $mapFile r]
set raw [read $fp]
close $fp

# Build map: dict from old-marker -> new-marker.
set MAP [dict create]
foreach line [split $raw \n] {
    set line [string trim $line]
    if {$line eq "" || [string index $line 0] eq "#"} then { continue }
    set parts [split $line]
    set parts [lsearch -inline -all -not $parts ""]
    if {[llength $parts] < 2} then {
        puts stderr "skip: $line"
        continue
    }
    set old [lindex $parts 0]
    set new [lindex $parts 1]
    dict set MAP $old $new
}

puts "Loaded [dict size $MAP] mappings from $mapFile"

# Walk every .tcl under tests/ and apply.
set changedFiles [list]
set totalRewrites 0
foreach path [glob -nocomplain tests/*.tcl tests/*/*.tcl] {
    set fp [open $path r]
    set raw [read $fp]
    close $fp
    set new $raw
    set fileChanged 0
    dict for {old newMarker} $MAP {
        set cnt [regsub -all $old $new $newMarker new]
        if {$cnt > 0} then {
            puts [format "  %-30s  %d x %s -> %s" $path $cnt $old $newMarker]
            incr totalRewrites $cnt
            set fileChanged 1
        }
    }
    if {$fileChanged} then {
        set fp [open $path w]
        puts -nonewline $fp $new
        close $fp
        lappend changedFiles $path
    }
}

puts ""
puts "Files modified: [llength $changedFiles]"
puts "Total marker rewrites: $totalRewrites"
foreach f $changedFiles {
    puts "  $f"
}
