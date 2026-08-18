#!/usr/bin/env tclsh
###############################################################################
#
# mkamal.tcl --
#
#     Amalgamation builder for TH8.  Combines all TH8 core source
#     files, internal headers, and vendored external dependencies
#     into a single "th8.c" file that can be compiled standalone
#     (alongside the public "th8.h" header).
#
#     Modeled after SQLite's mksqlite3c.tcl.
#
# Usage:
#     tclsh tools/mkamal.tcl ?-o OUTPUT? ?-line?
#
#     -o OUTPUT   Write to OUTPUT (default: th8.c)
#     -line       Emit #line directives for debugging
#
# The public header th8.h is NOT inlined.  The consumer compiles:
#     cc -I. th8.c -o libth8.a
# with th8.h in the same directory or on the include path.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

set outputFile "th8.c"
set emitLine 1

for {set i 0} {$i < [llength $argv]} {incr i} {
  switch -- [lindex $argv $i] {
    -o        { incr i; set outputFile [lindex $argv $i] }
    -line     { set emitLine 1 }
    -noline   { set emitLine 0 }
    default {
      puts stderr "Usage: tclsh tools/mkamal.tcl ?-o OUTPUT? ?-line? ?-noline?"
      exit 1
    }
  }
}

###############################################################################
# Configuration
###############################################################################

#
# Source root directories.
#
set S "src"
set E "externals"

#
# Headers that should be INLINED on first #include and then
# suppressed on subsequent includes.  The public header th8.h
# is intentionally excluded -- it stays as a normal #include.
#
# Key = header filename (as it appears in #include "..."),
# Value = path to the file.
#
set inlineHeaders {
  "th8_version_gen.h"      "bin/th8_version_gen.h"
  "th8_int.h"              "src/th8_int.h"
  "th8_int_core.h"         "src/th8_int_core.h"
  "th8_mem.h"              "src/th8_mem.h"
  "th8_plat.h"             "src/th8_plat.h"
  "th8_hash.h"             "src/th8_hash.h"
  "th8_util.h"             "src/th8_util.h"
  "th8_plugin.h"           "src/th8_plugin.h"
  "th8Decls.h"             "src/th8Decls.h"
  "th8_bigint.h"           "src/th8_bigint.h"
  "th8_meta_defs.h"        "src/th8_meta_defs.h"
  "th8_meta_libc.h"        "src/th8_meta_libc.h"
  "th8_meta_msvc.h"        "src/th8_meta_msvc.h"
  "th8_meta_posix.h"       "src/th8_meta_posix.h"
  "th8_meta_win32.h"       "src/th8_meta_win32.h"
  "th8_meta_macos.h"       "src/th8_meta_macos.h"
  "th8_meta_glibc.h"       "src/th8_meta_glibc.h"
  "ConvertUTF_v2.h"        "externals/utf/ConvertUTF_v2.h"
  "th8_spilornis.h"        "src/th8_spilornis.h"
  "Spilornis.h"            "bin/Spilornis.h"
  "SpilornisDef.h"         "bin/SpilornisDef.h"
  "SpilornisInt.h"         "bin/SpilornisInt.h"
  "pkgVersion.h"           "externals/spilornis/pkgVersion.h"
  "rcVersion.h"            "externals/spilornis/rcVersion.h"
  "tommath.h"              "externals/tommath/build/tommath.h"
  "regcustom_th8.h"        "src/plugins/regexp/regcustom_th8.h"
  "regex_th8.h"            "src/plugins/regexp/regex_th8.h"
  "regexport.h"            "externals/regex/build/regexport.h"
  "th8InternalDecls.h"     "src/th8InternalDecls.h"
  "th8_expr.h"             "src/th8_expr.h"
  "th8_shell.h"            "src/th8_shell.h"
  "th8_vars.h"             "src/th8_vars.h"
  "th8_unbound.h"          "src/th8_unbound.h"
}

#
# Headers that should NEVER be inlined (kept as #include).
#
set keepHeaders {
  "th8.h"
  "tcl.h"
  "bestline.h"
}

#
# System headers: deduplicated across all source files.
# The first occurrence is kept; subsequent are commented out.
#
# (Handled automatically by scanning for #include <...>)
#

#
# Source files to include in the amalgamation, in dependency
# order.  Each entry is a list: {path ?guard_begin? ?guard_end?}
#
# Optional guard_begin/guard_end wrap the file in preprocessor
# directives.  For vendored files, these set up and tear down
# macro redirections (e.g., Spilornis CRT macros, Spencer regex
# struct vars rename).
#

