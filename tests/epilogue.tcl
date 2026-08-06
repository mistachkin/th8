###############################################################################
#
# epilogue.tcl --
#
# Tcl Language Standard
# Conformance Test Infrastructure Epilogue File
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

if {[testConstraint symlink] && [testConstraint symlink_allowed]} then {
    catch {th8testlib::symlink delete \
	[file join $::th8test::binPath _th8test_flink]}
    catch {th8testlib::symlink delete \
	[file join $::th8test::binPath _th8test_dlink]}
}

###############################################################################
#
# Undo what the prologue did: the prologue loads the testlib via
# testLoadLib, so the epilogue must unload it.  TH8's [load] is
# reference-counted, and without this symmetric undo the load count
# would grow with every test file in the suite, breaking tests like
# load-3.6 that depend on the count reaching 0 after one unload.
#
###############################################################################

testUnloadLib

###############################################################################

if {[isEagle] && \
    [info exists eagle_debugger(savedExpressionFlags)]} then {
  object invoke \
      -flags +NonPublic -objectflags +AutoFlagsEnum \
      Interpreter.GetActive expressionFlags \
      $eagle_debugger(savedExpressionFlags)

  unset -nocomplain eagle_debugger(savedExpressionFlags)
}

###############################################################################

cleanupTests
