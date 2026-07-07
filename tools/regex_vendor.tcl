#!/usr/bin/env tclsh
#
# regex_vendor.tcl -- Prepare the Spencer regex engine for TH8.
#
# This script copies the pristine PostgreSQL source files from
# externals/regex/vendor/ into externals/regex/build/, then
# applies TH8-specific patches.  The vendor/ directory is
# NEVER modified.
#
# Usage:
#   tclsh tools/regex_vendor.tcl          ;# prepare build files
#   tclsh tools/regex_vendor.tcl diff     ;# show what would change
#   tclsh tools/regex_vendor.tcl update   ;# re-download from PostgreSQL
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

set scriptDir [file dirname [file normalize [info script]]]
set topDir    [file dirname $scriptDir]
set vendorDir [file join $topDir externals regex vendor]
set buildDir  [file join $topDir externals regex build]
set patchDir  [file join $topDir externals regex patches]

# ---------------------------------------------------------------
# Utility: copy file
# ---------------------------------------------------------------

proc copyFile {src dst} {
  set fd [open $src r]
  fconfigure $fd -translation binary
  set data [read $fd]
  close $fd
  file mkdir [file dirname $dst]
  set fd [open $dst w]
  fconfigure $fd -translation binary
  puts -nonewline $fd $data
  close $fd
}

# ---------------------------------------------------------------
# Utility: apply text replacements to a file
# ---------------------------------------------------------------

proc patchFile {path replacements} {
  set fd [open $path r]
  fconfigure $fd -translation binary
  set data [read $fd]
  close $fd

  foreach {from to} $replacements {
    set data [string map [list $from $to] $data]
  }

  set fd [open $path w]
  fconfigure $fd -translation binary
  puts -nonewline $fd $data
  close $fd
}

# ---------------------------------------------------------------
# The list of vendor files to copy and patch.
# ---------------------------------------------------------------

set vendorFiles {
  regcomp.c
  regexec.c
  regfree.c
  regprefix.c
  regerror.c
  regexport.c
  regc_color.c
  regc_cvec.c
  regc_lex.c
  regc_locale.c
  regc_nfa.c
  regc_pg_locale.c
  rege_dfa.c
  regguts.h
  regerrs.h
  regexport.h
  COPYRIGHT
}

# ---------------------------------------------------------------
# Patches: { filename { from1 to1 from2 to2 ... } }
#
# These are the minimal changes needed to compile the Spencer
# engine under TH8 instead of PostgreSQL.
# ---------------------------------------------------------------

#
# Universal replacements applied to ALL files.
# These strip the "regex/" include prefix and map PostgreSQL
# headers to TH8 versions.
#

