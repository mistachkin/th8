###############################################################################
#
# coverage5.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Tests exercising uncovered code paths across th8_lang.c, th8_regex.c,
# and th8.c to boost line/branch coverage.  Focuses on namespace
# operations, regex features, file commands, error handling, and
# interp/package commands.  These are coverage-driven, not
# requirement-driven, so no R-markers are used.
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
# Section 1 -- Namespace operations (th8_lang.c)
#
###############################################################################

runTest {test coverage5-1.1 {namespace eval creates namespace} -constraints {
    namespace
} -body {
  namespace eval ::cov5_ns1 {set x 1}
  namespace delete ::cov5_ns1
} -result {}}

###############################################################################

runTest {test coverage5-1.2 {namespace current} -constraints {
    namespace
} -body {
  namespace eval ::cov5_ns2 {namespace current}
} -cleanup {
  catch {namespace delete ::cov5_ns2}
} -result {::cov5_ns2}}

###############################################################################

runTest {test coverage5-1.3 {namespace children} -constraints {
    namespace namespace_children
} -body {
  namespace eval ::cov5_ns3 {namespace eval child {}}
  set kids [namespace children ::cov5_ns3]
  namespace delete ::cov5_ns3
  set kids
} -cleanup {
  unset -nocomplain kids
} -result {::cov5_ns3::child}}

###############################################################################

runTest {test coverage5-1.4 {namespace parent of child} -constraints {
    namespace namespace_parent
} -body {
  namespace eval ::cov5_ns4 {namespace eval child {}}
  namespace parent ::cov5_ns4
} -cleanup {
  catch {namespace delete ::cov5_ns4}
} -result {::}}

###############################################################################

runTest {test coverage5-1.5 {namespace export/import} -constraints {
    namespace namespace_export namespace_import
} -body {
  namespace eval ::cov5_exp {
    proc myfunc {} {return exported}
    namespace export myfunc
  }
  namespace eval ::cov5_imp {
    namespace import ::cov5_exp::myfunc
    myfunc
  }
} -cleanup {
  catch {namespace delete ::cov5_imp}
  catch {namespace delete ::cov5_exp}
} -result {exported}}

###############################################################################

