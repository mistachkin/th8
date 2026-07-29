###############################################################################
#
# string.tcl --
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
# Section 1 -- string compare
#
###############################################################################

runTest {test string-1.1 {
  R-42418-26915: string compare returns 0 for equal strings
} -body {
  string compare "abc" "abc"
} -result {0}}

###############################################################################

runTest {test string-1.2 {
  R-16156-64588: string compare returns -1 when first string is less
} -body {
  string compare "abc" "abd"
} -result {-1}}

###############################################################################

runTest {test string-1.3 {
  R-2000-0003: string compare returns 1 when first string is greater
} -body {
  string compare "abd" "abc"
} -result {1}}

###############################################################################
#
# Section 2 -- string first / string last
#
###############################################################################

runTest {test string-2.1 {
  R-04230-52791: string first finds substring and returns index
} -body {
  string first "cd" "abcdef"
} -result {2}}

###############################################################################

runTest {test string-2.2 {
  R-2000-0005: string first returns -1 when substring not found
} -body {
  string first "xyz" "abcdef"
} -result {-1}}

###############################################################################

runTest {test string-2.3 {
  R-02521-52297: string last finds last occurrence of substring
} -body {
  string last "ab" "ababab"
} -result {4}}

###############################################################################

runTest {test string-2.4 {
  R-2000-0007: string last returns -1 when substring not found
} -body {
  string last "xyz" "abcdef"
} -result {-1}}

###############################################################################
#
# Section 3 -- string index / string range / string length
#
###############################################################################

runTest {test string-3.1 {
  R-29414-04053: string index returns character at given position
} -body {
  string index "abcdef" 2
} -result {c}}

###############################################################################

runTest {test string-3.2 {
  R-23190-27431: string index returns empty for out of range index
} -body {
  string index "abc" 10
} -result {}}

###############################################################################

runTest {test string-3.3 {
  R-40313-14181: string index with end returns last character
} -body {
  string index "abcdef" end
} -result {f}}

###############################################################################

runTest {test string-3.4 {
  R-07615-63304: string range returns substring between indices
} -body {
  string range "abcdef" 1 3
} -result {bcd}}

###############################################################################

runTest {test string-3.5 {
  R-2000-0012: string range with end returns to end of string
} -body {
  string range "abcdef" 2 end
} -result {cdef}}

###############################################################################

runTest {test string-3.6 {
  R-54796-49566: string length returns number of characters
} -body {
  string length "hello"
} -result {5}}

###############################################################################

runTest {test string-3.7 {
  R-27926-63046: string length of empty string returns 0
} -body {
  string length ""
} -result {0}}

###############################################################################
#
# Section 4 -- string repeat
#
###############################################################################

runTest {test string-4.1 {
  R-63308-24468: string repeat duplicates string specified number of times
} -body {
  string repeat "ab" 3
} -result {ababab}}

###############################################################################

runTest {test string-4.2 {
  R-59360-18389: string repeat zero times returns empty string
} -body {
  string repeat "ab" 0
} -result {}}

###############################################################################
#
# Section 5 -- string trim / trimleft / trimright
#
###############################################################################

runTest {test string-5.1 {
  R-47333-52710: string trim removes leading and trailing whitespace
} -body {
  string trim "  hello  "
} -result {hello}}

###############################################################################

runTest {test string-5.2 {
  R-05938-07453: string trim removes custom characters from both ends
} -body {
  string trim "xxhelloxx" "x"
} -result {hello}}

###############################################################################

runTest {test string-5.3 {
  R-26884-20829: string trimleft removes leading whitespace only
} -body {
  string trimleft "  hello  "
} -result {hello  }}

###############################################################################

runTest {test string-5.4 {
  R-52896-29531: string trimright removes trailing whitespace only
} -body {
  string trimright "  hello  "
} -result {  hello}}

###############################################################################
#
# Section 6 -- string tolower / string toupper
#
###############################################################################

runTest {test string-6.1 {
  R-24333-26171: string tolower converts all characters to lowercase
} -body {
  string tolower "HELLO WORLD"
} -result {hello world}}

###############################################################################

runTest {test string-6.2 {
  R-23460-14939: string toupper converts all characters to uppercase
} -body {
  string toupper "hello world"
} -result {HELLO WORLD}}

###############################################################################
#
# Section 7 -- string match
#
###############################################################################

runTest {test string-7.1 {
  R-61454-64199: string match returns 1 for exact match
} -body {
  string match "hello" "hello"
} -result {1}}

###############################################################################

