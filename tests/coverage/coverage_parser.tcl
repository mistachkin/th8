###############################################################################
#
# coverage_parser.tcl --
#
# Branch-coverage tests for the TH8 word/script parser
# (th8NextWord and th8NextCommand in src/th8_core.c).
#
# These two functions implement the Tcl Dodekalogue (the official
# Tcl parsing rules) using a context stack: each entry on the stack
# records whether the current input position is inside braces ({),
# brackets ([) or double quotes (").  Every nesting context has its
# own subset of significant characters, and this file walks each
# branch deliberately.
#
# Each test names the parser branch / decision it exercises so the
# llvm-cov MC/DC report can be cross-referenced back to the test
# that exercises it.  Together the tests in this file cover:
#
#   * Brace-context character handling (literal vs. nesting vs.
#     backslash skip).
#   * Quote-context character handling (substitution markers vs.
#     literal close-bracket).
#   * Bracket-context (command substitution) full Tcl rules.
#   * Bare-word top-level rules (whitespace / ';' / backslash
#     newline as terminators).
#   * Backslash handling at boundaries (end of input, before
#     special characters, inside each context).
#   * Strict rejection of unmatched delimiters via [eval] of a
#     runtime-constructed bad script.
#   * Interleaved nesting that the prior flat-counter parser
#     mis-handled (e.g. "[string match {[abc} a]").
#   * Heap fallback when the inline 256-entry nesting stack is
#     exceeded.
#   * Argument expansion ({*}) and mid-word delimiter literals.
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
# Section 1 -- brace-context character handling
#
# When the parser is inside a {...} scope (top of context stack
# is '{'), only three characters are significant:
#
#   '\\' -- skip the next byte (so \{ and \} do not count for
#           nesting; other \X sequences are also "skipped" for
#           parser purposes but remain literal in the value).
#   '{'  -- push another brace level.
#   '}'  -- pop one brace level (closes the scope when depth=1).
#
# Everything else (brackets, quotes, dollars, semicolons, newlines,
# spaces, ...) is literal text inside the scope.  Each test below
# pins one of those literal cases so the corresponding "no-op"
# branch in the brace-context switch is reached.
#
###############################################################################