#
# Source files to include in the amalgamation, in dependency
# order.  Each entry is: {path ?guard_begin? ?guard_end?}
#
set sourceFiles [list \
    [list "src/th8_hash.c"           ""  ""] \
    [list "src/th8_util.c"           ""  ""] \
    [list "src/th8_base64.c"         ""  ""] \
    [list "src/th8_glob.c"           ""  ""] \
    [list "src/th8_math.c"           ""  ""] \
    [list "src/th8_cache.c"          ""  ""] \
    [list "src/th8_channel.c"        ""  ""] \
    [list "src/th8_core.c"           ""  ""] \
    [list "src/th8_expr.c"           ""  ""] \
    [list "src/th8_load.c"           ""  ""] \
    [list "src/th8_vars.c"           ""  ""] \
    [list "src/th8_plat.c"           ""  ""] \
    [list "src/th8_plugin.c"         ""  ""] \
    [list "src/plugins/th8_control.c"        ""  ""] \
    [list "src/plugins/th8_events.c"         ""  ""] \
    [list "src/plugins/th8_expressions.c"    ""  ""] \
    [list "src/plugins/th8_extensibility.c"  ""  ""] \
    [list "src/plugins/th8_filesystems.c"    ""  ""] \
    [list "src/plugins/th8_formatting.c"     ""  ""] \
    [list "src/plugins/th8_introspection.c"  ""  ""] \
    [list "src/plugins/th8_io.c"             ""  ""] \
    [list "src/plugins/th8_lists.c"          ""  ""] \
    [list "src/plugins/th8_looping.c"        ""  ""] \
    [list "src/plugins/th8_management.c"     ""  ""] \
    [list "src/plugins/th8_procedures.c"     ""  ""] \
    [list "src/plugins/th8_strings.c"        ""  ""] \
    [list "src/plugins/th8_timekeeping.c"    ""  ""] \
    [list "src/plugins/th8_variables.c"      ""  ""] \
    [list "src/plugins/th8_binary.c"         ""  ""] \
    [list "src/th8_lang.c"           ""  ""] \
    [list "src/th8_mem.c"            ""  ""] \
    [list "src/th8_memtrack.c"       ""  ""] \
    [list "src/th8_xlib.c"           ""  ""] \
    [list "src/th8_nullio.c"         ""  ""] \
    [list "src/th8_unwind.c"         ""  ""] \
    [list "src/th8_ctime.c"          ""  ""] \
    [list "src/th8StubInit.c"        ""  ""] \
    [list "src/th8InternalStubInit.c" ""  ""] \
    [list "externals/utf/ConvertUTF_v2.c"  ""  ""] \
    [list "src/th8_spilornis.c"      ""  ""] \
    [list "bin/Spilornis.c"          "" {
  /* Undo Spilornis se_* CRT and wchar macros */
  #undef se_calloc
  #undef se_free
  #undef se_memcpy
  #undef se_memset
  #undef se_memcmp
  #undef se_strlen
  #undef se_strncmp
  #undef se_strncpy
  #undef se_snprintf
  #undef se_vsnprintf
  #undef se_wcslen
  #undef se_wcsncmp
  #undef se_wmemcpy
  #undef se_wmemset
  #undef se_swprintf
  #undef se_vswprintf
  #undef se_wcsncpy
  #undef se_iswspace
  #undef se_iswdigit
  #undef se_iswxdigit
  #undef se_iswbdigit
  #undef se_iswodigit
  #undef EagleAllocateMemory
  #undef EagleFreeMemory
  #undef EagleMemorySize
  #undef MemorySizeWrapper
  #undef AllocateMemoryWrapper
  #undef FreeMemoryWrapper
  #undef EAGLE_TRACE_ENTRY
  #undef EAGLE_TRACE_EXIT
  #undef EAGLE_TRACE
  /* Undo simple macros that conflict with winnt.h guards */
  #undef VOID
  #undef TRUE
  #undef FALSE
  #undef CONST
  #undef EXTERN
  #undef EAGLE_EXTERN
}] \
    [list "src/plugins/regexp/th8_regex.c"   ""  ""] \
    [list "bin/regex_amalg.c"        ""  ""] \
    ]

#
# Conditionally include bigint files if they exist.
#
if {[file exists "src/th8_bigint.c"]} then {
  lappend sourceFiles [list "src/th8_bigint.c" "" ""]
}
if {[file exists "bin/tommath_amalg.c"]} then {
  lappend sourceFiles [list "bin/tommath_amalg.c" "" ""]
}

