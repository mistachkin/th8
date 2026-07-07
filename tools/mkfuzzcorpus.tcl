#!/usr/bin/env tclsh
###############################################################################
#
# mkfuzzcorpus.tcl --
#
#     Generate seed corpus files for the TH8 fuzz harnesses.
#
#     Extracts test bodies from the TH8 test suite and produces
#     individual seed files in the fuzz/corpus_* directories.
#     Also generates synthetic edge-case inputs for each target.
#
# Usage:
#
#     tclsh tools/mkfuzzcorpus.tcl ?targetDir?
#
#     targetDir defaults to "fuzz" relative to the project root.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################


proc writeSeed {dir name data} {
  file mkdir $dir
  set path [file join $dir $name]
  set fd [open $path w]
  fconfigure $fd -translation binary
  puts -nonewline $fd $data
  close $fd
}


###############################################################################
#
# extractTestBodies --
#
#     Scan a .tcl test file and extract the -body content of each
#     runTest block.  Returns a list of body strings.
#
###############################################################################

proc extractTestBodies {filePath bodyPat endPats} {
  set fd [open $filePath r]
  set content [read $fd]
  close $fd

  set bodies [list]
  set idx 0

  while {[set pos [string first $bodyPat $content $idx]] >= 0} {
    set start [expr {$pos + [string length $bodyPat]}]
    set end -1
    foreach marker $endPats {
      set m [string first $marker $content $start]
      if {$m >= 0 && ($end < 0 || $m < $end)} then {
        set end $m
      }
    }
    if {$end < 0} then {
      set idx [expr {$start + 1}]
      continue
    }

    set body [string trim [string range $content $start [expr {$end - 1}]]]
    if {[string length $body] > 0 && [string length $body] < 10000} then {
      lappend bodies $body
    }
    set idx [expr {$end + 1}]
  }
  return $bodies
}


###############################################################################
#
# generateEvalSeeds --
#
#     Extract test bodies from test files + add synthetic edge cases.
#
###############################################################################

proc generateEvalSeeds {corpusDir testDir} {
  set n 0

  #
  # Extract bodies from test files.
  #
  # Build pattern strings with literal braces outside proc bodies
  # to avoid Tcl brace-counting issues.
  set rb [format %c 125]
  set lb [format %c 123]
  set bodyPat "${rb} -body ${lb}"
  set endPats [list "${rb} -result" "${rb} -match" "${rb} -cleanup" "${rb} -returnCodes"]

  foreach f [glob -nocomplain [file join $testDir *.tcl]] {
    set tail [file tail $f]
    if {$tail eq "all.tcl" || $tail eq "prologue.tcl" || $tail eq "epilogue.tcl"} then { continue }

    foreach body [extractTestBodies $f $bodyPat $endPats] {
      writeSeed $corpusDir "test_[incr n].tcl" $body
    }
  }

  #
  # Synthetic edge cases.
  #
  set synthetics {
    "set x 1"
    "expr {1 + 2}"
    "if {1} {set y 2}"
    "while {0} {}"
    "foreach i {a b c} {}"
    {proc foo {a b} {expr {$a + $b}}; foo 1 2}
    "catch {error boom} msg; set msg"
    "string length hello"
    "string match {a*} abc"
    "lsearch -exact {a b c} b"
    "lsort -dictionary {a10 a2 a1}"
    "format {%010d %-20s %e} 42 hello 3.14"
    "namespace eval ::test { proc bar {} { return 42 } }; ::test::bar"
    "set arr(x) 1; set arr(y) 2; array names arr"
    "regexp {^[a-z]+$} hello"
    "return -code error -errorcode FUZZ oops"
    "uplevel 1 {set z 99}"
    "info commands *"
    "info procs ::*"
    "concat  a  {b c}  d "
    {subst -nocommands {hello $x}}
    {set x ""; for {set i 0} {$i < 100} {incr i} {append x a}; string length $x}
    "expr {0xdeadbeef}"
    "expr {0b10101010}"
    "expr {0o777}"
    "expr {1e308}"
    "expr {1e-308}"
    "expr {nan}"
    "expr {inf}"
    {set A 1}
    "list"
    ""
    "\n\n\n"
    "\r\n\r\n"
    "# comment only"
  }

  foreach s $synthetics {
    writeSeed $corpusDir "synth_[incr n].tcl" $s
  }

  puts "  eval: $n seeds"
}


###############################################################################
#
# generateExprSeeds --
#
###############################################################################