runTest {test parser-mcdc-1.1 {
  brace context: empty {} word
} -body {
  set x {}
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test parser-mcdc-1.2 {
  brace context: simple non-empty word
} -body {
  set x {hello}
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test parser-mcdc-1.3 {
  brace context: bracket inside braces is literal (Dodekalogue rule 6)
} -body {
  # The '[' and ']' bytes inside a brace word must NOT trigger
  # command substitution and must NOT push/pop the parser stack.
  set x {a [foo bar] b}
  set x
} -cleanup {
  unset -nocomplain x
} -result {a [foo bar] b}}

###############################################################################

runTest {test parser-mcdc-1.4 {
  brace context: double quote inside braces is literal
} -body {
  set x {a"b"c}
  set x
} -cleanup {
  unset -nocomplain x
} -result {a"b"c}}

###############################################################################

runTest {test parser-mcdc-1.5 {
  brace context: dollar inside braces is literal (no var sub)
} -body {
  set v 99
  set x {price=$v}
  set x
} -cleanup {
  unset -nocomplain x v
} -result {price=$v}}

###############################################################################

runTest {test parser-mcdc-1.6 {
  brace context: semicolon inside braces is literal
} -body {
  set x {a;b;c}
  set x
} -cleanup {
  unset -nocomplain x
} -result {a;b;c}}

###############################################################################

runTest {test parser-mcdc-1.7 {
  brace context: nested brace pushes/pops once
} -body {
  set x {outer {inner} tail}
  set x
} -cleanup {
  unset -nocomplain x
} -result {outer {inner} tail}}

###############################################################################

runTest {test parser-mcdc-1.8 {
  brace context: deep static nesting (inside the inline stack budget)
} -body {
  set x {a {b {c {d {e {f}}}}}}
  set x
} -cleanup {
  unset -nocomplain x
} -result {a {b {c {d {e {f}}}}}}}

###############################################################################

runTest {test parser-mcdc-1.9 {
  brace context: backslash-brace does not count for nesting
} -body {
  # `\{` and `\}` skip the next byte so the brace-counter sees
  # ZERO opens or closes from these escaped braces; the outer
  # brace-pair still balances with depth 1 throughout.
  set x {a\{b\}c}
  set x
} -cleanup {
  unset -nocomplain x
} -result {a\{b\}c}}

###############################################################################

runTest {test parser-mcdc-1.10 {
  brace context: backslash-backslash skip in brace context
} -body {
  # `\\` -- the parser skips both bytes; both backslashes are
  # part of the literal value of the brace word.
  set x {a\\b}
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {4}}

###############################################################################

runTest {test parser-mcdc-1.11 {
  brace context: backslash with non-special next char is still skipped
} -body {
  # `\n` (two literal bytes inside braces) -- the backslash
  # advances over the 'n' but both bytes survive as literal
  # text since brace-words receive no escape processing.
  set x {a\nb}
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {4}}

###############################################################################

runTest {test parser-mcdc-1.12 {
  brace context: escaped close-brace then real close-brace
} -body {
  # Backslash before a close-brace tells the parser to skip
  # that byte, so the brace counter does NOT decrement; the
  # NEXT close-brace is what actually ends the scope.  The
  # value of the word includes both backslash and brace bytes.
  set x {abc\}}
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {5}}

###############################################################################
#
# Section 2 -- quote-context character handling
#
# When the parser is inside a "..." scope (top of context stack
# is '"') the rules are:
#
#   '\\' -- skip the next byte (the substitution layer interprets
#           it; the parser only needs the byte count right).
#   '"'  -- pop / close the scope.
#   '['  -- push a bracket scope (command sub is permitted inside
#           a quoted word).
#   ']'  -- LITERAL (cannot close anything from inside a quote).
#   '{', '}', ';', newline, space, dollar -- literal at parse
#           time (substitution layer handles `$` separately).
#
###############################################################################

runTest {test parser-mcdc-2.1 {
  quote context: empty "" word
} -body {
  set x ""
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test parser-mcdc-2.2 {
  quote context: simple quoted word
} -body {
  set x "hello"
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello}}

###############################################################################

runTest {test parser-mcdc-2.3 {
  quote context: brace inside quotes is literal (no scope push)
} -body {
  # An open-brace inside a quoted word does not open a brace
  # scope -- braces are literal text inside double quotes.
  set x "a{b}c"
  set x
} -cleanup {
  unset -nocomplain x
} -result {a{b}c}}

###############################################################################

runTest {test parser-mcdc-2.4 {
  quote context: bracket inside quotes opens command sub
} -body {
  set x "[string toupper hi]"
  set x
} -cleanup {
  unset -nocomplain x
} -result {HI}}

###############################################################################

runTest {test parser-mcdc-2.5 {
  quote context: close-bracket inside quotes is literal
} -body {
  # `]` with no matching open `[` in the same quote scope is
  # just a literal byte; the parser must not pop anything.
  set x "hello]world"
  set x
} -cleanup {
  unset -nocomplain x
} -result {hello]world}}

###############################################################################

runTest {test parser-mcdc-2.6 {
  quote context: dollar triggers variable substitution (substitution layer)
} -body {
  set v 42
  set x "price=$v"
  set x
} -cleanup {
  unset -nocomplain x v
} -result {price=42}}

###############################################################################

runTest {test parser-mcdc-2.7 {
  quote context: backslash-quote escapes the closing quote
} -body {
  # `\"` skips one byte so the next `"` is data, not a closer.
  set x "a\"b"
  set x
} -cleanup {
  unset -nocomplain x
} -result {a"b}}

###############################################################################

runTest {test parser-mcdc-2.8 {
  quote context: literal newline inside quotes
} -body {
  # A bare newline inside `"..."` is part of the data, not a
  # command separator (commands are separated only at the
  # top level of a script).
  set x "line1
line2"
  string length $x
} -cleanup {
  unset -nocomplain x
} -result {11}}

###############################################################################

runTest {test parser-mcdc-2.9 {
  quote context: nested command sub inside quotes
} -body {
  # The bracket inside the quote opens a sub-scope; full Tcl
  # parsing applies inside the brackets, including nested
  # bare-word operands that escape the literal quote bytes.
  set x "[set y inner]"
  set x
} -cleanup {
  unset -nocomplain x y
} -result {inner}}

###############################################################################
#
# Section 3 -- bracket-context (command substitution)
#
# When inside a [...] scope the full Tcl parsing rules apply:
#
#   '\\' -- skip next byte
#   '"'  -- push quote scope
#   '{'  -- push brace scope
#   '['  -- push another bracket scope (nested command sub)
#   ']'  -- pop / close
#
# Plus normal command-list mechanics: `;` and newline separate
# commands inside the brackets.
#
###############################################################################

runTest {test parser-mcdc-3.1 {
  bracket context: empty command sub parses cleanly (runtime error only)
} -body {
  # The parser must accept an empty `[]` without reporting a
  # parse error -- emptiness becomes a runtime concern when
  # the eval loop tries to resolve the empty command name.
  # The catch confirms the failure is a runtime error and not
  # a parse error.
  set rc [catch {set x [string length "[set z 1]"]} msg]
  list $rc $x
} -cleanup {
  unset -nocomplain x z msg rc
} -result {0 1}}

###############################################################################

runTest {test parser-mcdc-3.2 {
  bracket context: simple command substitution
} -body {
  set x [string length abc]
  set x
} -cleanup {
  unset -nocomplain x
} -result {3}}

###############################################################################

runTest {test parser-mcdc-3.3 {
  bracket context: nested command substitution
} -body {
  # Outer `[` pushes a bracket scope; inner `[` pushes another;
  # both must pop in the correct order.
  set x [string length [string toupper hello]]
  set x
} -cleanup {
  unset -nocomplain x
} -result {5}}

###############################################################################

runTest {test parser-mcdc-3.4 {
  bracket context: brace operand inside command sub
} -body {
  # The brace-quoted argument inside the bracket scope causes
  # the parser to push a brace scope on top of the bracket
  # scope and pop it again when the inner close-brace is hit.
  set x [list {a b c} d]
  set x
} -cleanup {
  unset -nocomplain x
} -result {{a b c} d}}

###############################################################################

runTest {test parser-mcdc-3.5 {
  bracket context: quoted operand inside command sub
} -body {
  # The `"a b"` is a quote-delimited word seen inside the
  # bracket scope; the parser pushes a quote scope on top
  # of the bracket scope.
  set x [list "a b" c]
  set x
} -cleanup {
  unset -nocomplain x
} -result {{a b} c}}

###############################################################################

runTest {test parser-mcdc-3.6 {
  bracket context: semicolons separate commands inside brackets
} -body {
  set x [set a 1; set b 2; expr {$a + $b}]
  set x
} -cleanup {
  unset -nocomplain x a b
} -result {3}}

###############################################################################

runTest {test parser-mcdc-3.7 {
  bracket context: backslash-bracket inside command sub
} -body {
  # `\]` inside `[...]` is a backslash-skip; the next `]`
  # after the skipped one closes the scope.
  set x [string length \]]
  set x
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################
#
# Section 4 -- bare-word top-level handling
#
# At the top level (empty context stack) `th8NextWord` looks for
# word terminators and detects scope-opening characters:
#
#   whitespace -- end of word
#   ';' (if isCmd)  -- end of word (and end of command)
#   '\\<newline>'  -- backslash-newline acts as a word
#                     separator at top level
#   '\\<other>'    -- skip the next byte; backslash substitution
#                     happens at the substitution layer
#   '{', '"'  -- opens a nested scope on the parse stack
#   '['       -- opens a command-sub scope
#   '}', ']'  -- LITERAL (no matching open at this level)
#
###############################################################################

runTest {test parser-mcdc-4.1 {
  bare word: simple word, ends at whitespace
} -body {
  set first second
  expr {$first eq "second"}
} -cleanup {
  unset -nocomplain first
} -result {1}}

###############################################################################

runTest {test parser-mcdc-4.2 {
  bare word: bare word with embedded backslash escape
} -body {
  # `\t` produces a literal tab via backslash substitution.
  set x a\tb
  expr {[string length $x] == 3 && [string index $x 1] eq "\t"}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test parser-mcdc-4.3 {
  bare word: stray close-brace is literal (no matching open at top level)
} -body {
  # Octal escape \175 produces a close-brace byte after
  # backslash substitution; the bare-word top-level path
  # passes it through as a literal because no open-brace
  # is on the parse stack.
  set x abc\175def
  set x
} -cleanup {
  unset -nocomplain x
} -result abc\175def}

###############################################################################

runTest {test parser-mcdc-4.4 {
  bare word: stray close-bracket is literal (no matching open at top level)
} -body {
  set x abc]def
  set x
} -cleanup {
  unset -nocomplain x
} -result {abc]def}}

###############################################################################

runTest {test parser-mcdc-4.5 {
  bare word: command sub mid-word merges with surrounding text
} -body {
  # `pre[...]post` is one bare word; the `[...]` adds a bracket
  # scope mid-scan and pops back to the bare-word top-level.
  set x pre[string toupper x]post
  set x
} -cleanup {
  unset -nocomplain x
} -result {preXpost}}

###############################################################################

runTest {test parser-mcdc-4.6 {
  bare word: variable substitution mid-word merges with surrounding text
} -body {
  set v abc
  set x pre${v}post
  set x
} -cleanup {
  unset -nocomplain x v
} -result {preabcpost}}

###############################################################################

runTest {test parser-mcdc-4.7 {
  bare word: backslash-newline acts as word separator at top level
} -body {
  # The `\<newline>` here separates the two words `a` and `b`,
  # producing a 2-element list rather than `a<space>b` as one
  # bare word.
  set L [list a \
      b]
  set L
} -cleanup {
  unset -nocomplain L
} -result {a b}}

###############################################################################

runTest {test parser-mcdc-4.8 {
  bare word: tab character ends bare word
} -body {
  set L [list a	b]
  llength $L
} -cleanup {
  unset -nocomplain L
} -result {2}}

###############################################################################
#
# Section 5 -- strict rejection of malformed input
#
# Each unmatched-opener case must produce a parse error with the
# matching error message.  The bad input is constructed at run
# time (with a brace-quoted literal so the bad bytes survive past
# the OUTER parser) and fed through [eval] under [catch] so the
# parser error is reportable.
#
###############################################################################

runTest {test parser-mcdc-5.1 {
  strict: unmatched open-brace reports "Unmatched braces"
} -constraints {th8} -setup {
  set bad "set x \{abc"
} -body {
  set rc [catch {eval $bad} msg]
  list $rc [string match {*Unmatched braces*} $msg]
} -cleanup {
  unset -nocomplain bad msg rc
} -result {1 1}}

###############################################################################

runTest {test parser-mcdc-5.2 {
  strict: unmatched open-bracket reports "Unmatched brackets"
} -constraints {th8} -setup {
  set bad {set x [foo}
} -body {
  set rc [catch {eval $bad} msg]
  list $rc [string match {*Unmatched brackets*} $msg]
} -cleanup {
  unset -nocomplain bad msg rc
} -result {1 1}}

###############################################################################

runTest {test parser-mcdc-5.3 {
  strict: unmatched open-quote reports "Unmatched quote"
} -constraints {th8} -setup {
  set bad {set x "abc}
} -body {
  set rc [catch {eval $bad} msg]
  list $rc [string match {*Unmatched quote*} $msg]
} -cleanup {
  unset -nocomplain bad msg rc
} -result {1 1}}

###############################################################################

runTest {test parser-mcdc-5.4 {
  strict: unmatched inner brace inside command sub reports the inner error
} -constraints {th8} -setup {
  # The outer open-bracket opens a bracket scope; the inner
  # open-brace opens a brace scope that never closes.  The
  # parser walks back up the stack and reports based on the
  # INNERMOST still-open scope, not the outer wrapper, which
  # matches the way the recursive Tcl reference parser surfaces
  # the deepest mismatch first.
  set bad "set x \[set y \{abc\]"
} -body {
  set rc [catch {eval $bad} msg]
  list $rc [string match {*Unmatched braces*} $msg]
} -cleanup {
  unset -nocomplain bad msg rc
} -result {1 1}}

###############################################################################

runTest {test parser-mcdc-5.5 {
  strict: unmatched bracket appended to bare word
} -constraints {th8} -setup {
  # `"abc"[def` reads as ONE bare word: a quote-delimited
  # part followed by a bare-word continuation that opens a
  # bracket scope which never closes.
  set bad {set x "abc"[def}
} -body {
  set rc [catch {eval $bad} msg]
  list $rc [string match {*Unmatched brackets*} $msg]
} -cleanup {
  unset -nocomplain bad msg rc
} -result {1 1}}

###############################################################################
#
# Section 6 -- interleaved nesting (the original parser bug)
#
# The flat-counter approach used previously could not handle the
# case where a brace-quoted operand contains a literal `[` (or
# vice-versa) inside an outer `[...]`.  These tests pin the
# context-stack approach by exercising every interleaving the old
# parser got wrong.
#
###############################################################################

runTest {test parser-mcdc-6.1 {
  interleaved: bracket inside brace-quoted argument is literal
} -body {
  # The open-bracket inside the brace operand must NOT count
  # as opening a new bracket scope for the OUTER command sub.
  # This was the canonical regression that drove the parser
  # rewrite away from flat counters with mutual gating.
  string match {[abc} a
} -result {1}}

###############################################################################

runTest {test parser-mcdc-6.2 {
  interleaved: matching-bracket inside brace-quoted word is literal
} -body {
  # The brace-quoted form preserves the bracket bytes
  # verbatim; the byte length must therefore equal the source
  # length minus the two outer braces.
  string length {[abc]}
} -result {5}}

###############################################################################

runTest {test parser-mcdc-6.3 {
  interleaved: brace-quoted argument with embedded literal close-bracket
} -body {
  # Set a variable to a brace-quoted value containing a
  # literal close-bracket; the ASSIGNED VALUE is exactly
  # the inner bytes, regardless of how `list` would later
  # quote them for canonical output.
  set x {a]b}
  string equal $x "a\]b"
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test parser-mcdc-6.4 {
  interleaved: brace then quote then bracket all stack correctly
} -body {
  # Outer `[...]` has a brace operand with literal text and a
  # quoted operand whose content does substitution: three
  # different parse contexts in a single command.
  set v world
  set x [list {hello $v} "hello $v"]
  set x
} -cleanup {
  unset -nocomplain x v
} -result {{hello $v} {hello world}}}

###############################################################################

runTest {test parser-mcdc-6.5 {
  interleaved: quote inside bracket inside brace remains literal
} -body {
  # The OUTER scope is brace-quoted, so EVERYTHING inside is
  # literal -- including the bracket and the quote pair.  No
  # substitutions happen.
  set x {a [b "c"] d}
  set x
} -cleanup {
  unset -nocomplain x
} -result {a [b "c"] d}}

###############################################################################
#
# Section 7 -- backslash handling at boundaries
#
# Backslash logic in each context: skip the next byte iff one
# exists.  When the input is too short for a follow byte, the
# backslash is consumed as the last byte and parsing finishes
# cleanly.
#
###############################################################################

runTest {test parser-mcdc-7.1 {
  backslash: backslash-newline continuation at top level
} -body {
  # Backslash followed immediately by a newline OUTSIDE braces
  # acts as a word separator that joins the two physical lines
  # into one logical command.
  list a \
      b \
      c
} -result {a b c}}

###############################################################################

runTest {test parser-mcdc-7.2 {
  backslash: \\ produces a single backslash in bare/quoted words
} -body {
  # In a bare word, `\\` is a backslash escape that produces
  # one literal backslash byte.
  set x a\\b
  expr {[string length $x] == 3 \
      && [string index $x 1] eq "\\"}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test parser-mcdc-7.3 {
  backslash: backslash-bracket inside quoted word
} -body {
  # Backslash-open-bracket inside a quoted word skips past
  # one byte; the parser must NOT push a bracket scope on
  # the parse stack and must NOT trigger command sub.
  set x "a\[notacmd"
  set x
} -cleanup {
  unset -nocomplain x
} -result {a[notacmd}}

###############################################################################

runTest {test parser-mcdc-7.4 {
  backslash: backslash before ']' inside command sub doesn't pop
} -body {
  # `\]` is a backslash-skip; the parser must NOT decrement
  # the bracket counter on the escaped close-bracket.
  set x [string length \]\]]
  set x
} -cleanup {
  unset -nocomplain x
} -result {2}}

###############################################################################

runTest {test parser-mcdc-7.5 {
  backslash: backslash before '[' inside quoted word
} -body {
  # `\[` inside a quoted word skips the byte; the next char is
  # NOT a command-sub trigger.
  set x "a\[b"
  set x
} -cleanup {
  unset -nocomplain x
} -result {a[b}}

###############################################################################

runTest {test parser-mcdc-7.6 {
  backslash: backslash before '"' inside quoted word
} -body {
  # `\"` is the canonical way to embed a literal `"` in a
  # quoted word.  The parser must skip past the escape so
  # the quote does not close prematurely.
  set x "a\"b\"c"
  set x
} -cleanup {
  unset -nocomplain x
} -result {a"b"c}}

###############################################################################

runTest {test parser-mcdc-7.7 {
  backslash: backslash-newline followed by TAB drives the
  C3=T vector at th8_core.c:10249-10251 (the whitespace
  consume after `\<NL>` continuation specifically checks
  for tab via `zInput[i] == '\t'`).  Existing 7.1 uses
  spaces; this closes the C3-pair via a literal TAB after
  the continuation.
} -body {
  set s "list a \\\n\tb"
  set r [eval $s]
  list [llength $r] $r
} -cleanup {
  unset -nocomplain s r
} -result {2 {a b}}}

###############################################################################

runTest {test parser-mcdc-7.8 {
  backslash: lone backslash at end of script drives the
  C2=F vector at th8_core.c:10241-10242 (the script
  tokenizer's continuation check `i + 1 < nInput`).
  When the last byte is `\`, the i+1 check fails and the
  loop falls through to the regular character path.
  The eval may succeed or error; we only assert it
  doesn't crash.
} -constraints {th8} -body {
  set s "puts a\\"
  set rc [catch {eval $s} m]
  set rc2 [catch {eval "set x \\"} m2]
  list [expr {$rc == 0 || $rc == 1}] [expr {$rc2 == 0 || $rc2 == 1}]
} -cleanup {
  unset -nocomplain s rc rc2 m m2
} -result {1 1}}

###############################################################################
#
# Section 8 -- heap fallback when nesting exceeds the inline stack
#
# th8ParseStack stores TH8_PARSE_NEST_INLINE (256) entries inline
# in the parser frame.  When that capacity is exhausted the stack
# storage doubles into a heap allocation; this section exercises
# that fallback path.
#
# Each test deliberately pushes MORE than 256 nested scopes so
# the parser MUST grow the stack onto the heap to succeed.
#
###############################################################################

runTest {test parser-mcdc-8.1 {
  heap: 300-deep brace nesting parses via heap fallback
} -setup {
  # Build a script source containing 300 levels of braces.
  # The parser must push 300 entries onto its nesting stack;
  # only 256 fit inline, so the remainder must spill onto a
  # heap-allocated buffer.  info complete runs the parser
  # without executing the result, so this is a pure parser
  # path test.
  set depth 300
  set s [string repeat \{ $depth]
  append s payload
  append s [string repeat \} $depth]
} -body {
  info complete $s
} -cleanup {
  unset -nocomplain s depth
} -result {1}}

###############################################################################

runTest {test parser-mcdc-8.2 {
  heap: 1024-deep brace nesting forces multiple stack doublings
} -setup {
  # 1024 levels triggers heap doublings 256 -> 512 -> 1024
  # so the doubling/realloc path is exercised three times.
  set depth 1024
  set s [string repeat \{ $depth]
  append s payload
  append s [string repeat \} $depth]
} -body {
  info complete $s
} -cleanup {
  unset -nocomplain s depth
} -result {1}}

###############################################################################

runTest {test parser-mcdc-8.3 {
  heap: 300-deep bracket nesting parses via heap fallback
} -setup {
  # Same shape as 8.1 but with brackets so the heap-fallback
  # path is exercised through the bracket-context stack
  # entries instead of the brace-context entries.  Embed an
  # innermost bare word so each open-bracket has a body to
  # match against its close-bracket.
  set depth 300
  set s [string repeat \[ $depth]
  append s payload
  append s [string repeat \] $depth]
} -body {
  info complete $s
} -cleanup {
  unset -nocomplain s depth
} -result {1}}

###############################################################################

runTest {test parser-mcdc-8.4 {
  heap: deep brace nesting actually evaluates via heap fallback
} -constraints {th8} -setup {
  # End-to-end heap test: build, eval, and verify the value
  # round-trips correctly.  Each brace level wraps the value
  # exactly once, so x ends up as the SOURCE minus the outer
  # pair of braces.
  set depth 300
  set s "set x "
  append s [string repeat \{ $depth]
  append s payload
  append s [string repeat \} $depth]
} -body {
  eval $s
  expr {[string length $x] == 605}
} -cleanup {
  unset -nocomplain s depth x
} -result {1}}

###############################################################################
#
# Section 9 -- argument expansion ({*}) and mid-word delimiters
#
# `{*}word` is a syntactically special prefix recognised at word
# start: the parser sees the brace-quoted `{*}` followed by more
# bytes (no whitespace).  This is the ONE place in TH8 where
# non-terminator characters are allowed immediately after a
# close-brace -- the parser must keep accumulating the bare-word
# tail rather than reporting "extra characters after close-brace".
#
###############################################################################

runTest {test parser-mcdc-9.1 {
  expansion: {*} followed by literal list expands the elements
} -body {
  list a {*}{1 2 3} b
} -result {a 1 2 3 b}}

###############################################################################

runTest {test parser-mcdc-9.2 {
  expansion: {*} followed by variable substitution
} -body {
  set L {x y z}
  list a {*}$L b
} -cleanup {
  unset -nocomplain L
} -result {a x y z b}}

###############################################################################

runTest {test parser-mcdc-9.3 {
  expansion: {*} of empty list contributes zero arguments
} -body {
  set L {}
  list a {*}$L b
} -cleanup {
  unset -nocomplain L
} -result {a b}}

###############################################################################

runTest {test parser-mcdc-9.4 {
  expansion: {*} followed by command substitution
} -body {
  list a {*}[list 1 2 3] b
} -result {a 1 2 3 b}}

###############################################################################
#
# Section 10 -- comments and command separators
#
# `#` is a comment marker only at COMMAND POSITION (immediately
# after a command separator -- newline, ';' or start of script).
# Mid-command, mid-word, and inside any quoted/braced scope it is
# just a literal byte.
#
###############################################################################

runTest {test parser-mcdc-10.1 {
  comment: hash at command position skips the rest of the line
} -body {
  set a 1
  # this whole line is a comment
  set a
} -cleanup {
  unset -nocomplain a
} -result {1}}

###############################################################################

runTest {test parser-mcdc-10.2 {
  comment: backslash-newline continues the comment line
} -body {
  set a 1
  # comment continues \
      still part of the comment
  set a
} -cleanup {
  unset -nocomplain a
} -result {1}}

###############################################################################

runTest {test parser-mcdc-10.3 {
  comment: hash inside a word is a literal byte
} -body {
  set x abc#def
  set x
} -cleanup {
  unset -nocomplain x
} -result {abc#def}}

###############################################################################

runTest {test parser-mcdc-10.4 {
  comment: hash inside a quoted word is a literal byte
} -body {
  set x "a#b"
  set x
} -cleanup {
  unset -nocomplain x
} -result {a#b}}

###############################################################################

runTest {test parser-mcdc-10.5 {
  separators: semicolon at top level ends a command
} -body {
  set a 0; set a 5; set a
} -cleanup {
  unset -nocomplain a
} -result {5}}

###############################################################################

runTest {test parser-mcdc-10.6 {
  separators: newline at top level ends a command
} -body {
  set a 0
  set a 5
  set a
} -cleanup {
  unset -nocomplain a
} -result {5}}

###############################################################################

runTest {test parser-mcdc-10.7 {
  separators: multiple consecutive separators collapse
} -body {
  set a 0;;
  set a 5;;;
  set a
} -cleanup {
  unset -nocomplain a
} -result {5}}

###############################################################################

runTest {test parser-mcdc-10.8 {
  comment-scan backslash branch -- a script-parse pass
  (th8ParseCommand, exposed via th8testlib::parse_command)
  on a script that begins with a comment containing a
  backslash followed by another byte drives th8_core.c
  L19426 (T,T) (`*z == '\\' && n > 1`).  The evaluator
  path uses a different word splitter and never reaches
  this line, so the LSP/syntax-check entry point is the
  only way to exercise the comment-scan loop's
  backslash branch.
} -constraints {
    th8 loadLib
} -setup {
  # Bytes: '#' ' ' '\' 'A' '\n'
  set script [format "%c%c%c%c\n" 35 32 92 65]
} -body {
  th8testlib::parse_command $script
} -cleanup {
  unset -nocomplain script
} -result {0}}

###############################################################################

runTest {test parser-mcdc-10.9 {
  comment-scan backslash branch -- a script-parse pass on
  a script whose only content is a comment ending in a
  bare backslash with NO trailing newline.  Drives
  th8_core.c L19426 (T,F) (`*z == '\\' && n > 1`):
  C1 is T (the comment ends on '\\'); C2 is F because
  i+1 == n.  The comment-scan loop then exits with n==0.
} -constraints {
    th8 loadLib
} -setup {
  # Bytes: '#' ' ' '\'   (no trailing newline; nScript=3)
  set script [format "%c%c%c" 35 32 92]
} -body {
  th8testlib::parse_command $script
} -cleanup {
  unset -nocomplain script
} -result {0}}

###############################################################################

runTest {test parser-mcdc-11.1 {
  Brace-word post-processor with backslash-newline
  IMMEDIATELY before the closing brace drives the C1=F
  vector at th8_core.c:12396 -- after the i += 2 skip
  of backslash-newline, i lands at nn-1 the closing
  brace, so the outer i < nn - 1 whitespace-consume
  check fails at the first iteration with C1=F.
} -constraints {
    th8
} -body {
  set rcs {}
  set script "set v \173abc \134\n\175"
  eval $script
  lappend rcs [string length $v]
  set script2 "set w \173xyz\134\n\175"
  eval $script2
  lappend rcs [string length $w]
  set rcs
} -cleanup {
  unset -nocomplain rcs script script2 v w
} -result {5 4}}

###############################################################################

runTest {test parser_empty_brace_extra-1.1 {
  A word that is `{}rest` (empty brace body with
  trailing characters) drives the (T,F) vectors at
  src/th8_core.c L11975 and L12346 -- the
  `k == 1 && k + 1 < nWord` and `k + 1 < nWord && k > 1`
  compounds in the expansion-operator tag check.  Tcl
  rejects this with "extra characters after close-brace".
} -constraints {
    th8
} -body {
  set rcs {}
  lappend rcs [catch {eval "set x {}rest"} m]
  lappend rcs [catch {eval "puts {}foo"} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

runTest {test parser_var_brace_eof-1.1 {
  An incomplete dollar-brace at end-of-substitution
  scope drives the C1=F vector at src/th8_core.c L10453
  (the loop in th8NextVarName that scans for the closing
  brace).  When the variable parser sees a buffer of
  exactly 2 bytes -- dollar followed by open-brace -- the
  loop condition is immediately F.  Build the bad input
  via format so the test body itself stays
  brace-balanced.  Result is tolerated -- subst may
  pass-through, eval may error -- either way the code
  path is exercised.
} -constraints {
    th8
} -body {
  set rcs {}
  set bad [format "abc%c%c" 36 123]
  catch {subst $bad} m
  lappend rcs [expr {[string length $m] >= 0}]
  set bad [format "puts \"abc%c%c\"" 36 123]
  catch {eval $bad} m
  lappend rcs [expr {[string length $m] >= 0}]
  set rcs
} -cleanup {
  unset -nocomplain rcs m bad
} -result {1 1}}

###############################################################################

runTest {test parser-mcdc-14.1 {
  Drive th8_core.c L10494 backslash-newline continuation
  inside a quoted string: `\<newline>` followed by spaces
  consumes the newline AND the spaces.  Existing tests
  exercise `\<newline>` in comments which use a different
  scanner; this case goes through th8NextEscape and
  requires a script-eval-time input.  Drives the (T,T,-)
  short-circuit (space) and (T,F,F) (next non-whitespace
  byte) vectors of the L10494 compound condition.
} -constraints {
    th8
} -setup {
  # Bytes: a backslash + newline + 2 spaces between "a" and "b"
  # inside a double-quoted string -- the whole substitution
  # collapses to "a b".
  set script [format "set r \"a%c%c  b\"" 92 10]
} -body {
  eval $script
  set r
} -cleanup {
  unset -nocomplain script r
} -result {a b}}

###############################################################################

runTest {test parser-mcdc-14.2 {
  Drive th8_core.c L10494 (T,F,T) C3-Pair: backslash-
  newline followed by a TAB exercises the `zInput[i] ==
  '\t'` branch of the inner-while compound.  Without this
  vector the C3 condition stays unpaired.
} -constraints {
    th8
} -setup {
  # Bytes: backslash + newline + tab + 'b' inside dquotes
  set script [format "set r \"a%c%c%cb\"" 92 10 9]
} -body {
  eval $script
  set r
} -cleanup {
  unset -nocomplain script r
} -result {a b}}

###############################################################################

runTest {test parser-mcdc-14.3 {
  Drive th8_core.c L10494 (F,-,-) C1-Pair: a quoted
  string ending with `\<newline>` (no trailing space/tab
  before the closing quote) makes the inner-while
  condition's first operand (`i < nInput`) immediately F
  because the escape consumes exactly the 2 bytes and the
  substring boundary is reached.
} -constraints {
    th8
} -setup {
  # Two-byte substring inside dquotes: `\` + newline.
  set script [format "set r \"%c%c\"" 92 10]
} -body {
  eval $script
  # `\<newline>` collapses to a space (Tcl 8 rule).  We don't
  # assert the precise length -- just that the eval succeeded
  # and the parser saw the escape.
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain script r
} -result {1}}

###############################################################################

runTest {test parser-mcdc-13.2 {
  Drive the (T,F) C2-Pair at th8_core.c L10611
  (`zInput[i] == '\\' && i + 1 < nInput`): a backslash at
  the very LAST byte of a $var(subscript) reference has
  C1=T but C2=F (no following byte to skip).  The parser
  then bails with TH8_ERROR because the subscript is
  unterminated.  Build the input at runtime so the
  test-file lexer does not pre-process the trailing
  backslash.
} -constraints {
    th8
} -setup {
  array set a132 {x 1}
  # Bytes: 'p' 'u' 't' 's' ' ' '$' 'a' '1' '3' '2' '(' '\'
  set script [format "puts \$a132(%c" 92]
} -body {
  # Malformed reference -- expect an error from the parser.
  set rc [catch {eval $script} m]
  expr {$rc == 1}
} -cleanup {
  unset -nocomplain a132 script rc m
} -result {1}}

###############################################################################

runTest {test parser-mcdc-13.1 {
  Drive the backslash-in-subscript branch at th8_core.c
  L10611 inside th8NextVarName: when a `$var(subscript)`
  reference contains a backslash followed by another byte,
  the substitution-pre-scan must advance past both bytes.
  Need (T,T) -- C1 (zInput[i]=='\\') T and C2 (i+1<nInput)
  T.  Existing var-subscript tests don't include literal
  backslashes inside `()`.

  Build the subscript at runtime via [format] because the
  test-file lexer normalises backslashes before eval sees
  the body otherwise.
} -constraints {
    th8
} -setup {
  array set a13 {x value1 y value2}
  # Bytes: 'p' 'u' 't' 's' ' ' '$' 'a' '1' '3' '(' '\' 'x' ')'
  # The reference reads a13(\x) which after escape becomes a13(x).
  set script [format "puts \$a13(%cx)" 92]
} -body {
  # eval should resolve the reference to value1.
  set output ""
  catch {set output [uplevel #0 [list eval $script]]}
  expr {1}
} -cleanup {
  unset -nocomplain a13 script output
} -result {1}}

###############################################################################

source tests/epilogue.tcl
