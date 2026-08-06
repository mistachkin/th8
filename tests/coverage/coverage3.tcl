###############################################################################
#
# coverage3.tcl --
#
# Tcl Language Standard
# Conformance Test File
#
# Tests for untested requirements across Sections 5, 9, 13, 18, 21, 22, 24,
# and 26 of the Tcl Language Standard.
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
# Section 1 -- Procedures / uplevel (Section 13)
#
###############################################################################

runTest {test coverage3-1.1 {
  R-05612-10993: Level #0 refers to the global frame
} -setup {
} -body {
  proc _cov3_uplevel_global {} {
    uplevel #0 {set _cov3_glob 42}
  }
  _cov3_uplevel_global
  set _cov3_glob
} -cleanup {
  catch {rename _cov3_uplevel_global ""}
  unset -nocomplain _cov3_glob
} -result {42}}

###############################################################################
#
# Section 2 -- I/O / source (Section 18)
#
###############################################################################

runTest {test coverage3-2.1 {
  R-09664-38899: source retrieves script via the platform's data-retrieval
                 callback and evaluates it
} -setup {
} -body {
  source tests/helpers/coverage3_source.tcl
  set ::_cov3_sourced
} -cleanup {
  unset -nocomplain ::_cov3_sourced
} -result {ok}}

###############################################################################
#
# Section 3 -- Namespaces (Section 21)
#
###############################################################################

runTest {test coverage3-3.1 {
  R-00397-49795: Namespace names are separated by ::
} -constraints {
    namespace
} -setup {
} -body {
  namespace eval ::ns1::ns2 {
    proc foo {} {return nested}
  }
  ::ns1::ns2::foo
} -cleanup {
  catch {namespace delete ::ns1}
} -result {nested}}

###############################################################################

runTest {test coverage3-3.2 {
  R-39235-55125: Namespaces form a tree rooted at the global namespace ::
} -constraints {
    namespace namespace_children
} -setup {
} -body {
  namespace eval ::cov3tree {
    namespace eval child {}
  }
  expr {[lsearch [namespace children ::cov3tree] ::cov3tree::child] >= 0}
} -cleanup {
  catch {namespace delete ::cov3tree}
} -result {1}}

###############################################################################

runTest {test coverage3-3.3 {
  R-11940-58702: The global namespace is both the root and the default
                 namespace at the top level
} -constraints {
    namespace
} -body {
  string equal [namespace current] ::
} -result {1}}

###############################################################################

runTest {test coverage3-3.4 {
  R-56076-25034: Variable names follow the same resolution rules when qualified
                 with ::
} -constraints {
    namespace
} -setup {
} -body {
  set ::_cov3_qualvar hello
  namespace eval ::cov3ns {
    set ::_cov3_qualvar
  }
} -cleanup {
  catch {namespace delete ::cov3ns}
  unset -nocomplain ::_cov3_qualvar
} -result {hello}}

###############################################################################

runTest {test coverage3-3.5 {
  R-49982-44031: namespace code returns a script prefix that, when evaluated,
                 will execute script in the current namespace
} -constraints {
    namespace namespace_code
} -setup {
} -body {
  namespace eval ::cov3code {
    proc foo {} {return "cov3code"}
  }
  set p [namespace eval ::cov3code {namespace code foo}]
  eval $p
} -cleanup {
  catch {namespace delete ::cov3code}
  unset -nocomplain p
} -result {cov3code}}

###############################################################################
#
# Section 4 -- Packages (Section 22)
#
###############################################################################

runTest {test coverage3-4.1 {
  R-22252-01822: package present checks whether the named package is already
                 loaded, returning its version if so
} -constraints {
    package package_present
} -setup {
  catch {package forget _cov3_present}
} -body {
  package provide _cov3_present 1.0
  package present _cov3_present
} -cleanup {
  catch {package forget _cov3_present}
} -result {1.0}}

###############################################################################

runTest {test coverage3-4.2 {
  R-09837-41339: package require loads the named package, invoking the
                 registered ifneeded script if necessary
} -constraints {
    package package_ifneeded package_require
} -setup {
  catch {package forget _cov3_req}
} -body {
  package ifneeded _cov3_req 1.0 {package provide _cov3_req 1.0}
  package require _cov3_req
} -cleanup {
  catch {package forget _cov3_req}
} -result {1.0}}

###############################################################################

runTest {test coverage3-4.3 {
  R-32039-29681: package unknown sets or queries the script invoked when
                 package require cannot find a package
} -constraints {
    package package_unknown
} -setup {
  set _cov3_old_unknown [package unknown]
} -body {
  package unknown myhandler
  package unknown
} -cleanup {
  package unknown $_cov3_old_unknown
  unset -nocomplain _cov3_old_unknown
} -result {myhandler}}

runTest {test coverage3-4.4 {
  R-36419-44049: package require -exact requires an exact version match
} -constraints {
    package package_require
} -setup {
  catch {package forget _cov3_exact}
} -body {
  package ifneeded _cov3_exact 2.0 {package provide _cov3_exact 2.0}
  package require -exact _cov3_exact 2.0
} -cleanup {
  catch {package forget _cov3_exact}
} -result {2.0}}

###############################################################################

runTest {test coverage3-4.5 {
  R-36419-44049: package require -exact rejects version mismatch
} -constraints {
    package package_require
} -setup {
  catch {package forget _cov3_exact2}
} -body {
  package ifneeded _cov3_exact2 2.0 {package provide _cov3_exact2 2.0}
  list [catch {package require -exact _cov3_exact2 3.0} msg] $msg
} -cleanup {
  catch {package forget _cov3_exact2}
  unset -nocomplain msg
} -match glob -result {1 *}}

