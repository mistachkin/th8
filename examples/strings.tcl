###############################################################################
#
# strings.tcl --
#
# TH8 Example Scripts
# Pure-computation example: string algorithms (palindrome, anagram,
# Caesar cipher, run-length encoding).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

namespace eval ::examples {
  #
  # isPalindrome --
  #
  #   Returns 1 when TEXT reads the same forwards and backwards
  #   (case-sensitive, whitespace-significant), 0 otherwise.  Uses
  #   the built-in [string reverse] for the comparison.
  #
  proc isPalindrome { text } {
    return [expr {$text eq [string reverse $text]}]
  }

  #
  # isAnagram --
  #
  #   Returns 1 when strings A and B contain exactly the same
  #   characters in some order, 0 otherwise.  Normalizes by
  #   splitting each string into characters and sorting; equal
  #   sorted character lists mean the strings are anagrams.
  #
  proc isAnagram { a b } {
    set sortedA [lsort [split $a ""]]
    set sortedB [lsort [split $b ""]]

    return [expr {$sortedA eq $sortedB}]
  }

  #
  # caesar --
  #
  #   Returns TEXT with each ASCII letter shifted forward by SHIFT
  #   positions within its own case (wrapping past Z back to A);
  #   non-letters pass through unchanged.  Decrypt by shifting with
  #   26 - SHIFT.  Uses [scan]/[format] for character arithmetic.
  #
  proc caesar { text shift } {
    set shift [expr {(($shift % 26) + 26) % 26}]
    set result ""

    foreach ch [split $text ""] {
      scan $ch %c code

      if {$code >= 65 && $code <= 90} then {
        set code [expr {(($code - 65 + $shift) % 26) + 65}]
        append result [format %c $code]
      } elseif {$code >= 97 && $code <= 122} then {
        set code [expr {(($code - 97 + $shift) % 26) + 97}]
        append result [format %c $code]
      } else {
        append result $ch
      }
    }

    return $result
  }

  #
  # runLengthEncode --
  #
  #   Returns a run-length encoding of TEXT as a flat list of
  #   count/character pairs -- e.g. "aaabbc" becomes {3 a 2 b 1 c}.
  #   Walks the string once, extending the current run while the
  #   character repeats.  Inverse of [runLengthDecode].
  #
  proc runLengthEncode { text } {
    set result [list]
    set n [string length $text]

    for {set i 0} {$i < $n} {} {
      set ch [string index $text $i]
      set count 0

      while {$i < $n && [string index $text $i] eq $ch} {
        incr count
        incr i
      }

      lappend result $count $ch
    }

    return $result
  }

  #
  # runLengthDecode --
  #
  #   Returns the original string reconstructed from a run-length
  #   encoding PAIRS (a flat list of count/character pairs as built
  #   by [runLengthEncode]).  Inverse of [runLengthEncode].
  #
  proc runLengthDecode { pairs } {
    set result ""

    foreach {count ch} $pairs {
      append result [string repeat $ch $count]
    }

    return $result
  }
}

###############################################################################

#
# NOTE: Demonstration -- compute and print a few results.
#
puts "palindrome?     = [::examples::isPalindrome racecar]"
puts "anagram?        = [::examples::isAnagram listen silent]"
puts "caesar +3       = [::examples::caesar {Hello, World!} 3]"
puts "caesar decode   = [::examples::caesar [::examples::caesar {Hello, World!} 3] 23]"
puts "rle encode      = [::examples::runLengthEncode aaabbbbc]"
puts "rle decode      = [::examples::runLengthDecode {3 a 4 b 1 c}]"