runTest {test string-7.2 {
  R-14747-06481: string match with asterisk wildcard matches
} -body {
  string match "hel*" "hello"
} -result {1}}

###############################################################################

runTest {test string-7.3 {
  R-38300-13377: string match with question mark matches single character
} -body {
  string match "h?llo" "hello"
} -result {1}}

###############################################################################

runTest {test string-7.4 {
  R-2000-0026: string match returns 0 when pattern does not match
} -body {
  string match "xyz*" "hello"
} -result {0}}

###############################################################################

runTest {test string-7.5 {
  R-43744-48634: string match with bracket character range matches
} -body {
  string match {[a-z]ello} "hello"
} -result {1}}

###############################################################################
#
# Section 8 -- string map
#
###############################################################################

runTest {test string-8.1 {
  R-34313-48141: string map performs basic character substitution
} -body {
  string map {a A e E} "abcde"
} -result {AbcdE}}

###############################################################################

runTest {test string-8.2 {
  R-2000-0029: string map with no matching key leaves string unchanged
} -body {
  string map {x y} "hello"
} -result {hello}}

###############################################################################

runTest {test string-8.3 {
  R-2000-0030: string map replaces multi-character key
} -body {
  string map {ll LL} "hello"
} -result {heLLo}}

###############################################################################
#
# Section 9 -- string is
#
###############################################################################

runTest {test string-9.1 {
  R-26556-03981: string is integer returns 1 for valid integer
} -body {
  string is integer "42"
} -result {1}}

###############################################################################

runTest {test string-9.2 {
  R-2000-0032: string is integer returns 0 for non-integer
} -body {
  string is integer "abc"
} -result {0}}

###############################################################################

runTest {test string-9.3 {
  R-2000-0033: string is double returns 1 for valid double
} -body {
  string is double "3.14"
} -result {1}}

###############################################################################

runTest {test string-9.4 {
  R-2000-0034: string is alpha returns 1 for all-alphabetic string
} -body {
  string is alpha "abcXYZ"
} -result {1}}

###############################################################################

runTest {test string-9.5 {
  R-2000-0035: string is alpha returns 0 when string contains digits
} -body {
  string is alpha "abc123"
} -result {0}}

###############################################################################

runTest {test string-9.6 {
  R-2000-0036: string is digit returns 1 for all-digit string
} -body {
  string is digit "12345"
} -result {1}}

###############################################################################

runTest {test string-9.7 {
  R-2000-0037: string is alnum returns 1 for alphanumeric string
} -body {
  string is alnum "abc123"
} -result {1}}

###############################################################################

runTest {test string-9.8 {
  R-2000-0038: string is space returns 1 for all-whitespace string
} -body {
  string is space "  \t\n"
} -result {1}}

###############################################################################

runTest {test string-9.9 {
  R-2000-0039: string is list returns 1 for valid list
} -body {
  string is list "a b c"
} -result {1}}

###############################################################################
#
# Section 10 -- string: error cases
#
###############################################################################

