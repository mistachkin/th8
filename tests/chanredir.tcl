###############################################################################
#
# chanredir.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for I/O channel redirection (Section 32.1).
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test chanredir-1.1 {
  R-44914-34174: Th8_Output queries xGetOutput before xOutput
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_stdout.tmp]
} -body {
  th8testlib::chan set stdout $tmpfile
  puts -nonewline "hello from redirect"
  th8testlib::chan reset stdout
  th8testlib::chan set stdin $tmpfile
  set data [gets stdin]
  th8testlib::chan reset stdin
  set data
} -cleanup {
  catch {th8testlib::chan reset stdout}
  catch {th8testlib::chan reset stdin}
  unset -nocomplain data
  unset -nocomplain tmpfile
} -result {hello from redirect}}

###############################################################################

runTest {test chanredir-1.2 {
  R-41598-13571: Th8_RedirectOutput sets output channel via xSetOutput
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_stdout2.tmp]
} -body {
  th8testlib::chan set stdout $tmpfile
  puts "line1"
  th8testlib::chan reset stdout
  th8testlib::chan set stdin $tmpfile
  set data [gets stdin]
  th8testlib::chan reset stdin
  set data
} -cleanup {
  catch {th8testlib::chan reset stdout}
  catch {th8testlib::chan reset stdin}
  unset -nocomplain tmpfile data
} -result {line1}}

###############################################################################

runTest {test chanredir-2.1 {
  R-53110-05508: Th8_GetOutput returns NULL when no redirection active
} -constraints {
    th8 chan
} -body {
  th8testlib::chan get stdout
} -result {default}}

###############################################################################

runTest {test chanredir-2.2 {
  R-54889-38456: Th8_GetInput returns NULL when no redirection active
} -constraints {
    th8 chan
} -body {
  th8testlib::chan get stdin
} -result {default}}

###############################################################################

runTest {test chanredir-2.3 {
  R-06770-22179: Th8_GetErrorOutput returns NULL when no redirection active
} -constraints {
    th8 chan
} -body {
  th8testlib::chan get stderr
} -result {default}}

###############################################################################

runTest {test chanredir-2.4 {
  R-60126-19580: Th8_GetOutput retrieves current output channel
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_get.tmp]
} -body {
  th8testlib::chan set stdout $tmpfile
  set result [th8testlib::chan get stdout]
  th8testlib::chan reset stdout
  set result
} -cleanup {
  catch {th8testlib::chan reset stdout}
  unset -nocomplain tmpfile result
} -result [file join $::th8test::binPath _chantest_get.tmp]}

###############################################################################

runTest {test chanredir-2.5 {
  R-54749-53301: Th8_RedirectOutput with NULL resets to default
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_getrst.tmp]
} -body {
  th8testlib::chan set stdout $tmpfile
  th8testlib::chan reset stdout
  th8testlib::chan get stdout
} -cleanup {
  catch {th8testlib::chan reset stdout}
  unset -nocomplain tmpfile
} -result {default}}

###############################################################################

runTest {test chanredir-3.1 {
  R-50803-29436: Th8_RedirectInput returns error if file cannot be opened
} -constraints {
    th8 chan
} -body {
  th8testlib::chan set stdin /nonexistent/path/file.txt
} -returnCodes 1 -match glob -result {cannot open file:*}}

###############################################################################

runTest {test chanredir-3.2 {
  R-50803-29436: unknown channel name is rejected
} -constraints {
    th8 chan
} -body {
  th8testlib::chan get badchan
} -returnCodes 1 -match glob -result {unknown channel:*}}

###############################################################################

runTest {test chanredir-3.3 {
  R-41598-13571: unknown subcommand is rejected
} -constraints {
    th8 chan
} -body {
  th8testlib::chan badcmd stdout
} -returnCodes 1 -match glob -result {unknown subcommand:*}}

###############################################################################

