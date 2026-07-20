###############################################################################
#
# dom_bridge_mock.tcl --
#
# TH8 Example Scripts (LadyBird DOM integration)
# Test-only mock of the LadyBird DOM bridge command surface.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

#
# NOTE: This file is NOT part of the shipped LadyBird examples.  It is a
#       faithful, in-memory re-implementation of the DOM command surface
#       that LadyBird's C++ bridge (register_dom_commands in
#       Libraries/LibWeb/TH8/DOMBridge.cpp) registers, so the example
#       scripts under this directory can be smoke-tested with ./bin/th8sh
#       without a browser.  It mirrors the real contract:
#
#         dom::document ?subcommand ...?     ;# no args -> document handle
#         dom::console  log|warn|error msg
#         dom::release  handle
#         dom::eval_js  script               ;# cross-eval (stubbed)
#
#       and, on any node/element handle command:
#
#         getElementById createElement createTextNode body head title
#           (document handle only)
#         querySelector getAttribute setAttribute removeAttribute
#           hasAttribute tagName textContent innerHTML appendChild
#           removeChild insertBefore parentNode firstChild lastChild
#           nextSibling previousSibling addEventListener
#
#       The document handle carries BOTH document- and node-scoped
#       subcommands -- matching the DOMBridge.cpp fix that delegates
#       document ops from object_ensemble_command when the handle is the
#       Document, so `set doc [dom::document]; $doc getElementById x`
#       works.  Event dispatch is synchronous via [::dommock::dispatch].
#

namespace eval ::dommock {
  variable seq 0
  variable docHandle ""
  variable bodyHandle ""
  variable headHandle ""

  #
  # reset --
  #
  #   Rebuilds the base document tree (#document -> html -> {head,
  #   body}) and registers the dom::* ensemble commands.  Call once
  #   before building a fixture.
  #
  #   Handles are monotonic and never reused across resets, so a fresh
  #   node cannot inherit a previous node's text / children / attribute
  #   state.  This sidesteps a TH8 quirk: array unset / unset / array
  #   names on a namespace-qualified WHOLE-array name do not resolve
  #   (only per-element access does), so the arrays cannot be cleared
  #   wholesale here.  Element ids resolve within the current tree
  #   because setAttribute id overwrites the idmap entry.
  #
  proc reset {} {
    variable docHandle
    variable bodyHandle
    variable headHandle

    set docHandle [newHandle #document]
    set html [newHandle html]
    set headHandle [newHandle head]
    set bodyHandle [newHandle body]

    appendKid $docHandle $html
    appendKid $html $headHandle
    appendKid $html $bodyHandle

    registerCommands
  }
}

###############################################################################

namespace eval ::dommock {
  #
  # newHandle --
  #
  #   Allocates a fresh handle for a node of the given TAG, initializes
  #   its child list, and binds a global command of the handle's name
  #   that forwards to [::dommock::nodeDispatch].  Returns the handle.
  #
  proc newHandle { tag } {
    variable seq

    set h obj[incr seq]

    set ::dommock::tag($h) $tag
    set ::dommock::kids($h) [list]

    proc ::$h { args } [string map [list @H@ $h] {
      return [eval [list ::dommock::nodeDispatch @H@] $args]
    }]

    return $h
  }

  #
  # isDocument --
  #
  #   Returns non-zero when handle H is the document node.
  #
  proc isDocument { h } {
    return [expr {$h eq $::dommock::docHandle}]
  }

  #
  # appendKid --
  #
  #   Low-level tree link: appends CHILD to PARENT's child list and
  #   sets CHILD's parent pointer.  Used by [reset] and by the
  #   appendChild subcommand.
  #
  proc appendKid { parent child } {
    catch {detachKid $child}

    lappend ::dommock::kids($parent) $child
    set ::dommock::parent($child) $parent
  }

  #
  # detachKid --
  #
  #   Low-level tree unlink: removes CHILD from its current parent's
  #   child list and clears its parent pointer.
  #
  proc detachKid { child } {
    if {![info exists ::dommock::parent($child)]} then {
      return
    }

    set parent $::dommock::parent($child)
    set index [lsearch -exact $::dommock::kids($parent) $child]

    if {$index >= 0} then {
      set ::dommock::kids($parent) \
          [lreplace $::dommock::kids($parent) $index $index]
    }

    unset ::dommock::parent($child)
  }
}

###############################################################################

namespace eval ::dommock {
  #
  # getById --
  #
  #   Returns the handle whose id attribute equals ID, or "" when none
  #   is registered (mirrors getElementById returning an empty result).
  #
  proc getById { id } {
    if {[info exists ::dommock::idmap($id)]} then {
      return $::dommock::idmap($id)
    }

    return ""
  }

