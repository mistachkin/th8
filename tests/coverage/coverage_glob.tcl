###############################################################################
#
# coverage_glob.tcl --
#
# MC/DC-targeted coverage tests for src/th8_glob.c.
#
# Th8_GlobMatch is the project's glob-style pattern matcher used by
# [string match], [switch -glob], [info commands], [info procs],
# [lsearch -glob], etc.  It contains six compound boolean
# expressions totalling 13 MC/DC conditions (per
# llvm-cov --show-mcdc); this file is structured so that each
# section drives one of those compound expressions through every
# operand-independent-effect combination MC/DC requires.
#
# Each test names the line in src/th8_glob.c whose decision it is
# exercising, the operands involved, and the boolean assignment
# tested.  Together the tests in this file should lift th8_glob.c
# from 0% MC/DC to near-100% MC/DC.
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
# Section 1 -- line 90: `if (interp && Th8_Ready(interp) != TH8_OK)`
#
# This is the inner-loop cancellation check.  Two operands:
#   C1: `interp` non-NULL
#   C2: `Th8_Ready(interp) != TH8_OK`
#
# Th8_GlobMatch is normally called from script code with a non-NULL
# interp, so C1 is always true at this call site -- but the inner
# th8GlobMatch2 is also reached via `Th8_GlobMatch(NULL, ...)` from
# host C code that doesn't have an interp to associate with.  We
# can't directly exercise the NULL-interp branch from script (every
# script call passes the live interp), so this section uses
# [string match] for the typical C1=T C2=F path; the C1=F path is
# documented as exercisable only from C-level callers (an embedder-
# style test or a deliberate platform-callback that calls
# Th8_GlobMatch with NULL).
#
###############################################################################

runTest {test glob-mcdc-1.1 {
  C1=T C2=F: live interp, ready (normal match path)
} -constraints {
    th8
} -body {
  string match abc abc
} -result {1}}

###############################################################################

runTest {test glob-mcdc-1.2 {
  C1=T C2=F: live interp, ready, match through long pattern loop
} -constraints {
    th8
} -body {
  # Longer pattern walks the inner loop multiple times so the
  # cancellation-check decision fires on every iteration.
  string match {a*b*c*d*e} {axxbxxcxxdxxe}
} -result {1}}

###############################################################################
#
# Section 2 -- line 99: `while (iPat < nPat && zPat[iPat] == '*')`
#
# This is the consecutive-star collapse loop.  Two operands:
#   C1: `iPat < nPat` (more pattern to scan)
#   C2: `zPat[iPat] == '*'`
#
# MC/DC requires:
#   C1=F: no more pattern (loop ends because we ran out of pattern)
#   C1=T C2=F: more pattern, current char is NOT '*' (loop ends)
#   C1=T C2=T: more pattern, current char IS '*' (loop continues)
#
###############################################################################

runTest {test glob-mcdc-2.1 {
  C1=F: pattern is just `*`, loop exits via length check
} -constraints {
    th8
} -body {
  # Pattern "*" -- after consuming the first '*' on line 97,
  # iPat == nPat so line 99's first operand is false.
  string match {*} anything
} -result {1}}

###############################################################################

runTest {test glob-mcdc-2.2 {
  C1=T C2=F: pattern is `*x`, loop exits because next char is not '*'
} -constraints {
    th8
} -body {
  # After the leading '*', line 99 sees iPat<nPat (true) but
  # zPat[iPat]=='x' not '*' (false).  Single iteration of the
  # outer condition with the C2=F decision.
  string match {*end} {leading-end}
} -result {1}}

###############################################################################

runTest {test glob-mcdc-2.3 {
  C1=T C2=T: pattern has multiple consecutive `**`, loop body runs
} -constraints {
    th8
} -body {
  # Pattern "***x" -- collapse consumes iterations 1 and 2 with
  # both operands true, exits on iteration 3 with C2=F.
  string match {***end} {leading-end}
} -result {1}}

###############################################################################
#
# Section 3 -- line 127: `if (iPat < nPat && zPat[iPat] == '!')`
#
# Character class inversion check.  Two operands:
#   C1: `iPat < nPat` (haven't run off pattern end inside `[...]`)
#   C2: `zPat[iPat] == '!'`
#
###############################################################################

runTest {test glob-mcdc-3.1 {
  C1=T C2=F: character class without inversion -- `[abc]`
} -constraints {
    th8
} -body {
  list \
      [string match {[abc]} a] \
      [string match {[abc]} b] \
      [string match {[abc]} d]
} -result {1 1 0}}

###############################################################################

runTest {test glob-mcdc-3.2 {
  C1=T C2=T: character class WITH inversion -- `[!abc]`
} -constraints {
    th8
} -body {
  list \
      [string match {[!abc]} a] \
      [string match {[!abc]} d]
} -result {0 1}}

