###############################################################################
#
# mkmcdcwaiverdoc.tcl --
#
# Generate (and, with --check, validate) the PUBLIC MC/DC waiver register
# `docs/public/mcdc_waivers.md` from the machine-checked waiver file
# `tools/data/mcdc_waivers.tsv`.
#
# The register explains, per decision, why each waived MC/DC condition
# cannot be exercised by a test.  Because it is generated verbatim from the
# TSV, EVERY declared waiver is guaranteed to be documented -- and the
# `--check` mode wires that guarantee into the build: it regenerates the
# document in memory and compares it to the committed file, failing if they
# differ (a waiver added to the TSV but not reflected in the doc, a stale
# doc entry, or a hand-edit).  A waiver that is not documented is not valid.
#
# Usage:
#     tclsh tools/mkmcdcwaiverdoc.tcl              # (re)write the document
#     tclsh tools/mkmcdcwaiverdoc.tcl --check      # verify it is up to date
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set ::TSV "tools/data/mcdc_waivers.tsv"
set ::DOC "docs/public/mcdc_waivers.md"

set ::order {
    correlated-conditions defensive-omit-guard overflow-guard
    reserved-value-reject startup-cached-callback environmental
    win32-only network external-oom-unreachable synchronization-invasive
}

set ::title {
    correlated-conditions   "Correlated conditions"
    defensive-omit-guard    "Defensive invariant guards"
    overflow-guard          "Overflow / size-ceiling guards"
    reserved-value-reject   "Reserved-value rejection loops"
    startup-cached-callback "Startup-cached platform callbacks"
    environmental           "Environmental (host/filesystem) limits"
    win32-only              "Windows-only code paths"
    network                 "Network-gated conditions"
    external-oom-unreachable "External-library OOM guards"
    synchronization-invasive "Synchronization-invasive arms"
}

