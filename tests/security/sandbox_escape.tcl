###############################################################################
#
# sandbox_escape.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Red-team tests for sandbox escape attempts: binary loading, file
# access, source, unload, and other privilege escalation vectors.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################
#
# Binary loading gate.
#
###############################################################################

runTest {test sandbox-escape-1.1 {
  R-09721-12704: load command: binary loading is disabled
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {load ./lib/libfoo.so:Foo}]
  list [sandboxRc $r] [string match {*not enabled*} [sandboxResult $r]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################

runTest {test sandbox-escape-1.2 {
  R-09721-12704: unload command: unloading is disabled
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {unload ./lib/libfoo.so:Foo}]
  list [sandboxRc $r] [string match {*not enabled*} [sandboxResult $r]]
} -cleanup {
  unset -nocomplain r
} -result {1 1}}

###############################################################################
#
# File system escape attempts.
#
###############################################################################

runTest {test sandbox-escape-2.1 {
  R-02231-09675: source: absolute path outside sandbox rejected
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {source /etc/passwd}]
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-escape-2.2 {
  R-63048-10768: source: dotdot traversal outside sandbox rejected
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {source ../../etc/passwd}]
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-escape-2.3 {
  R-02231-09675: file exists: absolute path outside sandbox denied
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {file exists /etc/passwd}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

runTest {test sandbox-escape-2.4 {
  R-63048-10768: file exists: dotdot traversal denied
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {file exists ../../etc/passwd}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

runTest {test sandbox-escape-2.5 {
  R-56918-62587: file type: absolute path returns unknown
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {file type /bin/sh}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 unknown}}

###############################################################################

runTest {test sandbox-escape-2.6 {
  R-56918-62587: file normalize: absolute path outside sandbox blocked
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {file normalize /etc/passwd}]
  # Should return the path unchanged (normalize returns input
  # when the path resolves outside the base)
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 /etc/passwd}}

###############################################################################

runTest {test sandbox-escape-2.7 {
  R-56918-62587: file rootpath: absolute path returns empty (outside base)
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {file rootpath /etc/passwd}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 {}}}

###############################################################################

runTest {test sandbox-escape-2.8 {
  R-63048-10768: cd: cannot escape to parent directory
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {cd ..}]
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-escape-2.9 {
  R-02231-09675: cd: cannot escape to absolute path
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {cd /tmp}]
  expr {[sandboxRc $r] != 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test sandbox-escape-2.10 {
  R-02231-09675: file same: absolute paths denied
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {file same /etc/passwd /etc/passwd}]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################
#
# Command availability in sandbox.
#
###############################################################################

runTest {test sandbox-escape-3.1 {
  R-20999-34016: no parent commands leak into sandbox
} -constraints {
    th8 sandbox
} -body {
  # The sandbox should NOT have test library commands
  set r [::th8testlib::sandbox {
    info commands ::th8testlib::*
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 {}}}

###############################################################################

runTest {test sandbox-escape-3.2 {
  R-20999-34016: no parent variables leak into sandbox
} -constraints {
    th8 harpy_sign sandbox
} -body {
  # The parent's _harpyToken should not be visible
  set r [::th8testlib::sandbox {
    info exists ::_harpyToken
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

runTest {test sandbox-escape-3.3 {
  R-20999-34016: no parent namespaces leak into sandbox
} -constraints {
    th8 namespace_children sandbox
} -body {
  set r [::th8testlib::sandbox {
    namespace children ::
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 {}}}

###############################################################################

runTest {test sandbox-escape-3.4 {
  R-20999-34016: no parent procs leak into sandbox
} -constraints {
    th8 sandbox
} -body {
  set r [::th8testlib::sandbox {
    llength [info procs]
  }]
  list [sandboxRc $r] [sandboxResult $r]
} -cleanup {
  unset -nocomplain r
} -result {0 0}}

###############################################################################

source tests/epilogue.tcl
