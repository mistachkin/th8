#!/usr/bin/env tclsh
###############################################################################
#
# format_scripts.tcl --
#
#     Mechanical reformatter for Tcl / Eagle scripts.  Applies a
#     curated set of transformations that take source code closer
#     to the project style codified in
#     `docs/public/tcl_eagle_th8_style_guide.md` -- WITHOUT performing the
#     wholesale code-block reflow that no current Tcl tool can do
#     reliably.
#
#     The transformations here are SAFE: each leaves the script
#     semantically unchanged (it changes only whitespace, comment
#     placement, or adds a redundant `then` keyword that the Tcl
#     interpreter already accepted in its absence).  The tool will
#     not reorganise braces, will not split or join statements,
#     and will not touch comments other than to insert / remove
#     a form-feed page break between procs.
#
# Usage:
#     tclsh tools/format_scripts.tcl [OPTIONS] [FILE ...]
#
#     With no FILE arguments, walks the default tree (lib/**, tests/**
#     under the project root).  Positional FILEs override the default.
#
# Modes (mutually exclusive; default is --check):
#     --check     Report files that would change; exit 1 if any.
#     --diff      Print unified diff of proposed changes.
#     --write     Apply transformations in place.
#
# Transformation flags (each defaults ON unless noted):
#     --[no-]tabs-to-spaces    Convert leading tabs to spaces by
#                              expanding each tab to its visual
#                              column position at --tab-width (default
#                              8).  This preserves alignment in
#                              continuation lines.  DEFAULT OFF --
#                              opt-in because tabs in legacy files
#                              are often deliberate continuation
#                              alignment, and a mass conversion can
#                              break visual structure.
#     --[no-]trim-trailing     Strip trailing whitespace on every line.
#     --[no-]final-newline     Ensure file ends with exactly one newline.
#     --[no-]form-feed         Insert a `\x0C` page-break between
#                              adjacent procs inside `namespace eval
#                              { ... }` blocks (off if FF already there).
#     --[no-]then-keyword      Add the `then` keyword to `if`/`elseif`
#                              clauses that lack it.
#     --[no-]collapse-blanks   Collapse 3+ consecutive blank lines to 1.
#                              (default OFF -- some files use multi-blank
#                              spacing intentionally.)
#
# Other options:
#     --indent=N               Reserved for future use (default 2).
#     --tab-width=N            Visual column width of one tab character
#                              when expanding (default 8 -- the POSIX
#                              standard).
#     --line-endings={lf|crlf|preserve}
#                              How to write the output (default preserve;
#                              keeps the file's existing convention).
#     --binary=PATH            Reserved for future use (no external
#                              formatter is invoked today).
#     --root=PATH              Project root for default file discovery.
#     --include=GLOB           Add a glob to the include set (replaces
#                              the defaults if any --include is given).
#     --exclude=GLOB           Add a glob to the exclude set (augments
#                              defaults).
#     --files=PATH             Read newline-separated paths from PATH;
#                              `-` reads stdin.
#     --quiet                  Suppress per-file progress.
#     --verbose                Print each transformation that fires.
#     --no-color               Disable ANSI colour in diffs.
#     --                       End of options; rest are FILEs.
#
# Exit codes:
#     0   No changes needed (or --write succeeded everywhere).
#     1   Changes are needed (--check / --diff) OR a write failed.
#     2   Tool error (bad option, unreadable file, etc.).
#
# Limitations (intentional):
#     The tool does not reformat code blocks, does not realign
#     trailing comments, does not enforce brace placement, and
#     does not change indent width inside already-spaced code.
#     Those rules are AUDIT-ONLY (run `tools/audit_scripts.tcl`
#     once written) because no general-purpose transformer can
#     apply them reliably without a real Tcl parser.
#
# Examples:
#     tclsh tools/format_scripts.tcl --diff lib/Standard1.0/test.tcl
#     tclsh tools/format_scripts.tcl --write
#     tclsh tools/format_scripts.tcl --check --no-form-feed --quiet
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

package require Tcl 8.6


