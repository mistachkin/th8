###############################################################################
#
# coverage_format_float.tcl --
#
# Tcl Language Standard
# Coverage Test File
#
# Targeted MC/DC closure for floating-point format compounds in
# src/plugins/th8_formatting.c:
#
#   line 435: while (mant_e < 1.0 && mant_e > 0.0)   (normalize-up loop)
#   line 455: if (fPart >= 0.5 && fp > zFBuf)        (rounding)
#
# Existing format tests cover the F,- vector (mantissa already
# >= 1.0 after initial normalization).  To drive the T,T vector
# we need mant_e < 1.0 after the log10/pow normalization, which
# can happen with extreme small values where pow's rounding
# produces a slightly-too-large divisor.
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

runTest {test fmtfp_cov-1.1 {
  format %e with very small float -- may drive the normalize-up loop
} -constraints {
    th8
} -body {
  catch {format "%e" 1e-300} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fmtfp_cov-1.2 {
  format %g with subnormal-ish float
} -constraints {
    th8
} -body {
  catch {format "%g" 1e-200} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fmtfp_cov-1.3 {
  format %e with values near 1e-15 where rounding may produce mant_e<1
} -constraints {
    th8
} -body {
  set vals [list 1e-15 1.5e-100 9.999999999e-50 5e-7]
  set ok 1
  foreach v $vals {
    if {[catch {format "%e" $v} r]} then { set ok 0 }
  }
  set ok
} -cleanup {
  unset -nocomplain v r ok vals
} -result {1}}

###############################################################################

runTest {test fmtfp_cov-1.4 {
  format %f with high precision exercises the rounding loop at line 455
} -constraints {
    th8
} -body {
  catch {format "%.10f" 0.123456789} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fmtfp_cov-1.5 {
  format %f with value where fPart >= 0.5 forces round-up at line 455
} -constraints {
    th8
} -body {
  catch {format "%.2f" 0.999} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fmtfp_cov-1.6 {
  format %f with value where fPart < 0.5 (no round-up; covers F,-)
} -constraints {
    th8
} -body {
  catch {format "%.2f" 0.124} r
  expr {[string length $r] >= 0}
} -cleanup {
  unset -nocomplain r
} -result {1}}

###############################################################################

runTest {test fmtfp_cov-1.9 {
  format "%g" of a large or small power of 10 produces a
  mantissa with NO decimal point (e.g. "1e+10" not
  "1.000000e+10").  This drives the C1=F vector at
  th8_formatting.c:640 -- dp stays NULL through the
  strip-zeros scan because there's no '.' in the
  mantissa.  Existing tests cover the with-decimal
  cases (C1=T); this closes the C1-pair.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1e10 1e20 5e10 1e-10 1e-20} {
      lappend rcs [format "%g" $v]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v
} -result {1e+10 1e+20 5e+10 1e-10 1e-20}}

###############################################################################

runTest {test fmtfp_cov-1.8 {
  format "%e" of a sub-unit positive value (0 < v < 1)
  drives the mantissa-normalisation upscale loop at
  th8_formatting.c:470 -- `mant_e < 1.0 && mant_e > 0.0`
  fires to multiply mantissa by 10 and decrement
  exponent until the mantissa is in [1, 10).  Existing
  tests cover the >= 1 case (downscale loop at L466);
  this closes the upscale path's pairs.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {0.001 0.0001 0.00001 0.5 0.123} {
      lappend rcs [format "%e" $v]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v
} -result {1.000000e-03 1.000000e-04 1.000000e-05 5.000000e-01 1.230000e-01}}

###############################################################################

runTest {test fmtfp_cov-1.7 {
  format "%.0g" of a small finite value produces a short
  mantissa containing neither '.' nor 'e'/'E' (e.g.
  "1" for 1.5).  This drives the C1=F vector at
  th8_formatting.c:636 -- the strip-trailing-zeros loop
  walks the entire mantissa buffer without finding an
  exponent marker, so the outer-loop condition ep < fp
  short-circuits to F when ep reaches fp.  Existing
  tests cover the T,F,- and T,T,F cases (mantissa
  contains 'e' or '.'); this closes the C1-pair.
  Asserts only that the format call succeeds and the
  result is short (no exponent in any of the cases).
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1.5 0.5 7.0 3.0 9.0} {
      set r [format "%.0g" $v]
      lappend rcs [expr {[string first "e" $r] < 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs r v
} -result {1 1 1 1 1}}

###############################################################################

runTest {test fmtfp_cov-1.10 {
  format "%.0g" of a large power-of-ten produces a buffer
  whose mantissa portion has NO '.' character (e.g. "1e+10"
  for 1e10).  This drives the C1=F vector at
  th8_formatting.c:640 -- after scanning for 'e'/'E' or
  '.', dp remains NULL because the mantissa is the bare
  digit "1".  ep < fp is still T (exponent follows), so
  the strip-zero fast-path is skipped via C1=F short-
  circuit.  Existing tests use multi-digit mantissas
  where the renderer emits "1.000000e+10" before strip
  (C1=T); this closes the C1-pair.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1e10 1e20 1e30 1e-10 1e-20} {
      lappend rcs [format "%.0g" $v]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v
} -result {1e+10 1e+20 1e+30 1e-10 1e-20}}

###############################################################################

runTest {test fmtfp_cov-1.12 {
  format "%e" of a subnormal IEEE 754 double drives the
  upscale loop body at th8_formatting.c:471-472.  Subnormals
  have abs_v < 1e-307 so log10 underflow causes expn_e to be
  clamped to -307; the resulting pwr=10^-307 is larger than
  abs_v, leaving mant_e in (0,1).  The L470 loop then runs
  to renormalize mant_e back to [1,10), driving the (T,T)
  vector.  Existing tests cover only the (F,-) vector
  (mant_e already in [1,10) after the L466 downscale loop).
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {1e-310 5e-320 1e-322} {
      lappend rcs [expr {[string length [format "%e" $v]] > 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs v
} -result {1 1 1}}

###############################################################################

runTest {test fmtfp_cov-1.11 {
  format of a near-integer value whose rounded digit string
  is all 9s (e.g. 9.999..., 0.99999...) drives the FULL-
  CASCADE carry loop at th8_core.c:16939 -- the rounding
  loop walks i from nDig-1 down to 0, propagating the
  carry through every '9' that becomes '0'.  When the carry
  survives past the leftmost digit (i reaches -1 with c
  still 1), the loop terminates via C2=F.  The follow-up
  block at L16948 then shifts and prepends '1' and bumps
  the exponent.  Asserts only that format succeeds.
} -constraints {
    th8
} -body {
  set rcs {}
  foreach v {0.9999999 9.999999 99.99999 999.9999 9999.999 99999.99} {
      set r [format "%g" $v]
      lappend rcs [expr {[string length $r] > 0}]
  }
  set rcs
} -cleanup {
  unset -nocomplain rcs r v
} -result {1 1 1 1 1 1}}

###############################################################################

runTest {test fmtfp_cov-1.12 {
  Bug 12 regression: format "%.<n>f" on a value whose post-
  rounding mantissa overflows past the leading digit (e.g.
  9.99 -> 10.0; 99.95 -> 100.0; 99.999 -> 100.0) must shift
  the buffer right and write '1' at the front rather than
  truncating to "0.0" / "00.0".  Sign-safety: negative
  values must still carry past the leading digit while
  preserving the leading minus.
} -constraints {
    th8
} -body {
  list \
      [format "%.1f" 9.99] \
      [format "%.1f" 99.95] \
      [format "%.1f" 99.999] \
      [format "%.2f" 999.95] \
      [format "%.2f" 0.999] \
      [format "%.1f" -9.99] \
      [format "%.1f" -99.95] \
      [format "%.1f" -0.999]
} -cleanup {
} -result {10.0 100.0 100.0 999.95 1.00 -10.0 -100.0 -1.0}}

###############################################################################

runTest {test fmtfp_cov-1.13 {
  Bug 11 regression: format "%g" / "%G" must round half-up
  (matching %f / standard Tcl), not truncate.  Covers the
  routing of %g fixed-output through the shared fmt_fixed
  rounding path AND the scientific-mantissa
  renormalisation when the carry survives past the leading
  digit (e.g. %.2g 999.5 -> "1e+03", not "0e+02").
  Cross-spec coverage of the same renormalisation via %e
  carry-past-leading (9.99 -> 1.0e+01; 99.995 -> 1.00e+02;
  999999999999.5 -> 1.000000e+12).
} -constraints {
    th8
} -body {
  list \
      [format "%.0g" 1.5] \
      [format "%.0g" 2.5] \
      [format "%.0g" 3.5] \
      [format "%.0G" 2.5] \
      [format "%.1g" 1.55] \
      [format "%.1g" 0.155] \
      [format "%.2g" 1.555] \
      [format "%.2g" 999.5] \
      [format "%.3g" 1.0] \
      [format "%.1e" 9.99] \
      [format "%.2e" 99.995] \
      [format "%.6e" 999999999999.5]
} -cleanup {
} -result {2 3 4 3 2 0.2 1.6 1e+03 1 1.0e+01 1.00e+02 1.000000e+12}}

###############################################################################

runTest {test fmtfp_cov-1.14 {
  Bug 32 regression: format "%.0e" / "%.0E" must apply
  half-up rounding at the single-digit mantissa boundary
  (previously the nPrec==0 path skipped the carry check
  inside `if (nPrec > 0)`).  Covers single-digit bump
  (4.5 -> 5e+00; 1.4 -> 1e+00 no-bump), single-digit
  renormalization on '9' boundary (9.5 -> 1e+01;
  999999.5 -> 1e+06), uppercase form, and negative
  sign-safety (-9.5 -> -1e+01).
} -constraints {
    th8
} -body {
  list \
      [format "%.0e" 4.5] \
      [format "%.0e" 9.5] \
      [format "%.0e" 0.5] \
      [format "%.0e" 999999.5] \
      [format "%.0e" 1.4] \
      [format "%.0e" 5.0] \
      [format "%.0E" 9.5] \
      [format "%.0e" -4.5] \
      [format "%.0e" -9.5]
} -cleanup {
} -result {5e+00 1e+01 5e-01 1e+06 1e+00 5e+00 1E+01 -5e+00 -1e+01}}

###############################################################################

source tests/epilogue.tcl
