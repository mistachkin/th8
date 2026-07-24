###############################################################################
#
# unknown.tcl --
#
# Tcl Language Standard
# Regression Test File
#
# Regression coverage for the `unknown` command handler as invoked from the
# NRE-aware (command-substitution / "bracket") dispatch path in th8_core.c
# (th8NRCmdDispatch).  That path builds an "unknown"-prefixed wrapper argv
# (azNew) whose pointers borrow from the original argv (azElem) and dispatches
# the handler via xProc.  The handler's parameters are bound by a LATER NRE
# callback (proc_call_nr), so azNew must survive until th8EvalPostCmd frees it
# (via pData[3]) -- NOT be freed synchronously after xProc returns.  The async
# path previously freed azNew synchronously, a use-after-free that only
# manifested when the freed block was reused before the deferred parameter
# bind (a layout-sensitive heap-corruption Heisenbug; Bug 62 candidate).  A
# command that BOTH contains '[' (forcing the async path) AND resolves to an
# unknown name handled by a proc exercises the fix.
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
# Section 1 -- unknown handler via the async (command-substitution) path
#
###############################################################################

runTest {test unknown-1.1 {
  Regression (Bug 62 candidate): an unknown command that contains a '['
  command substitution is dispatched through th8NRCmdDispatch, which builds
  the azNew wrapper argv.  The proc-based unknown handler must receive the
  original command name and substituted arguments intact -- proving azNew
  survived until proc_call_nr bound the parameters.
} -setup {
  proc unknown args { return "UNK|[join $args |]" }
} -body {
  # 'nope' is unknown; the '[format ...]' forces the async bracket path.
  nope [format %s%s a b] gamma
} -cleanup {
  rename unknown ""
} -result {UNK|nope|ab|gamma}}

###############################################################################

runTest {test unknown-1.2 {
  Regression stress (Bug 62): repeatedly drive the async unknown path with
  a distinct command name each iteration (defeating any command cache) and
  intervening heap allocation to widen the use-after-free window.  Every
  invocation must observe exactly three arguments (name + two args); a
  freed/ reused azNew would yield a wrong count or crash.
} -setup {
  proc unknown args { return [llength $args] }
} -body {
  set ok 1
  for {set i 0} {$i < 500} {incr i} {
    set junk [string repeat x $i]
    set n [nope$i [format %d $i] tail]
    if {$n != 3} {
      set ok 0
      break
    }
  }
  set ok
} -cleanup {
  rename unknown ""
  unset -nocomplain ok i junk n
} -result {1}}

###############################################################################

runTest {test unknown-1.3 {
  The async unknown path must preserve argument boundaries when a
  substituted argument is empty and when arguments contain spaces, so the
  handler sees the exact word vector the parser produced.
} -setup {
  proc unknown args {
    return "[llength $args]|[lindex $args 0]|[lindex $args 2]"
  }
} -body {
  # Command name 'zzz' unknown; '[set e {}]' both forces the bracket path
  # and yields an empty middle argument.
  zzz [set e {}] {two words} last
} -cleanup {
  rename unknown ""
  unset -nocomplain e
} -result {4|zzz|two words}}

###############################################################################
#
# Section 2 -- unknown handler via the synchronous (no-bracket) path
#
###############################################################################

runTest {test unknown-2.1 {
  Control: the same handler reached through the synchronous dispatch path
  (no '[' in the command) must behave identically, confirming the fix did
  not change observable semantics between the two paths.
} -setup {
  proc unknown args { return "UNK|[join $args |]" }
} -body {
  nope literal gamma
} -cleanup {
  rename unknown ""
} -result {UNK|nope|literal|gamma}}

###############################################################################

source tests/epilogue.tcl
