#!/usr/bin/env tclsh
###############################################################################
#
# mkindex.tcl --
#
#     Generate the Command Index for the Tcl Language Standard from
#     three sources of truth:
#
#       1. The standard itself (`docs/public/tcl_language_standard_v1.md`),
#          which provides the section headings (`#### N.M  command-
#          name`) used as the canonical pointer for each command's
#          R-marker block.
#       2. The plugin source files under `src/plugins/th8_*.c`,
#          which expose the command-to-plugin mapping via the
#          `th8XxxCommands[]` static tables registered by
#          `th8XxxGetCommands`.
#       3. The plugin compile-time gate names (`TH8_PLUGIN_*`) that
#          appear at the top of each plugin source file as
#          `#if defined(TH8_PLUGIN_XXX)`.
#
#     The output is a Markdown document containing:
#
#       - A per-plugin breakdown.  For each plugin, its commands are
#         listed in alphabetical order with the section number where
#         their R-markers live.
#       - A master alphabetical index of every command, with its
#         plugin label and section pointer.
#
#     Sub-commands (e.g. `string compare`, `info default`, `file
#     join`) are extracted from the section headings themselves --
#     when a heading is `#### N.M  string compare`, the index
#     records `compare` as a sub-command of `string`.
#
# Usage:
#     tclsh tools/mkindex.tcl
#         Writes `docs/public/tcl_language_standard_command_index.md`.
#
#     tclsh tools/mkindex.tcl --check
#         Runs the same parsing but compares against the existing
#         output file; exits non-zero if drift is detected.  Useful
#         as a CI gate.
#
#     tclsh tools/mkindex.tcl --output PATH
#         Write to PATH instead of the default location.
#
# Design notes:
#     The tool is intentionally tolerant of mismatches: a command
#     present in the standard but not in any plugin is recorded as
#     plugin "core" (built-in, not gated).  A command present in a
#     plugin but absent from Part III is reported on stderr as a
#     coverage gap (this is information for the standard editor,
#     not an error).
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
# Argument handling.
#
###############################################################################

proc resolve_root {} {
    set scriptDir [file dirname [file normalize [info script]]]
    return [file normalize [file join $scriptDir ..]]
}


proc parse_args {argv} {
    array set ::OPT {
        mode      generate
        output    ""
        verbose   0
    }
    set ::OPT(root) [resolve_root]
    foreach arg $argv {
        switch -glob -- $arg {
            --check   { set ::OPT(mode) check }
            --verbose { set ::OPT(verbose) 1 }
            --output=* {
                set ::OPT(output) [string range $arg 9 end]
            }
            --help - -h {
                puts "Usage: tclsh tools/mkindex.tcl ?--check? ?--output=PATH? ?--verbose?"
                exit 0
            }
            --* {
                puts stderr "mkindex: unknown option: $arg"
                exit 2
            }
            default {
                puts stderr "mkindex: unexpected positional arg: $arg"
                exit 2
            }
        }
    }
    if {$::OPT(output) eq ""} then {
        set ::OPT(output) [file join $::OPT(root) \
                docs public tcl_language_standard_command_index.md]
    }
}


###############################################################################
#
# parse_standard_commands --
#
#     Walk Part III of the standard and yield a list of
#     {section command subcommand} triples.  For headings like
#     `#### 11.1  set` the triple is {"11.1" "set" ""}; for
#     `#### 15.1  string compare` it is {"15.1" "string" "compare"};
#     for `#### 11.8a  array unset` it is {"11.8a" "array" "unset"}.
#     The first whitespace-separated token after the section number
#     is the command name; anything after it is the sub-command (a
#     single space-separated suffix).
#
###############################################################################

