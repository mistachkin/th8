###############################################################################
#
# gcdlcm.tcl --
#
# TH8 Example Scripts
# Pure-computation example: greatest common divisor and least common multiple.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # gcd --
  #
  #   Returns the greatest common divisor of A and B using the
  #   Euclidean algorithm (repeated remainder).  The result is
  #   always non-negative; gcd of 0 and 0 is 0.
  #
  proc gcd { a b } {
    set a [expr {abs($a)}]
    set b [expr {abs($b)}]

    while {$b != 0} {
      set t $b
      set b [expr {$a % $b}]
      set a $t
    }

    return $a
  }

  #
  # lcm --
  #
  #   Returns the least common multiple of A and B, derived from
  #   the identity lcm(a,b) = |a*b| / gcd(a,b).  Dividing before
  #   multiplying keeps the intermediate value small.  lcm with a
  #   zero operand is 0.
  #
  proc lcm { a b } {
    if {$a == 0 || $b == 0} then {
      return 0
    }

    set g [gcd $a $b]

    return [expr {abs($a / $g * $b)}]
  }

  #
  # gcdList --
  #
  #   Returns the greatest common divisor of a whole list of
  #   integers by folding [gcd] across them left to right.
  #
  proc gcdList { values } {
    set result 0

    foreach value $values {
      set result [gcd $result $value]
    }

    return $result
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "gcd 48 36       = [::examples::gcd 48 36]"
puts "lcm 4 6         = [::examples::lcm 4 6]"
puts "gcd 1071 462    = [::examples::gcd 1071 462]"
puts "gcdList list    = [::examples::gcdList {24 36 48 60}]"
