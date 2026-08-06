#!/usr/bin/env tclsh
#
# gencmdindex.tcl --
#
#   Generate (and, with --check, validate) the Command Index (Appendix G)
#   of the Tcl Language Standard directly from the standard's own command
#   sections, so the index never drifts from the normative text.
#
#   Usage:
#       tclsh tools/gencmdindex.tcl <standard.md>
#           Emit the Appendix G markdown on stdout (for regeneration).
#       tclsh tools/gencmdindex.tcl --check <standard.md>
#           Regenerate in memory and compare to the Appendix G embedded in
#           <standard.md>; exit non-zero if they differ.  Wired into the
#           build as `make check-cmdindex` (part of `make audit`).
#
#   A "command chapter" is any "### N  Title" whose Title ends in "Command"
#   or "Commands" (this selects the built-in-command chapters and excludes
#   topic chapters such as "10  Command Evaluation" or "26  Error Model").
#   Within such a chapter each "#### N.M  text" section is a command unless
#   "text" begins with an uppercase letter (a topic sub-heading, e.g.
#   "Platform Variables").  Slash-separated variants ("string first /
#   string last") are indexed as separate entries pointing at the same
#   section.
#

# genIndex DATA -- return the Appendix G markdown (no trailing newline)
# derived from the command sections of the standard text DATA.
proc genIndex {data} {
    set chapterNum ""
    set inCmdChapter 0
    set catOrder {}
    array set catTitle {}
    array set catCmds {}
    set allCmds {}

    foreach line [split $data "\n"] {
        if {[regexp {^### ([0-9]+) +(.+?)\s*$} $line -> num title]} {
            set chapterNum $num
            set inCmdChapter [regexp {Commands?$} $title]
            if {$inCmdChapter && ![info exists catCmds($num)]} {
                set catCmds($num) {}
                set catTitle($num) $title
                lappend catOrder $num
            }
            continue
        }
        if {!$inCmdChapter} continue
        if {[regexp {^#### ([0-9]+\.[0-9]+) +(.+?)\s*$} $line -> sec text]} {
            regsub -all {`} $text "" text
            set text [string trim $text]
            if {![regexp {^[a-z]} $text]} continue
            foreach cmd [split $text "/"] {
                set cmd [string trim $cmd]
                if {$cmd eq ""} continue
                lappend catCmds($num) [list $cmd $sec]
                lappend allCmds [list $cmd $sec]
            }
        }
    }

    set out {}
    set total [llength $allCmds]

    lappend out "### Appendix G --- Command Index"
    lappend out ""
    lappend out "This appendix is informative.  It catalogs every script-visible"
    lappend out "command specified by this standard, with a pointer to the section"
    lappend out "that normatively defines it.  It is generated mechanically from the"
    lappend out "command sections of this document by `tools/gencmdindex.tcl`, so it"
    lappend out "cannot drift from the normative text.  Ensemble sub-command variants"
    lappend out "written with a slash in a section heading (for example `string"
    lappend out "first / string last`) are listed separately, both pointing at the"
    lappend out "shared section.  $total command entries are indexed."
    lappend out ""
    lappend out "#### G.1  By command category"
    lappend out ""
    lappend out "The category numbering mirrors the chapter numbering of Part III;"
    lappend out "each chapter corresponds to a built-in command group, several of"
    lappend out "which are individually removable at compile time via the"
    lappend out "`TH8_PLUGIN_*` feature gates."

    foreach num $catOrder {
        lappend out ""
        lappend out "##### Section $num --- $catTitle($num)"
        lappend out ""
        foreach entry [lsort -index 0 $catCmds($num)] {
            lassign $entry cmd sec
            lappend out "- `$cmd` --- §$sec"
        }
    }

    lappend out ""
    lappend out "#### G.2  Alphabetical index"
    lappend out ""
    foreach entry [lsort -unique -index 0 $allCmds] {
        lassign $entry cmd sec
        lappend out "- `$cmd` --- §$sec"
    }

    return [join $out "\n"]
}

# extractAppendixG DATA -- return the Appendix G block currently embedded
# in the standard text DATA (from "### Appendix G " to just before the
# final "---\n\n*End of Document*" marker), with trailing whitespace
# trimmed to match genIndex's output.
proc extractAppendixG {data} {
    set start [string first "### Appendix G " $data]
    if {$start < 0} { return "" }
    set sub [string range $data $start end]
    set end [string first "---\n\n*End of Document*" $sub]
    if {$end >= 0} { set sub [string range $sub 0 [expr {$end - 1}]] }
    return [string trimright $sub]
}

# --- main --------------------------------------------------------------

set mode gen
set rest $argv
if {[lindex $rest 0] eq "--check"} {
    set mode check
    set rest [lrange $rest 1 end]
}
if {[llength $rest] != 1} {
    puts stderr "usage: gencmdindex.tcl ?--check? <standard.md>"
    exit 2
}
set specFile [lindex $rest 0]

if {![file exists $specFile]} {
    puts stderr "gencmdindex: $specFile does not exist"
    exit 2
}
set fd [open $specFile r]
set data [read $fd]
close $fd

if {$mode eq "gen"} {
    puts [genIndex $data]
    exit 0
}

# --check
set expected [genIndex $data]
set actual [extractAppendixG $data]
if {$actual eq ""} {
    puts stderr "check-cmdindex: FAIL -- no Appendix G found in $specFile;\
run `tclsh tools/gencmdindex.tcl $specFile` and insert the output."
    exit 1
}
if {$expected eq $actual} {
    set n [regexp -all -line {^- `} $expected]
    puts "check-cmdindex: OK -- Appendix G is in sync with the command\
sections ($n index entries)"
    exit 0
}

# Report the first differing line to make drift easy to locate.
set el [split $expected "\n"]
set al [split $actual "\n"]
set lim [expr {min([llength $el], [llength $al])}]
set diffAt -1
for {set i 0} {$i < $lim} {incr i} {
    if {[lindex $el $i] ne [lindex $al $i]} { set diffAt $i; break }
}
if {$diffAt < 0} { set diffAt $lim }
puts stderr "check-cmdindex: FAIL -- Appendix G is out of date with the\
command sections of $specFile."
puts stderr "  first difference at line [expr {$diffAt + 1}] of the appendix:"
puts stderr "    embedded : [lindex $al $diffAt]"
puts stderr "    expected : [lindex $el $diffAt]"
puts stderr "  Regenerate with: tclsh tools/gencmdindex.tcl $specFile"
exit 1