runTest {test coverage5-1.6 {namespace delete} -constraints {
    namespace
} -body {
  namespace eval ::cov5_del {set x 1}
  namespace delete ::cov5_del
  catch {namespace eval ::cov5_del {set x}} msg
  expr {$msg ne ""}
} -cleanup {
  catch {namespace delete ::cov5_del}
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test coverage5-1.7 {namespace eval wrong args} -constraints {
    namespace
} -body {
  catch {namespace eval}
} -result {1}}

###############################################################################

runTest {test coverage5-1.8 {namespace delete wrong args} -constraints {
    th8 namespace
} -body {
  catch {namespace delete}
} -result {1}}

###############################################################################

runTest {test coverage5-1.9 {namespace eval nested} -constraints {
    namespace
} -body {
  namespace eval ::cov5_outer {
    namespace eval inner {
      proc foo {} {return "deep"}
    }
  }
  ::cov5_outer::inner::foo
} -cleanup {
  catch {namespace delete ::cov5_outer}
} -result {deep}}

###############################################################################

runTest {test coverage5-1.10 {namespace eval returns last result} -constraints {
    namespace
} -body {
  namespace eval ::cov5_var {
    set myvar hello
    set myvar
  }
} -cleanup {
  catch {namespace delete ::cov5_var}
} -result {hello}}

###############################################################################
#
# Section 2 -- Regex features (th8_regex.c)
#
###############################################################################

runTest {test coverage5-2.1 {regexp -all count} -constraints {
    regexp
} -body {
  regexp -all {[0-9]+} "a1b22c333"
} -result {3}}

###############################################################################

runTest {test coverage5-2.2 {regexp -indices} -constraints {
    regexp
} -setup {
} -body {
  regexp -indices {[0-9]+} "abc123def" match
  set match
} -cleanup {
  unset -nocomplain match
} -result {3 5}}

###############################################################################

runTest {test coverage5-2.3 {regexp -inline} -constraints {
    regexp
} -body {
  regexp -inline {([a-z]+)([0-9]+)} "abc123"
} -result {abc123 abc 123}}

###############################################################################

runTest {test coverage5-2.4 {regexp -all -inline} -constraints {
    regexp regexp_all_inline
} -body {
  regexp -all -inline {[0-9]+} "a1b2c3"
} -result {1 2 3}}

###############################################################################

runTest {test coverage5-2.5 {regexp -start} -constraints {
    regexp
} -body {
  regexp -start 5 {[0-9]+} "abc12def34"
} -result {1}}

###############################################################################

runTest {test coverage5-2.6 {regexp -nocase -all} -constraints {
    regexp regexp_nocase
} -body {
  regexp -all -nocase {abc} "ABCabcABC"
} -result {3}}

###############################################################################

runTest {test coverage5-2.7 {regsub with backreference} -constraints {
    regsub
} -setup {
} -body {
  regsub {([a-z]+)([0-9]+)} "abc123" {\2-\1} result
  set result
} -cleanup {
  unset -nocomplain result
} -result {123-abc}}

###############################################################################

runTest {test coverage5-2.8 {regsub -all -nocase} -constraints {
    regsub regsub_nocase
} -setup {
} -body {
  regsub -all -nocase {abc} "ABCxxABC" "replaced" result
  set result
} -cleanup {
  unset -nocomplain result
} -result {replacedxxreplaced}}

###############################################################################

runTest {test coverage5-2.9 {regexp -expanded} -constraints {
    regexp
} -body {
  regexp -expanded {
    [0-9]+   # one or more digits
  } "abc42def"
} -result {1}}

###############################################################################

runTest {test coverage5-2.10 {regexp -line} -constraints {
    regexp
} -body {
  regexp -line {^second$} "first\nsecond\nthird"
} -result {1}}

###############################################################################

runTest {test coverage5-2.11 {regexp with capture groups} -constraints {
    regexp
} -setup {
} -body {
  regexp {(\w+)@(\w+)} "user@host" all name domain
  list $all $name $domain
} -cleanup {
  unset -nocomplain all name domain
} -result {user@host user host}}

###############################################################################

runTest {test coverage5-2.12 {regexp no match with variables} -constraints {
    regexp
} -setup {
} -body {
  set r [regexp {xyz} "abc" match]
  list $r [info exists match]
} -cleanup {
  unset -nocomplain r
  unset -nocomplain match
} -result {0 0}}

###############################################################################
#
# Section 3 -- File commands (th8_lang.c)
#
###############################################################################

runTest {test coverage5-3.1 {file dirname /} -body {
    file dirname /
} -result {/}}

###############################################################################

runTest {test coverage5-3.2 {file dirname relative} -body {
    file dirname "a/b/c"
} -result {a/b}}

###############################################################################

runTest {test coverage5-3.3 {file join multiple} -body {
    file join a b c
} -result {a/b/c}}

###############################################################################

runTest {test coverage5-3.4 {file split} -constraints {
    file_split
} -body {
  file split "a/b/c"
} -result {a b c}}

###############################################################################

runTest {test coverage5-3.5 {file tail nested} -body {
    file tail "a/b/c.txt"
} -result {c.txt}}

###############################################################################

runTest {test coverage5-3.6 {file exists on nonexistent} -body {
    file exists _no_such_file_ever
} -result {0}}

###############################################################################

runTest {test coverage5-3.7 {file normalize relative} -constraints {
    file_normalize
} -body {
  expr {[string length [file normalize "."]] > 0}
} -result {1}}

###############################################################################

runTest {test coverage5-3.8 {file dirname wrong args} -body {
    catch {file dirname}
} -result {1}}

###############################################################################

runTest {test coverage5-3.9 {file join wrong args} -body {
    catch {file join}
} -result {1}}

###############################################################################

runTest {test coverage5-3.10 {file tail wrong args} -body {
    catch {file tail}
} -result {1}}

###############################################################################
#
# Section 4 -- Error handling and edge cases (th8.c)
#
###############################################################################

runTest {test coverage5-4.1 {deep recursion limit} -constraints {
    th8
} -body {
  proc _cov5_recurse {n} {
    if {$n > 0} then {_cov5_recurse [expr {$n - 1}]}
  }
  catch {_cov5_recurse 20000} msg
  string match "*recursion*" $msg
} -cleanup {
  catch {rename _cov5_recurse ""}
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test coverage5-4.2 {error in nested eval} -setup {
    unset -nocomplain msg
} -body {
  catch {eval {error "nested error"}} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {nested error}}

###############################################################################

runTest {test coverage5-4.3 {uplevel 0} -body {
    proc _cov5_up {} {
        uplevel 0 {set _cov5_local 99}
        set _cov5_local
    }
    _cov5_up
} -cleanup {
  catch {rename _cov5_up ""}
} -result {99}}

###############################################################################

runTest {test coverage5-4.4 {info level} -body {
    proc _cov5_lev {} {info level}
    _cov5_lev
} -cleanup {
  catch {rename _cov5_lev ""}
} -result {1}}

###############################################################################

runTest {test coverage5-4.5 {info level 0} -body {
    proc _cov5_lev0 {a b} {info level 0}
    _cov5_lev0 x y
} -cleanup {
  catch {rename _cov5_lev0 ""}
} -result {_cov5_lev0 x y}}

###############################################################################

runTest {test coverage5-4.6 {proc redefine} -body {
    proc _cov5_redef {} {return v1}
    proc _cov5_redef {} {return v2}
    _cov5_redef
} -cleanup {
  catch {rename _cov5_redef ""}
} -result {v2}}

###############################################################################

runTest {test coverage5-4.7 {proc with defaults} -body {
    proc _cov5_def {a {b default}} {list $a $b}
    _cov5_def hello
} -cleanup {
  catch {rename _cov5_def ""}
} -result {hello default}}

###############################################################################

runTest {test coverage5-4.8 {proc with args} -body {
    proc _cov5_args {a args} {list $a $args}
    _cov5_args 1 2 3
} -cleanup {
  catch {rename _cov5_args ""}
} -result {1 {2 3}}}

###############################################################################
#
# Section 5 -- Interp/package commands (th8_lang.c)
#
###############################################################################

runTest {test coverage5-5.1 {package provide and query} -body {
    package provide _cov5_pkg 2.5
    package provide _cov5_pkg
} -cleanup {
  catch {package forget _cov5_pkg}
} -result {2.5}}

###############################################################################

runTest {test coverage5-5.2 {package names includes provided} -body {
    package provide _cov5_pkg2 1.0
    expr {[lsearch [package names] _cov5_pkg2] >= 0}
} -cleanup {
  catch {package forget _cov5_pkg2}
} -result {1}}

###############################################################################

runTest {test coverage5-5.3 {package ifneeded and require} -body {
    package ifneeded _cov5_pkg3 1.0 {package provide _cov5_pkg3 1.0}
    package require _cov5_pkg3
} -cleanup {
  catch {package forget _cov5_pkg3}
} -result {1.0}}

###############################################################################

runTest {test coverage5-5.4 {info script} -body {
    expr {[string length [info script]] > 0}
} -result {1}}

###############################################################################

runTest {test coverage5-5.5 {info patchlevel} -body {
    expr {[string length [info patchlevel]] > 0}
} -result {1}}

###############################################################################

runTest {test coverage5-5.6 {info complete balanced} -setup {
    unset -nocomplain x
} -body {
  info complete {set x [expr {1+2}]}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test coverage5-5.7 {info complete unbalanced} -setup {
    unset -nocomplain x
} -body {
  info complete "set x \[expr \{"
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################
#
# Section 6 -- array unset (th8_lang.c)
#
###############################################################################

runTest {test coverage5-6.1 {
  R-27569-39885: array unset removes matching elements
} -constraints {
    namespace
} -body {
  array set _cov5_au {a 1 b 2 c 3}
  array unset _cov5_au b
  lsort [array names _cov5_au]
} -cleanup {
  unset -nocomplain _cov5_au
} -result {a c}}

###############################################################################

runTest {test coverage5-6.2 {
  R-59091-42737: array unset without pattern removes entire array
} -body {
  array set _cov5_au2 {x 1 y 2}
  array unset _cov5_au2
  array exists _cov5_au2
} -cleanup {
  unset -nocomplain _cov5_au2
} -result {0}}

###############################################################################

source tests/epilogue.tcl