#
# Crypto and Harpy plugin files.
#
lappend sourceFiles \
    [list "src/plugins/crypto/th8_crypto_cmds.c"  ""  ""] \
    [list "src/plugins/crypto/th8_secure.c"       ""  ""] \
    [list "src/plugins/harpy/th8_attrflags.c"     ""  ""] \
    [list "src/plugins/harpy/th8_harpy.c"         ""  ""] \
    [list "src/plugins/harpy/th8_key0.c"          ""  ""] \
    [list "src/plugins/harpy/th8_keyRoot.c"       ""  ""] \
    [list "src/plugins/harpy/th8_keyTime.c"       ""  ""] \
    [list "src/plugins/harpy/th8_keyring_stub.c"  ""  ""] \
    [list "src/plugins/harpy/th8_policy.c"        ""  ""] \
    [list "src/plugins/harpy/th8_snk.c"           ""  ""] \
    [list "src/plugins/harpy/th8_time.c"          ""  ""] \
    [list "src/plugins/th8_harpy.c"               ""  ""]

#
# Platform and CRT bridge files.
#
lappend sourceFiles \
    [list "src/th8_curl.c"           ""  ""] \
    [list "src/th8_unbound.c"        ""  ""] \
    [list "src/th8_libc.c"           ""  ""] \
    [list "src/th8_posix.c"          ""  ""] \
    [list "src/th8_macos.c"          ""  ""] \
    [list "src/th8_ios.c"            ""  ""] \
    [list "src/th8_android.c"        ""  ""] \
    [list "src/th8_win32.c"          ""  ""] \
    [list "src/th8_cosmopolitan.c"   ""  ""] \
    [list "src/th8_fault.c"          ""  ""] \
    [list "src/th8_env.c"            ""  ""] \
    [list "src/th8_mimalloc.c"       ""  ""] \
    [list "src/th8_protect.c"        ""  ""]

#
# Test key files (conditionally compiled).
#
if {[file exists "src/plugins/harpy/th8_keyTest.c"]} then {
  lappend sourceFiles [list "src/plugins/harpy/th8_keyTest.c" "" ""]
}

###############################################################################
# State
###############################################################################

# Track which headers have been inlined.
array set inlinedHdrs {}

# Track which system headers have been emitted.
array set seenSysHdrs {}

# Build lookup table for inline headers.
array set inlineHdrPath {}
foreach {name path} $inlineHeaders {
  set inlineHdrPath($name) $path
}

# Build set of "keep" headers.
array set keepHdr {}
foreach name $keepHeaders {
  set keepHdr($name) 1
}

# Output accumulator.
set out ""

###############################################################################
# Procedures
###############################################################################

#
# emit --
#
#     Append a line to the output.
#
proc emit {line} {
  global out
  append out $line "\n"
}

#
# readFile --
#
#     Read a file and return its contents.
#
proc readFile {path} {
  set fd [open $path r]
  set data [read $fd]
  close $fd
  return $data
}

#
# inlineHeader --
#
#     Inline a header file, recursively processing its own
#     #include directives.
#
proc inlineHeader {name} {
  global inlinedHdrs inlineHdrPath emitLine

  if {[info exists inlinedHdrs($name)]} then { return }
  if {![info exists inlineHdrPath($name)]} then { return }

  set inlinedHdrs($name) 1
  set path $inlineHdrPath($name)

  if {![file exists $path]} then {
    puts stderr "WARNING: header not found: $path"
    return
  }

  emit "/************** Begin file [file tail $path] *************/"
  if {$emitLine} then {
    emit "#line 1 \"$path\""
  }
  processLines $path [readFile $path]
  emit "/************** End of [file tail $path] *************/"
}

