#!/usr/bin/env tclsh
###############################################################################
#
# reformat_tests.tcl --
#
#     One-off reformatter for TH8 test files.  Applies the unified
#     2-space-indent rule (§2.1 of the style guide) to every
#     `-setup`/`-body`/`-cleanup` argument body inside `runTest`
#     invocations, and wraps R-marker description lines that exceed
#     the 79-column limit.
#
#     The two transformations are SAFE:
#       * Halving the leading-whitespace count inside option-body
#         blocks does not change the Tcl code's semantics; bodies
#         are evaluated in a fresh scope and Tcl ignores leading
#         whitespace.
#       * Wrapping an R-marker description line into multiple lines
#         hashes identically because mkreq.tcl normalises whitespace
#         to single spaces before MD5 (verified by the round-trip
#         test in `--check-tests` / `--verify`).
#
#     The tool deliberately does NOT modify:
#       * `-result { ... }` -- the literal expected output.
#       * `-constraints { ... }` -- a literal token list.
#       * `-match` / `-returnCodes` / other non-script options.
#       * Lines outside `runTest` invocations.
#
# Usage:
#     tclsh tools/reformat_tests.tcl [OPTIONS] [FILE ...]
#
#     With no FILE arguments, walks the default tree (tests/**/*.tcl).
#     Positional FILEs override the default.
#
# Modes (default --check):
#     --check     Report files that would change.  Exit 1 on any.
#     --diff      Show unified diffs (no write).
#     --write     Apply transformations in place.
#
# Toggles:
#     --[no-]halve-indent      4-space -> 2-space inside option bodies.
#                              Default ON.  Idempotent: only fires when
#                              the minimum non-blank indent is a multiple
#                              of 4 and >= 4.
#     --[no-]wrap-rmarkers     Wrap R-marker description lines that
#                              exceed --column-limit.  Default ON.
#     --[no-]reindent          Re-indent every line inside script option
#                              bodies based on actual brace nesting
#                              depth.  Catches and fixes pre-existing
#                              alignment bugs (e.g. inner proc closing
#                              brace at wrong column).  Default ON.
#     --column-limit=N         Wrap threshold for R-marker descriptions
#                              (default 79).
#
# Other options:
#     --root=PATH              Project root for default discovery.
#     --quiet                  Suppress per-file progress.
#     --verbose                Emit each transformation that fires.
#     --no-color               Disable ANSI colour in diffs.
#
# Exit codes:
#     0   No changes needed (or --write succeeded everywhere).
#     1   Changes are needed (--check / --diff).
#     2   Tool error.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

package require Tcl 8.6


###############################################################################
#
# Argument parsing (mirrors tools/format_scripts.tcl).
#
###############################################################################

proc parse_args {argv} {
  array set opt {
    mode             check
    column_limit     79
    root             ""
    quiet            0
    verbose          0
    no_color         0
    do_halve_indent  1
    do_wrap_rmarkers 1
    do_reindent      1
    end_of_opts      0
  }
  set files {}
  for {set i 0} {$i < [llength $argv]} {incr i} {
    set a [lindex $argv $i]
    if {$opt(end_of_opts)} then {
      lappend files $a
      continue
    }
    switch -glob -- $a {
      --check  { set opt(mode) check }
      --diff   { set opt(mode) diff }
      --write  { set opt(mode) write }
      --quiet  { set opt(quiet) 1 }
      --verbose { set opt(verbose) 1 }
      --no-color { set opt(no_color) 1 }
      --       { set opt(end_of_opts) 1 }
      --halve-indent     { set opt(do_halve_indent) 1 }
      --no-halve-indent  { set opt(do_halve_indent) 0 }
      --wrap-rmarkers    { set opt(do_wrap_rmarkers) 1 }
      --no-wrap-rmarkers { set opt(do_wrap_rmarkers) 0 }
      --reindent         { set opt(do_reindent) 1 }
      --no-reindent      { set opt(do_reindent) 0 }
      --column-limit=*   { set opt(column_limit) [string range $a 15 end] }
      --root=*           { set opt(root) [string range $a 7 end] }
      --help - -h        { print_usage; exit 0 }
      --* {
        puts stderr "reformat_tests: unknown option: $a"
        exit 2
      }
      default { lappend files $a }
    }
  }
  array unset ::OPT
  array set ::OPT [array get opt]
  set ::FILE_ARGS $files
}


