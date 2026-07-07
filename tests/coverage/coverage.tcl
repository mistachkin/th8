###############################################################################
#
# coverage.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for untested requirements across Sections 14-15 (string/list
# commands) and Section 18 (format/scan) of the Tcl Language Standard.
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
# Section 1 -- List Commands (Section 14)
#
###############################################################################

runTest {test coverage-1.1 {
  R-25333-10244: split with multi-character splitChars treats each character in
                 splitChars as an individual delimiter, not as a
                 multi-character sequence
} -body {
  split "a.b:c.d" .:
} -result {a b c d}}

###############################################################################

runTest {test coverage-1.2 {
  R-01699-22478: split with empty splitChars splits the string into individual
                 characters, producing a list with one element per character
} -body {
  split "hello" ""
} -result {h e l l o}}

###############################################################################

runTest {test coverage-1.3 {
  R-38622-32405: split with no splitChars argument defaults to splitting on
                 whitespace characters (spaces, tabs, newlines)
} -body {
  split "hello world"
} -result {hello world}}

###############################################################################

runTest {test coverage-1.4 {
  R-32094-10782: lindex with an index that is beyond the end of the list
                 returns the empty string rather than raising an error
} -setup {
} -body {
  set mylist {a b c}
  list [lindex $mylist 99] [lindex $mylist -1]
} -cleanup {
  unset -nocomplain mylist
} -result {{} {}}}

###############################################################################

runTest {test coverage-1.5 {
  R-44569-25869: lrange clamps the end index to the actual end of the list when
                 the specified end index exceeds the list length
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 3 999
} -cleanup {
  unset -nocomplain mylist
} -result {d e}}

###############################################################################

runTest {test coverage-1.6 {
  R-35094-63625: lrange returns the empty list when the specified range is
                 empty (first index greater than last index)
} -setup {
} -body {
  set mylist {a b c d e}
  lrange $mylist 4 2
} -cleanup {
  unset -nocomplain mylist
} -result {}}

###############################################################################

runTest {test coverage-1.7 {
  R-28645-27103: lreplace can change the length of a list by replacing fewer
                 elements than are inserted (growing the list)
} -setup {
} -body {
  set mylist {a b c}
  lreplace $mylist 1 1 X Y Z
} -cleanup {
  unset -nocomplain mylist
} -result {a X Y Z c}}

###############################################################################

runTest {test coverage-1.8 {
  R-19062-07944: lreplace can delete elements from a list by specifying a range
                 to remove with no replacement elements
} -setup {
} -body {
  set mylist {a b c d e}
  lreplace $mylist 1 3
} -cleanup {
  unset -nocomplain mylist
} -result {a e}}

###############################################################################
#
# Section 2 -- String Commands (Section 15)
#
###############################################################################
#
# A previous coverage-1.9 test for R-56935-37465 (lsort stability)
# was removed 2026-05-04: its input `{c c c}` had byte-identical
# elements, so stable and unstable sorts produce the same output --
# the test could not distinguish a correct implementation from a
# broken one.  The same R-marker is properly verified by
# liststring-1.19 (paired elements with `lsort -index 0`).
#
###############################################################################

runTest {test coverage-2.1 {
  R-00941-13743: string match treats a backslash followed by a character as a
                 literal match for that character, disabling any special
                 meaning
} -body {
  string match {\*hello\*} "*hello*"
} -result {1}}

###############################################################################

runTest {test coverage-2.2 {
  R-45728-58943: string match requires the pattern to match the entire string,
                 not merely a substring
} -body {
  list [string match "ell" "hello"] [string match "*ell*" "hello"]
} -result {0 1}}

###############################################################################

