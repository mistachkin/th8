###############################################################################
#
# factorial.tcl --
#
# TH8 Example Scripts
# Pure-computation example: factorial and binomial coefficients.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # factorial --
  #
  #   Returns N! (the product 1*2*...*N), with 0! defined as 1.
  #   Iterative and exact for any non-negative N thanks to TH8's
  #   arbitrary-precision integers.  Errors when N is negative.
  #
  proc factorial { n } {
    if {$n < 0} then {
      error "n must be non-negative"
    }

    set result 1

    for {set i 2} {$i <= $n} {incr i} {
      set result [expr {$result * $i}]
    }

    return $result
  }

  #
  # choose --
  #
  #   Returns the binomial coefficient "N choose K" -- the number
  #   of K-element subsets of an N-element set.  Computed with a
  #   multiplicative loop (no full factorials) so intermediate
  #   values stay small; exact for any valid N >= K >= 0.
  #
  proc choose { n k } {
    if {$k < 0 || $k > $n} then {
      return 0
    }

    if {$k > [expr {$n - $k}]} then {
      set k [expr {$n - $k}]
    }

    set result 1

    for {set i 0} {$i < $k} {incr i} {
      set result [expr {$result * ($n - $i) / ($i + 1)}]
    }

    return $result
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "20!             = [::examples::factorial 20]"
puts "30!             = [::examples::factorial 30]"
puts "52 choose 5     = [::examples::choose 52 5]"
puts "10 choose 3     = [::examples::choose 10 3]"