  #
  # matchesSel --
  #
  #   Returns non-zero when node H matches the simple selector SEL:
  #   "#id" (id attribute), ".class" (space-separated class token), or
  #   a bare tag name (case-insensitive).  A minimal subset of CSS
  #   sufficient for the examples.
  #
  proc matchesSel { h sel } {
    set first [string index $sel 0]

    if {$first eq "#"} then {
      set want [string range $sel 1 end]

      return [expr {[info exists ::dommock::attr($h,id)] && \
          $::dommock::attr($h,id) eq $want}]
    }

    if {$first eq "."} then {
      set want [string range $sel 1 end]

      if {![info exists ::dommock::attr($h,class)]} then {
        return 0
      }

      return [expr {[lsearch -exact $::dommock::attr($h,class) $want] >= 0}]
    }

    return [expr {[string equal -nocase $::dommock::tag($h) $sel]}]
  }

  #
  # querySelectorFrom --
  #
  #   Returns the first descendant of ROOT (pre-order) matching the
  #   simple selector SEL, or "" when none match.  ROOT itself is not
  #   considered, matching querySelector semantics.
  #
  proc querySelectorFrom { root sel } {
    foreach kid $::dommock::kids($root) {
      if {[matchesSel $kid $sel]} then {
        return $kid
      }

      set deep [querySelectorFrom $kid $sel]

      if {$deep ne ""} then {
        return $deep
      }
    }

    return ""
  }

