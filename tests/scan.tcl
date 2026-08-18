###############################################################################
#
# scan.tcl --
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
# Section 1 -- scan: %d (decimal integer)
#
###############################################################################

runTest {test scan-1.1 {
  R-56814-07430: scan %d basic integer
} -setup {
} -body {
  scan "42" "%d" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {42}}

###############################################################################

runTest {test scan-1.2 {
  R-2700-0102: scan %d negative integer
} -setup {
} -body {
  scan "-7" "%d" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {-7}}

###############################################################################

runTest {test scan-1.3 {
  R-03334-00938: scan %d returns count of conversions
} -setup {
} -body {
  scan "42" "%d" x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {1}}

###############################################################################
#
# Section 2 -- scan: %s (string)
#
###############################################################################

runTest {test scan-2.1 {
  R-38513-56453: scan %s basic string
} -setup {
} -body {
  scan "hello" "%s" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {hello}}

###############################################################################

runTest {test scan-2.2 {
  R-2700-0202: scan %s stops at whitespace
} -setup {
} -body {
  scan "hello world" "%s" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {hello}}

###############################################################################

runTest {test scan-2.3 {
  R-2700-0203: scan multiple %s
} -setup {
} -body {
  scan "hello world" "%s %s" a b
  list $a $b
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
} -constraints {scan} -result {hello world}}

###############################################################################
#
# Section 3 -- scan: %x (hexadecimal)
#
###############################################################################

runTest {test scan-3.1 {
  R-2700-0301: scan %x hexadecimal
} -setup {
} -body {
  scan "ff" "%x" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {255}}

###############################################################################

runTest {test scan-3.2 {
  R-47387-33797: scan %x with 0x prefix
} -setup {
} -body {
  scan "0xff" "%x" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {255}}

###############################################################################
#
# Section 4 -- scan: %o (octal)
#
###############################################################################

runTest {test scan-4.1 {
  R-33929-04283: scan %o octal
} -setup {
} -body {
  scan "10" "%o" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {8}}

###############################################################################
#
# Section 5 -- scan: %c (character)
#
###############################################################################

runTest {test scan-5.1 {
  R-30567-08335: scan %c character code
} -setup {
} -body {
  scan "A" "%c" x
  set x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {65}}

###############################################################################
#
# Section 6 -- scan: %f (floating point)
#
###############################################################################

runTest {test scan-6.1 {
  R-17663-64974: scan %f floating point
} -setup {
} -body {
  scan "3.14" "%f" x
  format "%.2f" $x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {3.14}}

###############################################################################
#
# Section 7 -- scan: %n (count of characters consumed)
#
###############################################################################

runTest {test scan-7.1 {
  R-27160-02205: scan %n characters consumed
} -setup {
} -body {
  scan "hello world" "%s%n" x n
  set n
} -cleanup {
  unset -nocomplain x
  unset -nocomplain n
} -constraints {scan} -result {5}}

###############################################################################
#
# Section 8 -- scan: list mode (no variable args)
#
###############################################################################

runTest {test scan-8.1 {
  R-27422-38271: scan in list mode returns list
} -body {
  scan "42 hello" "%d %s"
} -constraints {scan} -result {42 hello}}

###############################################################################

runTest {test scan-8.2 {
  R-2700-0802: scan list mode multiple integers
} -body {
  scan "1 2 3" "%d %d %d"
} -constraints {scan} -result {1 2 3}}

###############################################################################

runTest {test scan-8.3 {
  R-2700-0803: scan list mode with hex
} -body {
  scan "ff" "%x"
} -constraints {scan} -result {255}}

###############################################################################
#
# Section 9 -- scan: mixed conversions
#
###############################################################################

runTest {test scan-9.1 {
  R-2700-0901: scan mixed integer and string
} -setup {
} -body {
  scan "42 hello" "%d %s" a b
  list $a $b
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
} -constraints {scan} -result {42 hello}}

###############################################################################

runTest {test scan-9.2 {
  R-2700-0902: scan returns number of conversions
} -setup {
} -body {
  scan "42 hello" "%d %s" a b
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
} -constraints {scan} -result {2}}

###############################################################################
#
# Section 10 -- scan: error cases
#
###############################################################################

runTest {test scan-10.1 {
  R-2700-1001: scan with no args is error
} -setup {
} -body {
  list [catch {scan} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -constraints {scan} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test scan-10.2 {
  R-2700-1002: scan with insufficient args is error
} -setup {
} -body {
  list [catch {scan "hello"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -constraints {scan} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test scan-11.1 {
  R-08988-52516: scan with a * flag after % discards the converted value
                 without assigning it, so the next specifier assigns
} -body {
  list [scan {12 34} {%*d %d} c] $c
} -cleanup {
  unset -nocomplain c
} -constraints {scan} -result {1 34}}

###############################################################################

runTest {test scan-11.2 {
  R-08988-52516: multiple suppressed conversions are all discarded
} -body {
  list [scan {12 34 56} {%*d %*d %d} d] $d
} -cleanup {
  unset -nocomplain d
} -constraints {scan} -result {1 56}}

###############################################################################

runTest {test scan-11.3 {
  R-08988-52516: in list mode a suppressed conversion is omitted from the
                 returned list
} -body {
  scan {12 34} {%*d %d}
} -constraints {scan} -result {34}}

###############################################################################
#
# Section 12 -- scan: Tcl 8.6 conversion parity
#
###############################################################################

runTest {test scan-12.1 {
  R-57777-39078: %X scans hexadecimal identically to %x
} -body {
  list [scan FF %X x] $x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {1 255}}

###############################################################################

runTest {test scan-12.2 {
  R-24977-61729: %u stores the unsigned equivalent in the conversion's integer
                 type -- 32-bit by default, 64-bit with the l modifier
} -body {
  list [scan -1 %u a] $a [scan -1 %lu b] $b
} -cleanup {
  unset -nocomplain a b
} -constraints {th8 scan} -result {1 4294967295 1 18446744073709551615}}

###############################################################################

runTest {test scan-12.3 {
  R-36335-08118: %b scans a binary integer, recognizing the 0b prefix
} -body {
  list [scan 0b110 %b x] $x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {1 6}}

###############################################################################

runTest {test scan-12.4 {
  R-32826-38639: %i auto-detects the integer base (0x hex, 0 octal, else decimal)
} -body {
  list [scan 0x1f %i a] $a [scan 010 %i b] $b [scan 42 %i c] $c
} -cleanup {
  unset -nocomplain a b c
} -constraints {scan} -result {1 31 1 8 1 42}}

###############################################################################

runTest {test scan-12.5 {
  R-12730-11355: %e/%g scan a floating-point number like %f
} -body {
  list [scan 1.5e2 %e x] $x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {1 150.0}}

###############################################################################

runTest {test scan-12.6 {
  R-38982-22334: %[chars] scans a run in the set; %[^chars] a run not in the set
} -body {
  list [scan hello123 {%[a-z]%d} a b] $a $b [scan {abc def} {%[^ ]} c] $c
} -cleanup {
  unset -nocomplain a b c
} -constraints {scan} -result {2 hello 123 1 abc}}

###############################################################################

runTest {test scan-12.7 {
  R-45362-43419: a field width bounds a conversion to that many characters
} -body {
  list [scan 12345 {%3d%d} a b] $a $b
} -cleanup {
  unset -nocomplain a b
} -constraints {scan} -result {2 123 45}}

###############################################################################

runTest {test scan-12.8 {
  R-57836-23904: the size modifier selects the stored integer type -- hh 8-bit,
                 h 16-bit, none 32-bit, l/L 64-bit, ll BigInt (sign/zero
                 extended per signedness); an unsigned ll of a negative errors
} -body {
  list \
      [scan 200 %hhd p] $p \
      [scan 40000 %hd q] $q \
      [scan 3000000000 %d r] $r \
      [scan 4294967296 %ld s] $s \
      [scan 123456789012345678901234567890 %lld t] $t \
      [catch {scan -1 %llu u}]
} -cleanup {
  unset -nocomplain p q r s t u
} -constraints {th8 scan} -result \
    {1 -56 1 -25536 1 -1294967296 1 4294967296 1 123456789012345678901234567890 1}}

###############################################################################

runTest {test scan-12.9 {
  R-29110-64897: conversions except %c/%[/%n skip leading whitespace; %c does not
} -body {
  list [scan {   hi} %s a] $a [scan { A} %c b] $b
} -cleanup {
  unset -nocomplain a b
} -constraints {scan} -result {1 hi 1 32}}

###############################################################################

runTest {test scan-12.10 {
  R-61594-17219: %% matches a literal percent
} -body {
  list [scan %50 {%%%d} x] $x
} -cleanup {
  unset -nocomplain x
} -constraints {scan} -result {1 50}}

###############################################################################

runTest {test scan-12.11 {
  R-22618-63798: a floating-point conversion stores a double by default and a
                 single-precision float with the h modifier; other size
                 modifiers (l, etc.) are an error
} -body {
  scan 3.14159265358979 %hf b
  list [scan 0.5 %f a] $a \
      [expr {$b != 3.14159265358979}] \
      [catch {scan 1.0 %lf c}]
} -cleanup {
  unset -nocomplain a b c
} -constraints {th8 scan} -result {1 0.5 1 1}}

###############################################################################

runTest {test scan-12.12 {
  R-54996-11407: list mode returns one element per non-suppressed specifier,
                 padding unsatisfied trailing specifiers with empty strings,
                 but an EOF before any conversion yields the empty list
} -body {
  list [scan abc {%s %s}] [scan {abc} {%d %s}] [scan {} {%s %s}] [scan {5} {%d %d}]
} -constraints {scan} -result {{abc {}} {{} {}} {} {5 {}}}}

###############################################################################

source tests/epilogue.tcl