proc print_usage {} {
  set f [open [info script] r]
  set txt [read $f]; close $f
  foreach line [split $txt \n] {
    if {[regexp {^#\s?(.*)$} $line -> body]} then {
      if {[regexp {^!} $body]} continue
      puts $body
    } elseif {[string match "###*" $line]} then {
      continue
    } else {
      return
    }
  }
}


proc resolve_root {} {
  if {$::OPT(root) ne ""} then {
    return [file normalize $::OPT(root)]
  }
  set scriptDir [file dirname [file normalize [info script]]]
  return [file normalize [file join $scriptDir ..]]
}


proc relpath {target base} {
  set t [file split [file normalize $target]]
  set b [file split [file normalize $base]]
  set n [llength $b]
  if {[llength $t] >= $n
    && [lrange $t 0 [expr {$n - 1}]] eq $b} then {
    return [file join {*}[lrange $t $n end]]
  }
  return $target
}


proc discover_files {root} {
  if {[llength $::FILE_ARGS] > 0} then {
    set out {}
    foreach f $::FILE_ARGS {
      lappend out [file normalize $f]
    }
    return $out
  }
  set out {}
  set walk [list [file join $root tests]]
  while {[llength $walk] > 0} {
    set d [lindex $walk 0]
    set walk [lrange $walk 1 end]
    if {![file isdirectory $d]} then { continue }
    foreach f [glob -nocomplain -directory $d *.tcl] {
      lappend out [file normalize $f]
    }
    foreach sub [glob -nocomplain -directory $d -types d *] {
      lappend walk $sub
    }
  }
  return [lsort -unique $out]
}


###############################################################################
#
# read_file_text / write_file_text --
#
###############################################################################

proc read_file_text {fn} {
  set f [open $fn rb]
  set raw [read $f]
  close $f
  set finalNewline [string match "*\n" $raw]
  set lines [split $raw "\n"]
  if {$finalNewline && [lindex $lines end] eq ""} then {
    set lines [lrange $lines 0 end-1]
  }
  return [list $lines $finalNewline $raw]
}


proc write_file_text {fn lines finalNewline} {
  set buf [join $lines "\n"]
  if {$finalNewline} then { append buf "\n" }
  set tmp "$fn.reformat-tmp.[pid]"
  set f [open $tmp wb]
  puts -nonewline $f $buf
  close $f
  file rename -force -- $tmp $fn
}


###############################################################################
#
# Helpers --
#
###############################################################################

# Brace identifiers: we never store literal open/close-brace in regexes.
set ::BR_OPEN  [format %c 123]
set ::BR_CLOSE [format %c 125]


# match_runtest_open --
#
#     Return 1 iff LINE is the opening line of a runTest \{test ...
#     invocation that may extend across multiple lines.  Used to enter
#     the per-test scanning state.
proc match_runtest_open {line} {
  set re [format {^\s*runTest\s+%ctest\s+\S+\s+%c} 123 123]
  return [regexp -- $re $line]
}


# match_option_open --
#
#     If LINE looks like \} -OPT \{ (closing the previous block and
#     opening a new option block), return the OPT name.  Returns
#     empty string when no match.
proc match_option_open {line} {
  set re [format {^\s*%c\s*-(\w+)\s*%c\s*$} 125 123]
  if {[regexp -- $re $line -> opt]} then {
    return $opt
  }
  return ""
}


# match_option_terminator --
#
#     Return 1 iff LINE looks like the close-brace that ends a
#     -setup / -body / -cleanup option block.  The option-block
#     terminator is one of:
#
#         } -OPT {       (transition to next option block)
#         } -OPT VALUE   (option that takes a non-script literal)
#         } -OPT \       (continuation; option value continues)
#         }}             (end of runTest, with prior option's brace)
#         }}}            (end of runTest plus -result's closing brace)
#
#     A bare `}` deeper-than-level-zero is the inner code's brace,
#     NOT a terminator.  We require the close-brace to be at column
#     0 to avoid false positives from inner code structures.
proc match_option_terminator {line} {
  set re [format {^%c\s*(-\S+|%c|$)} 125 125]
  return [regexp -- $re $line]
}


# is_script_option --
#
#     Return 1 iff OPTNAME is an option whose argument is Tcl script
#     code (whose indent we may safely reformat).
proc is_script_option {optname} {
  return [expr {$optname in {setup body cleanup}}]
}


###############################################################################
#
# tx_halve_indent --
#
#     Walk the file lines.  When inside the script-argument body of a
#     `-setup` / `-body` / `-cleanup` option, halve the leading-space
#     count of each line (4 -> 2, 8 -> 4, 12 -> 6, etc.).
#
#     The state machine recognises:
#       OUTSIDE   - not currently in any runTest invocation.
#       IN_TEST   - inside a runTest invocation but not in a
#                   script-argument body.
#       IN_BODY   - inside the script-argument body of a script
#                   option (-setup / -body / -cleanup).
#
###############################################################################

proc tx_halve_indent {lines} {
  #
  # Two-pass: collect per-body line buffers, then halve only when
  # the body's minimum non-blank indent is a multiple of 4 and >= 4.
  # That makes the transform idempotent: a body already at the
  # 2-space rhythm is left alone, and a body still at the 4-space
  # rhythm gets converted to 2.
  #
  set out [list]
  set state OUTSIDE
  set changed 0
  set bodyBuf [list]

  foreach line $lines {
    switch -- $state {
      OUTSIDE {
        lappend out $line
        if {[match_runtest_open $line]} then {
          set state IN_TEST
        }
      }
      IN_TEST {
        set opt [match_option_open $line]
        if {$opt ne "" && [is_script_option $opt]} then {
          lappend out $line
          set state IN_BODY
          set bodyBuf [list]
          continue
        }
        lappend out $line
        if {[regexp -- {\}\s*\}\s*$} $line]} then {
          set state OUTSIDE
        }
      }
      IN_BODY {
        if {[match_option_terminator $line]} then {
          #
          # Body ends -- flush the buffer (with halving if
          # appropriate), emit the terminator, transition.
          #
          set flushed [maybe_halve_body $bodyBuf bodyChanged]
          foreach l $flushed { lappend out $l }
          if {$bodyChanged} then { set changed 1 }
          set bodyBuf [list]
          set state IN_TEST
          set opt [match_option_open $line]
          if {$opt ne "" && [is_script_option $opt]} then {
            lappend out $line
            set state IN_BODY
            continue
          }
          lappend out $line
          if {[regexp -- {\}\s*\}\s*$} $line]} then {
            set state OUTSIDE
          }
          continue
        }
        lappend bodyBuf $line
      }
    }
  }

  #
  # If the file ends inside a body (malformed input) flush what we
  # have so we do not lose lines.
  #
  if {[llength $bodyBuf] > 0} then {
    set flushed [maybe_halve_body $bodyBuf bodyChanged]
    foreach l $flushed { lappend out $l }
    if {$bodyChanged} then { set changed 1 }
  }
  return [list $out $changed]
}


