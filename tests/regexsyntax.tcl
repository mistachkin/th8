###############################################################################
#
# regexsyntax.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for regex pattern syntax (Sections 25.1-25.7).
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
# Section 1 -- Atoms and Quantifiers (Section 25.1)
#
###############################################################################

runTest {test regexsyntax-1.1 {
  R-33468-32177: dot matches any single character
} -constraints {
    regexp
} -body {
  list [regexp {a.c} "abc"] [regexp {a.c} "aXc"] [regexp {a.c} "ac"]
} -result {1 1 0}}

###############################################################################

runTest {test regexsyntax-1.2 {
  R-34777-25939: bracket expression [chars] matches any one of enclosed
                 characters
} -constraints {
    regexp
} -setup {
} -body {
  regexp {[aeiou]+} "hello" match
  list [regexp {[abc]} "a"] [regexp {[abc]} "d"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 0 e}}

###############################################################################

runTest {test regexsyntax-1.3 {
  R-64253-04052: negated bracket [^chars] matches any char NOT in set
} -constraints {
    regexp
} -setup {
} -body {
  regexp {[^0-9]+} "abc123" match
  list [regexp {[^abc]} "d"] [regexp {[^abc]} "a"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 0 abc}}

###############################################################################

runTest {test regexsyntax-1.4 {
  R-31107-57725: caret ^ matches beginning of string
} -constraints {
    regexp
} -body {
  list [regexp {^hello} "hello world"] [regexp {^hello} "say hello"]
} -result {1 0}}

###############################################################################

runTest {test regexsyntax-1.5 {
  R-51380-33368: dollar $ matches end of string
} -constraints {
    regexp
} -body {
  list [regexp {world$} "hello world"] [regexp {world$} "world hello"]
} -result {1 0}}

###############################################################################

runTest {test regexsyntax-1.6 {
  R-06400-59604: parentheses (re) define a capturing group
} -constraints {
    regexp
} -setup {
} -body {
  regexp {(foo)(bar)} "foobar" all sub1 sub2
  list $all $sub1 $sub2
} -cleanup {
  unset -nocomplain all
  unset -nocomplain sub1
  unset -nocomplain sub2
} -result {foobar foo bar}}

###############################################################################

runTest {test regexsyntax-1.7 {
  R-41718-25558: non-capturing (?:re) groups without capturing
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

runTest {test regexsyntax-1.8 {
  R-61274-09385: * quantifier matches zero or more times
} -constraints {
    regexp
} -setup {
} -body {
  regexp {ab*c} "ac" match
  list [regexp {ab*c} "ac"] [regexp {ab*c} "abbc"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 1 ac}}

###############################################################################

runTest {test regexsyntax-1.9 {
  R-07873-34500: + quantifier matches one or more times
} -constraints {
    regexp
} -body {
  list [regexp {ab+c} "abc"] [regexp {ab+c} "abbc"] [regexp {ab+c} "ac"]
} -result {1 1 0}}

###############################################################################

runTest {test regexsyntax-1.10 {
  R-52041-26691: ? quantifier matches zero or one time
} -constraints {
    regexp
} -body {
  list [regexp {ab?c} "ac"] [regexp {ab?c} "abc"] [regexp {ab?c} "abbc"]
} -result {1 1 0}}

###############################################################################

runTest {test regexsyntax-1.11 {
  R-28969-19748: {n} quantifier matches exactly n times
} -constraints {
    regexp
} -body {
  list [regexp {a{3}} "aaa"] [regexp {a{3}} "aa"] [regexp {a{3}} "aaaa"]
} -result {1 0 1}}

###############################################################################

runTest {test regexsyntax-1.12 {
  R-29967-02128: {n,} quantifier matches n or more times
} -constraints {
    regexp
} -setup {
} -body {
  regexp {a{2,}} "aaaa" match
  list [regexp {a{2,}} "aa"] [regexp {a{2,}} "a"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 0 aaaa}}

###############################################################################

runTest {test regexsyntax-1.13 {
  R-20758-24731: {n,m} quantifier matches at least n and at most m times
} -constraints {
    regexp
} -setup {
} -body {
  regexp {a{2,4}} "aaaaaa" match
  list [regexp {a{2,4}} "aa"] [regexp {a{2,4}} "a"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 0 aaaa}}

###############################################################################

runTest {test regexsyntax-1.14 {
  R-47559-26507: appending ? makes quantifier non-greedy
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
# Section 2 -- Character Classes (Section 25.2)
#
###############################################################################

runTest {test regexsyntax-2.1 {
  R-49340-27957: range a-z inside bracket matches chars in range
} -constraints {
    regexp
} -setup {
} -body {
  regexp {[a-z]+} "ABC123hello" match
  list [regexp {[a-z]} "m"] [regexp {[a-z]} "5"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 0 hello}}

###############################################################################

runTest {test regexsyntax-2.2 {
  R-27381-46764: [:lower:] matches lowercase letters
} -constraints {
    regexp posixCharClass
} -setup {
} -body {
  regexp {[[:lower:]]+} "ABCdefGHI" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {def}}

###############################################################################

runTest {test regexsyntax-2.3 {
  R-13282-57984: [:upper:] matches uppercase letters
} -constraints {
    regexp posixCharClass
} -setup {
} -body {
  regexp {[[:upper:]]+} "abcDEFghi" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {DEF}}

###############################################################################

runTest {test regexsyntax-2.4 {
  R-47123-17767: \d matches any digit
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\d+} "abc456def" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {456}}

###############################################################################

runTest {test regexsyntax-2.5 {
  R-24262-61017: \s matches any whitespace
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\s+} "hello\t world" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {	 }}

###############################################################################

runTest {test regexsyntax-2.6 {
  R-62736-24340: \w matches any word character
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\w+} "---hello_42---" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {hello_42}}

###############################################################################

runTest {test regexsyntax-2.7 {
  R-18473-15349: \D, \S, \W match complements of \d, \s, \w
} -constraints {
    regexp
} -setup {
} -body {
  regexp {\D+} "123abc456" m1
  regexp {\S+} "  hello  " m2
  regexp {\W+} "abc---def" m3
  list $m1 $m2 $m3
} -cleanup {
  unset -nocomplain m1
  unset -nocomplain m2
  unset -nocomplain m3
} -result {abc hello ---}}

###############################################################################
#
# Section 3 -- Escapes and Constraints (Section 25.3)
#
###############################################################################

runTest {test regexsyntax-3.1 {
  R-57379-18020: \n matches newline
} -constraints {
    regexp
} -body {
  regexp {\n} "hello\nworld"
} -result {1}}

###############################################################################

runTest {test regexsyntax-3.2 {
  R-00196-22132: \t matches tab
} -constraints {
    regexp
} -body {
  regexp {\t} "hello\tworld"
} -result {1}}

###############################################################################

runTest {test regexsyntax-3.3 {
  R-04473-65202: \xhh matches hex character
} -constraints {
    regexp
} -body {
  list [regexp {\x41} "A"] [regexp {\x41} "B"]
} -result {1 0}}

###############################################################################

runTest {test regexsyntax-3.4 {
  R-05573-61453: \A matches only at start of string
} -constraints {
    regexp
} -body {
  list [regexp {\Ahello} "hello world"] [regexp {\Ahello} "say hello"]
} -result {1 0}}

###############################################################################

runTest {test regexsyntax-3.5 {
  R-16169-01932: \Z matches only at end of string
} -constraints {
    regexp
} -body {
  list [regexp {world\Z} "hello world"] [regexp {world\Z} "world hello"]
} -result {1 0}}

###############################################################################

runTest {test regexsyntax-3.6 {
  R-29873-37784: \M matches at end of word
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

runTest {test regexsyntax-3.7 {
  R-32448-61263: \N back-reference matches Nth capturing group
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
# Section 4 -- Lookaround (Section 25.4)
#
###############################################################################

runTest {test regexsyntax-4.1 {
  R-35267-17023: positive look-ahead (?=re) matches without consuming
} -constraints {
    regexp
} -body {
  list [regexp {foo(?=bar)} "foobar"] [regexp {foo(?=bar)} "foobaz"]
} -result {1 0}}

###############################################################################

runTest {test regexsyntax-4.2 {
  R-62481-14132: negative look-ahead (?!re) fails when pattern follows
} -constraints {
    regexp
} -body {
  list [regexp {foo(?!bar)} "foobaz"] [regexp {foo(?!bar)} "foobar"]
} -result {1 0}}

###############################################################################
#
# Section 5 -- Embedded Options (Section 25.5)
#
###############################################################################

runTest {test regexsyntax-5.1 {
  R-26729-05107: (?i) enables case-insensitive matching inline
} -constraints {
    regexp
} -body {
  list [regexp {(?i)hello} "HELLO"] [regexp {(?i)hello} "HeLLo"] \
      [regexp {hello} "HELLO"]
} -result {1 1 0}}

###############################################################################

runTest {test regexsyntax-5.2 {
  R-37892-07426: (?x) enables expanded mode ignoring whitespace
} -constraints {
    regexp
} -body {
  list [regexp {(?x) a b c} "abc"] [regexp {(?x) a b c} "a b c"]
} -result {1 0}}

###############################################################################
#
# Section 6 -- regexp Switches (Section 25.6)
#
###############################################################################

runTest {test regexsyntax-6.1 {
  R-19399-61281: -all returns count of all non-overlapping matches
} -constraints {
    regexp
} -body {
  regexp -all {[0-9]+} "a1b22c333"
} -result {3}}

###############################################################################

runTest {test regexsyntax-6.2 {
  R-17691-40453: -indices stores index pairs instead of matched strings
} -constraints {
    regexp
} -setup {
} -body {
  regexp -indices {[0-9]+} "abc123def" all
  set all
} -cleanup {
  unset -nocomplain all
} -result {3 5}}

###############################################################################

runTest {test regexsyntax-6.3 {
  R-51773-07357: -inline returns matched strings as a list
} -constraints {
    regexp
} -body {
  regexp -inline {([a-z]+)([0-9]+)} "abc123"
} -result {abc123 abc 123}}

###############################################################################

runTest {test regexsyntax-6.4 {
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

runTest {test regexsyntax-6.5 {
  R-02077-01666: -line enables both linestop and lineanchor modes
} -constraints {
    regexp
} -body {
  list [regexp -line {^world$} "hello\nworld\nfoo"] \
      [regexp -line {^world$} "helloworld"]
} -result {1 0}}

###############################################################################

runTest {test regexsyntax-6.6 {
  R-44438-56877: -linestop prevents dot from matching newline
} -constraints {
    regexp
} -body {
  list [regexp -linestop {h.+d} "hello\nworld"] \
      [regexp {h.+d} "hello\nworld"]
} -result {0 1}}

###############################################################################

runTest {test regexsyntax-6.7 {
  R-59417-42036: -lineanchor makes ^ and $ match at embedded newlines
} -constraints {
    regexp
} -setup {
} -body {
  regexp -lineanchor {^world} "hello\nworld" match
  list [regexp -lineanchor {^world} "hello\nworld"] \
      [regexp {^world} "hello\nworld"] $match
} -cleanup {
  unset -nocomplain match
} -result {1 0 world}}

###############################################################################

runTest {test regexsyntax-6.8 {
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
# Section 7 -- regsub (Section 25.7)
#
###############################################################################

runTest {test regexsyntax-7.1 {
  R-52185-28363: regsub without variable returns modified string directly
} -constraints {
    regsub
} -body {
  regsub {[0-9]+} "abc123def" "NUM"
} -result {abcNUMdef}}

###############################################################################

source tests/epilogue.tcl