runTest {test chanredir-3.4 {
  R-49310-16859: wrong number of arguments is rejected
} -constraints {
    th8 chan
} -body {
  th8testlib::chan
} -returnCodes 1 -match glob -result {wrong # args:*}}

###############################################################################

runTest {test chanredir-4.1 {
  R-32259-63920: puts rejects any channel other than stdout
} -constraints {
    th8
} -body {
  puts stderr "this should fail"
} -returnCodes 1 -match glob -result {channel not available:*}}

###############################################################################

runTest {test chanredir-5.1 {
  R-35230-48647: Th8_Input queries xGetInput before xInput
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_stdin.tmp]
  th8testlib::chan set stdout $tmpfile
  puts -nonewline "input line one"
  th8testlib::chan reset stdout
} -body {
  th8testlib::chan set stdin $tmpfile
  set line [gets stdin]
  th8testlib::chan reset stdin
  set line
} -cleanup {
  catch {th8testlib::chan reset stdin}
  catch {th8testlib::chan reset stdout}
  unset -nocomplain line
  unset -nocomplain tmpfile
} -result {input line one}}

###############################################################################

runTest {test chanredir-5.2 {
  R-07718-17484: Th8_GetInput retrieves current input channel
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_stdin2.tmp]
  th8testlib::chan set stdout $tmpfile
  puts "alpha"
  puts "bravo"
  puts "charlie"
  th8testlib::chan reset stdout
} -body {
  th8testlib::chan set stdin $tmpfile
  set a [gets stdin]
  set b [gets stdin]
  set c [gets stdin]
  th8testlib::chan reset stdin
  list $a $b $c
} -cleanup {
  catch {th8testlib::chan reset stdin}
  catch {th8testlib::chan reset stdout}
  unset -nocomplain a
  unset -nocomplain b
  unset -nocomplain c
  unset -nocomplain tmpfile
} -result {alpha bravo charlie}}

###############################################################################

runTest {test chanredir-5.3 {
  R-50803-29436: Th8_RedirectInput sets input channel via xSetInput
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_stdin3.tmp]
  th8testlib::chan set stdout $tmpfile
  puts -nonewline "hello world"
  th8testlib::chan reset stdout
} -body {
  th8testlib::chan set stdin $tmpfile
  set count [gets stdin line]
  th8testlib::chan reset stdin
  list $count $line
} -cleanup {
  catch {th8testlib::chan reset stdin}
  catch {th8testlib::chan reset stdout}
  unset -nocomplain count
  unset -nocomplain line
  unset -nocomplain tmpfile
} -result {11 {hello world}}}

###############################################################################

runTest {test chanredir-5.4 {
  R-60525-53038: Th8_RedirectInput with NULL resets input to default
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_stdin_eof.tmp]
  th8testlib::chan set stdout $tmpfile
  puts -nonewline ""
  th8testlib::chan reset stdout
} -body {
  th8testlib::chan set stdin $tmpfile
  set count [gets stdin line]
  th8testlib::chan reset stdin
  set count
} -cleanup {
  catch {th8testlib::chan reset stdin}
  catch {th8testlib::chan reset stdout}
  unset -nocomplain tmpfile count line
} -result {-1}}

###############################################################################

runTest {test chanredir-5.5 {
  R-07718-17484: Th8_GetInput returns path after redirect
} -constraints {
    th8 chan
} -setup {
  set tmpfile [file join $::th8test::binPath _chantest_stdin5.tmp]
  th8testlib::chan set stdout $tmpfile
  puts -nonewline "x"
  th8testlib::chan reset stdout
} -body {
  th8testlib::chan set stdin $tmpfile
  set result [th8testlib::chan get stdin]
  th8testlib::chan reset stdin
  set result
} -cleanup {
  catch {th8testlib::chan reset stdin}
  catch {th8testlib::chan reset stdout}
  unset -nocomplain tmpfile result
} -result [file join $::th8test::binPath _chantest_stdin5.tmp]}

###############################################################################

source tests/epilogue.tcl
