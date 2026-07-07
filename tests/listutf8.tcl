###############################################################################
#
# listutf8.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Verify that the list parser (Spilornis) correctly handles
# multi-byte UTF-8 sequences.  UTF-8 continuation bytes (0x80-0xBF)
# must never be mistaken for ASCII list delimiters (space, brace,
# bracket, backslash, quote, semicolon, dollar).
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
# UTF-8 test strings:
#
#   2-byte:  \xC3\xA9  = U+00E9  LATIN SMALL LETTER E WITH ACUTE  (e-acute)
#   3-byte:  \xE4\xB8\xAD = U+4E2D  CJK UNIFIED IDEOGRAPH (zhong)
#   4-byte:  \xF0\x9F\x98\x80 = U+1F600  GRINNING FACE (emoji)
#
# These are constructed via [format] to ensure exact byte sequences
# regardless of source file encoding.
#
###############################################################################
#
# Section 1 -- list / llength / lindex with multi-byte elements
#
###############################################################################

runTest {test listutf8-1.1 {
  R-32728-09100: list with 2-byte UTF-8 element preserves bytes
} -setup {
} -body {
  set eacute [format %c 0xE9]
  set L [list abc $eacute def]
  list [llength $L] [lindex $L 1]
} -cleanup {
  unset -nocomplain eacute L
} -result [list 3 [format %c 0xE9]]}

###############################################################################

runTest {test listutf8-1.2 {
  R-32728-09100: list with 3-byte UTF-8 element preserves bytes
} -setup {
} -body {
  set zhong [format %c 0x4E2D]
  set L [list $zhong hello $zhong]
  list [llength $L] [lindex $L 0] [lindex $L 2]
} -cleanup {
  unset -nocomplain zhong L
} -result [list 3 [format %c 0x4E2D] [format %c 0x4E2D]]}

###############################################################################

runTest {test listutf8-1.3 {
  R-32728-09100: list with 4-byte UTF-8 element (emoji) preserves bytes
} -constraints {
    escapeU
} -setup {
} -body {
  set emoji [format %c 0x1F600]
  set L [list before $emoji after]
  list [llength $L] [lindex $L 1]
} -cleanup {
  unset -nocomplain emoji L
} -result [list 3 [format %c 0x1F600]]}

###############################################################################

runTest {test listutf8-1.4 {
  R-32728-09100: list with mixed multi-byte widths
} -constraints {
    escapeU
} -setup {
} -body {
  set e2 [format %c 0xE9]
  set e3 [format %c 0x4E2D]
  set e4 [format %c 0x1F600]
  set L [list $e2 $e3 $e4 ascii]
  list [llength $L] [lindex $L 0] [lindex $L 1] [lindex $L 2] [lindex $L 3]
} -cleanup {
  unset -nocomplain e2 e3 e4 L
} -result [list 4 [format %c 0xE9] [format %c 0x4E2D] [format %c 0x1F600] ascii]}

###############################################################################
#
# Section 2 -- lappend / lrange / lreplace with multi-byte elements
#
###############################################################################

runTest {test listutf8-2.1 {
  R-32728-09100: lappend preserves multi-byte UTF-8 elements
} -setup {
} -body {
  set L [list]
  lappend L [format %c 0xE9]
  lappend L [format %c 0x4E2D]
  lappend L plain
  list [llength $L] [lindex $L 0] [lindex $L 1] [lindex $L 2]
} -cleanup {
  unset -nocomplain L
} -result [list 3 [format %c 0xE9] [format %c 0x4E2D] plain]}

###############################################################################

runTest {test listutf8-2.2 {
  R-32728-09100: lrange with multi-byte UTF-8 elements
} -setup {
} -body {
  set e [format %c 0xE9]
  set z [format %c 0x4E2D]
  set L [list a $e b $z c]
  lrange $L 1 3
} -cleanup {
  unset -nocomplain e z L
} -result [list [format %c 0xE9] b [format %c 0x4E2D]]}

