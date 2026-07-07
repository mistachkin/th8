###############################################################################
#
# coverage_channel_reads.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_channel.c L372 and L373 -- the
# trailing-newline / trailing-carriage-return strip after a
# successful xInput read.  Existing channel tests in
# coverage8.tcl use [puts -nonewline] with no '\n', so the
# strip decisions only ever see (T, F) [n > 0, last byte
# NOT \n].  Reading content that ends with \n (or \r\n)
# drives the (T, T) vector closing both pairs.
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

runTest {test chanread-1.1 {
  Channel read of content ending with \n -- drives the
  (T, T) MC/DC vector at th8_channel.c L372 (the
  `nLine > 0 && zLine[nLine - 1] == '\n'` strip check).
} -constraints {
    file_tempname seek
} -setup {
} -body {
  set ch [file tempname 100]
  puts -nonewline $ch "hello\n"
  seek $ch 0 start
  set line [gets $ch]
  close $ch
  set line
} -cleanup {
  unset -nocomplain ch line
} -result {hello}}

###############################################################################

runTest {test chanread-1.2 {
  Channel read of content ending with \r\n -- drives the
  (T, T) vector at L373 (the `\r` strip).
} -constraints {
    file_tempname seek
} -setup {
} -body {
  set ch [file tempname 100]
  puts -nonewline $ch "abc\r\n"
  seek $ch 0 start
  set line [gets $ch]
  close $ch
  set line
} -cleanup {
  unset -nocomplain ch line
} -result {abc}}

###############################################################################
#
# F3 fault-injection extension (2026-05-29):
# -failChannelRead/-failChannelEOF/-failChannelWrite/-failChannelOpen
# are wired through Th8_FaultConfig and pt_xChannelControl, but
# the script-visible [file tempname] path uses in-memory
# channels (th8ChannelCreate, bypasses xChannelControl), and
# there is no [open] script command to obtain a filesystem
# channel.  Driving the new flags from script-level tests
# requires a testlib-side C helper that opens a real file via
# the internal channel API; not yet written.  C-level
# infrastructure is committed so future tests can land
# without further fault-layer changes.
###############################################################################

source tests/epilogue.tcl
