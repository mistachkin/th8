###############################################################################
#
# prologue.tcl --
#
# Tcl Language Standard
# Conformance Test Infrastructure Prologue File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

if {[catch {package require Tcl}] == 0 || \
    [catch {package require Eagle}] == 0} then {
  lappend ::auto_path [file join [file normalize \
      [file dirname [file dirname [info script]]]] lib th8]

  lappend ::auto_path [file join [file normalize \
      [file dirname [file dirname [info script]]]] lib Standard1.0]
}

###############################################################################

package require th8
package require th8test
package require th8test_exec
package require th8test_load
package require th8sqlite3

###############################################################################

initializeTests

###############################################################################

detectLoadLib
testLoadLib

###############################################################################

setupLoadConstraints
setupCryptoConstraints
setupFaultConstraints
setupSqliteConstraints
setupFileConstraints
setupCommandConstraints
setupSubCommandConstraints
setupNamespaceConstraints
setupCurlConstraints
setupCommonConstraints
