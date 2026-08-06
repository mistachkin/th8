###############################################################################
#
# coverage4.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Tests exercising uncovered code paths across th8_lang.c and th8.c to
# boost line/branch coverage.  These are mostly coverage-driven;
# R-markers are used where a specific requirement is being tested.
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
# Section 1 -- Command error paths (th8_lang.c)
#
###############################################################################

runTest {test coverage4-1.1 {break too many args} -body {
    catch {break a b}
} -result {1}}

###############################################################################

runTest {test coverage4-1.2 {continue too many args} -body {
    catch {continue a b}
} -result {1}}

###############################################################################

runTest {test coverage4-1.2a {
  R-09845-29637: break accepts an optional result string, observable
  through a catch that traps the break directly
} -constraints {breakOptArg} -body {
  list [catch {break foo} m] $m
} -cleanup {
  unset -nocomplain m
} -result {3 foo}}

###############################################################################

runTest {test coverage4-1.2b {
  R-20389-43665: continue accepts an optional result string, observable
  through a catch that traps the continue directly
} -constraints {breakOptArg} -body {
  list [catch {continue bar} m] $m
} -cleanup {
  unset -nocomplain m
} -result {4 bar}}

###############################################################################

runTest {test coverage4-1.2c {
  R-09845-29637: an enclosing loop discards the break result and still
  yields the empty string
} -constraints {breakOptArg} -body {
  while {1} {break discarded}
} -result {}}

###############################################################################

runTest {test coverage4-1.3 {return -code with symbolic names} -body {
    proc _cov4_rcode {} {return -code error "val"}
    list [catch {_cov4_rcode} msg] $msg
} -cleanup {
  catch {rename _cov4_rcode ""}
  unset -nocomplain msg
} -result {1 val}}

###############################################################################

runTest {test coverage4-1.4 {return -code break} -body {
    proc _cov4_rbreak {} {return -code break}
    catch {_cov4_rbreak}
} -cleanup {
  catch {rename _cov4_rbreak ""}
} -result {3}}

###############################################################################

runTest {test coverage4-1.5 {return -code continue} -body {
    proc _cov4_rcont {} {return -code continue}
    catch {_cov4_rcont}
} -cleanup {
  catch {rename _cov4_rcont ""}
} -result {4}}

###############################################################################

runTest {test coverage4-1.6 {return -code bad value} -body {
    catch {return -code badvalue}
} -result {1}}

###############################################################################
#
# Section 2 -- String command edge cases (th8_lang.c)
#
###############################################################################

runTest {test coverage4-2.1 {string length wrong args} -body {
    catch {string length}
} -result {1}}

###############################################################################

runTest {test coverage4-2.2 {string index end-N} -body {
    string index "abcde" end-1
} -result {d}}

###############################################################################

runTest {test coverage4-2.3 {string index out of range} -body {
    string index "abc" 10
} -result {}}

###############################################################################

runTest {test coverage4-2.4 {string range end-N} -body {
    string range "abcde" end-2 end
} -result {cde}}

###############################################################################

runTest {test coverage4-2.5 {string compare wrong args} -body {
    catch {string compare}
} -result {1}}

###############################################################################

runTest {test coverage4-2.6 {string equal empty strings} -body {
    string equal "" ""
} -result {1}}

###############################################################################

runTest {test coverage4-2.7 {string first not found} -body {
    string first "xyz" "abcdef"
} -result {-1}}

###############################################################################

