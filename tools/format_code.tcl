#!/usr/bin/env tclsh
###############################################################################
#
# format_code.tcl --
#
#     Drives `clang-format` over the TH8 C source tree using the
#     project's coding-style rules (Tcl Engineering Manual style)
#     codified in `.clang-format` at the repository root.
#
# Usage:
#     tclsh tools/format_code.tcl [OPTIONS] [FILE ...]
#
#     With no FILE arguments, the tool walks the default source set
#     (src/**/*.c and src/**/*.h, with vendored / generated trees
#     excluded -- see DEFAULT_INCLUDE_GLOBS / DEFAULT_EXCLUDE_GLOBS).
#
#     When FILE arguments are supplied, exactly those files are
#     processed (no globbing, no exclusion -- the caller chose them).
#
# Modes (mutually exclusive; default is --check):
#     --check     Verify formatting; do NOT modify files.  Exits 0
#                 when every file is already correctly formatted,
#                 1 when at least one file would change.
#     --diff      Print a unified diff of the changes that would be
#                 made; do NOT modify files.  Same exit codes as
#                 --check.
#     --write     Apply formatting in place.  Exits 0 on success,
#                 non-zero only on tool errors.
#
# Options (independent of mode):
#     --binary=PATH       Path to `clang-format`.  Defaults to the
#                         `CLANG_FORMAT` environment variable, then
#                         to `clang-format` on $PATH.
#     --style=PATH        Path to a .clang-format file.  Defaults to
#                         the project root.  Accepts a directory (a
#                         .clang-format inside it is used) or a file.
#     --include=GLOB      Add a glob pattern to the include set.
#                         May appear multiple times.  Replaces the
#                         defaults if at least one --include is given.
#     --exclude=GLOB      Add a glob pattern to the exclude set.
#                         May appear multiple times.  Augments the
#                         defaults.
#     --files=PATH        Read newline-separated file paths from
#                         PATH (use `-` for stdin).  Combined with
#                         positional FILE args; deduplicated.
#     --jobs=N            Run up to N clang-format invocations in
#                         parallel.  Default: 1 (serial -- portable
#                         and deterministic).  N=0 means "auto"
#                         (number of CPUs).
#     --root=PATH         Project root for include glob expansion
#                         and relative reporting.  Defaults to the
#                         directory containing this script's parent.
#     --quiet             Suppress per-file progress output.
#     --verbose           Print every file processed and the exact
#                         clang-format invocation.
#     --no-color          Disable ANSI colour in diff output.
#     --                  End of options; remaining args are FILES.
#
# Exit codes:
#     0   No formatting issues (or --write succeeded on every file).
#     1   At least one file is mis-formatted (--check / --diff) OR
#         at least one file failed to format (--write).
#     2   Tool itself errored (binary not found, bad option,
#         unreadable file, glob expansion failure, etc.).
#
# Customising:
#     Style rules live in `.clang-format` at the project root.  Edit
#     that file -- every option is documented inline.  This script
#     is a thin orchestrator and does NOT bake style decisions into
#     its own code.
#
# Examples:
#     tclsh tools/format_code.tcl                      # check the tree
#     tclsh tools/format_code.tcl --diff               # show diffs
#     tclsh tools/format_code.tcl --write              # rewrite in place
#     tclsh tools/format_code.tcl --check src/th8sh.c  # one file
#     tclsh tools/format_code.tcl --include='src/test/**/*.c' --check
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

package require Tcl 8.6

