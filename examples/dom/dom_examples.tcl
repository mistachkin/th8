###############################################################################
#
# dom_examples.tcl --
#
# TH8 Example Scripts (LadyBird DOM integration)
# Idiomatic DOM-scripting examples for the LadyBird TH8 bridge.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

#
# NOTE: These procedures use only the DOM bridge command surface that
#       LadyBird registers (dom::document, dom::console, dom::eval_js,
#       dom::release, and the node/element handle subcommands).  In a
#       real page each body would appear directly inside a
#       <script type="text/th8+signed"> block; they are wrapped as
#       procedures here so dom_smoke.tcl can drive them under ./bin/th8sh
#       against dom_bridge_mock.tcl (no browser required).  The shipped,
#       copy-pasteable HTML versions live beside this file's twin in the
#       LadyBird tree (Examples/TH8/dom/).
#

namespace eval ::examples {}

###############################################################################

#
# domLookup --
#
#   Element lookup: resolve elements by id and by CSS selector, then
#   read their text and attributes.  Returns a three-element list
#   {titleText firstItemText ctaHref} so callers can verify the reads.
#
proc ::examples::domLookup {} {
  set doc [dom::document]

  set title [$doc getElementById page-title]
  set titleText [$title textContent]

  set firstItem [$doc querySelector .item]
  set firstText [$firstItem textContent]

  set cta [$doc getElementById cta]
  set href [$cta getAttribute href]

  return [list $titleText $firstText $href]
}

###############################################################################

#
# domCreateInsert --
#
#   Node creation and insertion: build <li> elements with text and a
#   class attribute and append them to a list.  Returns {count labels}
#   read back by walking the list, confirming the tree was built.
#
proc ::examples::domCreateInsert {} {
  set doc [dom::document]
  set list [$doc getElementById todo]

  foreach task {Buy Cook Sleep} {
    set item [$doc createElement li]
    $item setAttribute class item

    set text [$doc createTextNode $task]
    $item appendChild $text

    $list appendChild $item
  }

  set count 0
  set labels [list]

  for {set node [$list firstChild]} {$node ne ""} {set node [$node nextSibling]} {
    incr count
    lappend labels [$node textContent]
  }

  return [list $count $labels]
}

###############################################################################

#
# domTraverse --
#
#   Tree traversal: walk a list's children via firstChild / nextSibling
#   collecting tag names, then climb back up with parentNode.  Returns
#   {childTags parentTag}.
#
proc ::examples::domTraverse {} {
  set doc [dom::document]
  set menu [$doc getElementById menu]

  set tags [list]

  for {set node [$menu firstChild]} {$node ne ""} {set node [$node nextSibling]} {
    lappend tags [$node tagName]
  }

  set parentTag [[[$menu firstChild] parentNode] tagName]

  return [list $tags $parentTag]
}

###############################################################################

#
# onActivate --
#
#   Event handler: invoked with the event dictionary the bridge builds
#   (type / bubbles / cancelable / eventPhase / target / currentTarget /
#   timeStamp).  Reads the event type and the target element's label,
#   then writes a status message back into the DOM.
#
proc ::examples::onActivate { event } {
  set doc [dom::document]

  set type [dict get $event type]
  set target [dict get $event target]
  set label [$target getAttribute data-label]

  set status [$doc getElementById status]
  $status textContent "$type on $label"

  dom::console log "handled $type on $label"
}

###############################################################################

#
# domEvents --
#
#   Event wiring: subscribe [onActivate] to a button's click event.
#   Returns the status element's handle so a caller can read the
#   message the handler writes once the event fires.
#
proc ::examples::domEvents {} {
  set doc [dom::document]

  set button [$doc getElementById go]
  $button addEventListener click ::examples::onActivate

  return [$doc getElementById status]
}

###############################################################################

#
# domCrossEval --
#
#   Cross-eval: call into the page's JavaScript realm via dom::eval_js
#   (requires the "cross-eval" TH8-Script-Policy directive) and return
#   the string result.
#
proc ::examples::domCrossEval {} {
  return [dom::eval_js {document.title}]
}
