###############################################################################
#
# ackermann.tcl --
#
# TH8 Example Scripts
# Pure-computation example: the Ackermann-Peter recursive function.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # ackermann --
  #
  #   Returns the Ackermann-Peter function A(m, n), the classic
  #   example of a total computable function that is not primitive
  #   recursive.  Defined by:
  #
  #     A(0, n) = n + 1
  #     A(m, 0) = A(m - 1, 1)                   when m > 0
  #     A(m, n) = A(m - 1, A(m, n - 1))         when m > 0, n > 0
  #
  #   Grows explosively: keep M small (0..3).  A(4, n) recurses far
  #   too deep to evaluate directly and is intentionally not used
  #   in the demonstration below.
  #
  proc ackermann { m n } {
    if {$m == 0} then {
      return [expr {$n + 1}]
    }

    if {$n == 0} then {
      return [ackermann [expr {$m - 1}] 1]
    }

    return [ackermann [expr {$m - 1}] [ackermann $m [expr {$n - 1}]]]
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "A(2, 3)         = [::examples::ackermann 2 3]"
puts "A(3, 3)         = [::examples::ackermann 3 3]"
puts "A(3, 5)         = [::examples::ackermann 3 5]"
