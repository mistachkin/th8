###############################################################################
#
# baseconv.tcl --
#
# TH8 Example Scripts
# Pure-computation example: converting integers between number bases.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  variable digits 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ

  #
  # toBase --
  #
  #   Returns the string representation of the non-negative integer
  #   N written in the given BASE (2..36), using digits 0-9 then
  #   A-Z.  Repeatedly divides by the base, collecting remainders
  #   least-significant first, then reverses.  Errors on a bad base
  #   or negative N.
  #
  proc toBase { n base } {
    variable digits

    if {$base < 2 || $base > 36} then {
      error "base must be between 2 and 36"
    }

    if {$n < 0} then {
      error "n must be non-negative"
    }

    if {$n == 0} then {
      return 0
    }

    set result ""

    while {$n > 0} {
      set d [expr {$n % $base}]
      set result "[string index $digits $d]$result"
      set n [expr {$n / $base}]
    }

    return $result
  }

  #
  # fromBase --
  #
  #   Returns the integer value of the string TEXT interpreted in
  #   the given BASE (2..36).  Case-insensitive; uses Horner's
  #   method (result = result*base + digit).  Errors on a bad base
  #   or a digit that is out of range for the base.
  #
  proc fromBase { text base } {
    variable digits

    if {$base < 2 || $base > 36} then {
      error "base must be between 2 and 36"
    }

    set text [string toupper $text]
    set result 0

    foreach ch [split $text ""] {
      set d [string first $ch $digits]

      if {$d < 0 || $d >= $base} then {
        error "invalid digit \"$ch\" for base $base"
      }

      set result [expr {$result * $base + $d}]
    }

    return $result
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "255 -> base 16  = [::examples::toBase 255 16]"
puts "10 -> base 2    = [::examples::toBase 10 2]"
puts "FF <- base 16   = [::examples::fromBase FF 16]"
puts "round-trip 1000 = [::examples::fromBase [::examples::toBase 1000 7] 7]"
