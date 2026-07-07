#!/usr/bin/tcl
###############################################################################
#
# replace.tcl --
#
#     Replace string with another string -OR- include only lines
#     successfully modified with a regular expression.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################
fconfigure stdout -translation binary
fconfigure stderr -translation binary
set mode [string tolower [lindex $argv 0]]
set from [lindex $argv 1]
set to [lindex $argv 2]
if {-1 == [lsearch -exact [list exact regsub include] $mode]} then {exit 1}
if {[string length $from]==0} then {exit 2}
while {![eof stdin]} {
  set line [gets stdin]
  if {[eof stdin]} then { break }
  switch -exact $mode {
    exact {set line [string map [list $from $to] $line]}
    regsub {regsub -all -- $from $line $to line}
    include {if {[regsub -all -- $from $line $to line]==0} continue}
  }
  puts stdout $line
}
