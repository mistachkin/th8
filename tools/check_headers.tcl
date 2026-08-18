#!/usr/bin/env tclsh
###############################################################################
#
# check_headers.tcl --
#
#     Function-header enforcement for TH8 C source.  Every function
#     DEFINITION in a TH8 .c file must be immediately preceded by a
#     Tcl-style header banner that names THAT function:
#
#         /*
#          *----------------------------------------------------------------------
#          *
#          * FunctionName --
#          *
#          *     ... description (Why / How) ...
#          *
#          *----------------------------------------------------------------------
#          */
#         static int
#         FunctionName(...)
#         {
#
#     The enforced, machine-checkable contract is the `FunctionName --`
#     banner line: the header block directly above a definition must
#     contain a line of the form `* <FunctionName> --`.  This catches
#     two failure modes at once:
#       * a function with NO header comment, and
#       * a GROUPED header (`Foo / Bar --`) that names several functions
#         in one block -- only the last-named function satisfies its own
#         `Name --` banner, so the others are flagged, forcing the
#         project's one-header-per-function convention.
#
#     Detection targets the TH8 layout (return type on its own line, the
#     function name at column 0 immediately followed by `(`, and a `{` at
#     column 0), which is unambiguous and keeps false positives near zero.
#
# Usage:
#     tclsh tools/check_headers.tcl [file.c ...]
#       No args -> scan the default TH8 source set (src/*.c, plugins, test).
#     tclsh tools/check_headers.tcl --list
#       Print the files that would be scanned and exit.
#
# Exit status:
#     0 if every scanned function has its banner; 1 if any violation is
#     found (so the build fails).  A one-line summary is always printed.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

#
# Whole-file exemptions: vendored third-party code (not TH8's to style) and
# generated files.  Matched by exact repo-relative path.
#
set ::EXEMPT_FILES [list \
    src/spilornis.c \
    src/th8_amal.c]

#
# Vendored trees are excluded by path substring (mimalloc, tommath, curl,
# openssl, unbound, regex engine, sqlite3 amalgamation proper, bestline).
#
set ::EXEMPT_SUBSTR [list \
    /externals/ \
    mimalloc \
    tommath \
    bestline \
    regex_regcomp regex_regexec regex_regfree regex_regprefix regex_regerror \
    ConvertUTF]

#
# Identifiers that appear at column 0 followed by `(` but are NOT function
# definitions: X-macro / code-generating macro invocations.  The return-type
# heuristic already filters most; this is a belt-and-suspenders allow-list.
#
set ::NOT_FUNCTIONS [list \
    PT_0 PT_1 PT_2 PT_3 PT_4 PT_5 PT_V0 PT_V1 PT_V2 PT_V3 PT_V4 \
    audit_rule MERGE_SLOT TH8_FAULT_SLOT]


###############################################################################
#
# default_source_files --
#
#     Return the default set of TH8 .c files to scan: core, plugins, and
#     the test library.  Header files are not scanned (they hold
#     declarations, not definitions).  Vendored / generated files are
#     removed via is_exempt.
#
###############################################################################

