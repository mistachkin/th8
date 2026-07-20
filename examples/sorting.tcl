###############################################################################
#
# sorting.tcl --
#
# TH8 Example Scripts
# Pure-computation example: quicksort and mergesort from scratch.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # quicksort --
  #
  #   Returns the numbers in VALUES sorted ascending, using a
  #   recursive Lomuto-free partition around the first element as
  #   pivot: elements smaller than the pivot are sorted to its
  #   left, the rest to its right.  Illustrative -- for production
  #   code prefer the built-in [lsort -integer].
  #
  proc quicksort { values } {
    if {[llength $values] <= 1} then {
      return $values
    }

    set pivot [lindex $values 0]
    set less [list]
    set more [list]

    foreach value [lrange $values 1 end] {
      if {$value < $pivot} then {
        lappend less $value
      } else {
        lappend more $value
      }
    }

    return [concat [quicksort $less] [list $pivot] [quicksort $more]]
  }

  #
  # merge --
  #
  #   Returns the ordered merge of two already-sorted numeric lists
  #   LEFT and RIGHT: repeatedly takes the smaller of the two front
  #   elements.  Helper for [mergesort].
  #
  proc merge { left right } {
    set result [list]
    set i 0
    set j 0

    set nLeft [llength $left]
    set nRight [llength $right]

    while {$i < $nLeft && $j < $nRight} {
      if {[lindex $left $i] <= [lindex $right $j]} then {
        lappend result [lindex $left $i]
        incr i
      } else {
        lappend result [lindex $right $j]
        incr j
      }
    }

    return [concat $result [lrange $left $i end] [lrange $right $j end]]
  }

  #
  # mergesort --
  #
  #   Returns the numbers in VALUES sorted ascending via classic
  #   top-down mergesort: split in half, sort each half, then
  #   [merge] the two sorted halves.  Stable and O(n log n).
  #
  proc mergesort { values } {
    set n [llength $values]

    if {$n <= 1} then {
      return $values
    }

    set mid [expr {$n / 2}]
    set left [mergesort [lrange $values 0 [expr {$mid - 1}]]]
    set right [mergesort [lrange $values $mid end]]

    return [merge $left $right]
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
set data {5 2 9 1 5 6 3 8 7 4}

puts "input           = $data"
puts "quicksort       = [::examples::quicksort $data]"
puts "mergesort       = [::examples::mergesort $data]"
