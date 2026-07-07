#!/usr/bin/env tclsh
#
# move_unset_to_cleanup.tcl --
#
#     Refactor: every cleanup-class command in -setup or -body
#     blocks of a runTest is moved to -cleanup of the same test.
#     If the same line already exists in -cleanup, the
#     setup/body copy is just deleted.
#
#     Tests in tests/unset.tcl (or named "*-unset-*") that test
#     the unset command itself are skipped.
#
# Usage: tclsh tools/move_unset_to_cleanup.tcl <file> ...
#

# Build literal patterns at file scope to keep the proc bodies
# brace-clean.
set OB [format %c 123]

set patterns [list]
lappend patterns [list "unset"             "unset"]
lappend patterns [list "unset"             "unset "]
lappend patterns [list "array-unset"       "array unset "]
lappend patterns [list "ns-delete"         "catch ${OB}namespace delete"]
lappend patterns [list "file-delete"       "catch ${OB}file delete"]
lappend patterns [list "file-delete-force" "file delete -force"]
lappend patterns [list "close-catch"       "catch ${OB}close"]
lappend patterns [list "rename-restore"    "catch ${OB}rename"]

set bodyOpen    "-body ${OB}"
set setupOpen   "-setup ${OB}"
set cleanupOpen "-cleanup ${OB}"
set testOpen    "runTest ${OB}"

# Return tag if cleanup-class, "" otherwise.
proc detect_tag {t patterns} {
    foreach p $patterns {
        set tag [lindex $p 0]
        set pre [lindex $p 1]
        if {$tag eq "unset" && $pre eq "unset" && $t ne "unset"} then {
            continue
        }
        if {[string first $pre $t] == 0} then { return $tag }
    }
    return ""
}

proc process_file {path bodyOpen setupOpen cleanupOpen testOpen patterns} {
    if {[catch {open $path r} fh]} then { return 0 }
    set lines [split [read $fh] "\n"]
    close $fh

    # Parse pass.
    set tests [list]
    set cur [dict create name "" valid 0]
    set section ""
    set n [llength $lines]
    for {set i 0} {$i < $n} {incr i} {
        set ln [lindex $lines $i]
        set t [string trim $ln]
        if {[string first $testOpen $ln] == 0} then {
            if {[dict get $cur valid]} then {
                lappend tests $cur
            }
            set cur [dict create name "" valid 1 skip 0 \
                setup [list] body [list] cleanup [list] \
                cleanup_close -1]
            if {[regexp -- "test (\\S+) " $ln -> tn]} then {
                dict set cur name $tn
                if {[string match "*-unset-*" $tn]} then {
                    dict set cur skip 1
                }
                if {[string match "unset-*" $tn]} then {
                    dict set cur skip 1
                }
            }
            set section ""
            continue
        }
        if {![dict get $cur valid]} then { continue }
        if {[string first $setupOpen $ln] >= 0} then {
            set section setup; continue
        }
        if {[string first $bodyOpen $ln] >= 0} then {
            set section body; continue
        }
        if {[string first $cleanupOpen $ln] >= 0} then {
            set section cleanup; continue
        }
        if {[string first "-result "      $ln] >= 0
                || [string first "-match "       $ln] >= 0
                || [string first "-returnCodes " $ln] >= 0} then {
            set section ""; continue
        }
        if {$section eq "cleanup" && $t eq "\}"} then {
            dict set cur cleanup_close $i
            set section ""
            continue
        }
        if {$section ne "" && $t eq "\}"} then {
            set section ""
            continue
        }
        if {$section ne ""} then {
            dict lappend cur $section $i
        }
    }
    if {[dict get $cur valid]} then { lappend tests $cur }

    # Plan edits.
    set delete_lines [list]
    set appends [list]
    set changes 0
    foreach trec $tests {
        if {[dict get $trec skip]} then { continue }
        set cleanup_trimmed [list]
        foreach li [dict get $trec cleanup] {
            lappend cleanup_trimmed [string trim [lindex $lines $li]]
        }
        foreach sect {setup body} {
            foreach li [dict get $trec $sect] {
                set orig [lindex $lines $li]
                set t [string trim $orig]
                set tag [detect_tag $t $patterns]
                if {$tag eq ""} then { continue }
                lappend delete_lines $li
                if {[lsearch -exact $cleanup_trimmed $t] < 0} then {
                    set indent "  "
                    if {[llength [dict get $trec cleanup]] > 0} then {
                        set first_cli [lindex [dict get $trec cleanup] 0]
                        regexp {^(\s*)} [lindex $lines $first_cli] -> indent
                    }
                    lappend appends [list \
                        [dict get $trec cleanup_close] \
                        "${indent}${t}"]
                    lappend cleanup_trimmed $t
                }
                incr changes
            }
        }
    }
    if {$changes == 0} then { return 0 }

    # Apply deletes and appends.
    array set deleted {}
    foreach li $delete_lines { set deleted($li) 1 }
    set out [list]
    array set old_to_new {}
    for {set i 0} {$i < $n} {incr i} {
        if {[info exists deleted($i)]} then { continue }
        set old_to_new($i) [llength $out]
        lappend out [lindex $lines $i]
    }
    foreach a $appends {
        set ccclose [lindex $a 0]
        set content [lindex $a 1]
        if {$ccclose < 0 || ![info exists old_to_new($ccclose)]} then {
            puts stderr "WARN: $path: skipping append for $content (no cleanup section)"
            continue
        }
        set ni $old_to_new($ccclose)
        set out [linsert $out $ni $content]
        foreach k [array names old_to_new] {
            if {$old_to_new($k) >= $ni} then { incr old_to_new($k) }
        }
    }

    set fh [open $path w]
    puts -nonewline $fh [join $out "\n"]
    close $fh
    puts "  $path: $changes changes"
    return $changes
}

set total 0
foreach f $argv {
    incr total [process_file $f $bodyOpen $setupOpen $cleanupOpen \
        $testOpen $patterns]
}
puts "Total changes: $total"
