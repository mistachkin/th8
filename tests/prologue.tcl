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

#
# th8sqlite3 provides the optional SQLite-backed key/value store used by the
# kv_sqlite / secure-persist tests.  It is NOT present under stock Tcl (the
# reference engine for the conformance gate) or in a TH8 build without SQLite,
# so requiring it is optional: catch the failure and let the kv_sqlite /
# secure_persist constraints skip the dependent tests.  (Without this, running
# tests/all.tcl under tclsh 8.x errors at load; see the RTM "Tcl 8.6 gate".)
#
catch {package require th8sqlite3}

###############################################################################

initializeTests

###############################################################################

if {[isEagle]} then {
  #
  # NOTE: Eagle renders [expr] booleans as .NET "True" / "False"; forcibly
  #       enable the BooleanToInteger expression flag so they are the Tcl
  #       compatible "1"/ "0" the standard test results expect.
  #
  set eagle_debugger(savedExpressionFlags) [object invoke \
      -flags +NonPublic Interpreter.GetActive expressionFlags]

  object invoke -flags +NonPublic -objectflags +AutoFlagsEnum \
      Interpreter.GetActive expressionFlags +BooleanToInteger
}

###############################################################################

detectLoadLib
testLoadLib

###############################################################################

setupCommonConstraints
setupLoadConstraints
setupCryptoConstraints
setupFaultConstraints
setupSqliteConstraints
setupFileConstraints
setupCommandConstraints
setupSubCommandConstraints
setupNamespaceConstraints
setupCurlConstraints
