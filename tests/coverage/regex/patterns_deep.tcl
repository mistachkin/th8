###############################################################################
#
# patterns_deep.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests that exercise deeper paths through the Spencer regex engine:
# complex quantifiers, nested groups, alternation edge cases, Unicode
# character classes, back-references with quantifiers, and patterns
# that stress the NFA/DFA compilation.
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
# Section 1 -- Quantifier edge cases
#
###############################################################################

runTest {test patterns_deep-1.1 {
  bounded quantifier exact count
} -constraints {
    regexp
} -body {
  regexp {^a{3}$} "aaa"
} -result {1}}

###############################################################################

runTest {test patterns_deep-1.2 {
  bounded quantifier too few
} -constraints {
    regexp
} -body {
  regexp {^a{3}$} "aa"
} -result {0}}

###############################################################################

runTest {test patterns_deep-1.3 {
  bounded quantifier range
} -constraints {
    regexp
} -body {
  list [regexp {^a{2,4}$} "a"] \
      [regexp {^a{2,4}$} "aa"] \
      [regexp {^a{2,4}$} "aaa"] \
      [regexp {^a{2,4}$} "aaaa"] \
      [regexp {^a{2,4}$} "aaaaa"]
} -result {0 1 1 1 0}}

###############################################################################

runTest {test patterns_deep-1.4 {
  bounded quantifier minimum only
} -constraints {
    regexp
} -body {
  regexp {^a{2,}$} "aaaaaa"
} -result {1}}

###############################################################################

runTest {test patterns_deep-1.5 {
  non-greedy bounded quantifier
} -constraints {
    regexp
} -body {
  regexp -inline {a{2,4}?} "aaaa"
} -result {aa}}

###############################################################################

runTest {test patterns_deep-1.6 {
  zero-or-more non-greedy
} -constraints {
    regexp
} -body {
  regexp -inline {a*?b} "aaab"
} -result {aaab}}

###############################################################################

runTest {test patterns_deep-1.7 {
  one-or-more non-greedy
} -constraints {
    regexp
} -body {
  regexp -inline {a+?} "aaa"
} -result {a}}

###############################################################################

runTest {test patterns_deep-1.8 {
  optional non-greedy
} -constraints {
    regexp
} -body {
  regexp -inline {a??b} "ab"
} -result {ab}}

###############################################################################
#
# Section 2 -- Nested groups and alternation
#
###############################################################################

runTest {test patterns_deep-2.1 {
  nested capturing groups
} -constraints {
    regexp
} -body {
  regexp -inline {((a)(b))(c)} "abc"
} -result {abc ab a b c}}

###############################################################################

runTest {test patterns_deep-2.2 {
  alternation in group
} -constraints {
    regexp
} -body {
  regexp -inline {(cat|dog|bird)} "I have a dog"
} -result {dog dog}}

###############################################################################

runTest {test patterns_deep-2.3 {
  alternation prefers leftmost match
} -constraints {
    regexp
} -body {
  regexp -inline {(a|ab)} "ab"
} -match glob -result {*}}

###############################################################################

runTest {test patterns_deep-2.4 {
  deeply nested groups
} -constraints {
    regexp
} -body {
  regexp {^((((a))))$} "a"
} -result {1}}

###############################################################################

runTest {test patterns_deep-2.5 {
  non-capturing group with quantifier
} -constraints {
    regexp
} -body {
  regexp -inline {(?:ab)+} "ababab"
} -result {ababab}}

###############################################################################

runTest {test patterns_deep-2.6 {
  alternation of different lengths
} -constraints {
    regexp
} -body {
  regexp -inline {(abc|de|f)} "xdey"
} -result {de de}}

###############################################################################
#
# Section 3 -- Character classes (deep paths)
#
###############################################################################

runTest {test patterns_deep-3.1 {
  negated character class
} -constraints {
    regexp
} -body {
  regexp -inline {[^aeiou]+} "hello"
} -result {h}}

###############################################################################

runTest {test patterns_deep-3.2 {
  character class with dash at edges
} -constraints {
    regexp
} -body {
  list [regexp {^[-abc]+$} "-a-b-c-"] \
      [regexp {^[abc-]+$} "a-b-c"]
} -result {1 1}}

