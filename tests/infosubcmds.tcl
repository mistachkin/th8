###############################################################################
#
# infosubcmds.tcl --
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
#
# Section 1 -- info subcommands: full listing
#
###############################################################################

runTest {test infosubcmds-1.1 {
  R-05560-54350: info subcommands file
} -constraints {
    info_subcommands
} -body {
  set subs [info subcommands file]
  expr {[llength $subs] > 0 && [lsearch $subs "tail"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-1.2 {
  R-05560-54350: info subcommands info
} -constraints {
    info_subcommands
} -body {
  set subs [info subcommands info]
  expr {[llength $subs] > 0 && [lsearch $subs "subcommands"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-1.3 {
  R-05560-54350: info subcommands string
} -constraints {
    info_subcommands
} -body {
  set subs [info subcommands string]
  expr {[llength $subs] > 0 && [lsearch $subs "length"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-1.4 {
  R-05560-54350: info subcommands namespace
} -constraints {
    info_subcommands
} -body {
  set subs [info subcommands namespace]
  expr {[llength $subs] > 0 && [lsearch $subs "eval"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-1.5 {
  R-05560-54350: info subcommands package
} -constraints {
    info_subcommands
} -body {
  set subs [info subcommands package]
  expr {[llength $subs] > 0 && [lsearch $subs "require"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-1.6 {
  R-05560-54350: info subcommands array
} -constraints {
    info_subcommands
} -body {
  set subs [info subcommands array]
  expr {[llength $subs] > 0 && [lsearch $subs "exists"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################
#
# Section 2 -- info subcommands: pattern filtering
#
###############################################################################

runTest {test infosubcmds-2.1 {
  R-11672-38479: info subcommands pattern
} -constraints {
    info_subcommands
} -body {
  set subs [info subcommands file "s*"]
  expr {[lsearch $subs "split"] >= 0 && [lsearch $subs "join"] < 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-2.2 {
  R-11672-38479: info subcommands pattern no match
} -constraints {
    info_subcommands
} -body {
  info subcommands file "zzz*"
} -result {}}

###############################################################################

runTest {test infosubcmds-2.3 {
  R-11672-38479: info subcommands exact pattern
} -constraints {
    info_subcommands
} -body {
  info subcommands info "exists"
} -result {exists}}

###############################################################################
#
# Section 3 -- info subcommands: error cases
#
###############################################################################

runTest {test infosubcmds-3.1 {
  R-05560-54350: info subcommands non-ensemble error
} -constraints {
    info_subcommands th8
} -setup {
} -body {
  list [catch {info subcommands set} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test infosubcmds-3.2 {
  R-05560-54350: info subcommands wrong args
} -constraints {
    info_subcommands
} -setup {
} -body {
  list [catch {info subcommands} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 4 -- info subcommands: ensemble availability at startup
#
# R-43740-46756: info subcommands must work for all ensemble commands
# without requiring them to have been invoked first.
#
###############################################################################

runTest {test infosubcmds-4.1 {
  R-43740-46756: info subcommands namespace works at startup
} -constraints {
    info_subcommands
} -setup {
} -body {
  set subs [info subcommands namespace]
  expr {[llength $subs] > 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-4.2 {
  R-43740-46756: info subcommands array works at startup
} -constraints {
    info_subcommands
} -setup {
} -body {
  set subs [info subcommands array]
  expr {[llength $subs] > 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-4.3 {
  R-43740-46756: info subcommands package works at startup
} -constraints {
    info_subcommands
} -setup {
} -body {
  set subs [info subcommands package]
  expr {[llength $subs] > 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-4.4 {
  R-43740-46756: info subcommands file works at startup
} -constraints {
    info_subcommands
} -setup {
} -body {
  set subs [info subcommands file]
  expr {[llength $subs] > 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-4.5 {
  R-43740-46756: info subcommands string works at startup
} -constraints {
    info_subcommands
} -setup {
} -body {
  set subs [info subcommands string]
  expr {[llength $subs] > 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-4.6 {
  R-43740-46756: info subcommands info works at startup
} -constraints {
    info_subcommands
} -setup {
} -body {
  set subs [info subcommands info]
  expr {[lsearch $subs "subcommands"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################

runTest {test infosubcmds-4.7 {
  R-05560-54350: info subcommands namespace returns expected list
} -constraints {
    info_subcommands th8
} -setup {
} -body {
  lsort [info subcommands namespace]
} -cleanup {
  unset -nocomplain subs
} -result {children code current delete eval exists export import origin parent which}}

###############################################################################

runTest {test infosubcmds-4.8 {
  R-05560-54350: info subcommands array returns expected list
} -constraints {
    info_subcommands th8
} -setup {
} -body {
  lsort [info subcommands array]
} -cleanup {
  unset -nocomplain subs
} -result {anymore donesearch exists get names nextelement set size startsearch statistics unset}}

###############################################################################

runTest {test infosubcmds-4.9 {
  R-05560-54350: info subcommands package includes scan
} -constraints {
    info_subcommands
} -setup {
} -body {
  set subs [lsort [info subcommands package]]
  expr {[lsearch $subs "require"] >= 0 && [lsearch $subs "scan"] >= 0}
} -cleanup {
  unset -nocomplain subs
} -result {1}}

###############################################################################
#
# info expansions
#
###############################################################################

runTest {test infosubcmds-4.10 {
  R-01803-53009: info expansions returns registered operators
} -constraints {info_expansions} -body {
  set result [info expansions]
  expr {[lsearch -exact $result "*"] >= 0}
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test infosubcmds-4.11 {
  R-01803-53009: info expansions with glob pattern
} -constraints {info_expansions} -body {
  info expansions {[*]}
} -result {*}}

###############################################################################

runTest {test infosubcmds-4.12 {
  R-01803-53009: info expansions no-match pattern returns empty
} -constraints {info_expansions} -body {
  info expansions {no_such_*}
} -result {}}

###############################################################################
#
# info breakpoints
#
###############################################################################

runTest {test infosubcmds-5.1 {
  R-16236-39798: info breakpoints with no breakpoints returns empty
} -constraints {info_breakpoints} -body {
  info breakpoints
} -result {}}

###############################################################################

source tests/epilogue.tcl