proc default_source_files {} {
  set files [list]
  foreach pattern {
    src/*.c
    src/plugins/*.c
    src/plugins/*/*.c
    src/sqlite3/*.c
    src/test/*.c
  } {
    foreach f [glob -nocomplain $pattern] {
      lappend files $f
    }
  }
  return [lsort -unique $files]
}


###############################################################################
#
# is_exempt --
#
#     Return 1 if path is a vendored / generated file that must not be
#     header-checked, else 0.
#
###############################################################################

proc is_exempt {path} {
  if {[lsearch -exact $::EXEMPT_FILES $path] >= 0} then {
    return 1
  }
  foreach s $::EXEMPT_SUBSTR {
    if {[string first $s $path] >= 0} then {
      return 1
    }
  }
  return 0
}


###############################################################################
#
# comment_start_mask --
#
#     Given a list of source lines, return a parallel list of booleans:
#     element i is 1 if line i BEGINS inside a /* ... */ block comment.
#     A small state machine scans each line for `/*` and `*/`, ignoring
#     `//` line comments and (best-effort) `/*` sequences inside string or
#     character literals.  This is used to avoid treating commented-out
#     code as a function definition.
#
###############################################################################

proc comment_start_mask {lines} {
  set mask [list]
  set inBlock 0
  foreach line $lines {
    lappend mask $inBlock
    set len [string length $line]
    set i 0
    set inStr 0
    set strCh ""
    while {$i < $len} {
      set c [string index $line $i]
      set c2 [string range $line $i [expr {$i + 1}]]
      if {$inBlock} then {
        if {$c2 eq "*/"} then {
          set inBlock 0
          incr i 2
          continue
        }
        incr i
        continue
      }
      if {$inStr} then {
        if {$c eq "\\"} then {
          incr i 2
          continue
        }
        if {$c eq $strCh} then {
          set inStr 0
        }
        incr i
        continue
      }
      if {$c2 eq "//"} then {
        break
      }
      if {$c2 eq "/*"} then {
        set inBlock 1
        incr i 2
        continue
      }
      if {$c eq "\"" || $c eq "'"} then {
        set inStr 1
        set strCh $c
        incr i
        continue
      }
      incr i
    }
  }
  return $mask
}


###############################################################################
#
# looks_like_return_type --
#
#     Return 1 if the given (trimmed) line looks like the return-type line
#     of a TH8 function definition -- i.e. a type expression that does NOT
#     end a statement or open a new construct.  Rejects lines ending in
#     any of ; , ) { } ( and preprocessor / label lines.
#
###############################################################################

proc looks_like_return_type {line} {
  set t [string trim $line]
  if {$t eq ""} then { return 0 }
  if {[string index $t 0] eq "#"} then { return 0 }
  if {[string index $t 0] eq "*"} then { return 0 }
  if {[string first "(" $t] >= 0} then { return 0 }
  set last [string index $t end]
  if {[string first $last ";,){}("] >= 0} then { return 0 }
  # Must end in a word char or a pointer star.
  if {![regexp {[\w*]$} $t]} then { return 0 }
  return 1
}


###############################################################################
#
# is_definition --
#
#     Return 1 if the construct whose name-and-open-paren begins on line
#     startIdx is a function DEFINITION (its argument list is followed by
#     an open brace), or 0 if it is a DECLARATION / prototype (followed by
#     a semicolon).  Scans forward for the first line that ends the
#     construct: in the TH8 layout the open brace is at column 0 on its own
#     line and a declaration ends in ");", so the first line ending in an
#     open brace (definition) or a semicolon (declaration) is decisive.
#
###############################################################################

proc is_definition {lines startIdx} {
  set n [llength $lines]
  set limit [expr {$startIdx + 80}]
  if {$limit >= $n} then { set limit [expr {$n - 1}] }
  for {set j $startIdx} {$j <= $limit} {incr j} {
    set t [string trim [lindex $lines $j]]
    if {$t eq ""} then { continue }
    if {[string index $t end] eq "\{"} then { return 1 }
    if {[string index $t end] eq ";"} then { return 0 }
  }
  return 0
}


###############################################################################
#
# header_end_index --
#
#     Walk upward from just above the return-type line and return the line
#     index of the header block's closing star-slash, or -1 if there is no
#     block comment there.  Skipped along the way: blank lines, single
#     forward-declaration / statement lines (ending in a semicolon), and
#     preprocessor structure.  A plain directive (#if / #define / #include)
#     is skipped; an #else / #elif / #endif means the definition sits in a
#     later branch of a conditional, so the ENTIRE preceding conditional is
#     skipped up to and including its opening #if -- this is what lets a
#     function whose body is #if/#else-split find the single header written
#     above the whole conditional.
#
###############################################################################

proc header_end_index {lines rettypeIdx} {
  set i [expr {$rettypeIdx - 1}]
  while {$i >= 0} {
    set t [string trim [lindex $lines $i]]
    if {$t eq ""} then { incr i -1; continue }
    if {[string index $t end] eq ";"} then { incr i -1; continue }
    if {[string index $t 0] eq "#"} then {
      if {[regexp {^#\s*(else|elif|endif)\M} $t]} then {
        # Definition is below an #else/#elif branch (or an #endif of a
        # nested conditional): rewind past the whole preceding conditional
        # to just above its opening #if.
        set depth 0
        if {[regexp {^#\s*endif\M} $t]} then { set depth 1 }
        incr i -1
        while {$i >= 0} {
          set u [string trim [lindex $lines $i]]
          if {[regexp {^#\s*endif\M} $u]} then { incr depth; incr i -1; continue }
          if {[regexp {^#\s*if} $u]} then {
            if {$depth == 0} then { incr i -1; break }
            incr depth -1
            incr i -1
            continue
          }
          incr i -1
        }
        continue
      }
      incr i -1
      continue
    }
    break
  }
  if {$i < 0} then { return -1 }
  if {[regexp {\*/\s*$} [lindex $lines $i]]} then { return $i }
  return -1
}


###############################################################################
#
# banner_names_function --
#
#     Return 1 if the header block preceding the definition (located by
#     header_end_index) contains a `* <name> --` banner line for exactly
#     this function, else 0.
#
###############################################################################

proc banner_marker_index {lines lineIdx name} {
  set i [header_end_index $lines $lineIdx]
  if {$i < 0} then { return -1 }
  # Walk up to the start of this comment block ("/*"), checking each line
  # for the banner naming this function.  An optional parenthetical
  # qualifier is allowed after the name -- the project's convention for a
  # platform / configuration variant (e.g. "Name (macOS) --").
  set re [format {^\s*\*\s+%s(\s*\([^)]*\))?\s+--} [string map {* {\*}} $name]]
  while {$i >= 0} {
    set line [lindex $lines $i]
    if {[regexp $re $line]} then {
      return $i
    }
    if {[regexp {/\*} $line]} then {
      return -1
    }
    incr i -1
  }
  return -1
}


###############################################################################
#
# banner_names_function --
#
#     Return 1 if the header block preceding the definition contains a
#     `* <name> --` banner line for exactly this function, else 0.
#
###############################################################################

proc banner_names_function {lines lineIdx name} {
  return [expr {[banner_marker_index $lines $lineIdx $name] >= 0}]
}


###############################################################################
#
# banner_has_content --
#
#     Given the line index of a `* <name> --` banner marker, return 1 if the
#     banner carries descriptive prose (any non-blank, non-separator comment
#     line before the block closes with `*/`), else 0.  A SKELETON banner --
#     just the marker line framed by rule separators, with no description,
#     Why/How, Results, or Side effects -- returns 0.  This is what turns the
#     gate from "a banner exists" into "a banner documents the function":
#     check_headers formerly accepted an empty `* Name --` marker (e.g.
#     Th8_RegisterPlugin), which passed presence but documented nothing.
#
###############################################################################

proc banner_has_content {lines markerIdx} {
  # (a) A one-line banner carries its description on the marker line itself,
  # after the "--" (e.g. "* foo -- does the thing.").  Accept that.
  if {[regexp -- {--\s*(\S.*)$} [lindex $lines $markerIdx] -> tail]} then {
    if {[string trim $tail] ne ""} then { return 1 }
  }
  # (b) Otherwise require a following comment line with real prose before the
  # block closes.
  set n [llength $lines]
  for {set j [expr {$markerIdx + 1}]} {$j < $n} {incr j} {
    set line [lindex $lines $j]
    if {[regexp {\*/} $line]} then { return 0 }
    # Strip a leading " *" comment prefix, then any surrounding whitespace.
    set body $line
    regsub {^\s*\*} $body "" body
    set body [string trim $body]
    if {$body eq ""} then { continue }
    # A rule separator (---- ... ----) is framing, not content.
    if {[regexp {^-+$} $body]} then { continue }
    return 1
  }
  return 0
}


###############################################################################
#
# banner_missing_sections --
#
#     Given the marker index of a `* <name> --` banner, return the list of
#     REQUIRED TH8 banner sections that are absent from the block.  The TH8
#     convention (Tcl/Tk-derived) is a description followed by, each on its own
#     `* Section:` line, "Why / How", "Results", and "Side effects".  The
#     description is handled by banner_has_content; this checks the three named
#     sections.  A one-line banner (`* foo -- does X.`) has none of them and so
#     returns all three.  Only consulted when section enforcement is enabled.
#
###############################################################################

proc banner_missing_sections {lines markerIdx} {
  set required [list "Why / How" "Results" "Side effects"]
  set seen [dict create]
  foreach r $required { dict set seen $r 0 }
  set n [llength $lines]
  for {set j [expr {$markerIdx + 1}]} {$j < $n} {incr j} {
    set line [lindex $lines $j]
    if {[regexp {\*/} $line]} then { break }
    set body $line
    regsub {^\s*\*\s?} $body "" body
    set body [string trimleft $body]
    foreach r $required {
      if {[string match "${r}:*" $body] || [string match "${r} :*" $body]} then {
        dict set seen $r 1
      }
    }
  }
  set missing [list]
  foreach r $required {
    if {![dict get $seen $r]} then { lappend missing $r }
  }
  return $missing
}


###############################################################################
#
# check_file --
#
#     Scan one .c file for function definitions and verify each has its
#     banner.  Appends "path:line: function 'name' ..." messages to the
#     global ::violations list.  Returns the number of functions checked.
#
###############################################################################

proc check_file {path} {
  set fh [open $path r]
  set data [read $fh]
  close $fh
  set lines [split $data \n]
  set mask [comment_start_mask $lines]
  set n [llength $lines]
  set checked 0
  for {set i 0} {$i < $n} {incr i} {
    if {[lindex $mask $i]} then { continue }
    set line [lindex $lines $i]
    # Function name at column 0 immediately followed by '('.
    if {![regexp {^([A-Za-z_]\w*)\(} $line -> name]} then { continue }
    if {[lsearch -exact $::NOT_FUNCTIONS $name] >= 0} then { continue }
    # The immediately preceding non-blank line must look like a return
    # type (this is what distinguishes a definition from a stray macro
    # call or continuation at column 0).
    set p [expr {$i - 1}]
    while {$p >= 0 && [string trim [lindex $lines $p]] eq ""} {
      incr p -1
    }
    if {$p < 0} then { continue }
    if {[lindex $mask $p]} then { continue }
    if {![looks_like_return_type [lindex $lines $p]]} then { continue }
    # Must be a DEFINITION (body follows), not a declaration (semicolon).
    if {![is_definition $lines $i]} then { continue }
    # It is a definition; require its banner AND that the banner has content.
    incr checked
    set markerIdx [banner_marker_index $lines $p $name]
    set problem ""
    if {$markerIdx < 0} then {
      set problem [format {has no '%s --' header banner} $name]
    } elseif {![banner_has_content $lines $markerIdx]} then {
      set problem [format \
          {has an EMPTY '%s --' header banner (no description/Why/Results)} \
          $name]
    } elseif {$::REQUIRE_SECTIONS} then {
      set missing [banner_missing_sections $lines $markerIdx]
      if {[llength $missing] == 0} then { continue }
      set problem [format {is missing required banner section(s): %s} \
          [join $missing {; }]]
    } else {
      continue
    }
    # Escape valve: a `CHECK-HEADERS-OK` marker on the return-type line or
    # the two lines above it waives a case the parser cannot see through
    # (documented, like audit_patterns.tcl's AUDIT-OK).
    set suppressed 0
    for {set s $p} {$s >= 0 && $s >= [expr {$p - 2}]} {incr s -1} {
      if {[string first "CHECK-HEADERS-OK" [lindex $lines $s]] >= 0} then {
        set suppressed 1
        break
      }
    }
    if {$suppressed} then { continue }
    lappend ::violations \
        [format {%s:%d: function '%s' %s} $path [expr {$i + 1}] $name $problem]
  }
  return $checked
}


###############################################################################
#
# main
#
###############################################################################

set ::violations [list]

# Section enforcement: require every banner to carry the standard TH8 sections
# (Why / How, Results, Side effects), not just a name + description.  The whole
# tree was brought to 0 violations on 2026-08-11, so this is now ON BY DEFAULT
# in the audit gate.  Set CHECK_HEADERS_NO_SECTIONS to fall back to the
# content-only check (escape valve for a future transition, not routine use).
set ::REQUIRE_SECTIONS [expr {![info exists ::env(CHECK_HEADERS_NO_SECTIONS)]}]

set fileArgs [list]
foreach a $argv {
  if {$a eq "--list"} then {
    foreach f [default_source_files] {
      if {![is_exempt $f]} then { puts $f }
    }
    exit 0
  }
  lappend fileArgs $a
}

if {[llength $fileArgs] > 0} then {
  set files $fileArgs
} else {
  set files [default_source_files]
}

set totalChecked 0
set totalFiles 0
foreach f $files {
  if {[is_exempt $f]} then { continue }
  if {![file exists $f]} then { continue }
  incr totalFiles
  incr totalChecked [check_file $f]
}

foreach v $::violations {
  puts $v
}

set nv [llength $::violations]
puts [format "check_headers: %d function(s) checked in %d file(s); %d violation(s)" \
    $totalChecked $totalFiles $nv]
if {$nv > 0} then {
  exit 1
}
exit 0