###############################################################################

runTest {test patterns_deep-3.3 {
  POSIX character class in bracket expression
} -constraints {
    regexp posixCharClass
} -body {
  regexp -inline {[[:upper:]]+} "helloWORLD"
} -result {WORLD}}

###############################################################################

runTest {test patterns_deep-3.4 {
  multiple POSIX classes
} -constraints {
    regexp posixCharClass
} -body {
  regexp {^[[:alpha:][:digit:]]+$} "abc123"
} -result {1}}

###############################################################################

runTest {test patterns_deep-3.5 {
  shorthand class \d \w \s
} -constraints {
    regexp
} -body {
  list [regexp {^\d+$} "12345"] \
      [regexp {^\w+$} "hello_42"] \
      [regexp {^\s+$} "  \t "]
} -result {1 1 1}}

###############################################################################

runTest {test patterns_deep-3.6 {
  negated shorthand class \D \W \S
} -constraints {
    regexp
} -body {
  list [regexp {\D} "12345"] \
      [regexp {\W} "hello"] \
      [regexp {\S} "   "]
} -result {0 0 0}}

###############################################################################

runTest {test patterns_deep-3.7 {
  character range spanning alphabet
} -constraints {
    regexp
} -body {
  regexp {^[a-z]+$} "thequickbrownfox"
} -result {1}}

###############################################################################
#
# Section 4 -- Anchors and constraints
#
###############################################################################

runTest {test patterns_deep-4.1 {
  \A matches only at string start
} -constraints {
    regexp
} -body {
  list [regexp {\Ahello} "hello world"] \
      [regexp {\Aworld} "hello world"]
} -result {1 0}}

###############################################################################

runTest {test patterns_deep-4.2 {
  \Z matches at string end
} -constraints {
    regexp
} -body {
  list [regexp {world\Z} "hello world"] \
      [regexp {hello\Z} "hello world"]
} -result {1 0}}

###############################################################################

runTest {test patterns_deep-4.3 {
  word boundary \m and \M
} -constraints {
    regexp constraintEscape
} -body {
  regexp -inline {\mfoo\M} "a foo bar"
} -result {foo}}

###############################################################################

runTest {test patterns_deep-4.4 {
  word start boundary \m matches word starts
} -constraints {
    regexp constraintEscape
} -body {
  # Match beginning-of-word boundaries
  regexp -all -inline {\m\w+} "hello world"
} -result {hello world}}

###############################################################################

runTest {test patterns_deep-4.5 {
  -lineanchor makes ^ and $ match at newlines
} -constraints {
    regexp
} -body {
  regexp -lineanchor -all {^[a-z]+} "abc\ndef\nghi"
} -result {3}}

###############################################################################

runTest {test patterns_deep-4.6 {
  -linestop prevents dot matching newline
} -constraints {
    regexp
} -body {
  list [regexp -linestop {a.b} "a\nb"] \
      [regexp -linestop {a.b} "axb"]
} -result {0 1}}

###############################################################################

runTest {test patterns_deep-4.7 {
  -line is -lineanchor + -linestop
} -constraints {
    regexp
} -body {
  regexp -line -all {^.+$} "abc\ndef"
} -result {2}}

###############################################################################
#
# Section 5 -- Back-references in patterns
#
###############################################################################

runTest {test patterns_deep-5.1 {
  simple back-reference
} -constraints {
    regexp
} -body {
  regexp {^(.)\1$} "aa"
} -result {1}}

###############################################################################

runTest {test patterns_deep-5.2 {
  back-reference no match
} -constraints {
    regexp
} -body {
  regexp {^(.)\1$} "ab"
} -result {0}}

###############################################################################

runTest {test patterns_deep-5.3 {
  back-reference with quantifier
} -constraints {
    regexp
} -body {
  regexp {^(.+) \1$} "abc abc"
} -result {1}}

###############################################################################

runTest {test patterns_deep-5.4 {
  multiple back-references
} -constraints {
    regexp
} -body {
  regexp -inline {^(.)(.)\2\1$} "abba"
} -result {abba a b}}

###############################################################################
#
# Section 6 -- Lookahead and lookbehind
#
###############################################################################

