###############################################################################
#
# curl.tcl --
#
# Tcl Language Standard
# Conformance Test File
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
# Section 1 -- HTTPS fetch: basic connectivity and content
#
###############################################################################

runTest {test curl-1.1 {
  R-10050-09986: libcurl fetch HTTPS content
} -constraints {
    libcurl
} -setup {
} -body {
  catch {source https://script.eagle.to/scripts/secureTest.eagle}
} -cleanup {
  catch {rename helloWorld ""}
  unset -nocomplain msg
} -result {0}}

###############################################################################

runTest {test curl-1.2 {
  R-10050-09986: libcurl fetched script has proc
} -constraints {
    libcurl
} -setup {
} -body {
  catch {source https://script.eagle.to/scripts/secureTest.eagle}
  set found [llength [info commands helloWorld]]
  expr {$found > 0}
} -cleanup {
  catch {rename helloWorld ""}
  unset -nocomplain found
} -result {1}}

###############################################################################
#
# Section 2 -- URI validation
#
###############################################################################

runTest {test curl-2.1 {
  R-10050-09986: libcurl invalid scheme rejected
} -constraints {
    libcurl
} -setup {
} -body {
  list [catch {source ftp://example.com/bad} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test curl-2.2 {
  R-10050-09986: libcurl file scheme rejected
} -constraints {
    libcurl
} -setup {
} -body {
  list [catch {source file:///etc/passwd} msg] [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################
#
# Section 3 -- Error handling
#
###############################################################################

runTest {test curl-3.1 {
  R-10050-09986: libcurl nonexistent host error
} -constraints {
    libcurl
} -setup {
} -body {
  list [catch {source https://this.host.does.not.exist.invalid/x} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

runTest {test curl-3.2 {
  R-10050-09986: libcurl 404 error
} -constraints {
    libcurl
} -setup {
} -body {
  list [catch {source https://script.eagle.to/nonexistent_path_404} msg] \
      [expr {$msg ne ""}]
} -cleanup {
  unset -nocomplain msg
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
