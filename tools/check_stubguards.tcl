#!/usr/bin/env tclsh
###############################################################################
#
# check_stubguards.tcl --
#
#     Guard the public stub table against feature-gating drift.
#
#     tools/mkstubs.tcl emits the public stub table (th8Decls.h /
#     th8StubInit.c) with a hand-maintained `guardMap` that wraps
#     conditionally-compiled TH8_API functions in #if / #else 0 so the
#     table stays link-clean when a feature is disabled.  If a TH8_API
#     function is declared inside a feature #if in th8.h but is MISSING
#     from that guardMap, the generated stub table references it
#     unconditionally and the reduced-feature build fails to link
#     (e.g. Th8_GetEmbeddedKeyring under ENABLE_CRYPTOGRAPHY=0).
#
#     This check re-derives each TH8_API function's feature guard from
#     th8.h's own #if context and FAILS if any is absent from the
#     guardMap -- the same drift-gate discipline as check_deps /
#     check_amal.  (Platform-only guards live in the guardMap alone and
#     are not required to appear in th8.h; this check is one-directional:
#     every feature-#if-guarded declaration must be covered.)
#
# Usage:
#
#     tclsh tools/check_stubguards.tcl [src/th8.h] [tools/mkstubs.tcl]
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set hdrPath [expr {$argc >= 1 ? [lindex $argv 0] : "src/th8.h"}]
set mkPath  [expr {$argc >= 2 ? [lindex $argv 1] : "tools/mkstubs.tcl"}]

# Only these macros denote a real feature/platform gate that the stub table
# must mirror; include-guards (#ifndef TH8_H_) and __cplusplus are ignored.
proc isFeatureCond {cond} {
    return [regexp {TH8_ENABLE_|TH8_PLUGIN_|TH8_USE_|TH8_PLATFORM_|_WIN32|WIN32|__APPLE__} $cond]
}

# Extract each TH8_API function -> its feature guard (joined #if stack).
proc scanHeader {path} {
    set fp [open $path r]; set lines [split [read $fp] \n]; close $fp
    set stack {}
    set out [dict create]
    set n [llength $lines]
    for {set i 0} {$i < $n} {incr i} {
        set t [string trim [lindex $lines $i]]
        if {[regexp {^#\s*if\s+(.*)} $t -> c]}      { lappend stack $c; continue }
        if {[regexp {^#\s*ifdef\s+(\S+)} $t -> m]}  { lappend stack "defined($m)"; continue }
        if {[regexp {^#\s*ifndef\s+(\S+)} $t -> m]} { lappend stack "!defined($m)"; continue }
        if {[regexp {^#\s*elif\s+(.*)} $t -> c]}    { if {[llength $stack]} {lset stack end $c}; continue }
        if {[regexp {^#\s*else} $t]}                { if {[llength $stack]} {lset stack end "!([lindex $stack end])"}; continue }
        if {[regexp {^#\s*endif} $t]}               { if {[llength $stack]} {set stack [lrange $stack 0 end-1]}; continue }
        if {[string match "TH8_API *" $t]} {
            set full $t
            while {![string match "*);*" $full] && $i < $n - 1} {
                incr i; append full " " [string trim [lindex $lines $i]]
            }
            regsub -all {\s+} $full { } full
            if {[regexp {^TH8_API\s+(.+?)\s*([Tt]h8_?\w+)\s*\((.*)\)\s*;} $full -> rt nm pr]} {
                if {$nm eq "Th8_Interp"} continue
                set fg {}
                foreach c $stack { if {[isFeatureCond $c]} { lappend fg $c } }
                dict set out $nm [join $fg " && "]
            }
        }
    }
    return $out
}

# Parse the guardMap names from mkstubs.tcl.
proc scanGuardMap {path} {
    set fp [open $path r]; set data [read $fp]; close $fp
    set inMap 0; set gm [dict create]
    foreach line [split $data \n] {
        if {[regexp {array set guardMap} $line]} { set inMap 1; continue }
        if {$inMap && [regexp {^\s*\}\s*$} $line]} { set inMap 0 }
        if {$inMap && [regexp {^\s*(Th8_\w+)\s+\{(.+)\}\s*$} $line -> nm c]} {
            dict set gm $nm 1
        }
    }
    return $gm
}

set apiGuard [scanHeader $hdrPath]
set gm [scanGuardMap $mkPath]

set missing {}
dict for {nm g} $apiGuard {
    if {$g ne "" && ![dict exists $gm $nm]} {
        lappend missing "$nm  (guarded by: $g)"
    }
}

if {[llength $missing] > 0} {
    puts stderr "check_stubguards: FAIL -- [llength $missing] feature-guarded\
        TH8_API function(s) missing from mkstubs.tcl guardMap:"
    foreach m [lsort $missing] { puts stderr "  $m" }
    puts stderr "Add each to the guardMap (with the shown condition) and\
        re-run genstubs, else the reduced-feature build will not link."
    exit 1
}

puts "check_stubguards: OK -- every feature-guarded TH8_API function is in the\
    stub guardMap ([dict size $apiGuard] APIs scanned)."
exit 0
