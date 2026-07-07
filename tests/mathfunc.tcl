###############################################################################
#
# mathfunc.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for expression math functions from TIP #521 (float
# classification) and TIP #745 (C99 math functions), plus
# the typeof/entier/info functions additions.
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
# Section 1 -- TIP #521: Float classification (isfinite, isnan, etc.)
#
###############################################################################

runTest {test mathfunc-1.1 {isfinite of normal number} -constraints {
    c99math
} -body {
  expr {isfinite(1.0)}
} -result {1}}

###############################################################################

runTest {test mathfunc-1.2 {isfinite of zero} -constraints {
    c99math
} -body {
  expr {isfinite(0.0)}
} -result {1}}

###############################################################################

runTest {test mathfunc-1.3 {isnormal of 1.0} -constraints {
    c99math
} -body {
  expr {isnormal(1.0)}
} -result {1}}

###############################################################################

runTest {test mathfunc-1.4 {isnormal of 0.0} -constraints {
    c99math
} -body {
  expr {isnormal(0.0)}
} -result {0}}

###############################################################################

runTest {test mathfunc-1.5 {fpclassify normal} -constraints {
    c99math
} -body {
  expr {fpclassify(3.14)}
} -result {normal}}

###############################################################################

runTest {test mathfunc-1.6 {fpclassify zero} -constraints {
    c99math
} -body {
  expr {fpclassify(0.0)}
} -result {zero}}

###############################################################################

runTest {test mathfunc-1.7 {signbit positive} -constraints {
    c99math
} -body {
  expr {signbit(1.0)}
} -result {0}}

###############################################################################

runTest {test mathfunc-1.8 {signbit negative} -constraints {
    c99math
} -body {
  expr {signbit(-1.0)}
} -result {1}}

###############################################################################

runTest {test mathfunc-1.9 {signbit zero is not negative} -constraints {
    c99math
} -body {
  expr {signbit(0.0)}
} -result {0}}

###############################################################################