###############################################################################

runTest {test listutf8-2.3 {
  R-32728-09100: lreplace with multi-byte UTF-8 replacement
} -setup {
} -body {
  set z [format %c 0x4E2D]
  set L [list a b c]
  lreplace $L 1 1 $z
} -cleanup {
  unset -nocomplain z L
} -result [list a [format %c 0x4E2D] c]}

###############################################################################
#
# Section 3 -- braced and quoted elements containing multi-byte UTF-8
#
###############################################################################

runTest {test listutf8-3.1 {
  R-32728-09100: braced element with multi-byte UTF-8 round-trips
} -setup {
} -body {
  set e [format %c 0xE9]
  set input "a {hello ${e} world} b"
  list [llength $input] [lindex $input 1]
} -cleanup {
  unset -nocomplain e input
} -result [list 3 "hello [format %c 0xE9] world"]}

###############################################################################

runTest {test listutf8-3.2 {
  R-32728-09100: element with UTF-8 and spaces is braced by list
} -setup {
} -body {
  set e [format %c 0xE9]
  set elem "caf${e} latte"
  set L [list $elem]
  list [llength $L] [lindex $L 0]
} -cleanup {
  unset -nocomplain e elem L
} -result [list 1 "caf[format %c 0xE9] latte"]}

###############################################################################

runTest {test listutf8-3.3 {
  R-32728-09100: nested braces with multi-byte UTF-8
} -setup {
} -body {
  set z [format %c 0x4E2D]
  set inner [list $z $z]
  set outer [list a $inner b]
  list [llength $outer] [llength [lindex $outer 1]] \
      [lindex [lindex $outer 1] 0]
} -cleanup {
  unset -nocomplain z inner outer
} -result [list 3 2 [format %c 0x4E2D]]}

###############################################################################
#
# Section 4 -- join / split with multi-byte UTF-8
#
###############################################################################

runTest {test listutf8-4.1 {
  R-32728-09100: join with multi-byte UTF-8 elements
} -setup {
} -body {
  set e [format %c 0xE9]
  set z [format %c 0x4E2D]
  join [list $e $z] ","
} -cleanup {
  unset -nocomplain e z
} -result "[format %c 0xE9],[format %c 0x4E2D]"}

###############################################################################

runTest {test listutf8-4.2 {
  R-32728-09100: split on ASCII delimiter preserves multi-byte elements
} -setup {
} -body {
  set e [format %c 0xE9]
  set input "caf${e},th${e}"
  set L [split $input ","]
  list [llength $L] [lindex $L 0] [lindex $L 1]
} -cleanup {
  unset -nocomplain e input L
} -result [list 2 "caf[format %c 0xE9]" "th[format %c 0xE9]"]}

###############################################################################
#
# Section 5 -- lsearch / lsort with multi-byte UTF-8
#
###############################################################################

runTest {test listutf8-5.1 {
  R-32728-09100: lsearch finds multi-byte UTF-8 element
} -setup {
} -body {
  set e [format %c 0xE9]
  set z [format %c 0x4E2D]
  set L [list alpha $e beta $z gamma]
  list [lsearch $L $e] [lsearch $L $z] [lsearch $L delta]
} -cleanup {
  unset -nocomplain e z L
} -result {1 3 -1}}

###############################################################################

runTest {test listutf8-5.2 {
  R-32728-09100: lsort with multi-byte UTF-8 elements does not corrupt
} -setup {
} -body {
  set e [format %c 0xE9]
  set z [format %c 0x4E2D]
  set L [list $z $e ascii]
  set sorted [lsort $L]
  llength $sorted
} -cleanup {
  unset -nocomplain e z L sorted
} -result {3}}

###############################################################################
#
# Section 6 -- stress: many multi-byte elements
#
###############################################################################