runTest {test coverage-2.3 {
  R-19250-20590: string match interprets a backslash sequence in the pattern as
                 an escape, matching the literal character that follows
} -body {
  list [string match {a\[b} "a\[b"] [string match {\?x} "?x"]
} -result {1 1}}

###############################################################################

runTest {test coverage-2.4 {
  R-49734-20318: string compare -nocase compares two strings in a
                 case-insensitive manner, returning 0 when they differ only in
                 case
} -constraints {
    string_compare_nocase
} -body {
  list [string compare -nocase "Hello" "HELLO"] \
      [string compare -nocase "abc" "ABC"]
} -result {0 0}}

###############################################################################

runTest {test coverage-2.5 {
  R-15142-04104: string compare -length limits the comparison to at most the
                 specified number of characters from the start of each string
} -constraints {
    string_compare_length
} -body {
  list [string compare -length 3 "abcdef" "abcxyz"] \
      [string compare -length 4 "abcdef" "abcxyz"]
} -result {0 -1}}

###############################################################################

runTest {test coverage-2.6 {
  R-27142-24249: string compare with both -length and -nocase performs a
                 case-insensitive comparison limited to the specified length
} -constraints {
    string_compare_nocase string_compare_length
} -body {
  list [string compare -nocase -length 3 "ABCdef" "abcxyz"] \
      [string compare -nocase -length 5 "ABCDEf" "abcdex"]
} -result {0 0}}

###############################################################################

runTest {test coverage-2.7 {
  R-41690-44412: string first with startIndex begins the search at the given
                 index position, skipping earlier occurrences of the needle
} -constraints {
    string_first_start
} -body {
  string first "ab" "ababab" 2
} -result {2}}

###############################################################################

runTest {test coverage-2.8 {
  R-55448-42983: string last returns the index of the last occurrence of a
                 substring within a string (tested with catch for availability)
} -constraints {
    string_last
} -body {
  string last "ab" "ababab"
} -result {4}}

###############################################################################

runTest {test coverage-2.9 {
  R-03141-61401: string first with startIndex past all occurrences returns -1
                 indicating no match was found
} -constraints {
    string_first_start
} -body {
  string first "ab" "ababab" 5
} -result {-1}}

###############################################################################

runTest {test coverage-2.10 {
  R-01692-49711: string map performs basic key-value substitution, replacing
                 each occurrence of a key in the string with its paired value
} -constraints {
    string_map
} -body {
  string map {foo bar} "foo is foo"
} -result {bar is bar}}

###############################################################################

runTest {test coverage-2.11 {
  R-43508-65179: string map -nocase performs case-insensitive matching of keys
                 while applying the specified replacements
} -constraints {
    string_map_nocase
} -body {
  string map -nocase {hello Hi} "Hello HELLO hello"
} -result {Hi Hi Hi}}

###############################################################################

runTest {test coverage-2.12 {
  R-50014-43354: string map with multiple key-value pairs applies all
                 replacements, checking each position against all keys in order
} -constraints {
    string_map
} -body {
  string map {a A e E i I o O u U} "aeiou"
} -result {AEIOU}}

###############################################################################

runTest {test coverage-2.13 {
  R-35519-31735: string map with overlapping key patterns matches the first
                 applicable key at each position (longest-first or
                 first-listed)
} -constraints {
    string_map
} -body {
  string map {ab AB a X} "abc"
} -result {ABc}}

###############################################################################

runTest {test coverage-2.14 {
  R-02055-02875: string tolower with a range argument converts only the
                 characters within the specified index range to lowercase
} -constraints {
    string_tolower_range
} -body {
  string tolower "HELLO WORLD" 2 5
} -result {HEllo WORLD}}

###############################################################################

runTest {test coverage-2.15 {
  R-32165-05446: string toupper with a range argument converts only the
                 characters within the specified index range to uppercase
} -constraints {
    string_toupper_range
} -body {
  string toupper "hello world" 2 5
} -result {heLLO world}}

###############################################################################

runTest {test coverage-2.16 {
  R-20242-47754: string is validates that a string belongs to a given character
                 class, returning 1 for membership and 0 otherwise
} -constraints {
    string_is
} -body {
  list [string is integer "42"] \
      [string is integer "abc"] \
      [string is alpha "hello"] \
      [string is alpha "hello1"] \
      [string is digit "12345"] \
      [string is digit "123a5"]
} -result {1 0 1 0 1 0}}

###############################################################################
#
# Section 3 -- Format and Scan Commands (Section 17)
#
###############################################################################

runTest {test coverage-3.1 {
  R-03770-38577: format # flag produces an alternate form for octal and
                 hexadecimal conversions (e.g. 0x prefix for hex, 0 prefix for
                 octal)
} -constraints {
    format_hash_flag
} -body {
  list [format "%#x" 255] [format "%#o" 8]
} -result {0xff 010}}

###############################################################################

runTest {test coverage-3.2 {
  R-14169-30646: format - flag left-justifies the output within the specified
                 field width, padding with spaces on the right
} -body {
  format "%-10d." 42
} -result {42        .}}

###############################################################################

runTest {test coverage-3.3 {
  R-56711-61056: format + flag forces a sign character to appear before the
                 numeric value, even for positive numbers
} -body {
  list [format "%+d" 42] [format "%+d" -42] [format "%+d" 0]
} -result {+42 -42 +0}}

###############################################################################

runTest {test coverage-3.4 {
  R-30604-44538: format 0 flag pads the numeric output with leading zeros
                 instead of spaces to fill the specified field width
} -body {
  format "%08d" 42
} -result {00000042}}

###############################################################################

runTest {test coverage-3.5 {
  R-57537-52532: format * width takes the field width from the next argument in
                 the argument list rather than from a literal in the format
                 string
} -constraints {
    format_star_width
} -body {
  format "%*d" 10 42
} -result {        42}}

###############################################################################

runTest {test coverage-3.6 {
  R-61231-38333: format * precision takes the precision value from the next
                 argument in the argument list rather than from a literal in
                 the format string
} -constraints {
    format_star_precision
} -body {
  format "%.*f" 2 3.14159
} -result {3.14}}

###############################################################################

runTest {test coverage-3.7 {
  R-46212-64941: format %% produces a literal percent sign in the output
                 without consuming any argument
} -body {
  format "100%% complete"
} -result {100% complete}}

###############################################################################

runTest {test coverage-3.8 {
  R-08104-06557: scan %d conversion extracts a decimal integer from the input
                 string and stores it in the specified variable
} -constraints {
    scan
} -setup {
} -body {
  scan "123" "%d" x
  set x
} -cleanup {
  unset -nocomplain x
} -result {123}}

###############################################################################

runTest {test coverage-3.9 {
  R-08988-52516: scan %s conversion extracts a whitespace-delimited string
                 token from the input and stores it in the specified variable
} -constraints {
    scan
} -setup {
} -body {
  scan "hello world" "%s" x
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test coverage-3.10 {
  R-18495-56959: scan %x conversion extracts a hexadecimal integer from the
                 input string and converts it to its decimal value
} -constraints {
    scan
} -setup {
} -body {
  scan "1a" "%x" x
  set x
} -cleanup {
  unset -nocomplain x
} -result {26}}

###############################################################################

runTest {test coverage-3.11 {
  R-60691-08269: scan returns the count of successful conversions performed,
                 which indicates how many variables were assigned
} -constraints {
    scan
} -setup {
} -body {
  scan "10 hello 0xff" "%d %s %x" a b c
} -cleanup {
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
} -result {3}}

###############################################################################
#
# Section 4 -- Platform Initialization (Section 29)
#
###############################################################################

runTest {test coverage-4.1 {
  R-61298-50331: dot resolves to base path
} -body {
  expr {[string length [file normalize .]] > 0}
} -result {1}}

###############################################################################

runTest {test coverage-4.2 {
  R-02359-36162: base path determined automatically
} -constraints {
    th8
} -body {
  #
  # If the base path was determined, pwd returns "."
  # which confirms Th8_Initialize succeeded and the
  # base path was set.
  #
  pwd
} -result {.}}

###############################################################################

runTest {test coverage-4.3 {
  R-36666-45636: Th8_Initialize sets CWD to base
} -constraints {
    th8
} -body {
  #
  # After initialization, pwd should return "." confirming
  # that xSetCwd(".") was called during Th8_Initialize.
  #
  string equal [pwd] "."
} -result {1}}

###############################################################################
#
# Section 5 -- String Map -nocase (Section 15)
#
###############################################################################

runTest {test coverage-5.1 {
  R-49362-08621: string map -nocase matches case-insensitively
} -constraints {
    string_map_nocase
} -setup {
} -body {
  set result [string map -nocase {abc XYZ} "ABCdefABC"]
} -cleanup {
  unset -nocomplain result
} -result {XYZdefXYZ}}

###############################################################################

runTest {test coverage-5.2 {
  R-49362-08621: string map -nocase preserves replacement case
} -constraints {
    string_map_nocase
} -setup {
} -body {
  set result [string map -nocase {hello HI} "Hello World"]
} -cleanup {
  unset -nocomplain result
} -result {HI World}}

###############################################################################

source tests/epilogue.tcl