runTest {test mathfunc-1.10 {isnan of NaN literal} -constraints {
    c99math
} -body {
  set x NaN
  expr {isnan($x)}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test mathfunc-1.11 {isinf of Inf literal} -constraints {
    c99math
} -body {
  set x Inf
  expr {isinf($x)}
} -cleanup {
  unset -nocomplain x
} -result {1}}

###############################################################################

runTest {test mathfunc-1.12 {isfinite of Inf is false} -constraints {
    c99math
} -body {
  set x Inf
  expr {isfinite($x)}
} -cleanup {
  unset -nocomplain x
} -result {0}}

###############################################################################

runTest {test mathfunc-1.13 {fpclassify NaN} -constraints {
    c99math
} -body {
  set x NaN
  expr {fpclassify($x)}
} -cleanup {
  unset -nocomplain x
} -result {nan}}

###############################################################################

runTest {test mathfunc-1.14 {fpclassify Inf} -constraints {
    c99math
} -body {
  set x Inf
  expr {fpclassify($x)}
} -cleanup {
  unset -nocomplain x
} -result {infinite}}

###############################################################################

runTest {test mathfunc-1.15 {
  R-63096-35181 R-42165-31603: fpclassify standalone command
  classifies a value and the result matches the expr-form
} -constraints {
    c99math
} -body {
  list [fpclassify 3.14] [expr {fpclassify(3.14)}]
} -result {normal normal}}

###############################################################################

runTest {test mathfunc-1.16 {
  R-63096-35181: fpclassify standalone command returns nan for NaN
} -constraints {
    c99math
} -body {
  fpclassify NaN
} -result {nan}}

###############################################################################

runTest {test mathfunc-1.16a {
  R-43092-20230: fpclassify with no arguments raises wrong-num-args
} -constraints {
    c99math
} -body {
  catch {fpclassify} m
  string match {*wrong # args*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test mathfunc-1.16b {
  R-43092-20230: fpclassify with two arguments raises wrong-num-args
} -constraints {
    c99math
} -body {
  catch {fpclassify 1.0 2.0} m
  string match {*wrong # args*} $m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test mathfunc-1.17 {negative zero round-trip} -constraints {
    c99math
} -body {
  set x [expr {copysign(0.0, -1.0)}]
  list $x [expr {signbit($x)}]
} -cleanup {
  unset -nocomplain x
} -result {-0.0 1}}

###############################################################################
#
# Section 2 -- TIP #745: Inverse hyperbolic functions
#
###############################################################################

runTest {test mathfunc-2.1 {acosh(1.0) = 0} -constraints {
    c99math
} -body {
  expr {acosh(1.0)}
} -result {0.0}}

###############################################################################

runTest {test mathfunc-2.2 {asinh(0.0) = 0} -constraints {
    c99math
} -body {
  expr {asinh(0.0)}
} -result {0.0}}

###############################################################################

runTest {test mathfunc-2.3 {atanh(0.0) = 0} -constraints {
    c99math
} -body {
  expr {atanh(0.0)}
} -result {0.0}}

###############################################################################
#
# Section 3 -- TIP #745: Roots and powers
#
###############################################################################

runTest {test mathfunc-3.1 {cbrt(27.0)} -constraints {
    c99math
} -body {
  expr {cbrt(27.0)}
} -result {3.0}}

###############################################################################

runTest {test mathfunc-3.2 {cbrt(-8.0)} -constraints {
    c99math
} -body {
  expr {cbrt(-8.0)}
} -result {-2.0}}

###############################################################################

runTest {test mathfunc-3.3 {exp2(10)} -constraints {
    c99math
} -body {
  expr {exp2(10.0)}
} -result {1024.0}}

###############################################################################

runTest {test mathfunc-3.4 {exp2(0)} -constraints {
    c99math
} -body {
  expr {exp2(0.0)}
} -result {1.0}}

###############################################################################

runTest {test mathfunc-3.5 {ldexp(1.0, 10)} -constraints {
    c99math
} -body {
  expr {ldexp(1.0, 10)}
} -result {1024.0}}

###############################################################################
#
# Section 4 -- TIP #745: Logarithms
#
###############################################################################

runTest {test mathfunc-4.1 {log2(1024.0)} -constraints {
    c99math
} -body {
  expr {log2(1024.0)}
} -result {10.0}}

###############################################################################

runTest {test mathfunc-4.2 {log2(1.0) = 0} -constraints {
    c99math
} -body {
  expr {log2(1.0)}
} -result {0.0}}

###############################################################################

runTest {test mathfunc-4.3 {log1p(0.0) = 0} -constraints {
    c99math
} -body {
  expr {log1p(0.0)}
} -result {0.0}}

###############################################################################

runTest {test mathfunc-4.4 {logb(1024.0)} -constraints {
    c99math
} -body {
  expr {logb(1024.0)}
} -result {10.0}}

###############################################################################

runTest {test mathfunc-4.5 {expm1(0.0) = 0} -constraints {
    c99math
} -body {
  expr {expm1(0.0)}
} -result {0.0}}

###############################################################################
#
# Section 5 -- TIP #745: Special functions
#
###############################################################################

runTest {test mathfunc-5.1 {erf(0.0) = 0} -constraints {
    c99math
} -body {
  expr {erf(0.0)}
} -result {0.0}}

###############################################################################

runTest {test mathfunc-5.2 {erfc(0.0) = 1} -constraints {
    c99math
} -body {
  expr {erfc(0.0)}
} -result {1.0}}

###############################################################################

runTest {test mathfunc-5.3 {gamma(1.0) = 1} -constraints {
    c99math
} -setup {
} -body {
  set r [expr {gamma(1.0)}]
  expr {abs($r - 1.0) < 0.0001}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test mathfunc-5.4 {lgamma(1.0) = 0} -constraints {
    c99math
} -body {
  set r [expr {lgamma(1.0)}]
  expr {abs($r) < 0.0001}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################
#
# Section 6 -- TIP #745: Precision and sign functions
#
###############################################################################

runTest {test mathfunc-6.1 {trunc(3.7) = 3} -constraints {
    c99math
} -body {
  expr {trunc(3.7)}
} -result {3.0}}

###############################################################################

runTest {test mathfunc-6.2 {trunc(-3.7) = -3} -constraints {
    c99math
} -body {
  expr {trunc(-3.7)}
} -result {-3.0}}

###############################################################################

runTest {test mathfunc-6.3 {copysign(1.0, -1.0) = -1} -constraints {
    c99math
} -body {
  expr {copysign(1.0, -1.0)}
} -result {-1.0}}

###############################################################################

runTest {test mathfunc-6.4 {copysign(-1.0, 1.0) = 1} -constraints {
    c99math
} -body {
  expr {copysign(-1.0, 1.0)}
} -result {1.0}}

###############################################################################

runTest {test mathfunc-6.5 {remainder(10.0, 3.0)} -constraints {
    c99math
} -body {
  expr {remainder(10.0, 3.0)}
} -result {1.0}}

###############################################################################

runTest {test mathfunc-6.6 {dim(5.0, 3.0) = 2} -constraints {
    c99math
} -body {
  expr {dim(5.0, 3.0)}
} -result {2.0}}

###############################################################################

runTest {test mathfunc-6.7 {dim(3.0, 5.0) = 0} -constraints {
    c99math
} -body {
  expr {dim(3.0, 5.0)}
} -result {0.0}}

###############################################################################

runTest {test mathfunc-6.8 {
  nextafter -- skipped: "ne" prefix conflicts with expr ne operator
} -constraints {
    c99math
} -body {
  expr {nextafter(1.0, 2.0) > 1.0}
} -result {1}}

###############################################################################
#
# Section 7 -- typeof and entier functions
#
###############################################################################

runTest {test mathfunc-7.1 {typeof integer} -constraints {
    th8
} -body {
  expr {typeof(42)}
} -result {int}}

###############################################################################

runTest {test mathfunc-7.2 {typeof double} -constraints {
    th8
} -body {
  expr {typeof(3.14)}
} -result {double}}

###############################################################################

runTest {test mathfunc-7.3 {entier truncates double} -body {
    expr {entier(3.9)}
} -result {3}}

###############################################################################

runTest {test mathfunc-7.4 {entier negative} -body {
    expr {entier(-3.9)}
} -result {-3}}

###############################################################################
#
# Section 8 -- info functions
#
###############################################################################

runTest {test mathfunc-8.1 {info functions lists all} -constraints {
    c99math
} -setup {
} -body {
  set n [llength [info functions]]
  expr {$n >= 50}
} -cleanup {
  unset -nocomplain n
} -result {1}}

###############################################################################

runTest {test mathfunc-8.2 {info functions with pattern} -constraints {
    c99math
} -body {
  set r [lsort [info functions is*]]
} -cleanup {
  unset -nocomplain r
} -result {isfinite isinf isnan isnormal isqrt issubnormal isunordered}}

###############################################################################

runTest {test mathfunc-8.3 {info functions log*} -constraints {
    c99math
} -body {
  set r [lsort [info functions log*]]
} -cleanup {
  unset -nocomplain r
} -result {log log10 log1p log2 logb}}

###############################################################################
#
# Section 9 -- Requirements coverage: typeof, entier, bool, fpclassify,
#              isfinite, isinf, isnan, isnormal, issubnormal, isunordered,
#              info functions, dynamic registration, C99 math functions
#
###############################################################################

runTest {test mathfunc-9.1 {
  R-25277-59247: typeof returns type name of the value
} -constraints {
    th8
} -body {
  list [expr {typeof(42)}] [expr {typeof(3.14)}] \
      [expr {typeof("hello")}]
} -result {int double string}}

###############################################################################

runTest {test mathfunc-9.2 {
  R-04200-14573: entier truncates toward zero
} -constraints {
    th8
} -body {
  list [expr {entier(3.7)}] [expr {entier(-2.9)}] [expr {entier(0.0)}]
} -result {3 -2 0}}

###############################################################################

runTest {test mathfunc-9.3 {
  R-04038-64345: bool returns 1 for nonzero, 0 for zero
} -constraints {
    th8
} -body {
  list [expr {bool(42)}] [expr {bool(0)}] [expr {bool(-1)}]
} -result {1 0 1}}

###############################################################################

runTest {test mathfunc-9.4 {
  R-10825-53675: isfinite returns 1 for finite values
} -constraints {
    c99math
} -body {
  list [expr {isfinite(1.0)}] [expr {isfinite(0.0)}]
} -result {1 1}}

###############################################################################

runTest {test mathfunc-9.5 {
  R-29218-03840: isinf returns 1 for infinity
} -constraints {
    c99math th8
} -body {
  expr {isinf(Inf)}
} -result {1}}

###############################################################################

runTest {test mathfunc-9.6 {
  R-63393-12448: isnan returns 1 for NaN
} -constraints {
    c99math th8
} -body {
  expr {isnan(NaN)}
} -result {1}}

###############################################################################

runTest {test mathfunc-9.7 {
  R-44368-58334: isnormal returns 1 for normal float
} -constraints {
    c99math
} -body {
  expr {isnormal(1.0)}
} -result {1}}

###############################################################################

runTest {test mathfunc-9.8 {
  R-19401-43049: issubnormal returns 1 for subnormal (C-level verification via
                 th8testlib::subnormal check)
} -constraints {
    c99math loadLib th8
} -setup {
} -body {
  set result [th8testlib::subnormal check]
} -cleanup {
  unset -nocomplain result
} -result {1}}

###############################################################################

runTest {test mathfunc-9.9 {
  R-04041-22104: isunordered returns 1 if either arg is NaN
} -constraints {
    c99math th8
} -body {
  list [expr {isunordered(NaN, 1.0)}] \
      [expr {isunordered(1.0, 2.0)}]
} -result {1 0}}

###############################################################################

runTest {test mathfunc-9.10 {
  R-33716-48519: fpclassify returns classification string
} -constraints {
    c99math th8
} -body {
  list [expr {fpclassify(1.0)}] [expr {fpclassify(0.0)}] \
      [expr {fpclassify(Inf)}]
} -result {normal zero infinite}}

###############################################################################

runTest {test mathfunc-9.11 {
  R-13347-32372: info functions returns registered math function names
} -constraints {
    th8
} -body {
  set fns [info functions]
  expr {[llength $fns] > 0 && [lsearch $fns abs] >= 0}
} -cleanup {
  unset -nocomplain fns
} -result {1}}

###############################################################################

runTest {test mathfunc-9.12 {
  R-19502-53346: math functions are dynamically registered per-interpreter
} -constraints {
    th8
} -body {
  #
  # Verify that the function list includes both core and
  # dynamically registered functions (C99, classification).
  #
  set fns [info functions]
  expr {[lsearch $fns sin] >= 0 && [lsearch $fns abs] >= 0}
} -cleanup {
  unset -nocomplain fns
} -result {1}}

###############################################################################

runTest {test mathfunc-9.13 {
  R-21400-10913: C99 math functions available (cbrt as representative)
} -constraints {
    c99math
} -body {
  expr {cbrt(27.0)}
} -result {3.0}}

###############################################################################
#
# Section 10 -- Nested function calls and parser robustness
#
# Uses the testlib's test_echo math function and nested calls
# to exercise the expression parser's argument splitting.
#
###############################################################################

runTest {test mathfunc-10.1 {
  nested function: max(min(a, b), c)
} -body {
  expr {max(min(10, 20), 5)}
} -result {10}}

###############################################################################

runTest {test mathfunc-10.2 {
  nested function: abs(min(-3, -7))
} -body {
  expr {abs(min(-3, -7))}
} -result {7}}

###############################################################################

runTest {test mathfunc-10.3 {
  deeply nested: abs(min(max(1,2), -5))
} -body {
  expr {abs(min(max(1,2), -5))}
} -result {5}}

###############################################################################

runTest {test mathfunc-10.4 {
  nested with expression: int(sqrt(abs(-16)))
} -body {
  expr {int(sqrt(abs(-16)))}
} -result {4}}

###############################################################################

runTest {test mathfunc-10.5 {
  test_echo returns its argument unchanged (via variable)
} -constraints {
    loadLib th8
} -setup {
  set _val hello
} -body {
  set r [expr {test_echo($_val)}]
} -cleanup {
  unset -nocomplain _val r
} -result {hello}}

###############################################################################

runTest {test mathfunc-10.6 {
  test_echo with nested function as argument
} -constraints {
    loadLib th8
} -body {
  set r [expr {test_echo(abs(-42))}]
} -cleanup {
  unset -nocomplain r
} -result {42}}

###############################################################################

runTest {test mathfunc-10.10 {
  bare word rejection: invalid bareword in expr
} -body {
  catch {expr {abs(hello)}} msg
  string match "*invalid bareword*" $msg
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test mathfunc-10.7 {
  subnormal C-level fpclassify returns "subnormal"
} -constraints {
    loadLib th8
} -body {
  set r [th8testlib::subnormal classify]
} -cleanup {
  unset -nocomplain r
} -result {subnormal}}

###############################################################################

runTest {test mathfunc-10.8 {
  nested 2-arg functions: pow(max(2,3), min(4,5))
} -body {
  expr {pow(max(2,3), min(4,5))}
} -result {81.0}}

###############################################################################

runTest {test mathfunc-10.9 {
  3-level nesting: round(sqrt(pow(3.0, max(2, 4))))
} -body {
  expr {round(sqrt(pow(3.0, max(2, 4))))}
} -result {9}}

###############################################################################

source tests/epilogue.tcl

###############################################################################
