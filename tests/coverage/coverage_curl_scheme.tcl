###############################################################################
#
# coverage_curl_scheme.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for src/th8_curl.c th8CurlIsValidUri L89-92
# (https://) and L95-98 (http://): scheme-prefix character
# comparisons.
#
# Existing curl_uri tests pass varied URIs via [source], but
# the Th8Shell_curlGetData wrapper at src/th8_shell.c L559-560
# rejects URIs whose first 4 characters are not "http" BEFORE
# delegating to curl.  As a result, the per-character C2-C5=F
# vectors in th8CurlIsValidUri are unreachable from any
# script-level call; only the "http"-prefixed cases (with
# mismatches at positions 4-7) ever reach the curl validator.
#
# ::th8testlib::curl_xgetdata bypasses the shell wrapper and
# calls Th8_GetCurlPlatform()->xGetData directly with an
# arbitrary URI.  th8CurlIsValidUri then sees the raw bytes
# and the per-position scheme-mismatch vectors fire.  No
# network traffic occurs because the validator rejects before
# any fetch attempt.
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

runTest {test curl_scheme-1.0 {
  source on a short relative path (less than 7 chars)
  drives src/th8_shell.c Th8Shell_curlGetData L559 C1=F
  (nName < 7) -- the wrapper falls through to the
  original (Posix) xGetData without examining the URI
  scheme.  Existing tests source longer paths like
  tests/prologue.tcl so the short-path branch was never
  observed.  We use 'xy' (2 chars) which errors with
  "couldn't retrieve"; the rc is incidental.
} -constraints {
    th8
} -setup {
} -body {
  catch {source xy} r
  expr {[string match {*xy*} $r]}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test curl_scheme-1.1 {
  ::th8testlib::curl_xgetdata with each per-position
  scheme-mismatch URI drives the corresponding C2-Cn=F
  vector at th8_curl.c L89-92 (https://) and L95-98
  (http://).  All variants are rejected with "invalid or
  disallowed URI scheme" -- assert each one errors.
} -constraints {
    th8 libcurl
} -setup {
} -body {
  set rcs {}
  foreach uri {
      Xttps://x  hXtps://x  htXps://x  httXs://x
      httpX://x  https_//x  https:_/x  https:/_x
      Xttp://x   hXtp://x   htXp://x   httX://x
      http_//x   http:_/x   http:/_x
      abc://hostname:80/path
      ftp://example.com/file
  } {
      set r [::th8testlib::curl_xgetdata $uri]
      lappend rcs [string match "*rc=1*" $r]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs uri r
} -result {1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1}}

###############################################################################

source tests/epilogue.tcl