# Per-class explanation of WHY the whole class is intrinsically unhittable.
set ::prose {
    correlated-conditions {MC/DC requires each condition of a decision to be
shown to *independently* change the decision's outcome -- an "independence
pair" of test vectors in which only that condition differs.  In these decisions
the conditions are structurally correlated, so no choice of inputs can form the
pair.  The clearest case is a sign test on one operand: `iLeft > 0` and
`iLeft < 0` are two *separate* conditions to the coverage tool, but they are
mutually determined by the single value of `iLeft` -- you cannot make one true
and the other true, nor flip one while holding the rest such that the outcome
changes.  The missing pairs are therefore unformable *by construction*, not
merely un-exercised.}
    defensive-omit-guard {These are runtime checks of an *internal invariant* --
for example that the expression parser always hands the evaluator a well-formed
tree, or that a live object always carries the sub-field being dereferenced.
Under correct operation the invariant always holds, so the check's "invariant
violated" arm never runs and its independence pair cannot be formed.

The natural way to tell a coverage tool that an arm is dead is to wrap the
condition in a "this never happens" macro, which folds it to a constant and
excludes it from the metric.  TH8 deliberately does NOT do that here, and the
reason is specific to how MC/DC is measured.  The MC/DC build is an *omit*
build (`TH8_OMIT_AUXILIARY_SAFETY_CHECKS`), in which such a wrapped guard is
COMPILED OUT entirely.  If the invariant were ever violated -- by a future
change, memory corruption, or an untrusted extension feeding in a malformed
structure -- a compiled-out guard would be gone, and the code would dereference
a NULL or garbage pointer and crash (SIGSEGV) instead of returning a clean
error.  Keeping the check as a plain `if` means that even in the omit build a
violated invariant degrades to a graceful error rather than a crash.

The unavoidable consequence is that the guard's error arm is uncovered: it
cannot be reached without actually violating the invariant, which no test can
safely do.  That coverage gap is the intended price of defense-in-depth, so the
guard is waived rather than wrapped.}
    overflow-guard {A belt-and-suspenders integer-overflow or size-ceiling
check whose failing arm cannot occur given the operand type's range or the
value's provenance -- e.g. `n + 1 < n` where `n` is bounded well below
`SIZE_MAX`, a path-length ceiling of 100000 bytes, or an RSA-signature-length
bound that a valid (modulus-sized) signature never exceeds.  The guard is cheap
insurance against a future change; its overflow arm is unreachable today.}
    reserved-value-reject {A loop that re-draws a cryptographic-strength random
token until it is not a reserved sentinel (`0`, `~0`, or `1`).  The sentinel
arms fire only when a full 64-bit CSPRNG draw lands on exactly one of those
three values -- astronomically improbable in a test run -- and the sentinels
gate no externally observable behavior.  (They could in principle be forced via
the `forceRandomBytes` test hook; they are waived rather than driven because
the resulting test would exercise a contrived, behaviorally-inert path.)}
    startup-cached-callback {A platform callback that runs ONCE during
interpreter or process initialization and caches its result -- the user name
via `getpwuid`, or the executable/module/base path via `dladdr`.  By the time
any script, test, or runtime fault could act, the callback has already run and
its value is cached at file scope; it is not re-invoked, so a runtime fixture
cannot re-drive its alternative arms.  (`pw->pw_name` etc. are also never NULL
when the lookup succeeds.)}
    environmental {Requires a host or filesystem condition that cannot be
synthesized in the sandbox: two paths under a single-filesystem sandbox base
that differ in `st_dev` (a cross-device comparison -- impossible when the base
is one filesystem), a symlink chain 256 levels deep, or a file larger than
256 MB.  These need environmental fixtures, not scripts.}
    win32-only {A code path selected only on Windows.  The whole block is
gated -- directly or transitively -- on `TH8_IS_SEP('\\')` (backslash treated
as a path separator), which is false on the POSIX host where MC/DC is measured,
so the path is never entered.}
    network {The condition sits behind a live network operation -- DNS
resolution (`getaddrinfo`, libunbound), an NTP query, or an HTTPS fetch -- or
is only reached after such an operation returns data to parse or verify.
Exercising it needs a real network or a mock that has not been built; the MC/DC
host runs offline.}
    external-oom-unreachable {An error-return guard on an EXTERNAL library's
API -- OpenSSL big-number allocation (`BN_new`/`BN_CTX_new`/`BN_dup`),
parameter-builder (`OSSL_PARAM_BLD_push_BN`), and key-import
(`EVP_PKEY_fromdata`) calls -- whose failing arm fires only under that
library's INTERNAL out-of-memory or internal error.  TH8's fault-injection
harness hooks TH8's own allocator, not OpenSSL's, so it cannot induce an
OpenSSL OOM.  This is distinct from TH8's *own* allocation-failure paths, which
ARE reachable via the alloc-fault harness and remain tracked debt, not waivers.}
    synchronization-invasive {Driving the arm requires faking a
`pthread_cond_wait` / `pthread_cond_timedwait` return code, or forcing a precise
signal-arrives-exactly-at-timeout race, in the event-wait path.  Either would
perturb real synchronization; the arm is not safely or deterministically
reachable from a test.}
}

proc read_waivers {} {
    set fh [open $::TSV r]
    set byclass [dict create]
    set n 0
    foreach line [split [read $fh] "\n"] {
        if {[string trim $line] eq "" || [string index [string trimleft $line] 0] eq "#"} continue
        set c [split $line "\t"]
        if {[llength $c] != 5} continue
        lassign $c file func text class rat
        dict lappend byclass $class [list $file $func $text $rat]
        incr n
    }
    close $fh
    return [list $byclass $n]
}

# Scrub internal-only references (FINDINGS.md and internal bug numbers are
# not part of the public repository) from a per-decision rationale.
proc scrub {rat} {
    regsub -all {\s*\(FINDINGS[^)]*\)} $rat "" rat
    regsub -all -nocase {\mBug[- ]\d+\M[:\s]*} $rat "" rat
    set rat [string trim $rat]
    if {$rat ne ""} { set rat "[string toupper [string index $rat 0]][string range $rat 1 end]" }
    return [string trimright $rat " ."]
}