###############################################################################

runTest {test glob-mcdc-3.3 {
  C1=F: malformed class `[` at end of pattern (no body, no close)
} -constraints {
    th8
} -body {
  # Pattern is just "[" -- iPat is incremented past it on line
  # 125, then iStr>=nStr check fails at line 126 (iStr=0, nStr=1
  # for input "x" so iStr<nStr).  The line 127 check fires with
  # iPat == nPat: C1=F, second operand short-circuits.
  string match {[} x
} -result {0}}

###############################################################################
#
# Section 4 -- line 131: `while (iPat < nPat && zPat[iPat] != ']')`
#
# Character-class body scan loop.  Two operands:
#   C1: `iPat < nPat`
#   C2: `zPat[iPat] != ']'`
#
###############################################################################

runTest {test glob-mcdc-4.1 {
  C1=T C2=F: well-formed class `[a]` -- loop exits on the ']'
} -constraints {
    th8
} -body {
  list \
      [string match {[a]} a] \
      [string match {[a]} b]
} -result {1 0}}

###############################################################################

runTest {test glob-mcdc-4.2 {
  C1=T C2=T: class with multiple chars `[abcdef]`
} -constraints {
    th8
} -body {
  # Loop iterates several times with both operands true before
  # the closing ']' makes C2=F.
  list \
      [string match {[abcdef]} c] \
      [string match {[abcdef]} z]
} -result {1 0}}

###############################################################################

runTest {test glob-mcdc-4.3 {
  C1=F: unterminated class `[abc`  -- loop exits on length, no ']'
} -constraints {
    th8
} -setup {
  # Build the pattern via concat so the unmatched `[` does not
  # confuse the TH8 parser's bracket counter inside `[...]`.
  set pat "\[abc"
} -body {
  # Pattern "[abc" never sees ']', so the loop terminates because
  # iPat == nPat (C1=F).  TH8's matcher then treats the class
  # as if it were properly closed (`[abc]`-style behaviour) --
  # we exercise the C1=F operand-independence proof here, with
  # 'a' inside the implicit class (matches) and 'z' outside
  # (no match).
  list \
      [string match $pat a] \
      [string match $pat z]
} -cleanup {
  unset -nocomplain pat
} -result {1 0}}

###############################################################################
#
# Section 5 -- lines 132-133:
#   `if (iPat + 2 < nPat && zPat[iPat + 1] == '-' && zPat[iPat + 2] != ']')`
#
# Three-operand decision for "is this a [a-z]-style range?".
#   C1: `iPat + 2 < nPat`
#   C2: `zPat[iPat + 1] == '-'`
#   C3: `zPat[iPat + 2] != ']'`
#
# MC/DC for 3-operand AND requires 4 distinct combinations:
#
#   {C1=F, _, _}             -- short class, no room for `[a-X]`
#   {C1=T, C2=F, _}          -- not a range (`[ab]`-style)
#   {C1=T, C2=T, C3=F}       -- looks like a range but `]` follows the dash (rare)
#   {C1=T, C2=T, C3=T}       -- real range (`[a-z]`)
#
###############################################################################

runTest {test glob-mcdc-5.1 {
  C1=F: class `[a]` -- only one char, no room for [a-X]
} -constraints {
    th8
} -body {
  string match {[a]} a
} -result {1}}

###############################################################################

runTest {test glob-mcdc-5.2 {
  C1=T C2=F: class `[ab]` -- two chars, not a range
} -constraints {
    th8
} -body {
  list \
      [string match {[ab]} a] \
      [string match {[ab]} b]
} -result {1 1}}

###############################################################################

runTest {test glob-mcdc-5.3 {
  C1=T C2=T C3=T: real range `[a-z]`
} -constraints {
    th8
} -body {
  list \
      [string match {[a-z]} m] \
      [string match {[a-z]} A] \
      [string match {[a-z]} z]
} -result {1 0 1}}

###############################################################################

runTest {test glob-mcdc-5.4 {
  C1=T C2=T C3=F: pattern `[a-]` -- dash followed by close bracket,
  treated as literal dash, NOT a range
} -constraints {
    th8
} -body {
  # Pattern `[a-]` -- the dash is at iPat+1, but zPat[iPat+2] is
  # ']' so the range branch is skipped, falling through to the
  # literal-char compare.  Matches 'a' or '-' but not 'b'.
  list \
      [string match {[a-]} a] \
      [string match {[a-]} -] \
      [string match {[a-]} b]
} -result {1 1 0}}

###############################################################################
#
# Section 6 -- line 142: `if (cCh >= cLo && cCh <= cHi)`
#
# Range membership test.  Two operands:
#   C1: `cCh >= cLo`
#   C2: `cCh <= cHi`
#
# MC/DC requires:
#   C1=F: char below range (e.g. '0' against [a-z])
#   C1=T C2=F: char above range (e.g. '~' against [a-z])
#   C1=T C2=T: char inside range
#
###############################################################################

runTest {test glob-mcdc-6.1 {
  C1=F: char below range -- '0' < 'a' against [a-z]
} -constraints {
    th8
} -body {
  string match {[a-z]} 0
} -result {0}}

###############################################################################

runTest {test glob-mcdc-6.2 {
  C1=T C2=F: char above range -- '~' (0x7e) > 'z' (0x7a) against [a-z]
} -constraints {
    th8
} -body {
  string match {[a-z]} ~
} -result {0}}

###############################################################################

runTest {test glob-mcdc-6.3 {
  C1=T C2=T: char inside range
} -constraints {
    th8
} -body {
  list \
      [string match {[a-z]} a] \
      [string match {[a-z]} m] \
      [string match {[a-z]} z]
} -result {1 1 1}}

###############################################################################
#
# Section 7 -- depth limit, recursion behaviour, and remaining
#              non-compound branches.  These contribute to LINE and
#              BRANCH coverage, not MC/DC, but are co-located here
#              for completeness.
#
###############################################################################

runTest {test glob-mcdc-7.1 {
  empty pattern matches empty string only
} -constraints {
    th8
} -body {
  list \
      [string match {} {}] \
      [string match {} a]
} -result {1 0}}

###############################################################################

runTest {test glob-mcdc-7.2 {
  `?` wildcard matches exactly one character
} -constraints {
    th8
} -body {
  list \
      [string match {a?c} abc] \
      [string match {a?c} ac] \
      [string match {a?c} aXc]
} -result {1 0 1}}

###############################################################################

runTest {test glob-mcdc-7.3 {
  backslash escapes a metacharacter
} -constraints {
    th8
} -body {
  list \
      [string match {a\*b} {a*b}] \
      [string match {a\*b} {aXb}] \
      [string match {a\?b} {a?b}] \
      [string match {a\?b} {aXb}]
} -result {1 0 1 0}}

###############################################################################

runTest {test glob-mcdc-7.4 {
  trailing backslash with no char to escape -- pattern terminates
  before string consumed -- treated as no match
} -constraints {
    th8
} -setup {
  # Build the pattern explicitly so the trailing `\` does not
  # interact with the parser's brace counter.
  set pat "abc\\"
} -body {
  string match $pat abc
} -cleanup {
  unset -nocomplain pat
} -result {0}}

###############################################################################

runTest {test glob-mcdc-7.5 {
  pattern longer than string -- mismatch
} -constraints {
    th8
} -body {
  string match {abcdef} {abc}
} -result {0}}

###############################################################################

runTest {test glob-mcdc-7.6 {
  pattern shorter than string, no `*` -- mismatch
} -constraints {
    th8
} -body {
  string match {abc} {abcdef}
} -result {0}}

###############################################################################

runTest {test glob-mcdc-7.7 {
  consecutive trailing `**` matches anything past the prefix
} -constraints {
    th8
} -body {
  list \
      [string match {abc**} abcdefghi] \
      [string match {abc**} abc] \
      [string match {abc**} ab]
} -result {1 1 0}}

###############################################################################

runTest {test glob-mcdc-7.8 {
  recursion-depth bound: many nested `*` does not crash
} -constraints {
    th8
} -body {
  # Generate a pattern with many *'s mixed with literals.  The
  # depth limit (50) protects against exponential blowup; this
  # test just verifies the path doesn't crash and either
  # matches or fails to match cleanly.
  set pat ""
  for {set i 0} {$i < 30} {incr i} {
    append pat "*a"
  }
  expr {[string match $pat aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa] in {0 1}}
} -cleanup {
  unset -nocomplain pat i
} -result {1}}

###############################################################################

runTest {test glob-mcdc-8.1 {
  string match with a SINGLE-CHARACTER NON-* pattern
  drives the C2=F vector at th8SimpleGlob
  (th8_core.c:3533) -- nPat == 1 (T) but zPat[0] != '*'
  (F).  The simple-glob path falls through to full-glob.
  Existing tests use multi-char or '*' patterns.
} -constraints {
    th8
} -body {
  list \
      [string match "a" "a"] \
      [string match "a" "b"] \
      [string match "x" "x"]
} -result {1 0 1}}

###############################################################################

runTest {test glob-mcdc-8.2 {
  string match with an EMPTY pattern drives the C1=F
  vector at th8SimpleGlob (th8_core.c:3538) -- nPat == 0
  (F), so the trailing-* prefix-match check short-circuits.
  Fall-through to full-glob, which handles empty pattern.
} -constraints {
    th8
} -body {
  list \
      [string match "" ""] \
      [string match "" "abc"]
} -result {1 0}}

###############################################################################

source tests/epilogue.tcl