runTest {test listutf8-6.1 {
  R-32728-09100: 100 multi-byte elements round-trip through list/lindex
} -setup {
} -body {
  set L [list]
  for {set i 0} {$i < 100} {incr i} {
    lappend L "[format %c 0x4E2D]$i"
  }
  set ok 1
  for {set i 0} {$i < 100} {incr i} {
    set elem [lindex $L $i]
    set expected "[format %c 0x4E2D]$i"
    if {$elem ne $expected} then {
      set ok 0
      break
    }
  }
  list [llength $L] $ok
} -cleanup {
  unset -nocomplain L i ok elem expected
} -result {100 1}}

###############################################################################

runTest {test listutf8-6.2 {
  R-32728-09100: multi-byte element adjacent to backslash in list
} -setup {
} -body {
  set e [format %c 0xE9]
  set L [list "\\${e}" "${e}\\"]
  list [llength $L] [lindex $L 0] [lindex $L 1]
} -cleanup {
  unset -nocomplain e L
} -result [list 2 "\\[format %c 0xE9]" "[format %c 0xE9]\\"]}

###############################################################################

runTest {test listutf8-6.3 {
  R-32728-09100: multi-byte element with embedded dollar sign
} -setup {
} -body {
  set z [format %c 0x4E2D]
  set L [list "price\$${z}" "${z}\$99"]
  list [llength $L] [lindex $L 0] [lindex $L 1]
} -cleanup {
  unset -nocomplain z L
} -result [list 2 "price\$[format %c 0x4E2D]" "[format %c 0x4E2D]\$99"]}

###############################################################################
#
# Section 7 -- zero-width and invisible format characters
#
# Zero-width joiner (U+200D), zero-width non-joiner (U+200C),
# zero-width space (U+200B), right-to-left mark (U+200F), and
# BOM (U+FEFF) are all multi-byte UTF-8 sequences that could
# confuse a byte-oriented parser.  They must pass through list
# operations unmodified.
#
###############################################################################

runTest {test listutf8-7.1 {
  R-32728-09100: zero-width joiner (U+200D) preserved in list element
} -setup {
} -body {
  set zwj [format %c 0x200D]
  set L [list "a${zwj}b" plain]
  list [llength $L] [string length [lindex $L 0]]
} -cleanup {
  unset -nocomplain zwj L
} -result [list 2 3]}

###############################################################################

runTest {test listutf8-7.2 {
  R-32728-09100: zero-width space (U+200B) preserved in list element
} -setup {
} -body {
  set zws [format %c 0x200B]
  set L [list "${zws}hello" "world${zws}"]
  list [llength $L] \
      [string length [lindex $L 0]] \
      [string length [lindex $L 1]]
} -cleanup {
  unset -nocomplain zws L
} -result [list 2 6 6]}

###############################################################################

runTest {test listutf8-7.3 {
  R-32728-09100: right-to-left mark (U+200F) preserved in list element
} -setup {
} -body {
  set rtl [format %c 0x200F]
  set L [list "${rtl}abc" "def${rtl}"]
  list [llength $L] [lindex $L 0] [lindex $L 1]
} -cleanup {
  unset -nocomplain rtl L
} -result [list 2 "[format %c 0x200F]abc" "def[format %c 0x200F]"]}

###############################################################################

runTest {test listutf8-7.4 {
  R-32728-09100: BOM (U+FEFF) within list element is preserved
} -setup {
} -body {
  set bom [format %c 0xFEFF]
  set L [list "${bom}data" "more${bom}data"]
  list [llength $L] \
      [string length [lindex $L 0]] \
      [string length [lindex $L 1]]
} -cleanup {
  unset -nocomplain bom L
} -result [list 2 5 9]}

###############################################################################

runTest {test listutf8-7.5 {
  R-32728-09100: zero-width non-joiner (U+200C) preserved in list element
} -setup {
} -body {
  set zwnj [format %c 0x200C]
  set L [list "x${zwnj}y"]
  list [llength $L] [string length [lindex $L 0]]
} -cleanup {
  unset -nocomplain zwnj L
} -result [list 1 3]}

