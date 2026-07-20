###############################################################################
#
# dom_smoke.tcl --
#
# TH8 Example Scripts (LadyBird DOM integration)
# Smoke test: drives the DOM examples against the bridge mock.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

#
# NOTE: Run with ./bin/th8sh examples/dom/dom_smoke.tcl from the TH8
#       repository root.  Sources the bridge mock and the example
#       procedures, builds a fixture DOM through the mocked dom::*
#       commands, runs each example, dispatches a synthetic click, and
#       asserts the observed results.  Exits non-zero on any failure.
#

source examples/dom/dom_bridge_mock.tcl
source examples/dom/dom_examples.tcl

set ::passed 0
set ::failed 0

###############################################################################

#
# check --
#
#   Compares GOT against WANT for the named case, printing PASS/FAIL and
#   tallying ::passed / ::failed.
#
proc check { name got want } {
  if {$got eq $want} then {
    puts "  PASS $name"
    incr ::passed
  } else {
    puts "  FAIL $name: got {$got} want {$want}"
    incr ::failed
  }
}

###############################################################################

#
# buildFixture --
#
#   Resets the mock DOM and constructs the demonstration page through
#   the same dom::* commands a real script would use: a titled heading,
#   a populated menu, an empty to-do list, a call-to-action link, an
#   activate button, and an empty status region.
#
proc buildFixture {} {
  ::dommock::reset

  set doc [dom::document]
  set body [$doc body]

  set title [$doc createElement h1]
  $title setAttribute id page-title
  $title textContent "Welcome"
  $body appendChild $title

  set menu [$doc createElement ul]
  $menu setAttribute id menu
  $body appendChild $menu

  foreach label {One Two Three} {
    set item [$doc createElement li]
    $item setAttribute class item
    $item textContent $label
    $menu appendChild $item
  }

  set todo [$doc createElement ul]
  $todo setAttribute id todo
  $body appendChild $todo

  set cta [$doc createElement a]
  $cta setAttribute id cta
  $cta setAttribute href /start
  $body appendChild $cta

  set go [$doc createElement button]
  $go setAttribute id go
  $go setAttribute data-label Go
  $body appendChild $go

  set status [$doc createElement div]
  $status setAttribute id status
  $body appendChild $status
}

###############################################################################

puts "==== LadyBird DOM example smoke test ===="

buildFixture
check lookup [::examples::domLookup] {Welcome One /start}

buildFixture
check create-insert [::examples::domCreateInsert] {3 {Buy Cook Sleep}}

buildFixture
check traverse [::examples::domTraverse] {{LI LI LI} UL}

buildFixture
set statusHandle [::examples::domEvents]
::dommock::dispatch [dom::document getElementById go] click
check events [$statusHandle textContent] {click on Go}

buildFixture
check cross-eval [::examples::domCrossEval] {js-result:document.title}

###############################################################################

puts "==== passed=$::passed failed=$::failed ===="

if {$::failed > 0} then {
  exit 1
}