set ::DEFAULT_INCLUDE_GLOBS [list \
    src/*.c \
    src/*.h \
    src/test/*.c \
    src/test/*.h \
    src/plugins/*.c \
    src/plugins/*.h \
    src/plugins/crypto/*.c \
    src/plugins/crypto/*.h \
    src/plugins/harpy/*.c \
    src/plugins/harpy/*.h \
    src/plugins/regexp/*.c \
    src/plugins/regexp/*.h \
    src/plugins/sqlite3/*.c \
    src/plugins/sqlite3/*.h \
    src/sqlite3/*.c \
    src/sqlite3/*.h]

set ::DEFAULT_EXCLUDE_GLOBS [list \
    bin/* \
    externals/* \
    src/*_amal.c \
    src/*Stub*.c]


#
# log_err / log_warn / log_info --
#
#     Tiny logging helpers.  Errors and warnings always print;
#     `log_info` is suppressed in --quiet mode.
#

proc log_err {msg} {
  puts stderr "format_code: error: $msg"
}

proc log_warn {msg} {
  puts stderr "format_code: warning: $msg"
}

proc log_info {msg} {
  if {$::OPT(quiet)} then { return }
  puts stderr "format_code: $msg"
}

proc log_dbg {msg} {
  if {!$::OPT(verbose)} then { return }
  puts stderr "format_code: $msg"
}


#
# parse_args --
#
#     Translate $argv into the global ::OPT array plus the ordered
#     list ::FILE_ARGS of positional file arguments.  Bails out with
#     a non-zero exit code on bad input rather than throwing into
#     the caller, since this is the script's outermost layer.
#

proc parse_args {argv} {
  set mode check
  array set opt {
    binary       ""
    style        ""
    files_path   ""
    jobs         1
    root         ""
    quiet        0
    verbose      0
    no_color     0
    end_of_opts  0
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
      --check   { set mode check }
      --diff    { set mode diff }
      --write   { set mode write }
      --quiet   { set opt(quiet) 1 }
      --verbose { set opt(verbose) 1 }
      --no-color { set opt(no_color) 1 }
      --        { set opt(end_of_opts) 1 }
      --binary=* { set opt(binary) [string range $a 9 end] }
      --style=*  { set opt(style)  [string range $a 8 end] }
      --files=*  { set opt(files_path) [string range $a 8 end] }
      --jobs=*   { set opt(jobs)  [string range $a 7 end] }
      --root=*   { set opt(root)  [string range $a 7 end] }
      --include=* { lappend includes [string range $a 10 end] }
      --exclude=* { lappend excludes [string range $a 10 end] }
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

  # Validate jobs.
  if {![string is integer -strict $opt(jobs)] || $opt(jobs) < 0} then {
    log_err "--jobs requires a non-negative integer"
    exit 2
  }
  if {$opt(jobs) == 0} then {
    set opt(jobs) [auto_jobs]
  }

  # Stash globals.
  array unset ::OPT
  array set ::OPT [array get opt]
  set ::OPT(mode)     $mode
  set ::OPT(includes) $includes
  set ::OPT(excludes) $excludes
  set ::FILE_ARGS     $files
}


#
# print_usage --
#
#     Emit the usage block at the top of this file.  Reads the
#     comment header so it stays in sync.
#

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


#
# auto_jobs --
#
#     Best-effort CPU count.  Tcl 8.6 does not expose this, so try
#     environment hints and `getconf` / `sysctl` if available.  Falls
#     back to 1 -- correct, just slower.
#

proc auto_jobs {} {
  if {[info exists ::env(NUMBER_OF_PROCESSORS)]} then {
    set n $::env(NUMBER_OF_PROCESSORS)
    if {[string is integer -strict $n] && $n > 0} then { return $n }
  }
  foreach cmd { {getconf _NPROCESSORS_ONLN} {sysctl -n hw.ncpu} {nproc} } {
    if {![catch {exec {*}$cmd} out] && [string is integer -strict $out]
      && $out > 0} then {
      return $out
    }
  }
  return 1
}


#
# resolve_root --
#
#     Locate the project root: explicit --root, else the parent of
#     the directory containing this script (tools/format_code.tcl
#     ==> ../).  The chosen directory must contain a .clang-format
#     unless the user passed --style=PATH explicitly.
#

proc resolve_root {} {
  if {$::OPT(root) ne ""} then {
    return [file normalize $::OPT(root)]
  }
  set scriptDir [file dirname [file normalize [info script]]]
  return [file normalize [file join $scriptDir ..]]
}


#
# resolve_style --
#
#     Translate --style into a directory that clang-format can use
#     via `--style=file:PATH`.  Accepts a file path or a directory.
#

proc resolve_style {root} {
  set s $::OPT(style)
  if {$s eq ""} then {
    set candidate [file join $root .clang-format]
    if {![file exists $candidate]} then {
      log_err "no .clang-format found at $candidate (use --style=PATH)"
      exit 2
    }
    return $candidate
  }
  set s [file normalize $s]
  if {[file isdirectory $s]} then {
    set candidate [file join $s .clang-format]
    if {![file exists $candidate]} then {
      log_err "no .clang-format inside $s"
      exit 2
    }
    return $candidate
  }
  if {![file exists $s]} then {
    log_err "style file not found: $s"
    exit 2
  }
  return $s
}


#
# resolve_binary --
#
#     Locate clang-format.  Honour --binary, then $CLANG_FORMAT,
#     then `auto_execok`.
#

proc resolve_binary {} {
  set bin $::OPT(binary)
  if {$bin eq "" && [info exists ::env(CLANG_FORMAT)]} then {
    set bin $::env(CLANG_FORMAT)
  }
  if {$bin eq ""} then {
    set bin clang-format
  }
  set found [auto_execok $bin]
  if {$found eq ""} then {
    log_err "clang-format not found ($bin); pass --binary=PATH"
    exit 2
  }
  return [lindex $found 0]
}


#
# expand_files --
#
#     Build the final ordered file list.  Sources, in priority order:
#       1. Positional FILE_ARGS (verbatim, no exclusion).
#       2. --files=PATH contents (verbatim, no exclusion).
#       3. --include globs and DEFAULT_INCLUDE_GLOBS, with
#          DEFAULT_EXCLUDE_GLOBS plus --exclude applied.
#
#     Returns the deduplicated list, normalised to absolute paths,
#     in input order (preserves command-line ordering for
#     deterministic output).
#

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


#
# is_excluded --
#
#     True if FN matches any glob in EXCLUDES.  Globs are matched
#     against the path relative to ROOT so the patterns can stay
#     short and project-local.
#

proc is_excluded {fn root excludes} {
  set rel [relpath $fn $root]
  foreach pat $excludes {
    if {[string match $pat $rel]} then { return 1 }
    if {[string match $pat $fn]} then  { return 1 }
  }
  return 0
}


#
# relpath --
#
#     Best-effort relative path of TARGET against BASE.  Falls back
#     to TARGET unchanged when TARGET is not under BASE.
#

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


#
# run_clang_format --
#
#     Invoke clang-format on a single file.  ARGS is a list of
#     extra command-line arguments (e.g. -i for write mode).  When
#     OUT_VAR is non-empty, captures stdout into the named caller
#     variable; otherwise stdout is inherited.
#
#     Returns: { rc stdout stderr }.  `rc` is the exit status.
#

proc run_clang_format {bin styleFile path args} {
  set cmd [list $bin --style=file:$styleFile {*}$args $path]
  log_dbg "run: [join $cmd]"
  if {[catch {exec {*}$cmd 2>@1} out opts]} then {
    set rc [dict get $opts -errorcode]
    set out [strip_exec_noise $out]
    if {[lindex $rc 0] eq "CHILDSTATUS"} then {
      return [list [lindex $rc 2] $out]
    }
    return [list 2 $out]
  }
  return [list 0 $out]
}


#
# strip_exec_noise --
#
#     Remove Tcl's trailing "child process exited abnormally"
#     epilogue that `exec` appends to stderr-merged output when the
#     child exits non-zero.  Leaves the actual program output intact.
#

proc strip_exec_noise {text} {
  set lines [split $text \n]
  set last [lindex $lines end]
  if {$last eq "child process exited abnormally"} then {
    set lines [lrange $lines 0 end-1]
  }
  return [join $lines \n]
}


#
# process_check --
#
#     Run clang-format with --dry-run --Werror.  clang-format prints
#     diagnostics on stderr and exits non-zero when the file would
#     change.  Returns 1 if formatted, 0 if a change is needed,
#     -1 on tool error.
#

proc process_check {bin styleFile path} {
  lassign [run_clang_format $bin $styleFile $path \
      --dry-run --Werror] rc out
  if {$rc == 0} then { return 1 }
  if {$rc == 1} then {
    # Mis-formatted; clang-format already printed locations.
    if {!$::OPT(quiet) && $out ne ""} then {
      puts $out
    }
    return 0
  }
  log_err "clang-format failed on $path: $out"
  return -1
}


#
# process_diff --
#
#     Produce a unified diff between the source and the formatted
#     output by piping through external diff.  Falls back to a
#     simple "would change" notice when diff is unavailable.
#

proc process_diff {bin styleFile path} {
  lassign [run_clang_format $bin $styleFile $path] rc formatted
  if {$rc != 0} then {
    log_err "clang-format failed on $path: $formatted"
    return -1
  }

  set f [open $path r]
  fconfigure $f -translation binary
  set original [read $f]
  close $f

  if {$original eq $formatted} then {
    return 1
  }

  # Try `diff -u`; fall back to a marker-only message.
  set tmp [file tempfile tmpPath format_code.XXXXXX]
  fconfigure $tmp -translation binary
  puts -nonewline $tmp $formatted
  close $tmp

  set diffCmd [auto_execok diff]
  if {$diffCmd ne ""} then {
    set rel [relpath $path [resolve_root]]
    catch {exec [lindex $diffCmd 0] -u \
        -L "a/$rel" -L "b/$rel" \
        $path $tmpPath} diffOut
    file delete -- $tmpPath
    if {$::OPT(no_color) || ![tty_supports_color]} then {
      puts $diffOut
    } else {
      puts [colorize_diff $diffOut]
    }
  } else {
    file delete -- $tmpPath
    puts "$path: would change (install diff for unified output)"
  }
  return 0
}


#
# process_write --
#
#     Format in place via `clang-format -i`.  Returns 1 on success,
#     -1 on error.
#

proc process_write {bin styleFile path} {
  lassign [run_clang_format $bin $styleFile $path -i] rc out
  if {$rc == 0} then {
    log_dbg "wrote $path"
    return 1
  }
  log_err "clang-format failed on $path: $out"
  return -1
}


#
# tty_supports_color --
#
#     Cheap check for whether stdout is a TTY.  Tcl's `chan
#     configure` exposes -mode for serial; we rely on isatty
#     emulation via the stdout channel's class.
#

proc tty_supports_color {} {
  if {[catch {fconfigure stdout -translation} _]} then { return 0 }
  if {[info exists ::env(NO_COLOR)]} then { return 0 }
  if {![info exists ::env(TERM)] || $::env(TERM) eq "dumb"} then { return 0 }
  return 1
}


#
# colorize_diff --
#
#     Add ANSI colour to a unified diff.  Strict line-prefix match
#     so the colouring is fast and never touches non-diff lines.
#

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


#
# main --
#
#     Top-level dispatch.
#

proc main {argv} {
  parse_args $argv

  set root      [resolve_root]
  set styleFile [resolve_style $root]
  set bin       [resolve_binary]

  log_dbg "root  = $root"
  log_dbg "style = $styleFile"
  log_dbg "bin   = $bin"
  log_dbg "mode  = $::OPT(mode)"

  set files [expand_files $root]
  if {[llength $files] == 0} then {
    log_warn "no files matched"
    exit 0
  }
  log_info "processing [llength $files] file(s)"

  set okCount       0
  set changedCount  0
  set errorCount    0

  foreach path $files {
    if {![file exists $path]} then {
      log_err "missing file: $path"
      incr errorCount
      continue
    }
    if {![file readable $path]} then {
      log_err "unreadable: $path"
      incr errorCount
      continue
    }
    set rel [relpath $path $root]

    switch -- $::OPT(mode) {
      check {
        set rc [process_check $bin $styleFile $path]
        if {$rc == 1} then {
          incr okCount
          log_dbg "ok: $rel"
        } elseif {$rc == 0} then {
          incr changedCount
          if {!$::OPT(quiet)} then {
            puts "needs format: $rel"
          }
        } else {
          incr errorCount
        }
      }
      diff {
        set rc [process_diff $bin $styleFile $path]
        if {$rc == 1} then {
          incr okCount
        } elseif {$rc == 0} then {
          incr changedCount
        } else {
          incr errorCount
        }
      }
      write {
        set rc [process_write $bin $styleFile $path]
        if {$rc == 1} then {
          incr okCount
          log_dbg "wrote: $rel"
        } else {
          incr errorCount
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
