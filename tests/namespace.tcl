###############################################################################
#
# namespace.tcl --
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
# Section 1 -- namespace eval
#
###############################################################################

runTest {test namespace-1.1 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  namespace eval ::testns {
    variable x 1
  }
  namespace exists ::testns
} -cleanup {
  catch {namespace delete ::testns}
} -result {1}}

###############################################################################

runTest {test namespace-1.2 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  namespace eval ::testns {
    proc hello {} {
      return "hello from testns"
    }
  }
  ::testns::hello
} -cleanup {
  catch {namespace delete ::testns}
} -result {hello from testns}}

###############################################################################

runTest {test namespace-1.3 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  namespace eval ::outer {
    namespace eval inner {
      variable val "nested"
    }
  }
  set ::outer::inner::val
} -cleanup {
  catch {namespace delete ::outer}
} -result {nested}}

###############################################################################
#
# Section 2 -- namespace current
#
###############################################################################

runTest {test namespace-2.1 {
  R-53980-30653: namespace current returns fully qualified name
} -body {
  namespace current
} -result {::}}

###############################################################################

runTest {test namespace-2.2 {
  R-53980-30653: namespace current returns fully qualified name
} -setup {
} -body {
  namespace eval ::testns {
    namespace current
  }
} -cleanup {
  catch {namespace delete ::testns}
} -result {::testns}}

###############################################################################
#
# Section 3 -- namespace exists
#
###############################################################################

runTest {test namespace-3.1 {
  R-09931-18274: namespace exists returns 1/0
} -body {
  namespace exists ::
} -result {1}}

###############################################################################

runTest {test namespace-3.2 {
  R-09931-18274: namespace exists returns 1/0
} -body {
  namespace exists ::nosuchns
} -result {0}}

###############################################################################

runTest {test namespace-3.3 {
  R-09931-18274: namespace exists returns 1/0
} -setup {
} -body {
  namespace eval ::testns {}
  namespace exists ::testns
} -cleanup {
  catch {namespace delete ::testns}
} -result {1}}

###############################################################################
#
# Section 4 -- namespace children
#
###############################################################################

runTest {test namespace-4.1 {
  R-05433-10222: namespace children returns list of children
} -setup {
} -body {
  namespace eval ::testchild1 {}
  namespace eval ::testchild2 {}
  set children [namespace children ::]
  list [expr {[lsearch $children "::testchild1"] >= 0}] \
      [expr {[lsearch $children "::testchild2"] >= 0}]
} -cleanup {
  catch {namespace delete ::testchild1}
  catch {namespace delete ::testchild2}
  unset -nocomplain children
} -result {1 1}}

###############################################################################

runTest {test namespace-4.2 {
  R-05433-10222: namespace children returns list of children
} -setup {
} -body {
  namespace eval ::nstestA {}
  namespace eval ::nstestB {}
  set children [namespace children ::]
  set count 0
  foreach ch $children {
    if {[string match "::nstest*" $ch]} then {
      incr count
    }
  }
  expr {$count >= 2}
} -cleanup {
  catch {namespace delete ::nstestA}
  catch {namespace delete ::nstestB}
  unset -nocomplain children
  unset -nocomplain count
  unset -nocomplain ch
} -result {1}}

###############################################################################
#
# Section 5 -- namespace parent
#
###############################################################################

runTest {test namespace-5.1 {
  R-41224-57965: namespace parent returns parent name
} -setup {
} -body {
  namespace eval ::testns {}
  namespace parent ::testns
} -cleanup {
  catch {namespace delete ::testns}
} -result {::}}

###############################################################################

runTest {test namespace-5.2 {
  R-41224-57965: namespace parent returns parent name
} -setup {
} -body {
  namespace eval ::outer {
    namespace eval inner {}
  }
  namespace parent ::outer::inner
} -cleanup {
  catch {namespace delete ::outer}
} -result {::outer}}

###############################################################################

runTest {test namespace-5.3 {
  R-41224-57965: namespace parent returns parent name
} -body {
  namespace parent ::
} -result {}}

