###############################################################################
#
# coverage_variable_single_colon.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/plugins/th8_variables.c variable_command
# L927-928 C4-Pair:
#
#   if (!zQual && nName > 2
#           && zName[0] == ':' && zName[1] == ':')
#
# Existing coverage drives vectors (F,-,-,-), (T,F,-,-),
# (T,T,F,-) and (T,T,T,T).  The C4-Pair (T,T,T,F) -- inside a
# proc, no qualifier built, name longer than 2, starts with
# single ':' but second char is NOT ':' -- was unreached
# because no existing test calls [variable] with a single-
# colon-prefixed name inside a proc.
#
# Driver: a proc defined in the global namespace calls
# [variable :foo ...].  Inside the proc, current namespace is
# "::" (global), so zNs[2]==0 -> the zQual-building branch at
# L878-881 short-circuits and zQual stays NULL.  inProc=T
# because frame level > 0.  Then L927 evaluates !zQual=T,
# nName>2=T (":foo" has length 4), zName[0]==':'=T,
# zName[1]==':'=F -> overall F, skip the qualified-name tail
# extraction.  L945 runs Th8_LinkVar(":foo", 4, 0, ":foo", 4)
# which creates a local link from the single-colon name.
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

runTest {test varsinglecolon-1.1 {
  [variable :foo value] inside a proc in the global namespace
  drives th8_variables.c L927 C4-Pair (T,T,T,F) -- single
  colon prefix where the second byte is NOT a colon.  The
  variable is created in the global namespace and the proc
  has a local link to it.
} -constraints {
    th8
} -setup {
  catch {unset -nocomplain ::test_var_sc_1}
} -body {
  proc ::test_proc_sc_1 {} {
    variable :foo bar
    return [info exists :foo]
  }
  set rc [::test_proc_sc_1]
  rename ::test_proc_sc_1 {}
  set rc
} -cleanup {
  catch {rename ::test_proc_sc_1 {}}
  catch {unset -nocomplain :foo}
  unset -nocomplain rc
} -result {1}}

###############################################################################

runTest {test varsinglecolon-1.2 {
  Variant of varsinglecolon-1.1 using a longer single-colon
  name (":mylongname") and without an initial value.  Same
  C4-Pair vector; confirms the path doesn't depend on the
  presence of an assignment.
} -constraints {
    th8
} -body {
  proc ::test_proc_sc_2 {} {
    variable :mylongname
    return [info exists :mylongname]
  }
  set rc [::test_proc_sc_2]
  rename ::test_proc_sc_2 {}
  set rc
} -cleanup {
  catch {rename ::test_proc_sc_2 {}}
  catch {unset -nocomplain :mylongname}
  unset -nocomplain rc
} -result {0}}

###############################################################################

source tests/epilogue.tcl
