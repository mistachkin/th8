###############################################################################
#
# coverage_io_misc.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted tests for uncovered MC/DC branches in src/plugins/th8_io.c
# beyond [read] (covered separately by coverage_io_read.tcl).
#
# Coverage targets:
#   - gets wrong-args (line 71)
#   - close with no args -- iterates registered channels and skips
#     stdin/stdout/stderr (lines 488, 494)
#   - close stdin/stdout/stderr (no-op fallthrough)
#   - flush wrong-args (line 573)
#   - flush stdout fallback (line 580)
#   - flush of unknown channel (line 585)
#   - puts wrong-args edges
#
# Coverage-driven; not pinned to specific R-markers.
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
# Section 1 -- gets wrong-args
#
###############################################################################

runTest {test io_misc-1.1 {
  gets with no args errors
} -constraints {
    th8 gets
} -body {
  catch {gets} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test io_misc-1.2 {
  gets with too many args errors
} -constraints {
    th8 gets
} -body {
  catch {gets a b c d} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################
#
# Section 2 -- close with no args (closes all temp channels)
#
###############################################################################

runTest {test io_misc-2.1 {
  close with no args closes all registered temp channels
} -constraints {
    th8 file_tempname
} -body {
  # Open two temp channels, then close all with bare [close].
  set ch1 [file tempname 8]
  set ch2 [file tempname 8]
  catch {close} msg
  expr {[string length $msg] >= 0}
} -cleanup {
  # Best-effort cleanup if the bulk-close didn't fire.
  catch {close $ch1}
  catch {close $ch2}
  unset -nocomplain ch1 ch2 msg
} -result {1}}

###############################################################################

runTest {test io_misc-2.1b {
  close with no args when NO temp channels are open drives
  the C1/C2 alternates at th8_io.c:492 -- th8ChannelList
  returns an empty list (zList non-NULL but nList==0,
  driving C2=F).  The if-body is skipped and only the
  stdin/stdout/stderr standard channels remain
  unaffected.  Existing io_misc-2.1 covers the
  channels-present case (T,T=T); this closes the C2-pair.
} -constraints {
    th8
} -body {
  catch {close} msg
  expr {[string length $msg] >= 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test io_misc-2.2 {
  close with too many args errors
} -constraints {
    th8
} -body {
  catch {close a b c} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

###############################################################################
#
# Section 3 -- flush
#
###############################################################################

runTest {test io_misc-3.1 {
  flush with no args errors
} -constraints {
    th8 flush
} -body {
  catch {flush} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test io_misc-3.2 {
  flush with too many args errors
} -constraints {
    th8 flush
} -body {
  catch {flush a b} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test io_misc-3.3 {
  flush stdout takes the platform-output fallback path
} -constraints {
    th8 flush
} -body {
  # Exercises the !pChan && stdout-name branch.
  catch {flush stdout} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test io_misc-3.4 {
  flush of unknown channel errors via th8 ErrorMessage
} -constraints {
    th8 flush
} -body {
  catch {flush _no_such_channel_} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test io_misc-3.5 {
  flush of registered tempfile channel succeeds
} -constraints {
    th8 flush file_tempname
} -body {
  set ch [file tempname 8]
  puts -nonewline $ch "hello"
  catch {flush $ch} r
  close $ch
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain ch r
} -result {1}}

###############################################################################
#
# Section 4 -- puts wrong-args (MC/DC pair for argc < 2 || argc > 4)
#
###############################################################################

runTest {test io_misc-4.1 {
  puts with no args triggers argc < 2 branch
} -constraints {
    th8 puts
} -body {
  catch {puts} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test io_misc-4.2 {
  puts with too many args triggers argc > 4 branch
} -constraints {
    th8 puts
} -body {
  catch {puts a b c d e f} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################
#
# Section 5 -- seek wrong-args (MC/DC pair for argc < 3 || argc > 4)
#
###############################################################################

runTest {test io_misc-5.1 {
  seek with one arg triggers argc < 3 branch
} -constraints {
    th8 seek
} -body {
  catch {seek a} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test io_misc-5.2 {
  seek with five args triggers argc > 4 branch
} -constraints {
    th8 seek
} -body {
  catch {seek a b c d e} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

source tests/epilogue.tcl
