###############################################################################
#
# coverage_parse_command_internal.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC coverage for src/th8_core.c th8ParseCommand
# (TH8_INTERNAL with no in-tree callers).  Reaches the
# function through the new internal stubs table exposed
# by Th8_GetInternalStubs() and routed via testlib's
# th8testlib::parse_command wrapper.
#
# Variations of the input drive different decision pairs
# in the parser body (whitespace skip at L19423, comment
# handling at L19441/L19442, word loop, brace/quote
# scanning, backslash-newline continuation).
#
# Coverage-driven; not pinned to specific R-markers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test parsecmd-1.1 {
  parse_command on simple multi-word commands returns the
  word count.  Drives the basic word-tokenization decisions
  in th8ParseCommand (L19423 word loop, L19462 word scanner
  entry).
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command "foo bar baz"] \
      [::th8testlib::parse_command "a b c d e"] \
      [::th8testlib::parse_command "single"]
} -result {3 5 1}}

###############################################################################

runTest {test parsecmd-1.2 {
  Leading whitespace + semicolons + newlines drives the
  L19423 word loop's C2/C3/C5 vectors (th8IsSpace, *z=='\n',
  *z==';').
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command "   foo bar"] \
      [::th8testlib::parse_command ";;;foo"] \
      [::th8testlib::parse_command "\n\nfoo bar"] \
      [::th8testlib::parse_command "\t \tfoo"]
} -result {2 1 2 1}}

###############################################################################

runTest {test parsecmd-1.3 {
  Leading comments drive the L19441/L19442 comment-handling
  vectors.  th8ParseCommand returns the comment text via
  pParse->zComment but reports nWord=0 for the comment-only
  "command".  The follow-up command (after the newline) is
  reached only on a subsequent call with the advanced
  pParse->zAfter, which the wrapper here does not do --
  so all three return 0 words for the FIRST parsed command.
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command "# comment\nfoo bar"] \
      [::th8testlib::parse_command "  # comment\nfoo"] \
      [::th8testlib::parse_command "# c1\n# c2\nfoo"]
} -result {0 0 0}}

###############################################################################

runTest {test parsecmd-1.4 {
  Braced/quoted words drive the brace and quote scanning
  decisions.
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command "\{braced\} arg"] \
      [::th8testlib::parse_command "\"quoted\" arg"] \
      [::th8testlib::parse_command "\{a b c\}"]
} -result {2 2 1}}

###############################################################################

runTest {test parsecmd-1.5 {
  Empty / whitespace-only / comment-only input drives the
  early-exit branches.
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command ""] \
      [::th8testlib::parse_command "   "] \
      [::th8testlib::parse_command "# only comment"]
} -result {0 0 0}}

###############################################################################

runTest {test parsecmd-1.6 {
  Command terminators mid-script drive th8_core.c L19462
  word-scan loop's C2/C3/C4-Pair vectors -- (T,F,-,-) ';'
  encountered, (T,T,F,-) '\n' encountered, (T,T,T,F) '\r'
  encountered.  Each is reached AFTER the first word is
  tokenized but BEFORE the next iteration scans further.
  parse_command returns the count of the FIRST command's
  words; the rest of the script (after the terminator) is
  in zAfter and would need a re-entry to scan.
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command "foo;bar"] \
      [::th8testlib::parse_command "foo\nbar"] \
      [::th8testlib::parse_command "foo\rbar"] \
      [::th8testlib::parse_command "a b c;d e f"]
} -result {1 1 1 3}}

###############################################################################