# scan_line_state --
#
#     Walk a line of source and return the {netBraces stringExitState}
#     pair, where:
#       * netBraces -- opens minus closes outside double-quoted
#         strings, with backslash escapes honoured.
#       * stringExitState -- 1 if the line ends with an unclosed
#         double-quoted string (state carries to next line), else 0.
#
#     STARTSTATE is 1 if the line begins inside an unclosed string
#     (because a previous line opened one); 0 otherwise.
#
#     Comment lines (those whose first non-whitespace character is
#     `#` AND the line did not start inside a string) contribute 0
#     net braces and 0 string-exit.
proc scan_line_state {line startState} {
  set inStr $startState
  set escape 0
  set net 0
  set len [string length $line]

  if {!$inStr && [regexp -- {^\s*#} $line]} then {
    return [list 0 0]
  }

  for {set i 0} {$i < $len} {incr i} {
    set ch [string index $line $i]
    if {$escape} then { set escape 0; continue }
    if {$ch eq "\\"} then { set escape 1; continue }
    if {$ch eq {"}} then { set inStr [expr {!$inStr}]; continue }
        if {$inStr} then { continue }
        if {$ch eq [format %c 123]} then { incr net }
        if {$ch eq [format %c 125]} then { incr net -1 }
    }
    return [list $net $inStr]
}


# brace_net_outside_strings --
#
#     Backwards-compatible single-line brace count.  Equivalent to
#     calling scan_line_state with startState=0 and discarding the
#     stringExitState component.
proc brace_net_outside_strings {line} {
    lassign [scan_line_state $line 0] net _exit
    return $net
}


# leading_closes --
#
#     Return the number of consecutive close-braces at the start of
#     LINE (ignoring leading whitespace and whitespace between
#     consecutive close-braces).  Stops at the first non-whitespace,
#     non-close-brace character.
proc leading_closes {line} {
    set stripped [string trimleft $line]
    set count 0
    set len [string length $stripped]
    set i 0
    while {$i < $len} {
        set ch [string index $stripped $i]
        if {$ch eq [format %c 125]} then { incr count; incr i; continue }
        if {$ch eq " " || $ch eq "\t"} then { incr i; continue }
        break
    }
    return $count
}


# is_continuation_line --
#
#     Return 1 iff LINE ends with a backslash that would be parsed by
#     Tcl as a line continuation (a single trailing `\` after any
#     final whitespace).
proc is_continuation_line {line} {
    return [regexp -- {\\$} $line]
}


# reindent_body_lines --
#
#     Re-indent the BODY (a list of lines from a single -setup /
#     -body / -cleanup option block) so that:
#       * Lines start at indent = depth * 2 spaces.
#       * Depth is computed by walking braces outside strings.
#       * Lines that are continuations of a previous line keep their
#         existing relative indent (we add +4 to whatever depth-based
#         indent the first line of the statement was placed at).
#       * Blank lines stay blank.
#
#     Sets CHANGED_VAR (in the caller's frame) to 1 iff anything
#     changed.
proc reindent_body_lines {body changedVar} {
    upvar 1 $changedVar changed
    set out [list]
    set depth 0
    set inContinuation 0
    set contIndent 0

    foreach line $body {
        # Blank line: emit as-is.
        if {[regexp -- {^\s*$} $line]} then {
            lappend out ""
            continue
        }

        # Strip the existing indent.
        regexp -- {^\s*(.*)$} $line -> rest
        # Trim trailing whitespace (preserve only the natural content
        # up to the last non-blank character).
        set rest [string trimright $rest]

        if {$inContinuation} then {
            # Continuation lines indent +4 from the start-of-statement.
            set newIndent [expr {$contIndent + 4}]
            lappend out "[string repeat { } $newIndent]$rest"
            if {![is_continuation_line $line]} then {
                set inContinuation 0
            }
            continue
        }

        # First line of a statement: indent based on current depth
        # minus any leading close-braces this line carries.
        set leading [leading_closes $rest]
        set effectiveDepth [expr {$depth - $leading}]
        if {$effectiveDepth < 0} then { set effectiveDepth 0 }
        set newIndent [expr {$effectiveDepth * 2}]
        lappend out "[string repeat { } $newIndent]$rest"

        # If the line ends with `\`, mark continuation for next.
        if {[is_continuation_line $line]} then {
            set inContinuation 1
            set contIndent $newIndent
        }

        # Update depth based on net brace count of the line.
        set net [brace_net_outside_strings $rest]
        incr depth $net
        if {$depth < 0} then { set depth 0 }
    }

    # Compare against original BODY to detect changes.
    if {[llength $out] != [llength $body]} then {
        set changed 1
    } else {
        set changed 0
        for {set i 0} {$i < [llength $out]} {incr i} {
            if {[lindex $out $i] ne [lindex $body $i]} then {
                set changed 1
                break
            }
        }
    }
    return $out
}


###############################################################################
#
# tx_reindent_bodies --
#
#     Walk the file and re-indent the contents of every script
#     option-body (-setup / -body / -cleanup) using brace-balanced
#     depth tracking.  This catches and fixes pre-existing alignment
#     bugs such as a closing `}` placed at the wrong column relative
#     to its opening `{`.
#
#     Each line in a body is set to indent = 2 + (depth * 2), where
#     depth starts at 0 at the body's first level and is updated by
#     walking unescaped braces outside of double-quoted strings.
#     Continuation lines (those following a `\`-terminated line) are
#     indented +4 from the start-of-statement indent.
#
###############################################################################

proc tx_reindent_bodies {lines} {
    set out [list]
    set state OUTSIDE
    set changed 0
    set bodyBuf [list]

    foreach line $lines {
        switch -- $state {
            OUTSIDE {
                lappend out $line
                if {[match_runtest_open $line]} then {
                    set state IN_TEST
                }
            }
            IN_TEST {
                set opt [match_option_open $line]
                if {$opt ne "" && [is_script_option $opt]} then {
                    lappend out $line
                    set state IN_BODY
                    set bodyBuf [list]
                    continue
                }
                lappend out $line
                if {[regexp -- {\}\s*\}\s*$} $line]} then {
                    set state OUTSIDE
                }
            }
            IN_BODY {
                if {[match_option_terminator $line]} then {
                    set flushed [reindent_full_body $bodyBuf bChanged]
                    foreach l $flushed { lappend out $l }
                    if {$bChanged} then { set changed 1 }
                    set bodyBuf [list]
                    set state IN_TEST
                    set opt [match_option_open $line]
                    if {$opt ne "" && [is_script_option $opt]} then {
                        lappend out $line
                        set state IN_BODY
                        continue
                    }
                    lappend out $line
                    if {[regexp -- {\}\s*\}\s*$} $line]} then {
                        set state OUTSIDE
                    }
                    continue
                }
                lappend bodyBuf $line
            }
        }
    }
    if {[llength $bodyBuf] > 0} then {
        set flushed [reindent_full_body $bodyBuf bChanged]
        foreach l $flushed { lappend out $l }
        if {$bChanged} then { set changed 1 }
    }
    return [list $out $changed]
}