###############################################################################
#
# Section 6 -- namespace delete
#
###############################################################################

runTest {test namespace-6.1 {
  R-41102-11253: namespace delete deletes namespace and contents
} -setup {
} -body {
  namespace eval ::testns {
    variable x 1
  }
  namespace delete ::testns
  namespace exists ::testns
} -result {0}}

###############################################################################

runTest {test namespace-6.2 {
  R-41102-11253: namespace delete deletes namespace and contents
} -setup {
} -body {
  namespace eval ::outer {
    namespace eval inner {
      variable x 1
    }
  }
  namespace delete ::outer
  namespace exists ::outer::inner
} -result {0}}

###############################################################################

runTest {test namespace-6.3 {
  R-41102-11253: namespace delete deletes namespace and contents
} -setup {
} -body {
  list [catch {namespace delete ::nosuchns} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 7 -- namespace export / import
#
###############################################################################

runTest {test namespace-7.1 {
  R-10701-37097: namespace import creates command copies
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export myProc
    proc myProc {} {
      return "exported"
    }
  }
  namespace eval ::dstns {
    namespace import ::srcns::myProc
  }
  ::dstns::myProc
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
} -result {exported}}

###############################################################################

runTest {test namespace-7.2 {
  R-04455-29532: namespace export adds patterns to export list
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export pub*
    proc pubFunc {} {return "public"}
    proc privFunc {} {return "private"}
  }
  set exported [namespace eval ::srcns {namespace export}]
  expr {[lsearch $exported "pub*"] >= 0}
} -cleanup {
  catch {namespace delete ::srcns}
  unset -nocomplain exported
} -result {1}}

###############################################################################
#
# Section 8 -- qualified names
#
###############################################################################

runTest {test namespace-8.1 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  namespace eval ::testns {
    proc greet {} {
      return "hello"
    }
  }
  ::testns::greet
} -cleanup {
  catch {namespace delete ::testns}
} -result {hello}}

###############################################################################

runTest {test namespace-8.2 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  namespace eval ::testns {
    variable myvar "qval"
  }
  set ::testns::myvar
} -cleanup {
  catch {namespace delete ::testns}
} -result {qval}}

###############################################################################
#
# Section 9 -- namespace variables (variable command interaction)
#
###############################################################################

runTest {test namespace-9.1 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  namespace eval ::testns {
    variable counter 0
  }
  namespace eval ::testns {
    incr ::testns::counter
  }
  namespace eval ::testns {
    incr ::testns::counter
  }
  set ::testns::counter
} -cleanup {
  catch {namespace delete ::testns}
} -result {2}}

###############################################################################

runTest {test namespace-9.2 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  namespace eval ::testns {
    variable data "secret"
    proc getData {} {
      return $::testns::data
    }
  }
  ::testns::getData
} -cleanup {
  catch {namespace delete ::testns}
} -result {secret}}

###############################################################################
#
# Section 10 -- namespace: error cases
#
###############################################################################

