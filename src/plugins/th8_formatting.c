/*
 * th8_formatting.c -- Formatting plugin for TH8.
 *
 * Implements the formatting commands: format and scan.
 *
 * This file is part of the plugin architecture.  The commands
 * are registered via Th8_RegisterPlugin using the static
 * command table returned by th8FormattingGetCommands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_util.h"
#include "th8_plugin.h"

#if defined(TH8_PLUGIN_FORMATTING)


/*
 *----------------------------------------------------------------------
 *
 * format_command --
 *
 *	Format a string according to %-specifiers.
 *
 *	format FORMATSTRING ?ARG ...?
 *
 *	Supports: %d/%i, %u, %o, %x/%X, %c, %s, %f, %e/%E, %g/%G,
 *	and %%.  Width, precision, and flag characters (-, +, 0,
 *	space, #) are handled.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on bad specifier or missing args.
 *
 * Why / How:
 *	Implements the Tcl [format] command.  Walks the format string
 *	character-by-character, parsing flags, width, precision, and
 *	specifier for each %-field.  Each specifier converts the next
 *	argument from the argv list and appends the formatted result
 *	to an accumulator buffer.  Padding and alignment are applied
 *	through a common emit path shared by all specifiers.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
format_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zFmt;
    size_t nFmt;
    size_t i;
    int iArg = 2;  /* Next argument to consume */
    char *zOut = 0;
    size_t nOut = 0;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "format formatString ?arg ...?");
    }
    zFmt = argv[1];
    nFmt = TH8_LEN(argl[1]);

    for (i = 0; i < nFmt; i++) {
	if (Th8_Ready(interp) != TH8_OK) {
	    Th8_Free(interp, zOut);
	    return TH8_ERROR;
	}
	if (zFmt[i] != '%') {
	    TH8_STR_APPEND(interp, &zOut, &nOut, &zFmt[i], 1);
	    continue;
	}
	i++;  /* skip '%' */
	if (i >= nFmt) break;

	/*
	 * Literal %%.
	 */

	if (zFmt[i] == '%') {
	    TH8_STR_APPEND(interp, &zOut, &nOut, "%", 1);
	    continue;
	}

	/*
	 * Parse flags.
	 */

	{
	    int flagMinus = 0;
	    int flagPlus = 0;
	    int flagZero = 0;
	    int flagSpace = 0;
	    int flagHash = 0;
	    int width = 0;
	    int prec = -1;
	    int havePrec = 0;
	    char spec;
	    char zNum[80];
	    size_t nNum = 0;

	    while (i < nFmt) {
		if (zFmt[i] == '-')
		    flagMinus = 1;
		else if (zFmt[i] == '+')
		    flagPlus = 1;
		else if (zFmt[i] == '0')
		    flagZero = 1;
		else if (zFmt[i] == ' ')
		    flagSpace = 1;
		else if (zFmt[i] == '#')
		    flagHash = 1;
		else
		    break;
		i++;
	    }

	    /*
	     * Width (* = from argument).
	     */

	    if (i < nFmt && zFmt[i] == '*') {
		if (iArg >= argc) {
		    goto not_enough;
		}
		Th8_ToInt(interp, argv[iArg], argl[iArg], &width);
		if (width < 0) {
		    flagMinus = 1;
		    width = -width;
		}
		iArg++;
		i++;
	    } else {
		while (i < nFmt && zFmt[i] >= '0' && zFmt[i] <= '9') {
		    width = width * 10 + (zFmt[i] - '0');
		    /* Cap at 10000 -- legitimate format widths
		     * are typically <= a few hundred; the 10K
		     * ceiling keeps the per-spec allocator
		     * bounded and prevents AFL "hang" verdicts
		     * on inputs that combine many wide specs
		     * (each %99999c produces ~100KB which under
		     * ASan+AFL instrumentation blows past the
		     * fuzz-harness timeout).  Test in
		     * sandbox-resource-5.1 still passes because
		     * its `%999999d` is well above the cap. */
		    if (width > 10000) {
			Th8_SetResultStatic(
			    interp, "format width too large", TH8_NOLEN);
			return TH8_ERROR;
		    }
		    i++;
		}
	    }

	    /*
	     * Precision.
	     */

	    if (i < nFmt && zFmt[i] == '.') {
		i++;
		havePrec = 1;
		prec = 0;
		if (i < nFmt && zFmt[i] == '*') {
		    if (iArg >= argc) {
			goto not_enough;
		    }
		    Th8_ToInt(interp, argv[iArg], argl[iArg], &prec);
		    iArg++;
		    i++;
		} else {
		    while (i < nFmt && zFmt[i] >= '0' && zFmt[i] <= '9') {
			prec = prec * 10 + (zFmt[i] - '0');
			i++;
		    }
		}
		if (prec > 10000) {
		    /* Match the per-spec width cap.  Caps that
		     * exceed this would re-enable the fuzz-hang
		     * profile (instrumented format calls running
		     * past 1s on hyper-wide precision). */
		    prec = 10000;
		}
	    }

	    if (i >= nFmt) {
		Th8_SetResultStatic(
		    interp,
		    "format string ended in middle"
		    " of field specifier",
		    TH8_NOLEN);
		Th8_Free(interp, zOut);
		return TH8_ERROR;
	    }
	    spec = zFmt[i];

	    switch (spec) {
	    case 'd':
	    case 'i': {
		th8_int64_t val;

		if (iArg >= argc) goto not_enough;
		if (Th8_ToWideInt(interp, argv[iArg], argl[iArg], &val) !=
		    TH8_OK) {
		    Th8_Free(interp, zOut);
		    return TH8_ERROR;
		}
		iArg++;

		/*
		 * Build the number string with optional
		 * '+' or ' ' prefix.
		 */

		Th8_SetResultWideInt(interp, val);
		{
		    size_t n;
		    const char *z = Th8_GetResult(interp, &n);
		    char *p = zNum;

		    if (z[0] != '-') {
			if (flagPlus) {
			    *p++ = '+';
			} else if (flagSpace) {
			    *p++ = ' ';
			}
		    }
		    while (n > 0 &&
		           ALWAYS((size_t)(p - zNum) < sizeof(zNum) - 1)) {
			*p++ = *z++;
			n--;
		    }
		    *p = 0;
		    nNum = (size_t)(p - zNum);
		}
		goto fmt_pad_and_emit;
	    }
	    case 'u': {
		th8_int64_t val;

		if (iArg >= argc) goto not_enough;
		if (Th8_ToWideInt(interp, argv[iArg], argl[iArg], &val) !=
		    TH8_OK) {
		    Th8_Free(interp, zOut);
		    return TH8_ERROR;
		}
		iArg++;
		{
		    th8_uint64_t u = (th8_uint64_t)val;
		    char *p = &zNum[sizeof(zNum) - 1];

		    *p = 0;
		    do {
			*(--p) = (char)('0' + (int)(u % 10));
			u /= 10;
		    } while (u > 0);
		    nNum = (size_t)(&zNum[sizeof(zNum) - 1] - p);
		    {
			size_t j;
			for (j = 0; j < nNum; j++) {
			    zNum[j] = p[j];
			}
			zNum[nNum] = 0;
		    }
		}
		goto fmt_pad_and_emit;
	    }
	    case 'o': {
		th8_int64_t val;

		if (iArg >= argc) goto not_enough;
		if (Th8_ToWideInt(interp, argv[iArg], argl[iArg], &val) !=
		    TH8_OK) {
		    Th8_Free(interp, zOut);
		    return TH8_ERROR;
		}
		iArg++;
		{
		    th8_uint64_t u = (th8_uint64_t)val;
		    char *p = &zNum[sizeof(zNum) - 1];

		    *p = 0;
		    do {
			*(--p) = (char)('0' + (int)(u & 7));
			u >>= 3;
		    } while (u > 0);
		    if (flagHash && *p != '0') {
			*(--p) = '0';
		    }
		    nNum = (size_t)(&zNum[sizeof(zNum) - 1] - p);
		    {
			size_t j;
			for (j = 0; j < nNum; j++) {
			    zNum[j] = p[j];
			}
			zNum[nNum] = 0;
		    }
		}
		goto fmt_pad_and_emit;
	    }
	    case 'x':
	    case 'X': {
		th8_int64_t val;
		const char *zHex;

		if (iArg >= argc) goto not_enough;
		zHex = (spec == 'x') ? "0123456789abcdef"
		                     : "0123456789ABCDEF";
		if (Th8_ToWideInt(interp, argv[iArg], argl[iArg], &val) !=
		    TH8_OK) {
		    Th8_Free(interp, zOut);
		    return TH8_ERROR;
		}
		iArg++;
		{
		    th8_uint64_t u = (th8_uint64_t)val;
		    char *p = &zNum[sizeof(zNum) - 1];

		    *p = 0;
		    do {
			*(--p) = zHex[u & 0xF];
			u >>= 4;
		    } while (u > 0);
		    if (flagHash) {
			*(--p) = (spec == 'x') ? 'x' : 'X';
			*(--p) = '0';
		    }
		    nNum = (size_t)(&zNum[sizeof(zNum) - 1] - p);
		    {
			size_t j;
			for (j = 0; j < nNum; j++) {
			    zNum[j] = p[j];
			}
			zNum[nNum] = 0;
		    }
		}
		goto fmt_pad_and_emit;
	    }
	    case 'c': {
		th8_int64_t val;
		int nBytes;

		if (iArg >= argc) goto not_enough;
		if (Th8_ToWideInt(interp, argv[iArg], argl[iArg], &val) !=
		    TH8_OK) {
		    Th8_Free(interp, zOut);
		    return TH8_ERROR;
		}
		iArg++;
		nBytes = Th8_Utf8Encode((int)val, zNum);
		zNum[nBytes] = 0;
		nNum = (size_t)nBytes;
		goto fmt_pad_and_emit;
	    }
	    case 's': {
		size_t nVal;
		const char *zVal;

		if (iArg >= argc) goto not_enough;
		zVal = argv[iArg];
		nVal = TH8_LEN(argl[iArg]);
		if (havePrec && prec >= 0 && (size_t)prec < nVal) {
		    nVal = (size_t)prec;
		}
		iArg++;

		/*
		 * Copy to zNum for width/padding.
		 */

		if (nVal >= sizeof(zNum)) {
		    nVal = sizeof(zNum) - 1;
		}
		{
		    size_t j;
		    for (j = 0; j < nVal; j++) {
			zNum[j] = zVal[j];
		    }
		    zNum[nVal] = 0;
		    nNum = nVal;
		}
		goto fmt_pad_and_emit;
	    }
	    case 'f':
	    case 'e':
	    case 'E':
	    case 'g':
	    case 'G': {
		double rVal;
		char zFBuf[80];
		char *fp = zFBuf;
		int nPrec;
		int neg;
		double abs_v;
		th8_int64_t iPart;
		double fPart;
		int k;
		int expn_e = 0;
		double mant_e = 0.0;
		int stripGZeros = 0;
		int stripGFixed = 0;

		if (iArg >= argc) goto not_enough;
		if (Th8_ToDouble(interp, argv[iArg], argl[iArg], &rVal) !=
		    TH8_OK) {
		    Th8_Free(interp, zOut);
		    return TH8_ERROR;
		}
		iArg++;
		nPrec = havePrec ? prec : 6;
		if (nPrec < 0) nPrec = 0;
		if (nPrec > 40) nPrec = 40;

		neg = (rVal < 0.0);
		abs_v = neg ? -rVal : rVal;

		/*
		 * IEEE 754 non-finite short-circuit.  NaN compares
		 * unequal to itself; (+/-)Inf doubles to itself but
		 * is non-zero.  Without this, the %e mantissa-
		 * normalisation loop below would not terminate when
		 * abs_v is Inf (Inf / 10.0 == Inf so the loop never
		 * exits), and analogous overflow paths in %g / %f
		 * would spin.  Emit "NaN", "Inf", or "-Inf" literally
		 * and skip the digit-print loops entirely.  See
		 * doc/internal/incomplete.md Sec. 1b for history.
		 */
		if (rVal != rVal || (rVal != 0.0 && rVal + rVal == rVal)) {
		    const char *zLit;
		    size_t nLit;
		    size_t j;

		    if (rVal != rVal) {
			zLit = "NaN";
			nLit = 3;
		    } else if (neg) {
			zLit = "-Inf";
			nLit = 4;
		    } else {
			zLit = "Inf";
			nLit = 3;
		    }
		    for (j = 0; j < nLit; j++) {
			zNum[j] = zLit[j];
		    }
		    zNum[nLit] = 0;
		    nNum = nLit;
		    goto fmt_pad_and_emit;
		}

		if (spec == 'e' || spec == 'E') {
fmt_scientific:
		    /*
		     * Scientific notation: M.DDDe+EE
		     */

		    expn_e = 0;
		    mant_e = abs_v;

		    if (abs_v != 0.0) {
			double lg;

			if (th8MathOp(
			        interp, TH8_MATH_LOG10, &lg, abs_v, 0.0) ==
			    TH8_OK) {
			    if (lg < 0.0) {
				lg -= 1.0;
			    }
			    expn_e = (int)lg;
			    if (expn_e < -307) expn_e = -307;
			    if (expn_e > 307) expn_e = 307;
			    {
				double pwr;

				th8MathOp(
				    interp, TH8_MATH_POW, &pwr, 10.0,
				    (double)expn_e);
				if (pwr != 0.0) mant_e = abs_v / pwr;
			    }
			    while (mant_e >= 10.0) {
				mant_e /= 10.0;
				expn_e++;
			    }
			    /* Split per Finding 005 sec. 5b: mant_e
			     * == abs_v/pwr is always positive here (abs_v
			     * was filtered != 0 above and pwr is finite),
			     * so the `mant_e > 0` arm is intrinsic-dead
			     * for script-driven inputs.  Guard converted
			     * to nested singles. */
			    while (mant_e < 1.0) {
				if (mant_e <= 0.0) break;
				mant_e *= 10.0;
				expn_e--;
			    }
			} else {
			    goto fmt_fixed;
			}
		    }

		    if (neg) *fp++ = '-';
		    iPart = (th8_int64_t)mant_e;
		    fPart = mant_e - (double)iPart;
		    *fp++ = (char)('0' + (int)(iPart % 10));
		    /* Bug 32 fix: when nPrec == 0 the mantissa emits a
		     * single integer digit and the half-up boundary
		     * (fPart >= 0.5) was previously ignored, so e.g.
		     * `%.0e 4.5` returned "4e+00" instead of "5e+00"
		     * and `%.0e 999999.5` returned "9e+05" instead of
		     * "1e+06".  Mirror the fmt_fixed L598
		     * "round-integer-when-precision-is-0" branch: bump
		     * the single digit, and when bumping past '9'
		     * renormalize in place to '1' with expn_e++ (same
		     * pattern as the post-carry renorm below). */
		    if (nPrec == 0 && fPart >= 0.5 && ALWAYS(fp > zFBuf)) {
			char *rp = fp - 1;

			if (*rp < '9') {
			    (*rp)++;
			} else {
			    *rp = '1';
			    expn_e++;
			}
		    }
		    if (nPrec > 0) {
			*fp++ = '.';
			for (k = 0; k < nPrec; k++) {
			    fPart *= 10.0;
			    *fp++ = (char)('0' + (int)fPart);
			    fPart -= (int)fPart;
			}
			if (fPart >= 0.5 && ALWAYS(fp > zFBuf)) {
			    char *rp = fp - 1;
			    char *zStart = zFBuf;

			    /* Sign-safety (zStart skip) parallels the %f
			     * carry loop fix. */
			    /* Sign-skip via nested single-condition `if`s
			     * so clang MC/DC sees three single-condition
			     * decisions instead of one 3-condition compound
			     * (whose C-pairs include the intrinsic-dead
			     * '+' arm -- TH8's float formatter never emits
			     * a leading '+').  See FINDINGS.md Finding 005. */
			    if (zStart < fp) {
				/* Split per Finding 005 sec. 5b: the
				 * '+' arm is intrinsic-dead because
				 * TH8's float formatter never emits a
				 * leading '+'.  Two single-condition
				 * checks remove the dead C-pair. */
				if (*zStart == '-') {
				    zStart++;
				} else if (*zStart == '+') {
				    zStart++;
				}
			    }
			    while (rp >= zStart) {
				if (*rp == '.') {
				    rp--;
				    continue;
				}
				if (*rp < '9') {
				    (*rp)++;
				    break;
				}
				*rp = '0';
				rp--;
			    }
			    /* Bug 11/12 follow-up: when the carry survives
			     * past the mantissa's leading digit (every digit
			     * was '9' -- e.g. 9.995 -> 0.0e+02 buffer), the
			     * mantissa renormalises in place to "1.0..0"
			     * with the exponent bumped by 1 (e.g.
			     * 1.0e+03).  All trailing digits are already
			     * '0' from the carry loop; we only need to
			     * promote the leading digit and bump expn_e. */
			    if (rp < zStart) {
				*zStart = '1';
				expn_e++;
			    }
			}
		    }
		    *fp++ = (spec == 'E') ? 'E' : 'e';
		    if (expn_e >= 0) {
			*fp++ = '+';
		    } else {
			*fp++ = '-';
			expn_e = -expn_e;
		    }
		    if (expn_e >= 100) {
			*fp++ = (char)('0' + expn_e / 100);
			*fp++ = (char)('0' + (expn_e / 10) % 10);
			*fp++ = (char)('0' + expn_e % 10);
		    } else {
			*fp++ = (char)('0' + expn_e / 10);
			*fp++ = (char)('0' + expn_e % 10);
		    }
		} else if (spec == 'g' || spec == 'G') {
		    /*
		     * General: use %e if exponent < -4 or >= prec,
		     * otherwise %f.  Strip trailing zeros.
		     */

		    int expn = 0;
		    int gPrec = (nPrec == 0) ? 1 : nPrec;

		    if (abs_v != 0.0) {
			double lg;

			if (th8MathOp(
			        interp, TH8_MATH_LOG10, &lg, abs_v, 0.0) ==
			    TH8_OK) {
			    if (lg >= 0.0) {
				expn = (int)lg;
			    } else {
				expn = (int)lg - 1;
			    }
			}
		    }
		    if (expn < -4 || expn >= gPrec) {
			/* Use scientific notation with gPrec-1 digits */
			prec = gPrec - 1;
			havePrec = 1;
			nPrec = prec;
			spec = (spec == 'G') ? 'E' : 'e';
			stripGZeros = 1;
			goto fmt_scientific;
		    } else {
			/* Bug 11 fix: route %g fixed-output through the
			 * shared fmt_fixed path so it inherits the
			 * half-up rounding (incl. the Bug 12 carry-past-
			 * leading-digit promotion).  stripGFixed tells
			 * the post-emit block to strip trailing zeros
			 * (and trailing '.') from the rendered buffer. */
			int fPrec = gPrec - 1 - expn;

			if (fPrec < 0) fPrec = 0;
			nPrec = fPrec;
			havePrec = 1;
			stripGFixed = 1;
			goto fmt_fixed;
		    }
		} else {
		    /*
		     * Fixed-point: DDDD.DDD with nPrec digits
		     * after the decimal point.
		     */

fmt_fixed:
		    /*
		     * Bug 44 (UBSan): casting a double outside the
		     * int64 range to int64_t is undefined behavior.
		     * Refuse values too large for the fixed-point
		     * digit-by-digit renderer (~9.2e18) rather than
		     * UB.  Callers wanting %f for huge values should
		     * use %g or %e (which routes through
		     * fmt_scientific without the int64 cast).
		     */
		    if (abs_v >= 9.0e18) {
			Th8_SetResultStatic(
			    interp,
			    "format: value too large for %f conversion",
			    TH8_NOLEN);
			return TH8_ERROR;
		    }
		    if (neg) *fp++ = '-';
		    iPart = (th8_int64_t)abs_v;
		    fPart = abs_v - (double)iPart;
		    /* Round integer part when precision is 0. */
		    if (nPrec == 0 && fPart >= 0.5) {
			iPart++;
			fPart = 0.0;
		    }
		    /* Integer part */
		    {
			char zInt[30];
			char *p = &zInt[sizeof(zInt) - 1];
			th8_int64_t v = iPart;

			*p = 0;
			do {
			    *(--p) = (char)('0' + (int)(v % 10));
			    v /= 10;
			} while (v > 0);
			while (*p)
			    *fp++ = *p++;
		    }
		    if (nPrec > 0) {
			*fp++ = '.';
			for (k = 0; k < nPrec; k++) {
			    fPart *= 10.0;
			    *fp++ = (char)('0' + (int)fPart);
			    fPart -= (int)fPart;
			}
			/* Round last digit, propagating the carry through
			 * the integer part.  Bug 12 fix: when every digit
			 * was '9' (e.g. "9.9" formatted from 9.99) the
			 * carry rolls off the most-significant digit and
			 * must be PROMOTED to a new leading '1' (the
			 * previous code left "0.0" instead of "10.0").
			 * zStart skips a leading sign char so '-' is
			 * neither corrupted (by *rp < '9' incrementing it)
			 * nor carried into.  The insert shifts the
			 * rendered digits right by one within zFBuf. */
			if (fPart >= 0.5 && ALWAYS(fp > zFBuf)) {
			    char *rp = fp - 1;
			    char *zStart = zFBuf;

			    /* Sign-skip via nested single-condition `if`s
			     * so clang MC/DC sees three single-condition
			     * decisions instead of one 3-condition compound
			     * (whose C-pairs include the intrinsic-dead
			     * '+' arm -- TH8's float formatter never emits
			     * a leading '+').  See FINDINGS.md Finding 005. */
			    if (zStart < fp) {
				/* Split per Finding 005 sec. 5b: the
				 * '+' arm is intrinsic-dead because
				 * TH8's float formatter never emits a
				 * leading '+'.  Two single-condition
				 * checks remove the dead C-pair. */
				if (*zStart == '-') {
				    zStart++;
				} else if (*zStart == '+') {
				    zStart++;
				}
			    }
			    while (rp >= zStart) {
				if (*rp == '.') {
				    rp--;
				    continue;
				}
				if (*rp < '9') {
				    (*rp)++;
				    break;
				}
				*rp = '0';
				rp--;
			    }
			    if (rp < zStart) {
				size_t nShift = (size_t)(fp - zStart);
				size_t k2;

				/* Shift digits right by one (backward
				 * loop -- regions overlap by 1 byte; no
				 * platform memmove dependency). */
				for (k2 = nShift; k2 > 0; k2--) {
				    zStart[k2] = zStart[k2 - 1];
				}
				*zStart = '1';
				fp++;
			    }
			}
		    }
		}
		/*
		 * For %g: strip trailing zeros from the
		 * mantissa before the exponent.
		 */

		if (stripGZeros) {
		    char *ep = zFBuf;
		    char *dp = 0;

		    /* Find the 'e' or 'E'.  stripGZeros is only set on
		     * the scientific path; the buffer ALWAYS contains
		     * an 'e' or 'E', so ep < fp holds on every iter. */
		    while (ALWAYS(ep < fp) && *ep != 'e' && *ep != 'E') {
			if (*ep == '.') dp = ep;
			ep++;
		    }
		    /* stripGZeros is only set on the scientific-
		     * notation path (L543: spec is 'e'/'E', goto
		     * fmt_scientific), so the rendered buffer
		     * ALWAYS contains an 'e' or 'E' separator.
		     * The scan loop above therefore stops at the
		     * exponent marker before reaching fp, leaving
		     * ep < fp.  C2 (ep < fp) is ALWAYS T at the
		     * inner check when dp is non-NULL. */
		    if (dp && ALWAYS(ep < fp)) {
			char *zp = ep - 1;

			while (zp > dp && *zp == '0')
			    zp--;
			if (zp == dp) zp--; /* remove '.' too */
			zp++;
			/* Shift exponent part down. */
			while (ep < fp) {
			    *zp++ = *ep++;
			}
			fp = zp;
		    }
		}
		/*
		 * %g fixed-path trailing-zero strip.  The fmt_fixed
		 * emit can leave trailing zeros (and a dangling '.')
		 * that %g must remove (e.g. fmt_fixed renders
		 * "1.000" for %.3f-equivalent of 1.0; %.3g must
		 * collapse that to "1").  Only enter when the
		 * %g-fixed branch routed here AND fmt_fixed actually
		 * emitted a fractional part.
		 */

		if (stripGFixed) {
		    char *dp = 0;
		    char *ep = zFBuf;

		    while (ep < fp) {
			if (*ep == '.') {
			    dp = ep;
			    break;
			}
			ep++;
		    }
		    if (dp) {
			char *zp = fp - 1;

			while (zp > dp && *zp == '0')
			    zp--;
			if (zp == dp) zp--; /* drop the '.' too */
			fp = zp + 1;
		    }
		}
		nNum = (size_t)(fp - zFBuf);
		if (nNum >= sizeof(zNum)) {
		    nNum = sizeof(zNum) - 1;
		}
		{
		    size_t j;
		    for (j = 0; j < nNum; j++) {
			zNum[j] = zFBuf[j];
		    }
		    zNum[nNum] = 0;
		}
		goto fmt_pad_and_emit;
	    }
	    default: {
		char zBad[2];

		zBad[0] = spec;
		zBad[1] = 0;
		Th8_ErrorMessage(interp, "bad field specifier \"", zBad, 1);
		Th8_Free(interp, zOut);
		return TH8_ERROR;
	    }

		/*
	     * Common pad-and-emit path.  Specifiers that build
	     * their output in zNum/nNum jump here for width and
	     * alignment handling.
	     */

fmt_pad_and_emit:
		if (width > 0 && nNum < (size_t)width) {
		    size_t pad = (size_t)width - nNum;
		    size_t k;
		    char cPad = flagZero ? '0' : ' ';

		    if (flagMinus) {
			/*
			 * Left-justify: content then spaces.
			 */

			TH8_STR_APPEND(interp, &zOut, &nOut, zNum, nNum);
			for (k = 0; k < pad; k++) {
			    TH8_STR_APPEND(interp, &zOut, &nOut, " ", 1);
			}
		    } else if (
		        flagZero && nNum > 0 &&
		        (zNum[0] == '-' || zNum[0] == '+')) {
			/*
			 * Zero-pad with sign: sign first, then
			 * zeros, then digits.
			 */

			TH8_STR_APPEND(interp, &zOut, &nOut, zNum, 1);
			for (k = 0; k < pad; k++) {
			    TH8_STR_APPEND(interp, &zOut, &nOut, "0", 1);
			}
			TH8_STR_APPEND(
			    interp, &zOut, &nOut, &zNum[1], nNum - 1);
		    } else {
			/*
			 * Right-justify with padding char.
			 */

			for (k = 0; k < pad; k++) {
			    TH8_STR_APPEND(interp, &zOut, &nOut, &cPad, 1);
			}
			TH8_STR_APPEND(interp, &zOut, &nOut, zNum, nNum);
		    }
		} else {
		    TH8_STR_APPEND(interp, &zOut, &nOut, zNum, nNum);
		}
		break;
	    }
	}
    }

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;