# reindent_full_body --
#
#     Wrapper around reindent_body_lines that uses base_indent=2
#     (the standard column for the first content level inside a
#     -setup / -body / -cleanup option body).
proc reindent_full_body {body changedVar} {
    upvar 1 $changedVar changed
    set out [list]
    set depth 0
    set inContinuation 0
    set contIndent 0
    set inMultilineString 0
    set baseIndent 2

    foreach line $body {
        # If we are currently inside a multi-line string literal,
        # the contents of this line are STRING DATA, not code.
        # Emit verbatim and check whether this line closes the
        # string.
        if {$inMultilineString} then {
            lappend out $line
            lassign [scan_line_state $line 1] net inStrExit
            set inMultilineString $inStrExit
            # Also update brace depth (a string can contain unescaped
            # braces but we never count them, so this is a no-op).
            continue
        }

        if {[regexp -- {^\s*$} $line]} then {
            lappend out ""
            continue
        }
        regexp -- {^\s*(.*)$} $line -> rest
        set rest [string trimright $rest]

        if {$inContinuation} then {
            set newIndent [expr {$contIndent + 4}]
            lappend out "[string repeat { } $newIndent]$rest"
            if {![is_continuation_line $line]} then {
                set inContinuation 0
            }
            lassign [scan_line_state $rest 0] net inStrExit
            incr depth $net
            if {$depth < 0} then { set depth 0 }
            set inMultilineString $inStrExit
            continue
        }

        set leading [leading_closes $rest]
        set effectiveDepth [expr {$depth - $leading}]
        if {$effectiveDepth < 0} then { set effectiveDepth 0 }
        set newIndent [expr {$baseIndent + $effectiveDepth * 2}]
        lappend out "[string repeat { } $newIndent]$rest"

        if {[is_continuation_line $line]} then {
            set inContinuation 1
            set contIndent $newIndent
        }

        lassign [scan_line_state $rest 0] net inStrExit
        incr depth $net
        if {$depth < 0} then { set depth 0 }
        set inMultilineString $inStrExit
    }

    set changed 0
    if {[llength $out] != [llength $body]} then {
        set changed 1
    } else {
        for {set i 0} {$i < [llength $out]} {incr i} {
            if {[lindex $out $i] ne [lindex $body $i]} then {
                set changed 1
                break
            }
        }
    }
    return $out
}


