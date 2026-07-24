#!/usr/bin/env tclsh
#
# check_deps.tcl -- verify that every object dependency line in Makefile and
# Makefile.msc lists EXACTLY the in-tree (src/) headers that its source
# transitively #includes.
#
# Rationale:
#   The makefiles use explicit, hand-auditable per-object header dependency
#   lists (not compiler-generated .d files).  Those lists MUST stay in sync
#   with the real #include graph, or an edit to a header (e.g. th8_int_core.h,
#   which defines the Th8_Interp struct) will NOT trigger recompilation of an
#   object that bakes in that struct's layout -- producing a mixed-layout
#   binary whose stale stores land at wrong offsets (the Bug 62 class).
#
#   The reference is the STATIC #include closure (all quoted includes,
#   ignoring #if), computed with no compiler.  It is a proven superset of the
#   real compiled dependency set on every platform, so a match guarantees no
#   missing edge on any OS while remaining verifiable on any host.  Both
#   makefiles are expected to list this identical set.
#
# Usage:   tclsh tools/check_deps.tcl
# Exit:    0 = all lines correct;  1 = one or more mismatches (printed).
#
# See doc/internal/FINDINGS.md (Bug 62 root cause) and incomplete.md.

set ROOT [file normalize [file join [file dirname [info script]] ..]]
cd $ROOT

# ---------------------------------------------------------------------------
# static_scan -- transitive closure of quoted #includes that resolve to a
# src/ header, ignoring #if guards (a safe superset).
# ---------------------------------------------------------------------------
proc read_file {path} {
    if {[catch {open $path r} fp]} { return "" }
    set txt [read $fp]
    close $fp
    return $txt
}

proc static_scan {srcpath} {
    global ROOT
    set searchdirs [list "" "src/" "src/test/"]
    set seen [dict create]
    set out  [dict create]
    set stack [list $srcpath]
    while {[llength $stack]} {
        set f [lindex $stack end]
        set stack [lrange $stack 0 end-1]
        if {[dict exists $seen $f]} { continue }
        dict set seen $f 1
        set txt [read_file $f]
        set dir [file dirname $f]
        foreach {_ inc} [regexp -all -inline {#[ \t]*include[ \t]+"([^"]+)"} $txt] {
            set cands [list [file join $dir $inc]]
            foreach d $searchdirs { lappend cands "$d$inc" }
            foreach c $cands {
                set c [file normalize $c]
                set rel [string range $c [expr {[string length $ROOT]+1}] end]
                if {[string match "src/*" $rel] && [string match "*.h" $rel] \
                        && [file exists $c]} {
                    dict set out $rel 1
                    lappend stack $rel
                    break
                }
            }
        }
    }
    return [lsort [dict keys $out]]
}

# ---------------------------------------------------------------------------
# Source-token resolution (expand the make dir-vars used as source prefixes).
# ---------------------------------------------------------------------------
proc resolve_src {token} {
    # Expand the make dir-vars used as source prefixes.  $(S) carries a
    # trailing "/" in the GNU Makefile (S = src/) but is followed by an
    # explicit "\" in Makefile.msc; expand to "src/" and collapse any
    # resulting duplicate separators so both styles normalise correctly.
    set map [list {$(HARPY_PLUGIN_DIR)} src/plugins/harpy \
                  {$(CRYPTOGRAPHY_PLUGIN_DIR)} src/plugins/crypto \
                  {$(S)} src/]
    set t [string map $map $token]
    set t [string map {\\ /} $t]
    regsub -all {/+} $t / t
    if {[string match "src/*" $t] && [file exists $t]} { return $t }
    return ""
}

# ---------------------------------------------------------------------------
# Parse one makefile: return list of {obj source {declared-src-headers}}.
# gnu=1 -> "$(B)name.o: ..." with "/" and "|" order-only.
# gnu=0 -> "$(B)\name.obj: ..." with "\" and no "|".
# ---------------------------------------------------------------------------
proc parse_makefile {path gnu} {
    set txt [read_file $path]
    # join backslash-continuations into logical lines
    regsub -all {\\\n} $txt " " txt
    set rows {}
    if {$gnu} {
        set re {(?n)^\$\(B\)([A-Za-z0-9_]+)\.o:[ \t]*([^\n]*)$}
        set hdrpat {^\$\(S\)(.*)\.h$}
    } else {
        set re {(?n)^\$\(B\)\\([A-Za-z0-9_]+)\.obj:[ \t]*([^\n]*)$}
        set hdrpat {^\$\(S\)\\(.*)\.h$}
    }
    foreach {_ obj body} [regexp -all -inline $re $txt] {
        if {$gnu} { set body [lindex [split $body |] 0] }
        set toks [regexp -all -inline {\S+} $body]
        if {![llength $toks]} { continue }
        set src [resolve_src [lindex $toks 0]]
        if {$src eq ""} { continue }
        set declared {}
        foreach t [lrange $toks 1 end] {
            if {[regexp $hdrpat $t _ stem]} {
                lappend declared "src/[string map {\\ /} $stem].h"
            }
        }
        lappend rows [list $obj $src [lsort -unique $declared]]
    }
    return $rows
}

# ---------------------------------------------------------------------------
set nbad 0
foreach {label path gnu} {Makefile Makefile 1 Makefile.msc Makefile.msc 0} {
    if {![file exists $path]} { continue }
    foreach row [parse_makefile $path $gnu] {
        lassign $row obj src declared
        set real [static_scan $src]
        set missing {}
        set extra {}
        foreach h $real     { if {[lsearch -exact $declared $h] < 0} { lappend missing $h } }
        foreach h $declared { if {[lsearch -exact $real     $h] < 0} { lappend extra   $h } }
        if {[llength $missing] || [llength $extra]} {
            incr nbad
            puts "$label: $obj"
            if {[llength $missing]} { puts "    MISSING: $missing" }
            if {[llength $extra]}   { puts "    EXTRA:   $extra" }
        }
    }
}

if {$nbad} {
    puts "\ncheck_deps: FAIL -- $nbad object dependency line(s) out of sync."
    puts "Run tools/mkdeps or update the offending \$(B)NAME.o: / .obj: line(s)."
    exit 1
}
puts "check_deps: OK -- all object dependency lines match the #include closure."
exit 0