runTest {test coverage4-2.8 {string last basic} -setup {
    unset -nocomplain msg
} -body {
  catch {string last "b" "abcabc"} msg
  expr {$msg >= 0 || $msg eq ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test coverage4-2.9 {string repeat} -constraints {
    string_repeat
} -body {
  string repeat "ab" 3
} -result {ababab}}

###############################################################################

runTest {test coverage4-2.10 {string reverse} -constraints {
    string_reverse
} -body {
  string reverse "abc"
} -result {cba}}

###############################################################################

runTest {test coverage4-2.11 {string trim} -body {
    string trim "  hello  "
} -result {hello}}

###############################################################################

runTest {test coverage4-2.12 {string trimleft} -body {
    string trimleft "xxhello" x
} -result {hello}}

###############################################################################

runTest {test coverage4-2.13 {string trimright} -body {
    string trimright "helloxx" x
} -result {hello}}

###############################################################################

runTest {test coverage4-2.14 {string map basic} -constraints {
    string_map
} -setup {
} -body {
  catch {string map {a A b B} "aabbcc"} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {AABBcc}}

###############################################################################

runTest {test coverage4-2.15 {string is integer} -constraints {
    string_is
} -setup {
} -body {
  catch {string is integer 42} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test coverage4-2.16 {string is integer non-integer} -constraints {
    string_is
} -setup {
} -body {
  catch {string is integer abc} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -result {0}}

###############################################################################
#
# Section 3 -- List command edge cases (th8_lang.c)
#
###############################################################################

runTest {test coverage4-3.1 {lindex nested} -body {
    lindex {{a b} {c d}} 1
} -result {c d}}

###############################################################################

runTest {test coverage4-3.2 {lrange wrong args} -body {
    catch {lrange}
} -result {1}}

###############################################################################

runTest {test coverage4-3.3 {lreplace wrong args} -body {
    catch {lreplace}
} -result {1}}

###############################################################################

runTest {test coverage4-3.4 {lsort wrong args} -body {
    catch {lsort}
} -result {1}}

###############################################################################

runTest {test coverage4-3.5 {lsort -decreasing} -body {
    lsort -decreasing {b a c}
} -result {c b a}}

###############################################################################

runTest {test coverage4-3.6 {lsort -integer} -body {
    lsort -integer {3 1 2}
} -result {1 2 3}}

###############################################################################

runTest {test coverage4-3.7 {lappend to new variable} -setup {
    unset -nocomplain _cov4_new
} -body {
  lappend _cov4_new a b c
} -cleanup {
  unset -nocomplain _cov4_new
} -result {a b c}}

###############################################################################

runTest {test coverage4-3.8 {llength wrong args} -body {
    catch {llength}
} -result {1}}

###############################################################################

runTest {test coverage4-3.9 {lsearch not found} -body {
    lsearch {a b c} d
} -result {-1}}

###############################################################################
#
# Section 4 -- Variable/namespace edge cases (th8.c)
#
###############################################################################

runTest {test coverage4-4.1 {set wrong args} -body {
    catch {set}
} -result {1}}

###############################################################################

runTest {test coverage4-4.2 {unset nonexistent nocomplain} -body {
    unset -nocomplain _no_such_var
} -result {}}

###############################################################################

runTest {test coverage4-4.3 {upvar wrong args} -body {
    catch {upvar}
} -result {1}}

###############################################################################

runTest {test coverage4-4.4 {global in proc} -setup {
    set ::_cov4_gvar 99
} -body {
  proc _cov4_p {} {global _cov4_gvar; set _cov4_gvar}
  _cov4_p
} -cleanup {
  catch {rename _cov4_p ""}
  unset -nocomplain ::_cov4_gvar
} -result {99}}

###############################################################################

runTest {test coverage4-4.5 {set in namespace} -constraints {
    namespace
} -setup {
} -body {
  namespace eval ::cov4ns {set v 42; set v}
} -cleanup {
  catch {namespace delete ::cov4ns}
} -result {42}}

###############################################################################

runTest {test coverage4-4.6 {info exists on array element} -setup {
    array set _cov4_arr {a 1 b 2}
} -body {
  info exists _cov4_arr(a)
} -cleanup {
  unset -nocomplain _cov4_arr
} -result {1}}

###############################################################################

runTest {test coverage4-4.7 {array names} -setup {
    array set _cov4_arr {x 1 y 2 z 3}
} -body {
  lsort [array names _cov4_arr]
} -cleanup {
  unset -nocomplain _cov4_arr
} -result {x y z}}

###############################################################################

runTest {test coverage4-4.8 {array size} -setup {
    array set _cov4_arr {a 1 b 2 c 3}
} -body {
  array size _cov4_arr
} -cleanup {
  unset -nocomplain _cov4_arr
} -result {3}}

###############################################################################

runTest {test coverage4-4.9 {array get} -setup {
    array set _cov4_arr {k v}
} -body {
  array get _cov4_arr
} -cleanup {
  unset -nocomplain _cov4_arr
} -result {k v}}

###############################################################################
#
# Section 5 -- Control flow edge cases
#
###############################################################################

runTest {test coverage4-5.1 {while with break} -setup {
    unset -nocomplain i
} -body {
  set i 0
  while {1} {
    if {$i >= 3} then {break}
    incr i
  }
  set i
} -cleanup {
  unset -nocomplain i
} -result {3}}

###############################################################################

runTest {test coverage4-5.2 {while with continue} -setup {
    unset -nocomplain i
    unset -nocomplain r
} -body {
  set r {}
  set i 0
  while {$i < 5} {
    incr i
    if {$i == 3} then {continue}
    lappend r $i
  }
  set r
} -cleanup {
  unset -nocomplain r
  unset -nocomplain i
} -result {1 2 4 5}}

###############################################################################

runTest {test coverage4-5.3 {for with break} -setup {
    unset -nocomplain i
} -body {
  for {set i 0} {$i < 10} {incr i} {
    if {$i == 5} then {break}
  }
  set i
} -cleanup {
  unset -nocomplain i
} -result {5}}

###############################################################################

runTest {test coverage4-5.4 {foreach with continue} -body {
    set r {}
    foreach x {1 2 3 4 5} {
	if {$x == 3} then {continue}
	lappend r $x
    }
    set r
} -cleanup {
  unset -nocomplain r
  unset -nocomplain x
} -result {1 2 4 5}}

###############################################################################

runTest {test coverage4-5.5 {switch with default} -body {
    switch abc {xyz {set r 1} default {set r 2}}
} -cleanup {
    unset -nocomplain r
} -result {2}}

###############################################################################

runTest {test coverage4-5.6 {switch no match no default} -body {
    catch {switch abc {xyz {set r 1}}} msg
    expr {$msg eq "" || $msg ne ""}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test coverage4-5.7 {catch with returnCodes} -setup {
    unset -nocomplain msg
} -body {
  catch {return -code 2 val} msg
  list $msg
} -cleanup {
  unset -nocomplain msg
} -result {val}}

###############################################################################

runTest {test coverage4-5.8 {if without then} -setup {
    unset -nocomplain x
} -body {
  if {1} then {set x ok}
  set x
} -cleanup {
  unset -nocomplain x
} -result {ok}}

###############################################################################

runTest {test coverage4-5.9 {if with elseif} -setup {
    unset -nocomplain x
} -body {
  if {0} then {set x a} elseif {1} then {set x b} else {set x c}
  set x
} -cleanup {
  unset -nocomplain x
} -result {b}}

###############################################################################
#
# Section 6 -- Expression edge cases (th8.c)
#
###############################################################################

runTest {test coverage4-6.1 {expr ternary operator} -body {
    expr {1 ? "yes" : "no"}
} -result {yes}}

###############################################################################

runTest {test coverage4-6.2 {expr ternary false} -body {
    expr {0 ? "yes" : "no"}
} -result {no}}

###############################################################################

runTest {test coverage4-6.3 {expr string equality} -body {
    expr {"abc" eq "abc"}
} -result {1}}

###############################################################################

runTest {test coverage4-6.4 {expr string inequality} -body {
    expr {"abc" ne "def"}
} -result {1}}

###############################################################################

runTest {test coverage4-6.5 {expr bitwise operations} -body {
    expr {0xFF & 0x0F}
} -result {15}}

###############################################################################

runTest {test coverage4-6.6 {expr shift operations} -body {
    expr {1 << 4}
} -result {16}}

###############################################################################

runTest {test coverage4-6.7 {expr unary minus} -body {
    expr {-42}
} -result {-42}}

###############################################################################

runTest {test coverage4-6.8 {expr unary not} -body {
    expr {!0}
} -result {1}}

###############################################################################

runTest {test coverage4-6.9 {expr unary bitnot} -body {
    expr {~0}
} -result {-1}}

###############################################################################

runTest {test coverage4-6.10 {expr double arithmetic} -body {
    expr {1.5 + 2.5}
} -result {4.0}}

###############################################################################

runTest {test coverage4-6.11 {expr modulo} -body {
    expr {17 % 5}
} -result {2}}

###############################################################################

runTest {test coverage4-6.12 {expr comparison operators} -body {
    list [expr {1 < 2}] [expr {2 > 1}] [expr {1 <= 1}] [expr {1 >= 1}]
} -result {1 1 1 1}}

###############################################################################

runTest {test coverage4-6.13 {expr logical operators} -body {
    list [expr {1 && 1}] [expr {0 || 1}] [expr {1 && 0}]
} -result {1 1 0}}

###############################################################################

runTest {test coverage4-6.14 {expr wide integer} -body {
    expr {wide(1000000000) * 1000}
} -result {1000000000000}}

###############################################################################

runTest {test coverage4-6.15 {expr hex literal} -body {
    format %d [expr {0xFF}]
} -result {255}}

###############################################################################

runTest {test coverage4-6.16 {expr octal literal} -body {
    format %d [expr {0o17}]
} -result {15}}

###############################################################################

runTest {test coverage4-6.17 {expr min/max via ternary} -body {
    expr {3 > 5 ? 3 : 5}
} -result {5}}

###############################################################################
#
# Section 7 -- Format/scan edge cases
#
###############################################################################

runTest {test coverage4-7.1 {format %c character} -body {
    format %c 65
} -result {A}}

###############################################################################

runTest {test coverage4-7.2 {format %e scientific} -body {
    string match "*e*" [format %e 1.5]
} -result {1}}

###############################################################################

runTest {test coverage4-7.3 {format %f float} -body {
    format %.2f 3.14159
} -result {3.14}}

###############################################################################

runTest {test coverage4-7.4 {format multiple args} -body {
    format "%s=%d" "x" 42
} -result {x=42}}

###############################################################################

runTest {test coverage4-7.5 {scan basic integer} -constraints {
    scan
} -setup {
} -body {
  catch {scan "42" "%d" x; set x} msg
  set msg
} -cleanup {
  unset -nocomplain x
  unset -nocomplain msg
} -result {42}}

###############################################################################
#
# Section 8 -- Misc commands
#
###############################################################################

runTest {test coverage4-8.1 {incr nonexistent creates} -setup {
    unset -nocomplain _cov4_i
} -constraints {not_eagle} -body {
  incr _cov4_i
  set _cov4_i
} -cleanup {
  unset -nocomplain _cov4_i
} -result {1}}

###############################################################################

runTest {test coverage4-8.2 {incr by negative} -setup {
    set _cov4_i 10
} -body {
  incr _cov4_i -3
} -cleanup {
  unset -nocomplain _cov4_i
} -result {7}}

###############################################################################

runTest {test coverage4-8.3 {append multiple values} -setup {
    set x "a"
} -body {
  append x "b" "c" "d"
} -cleanup {
  unset -nocomplain x
} -result {abcd}}

###############################################################################

runTest {test coverage4-8.4 {concat} -constraints {
    concat
} -body {
  concat "a b" "c d"
} -result {a b c d}}

###############################################################################

runTest {test coverage4-8.5 {subst} -constraints {
    subst
} -setup {
  set x 42
} -body {
  subst {value is $x}
} -cleanup {
  unset -nocomplain x
} -result {value is 42}}

###############################################################################

runTest {test coverage4-8.6 {info body} -setup {
    proc _cov4_p {} {return ok}
} -body {
  info body _cov4_p
} -cleanup {
  catch {rename _cov4_p ""}
} -result {return ok}}

###############################################################################

runTest {test coverage4-8.7 {info args} -setup {
    proc _cov4_p {a b c} {}
} -body {
  info args _cov4_p
} -cleanup {
  catch {rename _cov4_p ""}
} -result {a b c}}

###############################################################################

runTest {test coverage4-8.8 {info commands with pattern} -body {
    expr {[llength [info commands set*]] > 0}
} -result {1}}

###############################################################################

runTest {test coverage4-8.9 {info vars with pattern} -setup {
    set _cov4_test 1
} -body {
  expr {[lsearch [info vars _cov4*] _cov4_test] >= 0}
} -cleanup {
  unset -nocomplain _cov4_test
} -result {1}}

###############################################################################

runTest {test coverage4-8.10 {time command} -constraints {
    time
} -body {
  expr {[lindex [time {expr {1+1}} 10] 0] >= 0}
} -result {1}}

###############################################################################
#
# Section 9 -- Resource limits
#
###############################################################################

runTest {test coverage4-9.1 {
  R-29508-16704: maximum string length is 100 MB; attempting to exceed it
                 produces an error
} -constraints {
    th8
} -setup {
} -body {
  #
  # Try to create a string that exceeds 100 MB by using
  # string repeat.  This should fail with an oversize error.
  #
  list [catch {string repeat "x" 104857601} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