proc parse_standard_commands_in {path partStart partEnd} {
    # Scan PATH, returning {sec cmd subcmd} triples for every
    # `#### N.M[a]  cmd ?subcmd?` heading that lives between the
    # `## PARTSTART ---` and `## PARTEND ---` Part headings.
    set fd [open $path r]
    set raw [read $fd]
    close $fd

    set entries [list]
    set in 0
    foreach line [split $raw \n] {
        if {[regexp -- "^## $partStart" $line]} then {
            set in 1
            continue
        }
        if {[regexp -- "^## $partEnd" $line]} then {
            set in 0
            continue
        }
        if {!$in} then { continue }

        if {[regexp -- {^#### ([0-9]+\.[0-9]+[a-z]?)  (\S+)(?:\s+(.+))?$} \
                $line -> sec cmd subcmd]} then {
            set subcmd [string trim $subcmd]
            lappend entries [list $sec $cmd $subcmd]
        }
    }
    return $entries
}


proc parse_standard_commands {standardPath} {
    # Collect entries from both source documents:
    #   - Tcl Language Standard (Part III only)
    #   - TH8 Language Extensions (Part I only -- script-level extensions)
    set entries [parse_standard_commands_in $standardPath {Part III} {Part IV}]
    set extPath [file join [file dirname $standardPath] \
            th8_language_extensions.md]
    if {[file readable $extPath]} then {
        # Scan all of extensions: Part I (script-level extensions)
        # plus Parts II-V (which include script-level commands like
        # `harpy` mixed with C-API entries and many subsystem
        # subsections that are not commands).  Use an end marker
        # that never matches so the scan continues to end-of-file,
        # then filter out anything whose "cmd" doesn't look like a
        # lowercase Tcl command identifier (e.g. `Th8_Initialize`,
        # `EXISTS Operation`).
        set extEntries [parse_standard_commands_in $extPath \
                {Part I ---} {NEVER_MATCHES_END}]
        foreach e $extEntries {
            set cmd [lindex $e 1]
            if {![regexp -- {^[a-z][a-z0-9_]*$} $cmd]} continue
            lappend entries $e
        }
    }
    return $entries
}


###############################################################################
#
# parse_standard_options --
#
#     Walk Part III of the standard.  For each command section
#     (`#### N.M  cmd ?subcmd?`), collect the body lines until the
#     next `####` / `###` / `##` heading, then extract every
#     backtick-quoted token of the form `-foo` (or `--`) that appears
#     in those lines.  Returns a dict mapping the **top-level command
#     name** (e.g. `lsort`, `regexp`, `string`) to a sorted list of
#     unique options.
#
#     Options associated with sub-commands are merged onto the parent
#     command (e.g. `dict get -default ...` becomes an option of
#     `dict`).  This keeps the index aligned with the plugin
#     registration granularity, which is per top-level command.
#
#     A token is considered an option if and only if:
#       - it begins with `-` followed by an ASCII letter, OR is the
#         literal end-of-switches marker `--`;
#       - it is enclosed in backticks `` ` ` ``;
#       - the part before any internal whitespace is taken as the
#         option name (so `` `-start N` `` yields `-start`).
#
#     Numeric tokens like `` `-1` ``, operators like `` `||` `` or
#     `` `%` ``, and bare hyphens are excluded by the leading-letter
#     rule.
#
###############################################################################

proc parse_standard_options_in {path partStart partEnd opts} {
    # Parse PATH in the range [partStart, partEnd), updating OPTS
    # (a dict cmd -> list of options).  Returns the updated dict.
    set fd [open $path r]
    set raw [read $fd]
    close $fd

    # Commands whose "options" are caller-defined parameter names, not
    # a fixed set of switches accepted by the command itself.  For
    # these, any backticked `-foo` token in the section text is a
    # placeholder example, not a literal option that the command
    # interprets.  Skip them to avoid misleading entries in the index.
    set callerDefined {nproc napply}

    set in 0
    set currentCmd ""
    foreach line [split $raw \n] {
        if {[regexp -- "^## $partStart" $line]} then {
            set in 1
            continue
        }
        if {[regexp -- "^## $partEnd" $line]} then {
            set in 0
            set currentCmd ""
            continue
        }
        if {!$in} then { continue }

        # A new section heading (any depth ###/####/#####) resets the
        # current command tracker if it's at part-level (### N) -- we
        # only want to emit options when inside a command section.
        if {[regexp -- {^### [0-9]} $line]} then {
            set currentCmd ""
            continue
        }

        # `#### N.M  cmd ?subcmd?` --- new command context.  For top-
        # level command headings, set currentCmd to the command.  For
        # `##### N.M.K  cmd subcmd` (sub-subsections), currentCmd
        # remains the parent command.
        if {[regexp -- {^#### [0-9]+\.[0-9]+[a-z]?  (\S+)} \
                $line -> cmd]} then {
            set currentCmd $cmd
            if {![dict exists $opts $currentCmd]} then {
                dict set opts $currentCmd [list]
            }
            continue
        }

        if {$currentCmd eq ""} then { continue }
        if {[lsearch -exact $callerDefined $currentCmd] >= 0} then { continue }

        # Extract backtick-quoted option tokens.  Match a backtick,
        # then `-` followed by either another `-` (the `--` marker)
        # or an ASCII letter, then any non-backtick run.  The token
        # is everything up to the first whitespace inside the run.
        set pos 0
        while {[regexp -indices -start $pos -- \
                {`(-(?:-|[a-zA-Z][a-zA-Z0-9_-]*))(?:\s[^`]*)?`} \
                $line matchRange optRange]} {
            set opt [string range $line \
                    [lindex $optRange 0] [lindex $optRange 1]]
            set list [dict get $opts $currentCmd]
            if {[lsearch -exact $list $opt] < 0} then {
                lappend list $opt
                dict set opts $currentCmd $list
            }
            set pos [expr {[lindex $matchRange 1] + 1}]
        }
    }
    return $opts
}


proc parse_standard_options {standardPath} {
    set opts [dict create]
    set opts [parse_standard_options_in $standardPath \
            {Part III} {Part IV} $opts]
    set extPath [file join [file dirname $standardPath] \
            th8_language_extensions.md]
    if {[file readable $extPath]} then {
        set opts [parse_standard_options_in $extPath \
                {Part I ---} {NEVER_MATCHES_END} $opts]
    }
    # Sort each command's option list.
    dict for {cmd list} $opts {
        dict set opts $cmd [lsort $list]
    }
    return $opts
}


###############################################################################
#
# parse_plugin_commands --
#
#     Walk every `src/plugins/th8_*.c` file and yield a list of
#     {command pluginShortName pluginGate} triples.  The plugin
#     short name is derived from the file stem (e.g.
#     `th8_lists.c` -> `lists`).  The plugin gate is the
#     `TH8_PLUGIN_*` symbol referenced inside the file's first
#     `#if defined(...)` -- if none is found, gate is empty.
#
#     Sub-plugin directories (`crypto/`, `harpy/`, `regexp/`)
#     contribute their own command tables; the top-level
#     `src/plugins/th8_*.c` files are the canonical Part-III-aligned
#     plugins.  The sub-plugin contents are reported under their
#     parent plugin name.
#
###############################################################################

proc parse_plugin_commands {root} {
    set entries [list]
    set seen [list]
    set glob [file join $root src plugins th8_*.c]
    foreach f [lsort [glob -nocomplain -- $glob]] {
        set short [file rootname [file tail $f]]
        # Strip the leading "th8_" prefix.
        regsub {^th8_} $short "" pluginName
        # Read file.
        set fd [open $f r]
        set src [read $fd]
        close $fd
        # Pluck the gate from the first "#if defined(TH8_PLUGIN_XXX)".
        set gate ""
        if {[regexp -- {#if defined\((TH8_PLUGIN_[A-Z_]+)\)} \
                $src -> match]} then {
            set gate $match
        }
        # Pluck command names from any line of the form:
        #     { 1, 0, "name",  function },
        # inside a static array (we are tolerant here -- any such
        # pattern in the file is taken as a command registration).
        foreach m [regexp -all -inline -- \
                {\{\s*1,\s*0,\s*"([a-zA-Z_][a-zA-Z0-9_]*)"\s*,} $src] {
            # regexp -all -inline returns alternating full-match,
            # capture pairs; we want the captures.
        }
        # Re-do with a loop that also extracts captures.
        set pos 0
        while {[regexp -indices -start $pos -- \
                {\{\s*1,\s*0,\s*"([a-zA-Z_][a-zA-Z0-9_]*)"\s*,} \
                $src matchRange nameRange]} {
            set name [string range $src \
                    [lindex $nameRange 0] [lindex $nameRange 1]]
            set key "${name}|${pluginName}"
            if {[lsearch -exact $seen $key] < 0} then {
                lappend seen $key
                lappend entries [list $name $pluginName $gate]
            }
            set pos [expr {[lindex $matchRange 1] + 1}]
        }
    }
    return $entries
}


###############################################################################
#
# build_command_table --
#
#     Combine the standard parse and the plugin parse into a
#     single command -> {section subcommand-list plugin gate}
#     mapping.  Returns a dict keyed by command name.
#
###############################################################################

proc build_command_table {standardEntries pluginEntries optionsByCmd} {
    set cmds [dict create]
    # Standard provides section pointers and sub-commands.
    foreach e $standardEntries {
        lassign $e sec cmd sub
        if {![dict exists $cmds $cmd]} then {
            dict set cmds $cmd \
                [dict create section "" subs [list] \
                    plugin "" gate "" options [list]]
        }
        # Primary section: pick the first sec we see for which sub == "".
        # If only sub-command headings exist for this command (no top-
        # level entry), pick the first sec encountered.
        set entry [dict get $cmds $cmd]
        if {[dict get $entry section] eq ""} then {
            dict set entry section $sec
        }
        if {$sub ne ""} then {
            set subs [dict get $entry subs]
            if {[lsearch -exact $subs $sub] < 0} then {
                lappend subs $sub
                dict set entry subs $subs
            }
        }
        dict set cmds $cmd $entry
    }
    # Plugin entries supply plugin name and gate.
    foreach e $pluginEntries {
        lassign $e cmd plugin gate
        if {![dict exists $cmds $cmd]} then {
            # Command in a plugin but not in Part III -- record it
            # so it shows up in the gap report; mark section as "?".
            dict set cmds $cmd \
                [dict create section "?" subs [list] \
                    plugin $plugin gate $gate options [list]]
        } else {
            set entry [dict get $cmds $cmd]
            if {[dict get $entry plugin] eq ""} then {
                dict set entry plugin $plugin
                dict set entry gate $gate
                dict set cmds $cmd $entry
            }
        }
    }
    # Mark commands that appear in the standard but no plugin as "core"
    # (built-in by the interpreter, not gateable).
    dict for {cmd entry} $cmds {
        if {[dict get $entry plugin] eq ""} then {
            dict set entry plugin "core"
            dict set cmds $cmd $entry
        }
    }
    # Attach options harvested from the standard (already de-duped and
    # sorted by parse_standard_options).
    dict for {cmd list} $optionsByCmd {
        if {[dict exists $cmds $cmd]} then {
            set entry [dict get $cmds $cmd]
            dict set entry options $list
            dict set cmds $cmd $entry
        }
    }
    return $cmds
}


###############################################################################
#
# render_index --
#
#     Compose the Markdown content of the index from the command
#     table.  Two organising views: per-plugin (alphabetical within
#     each plugin) and master alphabetical.
#
###############################################################################

proc render_index {cmds} {
    set out ""
    append out "# Tcl Language Standard --- Command Index\n\n"
    append out "*Auto-generated by `tools/mkindex.tcl` from\n"
    append out "`docs/public/tcl_language_standard_v1.md` and `src/plugins/th8_*.c`.\n"
    append out "Do not edit by hand; re-run the tool after changes to either source.*\n\n"

    append out "This document is a navigation aid for the formal\n"
    append out "**Tcl Language Standard**.  Every command in the language\n"
    append out "is listed here with a pointer to the section that\n"
    append out "specifies its normative requirements, plus the plugin\n"
    append out "category that registers it (and the compile-time gate\n"
    append out "that controls its inclusion).\n\n"
    append out "Commands without a section pointer (`?`) are registered by\n"
    append out "a plugin but not yet specified in the standard --- they\n"
    append out "are listed for completeness and identify standardisation\n"
    append out "work still to be done.\n\n"
    append out "---\n\n"

    # ---- Per-plugin breakdown ----
    append out "## Part 1 --- By Plugin\n\n"

    # Group commands by plugin.
    set byPlugin [dict create]
    dict for {cmd entry} $cmds {
        set p [dict get $entry plugin]
        if {![dict exists $byPlugin $p]} then {
            dict set byPlugin $p [list]
        }
        set list [dict get $byPlugin $p]
        lappend list $cmd
        dict set byPlugin $p $list
    }

    foreach plugin [lsort [dict keys $byPlugin]] {
        # Pull out the gate from the first command in this plugin.
        set gate ""
        foreach cmd [dict get $byPlugin $plugin] {
            set g [dict get [dict get $cmds $cmd] gate]
            if {$g ne ""} then { set gate $g; break }
        }
        append out "### plugin: `$plugin`"
        if {$gate ne ""} then {
            append out " *(gate: `$gate`)*"
        }
        append out "\n\n"

        append out "| Command | Section | Sub-commands | Options |\n"
        append out "|---------|---------|--------------|---------|\n"
        foreach cmd [lsort [dict get $byPlugin $plugin]] {
            set entry [dict get $cmds $cmd]
            set sec  [dict get $entry section]
            set subs [dict get $entry subs]
            set opts [dict get $entry options]
            if {[llength $subs] == 0} then {
                set subList "&mdash;"
            } else {
                set subList ""
                foreach s [lsort $subs] {
                    if {$subList ne ""} then { append subList ", " }
                    append subList "`$s`"
                }
            }
            if {[llength $opts] == 0} then {
                set optList "&mdash;"
            } else {
                set optList ""
                foreach o $opts {
                    if {$optList ne ""} then { append optList ", " }
                    append optList "`$o`"
                }
            }
            append out "| `$cmd` | "
            if {$sec eq "?"} then {
                append out "*(unspecified)*"
            } else {
                append out "&sect;$sec"
            }
            append out " | $subList | $optList |\n"
        }
        append out "\n"
    }

    # ---- Master alphabetical index ----
    append out "---\n\n## Part 2 --- Master Alphabetical Index\n\n"
    append out "| Command | Plugin | Section |\n"
    append out "|---------|--------|---------|\n"
    foreach cmd [lsort [dict keys $cmds]] {
        set entry [dict get $cmds $cmd]
        set plugin [dict get $entry plugin]
        set sec    [dict get $entry section]
        append out "| `$cmd` | `$plugin` | "
        if {$sec eq "?"} then {
            append out "*(unspecified)*"
        } else {
            append out "&sect;$sec"
        }
        append out " |\n"
    }

    # ---- Master option index (each option -> commands that use it) ----
    append out "---\n\n## Part 3 --- Master Option Index\n\n"
    append out "Every option (switch) named in any command's R-marker block,\n"
    append out "with the commands that accept it.  An option appearing under\n"
    append out "more than one command is listed once with all owners.\n\n"

    set byOption [dict create]
    dict for {cmd entry} $cmds {
        foreach o [dict get $entry options] {
            if {![dict exists $byOption $o]} then {
                dict set byOption $o [list]
            }
            set list [dict get $byOption $o]
            lappend list $cmd
            dict set byOption $o $list
        }
    }

    if {[dict size $byOption] == 0} then {
        append out "*(no options harvested)*\n"
    } else {
        append out "| Option | Commands |\n"
        append out "|--------|----------|\n"
        foreach o [lsort [dict keys $byOption]] {
            set list [lsort [dict get $byOption $o]]
            set cmdList ""
            foreach c $list {
                if {$cmdList ne ""} then { append cmdList ", " }
                append cmdList "`$c`"
            }
            append out "| `$o` | $cmdList |\n"
        }
    }

    # Footer with stamp.
    append out "\n---\n\n"
    append out "*Generated by `tools/mkindex.tcl`.*\n"
    return $out
}


###############################################################################
#
# main --
#
###############################################################################

proc main {argv} {
    parse_args $argv

    set standardPath [file join $::OPT(root) \
            docs public tcl_language_standard_v1.md]
    if {![file readable $standardPath]} then {
        puts stderr "mkindex: cannot read $standardPath"
        exit 2
    }

    set standardEntries [parse_standard_commands $standardPath]
    set pluginEntries   [parse_plugin_commands $::OPT(root)]
    set standardOptions [parse_standard_options $standardPath]
    set cmds [build_command_table \
            $standardEntries $pluginEntries $standardOptions]

    if {$::OPT(verbose)} then {
        set totalOpts 0
        dict for {_ list} $standardOptions {
            incr totalOpts [llength $list]
        }
        puts stderr "mkindex: standard yielded [llength $standardEntries] command/sub-command headings"
        puts stderr "mkindex: plugins yielded   [llength $pluginEntries] command registrations"
        puts stderr "mkindex: standard yielded $totalOpts option occurrences across [dict size $standardOptions] commands"
        puts stderr "mkindex: combined table has [dict size $cmds] unique commands"
    }

    # Emit gap report on stderr.
    set gaps [list]
    dict for {cmd entry} $cmds {
        if {[dict get $entry section] eq "?"} then {
            lappend gaps "$cmd ([dict get $entry plugin])"
        }
    }
    if {[llength $gaps] > 0} then {
        puts stderr "mkindex: WARNING --- [llength $gaps] commands present in plugins but not specified in Part III:"
        foreach g [lsort $gaps] {
            puts stderr "    $g"
        }
    }

    set content [render_index $cmds]

    if {$::OPT(mode) eq "check"} then {
        if {![file readable $::OPT(output)]} then {
            puts stderr "mkindex: --check: $::OPT(output) does not exist"
            exit 1
        }
        set fd [open $::OPT(output) r]
        set existing [read $fd]
        close $fd
        if {$existing ne $content} then {
            puts stderr "mkindex: --check: $::OPT(output) is out of date.  Re-run mkindex.tcl."
            exit 1
        }
        puts stderr "mkindex: --check: ok"
        return
    }

    set fd [open $::OPT(output) w]
    puts -nonewline $fd $content
    close $fd
    puts stderr "mkindex: wrote $::OPT(output) ([dict size $cmds] commands across [llength [dict keys [build_byPlugin_dict $cmds]]] plugins)"
}


# Helper used only for the summary line.
proc build_byPlugin_dict {cmds} {
    set bp [dict create]
    dict for {cmd entry} $cmds {
        dict set bp [dict get $entry plugin] 1
    }
    return $bp
}


main $argv
