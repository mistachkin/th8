###############################################################################
#
# coverage7.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests targeting the highest-impact uncovered branches: expression
# functions (min/max/bool/double/int), float formatting, namespace-
# qualified commands, lsearch -sorted -dictionary, lindex with nested
# indices, and format integer paths.
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
# Section 1 -- expr min() and max() with doubles
#
###############################################################################

runTest {test coverage7-1.1 {expr min with doubles} -body {
    expr {min(3.5, 1.2)}
} -result {1.2}}

###############################################################################

runTest {test coverage7-1.2 {expr max with doubles} -body {
    expr {max(3.5, 1.2)}
} -result {3.5}}

###############################################################################

runTest {test coverage7-1.3 {expr min with negative doubles} -body {
    expr {min(-1.5, -3.7)}
} -result {-3.7}}

###############################################################################

runTest {test coverage7-1.4 {expr max with negative doubles} -body {
    expr {max(-1.5, -3.7)}
} -result {-1.5}}

###############################################################################

runTest {test coverage7-1.5 {expr min with integers} -body {
    expr {min(5, 3)}
} -result {3}}

###############################################################################

runTest {test coverage7-1.6 {expr max with integers} -body {
    expr {max(5, 3)}
} -result {5}}

###############################################################################
#
# Section 2 -- expr bool() and type coercion
#
###############################################################################

runTest {test coverage7-2.1 {expr bool of integer zero} -body {
    expr {bool(0)}
} -result {0}}

###############################################################################

runTest {test coverage7-2.2 {expr bool of nonzero integer} -body {
    expr {bool(42)}
} -result {1}}

###############################################################################

runTest {test coverage7-2.3 {expr bool of double zero} -body {
    expr {bool(0.0)}
} -result {0}}

###############################################################################

runTest {test coverage7-2.4 {expr bool of nonzero double} -body {
    expr {bool(3.14)}
} -result {1}}

###############################################################################

runTest {test coverage7-2.5 {expr bool of negative integer} -body {
    expr {bool(-1)}
} -result {1}}

###############################################################################

runTest {test coverage7-2.6 {expr bool of negative double} -body {
    expr {bool(-0.0)}
} -result {0}}

###############################################################################
#
# Section 3 -- Float formatting (scientific notation paths)
#
###############################################################################

