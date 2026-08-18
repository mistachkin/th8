###############################################################################
#
# subcommand_registration.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for dynamic sub-command registration (TH8K-025).  Ensemble sub-commands
# are no longer a static per-command table: each command owns a per-interpreter
# sub-command hash, and the public Th8_CreateSubCommand adds or replaces
# sub-commands on any command (built-in or embedder-created).  The core
# dispatches "cmd sub ..." from that hash (th8InvokeCommand -> th8DispatchEnsemble),
# so an omitted sub-command is simply absent -- the mechanism the named-command-
# subset feature builds on.  See docs/internal/design_notes_command_subsets.md.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test subcommand-registration-1.1 {
  TH8K-025: the public Th8_CreateSubCommand dynamically registers a NEW
  sub-command onto the built-in `string` ensemble, an existing built-in
  sub-command is unaffected, and re-registering REPLACES the handler.
  ::th8testlib::sub_register_probe adds `string twice` and checks it runs
  ("TWICE:hi"), checks `string toupper hi` still yields "HI", replaces
  `string twice` with a different handler and checks the new one wins
  ("REPL:hi"); deleting the child interpreter exercises the sub-command free
  path (TH8_HEAP_CHECKS catches a leak or double-free).  Returns "ok" or a
  skip token.
} -constraints {
    th8
} -body {
  ::th8testlib::sub_register_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

runTest {test subcommand-registration-1.2 {
  The converted `string` ensemble dispatches its built-in sub-commands from the
  per-interpreter hash and reports the standard errors: a bare `string` is a
  wrong-# args error, and an unknown sub-command lists the valid ones in
  registration order.
} -constraints {
    th8
} -setup {
  unset -nocomplain r e1 e2
} -body {
  set r [list [string toupper abc] [string length hello]]
  lappend r [catch {string} e1] [catch {string nope x} e2]
  lappend r [string match "unknown or ambiguous subcommand*must be compare*" $e2]
  set r
} -cleanup {
  unset -nocomplain r e1 e2
} -result {ABC 5 1 1 1}}

###############################################################################

runTest {test subcommand-registration-2.1 {
  TH8K-025: Th8_CreateSubCommand registers sub-commands on a BRAND-NEW command
  (a NULL-handler ensemble shell), the core dispatches them and [info
  subcommands] lists them in registration order, and Th8_DeleteSubCommand
  removes one by token; deleting the last sub-command reverts the command to a
  non-ensemble.  ::th8testlib::sub_newcmd_probe drives the whole lifecycle in a
  child interpreter (TH8_HEAP_CHECKS catches any leak/double-free).
} -constraints {
    th8
} -body {
  ::th8testlib::sub_newcmd_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

runTest {test subcommand-registration-3.1 {
  TH8K-025: a sub-command overlay on a command that has a real handler falls
  back to that handler for anything it does not match, and deleting the
  sub-command restores the pristine command.  ::th8testlib::sub_fallback_probe
  creates `tmpbase` (handler -> "BASE"), adds `special` (-> "SUBA"), and checks
  that `tmpbase special` runs the sub while `tmpbase other`/bare `tmpbase` fall
  back to "BASE", then that deleting `special` makes `tmpbase special` fall
  back too.
} -constraints {
    th8
} -body {
  ::th8testlib::sub_fallback_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

runTest {test subcommand-registration-4.1 {
  TH8K-025: save/restore of a built-in sub-command via Th8_GetSubCommandInfo.
  ::th8testlib::sub_saverestore_probe captures `string toupper`'s binding,
  replaces it with a handler returning "SUBA", confirms the replacement wins,
  restores the captured binding, and confirms the original behavior ("HI") is
  back.
} -constraints {
    th8
} -body {
  ::th8testlib::sub_saverestore_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

runTest {test subcommand-registration-4.2 {
  TH8K-025: "sub-classing" a built-in sub-command -- a custom handler captures
  the built-in's binding (Th8_GetSubCommandInfo), replaces it with a wrapper
  that CALLS BACK into the captured implementation and post-processes the
  result, then restores the built-in.  ::th8testlib::sub_wrap_probe wraps
  `string toupper` so `string toupper hi` yields "W:HI" (the built-in produced
  "HI", the wrapper prefixed "W:"), then restores it to plain "HI".  This proves
  the queried pContext round-trips and the captured handler is directly callable.
} -constraints {
    th8
} -body {
  ::th8testlib::sub_wrap_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

runTest {test subcommand-registration-5.1 {
  TH8K-025: [info subcommands] reads the per-interpreter sub-command hash the
  evaluator dispatches from, so it covers EVERY ensemble -- including the four
  whose sub-command tables were previously function-local (dict, clock, binary,
  interp) -- and honors a glob pattern.
} -constraints {
    th8
} -setup {
  unset -nocomplain r
} -body {
  set r [list]
  lappend r [expr {"format" in [info subcommands binary]}]
  lappend r [expr {"seconds" in [info subcommands clock]}]
  lappend r [expr {"cancel" in [info subcommands interp]}]
  lappend r [expr {"keys" in [info subcommands dict]}]
  lappend r [lsort [info subcommands string to*]]
  set r
} -cleanup {
  unset -nocomplain r
} -result {1 1 1 1 {tolower totitle toupper}}}

###############################################################################

runTest {test subcommand-registration-5.2 {
  TH8K-025: the "unknown subcommand" error uses Tcl's two-item form "a or b"
  (no Oxford comma) when an ensemble has exactly two sub-commands.  `binary`
  has exactly `format` and `scan`.
} -constraints {
    th8
} -setup {
  unset -nocomplain e
} -body {
  catch {binary bogus} e
  list [string match "*must be format or scan*" $e] \
      [string match "*format, or scan*" $e]
} -cleanup {
  unset -nocomplain e
} -result {1 0}}

###############################################################################

runTest {test subcommand-registration-6.1 {
  TH8K-025: Th8_RegisterSubsets installs only the chosen subsets (an allowlist).
  ::th8testlib::subset_probe registers {strings lists} into a bare child and
  checks `string` and `lappend` work while `for` (the withheld "looping" subset)
  is absent -- withholding Turing-completeness -- then checks that an unknown
  subset name is transactional (fails and registers nothing).
} -constraints {
    th8
} -body {
  ::th8testlib::subset_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

runTest {test subcommand-registration-6.2 {
  TH8K-025: the subset introspection APIs and the hybrid model's fine-grained
  curated subset.  ::th8testlib::subset_curated_probe checks Th8_ListSubsets
  lists plugin + curated names, Th8_GetSubsetMembers audits a subset's members
  (and errors on an unknown name), then registers the curated "safe-file" subset
  and confirms the FILTERED [file] ensemble: `file dirname` works but
  `file delete` is absent.
} -constraints {
    th8
} -body {
  ::th8testlib::subset_curated_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

runTest {test subcommand-registration-7.1 {
  TH8K-025 / TH8K-030: the ERROR, edge, and OOM arms of the sub-command and
  named-subset APIs.  ::th8testlib::subset_mcdc_probe drives every NULL-argument
  guard (Th8_GetSubsetMembers / Th8_RegisterSubsets / Th8_CreateSubCommand /
  Th8_GetSubCommandInfo), argument errors (nNames<0, NULL azNames), a not-found
  sub-command query and a bogus-token delete, a curated "safe-file" audit (the
  COMMAND + SUBCOMMAND member-resolution arms), a create/replace/delete-by-token
  cycle, and a one-shot allocation-fault sweep over Th8_GetSubsetMembers (the
  th8SubsetAppendMember OOM arms) -- all in throwaway child interpreters.
} -constraints {
    th8
} -body {
  ::th8testlib::subset_mcdc_probe
} -match regexp -result {^(ok|skip:.*)$}}

###############################################################################

source tests/epilogue.tcl