# maybe_halve_body --
#
#     Decide whether to halve the leading indent of every line in
#     BODY based on the minimum non-blank indent.  Halving fires only
#     when the minimum is a multiple of 4 and at least 4.  Sets
#     CHANGED_VAR (in the caller's frame) to 1 iff anything changed.
proc maybe_halve_body {body changedVar} {
    upvar 1 $changedVar changed
    set changed 0
    set minIndent -1
    foreach line $body {
        if {[regexp -- {^[ ]*$} $line]} continue
        if {[regexp -- {^( *)} $line -> ws]} then {
            set n [string length $ws]
            if {$minIndent < 0 || $n < $minIndent} then {
                set minIndent $n
            }
        }
    }
    if {$minIndent < 4 || ($minIndent % 4) != 0} then {
        return $body
    }
    set out [list]
    foreach line $body {
        if {[regexp -- {^( +)(.*)$} $line -> ws rest]} then {
            set n [string length $ws]
            set newN [expr {$n / 2}]
            set newLine "[string repeat { } $newN]$rest"
            if {$newLine ne $line} then { set changed 1 }
            lappend out $newLine
        } else {
            lappend out $line
        }
    }
    return $out
}


###############################################################################
#
# tx_wrap_rmarkers --
#
#     Find R-marker description blocks inside a runTest \{test NAME
#     \{ ... \} invocation.  When the description's first content
#     line exceeds the column limit, wrap it onto continuation lines
#     aligned to the R-marker column plus the prefix length.
#
#     The R-marker block has the canonical shape (open/close braces
#     escaped to keep the Tcl parser from counting them):
#
#         runTest \{test NAME \{
#         <indent>R-NNNNN-NNNNN: text...
#         \} -setup ...
#
#     This transform rewrites the single content line as multiple
#     lines, each ending at or before --column-limit columns, breaking
#     at word boundaries.
#
###############################################################################

