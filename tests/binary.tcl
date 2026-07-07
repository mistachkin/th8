###############################################################################
#
# binary.tcl --
#
# Conformance test file for `[binary format]` and `[binary scan]`.
# Phase 1 (per doc/internal/binary.md) covers the integer
# specifiers only -- c, s, S, t, i, I, n, w, W, m.
#
# These tests do not yet carry R-markers because the binary
# ensemble has not been added to the standard.  Once the
# Tcl/TH8 standard incorporates the binary command spec, the
# tests below will be annotated with R-markers via mkreq.tcl.
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
# Section 1 -- ensemble structure and error reporting
#
###############################################################################

runTest {test binary-1.1 {
  R-42906-22341: binary with no subcommand reports wrong-args
} -constraints {
    th8
} -body {
  list [catch {binary} msg] [string match {*subcommand*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-1.2 {
  R-42906-22341: binary with an unknown subcommand errors
} -constraints {
    th8
} -body {
  list [catch {binary nosuchsub} msg]
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test binary-1.3 {
  R-20661-36254: binary format with no formatString errors
} -constraints {
    th8
} -body {
  list [catch {binary format} msg]
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test binary-1.4 {
  R-61607-09342: binary scan with no formatString errors
} -constraints {
    th8
} -body {
  list [catch {binary scan} msg]
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test binary-1.5 {
  R-42906-22341: binary format rejects an unknown specifier letter
} -constraints {
    th8
} -body {
  list [catch {binary format z 0} msg] [string match {*bad field*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-1.5a {
  R-42906-22341: binary format rejects a control-byte specifier letter
  (covers the bad-field-letter < 32 arm; letter prints as '?')
} -constraints {
    th8
} -body {
  list [catch {binary format [binary format c 1] 0} msg] \
       [string match {*bad field*\"?\"*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-1.5b {
  R-42906-22341: binary format rejects a high-byte specifier letter
  (covers the bad-field-letter >= 127 arm; letter prints as '?')
} -constraints {
    th8
} -body {
  list [catch {binary format [binary format c 254] 0} msg] \
       [string match {*bad field*\"?\"*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-1.6 {
  R-42906-22341: binary scan rejects an unknown specifier letter
} -constraints {
    th8
} -body {
  list [catch {binary scan "x" z v} msg] [string match {*bad field*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-1.7 {
  R-59103-40802: binary format rejects `*` count on @ (cursor specifier)
} -constraints {
    th8
} -body {
  list [catch {binary format @* hello} msg] [string match {*\"*\"*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-1.8 {
  R-47116-54899: binary format errors when not enough args supplied
} -constraints {
    th8
} -body {
  list [catch {binary format c3 1 2} msg] [string match {*not enough*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-1.9 {
  R-49218-09375: empty format string produces an empty result
} -constraints {
    th8
} -body {
  string bytelength [binary format ""]
} -result {0}}

###############################################################################

runTest {test binary-1.10 {
  R-46440-49876: whitespace in the format string is ignored
} -constraints {
    th8
} -body {
  set a [binary format "c c c" 1 2 3]
  set b [binary format "ccc" 1 2 3]
  string equal $a $b
} -cleanup {
  unset -nocomplain b a
} -result {1}}

###############################################################################

runTest {test binary-1.11 {
  R-45855-01725: numbered count consumes that many args
} -constraints {
    th8
} -body {
  string bytelength [binary format c3 1 2 3]
} -result {3}}

###############################################################################
#
# Section 2 -- c specifier (8-bit signed integer)
#
###############################################################################

runTest {test binary-c-1.1 {
  R-25063-08152: binary format c produces 1 byte
} -constraints {
    th8
} -body {
  string bytelength [binary format c 65]
} -result {1}}

###############################################################################

runTest {test binary-c-1.2 {
  R-31495-52668: binary format c truncates beyond 8 bits
} -constraints {
    th8
} -body {
  binary scan [binary format c 0x1FF] c v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-1}}

###############################################################################

runTest {test binary-c-1.3 {
  R-25063-08152: binary format / scan round trip for c, positive value
} -constraints {
    th8
} -body {
  binary scan [binary format c 65] c v
  set v
} -cleanup {
  unset -nocomplain v
} -result {65}}

###############################################################################

runTest {test binary-c-1.4 {
  R-61115-28808: binary scan c sign-extends negative byte
} -constraints {
    th8
} -body {
  binary scan [binary format c -1] c v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-1}}

###############################################################################

runTest {test binary-c-1.5 {
  R-45855-01725: binary format c3 packs three bytes
} -constraints {
    th8
} -body {
  string bytelength [binary format c3 1 2 3]
} -result {3}}

###############################################################################

runTest {test binary-c-1.6 {
  R-45855-01725: binary scan c3 reads three bytes into three vars
} -constraints {
    th8
} -body {
  binary scan [binary format c3 1 2 3] c3 a b c
  list $a $b $c
} -cleanup {
  unset -nocomplain c b a
} -result {1 2 3}}

###############################################################################

runTest {test binary-c-1.7 {
  R-32465-27648: binary scan c* reads remaining bytes into a list
} -constraints {
    th8
} -body {
  binary scan [binary format c4 1 2 3 4] c* v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1 2 3 4}}

###############################################################################
#
# Section 3 -- s / S / t (16-bit integers)
#
###############################################################################

runTest {test binary-s-1.1 {
  R-42966-06721 R-06885-20942: s emits little-endian 16-bit
} -constraints {
    th8
} -body {
  binary scan [binary format s 0x1234] cc lo hi
  list [expr {$lo & 0xff}] [expr {$hi & 0xff}]
} -cleanup {
  unset -nocomplain lo hi
} -result {52 18}}

###############################################################################

runTest {test binary-S-1.1 {
  R-42592-48705 R-06885-20942: S emits big-endian 16-bit
} -constraints {
    th8
} -body {
  binary scan [binary format S 0x1234] cc hi lo
  list [expr {$hi & 0xff}] [expr {$lo & 0xff}]
} -cleanup {
  unset -nocomplain lo hi
} -result {18 52}}

###############################################################################

runTest {test binary-s-1.2 {
  R-42966-06721 R-61115-28808: s round-trip for max positive int16
} -constraints {
    th8
} -body {
  binary scan [binary format s 32767] s v
  set v
} -cleanup {
  unset -nocomplain v
} -result {32767}}

###############################################################################

runTest {test binary-s-1.3 {
  R-42966-06721 R-61115-28808: s round-trip for min negative int16
} -constraints {
    th8
} -body {
  binary scan [binary format s -32768] s v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-32768}}

###############################################################################

runTest {test binary-t-1.1 {
  R-15892-10663: t (native 16-bit) round-trips against s OR S depending on host endianness
} -constraints {
    th8
} -body {
  binary scan [binary format t 0x4243] s vs
  binary scan [binary format t 0x4243] S vS
  # On a little-endian host vs == 0x4243; on big-endian, vS == 0x4243.
  expr {$vs == 0x4243 || $vS == 0x4243}
} -cleanup {
  unset -nocomplain vs vS
} -result {1}}

###############################################################################

runTest {test binary-s-1.4 {
  R-32465-27648: s* packs / unpacks an arbitrary list
} -constraints {
    th8
} -body {
  binary scan [binary format s4 10 20 30 40] s* v
  set v
} -cleanup {
  unset -nocomplain v
} -result {10 20 30 40}}

###############################################################################
#
# Section 4 -- i / I / n (32-bit integers)
#
###############################################################################

runTest {test binary-i-1.1 {
  R-54981-36508 R-06885-20942: i emits little-endian 32-bit
} -constraints {
    th8
} -body {
  binary scan [binary format i 0x11223344] cccc a b c d
  list [expr {$a & 0xff}] [expr {$b & 0xff}] \
      [expr {$c & 0xff}] [expr {$d & 0xff}]
} -cleanup {
  unset -nocomplain d c b a
} -result {68 51 34 17}}

###############################################################################

runTest {test binary-I-1.1 {
  R-50117-49851 R-06885-20942: I emits big-endian 32-bit
} -constraints {
    th8
} -body {
  binary scan [binary format I 0x11223344] cccc a b c d
  list [expr {$a & 0xff}] [expr {$b & 0xff}] \
      [expr {$c & 0xff}] [expr {$d & 0xff}]
} -cleanup {
  unset -nocomplain d c b a
} -result {17 34 51 68}}

###############################################################################

runTest {test binary-i-1.2 {
  R-54981-36508 R-61115-28808: i round-trip for max positive int32
} -constraints {
    th8
} -body {
  binary scan [binary format i 2147483647] i v
  set v
} -cleanup {
  unset -nocomplain v
} -result {2147483647}}

###############################################################################

runTest {test binary-i-1.3 {
  R-54981-36508 R-61115-28808: i round-trip for min negative int32
} -constraints {
    th8
} -body {
  binary scan [binary format i -2147483648] i v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-2147483648}}

###############################################################################

runTest {test binary-n-1.1 {
  R-64038-44860: n (native 32-bit) round-trips against i on host of matching endianness
} -constraints {
    th8
} -body {
  binary scan [binary format n 0x12345678] i vi
  binary scan [binary format n 0x12345678] I vI
  expr {$vi == 0x12345678 || $vI == 0x12345678}
} -cleanup {
  unset -nocomplain vi vI
} -result {1}}

###############################################################################

runTest {test binary-i-1.4 {
  R-32465-27648: i* packs / unpacks a list
} -constraints {
    th8
} -body {
  binary scan [binary format i3 100 200 300] i* v
  set v
} -cleanup {
  unset -nocomplain v
} -result {100 200 300}}

###############################################################################
#
# Section 5 -- w / W / m (64-bit integers)
#
###############################################################################

runTest {test binary-w-1.1 {
  R-26067-44132 R-06885-20942: w emits little-endian 64-bit
} -constraints {
    th8
} -body {
  binary scan [binary format w 1] cccccccc a b c d e f g h
  list [expr {$a & 0xff}] [expr {$b & 0xff}] \
      [expr {$c & 0xff}] [expr {$d & 0xff}] \
      [expr {$e & 0xff}] [expr {$f & 0xff}] \
      [expr {$g & 0xff}] [expr {$h & 0xff}]
} -cleanup {
  unset -nocomplain e f h g d c a b
} -result {1 0 0 0 0 0 0 0}}

###############################################################################

runTest {test binary-W-1.1 {
  R-37793-17393 R-06885-20942: W emits big-endian 64-bit
} -constraints {
    th8
} -body {
  binary scan [binary format W 1] cccccccc a b c d e f g h
  list [expr {$a & 0xff}] [expr {$b & 0xff}] \
      [expr {$c & 0xff}] [expr {$d & 0xff}] \
      [expr {$e & 0xff}] [expr {$f & 0xff}] \
      [expr {$g & 0xff}] [expr {$h & 0xff}]
} -cleanup {
  unset -nocomplain e h f g d c a b
} -result {0 0 0 0 0 0 0 1}}

###############################################################################

runTest {test binary-w-1.2 {
  R-26067-44132 R-61115-28808: w round-trip for a large positive value
} -constraints {
    th8
} -body {
  binary scan [binary format w 1234567890123456789] w v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1234567890123456789}}

###############################################################################

runTest {test binary-w-1.3 {
  R-37793-17393 R-61115-28808: W round-trip for a negative value
} -constraints {
    th8
} -body {
  binary scan [binary format W -1] W v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-1}}

###############################################################################

runTest {test binary-m-1.1 {
  R-17918-60828: m (native 64-bit) round-trips against w on host of matching endianness
} -constraints {
    th8
} -body {
  binary scan [binary format m 42] w vw
  binary scan [binary format m 42] W vW
  expr {$vw == 42 || $vW == 42}
} -cleanup {
  unset -nocomplain vW vw
} -result {1}}

###############################################################################
#
# Section 6 -- scan return value and partial-scan semantics
#
###############################################################################

runTest {test binary-scan-rv-1.1 {
  R-31564-03739 R-65410-51824: binary scan returns 0 when input is too short for any specifier
} -constraints {
    th8
} -body {
  binary scan "" c v
} -cleanup {
  unset -nocomplain v
} -result {0}}

###############################################################################

runTest {test binary-scan-rv-1.2 {
  R-65410-51824: binary scan returns the number of vars successfully assigned
} -constraints {
    th8
} -body {
  binary scan [binary format c2 7 8] c3 a b c
} -cleanup {
  unset -nocomplain a b
} -result {2}}

###############################################################################

runTest {test binary-scan-rv-1.3 {
  R-32465-27648: binary scan c* assigning to one variable counts as 1 assignment
} -constraints {
    th8
} -body {
  binary scan [binary format c3 1 2 3] c* v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################
#
# Section 7 -- Phase 2: ASCII string specifiers (a / A)
#
###############################################################################

runTest {test binary-a-1.1 {
  R-03852-65413: binary format a NUL-pads to count
} -constraints {
    th8
} -body {
  binary scan [binary format a4 ab] cccc a b c d
  list $a $b $c $d
} -cleanup {
  unset -nocomplain d c b a
} -result {97 98 0 0}}

###############################################################################

runTest {test binary-A-1.1 {
  R-53174-31709: binary format A space-pads to count
} -constraints {
    th8
} -body {
  binary scan [binary format A4 ab] cccc a b c d
  list $a $b $c $d
} -cleanup {
  unset -nocomplain d c a b
} -result {97 98 32 32}}

###############################################################################

runTest {test binary-a-1.2 {
  R-34542-04103: binary format a truncates when input is longer than count
} -constraints {
    th8
} -body {
  set bytes [binary format a3 helloworld]
  binary scan $bytes ccc a b c
  list [string bytelength $bytes] $a $b $c
} -cleanup {
  unset -nocomplain bytes c b a
} -result {3 104 101 108}}

###############################################################################

runTest {test binary-a-1.3 {
  R-03852-65413: binary scan a reads exact count bytes verbatim
} -constraints {
    th8
} -body {
  binary scan "hello" a3 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {hel}}

###############################################################################

runTest {test binary-A-1.2 {
  R-53255-62069: binary scan A strips trailing spaces and NULs
} -constraints {
    th8
} -body {
  binary scan "abc   " A6 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {abc}}

###############################################################################

runTest {test binary-A-1.3 {
  R-53255-62069: binary scan A strips trailing NUL bytes specifically
  (covers the zSrc[n-1] == 0 arm of the strip loop)
} -constraints {
    th8
} -body {
  binary scan [binary format a4 "ab\x00\x00"] A4 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {ab}}

###############################################################################

runTest {test binary-A-1.4 {
  R-53255-62069: binary scan A on all-strippable input yields empty
  (covers the n > 0 loop-exit arm of the strip loop)
} -constraints {
    th8
} -body {
  binary scan "    " A4 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {}}

###############################################################################

runTest {test binary-a-1.4 {
  R-05069-28317: binary scan a* reads all remaining bytes
} -constraints {
    th8
} -body {
  binary scan "abcdef" a* v
  set v
} -cleanup {
  unset -nocomplain v
} -result {abcdef}}

###############################################################################
#
# Section 8 -- Phase 2: bit specifiers (b / B)
#
###############################################################################

runTest {test binary-b-1.1 {
  R-48584-10712: binary format b emits bits low-to-high in each byte
} -constraints {
    th8
} -body {
  binary scan [binary format b8 10100000] c v
  set v
} -cleanup {
  unset -nocomplain v
} -result {5}}

###############################################################################

runTest {test binary-B-1.1 {
  R-00830-46773: binary format B emits bits high-to-low in each byte
} -constraints {
    th8
} -body {
  binary scan [binary format B8 10100000] c v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-96}}

###############################################################################

runTest {test binary-b-1.2 {
  R-48584-10712: binary scan b decodes bits low-to-high
} -constraints {
    th8
} -body {
  binary scan [binary format b8 11010000] b8 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {11010000}}

###############################################################################

runTest {test binary-B-1.2 {
  R-00830-46773: binary scan B decodes bits high-to-low
} -constraints {
    th8
} -body {
  binary scan [binary format B8 11010000] B8 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {11010000}}

###############################################################################

runTest {test binary-b-1.3 {
  R-47120-33399: binary format b pads remaining bits with 0
} -constraints {
    th8
} -body {
  binary scan [binary format b8 10] c v
  expr {$v & 0xff}
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################

runTest {test binary-b-1.4 {
  R-05069-28317: binary scan b* expands to remaining bytes * 8 bits
} -constraints {
    th8
} -body {
  binary scan "\x05\x0a" b* v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1010000001010000}}

###############################################################################
#
# Section 9 -- Phase 2: hex specifiers (h / H)
#
###############################################################################

runTest {test binary-h-1.1 {
  R-06973-20849: binary format h emits the low nibble first per byte
} -constraints {
    th8
} -body {
  binary scan [binary format h2 1f] H2 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {f1}}

###############################################################################

runTest {test binary-H-1.1 {
  R-07840-57050: binary format H emits the high nibble first per byte
} -constraints {
    th8
} -body {
  binary scan [binary format H2 1f] H2 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1f}}

###############################################################################

runTest {test binary-h-1.2 {
  R-06973-20849: binary scan h decodes hex low-nibble-first
} -constraints {
    th8
} -body {
  binary scan "\x12\x34" h4 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {2143}}

###############################################################################

runTest {test binary-H-1.2 {
  R-07840-57050: binary scan H decodes hex high-nibble-first
} -constraints {
    th8
} -body {
  binary scan "\x12\x34" H4 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1234}}

###############################################################################

runTest {test binary-H-1.3 {
  R-47120-33399: binary format H pads short input with 0 nibbles
} -constraints {
    th8
} -body {
  binary scan [binary format H4 a] H4 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {a000}}

###############################################################################

runTest {test binary-h-1.3 {
  R-05069-28317: binary scan h* expands to remaining bytes * 2 nibbles
} -constraints {
    th8
} -body {
  binary scan "\xab\xcd" H* v
  set v
} -cleanup {
  unset -nocomplain v
} -result {abcd}}

###############################################################################
#
# th8BinaryHexValue per-range MC/DC coverage: drive each
# `'0'..'9'`, `'a'..'f'`, `'A'..'F'` compound's (T,T) and
# (T,F) C-pairs as well as the (F,-) C1 vector by formatting
# bytes whose nibble values come from every character range.
#
###############################################################################

runTest {test binary-h-2.1 {
  binary format H with lowercase hex digits drives the
  L938 'a'..'f' compound's (T, T) and (T, F) vectors at
  every position.
} -constraints {
    th8
} -body {
  binary scan [binary format H6 abcdef] H6 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {abcdef}}

###############################################################################

runTest {test binary-h-2.2 {
  binary format H with uppercase hex digits drives the
  L939 'A'..'F' compound's (T, T) and (T, F) vectors.
} -constraints {
    th8
} -body {
  binary scan [binary format H6 ABCDEF] H6 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {abcdef}}

###############################################################################

runTest {test binary-h-2.3 {
  binary format H with a non-hex character (e.g. 'g')
  drives the `return 0;` final arm: the F vector on all
  three range checks.  The non-hex char encodes as 0.
} -constraints {
    th8
} -body {
  binary scan [binary format H2 g0] H2 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {00}}

###############################################################################

runTest {test binary-h-2.4 {
  binary format H with character above 'F' but below 'a'
  (e.g. 'G' = 0x47, '[' = 0x5B) drives the L938 C1 (F)
  vector for the 'a'..'f' range AND the L939 (T, F) vector
  for the 'A'..'F' range.
} -constraints {
    th8
} -body {
  binary scan [binary format H2 GG] H2 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {00}}

###############################################################################

runTest {test binary-h-2.5 {
  binary format H with character above '9' but below 'A'
  (e.g. ':' = 0x3A) drives the L937 (T, F) vector for
  '0'..'9' and the L939 (F, -) for 'A'..'F'.
} -constraints {
    th8
} -body {
  binary scan [binary format H2 ":;"] H2 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {00}}

###############################################################################
#
# Section 10 -- Phase 2: cursor ops (x / X / @)
#
###############################################################################

runTest {test binary-x-1.1 {
  R-32135-39472: binary format x inserts count NUL bytes
} -constraints {
    th8
} -body {
  binary scan [binary format x3c 42] cccc a b c d
  list $a $b $c $d
} -cleanup {
  unset -nocomplain d c a b
} -result {0 0 0 42}}

###############################################################################

runTest {test binary-x-1.2 {
  R-32135-39472 R-14406-51731: binary scan x skips count bytes
} -constraints {
    th8
} -body {
  binary scan "\x01\x02\x03\x04\x05" x2cc x y
  list $x $y
} -cleanup {
  unset -nocomplain y x
} -result {3 4}}

###############################################################################

runTest {test binary-X-1.1 {
  R-20624-04095: binary format X backs up the write cursor
} -constraints {
    th8
} -body {
  binary scan [binary format c3X1c 1 2 3 99] ccc a b c
  list $a $b $c
} -cleanup {
  unset -nocomplain c a b
} -result {1 2 99}}

###############################################################################

runTest {test binary-X-1.2 {
  R-20624-04095: binary scan X backs up the read cursor (cap at 0)
} -constraints {
    th8
} -body {
  binary scan "\x01\x02\x03\x04" cX1c a b
  list $a $b
} -cleanup {
  unset -nocomplain a b
} -result {1 1}}

###############################################################################

runTest {test binary-at-1.1 {
  R-25940-65513: binary format @ seeks to an absolute byte position, zero-filling any gap
} -constraints {
    th8
} -body {
  binary scan [binary format "@5c" 42] cccccc a b c d e f
  list $a $b $c $d $e $f
} -cleanup {
  unset -nocomplain f e d a b c
} -result {0 0 0 0 0 42}}

###############################################################################

runTest {test binary-at-1.2 {
  R-25940-65513 R-14406-51731: binary scan @ seeks to an absolute byte position
} -constraints {
    th8
} -body {
  binary scan "ABCD" @2cc a b
  list $a $b
} -cleanup {
  unset -nocomplain a b
} -result {67 68}}

###############################################################################

runTest {test binary-at-1.3 {
  R-65410-51824 R-25940-65513: binary scan @ past end caps cursor at end; subsequent reads stop the scan
} -constraints {
    th8
} -body {
  binary scan "AB" @99c v
} -cleanup {
  unset -nocomplain v
} -result {0}}

###############################################################################
#
# Section 11 -- Phase 2: format `*` rejection
#
###############################################################################

runTest {test binary-fmt-star-1.1 {
  R-59103-40802: binary format rejects `*` on x (cursor specifier)
} -constraints {
    th8
} -body {
  list [catch {binary format x*} msg] [string match {*\"*\"*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-fmt-star-1.2 {
  R-59103-40802: binary format rejects `*` on @
} -constraints {
    th8
} -body {
  catch {binary format @* hello}
} -result {1}}

###############################################################################
#
# Section 11b -- Phase 4: format `*` accepted on other specifiers
#
###############################################################################

runTest {test binary-fmt-star-a-2.1 {
  R-18953-16554: binary format a* uses input string length as count
} -constraints {
    th8
} -body {
  string length [binary format a* hello]
} -result {5}}

###############################################################################

runTest {test binary-fmt-star-A-2.1 {
  R-18953-16554: binary format A* uses input string length as count
} -constraints {
    th8
} -body {
  string bytelength [binary format A* "ab cd"]
} -result {5}}

###############################################################################

runTest {test binary-fmt-star-b-2.1 {
  R-14183-57437: binary format b* uses input bit count as count
} -constraints {
    th8
} -body {
  # 16 bits -> 2 bytes
  string bytelength [binary format b* 0101010101010101]
} -result {2}}

###############################################################################

runTest {test binary-fmt-star-h-2.1 {
  R-14183-57437: binary format h* uses input nibble count as count
} -constraints {
    th8
} -body {
  # 6 nibbles -> 3 bytes
  string bytelength [binary format h* abcdef]
} -result {3}}

###############################################################################

runTest {test binary-fmt-star-c-2.1 {
  R-33956-61677: binary format c* consumes one list arg and packs each element
} -constraints {
    th8
} -body {
  binary scan [binary format c* {1 2 3 4}] c4 a b c d
  list $a $b $c $d
} -cleanup {
  unset -nocomplain d a b c
} -result {1 2 3 4}}

###############################################################################

runTest {test binary-fmt-star-i-2.1 {
  R-33956-61677: binary format i* packs N int32s for N list elements
} -constraints {
    th8
} -body {
  string bytelength [binary format i* {1 2 3}]
} -result {12}}

###############################################################################

runTest {test binary-fmt-star-w-2.1 {
  R-33956-61677: binary format w* round-trips a list of int64s
} -constraints {
    th8
} -body {
  binary scan [binary format w* {10 20 30}] w3 a b c
  list $a $b $c
} -cleanup {
  unset -nocomplain a b c
} -result {10 20 30}}

###############################################################################

runTest {test binary-fmt-star-d-2.1 {
  R-57487-54613: binary format d* round-trips a list of doubles
} -constraints {
    th8
} -body {
  binary scan [binary format d* {1.5 2.5 3.5}] d3 a b c
  list $a $b $c
} -cleanup {
  unset -nocomplain a b c
} -result {1.5 2.5 3.5}}

###############################################################################

runTest {test binary-fmt-star-f-2.1 {
  R-57487-54613: binary format f* produces 4 bytes per list element
} -constraints {
    th8
} -body {
  string bytelength [binary format f* {1.0 2.0 3.0 4.0}]
} -result {16}}

###############################################################################

runTest {test binary-fmt-list-err-i-2.1 {
  R-57487-54613: binary format i* propagates a per-element parse
  error mid-list (covers the rc==TH8_OK && j<nList loop-exit arm)
} -constraints {
    th8
} -body {
  list [catch {binary format i* {1 garbage 3}} msg] \
       [expr {[string length $msg] > 0}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-fmt-list-err-f-2.1 {
  R-57487-54613: binary format f* propagates a per-element parse
  error mid-list (covers the float-list rc==TH8_OK && j<nList arm)
} -constraints {
    th8
} -body {
  list [catch {binary format f* {1.0 garbage 3.0}} msg] \
       [expr {[string length $msg] > 0}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test binary-fmt-star-X-2.1 {
  R-28819-01644: binary format X* rewinds the cursor to zero
} -constraints {
    th8
} -body {
  # Write 3 bytes "AAA", then X* rewinds to 0, then a3 overwrites with "BBB".
  binary format a3X*a3 AAA BBB
} -result {BBB}}

###############################################################################
#
# Section 12 -- Phase 3: float specifiers (f / r / R / d / q / Q)
#
###############################################################################

runTest {test binary-f-1.1 {
  R-30882-58136: binary format f produces 4 bytes; round-trip returns same value
} -constraints {
    th8
} -body {
  binary scan [binary format f 1.5] f v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1.5}}

###############################################################################

runTest {test binary-f-1.2 {
  R-30882-58136 R-65398-59132: binary format f emits IEEE 754 single (LE 1.5 = 0x3FC00000)
} -constraints {
    th8
} -body {
  binary scan [binary format r 1.5] cccc a b c d
  list [expr {$a & 0xff}] [expr {$b & 0xff}] \
      [expr {$c & 0xff}] [expr {$d & 0xff}]
} -cleanup {
  unset -nocomplain d a b c
} -result {0 0 192 63}}

###############################################################################

runTest {test binary-r-1.1 {
  R-65398-59132: binary format r emits little-endian single
} -constraints {
    th8
} -body {
  binary scan [binary format r 1.5] r v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1.5}}

###############################################################################

runTest {test binary-R-1.1 {
  R-40141-64595: binary format R emits big-endian single
} -constraints {
    th8
} -body {
  binary scan [binary format R 1.5] cccc a b c d
  list [expr {$a & 0xff}] [expr {$b & 0xff}] \
      [expr {$c & 0xff}] [expr {$d & 0xff}]
} -cleanup {
  unset -nocomplain a b c d
} -result {63 192 0 0}}

###############################################################################

runTest {test binary-d-1.1 {
  R-52505-47336: binary format d / scan d round-trips a double precisely
} -constraints {
    th8
} -body {
  set d 3.141592653589793
  binary scan [binary format d $d] d v
  # Round-trip equality (not string equality): the scanned value
  # must equal the original to the bit when re-fed through Th8_ToDouble.
  expr {$v == $d}
} -cleanup {
  unset -nocomplain d v
} -result {1}}

###############################################################################

runTest {test binary-q-1.1 {
  R-44824-14970: binary format q / scan q round-trips in little-endian
} -constraints {
    th8
} -body {
  binary scan [binary format q 1.5e308] q v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1.5e+308}}

###############################################################################

runTest {test binary-Q-1.1 {
  R-24212-52504: binary format Q / scan Q round-trips in big-endian
} -constraints {
    th8
} -body {
  binary scan [binary format Q -2.5] Q v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-2.5}}

###############################################################################

runTest {test binary-fd-nan-1.1 {
  R-45917-21828 R-65214-62481: binary format d NaN -> canonical; scan returns "NaN"
} -constraints {
    th8
} -body {
  binary scan [binary format d NaN] d v
  set v
} -cleanup {
  unset -nocomplain v
} -result {NaN}}

###############################################################################

runTest {test binary-fd-nan-1.2 {
  R-55429-28115 R-65214-62481: binary format f NaN -> canonical single-precision NaN
} -constraints {
    th8
} -body {
  binary scan [binary format f NaN] f v
  set v
} -cleanup {
  unset -nocomplain v
} -result {NaN}}

###############################################################################

runTest {test binary-fd-inf-1.1 {
  R-45917-21828 R-65214-62481: binary format d Inf and scan d round-trip
} -constraints {
    th8
} -body {
  binary scan [binary format d Inf] d v
  set v
} -cleanup {
  unset -nocomplain v
} -result {Inf}}

###############################################################################

runTest {test binary-fd-inf-1.2 {
  R-45917-21828 R-65214-62481: binary format d -Inf and scan d round-trip
} -constraints {
    th8
} -body {
  binary scan [binary format d -Inf] d v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-Inf}}

###############################################################################

runTest {test binary-ff-inf-1.1 {
  R-45917-21828 R-65214-62481: binary format f Inf and scan f round-trip
} -constraints {
    th8
} -body {
  binary scan [binary format f Inf] f v
  set v
} -cleanup {
  unset -nocomplain v
} -result {Inf}}

###############################################################################

runTest {test binary-ff-inf-1.2 {
  R-45917-21828 R-65214-62481: binary format f -Inf and scan f round-trip
} -constraints {
    th8
} -body {
  binary scan [binary format f -Inf] f v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-Inf}}

###############################################################################

runTest {test binary-fd-zero-1.1 {
  R-30882-58136: binary format f and scan f handle 0.0 cleanly
} -constraints {
    th8
} -body {
  binary scan [binary format f 0.0] f v
  set v
} -cleanup {
  unset -nocomplain v
} -result {0}}

###############################################################################

runTest {test binary-fd-count-1.1 {
  R-52505-47336 R-45855-01725: binary format d3 / scan d3 packs and unpacks multiple doubles
} -constraints {
    th8
} -body {
  binary scan [binary format d3 1.0 2.0 3.0] d3 a b c
  list $a $b $c
} -cleanup {
  unset -nocomplain a b c
} -result {1 2 3}}

###############################################################################

runTest {test binary-fd-star-1.1 {
  R-32465-27648: binary scan d* assigns a list of doubles
} -constraints {
    th8
} -body {
  binary scan [binary format d3 1.0 2.0 3.0] d* lst
  set lst
} -cleanup {
  unset -nocomplain lst
} -result {1 2 3}}

###############################################################################

runTest {test binary-bigint-1.1 {
  R-36503-62216: binary format w accepts a bignum and truncates modulo 2^64
} -constraints {
    th8 bigint
} -body {
  binary scan [binary format w 0x10000000000000001] w v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################

runTest {test binary-bigint-1.2 {
  R-36503-62216: binary format c truncates a bignum input modulo 2^8
} -constraints {
    th8 bigint
} -body {
  binary scan [binary format c 0x101] c v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################

runTest {test binary-bigint-1.3 {
  R-36503-62216: binary format i truncates a bignum input modulo 2^32
} -constraints {
    th8 bigint
} -body {
  binary scan [binary format i 0x100000001] i v
  set v
} -cleanup {
  unset -nocomplain v
} -result {1}}

###############################################################################

runTest {test binary-bigint-1.4 {
  R-36503-62216: binary format errors on malformed integer when bigint truncation also fails
} -constraints {
    th8 bigint
} -body {
  catch {binary format c "not-a-number"} msg
  string match {*integer*} $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test binary-bigint-1.5 {
  R-36503-62216: when bigint is runtime-disabled, an out-of-range integer
  argument errors (the KIND_BIGINT dispatch is runtime-gated by
  Th8_IsBigintEnabled and does not silently truncate when bigint is off)
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  catch {binary format w 0x10000000000000001} msg
  set ok [string match {*too large*} $msg]
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  set ok
} -cleanup {
  unset -nocomplain wasEnabled ok msg
} -result {1}}

###############################################################################

runTest {test binary-fd-canonical-nan-1.1 {
  R-55429-28115: any NaN input through binary format d normalises to 0x7FF8000000000000
} -constraints {
    th8
} -body {
  # Format NaN as double, then read back as big-endian 64-bit
  # by transcoding through Q (which round-trips BE bytes) -- but
  # the native d emits LE on this host, so scan as the matching
  # native order to recover the canonical bit pattern.
  binary scan [binary format Q NaN] W bits
  format %016x [expr {$bits & 0xffffffffffffffff}]
} -cleanup {
  unset -nocomplain bits
} -result {7ff8000000000000}}

###############################################################################

###############################################################################
#
# Section 14 -- j / J: arbitrary-width signed bigint (two's complement)
#
###############################################################################

runTest {test binary-j-1.1 {
  R-64289-18387: binary format j2 packs in little-endian two's complement
} -constraints {
    th8
} -body {
  binary scan [binary format j2 0x1234] H* h
  set h
} -cleanup {
  unset -nocomplain h
} -result {3412}}

###############################################################################

runTest {test binary-j-1.2 {
  R-64289-18387: binary format J2 packs in big-endian two's complement
} -constraints {
    th8
} -body {
  binary scan [binary format J2 0x1234] H* h
  set h
} -cleanup {
  unset -nocomplain h
} -result {1234}}

###############################################################################

runTest {test binary-j-1.3 {
  R-64289-18387: binary scan j is signed and sign-extends; -1 round-trips
} -constraints {
    th8
} -body {
  binary scan [binary format j4 -1] j4 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-1}}

###############################################################################

runTest {test binary-j-1.4 {
  R-64289-18387: J8 round-trip of a negative value via the bigint path
} -constraints {
    th8 bigint
} -body {
  binary scan [binary format J8 -42] J8 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-42}}

###############################################################################

runTest {test binary-j-1.5 {
  R-05827-41628: J16 round-trip of a 30-digit bigint (well outside int64)
} -constraints {
    th8 bigint
} -body {
  set big -123456789012345678901234567890
  binary scan [binary format J16 $big] J16 v
  set v
} -cleanup {
  unset -nocomplain big v
} -result {-123456789012345678901234567890}}

###############################################################################

runTest {test binary-j-2.1 {
  R-22686-46265: binary format j1 errors when value is +128 (out of range)
} -constraints {
    th8
} -body {
  catch {binary format j1 128} m
  string match {*too large*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test binary-j-2.2 {
  R-22686-46265: binary format j1 errors when value is -129 (out of range)
} -constraints {
    th8
} -body {
  catch {binary format j1 -129} m
  string match {*too large*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test binary-j-2.3 {
  R-22686-46265: binary format j1 accepts the boundary value -128
} -constraints {
    th8
} -body {
  binary scan [binary format j1 -128] H* h
  set h
} -cleanup {
  unset -nocomplain h
} -result {80}}

###############################################################################

runTest {test binary-j-3.1 {
  R-54898-61945: binary format j* packs in the value's minimum byte width;
  zero takes one byte
} -constraints {
    th8
} -body {
  string bytelength [binary format j* 0]
} -result {1}}

###############################################################################

runTest {test binary-j-3.2 {
  R-54898-61945: binary format J* packs a 128-bit bigint in exactly 16 bytes
} -constraints {
    th8 bigint
} -body {
  string bytelength [binary format J* 0x123456789ABCDEF0123456789ABCDEF0]
} -result {16}}

###############################################################################

runTest {test binary-j-3.3 {
  R-54898-61945: binary format j* picks tight width (127 fits in 1 byte;
  128 needs 2 bytes because of the sign bit)
} -constraints {
    th8
} -body {
  list [string bytelength [binary format j* 127]] \
       [string bytelength [binary format j* 128]]
} -result {1 2}}

###############################################################################

runTest {test binary-j-4.1 {
  R-07235-20862: binary scan j* consumes all remaining bytes into one bigint;
  round-trip the value to hex via a second J16 pack
} -constraints {
    th8 bigint
} -body {
  set src [binary format J16 0x0102030405060708090A0B0C0D0E0F10]
  binary scan $src J* v
  binary scan [binary format J16 $v] H* h
  set h
} -cleanup {
  unset -nocomplain src h v
} -result {0102030405060708090a0b0c0d0e0f10}}

###############################################################################

runTest {test binary-j-4.2 {
  R-07235-20862: binary scan j* on empty input yields the value 0
} -constraints {
    th8
} -body {
  binary scan "" J* v
  set v
} -cleanup {
  unset -nocomplain v
} -result {0}}

###############################################################################

runTest {test binary-j-4.3 {
  R-64289-18387: little-endian scan with sign-extension on a negative
  value (round-trip through j2 of -2)
} -constraints {
    th8
} -body {
  binary scan [binary format j2 -2] j2 v
  set v
} -cleanup {
  unset -nocomplain v
} -result {-2}}

###############################################################################

runTest {test binary-j-5.1 {
  binary format j2 errors when no value argument is supplied
} -constraints {
    th8
} -body {
  catch {binary format j2} m
  string match {*not enough arguments*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test binary-j-5.2 {
  binary format j2 errors when the value cannot be parsed as an integer
} -constraints {
    th8 bigint
} -body {
  catch {binary format j2 "not-a-number"} m
  string match {*integer*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test binary-j-5.3 {
  R-54898-61945: binary format j* picks a tight width for a negative
  power-of-2 magnitude (-128 fits in 1 byte; -129 needs 2 bytes)
} -constraints {
    th8
} -body {
  list [string bytelength [binary format j* -128]] \
       [string bytelength [binary format j* -129]]
} -result {1 2}}

###############################################################################

runTest {test binary-j-5.4 {
  R-54898-61945: binary format j* picks a tight width for a non-power-of-2
  negative (-1 fits in 1 byte; -200 needs 2 bytes)
} -constraints {
    th8
} -body {
  list [string bytelength [binary format j* -1]] \
       [string bytelength [binary format j* -200]]
} -result {1 2}}

###############################################################################

runTest {test binary-j-6.1 {
  R-05827-41628: with bigint runtime-disabled, j8 still works for
  int64-range values (sign-extend fallback path)
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set v -42
  binary scan [binary format j8 $v] j8 r
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  set r
} -cleanup {
  unset -nocomplain wasEnabled v r
} -result {-42}}

###############################################################################

runTest {test binary-j-6.2 {
  R-05827-41628: with bigint runtime-disabled, j-scan of width > 8 errors
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set bytes [string repeat \x00 16]
  set ok [catch {binary scan $bytes j16 v} m]
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  list $ok [string match {*exceeds int64*} $m]
} -cleanup {
  unset -nocomplain wasEnabled bytes ok m v
} -result {1 1}}

###############################################################################

runTest {test binary-j-6.3 {
  R-05827-41628: with bigint runtime-disabled, j-format range check on a
  small (< int64) field still rejects out-of-range values
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set ok [catch {binary format j2 50000} m]
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  list $ok [string match {*too large*} $m]
} -cleanup {
  unset -nocomplain wasEnabled ok m
} -result {1 1}}

###############################################################################

runTest {test binary-j-6.4 {
  R-05827-41628: with bigint runtime-disabled, in-range j2 value packs
  successfully (drives the L1371 F,F vector on the range check)
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set v 1234
  binary scan [binary format j2 $v] j2 r
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  set r
} -cleanup {
  unset -nocomplain wasEnabled v r
} -result {1234}}

###############################################################################

runTest {test binary-j-6.5 {
  R-05827-41628: with bigint runtime-disabled, j-format range check
  rejects a negative value below minNeg (drives the L1371 F,T vector)
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set ok [catch {binary format j2 -50000} m]
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  list $ok [string match {*too large*} $m]
} -cleanup {
  unset -nocomplain wasEnabled ok m
} -result {1 1}}

###############################################################################

runTest {test binary-j-6.6 {
  R-54898-61945: with bigint runtime-disabled, j* falls back to a fixed
  8-byte field (bigint min-width helper not available)
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set n [string bytelength [binary format j* 1234]]
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  set n
} -cleanup {
  unset -nocomplain wasEnabled n
} -result {8}}

###############################################################################

runTest {test binary-j-6.7 {
  R-05827-41628: with bigint runtime-disabled, big-endian J-scan of an
  int64-range value (drives the L2001 True branch in the scan helper)
} -constraints {
    th8 bigint_toggle
} -setup {
  set wasEnabled [::th8testlib::bigint query]
  ::th8testlib::bigint disable
} -body {
  set bytes [binary format J4 0x12345678]
  binary scan $bytes J4 v
  if {$wasEnabled} then { ::th8testlib::bigint enable }
  format %x $v
} -cleanup {
  unset -nocomplain wasEnabled bytes v
} -result {12345678}}

###############################################################################

runTest {test binary-j-7.1 {
  binary format j0 errors with a count-out-of-range diagnostic
  (zero-width fields are rejected at the count check)
} -constraints {
    th8
} -body {
  catch {binary format j0 0} m
  string match {*count*out of range*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

source tests/epilogue.tcl
