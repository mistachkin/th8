###############################################################################
#
# sqrt.tcl --
#
# TH8 Example Scripts
# Pure-computation example: square roots by Newton's method.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # isqrt --
  #
  #   Returns the integer square root of N -- the largest integer R
  #   with R*R <= N -- using Newton's method on integers.  Because
  #   TH8 integers are arbitrary-precision, this is exact for
  #   arbitrarily large N.  Errors when N is negative.
  #
  proc isqrt { n } {
    if {$n < 0} then {
      error "n must be non-negative"
    }

    if {$n < 2} then {
      return $n
    }

    set x $n
    set y [expr {($x + 1) / 2}]

    while {$y < $x} {
      set x $y
      set y [expr {($x + $n / $x) / 2}]
    }

    return $x
  }

  #
  # newtonSqrt --
  #
  #   Returns a floating-point approximation of the square root of
  #   the positive real X, iterating Newton's update
  #   g <- (g + x/g) / 2 until it converges within a small
  #   tolerance.  Errors when X is negative.
  #
  proc newtonSqrt { x } {
    if {$x < 0} then {
      error "x must be non-negative"
    }

    if {$x == 0} then {
      return 0.0
    }

    set guess [expr {double($x)}]

    while {abs($guess * $guess - $x) > 1.0e-12} {
      set guess [expr {($guess + $x / $guess) / 2.0}]
    }

    return $guess
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "isqrt 1000000   = [::examples::isqrt 1000000]"
puts "isqrt 2         = [::examples::isqrt 2]"
puts "isqrt bignum    = [::examples::isqrt 12345678987654321]"
puts "newtonSqrt 2.0  = [::examples::newtonSqrt 2.0]"