runTest {test namespace-10.1 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  list [catch {namespace} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *wrong # args*}}

###############################################################################

runTest {test namespace-10.2 {
  R-24585-38621: namespace eval evaluates in namespace context
} -setup {
} -body {
  list [catch {namespace nosuchsub} msg] $msg
} -cleanup {
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 11 -- namespace import: imported proc namespace context
#
###############################################################################

runTest {test namespace-11.1 {
  R-53980-30653: imported proc sees defining namespace via namespace current
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export whereAmI
    proc whereAmI {} {
      namespace current
    }
  }
  namespace eval ::dstns {
    namespace import ::srcns::whereAmI
  }
  ::dstns::whereAmI
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
} -result {::srcns}}

###############################################################################

runTest {test namespace-11.2 {
  R-10701-37097: uplevel 1 from imported proc evaluates in caller namespace
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export getCaller
    proc getCaller {} {
      uplevel 1 {namespace current}
    }
  }
  namespace eval ::dstns {
    namespace import ::srcns::getCaller
  }
  namespace eval ::dstns {
    getCaller
  }
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
} -result {::dstns}}

###############################################################################

runTest {test namespace-11.3 {
  R-10701-37097: namespace import -force replaces existing imported command
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export myCmd
    proc myCmd {} {
      return "version1"
    }
  }
  namespace eval ::dstns {
    namespace import ::srcns::myCmd
  }
  set r1 [::dstns::myCmd]
  namespace eval ::srcns {
    proc myCmd {} {
      return "version2"
    }
  }
  namespace eval ::dstns {
    namespace import -force ::srcns::myCmd
  }
  set r2 [::dstns::myCmd]
  list $r1 $r2
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
  unset -nocomplain r1
  unset -nocomplain r2
} -result {version1 version2}}

###############################################################################

runTest {test namespace-11.4 {
  R-53980-30653: imported proc accesses defining namespace variables
} -setup {
} -body {
  namespace eval ::srcns {
    variable secret "hidden"
    namespace export getSecret
    proc getSecret {} {
      variable secret
      return $secret
    }
  }
  namespace eval ::dstns {
    namespace import ::srcns::getSecret
  }
  ::dstns::getSecret
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
} -result {hidden}}

###############################################################################

runTest {test namespace-11.5 {
  R-10701-37097: namespace import -force with no prior import is not error
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export foo
    proc foo {} {
      return "bar"
    }
  }
  namespace eval ::dstns {
    namespace import -force ::srcns::foo
  }
  ::dstns::foo
} -cleanup {
  catch {namespace delete ::srcns}
  catch {namespace delete ::dstns}
} -result {bar}}

###############################################################################

runTest {test namespace-11.6 {
  R-10701-37097: imported proc called from global sees global as caller
} -setup {
} -body {
  namespace eval ::srcns {
    namespace export callerNs
    proc callerNs {} {
      uplevel 1 {namespace current}
    }
  }
  namespace import -force ::srcns::callerNs
  callerNs
} -cleanup {
  catch {rename callerNs ""}
  catch {namespace delete ::srcns}
} -result {::}}

###############################################################################
#
# Section 12 -- namespace nesting depth bound (TH8K-011 / Bug 81).
#
# A deeply QUALIFIED name creates one namespace per "::" component in a
# single command, bypassing the evaluation-depth limit that bounds
# nested `namespace eval`.  TH8's teardown (th8FreeNamespace) recurses
# per level, so an unbounded tree used to overflow the native C stack at
# interpreter deletion (SIGSEGV).  TH8 now bounds nesting at
# TH8_MX_NS_DEPTH (1000).  th8-constrained: the reference Tcl accepts
# arbitrarily deep namespaces (it does not use a bounded recursive
# teardown), so it would not raise this error.
#
###############################################################################

runTest {test namespace-12.1 {
  Creating a namespace nested beyond TH8_MX_NS_DEPTH via a deeply
  qualified name is rejected cleanly ("namespace nested too deeply")
  rather than crashing the interpreter at teardown (Bug 81).
} -constraints {
    th8
} -setup {
  set qual [string repeat "a::" 5000]x
} -body {
  catch {namespace eval $qual { set z 1 }} m
  list [catch {namespace eval $qual { set z 1 }}] $m
} -cleanup {
  # The rejected attempt still creates the partial chain up to the
  # limit before failing; delete its root (a bounded recursive delete).
  catch {namespace delete ::a}
  unset -nocomplain qual m
} -result {1 {namespace nested too deeply}}}

###############################################################################

runTest {test namespace-12.2 {
  A namespace nested well UNDER the limit is created normally and the
  interpreter tears down cleanly (bounded recursion), and an ordinary
  shallow namespace is unaffected.
} -constraints {
    th8
} -setup {
  set deep [string repeat "n::" 500]leaf
} -body {
  list [catch {namespace eval $deep { variable v 7; set v }} r] $r \
      [namespace eval ::demo::sub { variable w 9; set w }]
} -cleanup {
  catch {namespace delete ::n}
  catch {namespace delete ::demo}
  unset -nocomplain deep r
} -result {0 7 9}}

###############################################################################

source tests/epilogue.tcl