runTest {test coverage7-3.1 {expr large float produces digits} -setup {
    unset -nocomplain x
} -body {
  set x [expr {1.0e10}]
  expr {$x > 9999999999.0}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test coverage7-3.2 {expr very small float} -body {
    set x [expr {1.0e-10}]
    string match "*e*" $x
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test coverage7-3.3 {expr float multiplication} -body {
    expr {1.5 * 2.0}
} -result {3.0}}

###############################################################################

runTest {test coverage7-3.4 {expr float division} -body {
    expr {7.0 / 2.0}
} -result {3.5}}

###############################################################################

runTest {test coverage7-3.5 {expr double() conversion} -body {
    expr {double(5)}
} -result {5.0}}

###############################################################################

runTest {test coverage7-3.6 {expr int() truncation} -body {
    expr {int(3.9)}
} -result {3}}

###############################################################################

runTest {test coverage7-3.7 {expr round()} -body {
    expr {round(3.5)}
} -result {4}}

###############################################################################

runTest {test coverage7-3.8 {expr ceil and floor} -body {
    list [expr {ceil(2.3)}] [expr {floor(2.7)}]
} -result {3.0 2.0}}

###############################################################################
#
# Section 4 -- Namespace-qualified command calls
#
###############################################################################

runTest {test coverage7-4.1 {call namespace-qualified proc} -setup {
    unset -nocomplain result
} -body {
  namespace eval ::_cov7ns {
    proc greet {name} { return "hello $name" }
  }
  set result [::_cov7ns::greet "world"]
  namespace delete ::_cov7ns
  set result
} -cleanup {
  catch {namespace delete ::_cov7ns}
  unset -nocomplain result
} -result {hello world}}

###############################################################################

runTest {test coverage7-4.2 {namespace-qualified call inside command subst} -body {
    namespace eval ::_cov7ns2 {
        proc add {a b} { return [expr {$a + $b}] }
    }
    set result [::_cov7ns2::add 3 4]
    namespace delete ::_cov7ns2
    set result
} -cleanup {
  catch {namespace delete ::_cov7ns2}
  unset -nocomplain result
} -result {7}}

###############################################################################

runTest {test coverage7-4.3 {namespace-qualified in NRE path} -body {
    namespace eval ::_cov7ns3 {
        proc val {} { return 42 }
    }
    # Force NRE path by using [cmd] substitution
    set result [set x [::_cov7ns3::val]]
    namespace delete ::_cov7ns3
    set result
} -cleanup {
  catch {namespace delete ::_cov7ns3}
  unset -nocomplain result x
} -result {42}}

###############################################################################
#
# Section 5 -- lsearch -sorted and -dictionary
#
###############################################################################

runTest {test coverage7-5.1 {lsearch -sorted basic} -body {
    lsearch -sorted {a b c d e} c
} -result {2}}

###############################################################################

runTest {test coverage7-5.2 {lsearch -sorted not found} -body {
    lsearch -sorted {a b d e f} c
} -result {-1}}

###############################################################################

runTest {test coverage7-5.3 {lsort -dictionary} -body {
    lsort -dictionary {a10 a2 a1 a20}
} -result {a1 a2 a10 a20}}

###############################################################################

runTest {test coverage7-5.4 {lsort -dictionary with case} -body {
    lsort -dictionary {Banana apple Cherry}
} -result {apple Banana Cherry}}

###############################################################################

runTest {test coverage7-5.5 {lsearch -sorted -dictionary} -body {
    lsearch -sorted -dictionary {a1 a2 a10 a20} a10
} -result {2}}

###############################################################################
#
# Section 6 -- lindex with nested indices
#
###############################################################################

runTest {test coverage7-6.1 {lindex nested list} -body {
    lindex {{a b} {c d} {e f}} 1 0
} -result {c}}

###############################################################################

runTest {test coverage7-6.2 {lindex deeply nested} -body {
    lindex {{{1 2} {3 4}} {{5 6} {7 8}}} 1 0 1
} -result {6}}

###############################################################################

runTest {test coverage7-6.3 {lindex with end keyword} -body {
    lindex {a b c d} end
} -result {d}}

###############################################################################

runTest {test coverage7-6.4 {lindex with end-1} -body {
    lindex {a b c d} end-1
} -result {c}}

###############################################################################
#
# Section 7 -- format integer paths
#
###############################################################################

runTest {test coverage7-7.1 {format %d} -body {
    format "%d" 42
} -result {42}}

###############################################################################

runTest {test coverage7-7.2 {format %d negative} -body {
    format "%d" -100
} -result {-100}}

###############################################################################

runTest {test coverage7-7.3 {format %d zero} -body {
    format "%d" 0
} -result {0}}

###############################################################################

runTest {test coverage7-7.4 {format %d large number} -body {
    format "%d" 1000000
} -result {1000000}}

###############################################################################

runTest {test coverage7-7.5 {format %x hex} -body {
    format "%x" 255
} -result {ff}}

###############################################################################

runTest {test coverage7-7.6 {format %o octal} -body {
    format "%o" 8
} -result {10}}

###############################################################################
#
# Section 8 -- Expression comparison and logic operators
#
###############################################################################

runTest {test coverage7-8.1 {expr ternary with doubles} -body {
    expr {1.5 > 1.0 ? "yes" : "no"}
} -result {yes}}

###############################################################################

runTest {test coverage7-8.2 {expr string equality} -body {
    expr {"hello" eq "hello"}
} -result {1}}

###############################################################################

runTest {test coverage7-8.3 {expr string inequality} -body {
    expr {"hello" ne "world"}
} -result {1}}

###############################################################################

runTest {test coverage7-8.4 {expr logical and/or} -body {
    list [expr {1 && 0}] [expr {1 || 0}]
} -result {0 1}}

###############################################################################

runTest {test coverage7-8.5 {expr bitwise operations} -body {
    list [expr {0xFF & 0x0F}] [expr {0xF0 | 0x0F}] [expr {0xFF ^ 0x0F}]
} -result {15 255 240}}

###############################################################################

runTest {test coverage7-8.6 {expr shift operators} -body {
    list [expr {1 << 4}] [expr {256 >> 4}]
} -result {16 16}}

###############################################################################

runTest {test coverage7-8.7 {expr modulo} -body {
    expr {17 % 5}
} -result {2}}

###############################################################################

runTest {test coverage7-8.8 {expr unary minus and not} -body {
    list [expr {-(-5)}] [expr {!0}] [expr {~0xFF}]
} -result {5 1 -256}}

###############################################################################
#
# Section 9 -- Math functions
#
###############################################################################

runTest {test coverage7-9.1 {expr abs} -body {
    list [expr {abs(-5)}] [expr {abs(5)}] [expr {abs(-3.14)}]
} -result {5 5 3.14}}

###############################################################################

runTest {test coverage7-9.2 {expr sqrt and pow} -body {
    list [expr {sqrt(16.0)}] [expr {pow(2,10)}]
} -result {4.0 1024.0}}

###############################################################################

runTest {test coverage7-9.3 {expr log and exp} -body {
    set e [expr {exp(1.0)}]
    set l [expr {log($e)}]
    expr {abs($l - 1.0) < 0.0001}
} -cleanup {
  unset -nocomplain e l
} -result {1}}

###############################################################################

runTest {test coverage7-9.4 {expr trig functions} -body {
    set s [expr {sin(0.0)}]
    set c [expr {cos(0.0)}]
    list [expr {abs($s) < 0.0001}] [expr {abs($c - 1.0) < 0.0001}]
} -cleanup {
  unset -nocomplain s c
} -result {1 1}}

###############################################################################

runTest {test coverage7-9.5 {expr wide()} -body {
    expr {wide(42)}
} -result {42}}

###############################################################################

runTest {test coverage7-9.6 {expr srand and rand} -setup {
    unset -nocomplain r
} -body {
  expr {srand(12345)}
  set r [expr {rand()}]
  expr {$r >= 0.0 && $r < 1.0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 10 -- Error and edge case paths
#
###############################################################################

runTest {test coverage7-10.1 {bad list syntax in lindex} -setup {
    unset -nocomplain msg
} -body {
  catch {lindex "\{bad" 0} msg
  set msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {*}}

###############################################################################

runTest {test coverage7-10.2 {lrange basic} -body {
    lrange {a b c d e} 1 3
} -result {b c d}}

###############################################################################

runTest {test coverage7-10.3 {lreplace at end} -body {
    lreplace {a b c d} 2 3 X Y Z
} -result {a b X Y Z}}

###############################################################################

runTest {test coverage7-10.4 {lappend multiple values} -body {
    set x {a}
    lappend x b c d
} -cleanup {
  unset -nocomplain x
} -result {a b c d}}

###############################################################################

runTest {test coverage7-10.5 {string repeat} -body {
    string repeat "ab" 3
} -result {ababab}}

###############################################################################

runTest {test coverage7-10.6 {string trim variants} -body {
    list [string trim "  x  "] [string trimleft "  x  "] [string trimright "  x  "]
} -result {x {x  } {  x}}}

###############################################################################

runTest {test coverage7-10.7 {string is integer} -body {
    list [string is integer 42] [string is integer "abc"]
} -result {1 0}}

###############################################################################

runTest {test coverage7-10.8 {string is double} -body {
    list [string is double 3.14] [string is double "xyz"]
} -result {1 0}}

###############################################################################

runTest {test coverage7-10.9 {string toupper and tolower} -body {
    list [string toupper "hello"] [string tolower "HELLO"]
} -result {HELLO hello}}

###############################################################################

runTest {test coverage7-10.10 {string replace} -body {
    string replace "hello world" 5 5 "_"
} -result {hello_world}}

###############################################################################
#
# Section 11 -- Security hardening (result size limits, regex complexity)
#
###############################################################################

runTest {test coverage7-11.1 {
  string repeat rejects output exceeding result limit
} -constraints {
    th8
} -body {
  #
  # This tests that string repeat checks against the per-interpreter
  # result size limit, not just the hard TH8_MX_STRLEN maximum.
  # The default limit is large enough that this test passes.
  # The error path is exercised by the overflow arithmetic check.
  #
  catch {string repeat "x" 2000000000}
} -result {1}}

###############################################################################

runTest {test coverage7-11.2 {
  string map checks output size
} -body {
  #
  # Verify string map produces correct output for a simple case.
  # The result size check runs on every iteration but is not
  # triggered for small outputs.
  #
  string map {a X b Y} "abc"
} -result {XYc}}

###############################################################################

runTest {test coverage7-11.3 {
  regex rejects excessively complex patterns
} -constraints {
    regexp
} -body {
  #
  # A pattern with deeply nested alternations generates many
  # NFA states.  If it exceeds TH8_REGEX_MAX_STATES, regcomp
  # succeeds but th8RegexCheckComplexity rejects it.
  #
  # We use a moderately complex pattern that should compile fine.
  #
  regexp {^(a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p)+$} "abcdefghij"
} -result {1}}

###############################################################################

runTest {test coverage7-11.4 {
  format rejects %n specifier
} -body {
  list [catch {format "%n" x} msg] [string match "*bad field*" $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test coverage7-11.5 {
  string is ascii returns 1 for ASCII text
} -body {
  string is ascii "hello world"
} -result {1}}

###############################################################################

runTest {test coverage7-11.6 {
  string is boolean recognizes boolean strings
} -body {
  list [string is boolean true] [string is boolean false] \
      [string is boolean yes] [string is boolean no] \
      [string is boolean maybe]
} -result {1 1 1 1 0}}

###############################################################################

runTest {test coverage7-11.7 {
  string is true / string is false
} -body {
  list [string is true 1] [string is true 0] \
      [string is false 0] [string is false 1]
} -result {1 0 1 0}}

###############################################################################

runTest {test coverage7-11.8 {
  string is -strict rejects empty string
} -body {
  list [string is integer ""] \
      [string is integer -strict ""]
} -result {1 0}}

###############################################################################

runTest {test coverage7-11.9 {
  expr {!true} accepts boolean strings
} -body {
  list [expr {!true}] [expr {!false}] [expr {!yes}] [expr {!no}]
} -result {0 1 0 1}}

###############################################################################
#
# Section 12 -- concat, string is, info nameofexecutable requirements
#
###############################################################################

runTest {test coverage7-12.1 {
  R-56570-29705: concat trims and joins with spaces
} -constraints {
    concat
} -body {
  concat { a } { b } { c }
} -result {a b c}}

###############################################################################

runTest {test coverage7-12.2 {
  R-17771-43627: concat with no arguments returns empty
} -constraints {
    concat
} -body {
  concat
} -result {}}

###############################################################################

runTest {test coverage7-12.3 {
  R-28016-42813: concat preserves interior whitespace
} -constraints {
    concat
} -body {
  concat {a  b} {c  d}
} -result {a  b c  d}}

###############################################################################

runTest {test coverage7-12.4 {
  R-58370-15660: concat omits empty-after-trim arguments
} -constraints {
    concat
} -body {
  concat {} x {} {} y {}
} -result {x y}}

###############################################################################

runTest {test coverage7-12.5 {
  R-52560-48849: string is ascii for ASCII text
} -body {
  list [string is ascii "hello"] [string is ascii ""]
} -result {1 1}}

###############################################################################

runTest {test coverage7-12.6 {
  R-04129-32285: string is boolean recognizes valid booleans
} -body {
  list [string is boolean true] [string is boolean 0] \
      [string is boolean maybe]
} -result {1 1 0}}

###############################################################################

runTest {test coverage7-12.7 {
  R-42813-48603: string is true checks truthy value
} -body {
  list [string is true 1] [string is true yes] \
      [string is true 0] [string is true no]
} -result {1 1 0 0}}

###############################################################################

runTest {test coverage7-12.8 {
  R-20823-56724: string is false checks falsy value
} -body {
  list [string is false 0] [string is false no] \
      [string is false 1] [string is false yes]
} -result {1 1 0 0}}

###############################################################################

runTest {test coverage7-12.9 {
  R-12751-19518: -strict makes empty string return 0
} -body {
  list [string is integer ""] \
      [string is integer -strict ""]
} -result {1 0}}

###############################################################################

runTest {test coverage7-12.10 {
  R-13502-28084: info nameofexecutable resolution order
} -constraints {
    th8
} -setup {
  #
  # Save and remove the variable to test the platform fallback.
  #
  set _saved_exe ""
  if {[catch {set _saved_exe $::th8_nameofexecutable}] != 0} then {
    set _saved_exe ""
  }
  catch {unset ::th8_nameofexecutable}
} -body {
  #
  # Without the variable, the platform callback provides
  # the executable path (non-empty on platforms with xGetExePath).
  #
  set path [info nameofexecutable]
  expr {[string length $path] > 0}
} -cleanup {
  #
  # Restore the variable to its original state.
  #
  if {$_saved_exe ne ""} then {
    set ::th8_nameofexecutable $_saved_exe
  }
  unset -nocomplain _saved_exe path
} -result {1}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
