###############################################################################
#
# fibonacci.tcl --
#
# TH8 Example Scripts
# Pure-computation example: exact Fibonacci numbers (iterative).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # fib --
  #
  #   Returns the Nth Fibonacci number (0-based: fib 0 -> 0, fib 1
  #   -> 1) computed iteratively in O(n) additions.  TH8 integers
  #   are arbitrary-precision, so the result is exact for any N;
  #   no overflow, no floating-point rounding.  Errors when N is
  #   negative.
  #
  proc fib { n } {
    if {$n < 0} then {
      error "n must be non-negative"
    }

    set a 0
    set b 1

    for {set i 0} {$i < $n} {incr i} {
      set next [expr {$a + $b}]
      set a $b
      set b $next
    }

    return $a
  }

  #
  # fibSequence --
  #
  #   Returns the first N Fibonacci numbers (F(0)..F(N-1)) as a
  #   list.  Computed in a single O(n) pass rather than by calling
  #   [fib] N times, so it is linear overall.
  #
  proc fibSequence { n } {
    set result [list]

    set a 0
    set b 1

    for {set i 0} {$i < $n} {incr i} {
      lappend result $a

      set next [expr {$a + $b}]
      set a $b
      set b $next
    }

    return $result
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.  This is
#       the script's only output (pure computation otherwise).
#
puts "fib 10          = [::examples::fib 10]"
puts "fib 100         = [::examples::fib 100]"
puts "first 10 fibs   = [::examples::fibSequence 10]"