proc tx_wrap_rmarkers {lines limit} {
    set out [list]
    set changed 0
    set n [llength $lines]
    set i 0
    set state OUTSIDE

    while {$i < $n} {
        set line [lindex $lines $i]
        switch -- $state {
            OUTSIDE {
                lappend out $line
                if {[match_runtest_open $line]} then {
                    set state IN_DESC
                }
                incr i
            }
            IN_DESC {
                # First non-blank line inside description block --
                # check if it is the R-marker line.
                set re [format \
                    {^(\s*)(R-\d{5}-\d{5}):\s*(.*)$} 0]
                if {[regexp -- {^(\s*)(R-\d{5}-\d{5}):\s*(.*)$} \
                        $line -> indent rid descText]} then {
                    # Combine subsequent description-only lines
                    # (already-wrapped continuations) so we always
                    # rewrap from the canonical text.
                    set j [expr {$i + 1}]
                    while {$j < $n} {
                        set next [lindex $lines $j]
                        if {[regexp -- {^\s*\}} $next]} then { break }
                        # A continuation line is whitespace+text.
                        if {[regexp -- {^(\s*)(.+)$} $next \
                                -> _ contPart]} then {
                            append descText " " $contPart
                            incr j
                            continue
                        }
                        # Blank line in description -- preserve.
                        break
                    }
                    set descText [string trim $descText]
                    regsub -all -- {\s+} $descText { } descText

                    set wrapped [wrap_rmarker_text \
                            $indent $rid $descText $limit]
                    set originalBlock {}
                    for {set k $i} {$k < $j} {incr k} {
                        lappend originalBlock [lindex $lines $k]
                    }
                    if {$wrapped ne $originalBlock} then { set changed 1 }
                    foreach w $wrapped { lappend out $w }
                    set i $j
                    set state SCAN_TEST_END
                    continue
                }
                lappend out $line
                incr i
                if {[regexp -- {^\s*\}\s*-} $line]} then {
                    set state SCAN_TEST_END
                }
            }
            SCAN_TEST_END {
                lappend out $line
                if {[regexp -- {\}\s*\}\s*$} $line]} then {
                    set state OUTSIDE
                }
                incr i
            }
        }
    }
    return [list $out $changed]
}