runTest {test string-10.1 {
  R-2000-0040: string with no subcommand is error
} -setup {
} -body {
  list [catch {string} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################

runTest {test string-10.2 {
  R-2000-0041: string with unknown subcommand is error
} -setup {
} -body {
  list [catch {string nosuch "hello"} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 11 -- string match: additional pattern tests
#
###############################################################################

runTest {test string-11.1 {
  R-61454-64199: string match with * at end matches remaining chars
} -body {
  string match "hello*" "hello world"
} -result {1}}

###############################################################################

runTest {test string-11.2 {
  R-61454-64199: string match empty pattern only matches empty string
} -body {
  list [string match "" ""] [string match "" "notempty"]
} -result {1 0}}

###############################################################################

runTest {test string-11.3 {
  R-61454-64199: string match with backslash escaping special characters
} -body {
  string match {\*literal\*} "*literal*"
} -result {1}}

###############################################################################

runTest {test string-11.4 {
  R-61454-64199: string match backslash-escaped question mark
} -body {
  string match {abc\?} "abc?"
} -result {1}}

###############################################################################

runTest {test string-11.5 {
  R-16347-20268: string match with multiple ranges in brackets
} -body {
  list [string match {[a-zA-Z]} "m"] [string match {[a-zA-Z]} "M"] \
      [string match {[a-zA-Z]} "5"]
} -result {1 1 0}}

###############################################################################

runTest {test string-11.6 {
  R-61454-64199: string match with * in middle matches any sequence
} -body {
  string match "h*d" "hello world"
} -result {1}}

###############################################################################
#
# Section 12 -- string is: additional type checks
#
###############################################################################

runTest {test string-12.1 {
  R-26556-03981: string is integer with valid positive integer
} -body {
  string is integer "12345"
} -result {1}}

###############################################################################

runTest {test string-12.2 {
  R-26556-03981: string is integer with valid negative integer
} -body {
  string is integer "-42"
} -result {1}}

###############################################################################

runTest {test string-12.3 {
  R-26556-03981: string is integer with floating point is false
} -body {
  string is integer "3.14"
} -result {0}}

###############################################################################

runTest {test string-12.4 {
  R-26556-03981: string is integer with empty string
} -body {
  string is integer ""
} -result {1}}

###############################################################################

runTest {test string-12.5 {
  R-26556-03981: string is double with valid double
} -body {
  string is double "3.14159"
} -result {1}}

###############################################################################

runTest {test string-12.6 {
  R-26556-03981: string is double with scientific notation
} -body {
  string is double "1.5e10"
} -result {1}}

###############################################################################

runTest {test string-12.7 {
  R-26556-03981: string is double with non-numeric string
} -body {
  string is double "hello"
} -result {0}}

###############################################################################

runTest {test string-12.8 {
  R-26556-03981: string is double with integer is true
} -body {
  string is double "42"
} -result {1}}

###############################################################################

runTest {test string-12.9 {
  R-26556-03981: string is alpha with mixed alpha and punctuation
} -body {
  string is alpha "hello!"
} -result {0}}

###############################################################################

runTest {test string-12.10 {
  R-26556-03981: string is alpha with single character
} -body {
  string is alpha "Z"
} -result {1}}

###############################################################################

runTest {test string-12.11 {
  R-26556-03981: string is space with whitespace-only string
} -body {
  string is space "   \t  \n  "
} -result {1}}

###############################################################################

runTest {test string-12.12 {
  R-26556-03981: string is space with non-whitespace content
} -body {
  string is space " hello "
} -result {0}}

###############################################################################
#
# Section 13 -- string compare: additional tests
#
###############################################################################

runTest {test string-13.1 {
  R-42418-26915: string compare returns 0 for identical strings
} -body {
  string compare "the quick brown fox" "the quick brown fox"
} -result {0}}

###############################################################################

runTest {test string-13.2 {
  R-42418-26915: string compare with empty strings
} -body {
  string compare "" ""
} -result {0}}

###############################################################################

runTest {test string-13.3 {
  R-42418-26915: string compare empty vs non-empty
} -body {
  expr {[string compare "" "a"] < 0}
} -result {1}}

###############################################################################
#
# Section 14 -- string first: startIndex parameter
#
###############################################################################

runTest {test string-14.1 {
  R-04230-52791: string first with startIndex skips earlier matches
} -body {
  string first "ab" "ababab" 2
} -result {2}}

###############################################################################

runTest {test string-14.2 {
  R-04230-52791: string first with startIndex past all occurrences
} -body {
  string first "ab" "ababab" 5
} -result {-1}}

###############################################################################

runTest {test string-14.3 {
  R-04230-52791: string first with startIndex finds later occurrence
} -body {
  string first "cd" "abcdabcd" 4
} -result {6}}

###############################################################################
#
# Section 15 -- string map: multiple replacements
#
###############################################################################

runTest {test string-15.1 {
  R-34313-48141: string map with multiple replacement pairs
} -body {
  string map {foo bar baz qux} "foo and baz"
} -result {bar and qux}}

###############################################################################

runTest {test string-15.2 {
  R-34313-48141: string map with overlapping source patterns
} -body {
  string map {ab AB a X} "abc"
} -result {ABc}}

###############################################################################

runTest {test string-15.3 {
  R-34313-48141: string map with empty replacement value
} -body {
  string map {world {}} "hello world"
} -result {hello }}

###############################################################################

runTest {test string-15.4 {
  R-34313-48141: string map replaces all occurrences
} -body {
  string map {o 0} "hello world foo"
} -result {hell0 w0rld f00}}

###############################################################################
#
# Section 16 -- string compare: -nocase and -length options
#
###############################################################################

runTest {test string-16.1 {
  R-41690-44412: string compare -nocase equal
} -body {
  string compare -nocase "ABC" "abc"
} -result {0}}

###############################################################################

runTest {test string-16.2 {
  R-41690-44412: string compare -nocase less
} -body {
  string compare -nocase "abc" "DEF"
} -result {-1}}

###############################################################################

runTest {test string-16.3 {
  R-41690-44412: string compare -nocase greater
} -body {
  string compare -nocase "XYZ" "abc"
} -result {1}}

###############################################################################

runTest {test string-16.4 {
  R-41690-44412: string compare -nocase mixed case
} -body {
  string compare -nocase "HeLLo" "hEllO"
} -result {0}}

###############################################################################

runTest {test string-16.5 {
  R-55448-42983: string compare -length truncates
} -body {
  string compare -length 3 "abcdef" "abcxyz"
} -result {0}}

###############################################################################

runTest {test string-16.6 {
  R-55448-42983: string compare -length 4 differs
} -body {
  string compare -length 4 "abcdef" "abcxyz"
} -result {-1}}

###############################################################################

runTest {test string-16.7 {
  R-55448-42983: string compare -nocase -length combined
} -body {
  string compare -nocase -length 3 "ABCdef" "abcxyz"
} -result {0}}

###############################################################################

runTest {test string-16.8 {
  R-55448-42983: string compare -length 0 always equal
} -body {
  string compare -length 0 "abc" "xyz"
} -result {0}}

###############################################################################
#
# Section 17 -- string replace
#
###############################################################################

runTest {test string-17.1 {
  R-34028-21505: string replace replaces character range
} -setup {
} -body {
  set result [string replace hello 1 3 XY]
} -cleanup {
  unset -nocomplain result
} -result {hXYo}}

###############################################################################

runTest {test string-17.2 {
  R-63394-43490: string replace with no newString deletes range
} -setup {
} -body {
  set result [string replace abcdef 2 4]
} -cleanup {
  unset -nocomplain result
} -result {abf}}

###############################################################################

runTest {test string-17.3 {
  R-34028-21505: string replace out of range returns unchanged
} -body {
  string replace hello 10 20 X
} -result {hello}}

###############################################################################
#
# Section 18 -- string totitle
#
###############################################################################

runTest {test string-18.1 {
  R-21634-12986: string totitle converts first char to upper
} -body {
  string totitle hello
} -result {Hello}}

###############################################################################

runTest {test string-18.2 {
  R-21634-12986: string totitle lowercases remaining chars
} -body {
  string totitle HELLO
} -result {Hello}}

###############################################################################

runTest {test string-18.3 {
  R-40657-41865: string totitle with range
} -body {
  string totitle "hello world" 6 end
} -result {hello World}}

###############################################################################
#
# Section 19 -- string wordend / wordstart / bytelength / reverse
#
###############################################################################

runTest {test string-19.1 {
  R-16185-02807: string wordend returns index after word
} -body {
  string wordend "hello world" 2
} -result {5}}

###############################################################################

runTest {test string-19.2 {
  R-54004-17470: string wordstart returns index of word start
} -body {
  string wordstart "hello world" 7
} -result {6}}

###############################################################################

runTest {test string-19.3 {
  R-56057-18552: string bytelength returns byte count
} -body {
  string bytelength hello
} -result {5}}

###############################################################################

runTest {test string-19.3.1 {
  R-56057-18552: string bytelength of raw bytes via \xNN
  escapes returns the byte count even when string length
  reports a smaller code-point count (raw bytes may form
  coincidentally-valid UTF-8 multi-byte sequences).
} -constraints {th8} -body {
  string bytelength "\xaa\xbb\xcc\xdd"
} -result {4}}

###############################################################################

runTest {test string-19.3.2 {
  R-56057-18552: string bytelength of a UTF-8 multi-byte
  character returns the byte count; string length returns
  the code-point count.  Compare against `string length`
  to make the byte-vs-char distinction explicit.
} -body {
  set s "é"
  list [string length $s] [string bytelength $s]
} -cleanup {
  unset -nocomplain s
} -result {1 2}}

###############################################################################

runTest {test string-19.3.3 {
  R-56057-18552: string bytelength of empty string is 0.
} -body {
  string bytelength ""
} -result {0}}

###############################################################################

runTest {test string-19.3.4 {
  R-56057-18552: string bytelength of NUL byte is 1.
} -constraints {th8} -body {
  string bytelength "\x00"
} -result {1}}

###############################################################################

runTest {test string-19.4 {
  R-32232-36774: string reverse reverses characters
} -body {
  string reverse hello
} -result {olleh}}

###############################################################################

runTest {test string-19.5 {
  R-32232-36774: string reverse on empty string
} -body {
  string reverse ""
} -result {}}

###############################################################################

source tests/epilogue.tcl
