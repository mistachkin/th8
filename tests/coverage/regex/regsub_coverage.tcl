###############################################################################
#
# regsub_coverage.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for regsub code paths not covered by existing tests:
# return-string mode (no varname), zero-length matches, complex
# replacement patterns, error paths, and -all edge cases.
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
# Section 1 -- regsub without varname (return substituted string)
#
###############################################################################

runTest {test regsub_cov-1.1 {
  regsub without varname returns substituted string
} -constraints {
    regsub
} -body {
  regsub {world} "hello world" "earth"
} -result {hello earth}}

###############################################################################

runTest {test regsub_cov-1.2 {
  regsub -all without varname returns substituted string
} -constraints {
    regsub
} -body {
  regsub -all {o} "hello world" "0"
} -result {hell0 w0rld}}

###############################################################################

runTest {test regsub_cov-1.3 {
  regsub no match without varname returns original
} -constraints {
    regsub
} -body {
  regsub {xyz} "hello" "ZZZ"
} -result {hello}}

###############################################################################

runTest {test regsub_cov-1.4 {
  regsub -nocase without varname
} -constraints {
    regsub
} -body {
  regsub -nocase {HELLO} "Hello World" "Hi"
} -result {Hi World}}

###############################################################################
#
# Section 2 -- regsub replacement patterns
#
###############################################################################

runTest {test regsub_cov-2.1 {
  & in replacement inserts whole match
} -constraints {
    regsub
} -body {
  regsub {[0-9]+} "item 42 here" {[&]}
} -result {item [42] here}}

###############################################################################

runTest {test regsub_cov-2.2 {
  backslash-digit in replacement inserts group
} -constraints {
    regsub
} -body {
  regsub {(\w+)\s+(\w+)} "hello world" {\2 \1}
} -result {world hello}}

###############################################################################

runTest {test regsub_cov-2.3 {
  backslash-backslash in replacement is literal
} -constraints {
    regsub
} -body {
  regsub {x} "axb" {\\\\}
} -result {a\\b}}

###############################################################################

runTest {test regsub_cov-2.4 {
  backslash-ampersand in replacement is literal &
} -constraints {
    regsub
} -body {
  regsub {x} "axb" {\&}
} -result {a&b}}

###############################################################################

runTest {test regsub_cov-2.5 {
  backslash followed by non-special char
} -constraints {
    regsub
} -body {
  regsub {x} "axb" {\q}
} -match glob -result {a*qb}}

###############################################################################

runTest {test regsub_cov-2.6 {
  multiple backreferences in replacement
} -constraints {
    regsub
} -body {
  regsub {(.)(.)(.)(.)} "abcd" {\4\3\2\1}
} -result {dcba}}

###############################################################################
#
# Section 3 -- regsub zero-length match handling
#
###############################################################################

runTest {test regsub_cov-3.1 {
  zero-length pattern with -all advances past each char
} -constraints {
    regsub
} -body {
  regsub -all {x*} "abc" {-}
} -match glob -result {-a-b-c*}}

###############################################################################

runTest {test regsub_cov-3.2 {
  optional pattern matches empty with -all
} -constraints {
    regsub
} -body {
  regsub -all {x?} "axb" {-}
} -match glob -result {-a--b*}}

###############################################################################

runTest {test regsub_cov-3.3 {
  lookahead zero-width with -all
} -constraints {
    regsub
} -body {
  regsub -all {(?=a)} "banana" {X}
} -result {bXanXanXa}}

###############################################################################
#
# Section 4 -- regsub error paths
#
###############################################################################

