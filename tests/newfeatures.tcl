###############################################################################
#
# newfeatures.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for recently implemented features.
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
# Section 1 -- string equal
#
###############################################################################

runTest {test newfeatures-1.1 {
  R-46584-30958: string equal returns 1 for equal strings
} -body {
  string equal hello hello
} -result {1}}

###############################################################################

runTest {test newfeatures-1.2 {
  R-46584-30958: string equal returns 0 for unequal strings
} -body {
  string equal hello world
} -result {0}}

###############################################################################

runTest {test newfeatures-1.3 {
  R-25119-35336: string equal -nocase
} -body {
  string equal -nocase Hello HELLO
} -result {1}}

###############################################################################

runTest {test newfeatures-1.4 {
  R-29243-18547: string equal -length match
} -body {
  string equal -length 3 hello help
} -result {1}}

###############################################################################

runTest {test newfeatures-1.5 {
  R-29243-18547: string equal -length mismatch
} -body {
  string equal -length 4 hello help
} -result {0}}

###############################################################################

runTest {test newfeatures-1.6 {
  R-46584-30958: string equal empty strings
} -body {
  string equal "" ""
} -result {1}}

###############################################################################
#
# Section 2 -- info globals
#
###############################################################################

runTest {test newfeatures-2.1 {
  R-50590-64823: info globals returns known globals
} -body {
  expr {[lsearch [info globals] "auto_path"] >= 0}
} -result {1}}

###############################################################################

