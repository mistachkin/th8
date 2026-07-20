###############################################################################
#
# primes.tcl --
#
# TH8 Example Scripts
# Pure-computation example: prime testing and the Sieve of Eratosthenes.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # isPrime --
  #
  #   Returns 1 when N is prime, 0 otherwise.  Uses 6k+/-1 trial
  #   division: after ruling out 2 and 3, only candidate divisors
  #   of the form 6k-1 and 6k+1 up to sqrt(N) are tested.  O(sqrt N).
  #
  proc isPrime { n } {
    if {$n < 2} then {
      return 0
    }

    if {$n < 4} then {
      return 1
    }

    if {$n % 2 == 0 || $n % 3 == 0} then {
      return 0
    }

    for {set i 5} {$i * $i <= $n} {incr i 6} {
      if {$n % $i == 0 || $n % ($i + 2) == 0} then {
        return 0
      }
    }

    return 1
  }

  #
  # sieve --
  #
  #   Returns the list of all primes less than or equal to LIMIT,
  #   computed with the Sieve of Eratosthenes: start with every
  #   number marked prime, then walk the multiples of each prime
  #   and mark them composite.  Runs in O(n log log n).
  #
  proc sieve { limit } {
    if {$limit < 2} then {
      return [list]
    }

    for {set i 0} {$i <= $limit} {incr i} {
      set mark($i) 1
    }

    for {set i 2} {$i * $i <= $limit} {incr i} {
      if {$mark($i)} then {
        for {set j [expr {$i * $i}]} {$j <= $limit} {incr j $i} {
          set mark($j) 0
        }
      }
    }

    set result [list]

    for {set i 2} {$i <= $limit} {incr i} {
      if {$mark($i)} then {
        lappend result $i
      }
    }

    return $result
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "primes <= 30    = [::examples::sieve 30]"
puts "isPrime 97      = [::examples::isPrime 97]"
puts "isPrime 100     = [::examples::isPrime 100]"
puts "count <= 100    = [llength [::examples::sieve 100]]"
