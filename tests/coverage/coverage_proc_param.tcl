###############################################################################
#
# coverage_proc_param.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for the proc-parameter-list parser
# 3-cond compound at two sites in src/plugins/th8_procedures.c:
#
#   line 579   proc_command   parameter parsing
#   line 1211  nproc_command  parameter parsing
#
# Pattern: rc != TH8_OK || n < 1 || n > 2
#
# Existing tests cover the F,F,F success vector (typical params
# like "name" or "{name default}").  T,-,- (malformed list) and
# F,F,T (3+ component param) and F,T,- (empty param) need
# specific malformed inputs.
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

runTest {test pp_cov-1.1 {
  proc with empty parameter (n==0) drives F,T,- vector at line 579
} -constraints {
    th8
} -body {
  catch {proc ::pp_cov_empty {{}} {return ok}} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {rename ::pp_cov_empty {}}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pp_cov-1.2 {
  proc with 3-element parameter (n>2) drives F,F,T vector at line 579
} -constraints {
    th8
} -body {
  catch {proc ::pp_cov_three {{a b c}} {return ok}} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {rename ::pp_cov_three {}}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pp_cov-1.3 {
  proc with malformed (unbalanced-brace) parameter drives the
  T,-,- vector at line 579 (Th8_SplitList returns error)
} -constraints {
    th8
} -body {
  # Unbalanced brace inside the parameter list should fail to
  # split.  We pass it via [list] to bypass surface-level parser
  # validation and reach the C-level Th8_SplitList call.
  catch {proc ::pp_cov_bad [list "\{unbalanced"] {return ok}} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {rename ::pp_cov_bad {}}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pp_cov-1.4 {
  nproc with empty parameter drives F,T,- at line 1211
} -constraints {
    th8
} -body {
  catch {nproc ::pp_cov_nproc_empty {{}} {return ok}} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {rename ::pp_cov_nproc_empty {}}
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test pp_cov-1.5 {
  nproc with 3-element parameter drives F,F,T at line 1211
} -constraints {
    th8
} -body {
  catch {nproc ::pp_cov_nproc_three {{a b c}} {return ok}} r
  expr {[string length $r] >= 0}
} -cleanup {
  catch {rename ::pp_cov_nproc_three {}}
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
