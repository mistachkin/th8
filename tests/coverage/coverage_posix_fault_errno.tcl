###############################################################################
#
# coverage_posix_fault_errno.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Exercises the per-errno / one-shot extension of the POSIX syscall
# fault-injection harness (::th8testlib::posixfaulteval opBit errno
# once script).  The base harness forces a wrapped syscall to fail
# with a fixed errno (EIO) on every armed call; that drives the
# generic error arms but can never reach a branch that inspects errno
# itself -- e.g. the `nRead < 0 && errno == EINTR` retry arms in
# th8PosixGetData's and th8PosixRandomBytes's read loops.  Forcing
# EINTR there with the always-fail base harness would spin forever, so
# the extension adds one-shot mode: fail only the FIRST armed call,
# then pass through, so the retry completes.
#
# Targeted th8_posix.c decisions (both read loops' `nRead < 0 &&
# errno == EINTR`, closing C1 and C2 to 100% MC/DC):
#
#   GETDATA read loop (op 2):
#     - one-shot EINTR: read retried, [source] SUCCEEDS   (C2=T)
#     - default EIO fault: loop breaks, [source] fails     (C1=T, C2=F)
#     - short read (errno -1): read()==0, loop breaks      (C1=F)
#   urandom read loop (op 3):
#     - default EIO fault: break -> PRNG fallback, random() ok (C1=T,C2=F)
#     - one-shot EINTR: read retried, random() ok          (C2=T)
#     - short read (errno -1): read()==0 -> PRNG fallback   (C1=F)
#
# The negative errno (-1) is the harness's short-read sentinel: the
# wrapped read() returns 0 instead of -1.  EINTR is 4 on the macOS/
# Linux hosts where MC/DC coverage runs.
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

runTest {test posixfaulterrno-1.1 {
  GETDATA read op (2) armed with errno EINTR (4) in one-shot mode
  (once=1): the FIRST read() fails with EINTR, driving the
  `nRead < 0 && errno == EINTR` retry arm (continue); the retry
  passes through and completes the read, so [source] SUCCEEDS
  (rc==0).  Contrast posixfaulterrno-1.2 where the default EIO
  fault breaks the loop and [source] fails.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::posixfaulteval 2 4 1 \
      {source tests/helpers/cov_getdata_probe.tcl}} m
} -cleanup {
  unset -nocomplain m
} -result {0}}

###############################################################################

runTest {test posixfaulterrno-1.2 {
  GETDATA read op (2) armed with the default fault (EIO, always):
  read() fails with EIO, so `errno == EINTR` is FALSE, the loop
  breaks, th8PosixGetData returns TH8_ERROR and [source] fails
  (rc==1).  This is the EINTR-arm's F-side companion to 1.1.
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::posixfaulteval 2 \
      {source tests/helpers/cov_getdata_probe.tcl}} m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test posixfaulterrno-1.3 {
  GETDATA read op (2) armed with a SHORT read (negative errno
  sentinel -1): read() returns 0, so the first `nRead > 0` is
  false and `nRead < 0` (C1) is ALSO false -- the else/break arm
  runs with C1=F, the read-loop's `nRead < 0 && errno == EINTR`
  C1-Pair companion to 1.1's C1=T.  th8PosixGetData then reports
  the short read, so [source] fails (rc==1).
} -constraints {
    th8 fault_injection
} -body {
  catch {::th8testlib::posixfaulteval 2 -1 0 \
      {source tests/helpers/cov_getdata_probe.tcl}} m
} -cleanup {
  unset -nocomplain m
} -result {1}}

###############################################################################

runTest {test posixfaulterrno-2.1 {
  urandom read op (3) armed with the default fault (EIO, always):
  the /dev/urandom read() fails with EIO, so the EINTR check is
  FALSE, the loop breaks, and th8PosixRandomBytes fills the tail
  via the PRNG fallback -- random() still returns an integer.
  Drives the read-loop else/break arm and the short-read fallback.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::posixfaulteval 3 {expr {random()}}]
  string is wideinteger -strict $r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test posixfaulterrno-2.2 {
  urandom read op (3) armed with errno EINTR (4) one-shot: the
  first read() fails with EINTR, driving the retry arm (continue);
  the retry passes through and fills the buffer from urandom, so
  random() still returns an integer.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::posixfaulteval 3 4 1 {expr {random()}}]
  string is wideinteger -strict $r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test posixfaulterrno-2.3 {
  urandom read op (3) armed with a SHORT read (negative errno
  sentinel -1): read() returns 0, driving the `nRead == 0` C1=F
  break arm; th8PosixRandomBytes falls back to the PRNG for the
  remaining bytes, so random() still returns an integer.  This is
  the C1-Pair companion to 2.2's C1=T on the urandom read loop.
} -constraints {
    th8 fault_injection
} -body {
  set r [::th8testlib::posixfaulteval 3 -1 0 {expr {random()}}]
  string is wideinteger -strict $r
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

source tests/epilogue.tcl