set universalReplacements {
  {#include "regex/regcustom.h"}  {#include "regcustom_th8.h"}
  {#include "regcustom.h"}        {#include "regcustom_th8.h"}
  {#include "regex/regguts.h"}    {#include "regguts.h"}
  {#include "regex/regerrs.h"}    {#include "regerrs.h"}
  {#include "regex/regex.h"}      {#include "regex_th8.h"}
  {#include "regex/regexport.h"}  {#include "regexport.h"}
}

#
# Per-file additional replacements.
#

#
# Per-file additional replacements.
#
# rege_dfa.c: Add periodic INTERRUPT checks in DFA hot loops.
# The Spencer engine only checks INTERRUPT on cache misses.
# Once the DFA cache is warm, the scanning loops run without
# any interrupt checks.  We add a counter-based check every
# TH8_DFA_CHECK_INTERVAL characters.
#

set patches {
  regcomp.c {
    {#include "regc_pg_locale.c"}
    {#include "regc_pg_locale_th8.c"}
  }
  rege_dfa.c {
    {css = ss;}
    {css = ss; if(((cp-v->start)&0xFFF)==0){INTERRUPT(v->re);}}
  }
  regexec.c {
    {#define VERR(vv,e)}
    {#undef VERR
      #undef NOERR
      #define VERR(vv,e)}
      {#include "rege_dfa.c"}
      {#include "rege_dfa.c"

        /* Forward declaration for -Wmissing-prototypes */
        extern void th8_regex_analyze(const regex_t *, int *, int *, int *, int *, int *, int *);

        /*
        * th8_regex_analyze -- TH8 extension.
        *
        *	Extract complexity-relevant metadata from a compiled regex.
        *	This function has access to the internal "struct guts" and
        *	"struct cnfa" that are opaque to callers outside the engine.
        *
        *	Outputs:
        *	  *pnStates     -- number of states in the search DFA
        *	  *pnSubre      -- number of subexpression tree nodes
        *	  *pnLacons     -- number of lookaround constraints
        *	  *pbMatchAll   -- 1 if the pattern is trivial MATCHALL
        *	  *pbBackref    -- 1 if the pattern uses back-references
        *	  *pbLookaround -- 1 if the pattern uses lookaround
        */
        void
        th8_regex_analyze(
        const regex_t *re,
        int *pnStates,
        int *pnSubre,
        int *pnLacons,
        int *pbMatchAll,
        int *pbBackref,
        int *pbLookaround)
        {
          struct guts *g = (struct guts *)re->re_guts;

          if (pnStates)     *pnStates     = g->search.nstates;
          if (pnSubre)      *pnSubre      = g->ntree;
          if (pnLacons)     *pnLacons     = g->nlacons > 1 ? g->nlacons - 1 : 0;
          if (pbMatchAll)   *pbMatchAll   = (g->search.flags & MATCHALL) ? 1 : 0;
          if (pbBackref)    *pbBackref    = (re->re_info & REG_UBACKREF) ? 1 : 0;
          if (pbLookaround) *pbLookaround = (re->re_info & REG_ULOOKAROUND) ? 1 : 0;
      }}
    }
  }

  # ---------------------------------------------------------------
  # Main: prepare the build directory
  # ---------------------------------------------------------------

  proc prepare {} {
    global vendorDir buildDir vendorFiles universalReplacements patches

    puts "Preparing Spencer regex engine for TH8..."
    puts "  Vendor: $vendorDir"
    puts "  Build:  $buildDir"

    # Clean build directory
    if {[file exists $buildDir]} then {
      file delete -force $buildDir
    }
    file mkdir $buildDir

    # Copy all vendor files
    foreach f $vendorFiles {
      set src [file join $vendorDir $f]
      set dst [file join $buildDir $f]
      if {![file exists $src]} then {
        puts "  WARNING: $f not found in vendor/"
        continue
      }
      copyFile $src $dst
      puts "  Copied: $f"
    }

    # Copy TH8-specific locale file into build/ so the
    # amalgamation can find it (regcomp.c #includes it).
    set localeSrc [file join [file dirname [info script]] \
        .. src plugins regexp regc_pg_locale_th8.c]
    if {[file exists $localeSrc]} then {
      copyFile $localeSrc [file join $buildDir regc_pg_locale_th8.c]
      puts "  Copied: regc_pg_locale_th8.c (TH8 locale)"
    }

    # Apply universal replacements to all source files
    foreach f [glob -nocomplain [file join $buildDir *.c] \
        [file join $buildDir *.h]] {
      patchFile $f $universalReplacements
    }
    puts "  Applied universal replacements"

    #
    # Add an include guard to regguts.h.  Without this, the
    # amalgamation build gets duplicate enum/typedef definitions
    # when multiple regex .c files include it.  The struct vars
    # rename (#define vars comp_vars) is handled separately by
    # regex_amalg.tcl.
    #
    set reggutsPath [file join $buildDir "regguts.h"]
    if {[file exists $reggutsPath]} then {
      set fd [open $reggutsPath r]
      set content [read $fd]
      close $fd
      set content "#ifndef REGGUTS_TH8_H\n#define REGGUTS_TH8_H\n${content}\n#endif /* REGGUTS_TH8_H */\n"
      set fd [open $reggutsPath w]
      puts -nonewline $fd $content
      close $fd
      puts "  Patched: regguts.h (added include guard)"
    }

    # Apply per-file patches
    foreach {filename replacements} $patches {
      set path [file join $buildDir $filename]
      if {![file exists $path]} then {
        continue
      }
      patchFile $path $replacements
      puts "  Patched: $filename"
    }

    puts "Done.  Build files are in $buildDir/"
    puts ""
    puts "To generate traditional patches for review:"
    puts "  cd [file dirname $vendorDir]"
    puts "  diff -ruN vendor/ build/ > patches/th8.patch"
  }

  # ---------------------------------------------------------------
  # Diff: show what the patches would change
  # ---------------------------------------------------------------

  proc showDiff {} {
    global vendorDir buildDir

    if {![file exists $buildDir]} then {
      puts "Build directory does not exist.  Run without"
      puts "arguments first to prepare it."
      return
    }

    # Use diff -ruN to show unified diffs
    set result [exec diff -ruN $vendorDir $buildDir]
    if {$result eq ""} then {
      puts "No differences."
    } else {
      puts $result
    }
  }

  # ---------------------------------------------------------------
  # Update: re-download vendor files from PostgreSQL
  # ---------------------------------------------------------------

  proc update {} {
    global vendorDir vendorFiles

    set baseUrl https://raw.githubusercontent.com/postgres/postgres/master
    puts "Updating vendor files from PostgreSQL..."

    foreach f $vendorFiles {
      # Determine the source path in the PostgreSQL tree
      if {[string match "*.h" $f]} then {
        set url "$baseUrl/src/include/regex/$f"
      } else {
        set url "$baseUrl/src/backend/regex/$f"
      }

      set dst [file join $vendorDir $f]
      puts "  Fetching: $f"

      # Use curl (available on all target platforms)
      if {[catch {exec curl -sL $url -o $dst} err]} then {
        puts "    ERROR: $err"
      } else {
        puts "    OK ([file size $dst] bytes)"
      }
    }
    puts "Done."
  }

  # ---------------------------------------------------------------
  # Entry point
  # ---------------------------------------------------------------

  if {[llength $argv] == 0} then {
    prepare
  } elseif {[lindex $argv 0] eq "diff"} then {
    showDiff
  } elseif {[lindex $argv 0] eq "update"} then {
    update
  } else {
    puts "Usage: tclsh vendor.tcl \[diff|update\]"
    exit 1
  }
