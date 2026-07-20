###############################################################################
#
# pascal.tcl --
#
# TH8 Example Scripts
# Pure-computation example: Pascal's triangle.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # pascalRow --
  #
  #   Returns row N of Pascal's triangle (0-based) as a list of
  #   binomial coefficients.  Row 0 is {1}.  Each row is built from
  #   the previous one by summing adjacent entries, so no factorials
  #   are needed and the values stay exact for any N.
  #
  proc pascalRow { n } {
    if {$n < 0} then {
      error "n must be non-negative"
    }

    set row [list 1]

    for {set i 0} {$i < $n} {incr i} {
      set next [list 1]

      for {set j 0} {$j < [expr {[llength $row] - 1}]} {incr j} {
        lappend next [expr {[lindex $row $j] + [lindex $row [expr {$j + 1}]]}]
      }

      lappend next 1
      set row $next
    }

    return $row
  }

  #
  # pascalTriangle --
  #
  #   Returns the first N rows of Pascal's triangle as a list of
  #   rows (each row itself a list).  Builds each row incrementally
  #   from its predecessor in a single pass.
  #
  proc pascalTriangle { n } {
    set result [list]
    set row [list 1]

    for {set i 0} {$i < $n} {incr i} {
      lappend result $row

      set next [list 1]

      for {set j 0} {$j < [expr {[llength $row] - 1}]} {incr j} {
        lappend next [expr {[lindex $row $j] + [lindex $row [expr {$j + 1}]]}]
      }

      lappend next 1
      set row $next
    }

    return $result
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "row 4           = [::examples::pascalRow 4]"
puts "row 10          = [::examples::pascalRow 10]"
puts "triangle 5      = [::examples::pascalTriangle 5]"