runTest {test newfeatures-2.2 {
  R-64634-09797: info globals pattern filtering
} -setup {
} -body {
  set result [info globals "auto_*"]
  expr {[lsearch $result "auto_path"] >= 0}
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test newfeatures-2.3 {
  R-64634-09797: info globals no match returns empty
} -body {
  info globals "zzz_nonexistent_*"
} -result {}}

###############################################################################

runTest {test newfeatures-2.4 {
  R-50590-64823: info globals contains tcl_platform
} -body {
  expr {[lsearch [info globals] "tcl_platform"] >= 0}
} -result {1}}

###############################################################################
#
# Section 3 -- info patchlevel
#
###############################################################################

runTest {test newfeatures-3.1 {
  R-01267-55373: info patchlevel returns non-empty
} -body {
  expr {[string length [info patchlevel]] > 0}
} -result {1}}

###############################################################################

runTest {test newfeatures-3.2 {
  R-01267-55373: info patchlevel matches version format
} -constraints {
    regexp
} -body {
  regexp {^\d+\.\d+\.\d+} [info patchlevel]
} -result {1}}

###############################################################################

runTest {test newfeatures-3.3 {
  R-01267-55373: info patchlevel matches tcl_platform
} -constraints {
    th8
} -body {
  string equal [info patchlevel] $::tcl_platform(patchLevel)
} -result {1}}

###############################################################################
#
# Section 4 -- info complete
#
###############################################################################

runTest {test newfeatures-4.1 {
  R-40646-36669: info complete with complete script
} -setup {
} -body {
  info complete {set x 1}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test newfeatures-4.2 {
  R-40646-36669: info complete with unmatched brace
} -setup {
} -body {
  info complete "set x \{"
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test newfeatures-4.3 {
  R-40646-36669: info complete with unmatched quote
} -setup {
} -body {
  info complete {set x "hello}
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test newfeatures-4.4 {
  R-40646-36669: info complete with empty string
} -body {
  info complete ""
} -result {1}}

###############################################################################

runTest {test newfeatures-4.5 {
  R-40646-36669: info complete multiline
} -setup {
} -body {
  info complete "set x 1\nset y 2"
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################
#
# Section 5 -- return -errorinfo -errorcode
#
###############################################################################

runTest {test newfeatures-5.1 {
  R-35585-00247: return -code error sets error
} -setup {
} -body {
  proc ::rettest {} {
    return -code error "bad thing"
  }
  list [catch {::rettest} msg] $msg
} -cleanup {
  catch {rename ::rettest ""}
  unset -nocomplain msg
} -result {1 {bad thing}}}

###############################################################################

runTest {test newfeatures-5.2 {
  R-36134-44560: return -errorinfo sets errorInfo
} -setup {
} -body {
  proc ::rettest {} {
    return -code error -errorinfo "custom trace" "oops"
  }
  catch {::rettest} msg
  string match "custom trace*" $::errorInfo
} -cleanup {
  catch {rename ::rettest ""}
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test newfeatures-5.3 {
  R-42915-22081: return -errorcode sets errorCode
} -setup {
} -body {
  proc ::rettest {} {
    return -code error -errorcode MY_CODE "fail"
  }
  catch {::rettest} msg
  set ::errorCode
} -cleanup {
  catch {rename ::rettest ""}
  unset -nocomplain msg
} -result {MY_CODE}}

###############################################################################

runTest {test newfeatures-5.4 {
  R-32746-05995: return -code ok returns normally
} -setup {
} -body {
  proc ::rettest {} {
    return -code ok "value"
  }
  ::rettest
} -cleanup {
  catch {rename ::rettest ""}
} -result {value}}

###############################################################################

runTest {test newfeatures-5.5 {
  R-36134-44560: return with all options combined
} -setup {
} -body {
  proc ::rettest {} {
    return -code error -errorinfo "stack" -errorcode MYERR "msg"
  }
  set rc [catch {::rettest} msg]
  list $rc $msg $::errorCode
} -cleanup {
  catch {rename ::rettest ""}
  unset -nocomplain rc msg
} -match glob -result {1 msg MYERR}}

###############################################################################
#
# Section 6 -- switch -regexp
#
###############################################################################

runTest {test newfeatures-6.1 {
  R-43818-50387: switch -regexp basic match
} -constraints {
    switch_regexp
} -setup {
} -body {
  switch -regexp "hello123" {
    {^[0-9]+$} { set x digits }
    {^hello}   { set x greeting }
    default    { set x other }
  }
} -cleanup {
  unset -nocomplain x
} -result {greeting}}

###############################################################################

runTest {test newfeatures-6.2 {
  R-43818-50387: switch -regexp default fallthrough
} -constraints {
    switch_regexp
} -setup {
} -body {
  switch -regexp "xyz" {
    {^[0-9]+$} { set x digits }
    {^hello}   { set x greeting }
    default    { set x other }
  }
} -cleanup {
  unset -nocomplain x
} -result {other}}

###############################################################################

runTest {test newfeatures-6.3 {
  R-43818-50387: switch -regexp case sensitive
} -constraints {
    switch_regexp
} -setup {
} -body {
  switch -regexp "HELLO" {
    {^hello$} { set x lower }
    {^HELLO$} { set x upper }
    default   { set x other }
  }
} -cleanup {
  unset -nocomplain x
} -result {upper}}

###############################################################################

runTest {test newfeatures-6.4 {
  R-43818-50387: switch -regexp multiple patterns
} -constraints {
    switch_regexp
} -body {
  set results [list]
  foreach val {123 abc 1a2b} {
    switch -regexp $val {
      {^[0-9]+$}      { lappend results digit }
      {^[a-z]+$}      { lappend results alpha }
      default         { lappend results mixed }
    }
  }
  set results
} -cleanup {
  unset -nocomplain results val
} -result {digit alpha mixed}}

###############################################################################
#
# Section 7 -- file normalize
#
###############################################################################

runTest {test newfeatures-7.1 {
  R-19004-61741: file normalize dot returns non-empty
} -body {
  expr {[string length [file normalize .]] > 0}
} -result {1}}

###############################################################################

runTest {test newfeatures-7.2 {
  R-19004-61741: file normalize relative path
} -setup {
} -body {
  set n [file normalize "somefile"]
  expr {[string length $n] >= [string length "somefile"]}
} -cleanup {
  unset -nocomplain n
} -result {1}}

###############################################################################

runTest {test newfeatures-7.3 {
  R-19004-61741: file normalize idempotent
} -body {
  set abs [file normalize .]
  string equal [file normalize $abs] $abs
} -cleanup {
  unset -nocomplain abs
} -result {1}}

###############################################################################
#
# Section 8 -- foreach multi-varlist
#
###############################################################################

runTest {test newfeatures-8.1 {
  R-55480-36320: foreach two pairs even lengths
} -setup {
} -body {
  set result ""
  foreach a {1 2} x {A B} {
    append result "($a,$x) "
  }
  set result
} -cleanup {
  unset -nocomplain result a x
} -result {(1,A) (2,B) }}

###############################################################################

runTest {test newfeatures-8.2 {
  R-55480-36320: foreach uneven lengths pads empty
} -setup {
} -body {
  set result ""
  foreach a {1 2 3} x {A B} {
    append result "($a,$x) "
  }
  set result
} -cleanup {
  unset -nocomplain result a x
} -result {(1,A) (2,B) (3,) }}

###############################################################################

runTest {test newfeatures-8.3 {
  R-62928-07465: foreach single pair regression
} -setup {
} -body {
  set result [list]
  foreach x {a b c} {
    lappend result $x
  }
  set result
} -cleanup {
  unset -nocomplain result x
} -result {a b c}}

###############################################################################
#
# Section 9 -- string match -nocase
#
###############################################################################

runTest {test newfeatures-9.1 {
  R-00941-13743: string match -nocase
} -body {
  string match -nocase HELLO* hello_world
} -result {1}}

###############################################################################

runTest {test newfeatures-9.2 {
  R-00941-13743: string match -nocase no match
} -body {
  string match -nocase HELLO* goodbye
} -result {0}}

###############################################################################

runTest {test newfeatures-9.3 {
  R-61454-64199: string match without -nocase is case-sensitive
} -body {
  string match HELLO* hello_world
} -result {0}}

###############################################################################
#
# Section 10 -- lsearch options
#
###############################################################################

runTest {test newfeatures-10.1 {
  R-38095-62274: lsearch -exact
} -body {
  lsearch -exact {apple banana cherry} banana
} -result {1}}

###############################################################################

runTest {test newfeatures-10.2 {
  R-38095-62274: lsearch -exact no match
} -body {
  lsearch -exact {apple banana cherry} ban*
} -result {-1}}

###############################################################################

runTest {test newfeatures-10.3 {
  R-65407-60482: lsearch -glob default
} -body {
  lsearch {apple banana cherry} ban*
} -result {1}}

###############################################################################

runTest {test newfeatures-10.4 {
  R-55396-44593: lsearch -all
} -body {
  lsearch -all {a b a c a} a
} -result {0 2 4}}

###############################################################################

runTest {test newfeatures-10.5 {
  R-01660-06229: lsearch -inline
} -body {
  lsearch -inline {apple banana cherry} b*
} -result {banana}}

###############################################################################

runTest {test newfeatures-10.6 {
  R-37219-60333: lsearch -not
} -body {
  lsearch -all -not -exact {a b c a b} a
} -result {1 2 4}}

###############################################################################

runTest {test newfeatures-10.7 {
  R-13428-50077: lsearch -start
} -body {
  lsearch -start 2 {a b c b d} b
} -result {3}}

###############################################################################

runTest {test newfeatures-10.8 {
  R-12193-55604: lsearch -regexp
} -constraints {
    lsearch_regexp
} -body {
  lsearch -regexp {abc 123 def 456} {^[0-9]+$}
} -result {1}}

###############################################################################

runTest {test newfeatures-10.9 {
  R-55396-44593: lsearch -all -inline
} -body {
  lsearch -all -inline {apple banana avocado cherry} a*
} -result {apple avocado}}

###############################################################################

source tests/epilogue.tcl