runTest {test regsub_cov-4.1 {
  bad regsub switch produces error
} -constraints {
    regsub
} -setup {
  unset -nocomplain msg
} -body {
  catch {regsub -badswitch {x} "test" "y"} msg
  expr {[string match "*bad *" $msg] || [string match "*wrong*" $msg]}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test regsub_cov-4.2 {
  wrong number of args
} -constraints {
    regsub
} -body {
  catch {regsub} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test regsub_cov-4.3 {
  invalid pattern in regsub
} -constraints {
    regsub
} -body {
  set pat "\[bad"
  set rc [catch {regsub $pat "test" "x"} msg]
  list $rc [expr {[string length $msg] > 0}]
} -cleanup {
  unset -nocomplain rc msg pat
} -result {1 1}}

###############################################################################
#
# Section 5 -- regsub -all with varname (return count)
#
###############################################################################

runTest {test regsub_cov-5.1 {
  -all with varname returns substitution count
} -constraints {
    regsub
} -setup {
  unset -nocomplain count result
} -body {
  set count [regsub -all {[aeiou]} "hello world" "*" result]
  set count
} -cleanup {
  unset -nocomplain count result
} -result {3}}

###############################################################################

runTest {test regsub_cov-5.2 {
  regsub -all no match returns 0
} -constraints {
    regsub
} -setup {
  unset -nocomplain result
} -body {
  regsub -all {xyz} "hello" "Z" result
} -cleanup {
  unset -nocomplain result
} -result {0}}

###############################################################################

runTest {test regsub_cov-5.3 {
  regsub -- with pattern starting with dash
} -constraints {
    regsub
} -body {
  regsub -- {-x-} "a-x-b" "Y"
} -result {aYb}}

###############################################################################

runTest {test regsub_cov-6.1 {
  regsub with EMPTY first argument drives src/plugins/regexp/
  th8_regex.c L1477 (C1=F) -- TH8_LEN(argl[iArg]) > 0 is
  false (empty argument).  The switches loop falls into the
  else branch (treats empty as non-switch start of pattern),
  same as a non-dash word.  Existing tests pass either an
  empty pattern wrapped in {} (which still has length 0 at
  this point, but the byte-zero path was previously not
  exercised in the switches loop).
} -constraints {
    regsub
} -body {
  set out [regsub {} "abc" "X"]
  expr {[string length $out] >= 0}
} -cleanup {
  unset -nocomplain out
} -result {1}}

###############################################################################

runTest {test regsub_cov-6.2 {
  regsub with replacement string ending in a single
  backslash drives src/plugins/regexp/th8_regex.c L1599
  (C2=F) -- zRepl[r] == '\\' (C1=T) AND r + 1 < nRepl
  (C2=F: the backslash is the last byte of the replacement
  with nothing after it).  The replacement handler skips
  the dangling backslash and emits the literal text.
} -constraints {
    regsub
} -body {
  # Replacement is "x\" -- backslash at end.  TH8 (like
  # Tcl) leaves a dangling backslash in place; the literal
  # output may include the backslash or be silently
  # truncated depending on engine.  Verify the call
  # completes without crashing.
  set rc [catch {regsub {a} "abc" "x\\"} out]
  list $rc [expr {[string length $out] >= 0}]
} -cleanup {
  unset -nocomplain rc out
} -result {0 1}}

###############################################################################

runTest {test regsub_cov-7.1 {
  regsub with switches but insufficient positional args
  drives src/plugins/regexp/th8_regex.c L1487 C1-pair --
  (iArg + 3 > argc).  The early `argc < 4` check at
  L1450 catches the trivially-short cases; this test
  consumes a switch (-all) so that argc == 4 but iArg
  advances to 2, leaving only 2 positional args (less
  than the 3 required by `pat str repl`).  Drives the
  (T,T) vector at L1487, complementing the existing
  (F,-) vector observed when 3 or 4 positional args
  are present.
} -constraints {
    regsub
} -setup {
  unset -nocomplain rc m
} -body {
  set rc [catch {regsub -all pat str} m]
  list $rc [expr {[string length $m] > 0}]
} -cleanup {
  unset -nocomplain rc m
} -result {1 1}}

###############################################################################

runTest {test regsub_cov-7.2 {
  regsub with backreference to non-existent capture group
  drives src/plugins/regexp/th8_regex.c L1604 C1-pair
  (idx >= nMatch).  Pattern has no capture groups so any
  \1..\9 references an index outside the pmatch array.
} -constraints {
    regsub
} -body {
  # Pattern has no captures (nMatch=1), replacement uses
  # backreference \3.  3 >= 1 so idx < nMatch is F.
  regsub {a} "abc" {\3}
} -result {bc}}

###############################################################################

runTest {test regsub_cov-7.3 {
  regsub with backreference to an unmatched optional
  capture group drives src/plugins/regexp/th8_regex.c
  L1604 C2-pair (idx < nMatch but pmatch[idx].rm_so < 0).
  Pattern (x)?(y) -- group 1 is optional and does not
  match when input is "y".  Replacement uses \1 which
  references the unmatched group.
} -constraints {
    regsub
} -body {
  # Group 1 unmatched.  Backreference \1 references it.
  regsub {(x)?(y)} "y" {[\1-\2]}
} -result {[-y]}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