proc generateExprSeeds {corpusDir} {
  set n 0
  set exprs {
    "1 + 2"
    "3 * 4 - 1"
    "(1 + 2) * (3 + 4)"
    "1 << 10"
    "0xff & 0x0f"
    "1 == 1"
    "1 != 2"
    "1 < 2"
    "1 > 2"
    "1 <= 2"
    "1 >= 2"
    "true && false"
    "!true"
    "true || false"
    "1 ? 2 : 3"
    "sin(3.14)"
    "cos(0)"
    "sqrt(2)"
    "pow(2, 10)"
    "abs(-42)"
    "int(3.7)"
    "double(42)"
    "wide(12345678901234)"
    "rand()"
    "round(3.5)"
    "ceil(3.1)"
    "floor(3.9)"
    "log(1)"
    "log10(100)"
    "exp(1)"
    "fmod(10, 3)"
    "min(1, 2)"
    "max(1, 2)"
    "isnan(0.0)"
    "isinf(1e999)"
    "0xdeadbeef"
    "0b10101010"
    "0o777"
    "1e308"
    "1e-308"
    "9999999999999999999"
    "\"hello\" eq \"hello\""
    "\"abc\" ne \"def\""
    ""
    "(((((1)))))"
    "1 + + + 1"
  }

  foreach e $exprs {
    writeSeed $corpusDir "expr_[incr n].txt" $e
  }

  puts "  expr: $n seeds"
}


###############################################################################
#
# generateListSeeds --
#
###############################################################################

proc generateListSeeds {corpusDir} {
  set n 0

  # Simple list seeds - avoid brace strings inside proc bodies.
  foreach l [list \
      "a b c" \
      "a b c d e f g h i j" \
      "hello world" \
      "" \
      "   a   b   c   " \
      ] {
    writeSeed $corpusDir "list_[incr n].txt" $l
  }

  puts "  list: $n seeds"
}


###############################################################################
#
# generateFormatSeeds --
#
###############################################################################

proc generateFormatSeeds {corpusDir} {
  set n 0
  set formats {
    "%d"
    "%10d"
    "%-10d"
    "%+d"
    "%010d"
    "%u"
    "%o"
    "%#o"
    "%x"
    "%X"
    "%#x"
    "%c"
    "%s"
    "%20s"
    "%-20s"
    "%f"
    "%10.2f"
    "%e"
    "%E"
    "%g"
    "%G"
    "%%"
    "%*d"
    "%.*f"
    "%10d %s %f"
    "%-20s %08x %c"
    ""
    "%"
    "%-"
    "%999999d"
    "%0"
  }

  foreach f $formats {
    writeSeed $corpusDir "fmt_[incr n].txt" $f
  }

  puts "  format: $n seeds"
}


###############################################################################
#
# generateHarpySeeds --
#
###############################################################################

proc generateHarpySeeds {corpusDir projectRoot} {
  set n 0

  #
  # Real .b64sig files from the project.
  #
  foreach f [glob -nocomplain [file join $projectRoot tests helpers *.b64sig] \
      [file join $projectRoot lib th8 *.b64sig] \
      [file join $projectRoot tests *.b64sig]] {
    set fd [open $f rb]
    set data [read $fd]
    close $fd
    writeSeed $corpusDir "real_[incr n].bin" $data
  }

  #
  # Synthetic malformed signatures.
  #
  set synthetics {
    ""
    "###\n#\n# test -- abc123\n#\n"
    "###\n#\n# test -- 0000000000000000\n#\nAAAA"
    "not a signature at all"
    "###############\n#\n# x -- short\n#\nQQ=="
  }

  foreach s $synthetics {
    writeSeed $corpusDir "synth_[incr n].bin" $s
  }

  puts "  harpy: $n seeds"
}


###############################################################################
#
# generateSnkSeeds --
#
###############################################################################

proc generateSnkSeeds {corpusDir projectRoot} {
  set n 0

  #
  # Real .snk files from the project.
  #
  foreach f [glob -nocomplain [file join $projectRoot keys *.snk] \
      [file join $projectRoot tests helpers *.snk]] {
    set fd [open $f rb]
    set data [read $fd]
    close $fd
    writeSeed $corpusDir "real_[incr n].bin" $data
  }

  #
  # Synthetic edge cases.
  #
  set synthetics {
    ""
    "\x07\x02\x00\x00"
    "\x06\x02\x00\x00"
    "\x00\x24\x00\x00\x04\x80\x00\x00"
  }

  foreach s $synthetics {
    writeSeed $corpusDir "synth_[incr n].bin" $s
  }

  #
  # Random bytes.
  #
  for {set i 0} {$i < 5} {incr i} {
    set data ""
    for {set j 0} {$j < 200} {incr j} {
      append data [format %c [expr {int(rand() * 256)}]]
    }
    writeSeed $corpusDir "random_[incr n].bin" $data
  }

  puts "  snk: $n seeds"
}


###############################################################################
#
# main --
#
###############################################################################

proc main {argv} {
  set scriptDir [file dirname [info script]]
  set projectRoot [file dirname $scriptDir]

  set fuzzDir [file join $projectRoot fuzz]
  if {[llength $argv] >= 1} then {
    set fuzzDir [lindex $argv 0]
  }

  set testDir [file join $projectRoot tests]

  puts "Generating fuzz corpus in $fuzzDir ..."

  generateEvalSeeds   [file join $fuzzDir corpus_eval]   $testDir
  generateExprSeeds   [file join $fuzzDir corpus_expr]
  generateListSeeds   [file join $fuzzDir corpus_list]
  generateFormatSeeds [file join $fuzzDir corpus_format]
  generateHarpySeeds  [file join $fuzzDir corpus_harpy]  $projectRoot
  generateSnkSeeds    [file join $fuzzDir corpus_snk]    $projectRoot

  puts "Done."
}

main $argv