not_enough:
    Th8_Free(interp, zOut);
    Th8_SetResultStatic(
        interp, "not enough arguments for all format specifiers", TH8_NOLEN);
    return TH8_ERROR;

oom:
    /* A TH8_STR_APPEND growth allocation failed; Th8_StringAppend
     * already set the "out of memory" result. */
    Th8_Free(interp, zOut);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ScanFormatUnsigned --
 *
 *	Format an unsigned 64-bit value as a decimal string.
 *
 * Why / How:
 *	Renders the unsigned interpretation of a scanned value for the
 *	64-bit-unsigned scan conversions (%lu and 64-bit %u variants),
 *	which Th8_SetResultWideInt (signed) cannot express.  Digits are
 *	generated least-significant first, then reversed into the
 *	caller's buffer, which must hold at least 20 digits.
 *
 * Results:
 *	The number of digits written (never zero: 0 renders as "0").
 *
 * Side effects:
 *	Writes into zBuf.
 *
 *----------------------------------------------------------------------
 */

static int
th8ScanFormatUnsigned(char *zBuf, th8_uint64_t v)
{
    char tmp[24];
    int n = 0;
    int i;

    if (v == 0) {
	zBuf[0] = '0';
	return 1;
    }
    while (v > 0) {
	tmp[n++] = (char)('0' + (int)(v % 10));
	v /= 10;
    }
    for (i = 0; i < n; i++) {
	zBuf[i] = tmp[n - 1 - i];
    }
    return n;
}


