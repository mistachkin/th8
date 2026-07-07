###############################################################################
#
# if.tcl --
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
# Section 1 -- if: Basic true/false conditions
#
###############################################################################

runTest {test if-1.1 {
  R-06371-03281: basic true condition executes body
} -setup {
} -body {
  set result "not reached"
  if {1} then {
    set result "reached"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {reached}}

###############################################################################

runTest {test if-1.2 {
  R-23357-48390: basic false condition skips body
} -setup {
} -body {
  set result "not reached"
  if {0} then {
    set result "reached"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {not reached}}

###############################################################################

runTest {test if-1.3 {
  R-06371-03281: non-zero integer is true
} -setup {
} -body {
  set result "not reached"
  if {42} then {
    set result "reached"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {reached}}

###############################################################################
#
# Section 2 -- if: then keyword
#
###############################################################################

runTest {test if-2.1 {
  R-08087-37238: then keyword is accepted
} -setup {
} -body {
  if {1} then {
    set result "then works"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {then works}}

###############################################################################
#
# Section 3 -- if: elseif
#
###############################################################################

runTest {test if-3.1 {
  R-31074-26008: elseif when first condition is false
} -setup {
} -body {
  if {0} then {
    set result "first"
  } elseif {1} then {
    set result "second"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {second}}

###############################################################################

runTest {test if-3.2 {
  R-31074-26008: elseif chain picks first true
} -setup {
} -body {
  if {0} then {
    set result "first"
  } elseif {0} then {
    set result "second"
  } elseif {1} then {
    set result "third"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {third}}

###############################################################################
#
# Section 4 -- if: else
#
###############################################################################

runTest {test if-4.1 {
  R-04753-00601: else is executed when condition is false
} -setup {
} -body {
  if {0} then {
    set result "if"
  } else {
    set result "else"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {else}}

###############################################################################

runTest {test if-4.2 {
  R-04753-00601: else with elseif all false
} -setup {
} -body {
  if {0} then {
    set result "if"
  } elseif {0} then {
    set result "elseif"
  } else {
    set result "else"
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {else}}

###############################################################################
#
# Section 5 -- if: expr in condition
#
###############################################################################

runTest {test if-5.1 {
  R-06371-03281: expression in condition
} -setup {
} -body {
  set x 5
  if {$x > 3} then {
    set result "greater"
  } else {
    set result "not greater"
  }
  set result
} -cleanup {
  unset -nocomplain x
  unset -nocomplain result
} -result {greater}}

###############################################################################

runTest {test if-5.2 {
  R-06371-03281: compound expression in condition
} -setup {
} -body {
  set x 5
  if {$x > 3 && $x < 10} then {
    set result "in range"
  } else {
    set result "out of range"
  }
  set result
} -cleanup {
  unset -nocomplain x
  unset -nocomplain result
} -result {in range}}

###############################################################################

runTest {test if-5.3 {
  R-06371-03281: string equality in condition
} -setup {
} -body {
  set x "hello"
  if {$x eq "hello"} then {
    set result "match"
  } else {
    set result "no match"
  }
  set result
} -cleanup {
  unset -nocomplain x
  unset -nocomplain result
} -result {match}}

###############################################################################
#
# Section 6 -- if: nested if
#
###############################################################################

runTest {test if-6.1 {
  R-23357-48390: nested if
} -setup {
} -body {
  if {1} then {
    if {1} then {
      set result "inner"
    }
  }
  set result
} -cleanup {
  unset -nocomplain result
} -result {inner}}

###############################################################################

runTest {test if-6.2 {
  R-23357-48390: nested if with else
} -setup {
} -body {
  set x 5
  if {$x > 0} then {
    if {$x > 10} then {
      set result "big"
    } else {
      set result "small positive"
    }
  } else {
    set result "non-positive"
  }
  set result
} -cleanup {
  unset -nocomplain x
  unset -nocomplain result
} -result {small positive}}

###############################################################################
#
# Section 7 -- if: return value from body
#
###############################################################################

runTest {test if-7.1 {
  R-23357-48390: if returns value of last command in body
} -setup {
} -body {
  set result [if {1} then {
    expr {2 + 3}
  }]
  set result
} -cleanup {
  unset -nocomplain result
} -result {5}}

###############################################################################

runTest {test if-7.2 {
  R-04753-00601: if returns value from else body
} -setup {
} -body {
  set result [if {0} then {
    expr {1}
  } else {
    expr {2}
  }]
  set result
} -cleanup {
  unset -nocomplain result
} -result {2}}

###############################################################################

runTest {test if-7.3 {
  R-55373-08648: if returns empty when no body is executed
} -setup {
} -body {
  set result [if {0} then {
    expr {1}
  }]
  set result
} -cleanup {
  unset -nocomplain result
} -result {}}

###############################################################################
#
# Section 8 -- if: error cases
#
###############################################################################

runTest {test if-8.1 {
  R-06371-03281: if with non-boolean condition is error
} -setup {
} -body {
  list [catch {if {notabool} then {set x 1}} msg] $msg
} -cleanup {
  unset -nocomplain msg
  unset -nocomplain x
} -match glob -result {1 *}}

###############################################################################

runTest {test if-8.2 {
  R-06371-03281: if with no args is error
} -setup {
} -body {
  list [catch {if} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################
#
# Section 9 -- if: Boolean coercion corner cases
#
# These tests cover the type-coercion paths in Th8_ToBoolean(),
# verifying that all accepted forms (string synonyms, integers,
# bigints, doubles) and rejected forms produce the right result.
#
###############################################################################

runTest {test if-9.1 {
  R-06371-03281: string "0" is falsy
} -setup {
} -body {
  if {0} then {set r truthy} else {set r falsy}
  set r
} -cleanup {
  unset -nocomplain r
} -result {falsy}}

###############################################################################

runTest {test if-9.2 {
  R-06371-03281: string "1" is truthy
} -setup {
} -body {
  if {1} then {set r truthy} else {set r falsy}
  set r
} -cleanup {
  unset -nocomplain r
} -result {truthy}}

###############################################################################

runTest {test if-9.3 {
  R-06371-03281: keyword "true" is truthy (case-insensitive)
} -setup {
} -body {
  set r ""
  if {True} then {append r T} else {append r F}
  if {TRUE} then {append r T} else {append r F}
  if {true} then {append r T} else {append r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {TTT}}

###############################################################################

runTest {test if-9.4 {
  R-06371-03281: keyword "false" is falsy (case-insensitive)
} -setup {
} -body {
  set r ""
  if {False} then {append r T} else {append r F}
  if {FALSE} then {append r T} else {append r F}
  if {false} then {append r T} else {append r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {FFF}}

###############################################################################

runTest {test if-9.5 {
  R-06371-03281: keywords yes/no/on/off are accepted
} -setup {
} -body {
  set r ""
  if {yes} then {append r T} else {append r F}
  if {no}  then {append r T} else {append r F}
  if {on}  then {append r T} else {append r F}
  if {off} then {append r T} else {append r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {TFTF}}

###############################################################################

runTest {test if-9.6 {
  R-06371-03281: nonzero integer is truthy
} -setup {
} -body {
  set r ""
  if {42}      then {append r T} else {append r F}
  if {-1}      then {append r T} else {append r F}
  if {1000000} then {append r T} else {append r F}
  if {0x1F}    then {append r T} else {append r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {TTTT}}

###############################################################################

runTest {test if-9.7 {
  R-06371-03281: zero in any integer base is falsy
} -setup {
} -body {
  set r ""
  if {0}   then {append r T} else {append r F}
  if {-0}  then {append r T} else {append r F}
  if {0x0} then {append r T} else {append r F}
  if {00}  then {append r T} else {append r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {FFFF}}

###############################################################################

runTest {test if-9.8 {
  R-06371-03281: bigint values exceeding int64 range are truthy
} -constraints {
  bigint
} -setup {
} -body {
  if {2348923847623847623847623847623487623} then {set r T} else {set r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {T}}

###############################################################################

runTest {test if-9.9 {
  R-06371-03281: large negative bigint is truthy
} -constraints {
  bigint
} -setup {
} -body {
  if {-12345678901234567890123456789012345678} then {set r T} else {set r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {T}}

###############################################################################

runTest {test if-9.10 {
  R-06371-03281: nonzero double is truthy
} -setup {
} -body {
  set r ""
  if {1.0}   then {append r T} else {append r F}
  if {-3.14} then {append r T} else {append r F}
  if {1e10}  then {append r T} else {append r F}
  if {1e-10} then {append r T} else {append r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {TTTT}}

###############################################################################

runTest {test if-9.11 {
  R-06371-03281: zero double is falsy (positive, negative, exponent forms)
} -setup {
} -body {
  set r ""
  if {0.0}  then {append r T} else {append r F}
  if {-0.0} then {append r T} else {append r F}
  if {0e0}  then {append r T} else {append r F}
  if {0e10} then {append r T} else {append r F}
  set r
} -cleanup {
  unset -nocomplain r
} -result {FFFF}}

###############################################################################

runTest {test if-9.12 {
  R-06371-03281: non-numeric, non-keyword bareword is rejected (by the bareword-rejection security envelope before reaching boolean coercion)
} -setup {
} -body {
  list [catch {if {hello} then {set x 1}} msg] [string match {*invalid bareword*} $msg]
} -cleanup {
  unset -nocomplain msg x
} -result {1 1}}

###############################################################################

runTest {test if-9.13 {
  R-06371-03281: empty string is rejected
} -setup {
} -body {
  list [catch {if {""} then {set x 1}} msg] [string match {*expected boolean*} $msg]
} -cleanup {
  unset -nocomplain msg x
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