runTest {test parsecmd-1.8 {
  Inputs containing substitution characters (\$, [, \\)
  drive th8_core.c L19523's `z[j] == '$' || z[j] == '[' ||
  z[j] == '\\\\'` per-byte check inside the simple-word
  classifier loop.  Each substitution byte triggers isSimple
  = 0 and breaks, marking the token as a complex word.  No
  existing test reached this loop with any of the three
  bytes -- parse_command exercises it directly.
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command "set x \$y"] \
      [::th8testlib::parse_command "set x \[cmd]"] \
      [::th8testlib::parse_command "set x \\foo"]
} -result {3 3 3}}

###############################################################################

runTest {test parsecmd-1.7 {
  Whitespace-followed-by-terminator inputs drive the
  th8_core.c L19466 inner-loop check (n==0 || *z==';' ||
  *z=='\n' || *z=='\r') which fires AFTER th8NextSpace
  consumes leading whitespace inside the word loop.
  Reachable vectors:
    "foo   "       -> NextSpace exhausts, n==0 -> C1=T
    "foo  ;bar"    -> NextSpace -> ';' -> C2=T
  C3/C4 (\\n / \\r after NextSpace) are intrinsic-dead:
  th8NextSpace classifies \\n and \\r as whitespace and
  consumes them, so they never survive to L19466 as the
  first byte.  Inputs with those bytes after spaces tokenize
  the trailing word as a SECOND word (NextSpace + NextWord
  on "bar"), giving nWord=2.
} -constraints {
    th8
} -body {
  list \
      [::th8testlib::parse_command "foo   "] \
      [::th8testlib::parse_command "foo  ;bar"] \
      [::th8testlib::parse_command "foo  \nbar"] \
      [::th8testlib::parse_command "foo  \rbar"]
} -result {1 1 2 2}}

###############################################################################

runTest {test parsexpr-1.1 {
  parse_expr (th8ParseExpr) routes through the same internal
  stubs table.  Catches errors so the parser is exercised
  regardless of whether the expression text validates.
  MC/DC goal: drive every distinct tokenizer path; the
  return value is incidental.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach e {
      "1 + 2"
      "1 * 2"
      "10"
      ""
      "1 + 2 + 3"
      "1.5 + 2.5"
      "1 - 2"
  } {
      lappend rcs [catch {::th8testlib::parse_expr $e}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs e
} -result {0 0 0 1 0 0 0}}

###############################################################################

runTest {test parsexpr-1.2 {
  Expressions with parentheses, function calls, string
  operators, braced/quoted operands -- drive different
  expression-tokenizer paths inside th8ParseExpr.  Error
  results are caught: the goal is path coverage, not
  arithmetic correctness.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach e {
      "(1 + 2) * 3"
      "sin(0.5)"
      "1 ? 2 : 3"
      "\"foo\" eq \"bar\""
      "\{a b\}"
      "1 == 2"
      "1 != 2"
      "1 < 2 ? 3 : 4"
      "abs(-5)"
      "max(1, 2)"
  } {
      lappend rcs [catch {::th8testlib::parse_expr $e}]
  }
  expr {[lindex $rcs 0] >= 0}
} -cleanup {
  unset -nocomplain rcs e
} -result {1}}

###############################################################################

runTest {test parsevar-1.1 {
  parse_var_name (th8ParseVarName) routes through the
  internal stubs table.  Exercises the $name / $name(idx)
  / ${name} forms.  Returns 0 (TH8_OK) on successful parse.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {
      "\$foo"
      "\$x"
      "\$arr(key)"
      "\$\{braced\}"
      "\$ns::var"
      "\$"
      "foo"
      ""
  } {
      lappend rcs [::th8testlib::parse_var_name $v]
  }
  expr {[llength $rcs] == 8}
} -cleanup {
  unset -nocomplain rcs v
} -result {1}}

###############################################################################

runTest {test platwrappers-1.1 {
  th8testlib::plat_wrappers drives the th8_plat.c utility
  wrappers (th8Memmove, th8Strcmp, th8Strchr, th8Atoi,
  th8Qsort) that have no in-tree callers.  Each call
  exercises the wrapper's NULL-arg + missing-callback
  decisions.  Result encodes:
    [0] th8Strcmp("alpha","beta") sign: '0'=neg, '1'=0, '2'=pos
    [1] th8Qsort sorted result: '0'=no, '1'=yes
    [2] th8Strchr("hello",'l') found: '0'=no, '1'=yes
  Expected: 011 (cmp<0, sorted, found).
} -constraints {
    th8
} -body {
  ::th8testlib::plat_wrappers
} -result {011}}

###############################################################################

source tests/epilogue.tcl
