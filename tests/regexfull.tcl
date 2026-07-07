###############################################################################
#
# regexfull.tcl --
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
# Section 1 -- Quantifiers
#
###############################################################################

runTest {test regexfull-1.1 {
  R-28969-19748: exact count {n} matches exactly n repetitions
} -constraints {
    regexp
} -body {
  list [regexp {a{3}} "aaa"] [regexp {a{3}} "aa"]
} -result {1 0}}

###############################################################################

runTest {test regexfull-1.2 {
  R-28969-19748: exact count {n} captures correct substring
} -constraints {
    regexp
} -setup {
} -body {
  regexp {a{3}} "xaaaay" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {aaa}}

###############################################################################

runTest {test regexfull-1.3 {
  R-29967-02128: n-or-more {n,} matches n or more repetitions
} -constraints {
    regexp
} -body {
  list [regexp {a{2,}} "aaa"] [regexp {a{2,}} "a"]
} -result {1 0}}

###############################################################################

runTest {test regexfull-1.4 {
  R-20758-24731: range {n,m} matches between n and m repetitions
} -constraints {
    regexp
} -setup {
} -body {
  regexp {a{2,4}} "aaaaaaa" match
  list [regexp {a{2,4}} "aaa"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 aaaa}}

###############################################################################

runTest {test regexfull-1.5 {
  R-47559-26507: non-greedy *? matches minimal repetitions
} -constraints {
    regexp
} -setup {
} -body {
  regexp {a.*?b} "aXbYb" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {aXb}}

###############################################################################

runTest {test regexfull-1.6 {
  R-47559-26507: non-greedy +? matches minimal one-or-more
} -constraints {
    regexp
} -setup {
} -body {
  regexp {a.+?b} "aXYb" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {aXYb}}

###############################################################################

runTest {test regexfull-1.7 {
  R-47559-26507: non-greedy ?? matches minimal zero-or-one
} -constraints {
    regexp
} -setup {
} -body {
  regexp {(a)??(b)} "ab" match sub
  set match
} -cleanup {
  unset -nocomplain match
  unset -nocomplain sub
} -result {ab}}

###############################################################################

runTest {test regexfull-1.8 {
  R-47559-26507: greedy vs non-greedy * comparison
} -constraints {
    regexp
} -setup {
} -body {
  regexp {a.*b} "aXbYb" greedy
  regexp {a.*?b} "aXbYb" lazy
  list $greedy $lazy
} -cleanup {
  unset -nocomplain greedy
  unset -nocomplain lazy
} -result {aXbYb aXb}}

###############################################################################
#
# Section 2 -- Alternation
#
###############################################################################

runTest {test regexfull-2.1 {
  R-54246-17008: basic alternation matches either branch
} -constraints {
    regexp
} -body {
  list [regexp {cat|dog} "I have a dog"] [regexp {cat|dog} "I have a cat"] \
      [regexp {cat|dog} "I have a fish"]
} -result {1 1 0}}

###############################################################################

runTest {test regexfull-2.2 {
  R-54246-17008: grouped alternation captures matched branch
} -constraints {
    regexp
} -setup {
} -body {
  regexp {(cat|dog)s} "dogs" all sub
  list $all $sub
} -cleanup {
  unset -nocomplain all
  unset -nocomplain sub
} -result {dogs dog}}

###############################################################################

runTest {test regexfull-2.3 {
  R-54246-17008: alternation matches longest branch (ARE)
} -constraints {
    regexp areLongestMatch
} -setup {
} -body {
  regexp {abc|abcdef} "abcdef" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {abcdef}}

###############################################################################
#
# Section 3 -- Character Classes
#
###############################################################################

runTest {test regexfull-3.1 {
  R-04954-51251: [:digit:] matches digit characters
} -constraints {
    regexp posixCharClass
} -setup {
} -body {
  regexp {[[:digit:]]+} "abc123def" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {123}}

###############################################################################

runTest {test regexfull-3.2 {
  R-58577-10255: [:alpha:] matches alphabetic characters
} -constraints {
    regexp posixCharClass
} -setup {
} -body {
  regexp {[[:alpha:]]+} "123abc456" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {abc}}

###############################################################################

runTest {test regexfull-3.3 {
  R-49612-39968: [:space:] matches whitespace characters
} -constraints {
    regexp posixCharClass
} -setup {
} -body {
  regexp {[[:space:]]+} "hello world" match
  set match
} -cleanup {
  unset -nocomplain match
} -result { }}

###############################################################################

runTest {test regexfull-3.4 {
  R-02272-51548: [:alnum:] matches alphanumeric characters
} -constraints {
    regexp posixCharClass
} -setup {
} -body {
  regexp {[[:alnum:]]+} "---abc123---" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {abc123}}

###############################################################################

runTest {test regexfull-3.5 {
  R-49340-27957: character range [a-z] matches lowercase letters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {[a-z]+} "Hello" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {ello}}

###############################################################################

runTest {test regexfull-3.6 {
  R-64253-04052: negated class [^0-9] matches non-digits
} -constraints {
    regexp
} -setup {
} -body {
  regexp {[^0-9]+} "abc123def" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {abc}}

###############################################################################
#
# Section 4 -- Escape Sequences
#
###############################################################################

runTest {test regexfull-4.1 {
  R-47123-17767: \d matches digit characters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\d+} "abc123def" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {123}}

###############################################################################

runTest {test regexfull-4.2 {
  R-18473-15349: \D matches non-digit characters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\D+} "123abc456" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {abc}}

###############################################################################

runTest {test regexfull-4.3 {
  R-24262-61017: \s matches whitespace characters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\s+} "hello world" match
  set match
} -cleanup {
  unset -nocomplain match
} -result { }}

###############################################################################

runTest {test regexfull-4.4 {
  R-18473-15349: \S matches non-whitespace characters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\S+} "  hello  " match
  set match
} -cleanup {
  unset -nocomplain match
} -result {hello}}

###############################################################################

runTest {test regexfull-4.5 {
  R-62736-24340: \w matches word characters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\w+} "---hello_world---" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {hello_world}}

###############################################################################

runTest {test regexfull-4.6 {
  R-18473-15349: \W matches non-word characters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\W+} "abc---def" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {---}}

###############################################################################

runTest {test regexfull-4.7 {
  R-00196-22132: \t matches tab character
} -constraints {
    regexp
} -body {
  regexp {\t} "hello\tworld"
} -result {1}}

###############################################################################

runTest {test regexfull-4.8 {
  R-57379-18020: \n matches newline character
} -constraints {
    regexp
} -body {
  regexp {\n} "hello\nworld"
} -result {1}}

###############################################################################
#
# Section 5 -- Constraint Escapes
#
###############################################################################

runTest {test regexfull-5.1 {
  R-32401-59222: \y matches word boundary
} -constraints {
    regexp constraintEscape
} -body {
  list [regexp {\yword\y} "a word here"] [regexp {\yword\y} "swordfish"]
} -result {1 0}}

###############################################################################

runTest {test regexfull-5.2 {
  R-05573-61453: \A matches start of string
} -constraints {
    regexp
} -body {
  list [regexp {\Ahello} "hello world"] [regexp {\Ahello} "say hello"]
} -result {1 0}}

###############################################################################

runTest {test regexfull-5.3 {
  R-16169-01932: \Z matches end of string
} -constraints {
    regexp
} -body {
  list [regexp {world\Z} "hello world"] [regexp {world\Z} "world hello"]
} -result {1 0}}

###############################################################################

runTest {test regexfull-5.4 {
  R-06227-52955: \m matches beginning of word and \M matches end of word
} -constraints {
    regexp constraintEscape
} -setup {
} -body {
  regexp {\mcat\M} "the cat sat" match
  list [regexp {\mcat\M} "the cat sat"] \
      [regexp {\mcat\M} "concatenate"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 0 cat}}

###############################################################################
#
# Section 6 -- Back-References
#
###############################################################################

runTest {test regexfull-6.1 {
  R-32448-61263: \1 matches text captured by first group
} -constraints {
    regexp
} -body {
  regexp {(a+)b\1} "aabaa"
} -result {1}}

###############################################################################

runTest {test regexfull-6.2 {
  R-32448-61263: back-reference fails when captured text does not repeat
} -constraints {
    regexp
} -body {
  regexp {(a)b\1} "abc"
} -result {0}}

###############################################################################

runTest {test regexfull-6.3 {
  R-32448-61263: back-reference captures correct substring
} -constraints {
    regexp
} -setup {
} -body {
  regexp {([a-z]+):\1} "abc:abc" all sub
  list $all $sub
} -cleanup {
  unset -nocomplain all
  unset -nocomplain sub
} -result {abc:abc abc}}

###############################################################################
#
# Section 7 -- Look-ahead
#
###############################################################################

runTest {test regexfull-7.1 {
  R-35267-17023: positive look-ahead matches when followed by pattern
} -constraints {
    regexp
} -body {
  regexp {foo(?=bar)} "foobar"
} -result {1}}

###############################################################################

runTest {test regexfull-7.2 {
  R-35267-17023: positive look-ahead fails when not followed by pattern
} -constraints {
    regexp
} -body {
  regexp {foo(?=bar)} "foobaz"
} -result {0}}

###############################################################################

runTest {test regexfull-7.3 {
  R-62481-14132: negative look-ahead matches when not followed by pattern
} -constraints {
    regexp
} -body {
  regexp {foo(?!bar)} "foobaz"
} -result {1}}

###############################################################################

runTest {test regexfull-7.4 {
  R-62481-14132: negative look-ahead fails when followed by pattern
} -constraints {
    regexp
} -body {
  regexp {foo(?!bar)} "foobar"
} -result {0}}

###############################################################################
#
# Section 8 -- Look-behind
#
###############################################################################

runTest {test regexfull-8.1 {
  R-41708-30134: positive look-behind matches when preceded by pattern
} -constraints {
    regexp lookbehind
} -body {
  regexp {(?<=foo)bar} "foobar"
} -result {1}}

###############################################################################

runTest {test regexfull-8.2 {
  R-41708-30134: positive look-behind fails when not preceded by pattern
} -constraints {
    regexp lookbehind
} -body {
  regexp {(?<=foo)bar} "xyzbar"
} -result {0}}

###############################################################################

runTest {test regexfull-8.3 {
  R-45746-40475: negative look-behind matches when not preceded by pattern
} -constraints {
    regexp lookbehind
} -body {
  regexp {(?<!foo)bar} "xyzbar"
} -result {1}}

###############################################################################

runTest {test regexfull-8.4 {
  R-45746-40475: negative look-behind fails when preceded by pattern
} -constraints {
    regexp lookbehind
} -body {
  regexp {(?<!foo)bar} "foobar"
} -result {0}}

###############################################################################
#
# Section 9 -- Non-capturing Groups
#
###############################################################################

runTest {test regexfull-9.1 {
  R-41718-25558: non-capturing group groups without creating capture
} -constraints {
    regexp
} -setup {
} -body {
  set rc [regexp {(?:foo)(bar)} "foobar" all sub]
  list $rc $all $sub
} -cleanup {
  unset -nocomplain rc all sub
} -result {1 foobar bar}}

###############################################################################

runTest {test regexfull-9.2 {
  R-41718-25558: non-capturing group with alternation
} -constraints {
    regexp
} -setup {
} -body {
  set rc [regexp {(?:cat|dog)(fish)} "dogfish" all sub]
  list $rc $all $sub
} -cleanup {
  unset -nocomplain rc all sub
} -result {1 dogfish fish}}

###############################################################################
#
# Section 10 -- Embedded Options
#
###############################################################################

runTest {test regexfull-10.1 {
  R-26729-05107: (?i) enables case-insensitive matching inline
} -constraints {
    regexp
} -body {
  list [regexp {(?i)hello} "HELLO"] [regexp {(?i)hello} "HeLLo"]
} -result {1 1}}

###############################################################################

runTest {test regexfull-10.2 {
  R-26729-05107: (?i) only applies to pattern after it
} -constraints {
    regexp
} -body {
  regexp {(?i)abc} "ABC"
} -result {1}}

###############################################################################

runTest {test regexfull-10.3 {
  R-37892-07426: (?x) enables expanded mode with whitespace ignored
} -constraints {
    regexp
} -body {
  regexp {(?x) a b c} "abc"
} -result {1}}

###############################################################################
#
# Section 11 -- regexp Options
#
###############################################################################

runTest {test regexfull-11.1 {
  R-19399-61281: -all returns count of all matches
} -constraints {
    regexp
} -body {
  regexp -all {[0-9]+} "a1b22c333"
} -result {3}}

###############################################################################

runTest {test regexfull-11.2 {
  R-51773-07357: -inline returns matches as list
} -constraints {
    regexp
} -body {
  regexp -inline {([a-z]+)([0-9]+)} "abc123"
} -result {abc123 abc 123}}

###############################################################################

runTest {test regexfull-11.3 {
  R-17691-40453: -indices returns index pairs instead of strings
} -constraints {
    regexp
} -setup {
} -body {
  regexp -indices {[0-9]+} "abc123def" all
  set all
} -cleanup {
  unset -nocomplain all
  unset -nocomplain sub
} -result {3 5}}

###############################################################################

runTest {test regexfull-11.4 {
  R-23678-64539: -expanded ignores whitespace and comments in pattern
} -constraints {
    regexp
} -body {
  regexp -expanded {
    a       # match a
    b       # match b
    c       # match c
  } "abc"
} -result {1}}

###############################################################################

runTest {test regexfull-11.5 {
  R-02077-01666: -line makes newlines act as line terminators
} -constraints {
    regexp
} -body {
  regexp -line {^world$} "hello\nworld\nfoo"
} -result {1}}

###############################################################################

runTest {test regexfull-11.6 {
  R-59417-42036: -lineanchor makes ^ and $ match at newlines
} -constraints {
    regexp
} -body {
  list [regexp -lineanchor {^world} "hello\nworld"] \
      [regexp {^world} "hello\nworld"]
} -result {1 0}}

###############################################################################

runTest {test regexfull-11.7 {
  R-44438-56877: -linestop prevents dot from matching newline
} -constraints {
    regexp
} -setup {
} -body {
  regexp -linestop {h.+d} "hello\nworld" match
  list [regexp -linestop {h.+d} "hello\nworld"] \
      [regexp {h.+d} "hello\nworld"]
} -cleanup {
  unset -nocomplain match
} -result {0 1}}

###############################################################################

runTest {test regexfull-11.8 {
  R-47378-21594: -start N begins matching at given offset
} -constraints {
    regexp
} -setup {
} -body {
  regexp -start 3 {[a-z]+} "123hello" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {hello}}

###############################################################################
#
# Section 12 -- regsub Features
#
###############################################################################

runTest {test regexfull-12.1 {
  R-52185-28363: regsub without varname returns substituted string
} -constraints {
    regsub
} -body {
  regsub {[0-9]+} "abc123def" "NUM"
} -result {abcNUMdef}}

###############################################################################

runTest {test regexfull-12.2 {
  R-22922-29165: regsub \1 backreference in replacement string
} -constraints {
    regsub
} -setup {
} -body {
  regsub {([a-z]+)([0-9]+)} "abc123" {\2-\1} result
  set result
} -cleanup {
  unset -nocomplain result
} -result {123-abc}}

###############################################################################

runTest {test regexfull-12.3 {
  R-22922-29165: regsub \1 captures first subexpression
} -constraints {
    regsub
} -setup {
} -body {
  regsub {([a-z]+)([0-9]+)} "abc123def" {[\1]-[\2]} result
  set result
} -cleanup {
  unset -nocomplain result
} -result {[abc]-[123]def}}

###############################################################################

runTest {test regexfull-12.4 {
  R-30346-01991: regsub -line mode processes multiline strings
} -constraints {
    regsub
} -setup {
} -body {
  regsub -all -line {^([a-z]+)$} "hello\nworld" {[\1]} result
  string match {*hello*world*} $result
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test regexfull-12.5 {
  R-60287-61395: regsub returns count of replacements with varname
} -constraints {
    regsub
} -setup {
} -body {
  set count [regsub -all {[aeiou]} "hello world" "*" result]
  set count
} -cleanup {
  unset -nocomplain count
  unset -nocomplain result
} -result {3}}

###############################################################################
#
# Section 13 -- Anchors
#
###############################################################################

runTest {test regexfull-13.1 {
  R-59417-42036: ^ matches at newlines with -lineanchor
} -constraints {
    regexp
} -setup {
} -body {
  regexp -lineanchor {^second} "first\nsecond\nthird" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {second}}

###############################################################################

runTest {test regexfull-13.2 {
  R-59417-42036: $ matches at newlines with -lineanchor
} -constraints {
    regexp
} -setup {
} -body {
  regexp -lineanchor {first$} "first\nsecond" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {first}}

###############################################################################

runTest {test regexfull-13.3 {
  R-31107-57725: ^ and $ match only at string boundaries without -lineanchor
} -constraints {
    regexp
} -body {
  list [regexp {^hello} "hello\nworld"] \
      [regexp {^world} "hello\nworld"] \
      [regexp {world$} "hello\nworld"] \
      [regexp {hello$} "hello\nworld"]
} -result {1 0 1 0}}

###############################################################################

source tests/epilogue.tcl