proc generate {} {
    lassign [read_waivers] byclass total
    set L [list]
    proc emit {args} { upvar 1 L L; lappend L {*}$args }

    emit "# TH8 MC/DC waiver register"
    emit ""
    emit "This document explains, per decision, why each condition waived from the TH8"
    emit "MC/DC (Modified Condition/Decision Coverage) coverage gate cannot be exercised"
    emit "by a test.  It is the human-readable companion to the machine-checked waiver"
    emit "file `tools/data/mcdc_waivers.tsv`, and is GENERATED from it by"
    emit "`tools/mkmcdcwaiverdoc.tcl`.  The RTM gate `make check-mcdc`"
    emit "(`tools/mcdc_gate.tcl`) enforces that every waiver still corresponds to a"
    emit "genuinely uncovered decision -- a waiver whose decision becomes covered, or"
    emit "disappears, fails the gate as **stale** and must be removed -- and"
    emit "`make check-mcdc-doc` enforces that every waiver is documented here."
    emit ""
    emit "## Why a waiver register exists"
    emit ""
    emit "TH8's RTM 1.0 MC/DC gate targets **95% of the *reachable* denominator**:"
    emit "`covered / (total - waived) >= 95%`.  A flat 95% is not achievable by writing"
    emit "more tests, because a residue of conditions is undrivable *by construction* --"
    emit "structurally-correlated conditions, defense-in-depth guards against states that"
    emit "cannot occur, Windows-only or network-only paths, external-library error"
    emit "returns, and so on.  Rather than wrap those away (which, under the omit build,"
    emit "would compile out real safety handlers) or pretend they are covered, each is"
    emit "recorded here with a concrete reason it cannot be hit, assigned to one of the"
    emit "waiver *classes* below.  Everything NOT waived is genuine, drivable coverage"
    emit "debt and still counts against the 95%."
    emit ""
    emit "As of the last generation there are **$total waivers**.  A decision may"
    emit "contribute more than one waived condition; the gate accounts for conditions,"
    emit "this register lists decisions."
    emit ""
    emit "## Waiver classes"
    emit ""
    foreach cl $::order {
        if {![dict exists $byclass $cl]} continue
        set items [dict get $byclass $cl]
        emit "### $cl -- [dict get $::title $cl] ([llength $items])"
        emit ""
        # Split the class prose into paragraphs on blank lines; collapse
        # intra-paragraph whitespace so it reflows cleanly in Markdown.
        set p2 [regsub -all {\n[ \t]*\n} [string trim [dict get $::prose $cl]] "\x00"]
        foreach chunk [split $p2 "\x00"] {
            regsub -all {\s+} [string trim $chunk] " " chunk
            emit $chunk
            emit ""
        }
        foreach it [lsort -index 0 $items] {
            lassign $it file func text rat
            emit "- **`$func`** (`src/$file`)"
            emit "  - Decision: `$text`"
            emit "  - Why it cannot be hit: [scrub $rat]."
        }
        emit ""
    }
    emit "## Maintenance"
    emit ""
    emit "This file is GENERATED -- do not edit it by hand.  The source of truth is"
    emit "`tools/data/mcdc_waivers.tsv` (one tab-separated row per waiver: file,"
    emit "function, decision source-text, class, rationale).  Regenerate with"
    emit "`tclsh tools/mkmcdcwaiverdoc.tcl`.  `make check-mcdc-doc` (part of the audit"
    emit "gate) fails if this file is out of date, so every waiver declared in the TSV"
    emit "must appear here to be valid.  `make check-mcdc` separately rejects a stale"
    emit "waiver whose decision is now covered or gone."
    return "[join $L \n]\n"
}

# --- main -------------------------------------------------------------------

set mode write
if {[llength $argv] == 1 && [lindex $argv 0] eq "--check"} { set mode check }

set doc [generate]

if {$mode eq "write"} {
    set fh [open $::DOC w]
    puts -nonewline $fh $doc
    close $fh
    puts "wrote $::DOC"
    exit 0
}

# --check
if {![file exists $::DOC]} {
    puts stderr "check-mcdc-doc: FAIL -- $::DOC does not exist; run\
        `tclsh tools/mkmcdcwaiverdoc.tcl`"
    exit 1
}
set fh [open $::DOC r]
set have [read $fh]
close $fh
if {$have eq $doc} {
    puts "check-mcdc-doc: OK -- every waiver is documented and $::DOC is up to date"
    exit 0
}
puts stderr "check-mcdc-doc: FAIL -- $::DOC is out of date with\
    tools/data/mcdc_waivers.tsv"
puts stderr "  (a waiver was added/changed/removed without regenerating the"
puts stderr "   public register).  Run: tclsh tools/mkmcdcwaiverdoc.tcl"
# show the first differing line to aid the fix.
set hl [split $have \n]
set dl [split $doc \n]
set max [expr {max([llength $hl],[llength $dl])}]
for {set i 0} {$i < $max} {incr i} {
    if {[lindex $hl $i] ne [lindex $dl $i]} {
        puts stderr "  first difference at line [expr {$i+1}]:"
        puts stderr "    committed:  [lindex $hl $i]"
        puts stderr "    generated:  [lindex $dl $i]"
        break
    }
}
exit 1
