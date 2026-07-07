###############################################################################
#
# coverage_curl_uri.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for th8CurlIsValidUri's scheme-prefix
# decisions in src/th8_curl.c L84-93.  The function validates
# that a URI starts with "https://" or "http://" via a flat
# per-character compound.
#
# Existing curl tests only call with valid https://... URIs
# (all sub-conditions T).  This file exercises each
# per-character mismatch and the http://-vs-https:// split so
# MC/DC vectors for each character position fire at least once.
#
# Coverage-driven; not pinned to specific R-markers.
#
# Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###############################################################################

source tests/prologue.tcl

###############################################################################

runTest {test curl_uri-1.1 {
  source with a valid http:// URI drives the second
  scheme-check compound at th8_curl.c:L90 (the
  http://-prefix path) -- all 8 sub-conditions evaluate
  T.  Existing curl tests use https://, so this is the
  first time the http://-branch is exercised.
} -constraints {
    th8 libcurl
} -body {
  catch {source "http://this.host.does.not.exist.invalid/x"} msg
  expr {[string length $msg] > 0}
} -cleanup {
  unset -nocomplain msg
} -result {1}}

###############################################################################

runTest {test curl_uri-1.2 {
  source with various INVALID URIs drives the C2-Cn=F
  vectors at th8_curl.c:L84 -- each URI has at least one
  character that mismatches "https://" or "http://" at a
  different position, so different per-character sub-
  conditions evaluate F in turn.  Each catch returns an
  error; we only assert that the call doesn't crash.
} -constraints {
    th8 libcurl
} -body {
  set rcs {}
  foreach uri {
      xttps://x  yttps://x  zttps://x
      hxtps://x  hxxps://x  htxps://x
      httxs://x  http_://x  https_/x
      https:_/x  https:/_x
      htps://x   xtp://x    abc://path
      ftp://server.example.com/file
      file:///etc/passwd
      xxxxxxxxxxxxxxxx
      http:Xy/x  http:Xy/   http:/Xx
      http:/X/x  http://_x
  } {
      lappend rcs [catch {source $uri} m]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs uri m
} -result {1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1}}

###############################################################################

runTest {test curl_uri-2.1 {
  source with a URI that has an EXPLICIT PORT
  (http://host:port/path) drives the C3-pair vector at
  th8_curl.c:L319 -- the host-name scanner walks until
  it hits ':' (port separator).  Existing curl tests
  only use bare http(s)://host/path which trips on '/'
  (C2=F) before reaching the colon.
} -constraints {
    th8 libcurl
} -body {
  set rcs {}
  lappend rcs [catch {source "http://this.host.does.not.exist.invalid:8080/x"} m]
  lappend rcs [catch {source "https://this.host.does.not.exist.invalid:443/x"} m]
  lappend rcs [catch {source "http://this.host.does.not.exist.invalid:1/x"} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1 1}}

###############################################################################

runTest {test curl_uri-2.2 {
  source with a URI that has a QUERY STRING immediately
  after the host (http://host?q) drives the C4-pair
  vector at th8_curl.c:L319 -- the host-name scanner
  walks until it hits '?' (query separator).
} -constraints {
    th8 libcurl
} -body {
  set rcs {}
  lappend rcs [catch {source "http://this.host.does.not.exist.invalid?q=1"} m]
  lappend rcs [catch {source "https://this.host.does.not.exist.invalid?foo=bar"} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

runTest {test curl_uri-2.3 {
  source with a URI that has an EXPLICIT PORT followed
  by a NON-DIGIT character that is ASCII > '9' (e.g.
  '?', '@', '/') drives the C2=F vector at th8_curl.c:
  L330 -- inside the port-number scanner, *p >= '0' is
  T (the terminator char is ASCII >= 48) but *p <= '9'
  is F.  Existing port tests use '/' as terminator
  (ASCII 47 < '0', C1=F); this closes the C2-pair via
  a terminator > '9'.
} -constraints {
    th8 libcurl
} -body {
  set rcs {}
  lappend rcs [catch {source "http://this.host.does.not.exist.invalid:80?q=1"} m]
  lappend rcs [catch {source "https://this.host.does.not.exist.invalid:443?x=y"} m]
  set rcs
} -cleanup {
  unset -nocomplain rcs m
} -result {1 1}}

###############################################################################

source tests/epilogue.tcl