###############################################################################
#
# Section 8 -- combining characters and composed sequences
#
# Combining characters (e.g. U+0301 COMBINING ACUTE ACCENT)
# follow a base character to form a grapheme cluster.  The list
# parser must treat the combining sequence as opaque bytes.
#
###############################################################################

runTest {test listutf8-8.1 {
  R-32728-09100: combining acute accent after base character
} -setup {
} -body {
  set base "e"
  set combining [format %c 0x0301]
  set elem "${base}${combining}"
  set L [list $elem plain]
  list [llength $L] [lindex $L 0]
} -cleanup {
  unset -nocomplain base combining elem L
} -result [list 2 "e[format %c 0x0301]"]}

###############################################################################

runTest {test listutf8-8.2 {
  R-32728-09100: multiple combining characters on one base
} -setup {
} -body {
  set acute [format %c 0x0301]
  set diaeresis [format %c 0x0308]
  set elem "u${diaeresis}${acute}"
  set L [list $elem other]
  list [llength $L] [string length [lindex $L 0]]
} -cleanup {
  unset -nocomplain acute diaeresis elem L
} -result [list 2 3]}

###############################################################################

runTest {test listutf8-8.3 {
  R-32728-09100: hangul jamo composed sequence
} -setup {
} -body {
  set ga [format %c 0xAC00]
  set na [format %c 0xB098]
  set L [list $ga $na plain]
  list [llength $L] [lindex $L 0] [lindex $L 1]
} -cleanup {
  unset -nocomplain ga na L
} -result [list 3 [format %c 0xAC00] [format %c 0xB098]]}

###############################################################################
#
# Section 9 -- multi-codepoint emoji sequences
#
# Emoji with skin tone modifiers and zero-width joiners form
# complex multi-codepoint sequences that span many bytes.
# The list parser must not split within these sequences.
#
###############################################################################

runTest {test listutf8-9.1 {
  R-32728-09100: emoji with skin tone modifier (2 codepoints)
} -constraints {
    escapeU
} -setup {
} -body {
  set wave [format %c 0x1F44B]
  set tone3 [format %c 0x1F3FD]
  set elem "${wave}${tone3}"
  set L [list $elem plain]
  list [llength $L] [string length [lindex $L 0]]
} -cleanup {
  unset -nocomplain wave tone3 elem L
} -result [list 2 2]}

###############################################################################

runTest {test listutf8-9.2 {
  R-32728-09100: ZWJ emoji sequence (multiple codepoints joined)
} -constraints {
    escapeU
} -setup {
} -body {
  set adult [format %c 0x1F9D1]
  set zwj [format %c 0x200D]
  set laptop [format %c 0x1F4BB]
  set elem "${adult}${zwj}${laptop}"
  set L [list $elem other items]
  list [llength $L] [string length [lindex $L 0]]
} -cleanup {
  unset -nocomplain adult zwj laptop elem L
} -result [list 3 3]}

###############################################################################

runTest {test listutf8-9.3 {
  R-32728-09100: flag sequence (two regional indicators)
} -constraints {
    escapeU
} -setup {
} -body {
  set ri_u [format %c 0x1F1FA]
  set ri_s [format %c 0x1F1F8]
  set flag "${ri_u}${ri_s}"
  set L [list $flag hello]
  list [llength $L] [string length [lindex $L 0]]
} -cleanup {
  unset -nocomplain ri_u ri_s flag L
} -result [list 2 2]}

###############################################################################
#
# Section 10 -- pathological byte patterns
#
# These tests verify that byte values which happen to match
# ASCII list delimiters in their numeric value cannot appear
# as UTF-8 continuation bytes (0x80-0xBF range), confirming
# that the parser won't be confused.
#
###############################################################################