###############################################################################
#
# Section 5 -- Time (Section 24)
#
###############################################################################

runTest {test coverage3-5.1 {
  R-55223-15762: clock seconds returns a wide integer sufficient to represent
                 dates beyond 2038
} -constraints {
    clock
} -body {
  set t [clock seconds]
  list [expr {$t > 0}] [string is integer $t]
} -cleanup {
  unset -nocomplain t
} -result {1 1}}

###############################################################################
#
# Section 6 -- Error Model (Section 26)
#
###############################################################################

runTest {test coverage3-6.1 {
  R-56189-52173: errorInfo accumulates a stack trace as the error propagates up
                 the call stack
} -setup {
} -body {
  #
  # Build a multi-level call chain (a -> b -> c -> error).  After
  # the catch, errorInfo must reference EVERY intermediate frame's
  # proc name.  A non-accumulating implementation that only
  # recorded the innermost frame would fail the per-frame match,
  # even though [string length $errorInfo] would still be > 0.
  # Verifying each frame's name appears is what makes this an
  # accumulation test, not just a non-empty test.
  #
  proc _cov3_err_a {} {error "deep oops"}
  proc _cov3_err_b {} {_cov3_err_a}
  proc _cov3_err_c {} {_cov3_err_b}
  catch {_cov3_err_c} msg
  list [string match {*_cov3_err_a*} $errorInfo] \
      [string match {*_cov3_err_b*} $errorInfo] \
      [string match {*_cov3_err_c*} $errorInfo]
} -cleanup {
  catch {rename _cov3_err_a ""}
  catch {rename _cov3_err_b ""}
  catch {rename _cov3_err_c ""}
  unset -nocomplain msg
} -result {1 1 1}}

###############################################################################

runTest {test coverage3-6.2 {
  R-42886-55673: Each level adds an annotation indicating the command that was
                 executing when the error occurred
} -setup {
} -body {
  proc _cov3_lvl_a {} {error "deep error"}
  proc _cov3_lvl_b {} {_cov3_lvl_a}
  proc _cov3_lvl_c {} {_cov3_lvl_b}
  catch {_cov3_lvl_c} msg
  #
  # The errorInfo should contain references to the nested
  # procedure calls, confirming that each level added an
  # annotation.
  #
  set has_a [string match *_cov3_lvl_a* $errorInfo]
  set has_b [string match *_cov3_lvl_b* $errorInfo]
  set has_c [string match *_cov3_lvl_c* $errorInfo]
  expr {$has_a && $has_b && $has_c}
} -cleanup {
  catch {rename _cov3_lvl_a ""}
  catch {rename _cov3_lvl_b ""}
  catch {rename _cov3_lvl_c ""}
  unset -nocomplain msg
  unset -nocomplain has_a has_b has_c
} -result {1}}

###############################################################################
#
# Section 7 -- UTF-8 Source Encoding (Section 5)
#
###############################################################################

runTest {test coverage3-7.1 {
  R-21609-56922: A conforming implementation shall interpret source text as a
                 sequence of bytes encoded in UTF-8
} -setup {
} -body {
  #
  # U+00E9 (é) is TWO bytes in UTF-8 (0xC3 0xA9).  An implementation
  # that decodes the source as UTF-8 reports [string length] == 1;
  # one that treats the source as Latin-1 / ISO-8859 reports 2.  The
  # parallel "abc" assertion guards against an implementation that
  # returns 1 for every literal regardless of content.
  #
  # TH8 and native Tcl decode script source as UTF-8 by default, so
  # the inline literal is a single code point.  Eagle's default
  # script encoding is (by design) iso-8859-1, so under Eagle the
  # same U+00E9 byte sequence is re-read from a temporary file with
  # [source -encoding utf-8] -- exercising Eagle's UTF-8 decoding via
  # the explicit override.  (An unsigned temp source is fine under
  # Eagle, which does not enforce signatures; TH8's signed-only
  # policy is why TH8 uses the inline literal instead.)
  #
  if {[isEagle]} then {
    set _f _cov3_utf8.tcl
    set _fd [open $_f w]
    fconfigure $_fd -encoding binary -translation binary
    puts -nonewline $_fd [encoding convertto utf-8 [format "set s %c\n" 0xe9]]
    close $_fd
    source -encoding utf-8 $_f
    file delete $_f
  } else {
    set s "é"
  }
  list [string length $s] [string length "abc"]
} -cleanup {
  catch {file delete _cov3_utf8.tcl}
  unset -nocomplain s _f _fd
} -result {1 3}}

###############################################################################
#
# Section 8 -- Integer Overflow (Section 9)
#
###############################################################################

runTest {test coverage3-8.1 {
  R-19715-59368: Integer arithmetic that would overflow the implementation's
                 integer range shall produce an error when overflow checking is
                 enabled
} -constraints {
    bigint_toggle
} -setup {
  #
  # Temporarily disable bigint promotion so the overflow-checking
  # path actually engages.  With bigint enabled, the multiply
  # would silently promote to arbitrary precision and the
  # overflow error would never fire -- making the test
  # unobservable in the build's default configuration.
  # Cleanup re-enables bigint regardless of body outcome.
  #
  ::th8testlib::bigint disable
} -body {
  catch {expr {0x7FFFFFFFFFFFFFFF * 2}}
} -cleanup {
  catch {::th8testlib::bigint enable}
} -result {1}}

###############################################################################

source tests/epilogue.tcl
