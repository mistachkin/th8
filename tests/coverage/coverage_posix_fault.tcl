###############################################################################
#
# coverage_posix_fault.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# MC/DC / branch closure for the raw-syscall failure arms in
# src/th8_posix.c.  Syscalls like open(), fstat(), and read()
# essentially never fail in the test corpus (files exist and read
# fine), so their error arms are otherwise unreachable.  The
# POSIX_CALL() / POSIX_CALL_PTR() wrappers -- armed via
# Th8_FaultConfig.nFailPosixMask and driven by
# ::th8testlib::posixfaulteval -- force the wrapped syscall to
# report failure (-1 / NULL) WITHOUT invoking it, so the error arm
# runs while the surrounding decision's condition text is
# unchanged (MC/DC counts stay honest).  Same facility as the
# OpenSSL shim (nFailOsslMask), applied to POSIX syscalls.
#
# Proof-of-concept: th8PosixGetData's open/fstat/read ops, driven
# through a [source] of a signed file.  Additional th8_posix.c
# syscall sites are added incrementally.
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

runTest {test posixfault-1.1 {
  Arming each th8PosixGetData syscall op (open=0, fstat=1,
  read=2) in turn and sourcing a signed file forces that syscall
  to fail, driving its error arm (open<0 / fstat!=0 / read short)
  so th8PosixGetData returns TH8_ERROR and [source] reports
  "couldn't retrieve".  Every armed op must make the source error
  (rc==1); `fails` is the set that did not -- expected empty.
} -constraints {
    th8 fault_injection
} -body {
  set fails [list]
  foreach op {0 1 2} {
    set rc [catch {::th8testlib::posixfaulteval $op {source tests/prologue.tcl}} m]
    if {$rc != 1} then { lappend fails "op$op=rc$rc" }
  }
  set fails
} -cleanup {
  unset -nocomplain fails rc m op
} -result {}}

###############################################################################

runTest {test posixfault-1.2 {
  Sourcing a DIRECTORY (a relative path under the sandbox base)
  drives th8PosixGetData's fstat compound C2 vector (F,T):
  fstat() succeeds (C1=F) but S_ISREG is false for a directory
  (C2=T), so the read is rejected with "couldn't retrieve".
  Combined with posixfault-1.1's fault-driven C1 (T,-) and the
  normal regular-file baseline (F,F), this closes the
  `fstat(fd,&st) != 0 || !S_ISREG(st.st_mode)` two-condition
  decision to 100% MC/DC.  No fault needed -- the file *type* is
  the driver.
} -constraints {
    th8
} -body {
  catch {source tests} m
  set m
} -cleanup {
  unset -nocomplain m
} -match glob -result {couldn't retrieve*}}

###############################################################################

source tests/epilogue.tcl