/* scan integer size modifiers (h/l/L run) -> stored integer type. */
#  define SCAN_MOD_NONE 0 /* (default)  -> 32-bit int */
#  define SCAN_MOD_HH   1 /* hh         -> 8-bit int  */
#  define SCAN_MOD_H    2 /* h          -> 16-bit int */
#  define SCAN_MOD_L    3 /* l or L     -> 64-bit int */
#  define SCAN_MOD_LL   4 /* ll         -> BigInt     */


/*
 *----------------------------------------------------------------------
 *
 * th8ScanDigitOk --
 *
 *	Test whether a character is a valid digit in the given radix.
 *
 * Why / How:
 *	Branches on the four bases `scan` supports so digit validation
 *	during a `%x`/`%o`/`%b`/`%d` conversion stops at the first
 *	out-of-radix character; hex defers to `th8IsHexDig` so both
 *	letter cases are accepted.  Any base other than 2, 8, or 16 is
 *	treated as decimal.
 *
 * Results:
 *	Non-zero if c is a digit of base (2, 8, 10, or 16); 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8ScanDigitOk(char c, int base)
{
    if (base == 16) return th8IsHexDig(c);
    if (base == 2) return (c == '0' || c == '1');
    if (base == 8) return (c >= '0' && c <= '7');
    return (c >= '0' && c <= '9');
}


/*
 *----------------------------------------------------------------------
 *
 * th8ScanCountSpecs --
 *
 *	Count the non-suppressed conversion specifiers in a scan format.
 *
 * Why / How:
 *	In list mode `scan` returns one element per non-suppressed
 *	conversion specifier, padding trailing unmatched specifiers with
 *	empty strings.  A `%` beginning `%%` is a literal; a `%`
 *	immediately followed by `*` is suppressed and produces no list
 *	element.  Every other `%` is a value-producing specifier.
 *
 * Results:
 *	The number of value-producing (non-suppressed) specifiers.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8ScanCountSpecs(const char *zFmt, size_t nFmt)
{
    size_t i;
    int n = 0;

    for (i = 0; i + 1 < nFmt; i++) {
	if (zFmt[i] != '%') continue;
	if (zFmt[i + 1] == '%') {
	    i++; /* literal %% */
	    continue;
	}
	if (zFmt[i + 1] != '*') n++;
    }
    return n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ScanStoreInt --
 *
 *	Convert a scanned integer digit run to the type selected by the
 *	conversion's size modifier and set it as the interpreter result.
 *
 * Why / How:
 *	TH8's scan uses the ACTUAL C type associated with each specifier:
 *	the digits are accumulated modulo 2^64, negated if signed, then
 *	truncated to the modifier's width (hh=8, h=16, default=32, l/L=64
 *	bits), sign-extended for signed conversions or zero-extended for
 *	unsigned ones.  The `ll` modifier stores an arbitrary-precision
 *	BigInt instead (th8ScanBignum); an unsigned BigInt scan of a
 *	negative value is an error, matching the impossibility of an
 *	unbounded unsigned magnitude.  Digits are assumed pre-validated
 *	for `base` by the caller.
 *
 * Results:
 *	TH8_OK with the interpreter result set; TH8_ERROR on an
 *	unsigned-BigInt scan of a negative value or an over-long BigInt.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8ScanStoreInt(
    Th8_Interp *interp,
    const char *zDig,
    size_t nDig,
    int base,
    int bNeg,
    int bUnsigned,
    int sizeMod)
{
    th8_uint64_t acc = 0;
    th8_uint64_t mask;
    th8_uint64_t low;
    size_t k;
    int width;

    if (sizeMod == SCAN_MOD_LL) {
#  if defined(TH8_ENABLE_BIGINT)
	char buf[600];
	size_t p = 0;
	const char *zPfx = (base == 16) ? "0x"
	                 : (base == 8)  ? "0o"
	                 : (base == 2)  ? "0b"
	                                : "";
	size_t nPfx = Th8_Strlen(interp, zPfx);

	if (bUnsigned && bNeg) {
	    Th8_SetResultStatic(
	        interp, "scan: unsigned bignum scans are invalid", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (nDig + nPfx + 2 >= sizeof(buf)) {
	    Th8_SetResultStatic(
	        interp, "scan: integer too long for conversion", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (bNeg) buf[p++] = '-';
	Th8_Memcpy(interp, &buf[p], zPfx, nPfx);
	p += nPfx;
	Th8_Memcpy(interp, &buf[p], zDig, nDig);
	p += nDig;
	return th8ScanBignum(interp, buf, p);
#  else
	width = 64; /* No BigInt support: fall back to 64-bit. */
#  endif
    } else {
	width = (sizeMod == SCAN_MOD_HH) ? 8
	      : (sizeMod == SCAN_MOD_H)  ? 16
	      : (sizeMod == SCAN_MOD_L)  ? 64
	                                 : 32;
    }

    for (k = 0; k < nDig; k++) {
	char c = zDig[k];
	int d;

	if (c >= '0' && c <= '9') {
	    d = c - '0';
	} else if (c >= 'a' && c <= 'f') {
	    d = c - 'a' + 10;
	} else {
	    d = c - 'A' + 10;
	}
	acc = acc * (th8_uint64_t)base + (th8_uint64_t)d; /* modular */
    }
    if (bNeg) acc = (th8_uint64_t)0 - acc; /* two's-complement negate */

    mask = (width == 64) ? ~(th8_uint64_t)0
                         : (((th8_uint64_t)1 << width) - 1);
    low = acc & mask;
    if (!bUnsigned && width < 64 && ((low >> (width - 1)) & 1)) {
	low |= ~mask; /* sign-extend */
    }

    if (bUnsigned && width == 64) {
	char ubuf[24];
	int nu = th8ScanFormatUnsigned(ubuf, low);

	Th8_SetResult(interp, ubuf, (size_t)nu);
    } else {
	Th8_SetResultWideInt(interp, (th8_int64_t)low);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * scan_command --
 *
 *	Parse an input string under the control of a format string,
 *	extracting values (the inverse of [format]).
 *
 *	scan STRING FORMAT ?VARNAME ...?
 *
 * Why / How:
 *	A scanf engine tracking the Tcl 8.6 specifier set with TH8's own
 *	type rules.  Each specifier is %[*][width][size]CONV:
 *	  d u i o x X b   -- integers.  The size modifier selects the
 *	                     stored C type: hh=8, h=16, (none)=32, l/L=64
 *	                     bits, ll=BigInt; signed conversions (d, i)
 *	                     sign-extend, the rest zero-extend.
 *	  e E f g G       -- floating point: (none)=double, h=float; any
 *	                     other size modifier is an error.
 *	  c               -- one character as its code point
 *	  s               -- run of non-whitespace characters
 *	  [set] [^set]    -- run of characters in / not in a set
 *	  n               -- count of characters consumed so far
 *	  %%              -- a literal '%'
 *	`*` suppresses assignment; a width bounds the field.  A blank/tab
 *	in the format matches any run of input whitespace; every
 *	conversion except c, [ and n skips leading whitespace.  In
 *	variable mode the result is the count of assignments (or -1 if
 *	end-of-input is reached before any conversion); in list mode (no
 *	VARNAMEs) it is a list with one element per non-suppressed
 *	specifier, trailing unmatched specifiers padded with empties.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on a bad size modifier, an
 *	unsigned-BigInt scan of a negative value, or if variables are
 *	requested but TH8_ENABLE_VARIABLES is not compiled in.
 *
 * Side effects:
 *	May set interpreter variables when operating in variable mode.
 *
 *----------------------------------------------------------------------
 */

static int
scan_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zStr;
    size_t nStr;
    const char *zFmt;
    size_t nFmt;
    size_t iStr = 0;
    size_t iFmt = 0;
#  if defined(TH8_ENABLE_VARIABLES)
    int iVar = 3; /* Next variable argument. */
#  endif
    int nAssign = 0; /* Assignments / list elements produced. */
    int nConvTotal = 0; /* Conversions including suppressed ones. */
    int bEof = 0; /* EOF reached before any conversion -> -1 / empty. */
    int nSpecs; /* Non-suppressed specifiers (list-mode padding). */
    int bConvAttempted = 0; /* A conversion specifier was reached. */
    char *zList = 0; /* For inline (no-variable) mode. */
    size_t nList = 0;
    int bVarMode;

    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(interp, "scan string format ?varname ...?");
    }
    zStr = argv[1];
    nStr = TH8_LEN(argl[1]);
    zFmt = argv[2];
    nFmt = TH8_LEN(argl[2]);
    bVarMode = (argc > 3);
    nSpecs = th8ScanCountSpecs(zFmt, nFmt);

#  if !defined(TH8_ENABLE_VARIABLES)
    if (bVarMode) {
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    while (iFmt < nFmt) {
	char cf;
	int bSuppress;
	int width;
	int bHaveWidth;
	int sizeMod;
	char conv;
	size_t maxEnd;
	int converted;

	if (Th8_Ready(interp) != TH8_OK) {
	    Th8_Free(interp, zList);
	    return TH8_ERROR;
	}
	cf = zFmt[iFmt];

	/* A blank or tab matches any run (including zero) of input ws. */
	if (th8IsSpace(cf)) {
	    iFmt++;
	    while (iStr < nStr && th8IsSpace(zStr[iStr]))
		iStr++;
	    continue;
	}

	/* Any other non-'%' character must match the input literally. */
	if (cf != '%') {
	    if (iStr >= nStr || zStr[iStr] != cf) break;
	    iStr++;
	    iFmt++;
	    continue;
	}

	iFmt++; /* Skip '%'. */
	if (iFmt >= nFmt) break;
	if (zFmt[iFmt] == '%') {
	    if (iStr >= nStr || zStr[iStr] != '%') break;
	    iStr++;
	    iFmt++;
	    continue;
	}

	/* [*] assignment-suppression flag. */
	bSuppress = 0;
	if (zFmt[iFmt] == '*') {
	    bSuppress = 1;
	    iFmt++;
	    if (iFmt >= nFmt) break;
	}

	/* [width] maximum field width. */
	width = 0;
	bHaveWidth = 0;
	while (iFmt < nFmt && zFmt[iFmt] >= '0' && zFmt[iFmt] <= '9') {
	    bHaveWidth = 1;
	    width = width * 10 + (zFmt[iFmt] - '0');
	    iFmt++;
	}
	if (iFmt >= nFmt) break;

	/* [size] modifier (hh, h, l, ll, L). */
	sizeMod = SCAN_MOD_NONE;
	if (zFmt[iFmt] == 'h') {
	    iFmt++;
	    if (iFmt < nFmt && zFmt[iFmt] == 'h') {
		iFmt++;
		sizeMod = SCAN_MOD_HH;
	    } else {
		sizeMod = SCAN_MOD_H;
	    }
	} else if (zFmt[iFmt] == 'l') {
	    iFmt++;
	    if (iFmt < nFmt && zFmt[iFmt] == 'l') {
		iFmt++;
		sizeMod = SCAN_MOD_LL;
	    } else {
		sizeMod = SCAN_MOD_L;
	    }
	} else if (zFmt[iFmt] == 'L') {
	    iFmt++;
	    sizeMod = SCAN_MOD_L;
	}
	if (iFmt >= nFmt) break;

	conv = zFmt[iFmt];

	/* Every conversion except %c, %[, and %n skips leading whitespace. */
	if (conv != 'c' && conv != '[' && conv != 'n') {
	    while (iStr < nStr && th8IsSpace(zStr[iStr]))
		iStr++;
	}

	/* EOF before a value-producing conversion, nothing converted -> -1. */
	if (conv != 'n' && iStr >= nStr) {
	    if (nConvTotal == 0) bEof = 1;
	    break;
	}

	/* The field width bounds the input region this conversion reads. */
	maxEnd = nStr;
	if (bHaveWidth && width > 0 && iStr + (size_t)width < maxEnd) {
	    maxEnd = iStr + (size_t)width;
	}

	converted = 0;
	bConvAttempted = 1;

	switch (conv) {
	case 'd':
	case 'i':
	case 'u': {
	    /*
	     * Decimal (d/u) or base-detected (i) integer.
	     */

	    size_t save = iStr;
	    size_t dstart;
	    int base = 10;
	    int bNeg = 0;
	    int bUnsigned = (conv == 'u');

	    if (iStr < maxEnd && (zStr[iStr] == '-' || zStr[iStr] == '+')) {
		bNeg = (zStr[iStr] == '-');
		iStr++;
	    }
	    if (conv == 'i') {
		if (iStr + 1 < maxEnd && zStr[iStr] == '0' &&
		    (zStr[iStr + 1] == 'x' || zStr[iStr + 1] == 'X')) {
		    base = 16;
		    iStr += 2;
		} else if (iStr < maxEnd && zStr[iStr] == '0') {
		    base = 8;
		}
	    }
	    dstart = iStr;
	    while (iStr < maxEnd && th8ScanDigitOk(zStr[iStr], base))
		iStr++;
	    if (iStr == dstart) {
		iStr = save;
		break;
	    }
	    if (th8ScanStoreInt(
	            interp, &zStr[dstart], iStr - dstart, base, bNeg,
	            bUnsigned, sizeMod) != TH8_OK) {
		Th8_Free(interp, zList);
		return TH8_ERROR;
	    }
	    converted = 1;
	    break;
	}
	case 'o':
	case 'x':
	case 'X':
	case 'b': {
	    /*
	     * Octal / hexadecimal / binary (unsigned), with optional 0x /
	     * 0b prefix for x/X and b.
	     */

	    size_t save = iStr;
	    size_t dstart;
	    int base = (conv == 'o') ? 8 : (conv == 'b') ? 2 : 16;

	    if (base == 16 && iStr + 1 < maxEnd && zStr[iStr] == '0' &&
	        (zStr[iStr + 1] == 'x' || zStr[iStr + 1] == 'X')) {
		iStr += 2;
	    } else if (
	        base == 2 && iStr + 1 < maxEnd && zStr[iStr] == '0' &&
	        (zStr[iStr + 1] == 'b' || zStr[iStr + 1] == 'B')) {
		iStr += 2;
	    }
	    dstart = iStr;
	    while (iStr < maxEnd && th8ScanDigitOk(zStr[iStr], base))
		iStr++;
	    if (iStr == dstart) {
		iStr = save;
		break;
	    }
	    if (th8ScanStoreInt(
	            interp, &zStr[dstart], iStr - dstart, base, 0, 1,
	            sizeMod) != TH8_OK) {
		Th8_Free(interp, zList);
		return TH8_ERROR;
	    }
	    converted = 1;
	    break;
	}
	case 'c': {
	    /*
	     * A single character (no ws skip, no width): its code point.
	     */

	    int nByte;
	    int cp = Th8_Utf8Decode(&zStr[iStr], nStr - iStr, &nByte);

	    iStr += (size_t)nByte;
	    Th8_SetResultInt(interp, cp);
	    converted = 1;
	    break;
	}
	case 's': {
	    /*
	     * A run of non-whitespace characters (bounded by the width).
	     */

	    size_t start = iStr;

	    while (iStr < maxEnd && !th8IsSpace(zStr[iStr]))
		iStr++;
	    if (iStr == start) break;
	    Th8_SetResult(interp, &zStr[start], iStr - start);
	    converted = 1;
	    break;
	}
	case 'e':
	case 'E':
	case 'f':
	case 'g':
	case 'G': {
	    /*
	     * Floating point.  The stored type follows the size modifier:
	     * (none) -> double, h -> float; any other modifier is an error.
	     */

	    size_t start = iStr;
	    double rVal;
	    int bSawExp = 0;

	    if (sizeMod != SCAN_MOD_NONE && sizeMod != SCAN_MOD_H) {
		Th8_SetResultStatic(
		    interp,
		    "scan: unsupported size modifier for floating-point "
		    "conversion",
		    TH8_NOLEN);
		Th8_Free(interp, zList);
		return TH8_ERROR;
	    }
	    if (iStr < maxEnd && (zStr[iStr] == '-' || zStr[iStr] == '+')) {
		iStr++;
	    }
	    while (iStr < maxEnd) {
		char c = zStr[iStr];

		if ((c >= '0' && c <= '9') || c == '.') {
		    iStr++;
		    continue;
		}
		if ((c == 'e' || c == 'E') && !bSawExp) {
		    bSawExp = 1;
		    iStr++;
		    if (iStr < maxEnd &&
		        (zStr[iStr] == '+' || zStr[iStr] == '-')) {
			iStr++;
		    }
		    continue;
		}
		break;
	    }
	    if (iStr == start) break;
	    if (Th8_ToDouble(0, &zStr[start], iStr - start, &rVal) !=
	        TH8_OK) {
		iStr = start;
		break;
	    }
	    if (sizeMod == SCAN_MOD_H) {
		rVal = (double)(float)rVal; /* narrow to single precision */
	    }
	    Th8_SetResultDouble(interp, rVal);
	    converted = 1;
	    break;
	}
	case 'n': {
	    /*
	     * Store the number of characters consumed so far (no input).
	     */

	    Th8_SetResultInt(interp, Th8_Utf8Len(zStr, iStr));
	    converted = 1;
	    break;
	}
	case '[': {
	    /*
	     * Character-set scan (honoring a leading '^' negation, a ']'
	     * first member, and 'a-b' ranges).
	     */

	    unsigned char inSet[256];
	    int bNeg = 0;
	    size_t start;
	    size_t bodyStart;
	    size_t k;

	    for (k = 0; k < 256; k++)
		inSet[k] = 0;

	    iFmt++; /* Past '['. */
	    if (iFmt < nFmt && zFmt[iFmt] == '^') {
		bNeg = 1;
		iFmt++;
	    }
	    bodyStart = iFmt;
	    if (iFmt < nFmt && zFmt[iFmt] == ']') {
		inSet[(unsigned char)']'] = 1;
		iFmt++;
	    }
	    while (iFmt < nFmt && zFmt[iFmt] != ']') {
		if (zFmt[iFmt] == '-' && iFmt > bodyStart &&
		    iFmt + 1 < nFmt && zFmt[iFmt + 1] != ']') {
		    unsigned char lo = (unsigned char)zFmt[iFmt - 1];
		    unsigned char hi = (unsigned char)zFmt[iFmt + 1];
		    int c;

		    if (lo <= hi) {
			for (c = lo; c <= hi; c++)
			    inSet[c] = 1;
		    } else {
			inSet[hi] = 1;
			inSet[(unsigned char)'-'] = 1;
		    }
		    iFmt += 2;
		    continue;
		}
		inSet[(unsigned char)zFmt[iFmt]] = 1;
		iFmt++;
	    }
	    if (iFmt >= nFmt) {
		goto scan_done; /* Unterminated set. */
	    }

	    start = iStr;
	    while (iStr < maxEnd) {
		unsigned char uc = (unsigned char)zStr[iStr];

		if (bNeg ? inSet[uc] : !inSet[uc]) break;
		iStr++;
	    }
	    if (iStr == start) break;
	    Th8_SetResult(interp, &zStr[start], iStr - start);
	    converted = 1;
	    break;
	}
	default:
	    goto scan_done; /* Unknown conversion character. */
	}

	if (!converted) {
	    break; /* Matching failure: stop scanning. */
	}

	iFmt++; /* Consume the conversion character (or the set's ']'). */

	if (!bSuppress) {
	    size_t nRes;
	    const char *zRes = Th8_GetResult(interp, &nRes);

	    if (bVarMode) {
#  if defined(TH8_ENABLE_VARIABLES)
		if (iVar < argc) {
		    Th8_SetVar(
		        interp, argv[iVar], TH8_LEN(argl[iVar]), zRes, nRes);
		    iVar++;
		}
#  endif
	    } else {
		Th8_ListAppend(interp, &zList, &nList, zRes, nRes);
	    }
	    nAssign++;
	}
	nConvTotal++;
    }

scan_done:
    if (bVarMode) {
	Th8_SetResultInt(interp, bEof ? -1 : nAssign);
    } else {
	/*
	 * List mode: pad to one element per non-suppressed specifier with
	 * empty strings, unless end-of-input was hit before any conversion
	 * (then the result is the empty list).
	 */

	if (bConvAttempted && !bEof) {
	    while (nAssign < nSpecs) {
		Th8_ListAppend(interp, &zList, &nList, "", 0);
		nAssign++;
	    }
	}
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8FormattingCommands[] = {
    {1, 0, "format", format_command},
    {1, 0, "scan", scan_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8FormattingGetCommands --
 *
 *	Return the command table for the formatting plugin.
 *
 * Why / How:
 *	Called by the plugin registration system to discover which
 *	commands this plugin provides.  On the first call pCommand
 *	is NULL and *pnCommand is set to the count; on the second
 *	call the entries are copied into the caller-provided array.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if pnCommand is NULL or the
 *	caller's buffer is too small.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8FormattingGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8FormattingCommands) /
                  sizeof(th8FormattingCommands[0]));

    if (!pnCommand) return TH8_ERROR;
    if (!pCommand) {
	*pnCommand = n;
	return TH8_OK;
    }
    if (*pnCommand < n) return TH8_ERROR;
    *pnCommand = n;
    {
	int i;

	for (i = 0; i < n; i++) {
	    pCommand[i] = th8FormattingCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_FORMATTING */
