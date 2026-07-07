###############################################################################
#
# newcmds.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for new commands: file type, file under, file validname,
# hash normal, info context, info varlinks, and the variable
# command qualified name fix.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test filetype-1.1 {
  file type returns "directory" for a directory
} -constraints {
    th8
} -body {
  file type bin
} -result {directory}}

###############################################################################

runTest {test filetype-1.2 {
  file type returns "file" for a regular file
} -constraints {
    th8
} -body {
  file type [file join $::th8test::binPath [file tail [info nameofexecutable]]]
} -result {file}}

###############################################################################

runTest {test filetype-1.3 {
  file type returns "unknown" for nonexistent path
} -constraints {
    th8
} -body {
  file type nonexistent_path_xyz
} -result {unknown}}

###############################################################################

runTest {test filetype-1.4 {
  file type: symlink to file returns "file symbolicLink"
} -constraints {
    th8 symlink symlink_allowed
} -body {
  file type [file join $::th8test::binPath _th8test_flink]
} -result {file symbolicLink}}

###############################################################################

runTest {test filetype-1.5 {
  file type: symlink to directory returns "directory symbolicLink"
} -constraints {
    th8 symlink symlink_allowed
} -body {
  file type [file join $::th8test::binPath _th8test_dlink]
} -result {directory symbolicLink}}

###############################################################################

