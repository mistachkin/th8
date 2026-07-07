###############################################################################
#
# base64.tcl --
#
# Tcl Language Standard
# Conformance Test File
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
# Section 1 -- base64: argument validation
#
###############################################################################

runTest {test base64-1.1 {
  base64: wrong number of arguments
} -constraints {
    base64
} -body {
  list [catch {base64} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {wrong # args: *}}}

###############################################################################

runTest {test base64-1.2 {
  base64: bad subcommand
} -constraints {
    base64
} -body {
  list [catch {base64 badcmd "x"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 {bad subcommand *}}}

###############################################################################
#
# Section 2 -- base64 encode: basic encoding
#
###############################################################################

runTest {test base64-2.1 {
  R-13975-09984: base64 encode produces standard RFC 4648 output
} -constraints {
    base64
} -body {
  base64 encode "Hello, World!"
} -result {SGVsbG8sIFdvcmxkIQ==}}

###############################################################################

runTest {test base64-2.2 {
  R-13975-09984: base64 encode with empty string
} -constraints {
    base64
} -body {
  base64 encode ""
} -result {}}

###############################################################################

runTest {test base64-2.3 {
  R-13975-09984: base64 encode with 1-byte input (two pads)
} -constraints {
    base64
} -body {
  base64 encode "a"
} -result {YQ==}}

###############################################################################

runTest {test base64-2.4 {
  R-13975-09984: base64 encode with 2-byte input (one pad)
} -constraints {
    base64
} -body {
  base64 encode "ab"
} -result {YWI=}}

###############################################################################

runTest {test base64-2.5 {
  R-13975-09984: base64 encode with 3-byte input (no pad)
} -constraints {
    base64
} -body {
  base64 encode "abc"
} -result {YWJj}}

###############################################################################

runTest {test base64-2.6 {
  R-13975-09984: base64 encode longer text
} -constraints {
    base64
} -body {
  base64 encode "The quick brown fox jumps over the lazy dog"
} -result {VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw==}}

###############################################################################

runTest {test base64-2.7 {
  R-13975-09984: base64 encode inserts CRLF every 76 chars (RFC 2045)
} -constraints {
    base64
} -setup {
  #
  # 57 bytes of input produce exactly 76 base64 chars (one full line).
  # 58 bytes produce 80 chars, requiring a line break.
  #
  set input [string repeat "A" 58]
} -body {
  set enc [base64 encode $input]
  #
  # The output should contain \r\n after the first 76 chars.
  #
  list [string length $enc] \
      [expr {[string index $enc 76] eq "\r"}] \
      [expr {[string index $enc 77] eq "\n"}]
} -cleanup {
  unset -nocomplain input enc
} -result {82 1 1}}

###############################################################################

runTest {test base64-2.8 {
  R-13975-09984: base64 encode no trailing CRLF
} -constraints {
    base64
} -body {
  set enc [base64 encode [string repeat "B" 57]]
  #
  # Exactly 76 chars: no trailing line break.
  #
  expr {[string length $enc] == 76
    && [string index $enc end] ne "\n"}
} -cleanup {
  unset -nocomplain enc
} -result {1}}

###############################################################################
#
# Section 3 -- base64 decode: basic decoding
#
###############################################################################

runTest {test base64-3.1 {
  R-27573-45023: base64 decode reverses encode
} -constraints {
    base64
} -body {
  base64 decode "SGVsbG8sIFdvcmxkIQ=="
} -result {Hello, World!}}

###############################################################################

runTest {test base64-3.2 {
  R-27573-45023: base64 decode with empty string
} -constraints {
    base64
} -body {
  base64 decode ""
} -result {}}

###############################################################################

runTest {test base64-3.3 {
  R-40774-56899: base64 decode ignores whitespace
} -constraints {
    base64
} -body {
  base64 decode "SGVs\nbG8="
} -result {Hello}}

###############################################################################

runTest {test base64-3.4 {
  R-10516-34894: base64 decode rejects invalid characters
} -constraints {
    base64
} -body {
  list [catch {base64 decode "SGVs!bG8="} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -result {1 {invalid base64 character}}}

###############################################################################
#
# Section 4 -- base64: round-trip verification
#
###############################################################################

runTest {test base64-4.1 {
  R-06825-47962: round-trip for all ASCII printable chars
} -constraints {
    base64
} -setup {
  set input ""
  for {set i 32} {$i < 127} {incr i} {
    append input [format %c $i]
  }
} -body {
  set encoded [base64 encode $input]
  set decoded [base64 decode $encoded]
  expr {$decoded eq $input}
} -cleanup {
  unset -nocomplain input encoded decoded i
} -result {1}}

###############################################################################

runTest {test base64-4.2 {
  R-06825-47962: round-trip for binary-like data (NUL bytes)
} -constraints {
    base64
} -body {
  set input "a\x00b\x00c"
  set encoded [base64 encode $input]
  set decoded [base64 decode $encoded]
  expr {$decoded eq $input}
} -cleanup {
  unset -nocomplain input encoded decoded
} -result {1}}

###############################################################################
#
# Section 5 -- base64: 8-bit clean (binary data)
#
###############################################################################

runTest {test base64-5.1 {
  R-06825-47962: round-trip high bytes (8-bit clean)
} -constraints {
    base64
} -body {
  set input "\x12\x90\xFC\xFF"
  set encoded [base64 encode $input]
  set decoded [base64 decode $encoded]
  expr {$decoded eq $input}
} -cleanup {
  unset -nocomplain input encoded decoded
} -result {1}}

###############################################################################

runTest {test base64-5.2 {
  R-06825-47962: round-trip all 256 byte values
} -constraints {
    base64
} -body {
  set input ""
  for {set i 0} {$i < 256} {incr i} {
    append input [format %c $i]
  }
  set encoded [base64 encode $input]
  set decoded [base64 decode $encoded]
  expr {$decoded eq $input}
} -cleanup {
  unset -nocomplain input encoded decoded i
} -result {1}}

###############################################################################

runTest {test base64-5.3 {
  R-06825-47962: encode of specific high-byte sequence
} -constraints {
    base64
} -body {
  base64 encode "\xFF\xFE\xFD"
} -result {//79}}

###############################################################################

runTest {test base64-5.4 {
  R-06825-47962: round-trip NUL + high bytes mixed
} -constraints {
    base64
} -body {
  set input "\x00\x01\x7F\x80\xFE\xFF"
  set encoded [base64 encode $input]
  set decoded [base64 decode $encoded]
  expr {$decoded eq $input}
} -cleanup {
  unset -nocomplain input encoded decoded
} -result {1}}

###############################################################################

runTest {test base64-5.5 {
  R-06825-47962: encode of single high byte
} -constraints {
    base64
} -body {
  base64 encode "\xFF"
} -result {/w==}}

###############################################################################
#
# Section 6 -- R-marker coverage: CRLF line wrapping per RFC 2045
#
###############################################################################

runTest {test base64-6.1 {
  R-53274-40319: base64 encode inserts CRLF every 76 characters per RFC 2045
} -constraints {
    base64
} -setup {
} -body {
  #
  # 114 bytes of input produce 152 base64 chars (two full 76-char
  # lines).  The output should have exactly one CRLF between
  # the two groups.
  #
  set input [string repeat "Z" 114]
  set enc [base64 encode $input]
  set lines [split $enc "\n"]
  #
  # After splitting on LF, the first element should end with CR
  # and be 77 chars (76 + CR), confirming CRLF insertion at
  # position 76.
  #
  list [string length [lindex $lines 0]] \
      [expr {[string index [lindex $lines 0] end] eq "\r"}] \
      [llength $lines]
} -cleanup {
  unset -nocomplain input enc lines
} -result {77 1 2}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