runTest {test patterns_deep-6.1 {
  positive lookahead
} -constraints {
    regexp
} -body {
  regexp -inline {\w+(?=\.)} "hello.world"
} -result {hello}}

###############################################################################

runTest {test patterns_deep-6.2 {
  negative lookahead
} -constraints {
    regexp
} -body {
  regexp -inline {foo(?!bar)} "foobar foobaz"
} -result {foo}}

###############################################################################

runTest {test patterns_deep-6.3 {
  positive lookbehind
} -constraints {
    regexp lookbehind
} -body {
  regexp -inline {(?<=@)\w+} "user@host"
} -result {host}}

###############################################################################

runTest {test patterns_deep-6.4 {
  negative lookbehind
} -constraints {
    regexp lookbehind constraintEscape
} -body {
  regexp -inline {(?<!@)\mhost\M} "my host"
} -result {host}}

###############################################################################
#
# Section 7 -- Embedded options
#
###############################################################################

runTest {test patterns_deep-7.1 {
  embedded case insensitive (?i)
} -constraints {
    regexp
} -body {
  regexp {(?i)hello} "HELLO"
} -result {1}}

###############################################################################

runTest {test patterns_deep-7.2 {
  expanded mode (?x) ignores whitespace
} -constraints {
    regexp
} -body {
  regexp {(?x)
    \d+     # digits
    \.      # dot
    \d+     # more digits
  } "3.14"
} -result {1}}

###############################################################################

runTest {test patterns_deep-7.3 {
  -expanded switch with comments
} -constraints {
    regexp
} -body {
  regexp -expanded {
    ^               # start
    [A-Z] [a-z]+    # capitalized word
    $               # end
  } "Hello"
} -result {1}}

###############################################################################
#
# Section 8 -- Complex real-world patterns
#
###############################################################################

runTest {test patterns_deep-8.1 {
  email-like pattern
} -constraints {
    regexp
} -body {
  regexp {^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$} \
      "user@example.com"
} -result {1}}

###############################################################################

runTest {test patterns_deep-8.2 {
  IPv4-like pattern
} -constraints {
    regexp
} -body {
  regexp -inline {(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})} \
      "addr 192.168.1.100 end"
} -result {192.168.1.100 192 168 1 100}}

###############################################################################

runTest {test patterns_deep-8.3 {
  HTML tag extraction
} -constraints {
    regexp
} -body {
  regexp -inline {<(\w+)[^>]*>} "<div class=\"foo\">"
} -result {{<div class="foo">} div}}

###############################################################################

runTest {test patterns_deep-8.4 {
  repeated alternation
} -constraints {
    regexp
} -body {
  regexp -all {(?:cat|dog)} "a cat and a dog and another cat"
} -result {3}}

###############################################################################

runTest {test patterns_deep-8.5 {
  CSV-like field extraction
} -constraints {
    regexp
} -body {
  regexp -inline -all {[^,]+} "a,bb,ccc,d"
} -result {a bb ccc d}}

###############################################################################

runTest {test patterns_deep-8.6 {
  pattern with many branches stresses NFA
} -constraints {
    regexp
} -body {
  regexp {^(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p|q|r|s|t|u|v|w|x|y|z)+$} \
      "thequickbrownfoxjumps"
} -result {1}}

###############################################################################
#
# Section 9 -- Multi-byte UTF-8 in patterns
#
###############################################################################

runTest {test patterns_deep-9.1 {
  match Unicode letter
} -constraints {
    regexp
} -body {
  regexp {\u00e9} "caf\u00e9"
} -result {1}}

###############################################################################

runTest {test patterns_deep-9.2 {
  capture Unicode content
} -constraints {
    regexp
} -body {
  regexp -inline {(caf\u00e9)} "a caf\u00e9 b"
} -result "caf\u00e9 caf\u00e9"}

###############################################################################

runTest {test patterns_deep-9.3 {
  dot matches multi-byte character
} -constraints {
    regexp
} -body {
  regexp {^.{4}$} "caf\u00e9"
} -result {1}}

###############################################################################

runTest {test patterns_deep-9.4 {
  character class with multi-byte
} -constraints {
    regexp
} -body {
  regexp -all {[\u00e0-\u00ff]} "\u00e9\u00e8\u00f1"
} -result {3}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
