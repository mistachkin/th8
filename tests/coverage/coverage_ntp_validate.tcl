###############################################################################
#
# coverage_ntp_validate.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC closure for th8NtpValidateResponse (src/plugins/harpy/
# th8_time.c) -- the NTP-packet protocol-validation logic factored
# out of th8NtpQueryOne so it can be driven directly with crafted
# packets, with NO live NTP exchange (see FINDINGS Finding 022).
#
# ::th8testlib::ntpvalidate flags stratum origTsHex txTsHex reqTxTsHex
# builds a synthetic response (flags=byte0 LI|VN|Mode, stratum=
# byte1, origTs@24, txTs@40) and request (txTs@40), then calls
# th8NtpValidateResponse via the internal stubs.  flags encodes
# version in bits 3-5 and mode in bits 0-2, so
# flags = (version << 3) | mode.  NTP_MODE_SERVER = 4.
#
# Decisions closed:
#   L492 `(version != 3 && version != 4) || mode != NTP_MODE_SERVER`
#        (3 conditions) -- version 4/3 accepted, version 2/5 and
#        wrong mode rejected.
#   L500 `resp.stratum == 0 || resp.stratum > 15` (2 conditions).
#   L511 origTs anti-spoof, L522 zero transmit timestamp.
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

# A well-formed response: version 4, mode 4 (server), stratum 1,
# origTs == req.txTs (no spoof), non-zero txTs.  Baseline (F,-,-)
# vectors for every validation decision.
runTest {test ntpvalidate-1.1 {
  A well-formed NTP response (version 4, mode 4, stratum 1,
  matching origTs, non-zero txTs) validates and yields an integer
  epoch -- the all-conditions-false baseline for L492/L500 and
  the pass side of the origTs / zero-txTs branches.
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 36 1 1111111111111111 2222222222222222 \
      1111111111111111
} -match regexp -result {^-?\d+$}}

###############################################################################

runTest {test ntpvalidate-1.2 {
  Version 3 (flags=28) is also accepted, driving L492 C1=F
  (version==3) -- the short-circuit vector where the version-3
  branch of `version != 3 && version != 4` is false.
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 28 1 1111111111111111 2222222222222222 \
      1111111111111111
} -match regexp -result {^-?\d+$}}

###############################################################################

runTest {test ntpvalidate-2.1 {
  Version 2 (flags=20) drives L492 (T,T,-): version != 3 AND
  version != 4, so the response is rejected.
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 20 1 1111111111111111 2222222222222222 \
      1111111111111111
} -returnCodes 1 -match glob -result {*bad version or mode*}}

###############################################################################

runTest {test ntpvalidate-2.2 {
  Version 5 (flags=44) also rejected (T,T,-) -- the second
  independence vector for L492 C1/C2.
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 44 1 1111111111111111 2222222222222222 \
      1111111111111111
} -returnCodes 1 -match glob -result {*bad version or mode*}}

###############################################################################

runTest {test ntpvalidate-2.3 {
  Version 4 but mode 3 (flags=35) drives L492 (T,F,T): version ok
  but mode != NTP_MODE_SERVER, so the || right arm rejects.
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 35 1 1111111111111111 2222222222222222 \
      1111111111111111
} -returnCodes 1 -match glob -result {*bad version or mode*}}

###############################################################################

runTest {test ntpvalidate-3.1 {
  Stratum 0 (kiss-of-death) drives L500 C1=T (stratum == 0).
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 36 0 1111111111111111 2222222222222222 \
      1111111111111111
} -returnCodes 1 -match glob -result {*invalid stratum*}}

###############################################################################

runTest {test ntpvalidate-3.2 {
  Stratum 16 drives L500 C2=T (stratum > 15).
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 36 16 1111111111111111 2222222222222222 \
      1111111111111111
} -returnCodes 1 -match glob -result {*invalid stratum*}}

###############################################################################

runTest {test ntpvalidate-4.1 {
  origTs != req.txTs drives the anti-spoof branch (L511) -- a
  reflected/replayed packet whose echoed origin timestamp does
  not match the request we sent.
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 36 1 aaaaaaaaaaaaaaaa 2222222222222222 \
      1111111111111111
} -returnCodes 1 -match glob -result {*origTs mismatch*}}

###############################################################################

runTest {test ntpvalidate-5.1 {
  A zero transmit timestamp drives L522 (t3Sec == 0).
} -constraints {
    th8 crypto_enabled
} -body {
  th8testlib::ntpvalidate 36 1 1111111111111111 0000000000000000 \
      1111111111111111
} -returnCodes 1 -match glob -result {*zero transmit timestamp*}}

###############################################################################

source tests/epilogue.tcl
