###############################################################################
#
# exec.tcl --
#
# Tcl Language Standard
# Test Suite Infrastructure Package File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::th8test {
  #
  # test_only_exec --
  #
  #	Execute a Tcl script file in a subprocess for testing.
  #	Returns the combined stdout+stderr output.  Raises an
  #	error if the subprocess exits with a non-zero code (the
  #	error message contains the captured output).
  #
  #	When the C-level __test_only_exec command is available
  #	(from the TH8 test library), it is used for subprocess
  #	execution.  This provides secure quoting via execvp
  #	(POSIX) or CreateProcessW (Win32) with no shell.
  #
  #	Otherwise, falls back to Tcl's [exec] command with
  #	list-based construction for safe argument quoting.
  #
  #	Security:
  #	  - Validates the interpreter executable path is non-empty.
  #	  - Normalizes the script path for cross-platform safety
  #	    (Win32 backslash/space handling, relative components).
  #	  - Verifies the script file exists and is a regular file.
  #	  - Uses list-based command construction throughout to
  #	    prevent argument injection on both POSIX and Win32.
  #	  - The "--" separator prevents the executable path from
  #	    being misinterpreted as an [exec] option (important
  #	    on Win32 where paths may start with "/" or "-").
  #
  #	Usage: test_only_exec scriptFile ?arg ...?
  #
  #	Additional arguments (including [exec] redirects such
  #	as << data) are passed through verbatim.
  #
  #	Examples:
  #	  test_only_exec tests/helpers/cancel_basic.tcl
  #	  test_only_exec tests/helpers/gets_novar.tcl << "hello\n"
  #
  proc test_only_exec { args } {
    if {[llength $args] < 1} then {
      error "wrong # args: should be \"test_only_exec scriptFile ?arg ...?\""
    }

    #
    # Validate the interpreter executable path.  On Win32
    # this may contain spaces (e.g. "C:\Program Files\...")
    # which is handled by list-based command construction
    # below.  An empty path would silently produce a broken
    # subprocess invocation, so reject it early.
    #
    set exe [info nameofexecutable]

    if {$exe eq ""} then {
      error "test_only_exec: no process executable path"
    }

    set script [lindex $args 0]

    #
    # Normalize the script path for cross-platform safety.
    # On Win32 this resolves forward/backslash mixing,
    # relative path components (. and ..), and 8.3 short-
    # name expansion.  On POSIX it resolves symlinks and
    # collapses redundant separators.
    #
    # Falls back to the raw path when [file normalize] is
    # not available (e.g. minimal TH8 builds).
    #
    if {[catch {file normalize $script} normScript] == 0} then {
      set script $normScript
    }

    #
    # Verify the script file exists and is a regular file.
    # This catches typos and path-traversal mistakes with
    # a clear diagnostic instead of a cryptic subprocess
    # failure.
    #
    if {![file exists $script]} then {
      error [appendArgs \
          "test_only_exec: script file does not exist: \"" $script \"]
    }

    if {[catch {file isfile $script} _isFile] == 0 && !$_isFile} then {
      error [appendArgs \
          "test_only_exec: path is not a regular file: \"" $script \"]
    }

    set scriptArgs [lrange $args 1 end]

    #
    # Preferred path: delegate to the C-level command which
    # uses execvp (POSIX) / CreateProcessW (Win32) directly.
    # No shell is involved, so no metacharacter injection is
    # possible.  The C command handles its own stderr merge
    # and trailing-newline stripping.
    #
    #
    # If __test_only_exec was removed by a testUnloadLib cycle,
    # try to restore it by re-loading the C testlib.
    #
    if {[llength [info commands __test_only_exec]] == 0 && \
        [info exists ::testlib_name] && $::testlib_name ne ""} then {
      catch {load $::testlib_name}
    }

    if {[llength [info commands __test_only_exec]] > 0} then {
      #
      # Extract any << redirect from the arguments.  The
      # C command accepts -stdin data instead.
      #
      set stdinData ""
      set haveStdin 0
      set passArgs [list]
      set i 0

      while {$i < [llength $scriptArgs]} {
        set a [lindex $scriptArgs $i]
        if {$a eq "<<"} then {
          incr i
          if {$i < [llength $scriptArgs]} then {
            set stdinData [lindex $scriptArgs $i]
            set haveStdin 1
          }
        } else {
          lappend passArgs $a
        }
        incr i
      }

      set cmd [list __test_only_exec]

      if {$haveStdin} then {
        lappend cmd -stdin $stdinData
      }

      lappend cmd $exe $script

      foreach a $passArgs {
        lappend cmd $a
      }

      return [uplevel 1 $cmd]
    }

    #
    # Fallback: use Tcl's [exec] with list-based command
    # construction.  Each argument is appended as a discrete
    # list element so that spaces, backslashes, and other
    # metacharacters in paths or data are never misinterpreted
    # by [exec] or the underlying OS process-creation API.
    #
    # Remaining arguments (e.g. << data, < file) pass
    # through as-is; [exec] interprets them as redirects.
    #
    if {[llength [info commands exec]] == 0} then {
      error "test_only_exec: no \[exec\] subsystem available"
    }

    set cmd [list exec -- $exe $script]
    foreach a $scriptArgs {lappend cmd $a}

    #
    # Redirect stderr into the result so the caller sees
    # error messages from the subprocess.
    #
    lappend cmd 2>@1

    return [uplevel 1 $cmd]
  }

  namespace export test_only_exec
  namespace eval :: {namespace import -force ::th8test::*}

  package provide th8test_exec 1.0
}
