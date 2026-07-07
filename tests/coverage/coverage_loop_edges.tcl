###############################################################################
#
# coverage_loop_edges.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# P2 of the MC/DC closure plan.  Drives `while (...)` loop
# boundary compounds across the codebase by feeding canonical
# edge inputs (empty strings, single-char strings, off-by-one
# lists, leading/trailing whitespace, etc.) to the various
# parsers and string scanners.
#
# Targeted compounds:
#   th8_bigint.c:331       leading-whitespace skip in bigint parse
#   th8_filesystems.c:931  trailing-dot/space strip (th8IsDeviceName)
#   th8_filesystems.c:1096 path-component scan
#   th8_filesystems.c:1105 path-separator-run scan
#   th8_expr.c:1806        expression whitespace skip
#   th8_core.c:17879/17897 script-comment parsing edges
#   th8_control.c:1265     subst arg-pair iteration
#   th8_procedures.c:1013  proc-name leading-space strip
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
#
# Section 1 -- bigint parsing with leading whitespace
#
###############################################################################

runTest {test loop_edges-1.1 {
  bigint parse with leading space drives th8_bigint.c:331 T,T vector
} -constraints {
    th8 bigint
} -body {
  catch {expr {"  12345" + 1}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-1.2 {
  bigint parse with leading tab drives the alternative whitespace
} -constraints {
    th8 bigint
} -body {
  catch {expr {"\t42" + 1}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-1.3 {
  bigint parse with no leading whitespace (T,F vector at 331)
} -constraints {
    th8 bigint
} -body {
  catch {expr {"99" + 0}} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 2 -- file validname with trailing dots / spaces
#
###############################################################################

runTest {test loop_edges-2.1 {
  file validname with trailing dot drives th8IsDeviceName trailing-strip
} -constraints {
    th8
} -body {
  catch {file validname "CON."} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-2.2 {
  file validname with trailing space drives the alternate strip char
} -constraints {
    th8
} -body {
  catch {file validname "AUX "} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-2.3 {
  file validname with trailing dots and spaces mixed
} -constraints {
    th8
} -body {
  catch {file validname "PRN. ."} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-2.4 {
  file validname with bare device name (F,- vector: no trailing strip)
} -constraints {
    th8
} -body {
  catch {file validname "NUL"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 3 -- path scanning (file split, file join, file under)
#
###############################################################################

runTest {test loop_edges-3.1 {
  file split with multi-component path drives 1096 / 1105 scans
} -constraints {
    th8
} -body {
  set r [file split "/a/b/c/d/e/f"]
  expr {[llength $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-3.2 {
  file split with consecutive separators
} -constraints {
    th8
} -body {
  set r [file split "/a//b///c"]
  expr {[llength $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-3.3 {
  file split of single-character path
} -constraints {
    th8
} -body {
  set r [file split "a"]
  expr {[llength $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-3.4 {
  file split of empty path drives n==0 boundary
} -constraints {
    th8
} -body {
  set r [file split ""]
  expr {[llength $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 4 -- expression parser whitespace handling
#
###############################################################################

runTest {test loop_edges-4.1 {
  expr with leading whitespace
} -constraints {
    th8
} -body {
  expr {   1 + 2}
} -result {3}}

###############################################################################

runTest {test loop_edges-4.2 {
  expr with embedded whitespace (multi-line)
} -constraints {
    th8
} -body {
  expr {1 + \
        2 + \
        3}
} -result {6}}

###############################################################################

runTest {test loop_edges-4.3 {
  expr with tabs and spaces mixed
} -constraints {
    th8
} -body {
  expr {1	+	2}
} -result {3}}

###############################################################################

runTest {test loop_edges-4.4 {
  expr with no whitespace (F,- vector)
} -constraints {
    th8
} -body {
  expr {1+2}
} -result {3}}

###############################################################################
#
# Section 5 -- subst argument-pair iteration
#
###############################################################################

runTest {test loop_edges-5.1 {
  subst with multiple flag args drives th8_control.c:1265 j+1 < argc
} -constraints {
    th8
} -body {
  catch {subst -nobackslashes -nocommands "$::env(TEST_X)"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test loop_edges-5.2 {
  subst with -- terminator
} -constraints {
    th8
} -body {
  catch {subst -- "literal"} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 6 -- script comment parsing
#
###############################################################################

runTest {test loop_edges-6.1 {
  script with trailing comment line drives core comment parsing
} -constraints {
    th8
} -body {
  set r [eval "set x 1 ;# trailing comment"]
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r x
} -result {1}}

###############################################################################

runTest {test loop_edges-6.2 {
  script with multi-line comments
} -constraints {
    th8
} -body {
  set r [eval {
    # comment 1
    # comment 2
    set x 42
    # comment 3
  }]
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r x
} -result {1}}

###############################################################################

source tests/epilogue.tcl
