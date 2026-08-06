###############################################################################
#
# infodefault.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test infodefault-1.1 {
  R-23634-54601: info default returns 1 when parameter has a default
} -body {
  proc myproc {{x hello}} {}
  info default myproc x result
} -cleanup {
  catch {rename myproc ""}
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test infodefault-1.2 {
  R-23634-54601: info default stores default value in variable
} -body {
  proc myproc {{x hello}} {}
  info default myproc x result
  set result
} -cleanup {
  catch {rename myproc ""}
  unset -nocomplain result
} -result {hello}}

###############################################################################

runTest {test infodefault-1.3 {
  R-23634-54601: info default returns 0 when parameter has no default
} -body {
  proc myproc {x} {}
  info default myproc x result
} -cleanup {
  catch {rename myproc ""}
  unset -nocomplain result
} -result {0}}

###############################################################################

runTest {test infodefault-1.4 {
  R-46238-50851: info default sets variable to empty when no default
} -body {
  proc myproc {x} {}
  set result "old"
  info default myproc x result
  set result
} -cleanup {
  catch {rename myproc ""}
  unset -nocomplain result
} -result {}}

###############################################################################

runTest {test infodefault-1.5 {
  R-23634-54601: info default with multiple parameters
} -body {
  proc myproc {a {b 42} {c end}} {}
  set r1 [info default myproc a va]
  set r2 [info default myproc b vb]
  set r3 [info default myproc c vc]
  list $r1 $va $r2 $vb $r3 $vc
} -cleanup {
  catch {rename myproc ""}
  unset -nocomplain r1 r2 r3 va vb vc
} -result {0 {} 1 42 1 end}}

###############################################################################

runTest {test infodefault-2.1 {
  R-26222-29181: info default with non-existent procedure
} -body {
  catch {info default nosuchproc x result} msg
  set msg
} -cleanup {
  unset -nocomplain msg result
} -match regexp -result {(not a procedure|isn't a procedure|invalid command name)}}

###############################################################################

runTest {test infodefault-2.2 {
  R-59596-03970: info default with non-existent argument
} -body {
  proc myproc {x} {}
  catch {info default myproc nosucharg result} msg
  set msg
} -cleanup {
  catch {rename myproc ""}
  unset -nocomplain msg result
} -match regexp -result {.*doesn't have an argument.*}}

###############################################################################

runTest {test infodefault-2.3 {
  R-23634-54601: info default wrong number of arguments
} -body {
  catch {info default myproc} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -match regexp -result {wrong # args}}

###############################################################################

runTest {test infodefault-3.1 {
  R-46238-50851: info default with empty string default
} -body {
  proc myproc {{x {}}} {}
  list [info default myproc x result] $result
} -cleanup {
  catch {rename myproc ""}
  unset -nocomplain result
} -result {1 {}}}

###############################################################################

runTest {test infodefault-3.2 {
  R-23634-54601: info default with nproc
} -constraints {
    nproc not_eagle
} -body {
  nproc ::ns::myproc {{x hello}} {}
  info default ::ns::myproc x result
} -cleanup {
  catch {namespace delete ::ns}
  unset -nocomplain result
} -result {1}}

###############################################################################

source tests/epilogue.tcl