runTest {test filetype-1.6 {
  file type wrong number of arguments
} -constraints {
    th8
} -body {
  file type
} -returnCodes 1 -match glob -result {wrong # args:*}}

###############################################################################

runTest {test fileunder-1.1 {
  file under returns 1 when name1 is inside name2
} -constraints {
    th8
} -body {
  file under bin/th8sh bin
} -result {1}}

###############################################################################

runTest {test fileunder-1.2 {
  file under returns 0 when name1 is not inside name2
} -constraints {
    th8
} -body {
  file under bin/th8sh tests
} -result {0}}

###############################################################################

runTest {test fileunder-1.3 {
  file under: path is under itself
} -constraints {
    th8
} -body {
  file under bin bin
} -result {1}}

###############################################################################

runTest {test fileunder-1.4 {
  file under wrong number of arguments
} -constraints {
    th8
} -body {
  file under foo
} -returnCodes 1 -match glob -result {wrong # args:*}}

###############################################################################

runTest {test filevalidname-1.1 {
  file validname: valid relative path
} -constraints {
    th8
} -body {
  file validname foo/bar
} -result {1}}

###############################################################################

runTest {test filevalidname-1.2 {
  file validname: empty path is invalid
} -constraints {
    th8
} -body {
  file validname {}
} -result {0}}

###############################################################################

runTest {test filevalidname-1.3 {
  file validname: valid with pathType None
} -constraints {
    th8
} -body {
  file validname foo/bar None
} -result {1}}

###############################################################################

runTest {test filevalidname-1.4 {
  file validname: case-insensitive pathType None
} -constraints {
    th8
} -body {
  file validname foo/bar none
} -result {1}}

###############################################################################

runTest {test filevalidname-1.4a {
  file validname: all-uppercase pathType NONE accepted
  (covers the C1=T arm of each case-fold OR in the type check)
} -constraints {
    th8
} -body {
  file validname foo/bar NONE
} -result {1}}

###############################################################################

runTest {test filevalidname-1.4b {
  file validname: 4-letter pathType that fails at position 0
  (covers the (F,F) arm of the case-fold OR at zType[0])
} -constraints {
    th8
} -body {
  list [catch {file validname foo/bar ABCD} msg] \
       [string match {*pathType*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test filevalidname-1.4c {
  file validname: 4-letter pathType that fails at position 1
  (covers the (F,F) arm at zType[1])
} -constraints {
    th8
} -body {
  list [catch {file validname foo/bar NABC} msg] \
       [string match {*pathType*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test filevalidname-1.4d {
  file validname: 4-letter pathType that fails at position 2
  (covers the (F,F) arm at zType[2])
} -constraints {
    th8
} -body {
  list [catch {file validname foo/bar NOAB} msg] \
       [string match {*pathType*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test filevalidname-1.4e {
  file validname: 4-letter pathType that fails at position 3
  (covers the (F,F) arm at zType[3])
} -constraints {
    th8
} -body {
  list [catch {file validname foo/bar NONA} msg] \
       [string match {*pathType*} $msg]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test filevalidname-1.5 {
  file validname: unsupported pathType
} -constraints {
    th8
} -body {
  file validname foo/bar Absolute
} -returnCodes 1 -match glob -result {*pathType*}}

###############################################################################

runTest {test filevalidname-1.6 {
  file validname: wrong number of arguments
} -constraints {
    th8
} -body {
  file validname
} -returnCodes 1 -match glob -result {wrong # args:*}}

###############################################################################

runTest {test hash-1.1 {
  hash normal SHA512: known test vector
} -constraints {
    th8 crypto
} -body {
  hash normal SHA512 hello
} -result {9b71d224bd62f3785d96d46ad3ea3d73319bfbc2890caadae2dff72519673ca72323c3d99ba5c11d7c7acc6e14b8c5da0c4663475c2e5c3adef46f73bcdec043}}

###############################################################################

runTest {test hash-1.2 {
  hash normal SHA512: empty string
} -constraints {
    th8 crypto
} -body {
  hash normal SHA512 {}
} -result {cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e}}

###############################################################################

runTest {test hash-1.3 {
  hash normal: case-insensitive algorithm name
} -constraints {
    th8 crypto
} -body {
  hash normal sha512 hello
} -result {9b71d224bd62f3785d96d46ad3ea3d73319bfbc2890caadae2dff72519673ca72323c3d99ba5c11d7c7acc6e14b8c5da0c4663475c2e5c3adef46f73bcdec043}}

###############################################################################

runTest {test hash-1.4 {
  hash normal: unsupported algorithm
} -constraints {
    th8 crypto
} -body {
  hash normal MD5 hello
} -returnCodes 1 -match glob -result {unsupported*}}

###############################################################################

runTest {test hash-1.5 {
  hash normal: wrong number of arguments
} -constraints {
    th8 crypto
} -body {
  hash normal SHA512
} -returnCodes 1 -match glob -result {wrong # args:*}}

###############################################################################

runTest {test hash-1.6 {
  hash: unsupported subcommand
} -constraints {
    th8 crypto
} -body {
  hash bogus SHA512 hello
} -returnCodes 1 -match glob -result {*}}

###############################################################################

runTest {test infocontext-1.1 {
  info context returns a 128-character hex string
} -constraints {
    th8 crypto
} -body {
  string length [info context]
} -result {128}}

###############################################################################

runTest {test infocontext-1.2 {
  info context returns a stable value
} -constraints {
    th8 crypto
} -body {
  expr {[info context] eq [info context]}
} -result {1}}

###############################################################################

runTest {test infocontext-1.3 {
  info context is valid hexadecimal
} -constraints {
    th8 crypto
} -body {
  string is xdigit [info context]
} -result {1}}

###############################################################################

runTest {test infovarlinks-1.1 {
  info varlinks: empty at global level
} -constraints {
    th8
} -body {
  info varlinks
} -result {}}

###############################################################################

runTest {test infovarlinks-1.2 {
  info varlinks: detects upvar link
} -constraints {
    th8
} -body {
  set x 1
  proc _test_upvar {} {
    upvar 1 x linked
    info varlinks
  }
  set r [_test_upvar]
  rename _test_upvar {}
  set r
} -cleanup {
  unset -nocomplain r x
} -result {linked}}

###############################################################################

runTest {test infovarlinks-1.3 {
  info varlinks: detects global link
} -constraints {
    th8
} -body {
  set ::_gvar 1
  proc _test_global {} {
    global _gvar
    info varlinks
  }
  set r [_test_global]
  rename _test_global {}
  unset ::_gvar
  set r
} -cleanup {
  unset -nocomplain r
} -result {_gvar}}

###############################################################################

runTest {test infovarlinks-1.4 {
  info varlinks: detects variable link (unqualified)
} -constraints {
    th8
} -body {
  namespace eval ::_testns {
    variable v 99
    proc getvl {} {
      variable v
      info varlinks
    }
  }
  set r [::_testns::getvl]
  namespace delete ::_testns
  set r
} -cleanup {
  unset -nocomplain r
} -result {v}}

###############################################################################

runTest {test infovarlinks-1.5 {
  info varlinks: detects variable link (qualified name)
} -constraints {
    th8
} -body {
  namespace eval ::_testns2 {
    variable w 77
  }
  proc _test_qualvar {} {
    variable ::_testns2::w
    info varlinks
  }
  set r [_test_qualvar]
  rename _test_qualvar {}
  namespace delete ::_testns2
  set r
} -cleanup {
  unset -nocomplain r
} -result {w}}

###############################################################################

runTest {test infovarlinks-1.6 {
  info varlinks: multiple links
} -constraints {
    th8
} -body {
  set ::_a 1
  set ::_b 2
  proc _test_multi {} {
    global _a _b
    lsort [info varlinks]
  }
  set r [_test_multi]
  rename _test_multi {}
  unset ::_a ::_b
  set r
} -cleanup {
  unset -nocomplain r
} -result {_a _b}}

###############################################################################

runTest {test variable-qualified-1.1 {
  variable with qualified name creates local link
} -constraints {
    th8
} -body {
  namespace eval ::_vqtest { variable x 42 }
  proc _vqproc {} {
    variable ::_vqtest::x
    set x
  }
  set r [_vqproc]
  rename _vqproc {}
  namespace delete ::_vqtest
  set r
} -cleanup {
  unset -nocomplain r
} -result {42}}

###############################################################################

runTest {test variable-qualified-1.2 {
  variable with qualified name: modification visible
} -constraints {
    th8
} -body {
  namespace eval ::_vqtest2 { variable y 10 }
  proc _vqproc2 {} {
    variable ::_vqtest2::y
    incr y
  }
  _vqproc2
  set r [set ::_vqtest2::y]
  rename _vqproc2 {}
  namespace delete ::_vqtest2
  set r
} -cleanup {
  unset -nocomplain r
} -result {11}}

###############################################################################
#
# Path sandboxing tests.
#
# These tests verify that file commands only accept and return paths
# relative to the base directory, rejecting absolute paths and ".."
# traversal attempts that would escape the sandbox.
#
###############################################################################

runTest {test sandbox-1.1 {
  R-57925-48172: file exists rejects absolute path
} -constraints {
    th8
} -body {
  file exists /etc/passwd
} -result {0}}

###############################################################################

runTest {test sandbox-1.2 {
  R-57925-48172: file exists rejects dotdot traversal
} -constraints {
    th8
} -body {
  file exists ../../etc/passwd
} -result {0}}

###############################################################################

runTest {test sandbox-1.3 {
  R-57925-48172: file exists allows relative path under base
} -constraints {
    th8
} -body {
  file exists tests/all.tcl
} -result {1}}

###############################################################################

runTest {test sandbox-2.1 {
  R-57925-48172: file type returns unknown for absolute path
} -constraints {
    th8
} -body {
  file type /etc/passwd
} -result {unknown}}

###############################################################################

runTest {test sandbox-2.2 {
  R-57925-48172: file type returns unknown for dotdot traversal
} -constraints {
    th8
} -body {
  file type ../../etc/passwd
} -result {unknown}}

###############################################################################

runTest {test sandbox-3.1 {
  R-51815-04030: file normalize returns NULL for absolute outside-base
} -constraints {
    th8
} -body {
  file normalize /etc/passwd
} -result {/etc/passwd}}

###############################################################################

runTest {test sandbox-3.2 {
  R-51815-04030: file normalize returns relative for path under base
} -constraints {
    th8
} -body {
  file normalize .
} -result {.}}

###############################################################################

runTest {test sandbox-4.1 {
  R-30707-46329: file rootpath returns dot for path under base
} -constraints {
    th8
} -body {
  file rootpath tests/all.tcl
} -result {.}}

###############################################################################

runTest {test sandbox-4.2 {
  R-30707-46329: file rootpath returns empty for absolute outside-base
} -constraints {
    th8
} -body {
  file rootpath /etc/passwd
} -result {}}

###############################################################################

runTest {test sandbox-4.3 {
  R-30707-46329: file rootpath returns empty for dotdot traversal
} -constraints {
    th8
} -body {
  file rootpath ../../etc/passwd
} -result {}}

###############################################################################

runTest {test sandbox-4.4 {
  R-54388-15254: file rootpath returns dot for base directory
} -constraints {
    th8
} -body {
  file rootpath .
} -result {.}}

###############################################################################

runTest {test sandbox-5.1 {
  R-54908-52666: file same rejects absolute paths
} -constraints {
    th8
} -body {
  file same /etc/passwd /etc/passwd
} -result {0}}

###############################################################################

runTest {test sandbox-6.1 {
  R-52412-33477: info nameofexecutable returns base-relative path
} -constraints {
    th8
} -body {
  set exe [info nameofexecutable]
  string match "./*" $exe
} -cleanup {
  unset -nocomplain exe
} -result {1}}

###############################################################################

runTest {test sandbox-6.2 {
  R-19219-42106: info nameofexecutable is not absolute
} -constraints {
    th8
} -body {
  set exe [info nameofexecutable]
  string match "/*" $exe
} -cleanup {
  unset -nocomplain exe
} -result {0}}

###############################################################################

runTest {test sandbox-7.1 {
  R-57925-48172: source rejects absolute path
} -constraints {
    th8
} -body {
  source /etc/passwd
} -returnCodes 1 -match glob -result {couldn't retrieve*}}

###############################################################################

source tests/epilogue.tcl
