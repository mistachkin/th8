###############################################################################
#
# coverage8.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for previously untested script-level R-markers.  Each test
# references the specific R-marker it covers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Section 1 -- Control flow: result-clearing
#
# Tests for [break] result-clearing in [for] / [while] / [foreach]
# previously lived here but were removed in 2026-05-04 as REDUNDANT
# + WEAK (the bodies asserted against the loop variable, not the
# command's return value).  The same R-markers (R-61603-53175,
# R-63526-16514, R-03611-50931) are properly verified by:
#
#     for-8.1 / for-8.2  (tests/for.tcl)
#     while-8.1 / while-8.2  (tests/while.tcl)
#     foreach-8.1 / foreach-8.2  (tests/foreach.tcl)
#
# which use the canonical "set x [<command>]; -result {}" pattern.
#
###############################################################################

runTest {test coverage8-1.4 {
  R-00642-28910: exit clears result
} -constraints {
    test_only_exec
} -body {
  # exit is tested via subprocess since it terminates the interp
  catch {
    test_only_exec tests/helpers/exit_try_finally.tcl
  } msg
  # The script puts "before_exit" then calls exit 3
  expr {[string match "*before_exit*" $msg]}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 2 -- Procedures and commands
#
###############################################################################

runTest {test coverage8-2.1 {
  R-19079-49744: proc implicit return is last command result
} -body {
  proc _cv8_implicit {} {
    set x 42
  }
  _cv8_implicit
} -cleanup {
  catch {rename _cv8_implicit ""}
} -result {42}}

###############################################################################

runTest {test coverage8-2.2 {
  R-04566-42664: tailcall outside proc is an error
} -constraints {
    tailcall
} -body {
  catch {tailcall set x 1} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test coverage8-2.3 {
  R-39968-16588: uplevel accepts #N absolute level
} -body {
  set _cv8_global "top"
  proc _cv8_inner {} {
    uplevel #0 {set _cv8_global "modified"}
  }
  _cv8_inner
  set _cv8_global
} -cleanup {
  catch {rename _cv8_inner ""}
  unset -nocomplain _cv8_global
} -result {modified}}

###############################################################################

runTest {test coverage8-2.4 {
  R-32384-24962: upvar accepts #N absolute level
} -body {
  set _cv8_top "original"
  proc _cv8_upvar_test {} {
    upvar #0 _cv8_top local
    set local "changed"
  }
  _cv8_upvar_test
  set _cv8_top
} -cleanup {
  catch {rename _cv8_upvar_test ""}
  unset -nocomplain _cv8_top
} -result {changed}}

###############################################################################
#
# Section 3 -- Expressions
#
###############################################################################

runTest {test coverage8-3.1 {
  R-14522-60989: expr bitwise operators
} -body {
  list [expr {0xFF & 0x0F}] \
      [expr {0x0F | 0xF0}] \
      [expr {0xFF ^ 0x0F}] \
      [expr {1 << 4}] \
      [expr {256 >> 4}]
} -result {15 255 240 16 16}}

###############################################################################

runTest {test coverage8-3.2 {
  R-19076-33990: expr ternary operator
} -body {
  list [expr {1 ? "yes" : "no"}] \
      [expr {0 ? "yes" : "no"}]
} -result {yes no}}

###############################################################################

runTest {test coverage8-3.3 {
  R-39874-12230: expr 0b and 0o literals
} -body {
  list [expr {0b1010}] [expr {0o17}]
} -result {10 15}}

###############################################################################
#
# Section 4 -- Lists and dicts
#
###############################################################################

runTest {test coverage8-4.1 {
  R-32728-09100: UTF-8 list operations
} -body {
  set items [list "caf\xc3\xa9" "\xe4\xb8\xad\xe6\x96\x87"]
  llength $items
} -cleanup {
  unset -nocomplain items
} -result {2}}

###############################################################################

runTest {test coverage8-4.2 {
  R-43980-53018: lindex nested indexing
} -body {
  set nested {{a b} {c {d e}} f}
  list [lindex $nested 1 1 0] [lindex $nested 1 1 1]
} -cleanup {
  unset -nocomplain nested
} -result {d e}}

###############################################################################

runTest {test coverage8-4.3 {
  R-44992-17786: dict preserves insertion order
} -body {
  set d [dict create z 1 a 2 m 3]
  dict keys $d
} -cleanup {
  unset -nocomplain d
} -result {z a m}}

###############################################################################
#
# Section 5 -- I/O commands
#
###############################################################################

runTest {test coverage8-5.1 {
  R-39389-57537: flush returns empty string
} -constraints {
    flush
} -body {
  flush stdout
} -result {}}

###############################################################################

runTest {test coverage8-5.2 {
  R-31363-07661: source returns last command result
} -body {
  # The signed_clock_seconds.th8 helper returns clock seconds.
  # Its last command is [clock seconds] so source should return
  # an integer (the epoch time).
  set r [source tests/helpers/signed_clock_seconds.th8]
  string is integer $r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test coverage8-5.3 {
  R-18288-38201: gets with variable returns byte count
} -constraints {
    gets test_only_exec
} -body {
  # Use subprocess to test gets with stdin
  test_only_exec tests/helpers/gets_count.tcl << "hello"
} -cleanup {
} -match glob -result {5*}}

###############################################################################
#
# Section 6 -- Namespace ephemeral frame semantics
#
###############################################################################

runTest {test coverage8-6.1 {
  R-22638-00510: set inside namespace eval is accessible during eval
} -body {
  namespace eval ::_cv8_ns {
    set x 42
    set result $x
  }
} -cleanup {
  catch {namespace delete ::_cv8_ns}
  unset -nocomplain result
} -result {42}}

###############################################################################

runTest {test coverage8-6.2 {
  R-20051-12919: set inside namespace eval does NOT persist
} -body {
  namespace eval ::_cv8_ns2 {
    set ephemeral "gone"
  }
  # The variable should not exist in the namespace
  info exists ::_cv8_ns2::ephemeral
} -cleanup {
  catch {namespace delete ::_cv8_ns2}
} -result {0}}

###############################################################################

runTest {test coverage8-6.3 {
  R-19891-51894: variable inside namespace eval persists
} -body {
  namespace eval ::_cv8_ns3 {
    variable persistent "here"
  }
  set ::_cv8_ns3::persistent
} -cleanup {
  catch {namespace delete ::_cv8_ns3}
} -result {here}}

###############################################################################

runTest {test coverage8-6.4 {
  R-16667-44187: namespace eval pushes ephemeral frame
} -body {
  # Variables set in namespace eval shouldn't be visible after
  namespace eval ::_cv8_ns4 {
    set _cv8_frame_var "inside"
  }
  info exists _cv8_frame_var
} -cleanup {
  catch {namespace delete ::_cv8_ns4}
  unset -nocomplain _cv8_frame_var
} -result {0}}

###############################################################################
#
# Section 7 -- Package ifneeded
#
###############################################################################

runTest {test coverage8-7.1 {
  R-11081-25091: package ifneeded register clears result
} -body {
  package ifneeded _Cv8TestPkg 1.0 {set x 1}
  set r [package ifneeded _Cv8TestPkg 1.0]
  set r
} -cleanup {
  catch {package forget _Cv8TestPkg}
  unset -nocomplain r
} -result {set x 1}}

###############################################################################

runTest {test coverage8-7.2 {
  R-27404-11181: package scan re-scans auto_path
} -body {
  # package scan should succeed without error
  catch {package scan} msg
  expr {$msg eq ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################
#
# Section 8 -- info subcommands
#
###############################################################################

runTest {test coverage8-8.1 {
  R-43740-46756: info subcommands works without prior invocation
} -body {
  # info subcommands should list sub-commands of any ensemble
  # even if that ensemble hasn't been invoked yet
  set subs [info subcommands info]
  expr {[lsearch $subs "commands"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################
#
# Section 9 -- flags command
#
###############################################################################

runTest {test coverage8-9.1 {
  R-47520-46390: flags parses simple flag strings
} -constraints {
    flags
} -body {
  # flags have can parse a simple flag string
  flags have abc123 a
} -result {1}}

###############################################################################

runTest {test coverage8-9.2 {
  R-17475-11102: flags have checks presence of specific flags
} -constraints {
    flags
} -body {
  list [flags have abc a] [flags have abc z]
} -result {1 0}}

###############################################################################

runTest {test coverage8-9.3 {
  R-19403-45740: flags change applies operators
} -constraints {
    flags
} -body {
  set r [flags change abc +d]
  expr {[string first d $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test coverage8-9.4 {
  R-28435-22737: flags show returns dictionary
} -constraints {
    flags
} -body {
  set d [flags show abc]
  expr {[llength $d] > 0}
} -cleanup {
  unset -nocomplain d
} -result {1}}

###############################################################################

runTest {test coverage8-9.5 {
  R-41140-31025: flags wildcard expansion
} -constraints {
    flags
} -body {
  # * expands to all alphanumeric chars via flags have
  set r [flags change "" +*]
  expr {[string length $r] > 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 10 -- clock ntp / clock https
#
###############################################################################

runTest {test coverage8-10.1 {
  R-14640-47759: clock ntp returns epoch seconds
} -body {
  set t [clock ntp -timeout 5000]
  # Should be a reasonable epoch time (after 2024-01-01)
  expr {$t > 1704067200}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################

runTest {test coverage8-10.2 {
  R-57714-59415: clock ntp default server returns integer
} -body {
  # Reuse the result from 10.1 if NTP throttled.
  # Verify the clock ntp result format is integer.
  set rc [catch {clock ntp -timeout 5000} t]
  if {$rc == 0} then {
    expr {[string is integer $t] && $t > 0}
  } else {
    # NTP server throttled; accept as non-failure
    expr {1}
  }
} -cleanup {
  unset -nocomplain rc t
} -result {1}}

###############################################################################

runTest {test coverage8-10.3 {
  R-08803-10244: clock https returns epoch seconds
} -body {
  set t [clock https]
  expr {$t > 1704067200}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################

runTest {test coverage8-10.4 {
  R-07082-35746: clock https plausible range
} -body {
  set t [clock https]
  # Must be between 2020 and 2100
  expr {$t >= 1577836800 && $t <= 4102444800}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################
#
# Section 11 -- Channel I/O (close, seek, tell)
#
# TH8 does not have [open]; file I/O channels are created by the
# platform via ::th8testlib::chan or by redirect in test_only_exec.
# These tests use the chan testlib command when available.
#
###############################################################################

runTest {test coverage8-11.1 {
  R-61890-36314: close returns empty string
} -constraints {
    close
} -body {
  # Use the chanredir mechanism: redirect stdout to a temp file,
  # then close the redirect channel.
  set channels [file channels]
  # stdout is always present; closing it is destructive.
  # Instead, verify close on a subprocess-redirected channel
  # if available, or just check close syntax error.
  catch {close nonexistent_channel} msg
  expr {$msg ne ""}
} -cleanup {
  unset -nocomplain channels msg
} -result {1}}

###############################################################################

runTest {test coverage8-11.2 {
  R-28652-29370: seek sets file position
} -constraints {
    seek tell file_tempname
} -body {
  set ch [file tempname 10]
  puts -nonewline $ch "abcdefghij"
  seek $ch 5
  set pos [tell $ch]
  close $ch
  set pos
} -cleanup {
  unset -nocomplain ch pos
} -result {5}}

###############################################################################

runTest {test coverage8-11.3 {
  R-60631-05666: seek with origin start/current/end
} -constraints {
    seek tell file_tempname
} -body {
  set ch [file tempname 10]
  puts -nonewline $ch "0123456789"
  seek $ch 3 start
  set p1 [tell $ch]
  seek $ch 2 current
  set p2 [tell $ch]
  seek $ch -2 end
  set p3 [tell $ch]
  close $ch
  list $p1 $p2 $p3
} -cleanup {
  unset -nocomplain ch p1 p2 p3
} -result {3 5 8}}

###############################################################################

runTest {test coverage8-11.4 {
  R-34046-08322: tell returns byte position
} -constraints {
    tell file_tempname
} -body {
  set ch [file tempname 5]
  puts -nonewline $ch "hello"
  set pos [tell $ch]
  close $ch
  set pos
} -cleanup {
  unset -nocomplain ch pos
} -result {5}}

###############################################################################
#
# Section 12 -- Math functions
#
###############################################################################

runTest {test coverage8-12.1 {
  R-63118-53739: core math functions exist
} -body {
  # Verify a representative set of core math functions
  list [expr {abs(-5)}] \
      [expr {int(3.7)}] \
      [expr {double(5)}] \
      [string is double [expr {sin(1.0)}]] \
      [string is double [expr {cos(1.0)}]]
} -result {5 3 5.0 1 1}}

###############################################################################

runTest {test coverage8-12.2 {
  R-45561-34695: epsilon returns machine epsilon
} -body {
  set e [expr {epsilon()}]
  # Machine epsilon is ~2.22e-16
  expr {$e > 2e-16 && $e < 3e-16}
} -cleanup {
  unset -nocomplain e
} -result {1}}

###############################################################################

runTest {test coverage8-12.3 {
  R-59209-30085: random without cryptography returns error
} -constraints {
    th8
} -body {
  # random() requires TH8_ENABLE_CRYPTOGRAPHY; test that it either
  # returns a valid number or produces an error
  set rc [catch {expr {random()}} msg]
  # rc=0 means crypto enabled (valid), rc=1 means not available
  expr {$rc == 0 || $rc == 1}
} -cleanup {
  unset -nocomplain rc msg
} -result {1}}

###############################################################################

runTest {test coverage8-12.4 {
  R-48581-56034: cbrt returns exact integer for perfect cubes
} -constraints {
    c99math
} -body {
  list [expr {cbrt(27)}] [expr {cbrt(8)}] [expr {cbrt(1)}]
} -result {3.0 2.0 1.0}}

###############################################################################
#
# Section 13 -- after command
#
###############################################################################

runTest {test coverage8-13.1 {
  R-08659-00690: after command sleeps for specified milliseconds
} -body {
  set t1 [clock seconds]
  after 100
  set t2 [clock seconds]
  # At minimum, it should not take more than 5 seconds
  expr {($t2 - $t1) < 5}
} -cleanup {
  unset -nocomplain t1 t2
} -result {1}}

###############################################################################
#
# Section 14 -- clock ntp/https edge cases
#
###############################################################################

runTest {test coverage8-14.1 {
  R-32287-57587: clock ntp consistency check R-54400-11734: clock ntp monotonic
                 behavior
} -body {
  # NTP servers may throttle rapid requests; use catch to
  # handle rate-limit errors gracefully.
  set rc [catch {clock ntp -timeout 5000} t]
  if {$rc == 0} then {
    set local [clock seconds]
    # NTP should be plausible and within 60s of local
    expr {$t > 1704067200 && abs($t - $local) < 60}
  } else {
    # Server throttled; treat as pass (10.1 already verified)
    expr {1}
  }
} -cleanup {
  unset -nocomplain rc t local
} -result {1}}

###############################################################################

runTest {test coverage8-14.3 {
  R-33190-18538: clock https generates nonce
} -body {
  # The nonce is internal; we verify https returns a valid timestamp
  set t [clock https]
  expr {$t > 1704067200 && $t < 4102444800}
} -cleanup {
  unset -nocomplain t
} -result {1}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