###############################################################################
#
# wrap_rmarker_text --
#
#     Render an R-marker description as one or more lines.  The first
#     line carries the R-marker prefix; continuation lines are aligned
#     to the column immediately past the colon and a single space, so
#     the text reads as a clean hanging indent:
#
#         <indent>R-NNNNN-NNNNN: first line of text up to the limit
#         <indent>               continuation aligned past the colon
#
#     Lines never exceed LIMIT columns.  Word boundaries are
#     preserved (no breaks mid-word).
#
###############################################################################

proc wrap_rmarker_text {indent rid descText limit} {
    set prefix "${indent}${rid}: "
    set prefixLen [string length $prefix]
    set contIndent [string repeat " " $prefixLen]
    set widthFirst [expr {$limit - $prefixLen}]
    set widthCont  [expr {$limit - $prefixLen}]

    if {[string length "${prefix}${descText}"] <= $limit} then {
        return [list "${prefix}${descText}"]
    }

    set words [split $descText " "]
    set lines [list]
    set cur ""
    foreach w $words {
        if {$cur eq ""} then {
            set cur $w
            continue
        }
        set candidate "$cur $w"
        set width [expr {[llength $lines] == 0 ? $widthFirst : $widthCont}]
        if {[string length $candidate] <= $width} then {
            set cur $candidate
        } else {
            lappend lines $cur
            set cur $w
        }
    }
    if {$cur ne ""} then { lappend lines $cur }

    set out [list]
    set first 1
    foreach l $lines {
        if {$first} then {
            lappend out "${prefix}${l}"
            set first 0
        } else {
            lappend out "${contIndent}${l}"
        }
    }
    return $out
}


###############################################################################
#
# format_one_file --
#
###############################################################################

proc format_one_file {fn} {
    lassign [read_file_text $fn] lines finalNewline _raw

    set anyChange 0
    if {$::OPT(do_wrap_rmarkers)} then {
        lassign [tx_wrap_rmarkers $lines $::OPT(column_limit)] \
                lines changed
        if {$changed} then {
            if {$::OPT(verbose)} then { puts stderr "  wrap_rmarkers fired" }
            set anyChange 1
        }
    }
    if {$::OPT(do_halve_indent)} then {
        lassign [tx_halve_indent $lines] lines changed
        if {$changed} then {
            if {$::OPT(verbose)} then { puts stderr "  halve_indent fired" }
            set anyChange 1
        }
    }
    if {$::OPT(do_reindent)} then {
        lassign [tx_reindent_bodies $lines] lines changed
        if {$changed} then {
            if {$::OPT(verbose)} then { puts stderr "  reindent fired" }
            set anyChange 1
        }
    }
    return [list $lines $finalNewline $anyChange]
}


