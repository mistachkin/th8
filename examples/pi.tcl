###############################################################################
#
# pi.tcl --
#
# TH8 Example Scripts
# Pure-computation example: digits of pi via Machin's formula.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # arctanTerm --
  #
  #   Returns the scaled integer value of arctan(1/x), that is
  #   floor(arctan(1/x) * 10^DIGITS), summing the Gregory series
  #     arctan(1/x) = 1/x - 1/(3 x^3) + 1/(5 x^5) - ...
  #   entirely in arbitrary-precision integers.  Each term is the
  #   previous power divided down by x*x; iteration stops once the
  #   term underflows the requested precision.  Helper for [pi].
  #
  proc arctanTerm { x digits } {
    set scale [expr {10 ** ($digits + 10)}]
    set power [expr {$scale / $x}]
    set xx [expr {$x * $x}]

    set total $power
    set k 1
    set sign -1

    while {$power != 0} {
      set power [expr {$power / $xx}]
      set term [expr {$power / (2 * $k + 1)}]
      set total [expr {$total + $sign * $term}]

      set sign [expr {-$sign}]
      incr k
    }

    return $total
  }

  #
  # pi --
  #
  #   Returns pi as the string "3." followed by DIGITS correct
  #   decimal digits, computed with Machin's 1706 identity
  #     pi = 16 * arctan(1/5) - 4 * arctan(1/239)
  #   in scaled integer arithmetic (10 guard digits absorb rounding
  #   in the final digit).  Pure integer math -- no floating point.
  #
  proc pi { digits } {
    if {$digits < 1} then {
      error "digits must be positive"
    }

    set scaled [expr {16 * [arctanTerm 5 $digits] - \
        4 * [arctanTerm 239 $digits]}]

    set scaled [expr {$scaled / (10 ** 10)}]

    set text [format %s $scaled]

    return "[string index $text 0].[string range $text 1 end]"
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "pi (10 digits)  = [::examples::pi 10]"
puts "pi (30 digits)  = [::examples::pi 30]"
puts "pi (50 digits)  = [::examples::pi 50]"
