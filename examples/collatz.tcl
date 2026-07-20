###############################################################################
#
# collatz.tcl --
#
# TH8 Example Scripts
# Pure-computation example: the Collatz (3n+1) sequence.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # collatzSequence --
  #
  #   Returns the Collatz sequence starting at N: repeatedly halve
  #   even numbers and map odd numbers to 3n+1, stopping at 1.
  #   The returned list begins with N and ends with 1.  Errors
  #   when N is not a positive integer.
  #
  proc collatzSequence { n } {
    if {$n < 1} then {
      error "n must be a positive integer"
    }

    set result [list $n]

    while {$n != 1} {
      if {$n % 2 == 0} then {
        set n [expr {$n / 2}]
      } else {
        set n [expr {3 * $n + 1}]
      }

      lappend result $n
    }

    return $result
  }

  #
  # collatzSteps --
  #
  #   Returns the number of steps needed to reach 1 from N (the
  #   "total stopping time").  This is one less than the length of
  #   the full sequence; collatzSteps 1 is 0.
  #
  proc collatzSteps { n } {
    return [expr {[llength [collatzSequence $n]] - 1}]
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "collatz 6       = [::examples::collatzSequence 6]"
puts "steps 27        = [::examples::collatzSteps 27]"
puts "steps 97        = [::examples::collatzSteps 97]"