  #
  # aggregateText --
  #
  #   Returns the concatenated text content of node H and all of its
  #   descendants, mirroring the textContent getter.
  #
  proc aggregateText { h } {
    set result ""

    if {[info exists ::dommock::text($h)]} then {
      append result $::dommock::text($h)
    }

    foreach kid $::dommock::kids($h) {
      append result [aggregateText $kid]
    }

    return $result
  }
}

###############################################################################

namespace eval ::dommock {
  #
  # nodeDispatch --
  #
  #   The per-handle command body.  Implements the element/node
  #   subcommands and, when H is the document, the document-scoped
  #   subcommands as well (getElementById / createElement /
  #   createTextNode / body / head / title).  Errors on an unknown
  #   subcommand, exactly like the real bridge.
  #
  proc nodeDispatch { h subcommand args } {
    if {[isDocument $h]} then {
      switch -exact -- $subcommand {
        getElementById {
          return [getById [lindex $args 0]]
        }
        createElement {
          return [newHandle [lindex $args 0]]
        }
        createTextNode {
          set node [newHandle #text]
          set ::dommock::text($node) [lindex $args 0]

          return $node
        }
        body {
          return $::dommock::bodyHandle
        }
        head {
          return $::dommock::headHandle
        }
        title {
          if {[llength $args] >= 1} then {
            set ::dommock::text(title) [lindex $args 0]

            return [lindex $args 0]
          }

          if {[info exists ::dommock::text(title)]} then {
            return $::dommock::text(title)
          }

          return ""
        }
      }
    }

    ###########################################################################

    switch -exact -- $subcommand {
      tagName {
        return [string toupper $::dommock::tag($h)]
      }
      getAttribute {
        set name [lindex $args 0]

        if {[info exists ::dommock::attr($h,$name)]} then {
          return $::dommock::attr($h,$name)
        }

        return ""
      }
      setAttribute {
        set name [lindex $args 0]
        set value [lindex $args 1]

        set ::dommock::attr($h,$name) $value

        if {$name eq "id"} then {
          set ::dommock::idmap($value) $h
        }

        return ""
      }
      removeAttribute {
        catch {unset ::dommock::attr($h,[lindex $args 0])}

        return ""
      }
      hasAttribute {
        return [expr {[info exists ::dommock::attr($h,[lindex $args 0])] ? 1 : 0}]
      }
      textContent {
        if {[llength $args] >= 1} then {
          foreach kid $::dommock::kids($h) {
            detachKid $kid
          }

          set ::dommock::kids($h) [list]
          set ::dommock::text($h) [lindex $args 0]

          return ""
        }

        return [aggregateText $h]
      }
      innerHTML {
        if {[llength $args] >= 1} then {
          set ::dommock::attr($h,__html) [lindex $args 0]

          return ""
        }

        if {[info exists ::dommock::attr($h,__html)]} then {
          return $::dommock::attr($h,__html)
        }

        return ""
      }
      querySelector {
        return [querySelectorFrom $h [lindex $args 0]]
      }
      appendChild {
        appendKid $h [lindex $args 0]

        return [lindex $args 0]
      }
      removeChild {
        detachKid [lindex $args 0]

        return [lindex $args 0]
      }
      insertBefore {
        set new [lindex $args 0]
        set ref [lindex $args 1]

        catch {detachKid $new}

        if {$ref eq ""} then {
          lappend ::dommock::kids($h) $new
        } else {
          set index [lsearch -exact $::dommock::kids($h) $ref]

          if {$index < 0} then {
            lappend ::dommock::kids($h) $new
          } else {
            set ::dommock::kids($h) \
                [linsert $::dommock::kids($h) $index $new]
          }
        }

        set ::dommock::parent($new) $h

        return $new
      }
      parentNode {
        if {[info exists ::dommock::parent($h)]} then {
          return $::dommock::parent($h)
        }

        return ""
      }
      firstChild {
        return [lindex $::dommock::kids($h) 0]
      }
      lastChild {
        return [lindex $::dommock::kids($h) end]
      }
      nextSibling {
        return [sibling $h 1]
      }
      previousSibling {
        return [sibling $h -1]
      }
      addEventListener {
        set type [lindex $args 0]
        set proc [lindex $args 1]

        set ::dommock::listen($h,$type) $proc

        return ""
      }
      default {
        error "unknown subcommand \"$subcommand\" for handle \"$h\""
      }
    }
  }

  #
  # sibling --
  #
  #   Returns the handle DELTA positions away from H within its
  #   parent's child list (+1 = next, -1 = previous), or "" when there
  #   is no such sibling.
  #
  proc sibling { h delta } {
    if {![info exists ::dommock::parent($h)]} then {
      return ""
    }

    set parent $::dommock::parent($h)
    set kids $::dommock::kids($parent)
    set index [lsearch -exact $kids $h]

    if {$index < 0} then {
      return ""
    }

    return [lindex $kids [expr {$index + $delta}]]
  }

  #
  # dispatch --
  #
  #   Test-only synchronous event dispatch: if a listener proc is
  #   registered for TYPE on handle H, builds the event dictionary
  #   exactly as serialize_event_to_dict does (type / bubbles /
  #   cancelable / eventPhase / target / currentTarget / timeStamp) and
  #   invokes the proc with it.  Returns 1 when a listener ran, else 0.
  #
  proc dispatch { h type } {
    if {![info exists ::dommock::listen($h,$type)]} then {
      return 0
    }

    set proc $::dommock::listen($h,$type)

    set event [list \
        type $type \
        bubbles 1 \
        cancelable 1 \
        eventPhase 2 \
        target $h \
        currentTarget $h \
        timeStamp 1000]

    eval [list $proc $event]

    return 1
  }
}

###############################################################################

namespace eval ::dommock {
  #
  # registerCommands --
  #
  #   Installs the dom::document / dom::console / dom::release /
  #   dom::eval_js ensemble commands as TH8 procedures, matching the
  #   names the real bridge registers.
  #
  proc registerCommands {} {
    namespace eval ::dom {}

    proc ::dom::document { args } {
      if {[llength $args] == 0} then {
        return $::dommock::docHandle
      }

      return [eval [list ::dommock::nodeDispatch $::dommock::docHandle] $args]
    }

    proc ::dom::console { level message } {
      puts "\[console/$level\] $message"

      return ""
    }

    proc ::dom::release { handle } {
      catch {rename ::$handle ""}

      return ""
    }

    proc ::dom::eval_js { script } {
      #
      # NOTE: A real page evaluates SCRIPT in the JS realm and returns
      #       its result as a string.  The mock cannot run JS, so it
      #       returns a deterministic marker demonstrating the
      #       round-trip shape.
      #
      return "js-result:$script"
    }
  }
}
