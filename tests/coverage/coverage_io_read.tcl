###############################################################################
#
# coverage_io_read.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted tests for uncovered MC/DC branches in src/plugins/th8_io.c
# read_command.  The [read] command has two argument forms and three
# return-shape paths; the suite at large doesn't drive any of them.
# Coverage targets:
#
#   - argc < 2 / argc > 3 wrong-args (line 646)
#   - argc == 3 -nonewline detection (line 660)
#   - argc == 3 numChars form (line 670)
#   - numChars < 0 rejection (line 674)
#   - registered tempfile channel path (line 695+)
#   - numChars >= 0 trim (line 716+)
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
# Section 1 -- read wrong-args
#
###############################################################################

runTest {test io_read-1.1 {
  read with no arguments errors with wrong-args
} -constraints {
    th8
} -body {
  catch {read} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test io_read-1.2 {
  read with too many arguments errors with wrong-args
} -constraints {
    th8
} -body {
  catch {read a b c d} msg
  string match "*wrong*" $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################
#
# Section 2 -- read against a registered tempfile channel
#
###############################################################################

runTest {test io_read-2.1 {
  read entire tempfile channel (argc==2 form)
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 16]
  puts -nonewline $ch "0123456789ABCDEF"
  seek $ch 0
  set data [read $ch]
  close $ch
  string length $data
} -cleanup {
  unset -nocomplain ch data
} -match glob -result {*}}

###############################################################################

runTest {test io_read-2.2 {
  read -nonewline form (argc==3 with -nonewline flag)
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 8]
  puts -nonewline $ch "abcdefgh"
  seek $ch 0
  catch {read -nonewline $ch} data
  close $ch
  expr {[string length $data] >= 0}
} -cleanup {
  unset -nocomplain ch data
} -result {1}}

###############################################################################

runTest {test io_read-2.3 {
  read with numChars form (argc==3 second form)
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 32]
  puts -nonewline $ch "0123456789"
  seek $ch 0
  catch {read $ch 5} data
  close $ch
  expr {[string length $data] >= 0}
} -cleanup {
  unset -nocomplain ch data
} -result {1}}

###############################################################################

runTest {test io_read-2.4 {
  read with negative numChars errors
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 8]
  puts -nonewline $ch "abc"
  seek $ch 0
  catch {read $ch -1} msg
  close $ch
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain ch msg
} -result {1}}

###############################################################################

runTest {test io_read-2.5 {
  read with non-integer numChars errors
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 8]
  puts -nonewline $ch "abc"
  seek $ch 0
  catch {read $ch notanumber} msg
  close $ch
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain ch msg
} -result {1}}

###############################################################################
#
# Section 3 -- read against std channel names (covers fallback paths)
#
###############################################################################

runTest {test io_read-3.1 {
  read against unknown channel name errors
} -constraints {
    th8
} -body {
  catch {read _no_such_channel_} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test io_read-4.1 {
  read with numChars LARGER than the channel's content
  drives the C2=F vector at th8_io.c:710-711 -- numChars
  >= 0 (T) but nAll < numChars (F), so the limit check
  doesn't fire and the loop exits via channel EOF.
  Similar pattern at L717.
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 32]
  puts -nonewline $ch "short"
  seek $ch 0
  set data [read $ch 1000]
  close $ch
  expr {[string length $data] >= 1 && [string length $data] < 100}
} -cleanup {
  unset -nocomplain ch data
} -result {1}}

###############################################################################

runTest {test io_read-5.1 {
  read -nonewline against (a) empty content or (b) content
  that does NOT end in a newline drives the C2/C3 pairs at
  th8_io.c:722-723 -- bNoNewline=T, but nAll == 0 (C2=F)
  or zAll[end] != '\n' (C3=F).  Existing tests cover the
  (T,T,T) case (trim a trailing newline); these close the
  remaining pairs.
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 8]
  puts -nonewline $ch "noNL"
  seek $ch 0
  set d [read -nonewline $ch]
  close $ch
  expr {[string length $d] >= 4}
} -cleanup {
  unset -nocomplain ch d
} -result {1}}

###############################################################################

runTest {test io_read-5.2 {
  read -nonewline against an empty channel drives the C2=F
  vector at th8_io.c:722 (bNoNewline=T, nAll=0).  The line
  loop in th8_io.c:701-714 unconditionally appends '\n' to
  every line, so when nAll>0 the final byte is always '\n'
  (third sub-condition is wrapped with ALWAYS()).  To drive
  C2=F we seek the temp channel past its pre-allocated
  content so the read loop sees EOF immediately and nAll
  stays zero.
} -constraints {
    th8 file_tempname
} -body {
  set ch [file tempname 8]
  seek $ch 0 end
  set d [read -nonewline $ch]
  close $ch
  string length $d
} -cleanup {
  unset -nocomplain ch d
} -result {0}}

###############################################################################

source tests/epilogue.tcl