runTest {test listutf8-10.1 {
  R-32728-09100: element containing bytes near ASCII brace values
} -setup {
} -body {
  # U+017B = LATIN Z WITH DOT ABOVE
  # 2-byte encoding: 0xC5 0xBB
  set zdot [format %c 0x017B]
  set L [list $zdot plain]
  list [llength $L] [lindex $L 0]
} -cleanup {
  unset -nocomplain zdot L
} -result [list 2 [format %c 0x017B]]}

###############################################################################

runTest {test listutf8-10.2 {
  R-32728-09100: element with bytes near ASCII backslash value
} -setup {
} -body {
  # U+015C = LATIN S WITH CIRCUMFLEX
  # 2-byte encoding: 0xC5 0x9C
  set scirc [format %c 0x015C]
  set L [list $scirc plain]
  list [llength $L] [lindex $L 0]
} -cleanup {
  unset -nocomplain scirc L
} -result [list 2 [format %c 0x015C]]}

###############################################################################

runTest {test listutf8-10.3 {
  R-32728-09100: element with bytes near ASCII bracket values
} -setup {
} -body {
  # U+015B and U+015D have encodings with continuation bytes
  # numerically close to ASCII bracket values
  set c1 [format %c 0x015B]
  set c2 [format %c 0x015D]
  set L [list $c1 $c2]
  list [llength $L] [lindex $L 0] [lindex $L 1]
} -cleanup {
  unset -nocomplain c1 c2 L
} -result [list 2 [format %c 0x015B] [format %c 0x015D]]}

###############################################################################

runTest {test listutf8-10.4 {
  R-32728-09100: element with bytes near ASCII space value
} -setup {
} -body {
  # U+00A0 = NO-BREAK SPACE (2-byte: \xC2\xA0)
  # Must NOT be treated as a list delimiter
  set nbsp [format %c 0x00A0]
  set elem "hello${nbsp}world"
  set L [list $elem]
  list [llength $L] [lindex $L 0]
} -cleanup {
  unset -nocomplain nbsp elem L
} -result [list 1 "hello[format %c 0xA0]world"]}

###############################################################################

runTest {test listutf8-10.5 {
  R-32728-09100: element with bytes near ASCII dollar sign value
} -setup {
} -body {
  # U+20AC = EURO SIGN
  # 3-byte encoding: 0xE2 0x82 0xAC
  set euro [format %c 0x20AC]
  set L [list "${euro}100" "50${euro}"]
  list [llength $L] [lindex $L 0] [lindex $L 1]
} -cleanup {
  unset -nocomplain euro L
} -result [list 2 "[format %c 0x20AC]100" "50[format %c 0x20AC]"]}

###############################################################################

runTest {test listutf8-10.6 {
  R-32728-09100: element with bytes near ASCII semicolon value
} -setup {
} -body {
  # U+037E = GREEK QUESTION MARK
  # 2-byte encoding: 0xCD 0xBE
  set gqm [format %c 0x037E]
  set L [list "a${gqm}b" plain]
  list [llength $L] [lindex $L 0]
} -cleanup {
  unset -nocomplain gqm L
} -result [list 2 "a[format %c 0x037E]b"]}

###############################################################################

runTest {test listutf8-10.7 {
  R-32728-09100: element with bytes near ASCII double-quote value
} -setup {
} -body {
  # U+201C = LEFT DOUBLE QUOTATION MARK (3-byte: 0xE2 0x80 0x9C)
  # U+201D = RIGHT DOUBLE QUOTATION MARK (3-byte: 0xE2 0x80 0x9D)
  set ldq [format %c 0x201C]
  set rdq [format %c 0x201D]
  set L [list "${ldq}hello${rdq}" plain]
  list [llength $L] [lindex $L 0]
} -cleanup {
  unset -nocomplain ldq rdq L
} -result [list 2 "[format %c 0x201C]hello[format %c 0x201D]"]}

###############################################################################

source tests/epilogue.tcl