#
# processLines --
#
#     Process the lines of a source file, handling #include
#     directives by inlining or deduplicating as appropriate.
#
proc processLines {path content} {
  global inlinedHdrs inlineHdrPath seenSysHdrs keepHdr emitLine

  set lineNum 0
  foreach line [split $content "\n"] {
    incr lineNum

    #
    # Check for #include "localheader.h"
    #
    if {[regexp {^#\s*include\s+"([^"]+)"} $line -> hdr]} then {
            set base [file tail $hdr]

            #
            # Keep headers: always pass through.
            #
            if {[info exists keepHdr($base)]} then {
                emit $line
                continue
            }

            #
            # Inline headers: inline on first encounter,
            # comment out on subsequent.
            #
            if {[info exists inlineHdrPath($base)]} then {
                if {![info exists inlinedHdrs($base)]} then {
                    inlineHeader $base
                } else {
                    emit "/* amalgamation: $base already included */"
                }

                #
                # Restore #line to the parent file after the
                # inlined header so subsequent lines are
                # attributed to the correct source location.
                #
                if {$emitLine} then {
                    emit "#line [expr {$lineNum + 1}] \"$path\""
                }
                continue
            }

            #
            # Regex engine internal includes: these are
            # #include "regc_*.c" style includes within
            # the Spencer regex engine.  Pass through.
            #
            emit $line
            continue
        }

        #
        # Check for #include <systemheader.h>
        #
        if {[regexp {^#\s*include\s+<([^>]+)>} $line -> hdr]} then {
            #
            # System headers are NOT deduplicated.  They have
            # their own include guards and may appear inside
            # conditional blocks that are inactive in one context
            # but active in another.  Letting the compiler see
            # all occurrences is safe and correct.
            #
            emit $line
            continue
        }

        #
        # All other lines: pass through.
        #
        emit $line
    }
}

#
# processSourceFile --
#
#     Process a single source file for inclusion in the
#     amalgamation.
#
proc processSourceFile {path guardBegin guardEnd} {
    global emitLine

    set tail [file tail $path]

    if {![file exists $path]} then {
        puts stderr "WARNING: source file not found: $path (skipped)"
        return
    }

    emit ""
    emit "/************** Begin file $tail **************/"

    if {$guardBegin ne ""} then {
        emit $guardBegin
    }

    if {$emitLine} then {
        emit "#line 1 \"$path\""
    }

    processLines $path [readFile $path]

    if {$guardEnd ne ""} then {
        emit $guardEnd
    }

    emit "/************** End of $tail **************/"
}

###############################################################################
# Main
###############################################################################

#
# Check prerequisites.
#
set missing {}
foreach entry $sourceFiles {
    set path [lindex $entry 0]
    if {![file exists $path]} then {
        lappend missing $path
    }
}
if {[llength $missing] > 0} then {
    puts stderr "Missing source files: $missing"
    foreach f $missing {
        puts stderr "  $f"
    }
    exit 1
}

#
# Emit the amalgamation header.
#
emit "/*"
emit "** th8.c -- TH8 amalgamation."
emit "**"
emit "** This file is generated by tools/mkamal.tcl.  DO NOT EDIT."
emit "**"
emit "** Compile with:"
emit "**   cc -DTH8_ENABLE_REGEXP th8.c -lm"
emit "**"
emit "** The public header th8.h must be alongside or on the include path."
emit "** All vendored dependencies are included in this file."
emit "**"
emit "** Optional defines:"
emit "**   -DTH8_ENABLE_REGEXP   Include regular expression support"
emit "**   -DTH8_ENABLE_LIBCURL  Include libcurl HTTP support"
emit "**   -DTH8_BUILD_DLL       Export symbols for shared library"
emit "**   -DTH8_DEBUG           Enable debug assertions"
emit "**   -DTH8_OMIT_AUXILIARY_SAFETY_CHECKS  Compile out defensive code"
emit "**"
emit "** See the file \"license.terms\" for information on usage and"
emit "** redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES."
emit "*/"
emit ""
emit "#define TH8_AMALGAMATION 1"
emit ""
emit "/*"
emit "** th8_plat.h MUST come before th8.h so that TH8_PLAT_H is defined"
emit "** and Th8_Mutex resolves to the concrete Th8_PlatformMutex type"
emit "** (not void)."
emit "*/"

#
# Inline th8_plat.h first (before th8.h).
#
inlineHeader "th8_plat.h"

emit ""
emit "#include \"th8.h\""
emit ""

#
# Pre-emit all remaining internal headers.  This ensures they appear
# at the top of the amalgamation OUTSIDE any conditional
# blocks (some source files include headers inside #ifdef
# guards that may be inactive in the amalgamation).
#
#
# Headers that should be inlined on demand (when #included from
# a source file) but NOT pre-emitted at the top.  These are
# external library headers whose include ordering is controlled
# by their own source files.
#
set noPreEmit {
    "Spilornis.h"
    "SpilornisDef.h"
    "SpilornisInt.h"
    "pkgVersion.h"
    "rcVersion.h"
    "regcustom_th8.h"
    "regex_th8.h"
    "regexport.h"
}

array set noPreEmitSet {}
foreach name $noPreEmit {
    set noPreEmitSet($name) 1
}

foreach {name path} $inlineHeaders {
    if {![info exists inlinedHdrs($name)]
	    && ![info exists noPreEmitSet($name)]} then {
	inlineHeader $name
    }
}

#
# Process all source files in order.
#
foreach entry $sourceFiles {
    set path [lindex $entry 0]
    set guardBegin [lindex $entry 1]
    set guardEnd [lindex $entry 2]
    processSourceFile $path $guardBegin $guardEnd
}

#
# Write the output.
#
set fd [open $outputFile w]
fconfigure $fd -translation binary
puts -nonewline $fd $out
close $fd

set nLines [llength [split $out "\n"]]
set nBytes [string length $out]
puts "Amalgamation written to: $outputFile"
puts "  Lines: $nLines"
puts "  Bytes: $nBytes"
puts "  Source files: [llength $sourceFiles]"