proc render {lines finalNewline} {
    set buf [join $lines "\n"]
    if {$finalNewline} then { append buf "\n" }
    return $buf
}


proc tty_supports_color {} {
    if {[info exists ::env(NO_COLOR)]} then { return 0 }
    if {![info exists ::env(TERM)] || $::env(TERM) eq "dumb"} then { return 0 }
    return 1
}


proc colorize_diff {text} {
    set out {}
    foreach line [split $text \n] {
        switch -glob -- $line {
            "+++*" - "---*" { lappend out "\x1b\[1m$line\x1b\[0m" }
            "@@*"           { lappend out "\x1b\[36m$line\x1b\[0m" }
            "+*"            { lappend out "\x1b\[32m$line\x1b\[0m" }
            "-*"            { lappend out "\x1b\[31m$line\x1b\[0m" }
            default         { lappend out $line }
        }
    }
    return [join $out \n]
}


proc show_diff {fn original updated} {
    set tmpA "$fn.reformat-orig.[pid]"
    set tmpB "$fn.reformat-new.[pid]"
    set fa [open $tmpA wb]; puts -nonewline $fa $original; close $fa
    set fb [open $tmpB wb]; puts -nonewline $fb $updated;  close $fb
    set diffCmd [auto_execok diff]
    if {$diffCmd eq ""} then {
        puts "$fn: would change"
        file delete -- $tmpA $tmpB
        return
    }
    set rel [relpath $fn [resolve_root]]
    catch {exec [lindex $diffCmd 0] -u \
            -L "a/$rel" -L "b/$rel" \
            $tmpA $tmpB} diffOut
    file delete -- $tmpA $tmpB
    set lines [split $diffOut \n]
    if {[lindex $lines end] eq "child process exited abnormally"} then {
        set diffOut [join [lrange $lines 0 end-1] \n]
    }
    if {$::OPT(no_color) || ![tty_supports_color]} then {
        puts $diffOut
    } else {
        puts [colorize_diff $diffOut]
    }
}


proc main {argv} {
    parse_args $argv
    set root [resolve_root]
    set files [discover_files $root]
    if {[llength $files] == 0} then {
        puts stderr "reformat_tests: no files matched"
        exit 0
    }
    if {!$::OPT(quiet)} then {
        puts stderr "reformat_tests: processing [llength $files] file(s)"
    }
    set okCount 0
    set changedCount 0
    set errorCount 0

    foreach fn $files {
        if {![file readable $fn]} then {
            puts stderr "reformat_tests: missing or unreadable: $fn"
            incr errorCount
            continue
        }
        set rel [relpath $fn $root]

        if {[catch {format_one_file $fn} ret]} then {
            puts stderr "reformat_tests: failure on $fn: $ret"
            incr errorCount
            continue
        }
        lassign $ret newLines newFinal anyChange
        if {!$anyChange} then {
            incr okCount
            if {$::OPT(verbose)} then {
                puts stderr "ok: $rel"
            }
            continue
        }
        switch -- $::OPT(mode) {
            check {
                incr changedCount
                if {!$::OPT(quiet)} then { puts "needs format: $rel" }
            }
            diff {
                incr changedCount
                lassign [read_file_text $fn] _l _f originalRaw
                set updatedRaw [render $newLines $newFinal]
                show_diff $fn $originalRaw $updatedRaw
            }
            write {
                if {[catch {
                    write_file_text $fn $newLines $newFinal
                } err]} then {
                    puts stderr "reformat_tests: write failed $rel: $err"
                    incr errorCount
                } else {
                    incr okCount
                    if {!$::OPT(quiet)} then { puts "wrote: $rel" }
                }
            }
        }
    }
    if {!$::OPT(quiet)} then {
        puts stderr "reformat_tests: ok=$okCount changed=$changedCount\
        error=$errorCount"
    }
    if {$errorCount > 0} then { exit 2 }
    if {$::OPT(mode) eq "write"} then { exit 0 }
    if {$changedCount > 0} then { exit 1 }
    exit 0
}


main $argv