set ::DEFAULT_INCLUDE_GLOBS [list \
    lib/Standard1.0/*.tcl \
    tools/*.tcl]

set ::DEFAULT_EXCLUDE_GLOBS [list \
    bin/* \
    externals/* \
    tests/* \
    *.b64sig]


proc log_err {msg} {
  puts stderr "format_scripts: error: $msg"
}

proc log_warn {msg} {
  puts stderr "format_scripts: warning: $msg"
}

proc log_info {msg} {
  if {$::OPT(quiet)} then { return }
  puts stderr "format_scripts: $msg"
}

proc log_dbg {msg} {
  if {!$::OPT(verbose)} then { return }
  puts stderr "format_scripts: $msg"
}


proc parse_args {argv} {
  array set opt {
    mode             check
    indent           2
    tab_width        8
    line_endings     preserve
    binary           ""
    root             ""
    files_path       ""
    quiet            0
    verbose          0
    no_color         0
    end_of_opts      0

    do_tabs_to_spaces 0
    do_trim_trailing  1
    do_final_newline  1
    do_form_feed      1
    do_then_keyword   1
    do_collapse_blanks 0
    do_reindent       0
    do_brace_bodies   1
  }
  set includes {}
  set excludes {}
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
      --tabs-to-spaces    { set opt(do_tabs_to_spaces) 1 }
      --no-tabs-to-spaces { set opt(do_tabs_to_spaces) 0 }
      --trim-trailing     { set opt(do_trim_trailing)  1 }
      --no-trim-trailing  { set opt(do_trim_trailing)  0 }
      --final-newline     { set opt(do_final_newline)  1 }
      --no-final-newline  { set opt(do_final_newline)  0 }
      --form-feed         { set opt(do_form_feed)      1 }
      --no-form-feed      { set opt(do_form_feed)      0 }
      --then-keyword      { set opt(do_then_keyword)   1 }
      --no-then-keyword   { set opt(do_then_keyword)   0 }
      --collapse-blanks   { set opt(do_collapse_blanks) 1 }
      --no-collapse-blanks { set opt(do_collapse_blanks) 0 }
      --reindent          { set opt(do_reindent) 1 }
      --no-reindent       { set opt(do_reindent) 0 }
      --brace-bodies      { set opt(do_brace_bodies) 1 }
      --no-brace-bodies   { set opt(do_brace_bodies) 0 }
      --indent=*       { set opt(indent) [string range $a 9 end] }
      --tab-width=*    { set opt(tab_width) [string range $a 12 end] }
      --line-endings=* { set opt(line_endings) [string range $a 15 end] }
      --binary=*       { set opt(binary) [string range $a 9 end] }
      --root=*         { set opt(root) [string range $a 7 end] }
      --files=*        { set opt(files_path) [string range $a 8 end] }
      --include=*      { lappend includes [string range $a 10 end] }
      --exclude=*      { lappend excludes [string range $a 10 end] }
      --help - -h {
        print_usage
        exit 0
      }
      --* {
        log_err "unknown option: $a (use --help)"
        exit 2
      }
      default {
        lappend files $a
      }
    }
  }

  if {![string is integer -strict $opt(indent)] || $opt(indent) < 1} then {
    log_err "--indent requires a positive integer"
    exit 2
  }
  if {[lsearch -exact {lf crlf preserve} $opt(line_endings)] == -1} then {
    log_err "--line-endings must be lf, crlf, or preserve"
    exit 2
  }

  array unset ::OPT
  array set ::OPT [array get opt]
  set ::OPT(includes) $includes
  set ::OPT(excludes) $excludes
  set ::FILE_ARGS    $files
}


proc print_usage {} {
  set f [open [info script] r]
  set txt [read $f]
  close $f
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


proc is_excluded {fn root excludes} {
  set rel [relpath $fn $root]
  foreach pat $excludes {
    if {[string match $pat $rel]} then { return 1 }
    if {[string match $pat $fn]} then  { return 1 }
  }
  return 0
}


proc expand_files {root} {
  set out [list]
  array set seen {}

  foreach f $::FILE_ARGS {
    set fn [file normalize $f]
    if {[info exists seen($fn)]} then { continue }
    set seen($fn) 1
    lappend out $fn
  }

  if {$::OPT(files_path) ne ""} then {
    set listFile $::OPT(files_path)
    if {$listFile eq "-"} then {
      set chan stdin
    } else {
      if {[catch {open $listFile r} chan err]} then {
        log_err "cannot read --files=$listFile: $err"
        exit 2
      }
    }
    while {[gets $chan line] >= 0} {
      set line [string trim $line]
      if {$line eq "" || [string index $line 0] eq "#"} then { continue }
      set fn [file normalize $line]
      if {[info exists seen($fn)]} then { continue }
      set seen($fn) 1
      lappend out $fn
    }
    if {$listFile ne "-"} then { close $chan }
  }

  if {[llength $::FILE_ARGS] == 0 && $::OPT(files_path) eq ""} then {
    set includes $::OPT(includes)
    if {[llength $includes] == 0} then {
      set includes $::DEFAULT_INCLUDE_GLOBS
    }
    set excludes [concat $::DEFAULT_EXCLUDE_GLOBS $::OPT(excludes)]

    foreach pat $includes {
      set abs [file join $root $pat]
      if {[catch {glob -nocomplain -- $abs} hits]} then {
        log_warn "glob failed for $abs: $hits"
        continue
      }
      foreach h $hits {
        set fn [file normalize $h]
        if {[info exists seen($fn)]} then { continue }
        if {[is_excluded $fn $root $excludes]} then { continue }
        set seen($fn) 1
        lappend out $fn
      }
    }
  }

  return $out
}


###############################################################################
#
# detect_eol --
#
#     Inspect the first few hundred bytes of TEXT and decide which
#     line-ending convention dominates.  Returns "crlf" or "lf".
#
###############################################################################

proc detect_eol {text} {
  set sample [string range $text 0 4095]
  set crlfCount [regexp -all -- "\r\n" $sample]
  set lfTotal   [regexp -all -- "\n"   $sample]
  if {$crlfCount > 0 && $crlfCount * 2 >= $lfTotal} then {
    return crlf
  }
  return lf
}


###############################################################################
#
# read_file_text --
#
#     Read FN as raw bytes, then split into lines using either CRLF
#     or LF.  Returns a dict: text=raw bytes, lines=list of lines (no
#     trailing newline on each), eol=crlf|lf, finalNewline=1 if file
#     ended with a newline.
#
###############################################################################

proc read_file_text {fn} {
  set f [open $fn rb]
  set raw [read $f]
  close $f

  set eol [detect_eol $raw]
  set finalNewline [string match "*\n" $raw]

  if {$eol eq "crlf"} then {
    set lines [split [string map [list "\r\n" "\n"] $raw] "\n"]
  } else {
    set lines [split $raw "\n"]
  }
  # split leaves an empty trailing element when the file ended in \n.
  if {$finalNewline && [lindex $lines end] eq ""} then {
    set lines [lrange $lines 0 end-1]
  }

  return [dict create \
      text $raw lines $lines \
      eol  $eol  finalNewline $finalNewline]
}


###############################################################################
#
# write_file_text --
#
#     Compose LINES with the requested EOL convention and final-
#     newline flag, then write atomically to FN.
#
###############################################################################

proc write_file_text {fn lines eol finalNewline} {
  set sep [expr {$eol eq "crlf" ? "\r\n" : "\n"}]
  set buf [join $lines $sep]
  if {$finalNewline} then { append buf $sep }

  set tmp "$fn.fmt-tmp.[pid]"
  set f [open $tmp wb]
  puts -nonewline $f $buf
  close $f
  file rename -force -- $tmp $fn
}


###############################################################################
#
# Brace / string-state walkers (used by tx_reindent_script).
#
###############################################################################

# scan_line_state --
#     Walk LINE and return {netBraces stringExitState}.  STARTSTATE
#     is 1 if the line begins inside an unclosed double-quoted string
#     (carry-over from a previous line); 0 otherwise.  Backslash
#     escapes inside strings are honoured.  Comment lines (first
#     non-whitespace char is `#`) contribute zero of both.
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


# leading_closes --
#     Count consecutive close-braces at start of LINE (ignoring
#     leading whitespace and whitespace between close-braces).
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
#     1 iff LINE ends with a `\` (Tcl line continuation).
proc is_continuation_line {line} {
    return [regexp -- {\\$} $line]
}


###############################################################################
#
# Transform: reindent_script --
#
#     Re-indent the entire file based on actual brace nesting depth.
#     Each non-blank, non-string-continuation line gets indent
#         (depth - leading_closes) * 2
#     where depth is computed by walking unescaped braces outside of
#     double-quoted strings.  Continuation lines (those following a
#     `\`-terminated line) get +4 from the start-of-statement indent.
#     Multi-line string contents are emitted verbatim.
#
#     This transform is the single largest behaviour-changing edit
#     the formatter performs.  It is OFF by default; pass --reindent
#     to enable it.  When run in --check / --diff mode the user can
#     preview every change before committing.
#
###############################################################################

proc tx_reindent_script {lines} {
    set out [list]
    set depth 0
    set inContinuation 0
    set contIndent 0
    set inMultilineString 0

    foreach line $lines {
        # Lines inside an unclosed multi-line string literal: emit
        # verbatim and check for closure.
        if {$inMultilineString} then {
            lappend out $line
            lassign [scan_line_state $line 1] _net inStrExit
            set inMultilineString $inStrExit
            continue
        }

        # Form-feed page-break lines (\x0C alone or with leading
        # whitespace) are structural markers (§1.2 of the style
        # guide) and must survive the reindenter unchanged.
        if {[string match "*\x0c*" $line]} then {
            lappend out $line
            continue
        }

        if {[regexp -- {^[ \t]*$} $line]} then {
            lappend out ""
            continue
        }

        regexp -- {^[ \t]*(.*)$} $line -> rest
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
        set newIndent [expr {$effectiveDepth * 2}]
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
    if {[llength $out] != [llength $lines]} then {
        set changed 1
    } else {
        for {set i 0} {$i < [llength $out]} {incr i} {
            if {[lindex $out $i] ne [lindex $lines $i]} then {
                set changed 1
                break
            }
        }
    }
    return [list $out $changed]
}


###############################################################################
#
# Transform: tabs_to_spaces --
#
#     Expand every tab character to spaces such that the column of
#     each non-whitespace character is preserved at --tab-width
#     (default 8).  This is the standard `expand` semantics:
#
#         column 0 + \t  -> 8 spaces (advance to column 8)
#         column 5 + \t  -> 3 spaces (advance to column 8)
#         column 9 + \t  -> 7 spaces (advance to column 16)
#
#     Doing this column-aware (rather than tab-as-fixed-N-spaces)
#     preserves visual layout in continuation lines that use a
#     leading tab to align with column 8 even when the file's
#     normal indent is 2 spaces.
#
###############################################################################

proc tx_tabs_to_spaces {lines} {
    set tw $::OPT(tab_width)
    set out [list]
    set changed 0
    foreach line $lines {
        if {![string match "*\t*" $line]} then {
            lappend out $line
            continue
        }
        set newLine ""
        set col 0
        foreach ch [split $line ""] {
            if {$ch eq "\t"} then {
                set advance [expr {$tw - ($col % $tw)}]
                append newLine [string repeat " " $advance]
                incr col $advance
            } else {
                append newLine $ch
                incr col
            }
        }
        if {$newLine ne $line} then { set changed 1 }
        lappend out $newLine
    }
    return [list $out $changed]
}


###############################################################################
#
# Transform: trim_trailing --
#
#     Strip trailing spaces and tabs from every line.
#
###############################################################################

proc tx_trim_trailing {lines} {
    set out [list]
    set changed 0
    foreach line $lines {
        set newLine [regsub -- {[ \t]+$} $line ""]
        if {$newLine ne $line} then { set changed 1 }
        lappend out $newLine
    }
    return [list $out $changed]
}


###############################################################################
#
# Transform: collapse_blanks --
#
#     Replace any run of 3+ consecutive empty lines with a single
#     empty line.  (Optional; OFF by default because some files use
#     multi-blank spacing intentionally between major sections.)
#
###############################################################################

proc tx_collapse_blanks {lines} {
    set out [list]
    set changed 0
    set blankRun 0
    foreach line $lines {
        if {[regexp -- {^[ \t]*$} $line]} then {
            incr blankRun
            if {$blankRun >= 3} then {
                set changed 1
                continue
            }
            lappend out $line
        } else {
            set blankRun 0
            lappend out $line
        }
    }
    return [list $out $changed]
}


###############################################################################
#
# Transform: form_feed --
#
#     Insert a form-feed (\x0C) page break alone on its own line
#     between every adjacent pair of procs that live inside a
#     `namespace eval ... { ... }` block.
#
#     The detector is heuristic but conservative: it fires only
#     when ALL of the following hold:
#       * The previous non-blank line is a closing brace `}` whose
#         indent is at least one level (it lives inside a namespace
#         or another block).
#       * The next non-blank line begins a `# NOTE:`-style block
#         comment OR is a `proc <name>` / `nproc <name>` declaration.
#       * The blank line(s) between them do not already contain a
#         form-feed.
#
#     The algorithm walks the file once, counting brace nesting at
#     statement boundaries only roughly (line-leading) so that
#     proc bodies are not misinterpreted as namespace level.  We
#     consider a line "at namespace level" when its leading-indent
#     column equals 2 (the namespace-eval body indent).
#
###############################################################################

proc is_blank_line {line} {
    return [regexp -- {^[ \t]*$} $line]
}

proc has_form_feed {line} {
    return [regexp -- "\x0c" $line]
}

proc leading_indent_cols {line} {
    if {[regexp -- {^([ \t]*)} $line -> ws]} then {
        # tabs count as 8 here, matching common editor display.
        set cols 0
        foreach ch [split $ws ""] {
            if {$ch eq "\t"} then {
                incr cols [expr {8 - ($cols % 8)}]
            } else {
                incr cols
            }
        }
        return $cols
    }
    return 0
}

proc looks_like_proc_intro {line} {
    if {[regexp -- {^\s*(proc|nproc|f_proc|s_proc)\s+\S+} $line]} then {
        return 1
    }
    if {[regexp -- {^\s*#\s*$} $line]} then { return 1 }
    if {[regexp -- {^\s*#\s+(NOTE|WARNING|TODO|HACK|MONO|BUGBUG|BUGFIX):} $line]} then {
        return 1
    }
    if {[regexp -- {^\s*#\s*<(public|bootstrap|experimental|callback|create|demonstration|help)>} $line]} then {
        return 1
    }
    return 0
}

# is_proc_decl_line --
#     1 iff LINE introduces a proc / nproc / f_proc / s_proc.
proc is_proc_decl_line {line} {
    return [regexp -- {^\s*(proc|nproc|f_proc|s_proc)\s+\S+} $line]
}

###############################################################################
#
# Transform: brace_bare_bodies --
#
#     Wrap a bare body of an `if` / `elseif` / `else` clause in
#     braces with the `then` keyword.  Bare bodies look like:
#
#         if {cond} continue
#         if {cond} return
#         if {cond} return value
#         if {cond} break
#         if {cond} exit
#         if {cond} exit 1
#
#     The canonical form is:
#
#         if {cond} then { continue }
#
#     The transform handles `if`, `elseif`, and `else` (the `else`
#     form has no condition: `} else continue`).
#
#     Limitations: only triggers when the condition braces are
#     simple (no nested `{}`).  Rare nested-condition cases are left
#     for the human; the resulting style violation would be flagged
#     by an audit pass.
#
###############################################################################

proc tx_brace_bare_bodies {lines} {
    set out [list]
    set changed 0
    set keywords {continue return break exit}
    set kwAlt [join $keywords |]
    set OB [format %c 123]    ;# literal open-brace
    set CB [format %c 125]    ;# literal close-brace
    # Build regexes via format() so the source contains no literal
    # open/close braces -- defends against Tcl's brace-balance
    # parser inside the proc's own braced word.
    set re1 [format \
        {^(\s*)(if|elseif)\s+(%c[^%c%c]+%c)\s+(%s)(\s+\S.*)?\s*$} \
        123 123 125 125 $kwAlt]
    set re2 [format \
        {^(\s*)%c\s+else\s+(%s)(\s+\S.*)?\s*$} \
        125 $kwAlt]
    foreach line $lines {
        set newLine $line
        # Match: leading-ws + (if|elseif) + braced-cond-no-nested
        # + space + KEYWORD + optional rest + EOL.
        if {[regexp -- $re1 $newLine -> ind kw cond bodyKw bodyArgs]} then {
            if {$bodyArgs eq ""} then {
                set body $bodyKw
            } else {
                set body "$bodyKw[string trimright $bodyArgs]"
            }
            set newLine "${ind}${kw} ${cond} then ${OB} ${body} ${CB}"
        }
        # Match: leading-ws + close-brace + else + KEYWORD + ...
        if {[regexp -- $re2 $newLine -> ind bodyKw bodyArgs]} then {
            if {$bodyArgs eq ""} then {
                set body $bodyKw
            } else {
                set body "$bodyKw[string trimright $bodyArgs]"
            }
            set newLine "${ind}${CB} else ${OB} ${body} ${CB}"
        }
        if {$newLine ne $line} then { set changed 1 }
        lappend out $newLine
    }
    return [list $out $changed]
}


proc tx_form_feed {lines} {
    set out [list]
    set changed 0
    set n [llength $lines]

    #
    # PASS 1 -- brace-balanced scan to identify proc bodies.
    #
    # Walk every line tracking depth (unescaped braces outside
    # double-quoted strings, multi-line strings honoured).  A line
    # matching is_proc_decl_line OPENS a proc body; the line whose
    # net brace delta carries depth back below the proc's opening
    # depth CLOSES it.  We record {open_line close_line opening_depth}
    # tuples for each detected proc.
    #
    set procs [list]
    set procStack [list]
    set depth 0
    set inMultilineString 0
    for {set i 0} {$i < $n} {incr i} {
        set line [lindex $lines $i]
        if {$inMultilineString} then {
            lassign [scan_line_state $line 1] _net inStrExit
            set inMultilineString $inStrExit
            continue
        }
        # Detect proc-decl on the OPENING line; the brace count of
        # the same line includes that proc's opening open-brace.
        if {[is_proc_decl_line $line]} then {
            lappend procStack [list $i $depth]
        }
        lassign [scan_line_state $line 0] net inStrExit
        set inMultilineString $inStrExit
        set newDepth [expr {$depth + $net}]
        # If a proc was open, see whether THIS line closed it (i.e.
        # newDepth <= openingDepth).
        while {[llength $procStack] > 0} {
            set top [lindex $procStack end]
            lassign $top openLine openDepth
            if {$newDepth <= $openDepth} then {
                lappend procs [list $openLine $i $openDepth]
                set procStack [lrange $procStack 0 end-1]
                continue
            }
            break
        }
        set depth $newDepth
        if {$depth < 0} then { set depth 0 }
    }

    #
    # Build a per-line classification: each line index gets a tag in
    # one of {OUTSIDE, PROC_OPEN, PROC_INSIDE, PROC_CLOSE}.  Used in
    # PASS 3 to remove FFs that sit INSIDE a proc body.
    #
    array set lineTag {}
    for {set i 0} {$i < $n} {incr i} { set lineTag($i) OUTSIDE }
    foreach p $procs {
        lassign $p openLine closeLine _od
        set lineTag($openLine) PROC_OPEN
        set lineTag($closeLine) PROC_CLOSE
        for {set k [expr {$openLine + 1}]} {$k < $closeLine} {incr k} {
            set lineTag($k) PROC_INSIDE
        }
    }

    #
    # PASS 2 -- compute, for each adjacent pair of procs at the same
    # opening depth, whether an FF is needed in the gap between them
    # (close of one to open of the next).
    #
    array set ffNeededAt {}    ;# key: line index where FF should be inserted
    set numProcs [llength $procs]
    for {set p 0} {$p < $numProcs - 1} {incr p} {
        lassign [lindex $procs $p]       _op1 closeLine d1
        lassign [lindex $procs [expr {$p + 1}]] open2 _cl2 d2
        # Only emit FF between procs at the same nesting depth -- a
        # nested proc inside an outer proc does not get an FF before
        # it (FFs are between siblings, not between parent and child).
        if {$d1 != $d2} then { continue }
        # The form-feed convention applies only to TOP-LEVEL procs
        # (depth 0) and NAMESPACE-LEVEL procs (depth 1, inside a
        # `namespace eval { ... }`).  Procs defined deeper than that
        # are typically nested inside catch / expr / apply / control
        # flow and are NOT page-break candidates.
        if {$d1 > 1} then { continue }
        # Only siblings inside the same enclosing block qualify --
        # i.e. the close-brace of the previous proc and the next
        # proc's `proc` line must lie within the same parent scope.
        # Detect this by walking the lines between them: if any line
        # opens or closes a brace at THIS depth (i.e. transitions
        # the parent scope), the procs are not siblings.
        set siblings 1
        for {set k [expr {$closeLine + 1}]} {$k < $open2} {incr k} {
            set candidate [lindex $lines $k]
            # Skip blank, comment, and FF-only lines.
            if {[is_blank_line $candidate]} then { continue }
            if {[has_form_feed $candidate]} then { continue }
            if {[regexp -- {^\s*#} $candidate]} continue
            # Any non-blank, non-comment line between procs means
            # they are not direct siblings; the previous proc's
            # parent scope closed and a new context opened.  Skip.
            set siblings 0
            break
        }
        if {!$siblings} then { continue }
        # Walk from closeLine+1 to open2-1 and decide:
        #   * if a blank-only line containing FF already exists, OK.
        #   * else find a blank line and mark it for FF insertion.
        set hadFF 0
        set blankIdx -1
        for {set k [expr {$closeLine + 1}]} {$k < $open2} {incr k} {
            set candidate [lindex $lines $k]
            if {[has_form_feed $candidate]} then { set hadFF 1; break }
            if {[is_blank_line $candidate] && $blankIdx < 0} then {
                set blankIdx $k
            }
        }
        if {!$hadFF} then {
            if {$blankIdx >= 0} then {
                set ffNeededAt($blankIdx) replace
            } else {
                set ffNeededAt([expr {$closeLine + 1}]) inject
            }
        }
    }

    #
    # PASS 3 -- emit, applying the FF decisions and removing any FF
    # lines that sit INSIDE a proc body (they were misplaced).
    #
    for {set i 0} {$i < $n} {incr i} {
        set line [lindex $lines $i]
        if {[has_form_feed $line] && $lineTag($i) eq "PROC_INSIDE"} then {
            # Strip a misplaced in-proc FF.  If the line was JUST
            # the FF, drop the line entirely.  Otherwise, remove
            # the FF char from the line.
            set stripped [string map [list "\x0c" ""] $line]
            if {[regexp -- {^[ \t]*$} $stripped]} then {
                # FF-only line -- drop it (replace with blank to
                # preserve visual spacing).
                lappend out ""
            } else {
                lappend out $stripped
            }
            set changed 1
            continue
        }
        if {[info exists ffNeededAt($i)]} then {
            switch -- $ffNeededAt($i) {
                replace {
                    # Replace this blank line with the FF line.
                    lappend out "\x0c"
                    set changed 1
                    continue
                }
                inject {
                    # Inject FF line BEFORE re-emitting current line.
                    lappend out "\x0c"
                    set changed 1
                }
            }
        }
        lappend out $line
    }
    return [list $out $changed]
}


###############################################################################
#
# Transform: then_keyword --
#
#     Add the `then` keyword to `if` and `elseif` clauses that
#     omit it.  The transformer needs to handle:
#       * Simple form on one line: `if {cond} { ... }`
#       * Multi-line conditions ending with `\` continuation.
#       * Existing `then` (idempotent: no double-insertion).
#       * Conditions containing nested braces.
#
#     Strategy: walk the joined-text byte-by-byte at statement
#     boundaries, find `if` / `elseif` keywords, find the matching
#     close-brace of the condition, and check what follows.
#
#     This routine handles only the common forms; pathological
#     scripts with conditions split across continuation lines AND
#     containing `{`/`}` substring patterns inside `"..."` strings
#     within the condition are left alone (those are vanishingly
#     rare in the corpus).
#
###############################################################################

proc tx_then_keyword {lines} {
    # Join lines, transform, then re-split.  We use a sentinel to
    # mark line breaks that we must preserve.
    set sep "\x01"
    set joined [join $lines $sep]
    set out ""
    set changed 0

    set i 0
    set n [string length $joined]
    while {$i < $n} {
        # Find the next `if` or `elseif` at a statement-start
        # boundary.  Statement-start = beginning of file, after
        # `;`, after a newline (sentinel), or after `{` `}` `[`.
        set rest [string range $joined $i end]
        if {![regexp -indices -- \
                {(?:^|[\x01;{}\[\]])([ \t]*)(if|elseif)([ \t]+)\{} \
                $rest match leadIdx kwIdx spIdx]} then {
            append out [string range $joined $i end]
            break
        }
        # Append everything before the keyword block verbatim.
        set absStart [expr {$i + [lindex $match 0]}]
        append out [string range $joined $i [expr {$absStart - 1}]]

        # Where is the leading boundary char?  Could be one byte
        # before (we matched it) or be the file start (offset 0).
        set boundary [string index $joined $absStart]
        if {[string match {[\x01;{}\[\]]} $boundary]} then {
            append out $boundary
            incr absStart
        }
        # Now copy the leading whitespace + keyword + space + `{`.
        set leadAbsStart [expr {$i + [lindex $leadIdx 0]}]
        set kwAbsEnd     [expr {$i + [lindex $kwIdx 1]}]
        set spAbsEnd     [expr {$i + [lindex $spIdx 1]}]
        # The close brace `}` follows at index $spAbsEnd + 1
        set braceAbs     [expr {$spAbsEnd + 1}]
        # Confirm the brace is `{`
        if {[string index $joined $braceAbs] ne "\{"} then {
            append out [string range $joined $absStart $braceAbs]
            set i [expr {$braceAbs + 1}]
            continue
        }

        # Walk braces forward, respecting backslash escapes, to
        # find the matching `}`.
        set depth 1
        set p [expr {$braceAbs + 1}]
        set closeAbs -1
        while {$p < $n} {
            set c [string index $joined $p]
            if {$c eq "\\"} then {
                # Escape: skip next char.
                incr p 2
                continue
            }
            if {$c eq "\{"} then { incr depth }
            if {$c eq "\}"} then {
                incr depth -1
                if {$depth == 0} then {
                    set closeAbs $p
                    break
                }
            }
            incr p
        }
        if {$closeAbs < 0} then {
            # Unbalanced -- bail out, copy rest verbatim.
            append out [string range $joined $absStart end]
            break
        }

        # Append the leading-ws + keyword + space + condition + close.
        append out [string range $joined $absStart $closeAbs]

        # What follows the close brace?  Skip whitespace (incl.
        # `\<sep>` continuations).
        set q [expr {$closeAbs + 1}]
        set whiteSpan ""
        while {$q < $n} {
            set c [string index $joined $q]
            if {$c eq " " || $c eq "\t" || $c eq $sep} then {
                append whiteSpan $c
                incr q
                continue
            }
            if {$c eq "\\" && [string index $joined [expr {$q + 1}]] eq $sep} then {
                append whiteSpan [string range $joined $q [expr {$q + 1}]]
                incr q 2
                continue
            }
            break
        }
        # Check what comes after the whitespace.
        set nextWord [string range $joined $q [expr {$q + 4}]]
        if {[regexp -- {^then[ \t\x01\{]} $nextWord]} then {
            # Already has `then`.  Copy through and continue.
            append out $whiteSpan
            set i $q
            continue
        }
        if {[string index $joined $q] eq "\{"} then {
            # Need to insert `then `.
            append out " then"
            append out $whiteSpan
            set i $q
            set changed 1
            continue
        }
        # Anything else (e.g. `;`, `]`, end-of-input): leave alone.
        append out $whiteSpan
        set i $q
    }

    set newLines [split $out $sep]
    return [list $newLines $changed]
}


###############################################################################
#
# format_one_file --
#
#     Apply enabled transforms to one file's lines.  Returns
#     {newLines newEol newFinalNewline anyChange} where anyChange
#     is 1 if any transform reported a modification.
#
###############################################################################

proc format_one_file {fn} {
    set info [read_file_text $fn]
    set lines [dict get $info lines]
    set eol   [dict get $info eol]
    set finalNewline [dict get $info finalNewline]

    if {$::OPT(line_endings) ne "preserve"} then {
        set eol $::OPT(line_endings)
    }

    set anyChange 0

    foreach {flag proc} {
        do_tabs_to_spaces  tx_tabs_to_spaces
        do_trim_trailing   tx_trim_trailing
        do_collapse_blanks tx_collapse_blanks
        do_brace_bodies    tx_brace_bare_bodies
        do_reindent        tx_reindent_script
        do_form_feed       tx_form_feed
        do_then_keyword    tx_then_keyword
    } {
        if {!$::OPT($flag)} then { continue }
        lassign [$proc $lines] lines changed
        if {$changed} then {
            log_dbg "  $proc fired"
            set anyChange 1
        }
    }

    if {$::OPT(do_final_newline) && !$finalNewline} then {
        set finalNewline 1
        set anyChange 1
    }

    return [list $lines $eol $finalNewline $anyChange]
}


###############################################################################
#
# render --
#
#     Compose LINES into a single byte string with the given EOL.
#
###############################################################################

proc render {lines eol finalNewline} {
    set sep [expr {$eol eq "crlf" ? "\r\n" : "\n"}]
    set buf [join $lines $sep]
    if {$finalNewline} then { append buf $sep }
    return $buf
}


###############################################################################
#
# tty_supports_color --
#
###############################################################################

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


###############################################################################
#
# show_diff --
#
###############################################################################

proc show_diff {fn original updated} {
    set tmpA "$fn.fmt-orig.[pid]"
    set tmpB "$fn.fmt-new.[pid]"
    set fa [open $tmpA wb]; puts -nonewline $fa $original; close $fa
    set fb [open $tmpB wb]; puts -nonewline $fb $updated;  close $fb

    set diffCmd [auto_execok diff]
    if {$diffCmd eq ""} then {
        puts "$fn: would change (install diff for unified output)"
        file delete -- $tmpA $tmpB
        return
    }
    set rel [relpath $fn [resolve_root]]
    catch {exec [lindex $diffCmd 0] -u \
            -L "a/$rel" -L "b/$rel" \
            $tmpA $tmpB} diffOut
    file delete -- $tmpA $tmpB

    # Strip Tcl's trailing "child process exited abnormally" noise
    # that exec appends when diff exits non-zero (which it does
    # whenever there is any difference -- the expected case here).
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


###############################################################################
#
# main --
#
###############################################################################

proc main {argv} {
    parse_args $argv

    set root [resolve_root]
    set files [expand_files $root]
    if {[llength $files] == 0} then {
        log_warn "no files matched"
        exit 0
    }
    log_info "processing [llength $files] file(s)"

    set okCount 0
    set changedCount 0
    set errorCount 0

    foreach fn $files {
        if {![file exists $fn] || ![file readable $fn]} then {
            log_err "missing or unreadable: $fn"
            incr errorCount
            continue
        }
        set rel [relpath $fn $root]

        if {[catch {format_one_file $fn} ret]} then {
            log_err "transform failed on $fn: $ret"
            incr errorCount
            continue
        }
        lassign $ret newLines newEol newFinal anyChange

        if {!$anyChange} then {
            incr okCount
            log_dbg "ok: $rel"
            continue
        }

        switch -- $::OPT(mode) {
            check {
                incr changedCount
                if {!$::OPT(quiet)} then {
                    puts "needs format: $rel"
                }
            }
            diff {
                incr changedCount
                set info [read_file_text $fn]
                set originalRaw [dict get $info text]
                set updatedRaw  [render $newLines $newEol $newFinal]
                show_diff $fn $originalRaw $updatedRaw
            }
            write {
                if {[catch {
                    write_file_text $fn $newLines $newEol $newFinal
                } err]} then {
                    log_err "write failed: $rel: $err"
                    incr errorCount
                } else {
                    incr okCount
                    log_dbg "wrote: $rel"
                }
            }
        }
    }

    log_info "summary: ok=$okCount changed=$changedCount error=$errorCount"

    if {$errorCount > 0} then { exit 2 }
    if {$::OPT(mode) eq "write"} then { exit 0 }
    if {$changedCount > 0} then { exit 1 }
    exit 0
}


main $argv
