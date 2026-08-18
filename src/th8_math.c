/*
 * th8_math.c -- Math function callbacks and registration for TH8.
 *
 * Contains the implementations of all built-in expression math
 * functions (abs, int, double, sin, cos, etc.) as Th8_MathFuncProc
 * callbacks, plus the static init table and registration function.
 *
 * The registration API (Th8_CreateMathFunc, Th8_DeleteMathFunc,
 * Th8_FindMathFunc, th8MathOp) is in th8_expr.c.
 * Th8_ListAppendMathFunctions is defined below.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"

#if defined(TH8_ENABLE_EXPRESSIONS)

#  if defined(TH8_ENABLE_BIGINT)
#    include "th8_bigint.h"
#  endif

/*
 * Bug 22 fix (2026-06-07): the shared dispatchers
 * (th8MathTranscendental, th8MathClassify) need to know their
 * function's arity so they can reject NULL operands at the right
 * threshold (arity == 0 means NULL is OK; arity >= 1 means z1
 * MUST be non-NULL; arity >= 2 means z2 MUST be non-NULL too).
 * The shared opcode-based dispatch already uses pCtx to carry
 * the TH8_MATH_* opcode; we extend the encoding to pack the
 * arity in the high 8 bits.  Per-op procs (th8MathAbs et al.)
 * ignore pCtx and don't need this encoding.
 *
 * Opcodes range 0..56 (well under 2^16), arity is 0/1/2.
 */
#  define TH8_MATH_CTX_PACK(arity, opcode)                                   \
      ((int)(((arity) & 0xFF) << 16) | ((opcode) & 0xFFFF))
#  define TH8_MATH_CTX_OPCODE(packed) ((int)(packed) & 0xFFFF)
#  define TH8_MATH_CTX_ARITY(packed)  (((int)(packed) >> 16) & 0xFF)

/*
 *----------------------------------------------------------------------
 *
 * th8MathAbs --
 *
 *	Compute the absolute value of a numeric argument.  Supports
 *	int, bigint, and double operands.
 *
 * Why / How:
 *	Implements the [expr abs(x)] math function.  Tries integer
 *	conversion first (cheapest), then bigint (strip leading '-'),
 *	then double.  Returns TH8_ERROR if the argument is not numeric.
 *
 * Results:
 *	TH8_OK with the absolute value in the interp result.
 *	TH8_ERROR if the argument cannot be parsed as a number.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathAbs(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int i;
    double d;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK == Th8_ToInt(0, z1, n1, &i)) {
	return Th8_SetResultInt(interp, i < 0 ? -i : i);
    }
#  if defined(TH8_ENABLE_BIGINT)
    if (Th8_IsBigintEnabled(interp) && th8IsBigint(interp, z1, n1)) {
	/* Bigint abs: strip leading '-' if present.  Split per
	 * Finding 005 sec. 5b: n1 > 0 is structurally guaranteed
	 * here because th8IsBigint just returned true (a valid
	 * bigint string has at least one digit, often 19+). */
	if (n1 > 0) {
	    if (z1[0] == '-') {
		return th8BigintUnary(
		    interp, z1, n1, 2 /* TH8_OP_UNARY_MINUS */);
	    }
	}
	Th8_SetResult(interp, z1, n1);
	return TH8_OK;
    }
#  endif
    if (TH8_OK == Th8_ToDouble(interp, z1, n1, &d)) {
	Th8_SetResultDouble(interp, d < 0 ? -d : d);
	return TH8_OK;
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathBool --
 *
 *	Convert a numeric argument to a boolean (0 or 1).
 *
 * Why / How:
 *	Implements the [expr bool(x)] math function.  Delegates to
 *	Th8_ToBoolean so that the math-function form has identical
 *	coercion semantics to the script-level boolean tests in
 *	`if`, `while`, `for`, etc. -- including the literal "0"/"1"
 *	fast paths, the keyword synonyms (true/false/yes/no/on/off),
 *	the wide-int and bigint paths, and the double path.
 *
 * Results:
 *	TH8_OK with 0 or 1 in the interp result.
 *	TH8_ERROR if the argument is not parseable as a boolean.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathBool(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int b;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (Th8_ToBoolean(interp, z1, n1, &b) == TH8_OK) {
	return Th8_SetResultInt(interp, b);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathDouble --
 *
 *	Convert an argument to a double-precision floating-point value.
 *
 * Why / How:
 *	Implements the [expr double(x)] math function.  Parses the
 *	string as a double via Th8_ToDouble and returns the result
 *	as a formatted double string.
 *
 * Results:
 *	TH8_OK with the double value in the interp result.
 *	TH8_ERROR if the argument is not numeric.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathDouble(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    double d;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK != Th8_ToDouble(interp, z1, n1, &d)) return TH8_ERROR;
    Th8_SetResultDouble(interp, d);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathEntier --
 *
 *	Convert an argument to an arbitrary-precision integer.
 *	Tries wide int first, then bigint passthrough, then
 *	truncation from double.
 *
 * Why / How:
 *	Implements the [expr entier(x)] math function.  Unlike int(),
 *	entier() preserves the full precision of the value: wide
 *	integers stay wide, bigints stay bigint strings.  Only when
 *	the value is a double is it truncated to a wide integer.
 *
 * Results:
 *	TH8_OK with the integer value in the interp result.
 *	TH8_ERROR if the argument is not numeric.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathEntier(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    th8_int64_t w;
    double d;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK == Th8_ToWideInt(0, z1, n1, &w)) {
	Th8_SetResultWideInt(interp, w);
	return TH8_OK;
    }
#  if defined(TH8_ENABLE_BIGINT)
    if (th8IsBigint(interp, z1, n1)) {
	Th8_SetResult(interp, z1, n1);
	return TH8_OK;
    }
#  endif
    if (TH8_OK == Th8_ToDouble(interp, z1, n1, &d)) {
	Th8_SetResultWideInt(interp, (th8_int64_t)d);
	return TH8_OK;
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathInt --
 *
 *	Convert an argument to a 32-bit integer.  Tries int first,
 *	then bigint passthrough, then truncation from double.
 *
 * Why / How:
 *	Implements the [expr int(x)] math function.  For values that
 *	already fit in an int, returns them directly.  Bigint values
 *	are passed through as strings.  Doubles are truncated via
 *	C cast to int.
 *
 * Results:
 *	TH8_OK with the int value in the interp result.
 *	TH8_ERROR if the argument is not numeric.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathInt(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int i;
    double d;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK == Th8_ToInt(0, z1, n1, &i)) {
	return Th8_SetResultInt(interp, i);
    }
#  if defined(TH8_ENABLE_BIGINT)
    if (th8IsBigint(interp, z1, n1)) {
	Th8_SetResult(interp, z1, n1);
	return TH8_OK;
    }
#  endif
    if (TH8_OK == Th8_ToDouble(interp, z1, n1, &d)) {
	return Th8_SetResultInt(interp, (int)d);
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathIsqrt --
 *
 *	Compute the integer square root of a non-negative integer.
 *
 * Why / How:
 *	Implements the [expr isqrt(x)] math function.  Uses a simple
 *	linear search (root+1)^2 <= val to find the largest integer
 *	whose square does not exceed the argument.  Returns TH8_ERROR
 *	for negative inputs.
 *
 * Results:
 *	TH8_OK with the integer square root in the interp result.
 *	TH8_ERROR if the argument is negative or not an integer.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathIsqrt(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int val, root;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK != Th8_ToInt(interp, z1, n1, &val)) return TH8_ERROR;
    if (val < 0) {
	Th8_SetResultStatic(
	    interp, "square root of negative number", TH8_NOLEN);
	return TH8_ERROR;
    }
    root = 0;
    while ((root + 1) * (root + 1) <= val)
	root++;
    return Th8_SetResultInt(interp, root);
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathMax --
 *
 *	Return the larger of two numeric arguments.
 *
 * Why / How:
 *	Implements the [expr max(a,b)] math function.  Tries int
 *	comparison first, then bigint comparison, then double.  The
 *	numeric type hierarchy ensures no precision is lost.
 *
 * Results:
 *	TH8_OK with the larger value in the interp result.
 *	TH8_ERROR if either argument is not numeric.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathMax(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int i1, i2;
    double d1, d2;
    (void)ctx;
    if (!z1 || !z2) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK == Th8_ToInt(0, z1, n1, &i1) &&
        TH8_OK == Th8_ToInt(0, z2, n2, &i2)) {
	return Th8_SetResultInt(interp, i1 > i2 ? i1 : i2);
    }
#  if defined(TH8_ENABLE_BIGINT)
    if (Th8_IsBigintEnabled(interp) &&
        (th8IsBigint(interp, z1, n1) || th8IsBigint(interp, z2, n2))) {
	int cmp = 0;
	if (TH8_OK == th8BigintCompare(interp, z1, n1, z2, n2, &cmp)) {
	    Th8_SetResult(interp, cmp >= 0 ? z1 : z2, cmp >= 0 ? n1 : n2);
	    return TH8_OK;
	}
    }
#  endif
    if (TH8_OK == Th8_ToDouble(interp, z1, n1, &d1) &&
        TH8_OK == Th8_ToDouble(interp, z2, n2, &d2)) {
	Th8_SetResultDouble(interp, d1 > d2 ? d1 : d2);
	return TH8_OK;
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathMin --
 *
 *	Return the smaller of two numeric arguments.
 *
 * Why / How:
 *	Implements the [expr min(a,b)] math function.  Tries int
 *	comparison first, then bigint comparison, then double.  The
 *	numeric type hierarchy ensures no precision is lost.
 *
 * Results:
 *	TH8_OK with the smaller value in the interp result.
 *	TH8_ERROR if either argument is not numeric.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathMin(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int i1, i2;
    double d1, d2;
    (void)ctx;
    if (!z1 || !z2) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK == Th8_ToInt(0, z1, n1, &i1) &&
        TH8_OK == Th8_ToInt(0, z2, n2, &i2)) {
	return Th8_SetResultInt(interp, i1 < i2 ? i1 : i2);
    }
#  if defined(TH8_ENABLE_BIGINT)
    if (Th8_IsBigintEnabled(interp) &&
        (th8IsBigint(interp, z1, n1) || th8IsBigint(interp, z2, n2))) {
	int cmp = 0;
	if (TH8_OK == th8BigintCompare(interp, z1, n1, z2, n2, &cmp)) {
	    Th8_SetResult(interp, cmp <= 0 ? z1 : z2, cmp <= 0 ? n1 : n2);
	    return TH8_OK;
	}
    }
#  endif
    if (TH8_OK == Th8_ToDouble(interp, z1, n1, &d1) &&
        TH8_OK == Th8_ToDouble(interp, z2, n2, &d2)) {
	Th8_SetResultDouble(interp, d1 < d2 ? d1 : d2);
	return TH8_OK;
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathRound --
 *
 *	Round a double to the nearest integer using round-half-away-
 *	from-zero semantics.
 *
 * Why / How:
 *	Implements the [expr round(x)] math function.  Adds 0.5
 *	(or -0.5 for negatives) then truncates to a wide integer,
 *	matching Tcl 8.4's rounding behavior.
 *
 * Results:
 *	TH8_OK with the rounded wide integer in the interp result.
 *	TH8_ERROR if the argument is not numeric.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathRound(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    double d;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK != Th8_ToDouble(interp, z1, n1, &d)) return TH8_ERROR;
    Th8_SetResultWideInt(interp, (th8_int64_t)(d + (d >= 0.0 ? 0.5 : -0.5)));
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathTypeof --
 *
 *	Return the numeric type name of an argument: "int", "wide",
 *	"entier", "double", or "string".
 *
 * Why / How:
 *	Implements the [expr typeof(x)] math function.  Probes the
 *	argument with Th8_ToInt, Th8_ToWideInt, th8IsBigint, and
 *	Th8_ToDouble in order of specificity.  The first successful
 *	conversion determines the type name.
 *
 * Results:
 *	TH8_OK with the type name string in the interp result.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathTypeof(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int i;
    th8_int64_t w;
    double d;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK == Th8_ToInt(0, z1, n1, &i)) {
	Th8_SetResultStatic(interp, "int", 3);
    } else if (TH8_OK == Th8_ToWideInt(0, z1, n1, &w)) {
	Th8_SetResultStatic(interp, "wide", 4);
#  if defined(TH8_ENABLE_BIGINT)
    } else if (th8IsBigint(interp, z1, n1)) {
	Th8_SetResultStatic(interp, "entier", 6);
#  endif
    } else if (TH8_OK == Th8_ToDouble(0, z1, n1, &d)) {
	Th8_SetResultStatic(interp, "double", 6);
    } else {
	Th8_SetResultStatic(interp, "string", 6);
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathWide --
 *
 *	Convert an argument to a 64-bit wide integer.
 *
 * Why / How:
 *	Implements the [expr wide(x)] math function.  Tries wide
 *	integer conversion first; if that fails, parses as double
 *	and truncates to a wide integer via C cast.
 *
 * Results:
 *	TH8_OK with the wide integer in the interp result.
 *	TH8_ERROR if the argument is not numeric.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathWide(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    th8_int64_t w;
    double d;
    (void)ctx;
    (void)z2;
    (void)n2;
    if (!z1) return TH8_ERROR; /* Bug 22 hardening. */
    if (TH8_OK == Th8_ToWideInt(0, z1, n1, &w)) {
	Th8_SetResultWideInt(interp, w);
	return TH8_OK;
    }
    if (TH8_OK == Th8_ToDouble(interp, z1, n1, &d)) {
	Th8_SetResultWideInt(interp, (th8_int64_t)d);
	return TH8_OK;
    }
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8MathClassify --
 *
 *	Return a human-readable string classifying a floating-point
 *	value: "zero", "subnormal", "normal", "infinite", or "nan".
 *
 * Why / How:
 *	Implements the [expr fpclassify(x)] math function (TIP #521).
 *	Dispatches through the platform's xMathFunc callback with
 *	TH8_MATH_FPCLASSIFY, then maps the encoded integer result
 *	(0..4) to the corresponding string name.
 *
 * Results:
 *	TH8_OK with the classification string in the interp result.
 *	TH8_ERROR if the argument is not numeric or the platform
 *	callback is unavailable.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathClassify(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    double d1 = 0.0, result = 0.0;
    const Th8_Platform *pPlat;

    (void)ctx;
    (void)z2;
    (void)n2;
    /* Bug 22 fix (2026-06-07): fpclassify has arity 1, so z1 MUST
     * be non-NULL.  Reject NULL explicitly rather than silently
     * classifying 0.0; otherwise a direct-API caller using
     * Th8_FindMathFunc("fpclassify") + NULL operand would get a
     * meaningless "zero" answer instead of an error. */
    if (!z1) {
	Th8_SetResultStatic(
	    interp, "fpclassify: missing argument", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Bug 52 fix (2026-06-09): platform-NULL check MUST come BEFORE
     * Th8_ToDouble -- otherwise the cache lookup inside ToDouble
     * dispatches through pPlatform->xMutexEnter and crashes when
     * pPlatform is NULL (teardown or test-driven platform-swap window).
     * The check is cheap and matches the th8MathOp ordering. */
    pPlat = Th8_GetPlatform(interp);
    /* Bug 26: Th8_GetPlatform can return NULL during teardown. */
    if (!pPlat || !pPlat->xMathFunc) {
	Th8_SetResultStatic(interp, "math function not available", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (TH8_OK != Th8_ToDouble(interp, z1, n1, &d1)) return TH8_ERROR;
    if (TH8_OK !=
        pPlat->xMathFunc(
            interp, pPlat->pCtx, TH8_MATH_FPCLASSIFY, &result, d1, 0.0)) {
	Th8_SetResultStatic(interp, "domain error", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* result encodes: 0=zero, 1=subnormal, 2=normal, 3=infinite, 4=nan */
    switch ((int)result) {
    case 0:
	Th8_SetResultStatic(interp, "zero", 4);
	break;
    case 1:
	Th8_SetResultStatic(interp, "subnormal", 9);
	break;
    case 2:
	Th8_SetResultStatic(interp, "normal", 6);
	break;
    case 3:
	Th8_SetResultStatic(interp, "infinite", 8);
	break;
    case 4:
	Th8_SetResultStatic(interp, "nan", 3);
	break;
    default:
	Th8_SetResultStatic(interp, "unknown", 7);
	break;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MathTranscendental --
 *
 *	Generic dispatcher for transcendental and C99 math functions
 *	(sin, cos, exp, log, acosh, cbrt, etc.).
 *
 * Why / How:
 *	All transcendental math functions share this single callback.
 *	The TH8_MATH_* opcode is stored in the ctx pointer (via
 *	TH8_INT2PTR at registration time) and extracted here to
 *	dispatch through the platform's xMathFunc callback.
 *	Classification and signbit opcodes (>= TH8_MATH_SIGNBIT)
 *	return integer results; all others return doubles.
 *
 * Results:
 *	TH8_OK with the computed value in the interp result.
 *	TH8_ERROR on domain error or if xMathFunc is unavailable.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathTranscendental(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    int packed = (int)(th8_int64_t)ctx;
    int op = TH8_MATH_CTX_OPCODE(packed);
    int nArity = TH8_MATH_CTX_ARITY(packed);
    double d1 = 0.0, d2 = 0.0, result;

    /* Bug 22 fix (2026-06-07): if arity says an argument is
     * required, reject NULL explicitly.  Without this check a
     * direct-API caller could pass NULL z1 to e.g. sin/cos and
     * silently get sin(0)=0 instead of an error.  The arity is
     * packed into ctx at registration time (see
     * th8RegisterMathFuncs).  arity == 0 (rand) means no operands
     * required; arity == 1 (sin, log, ...) means z1 required;
     * arity == 2 (atan2, pow, ...) means both z1 and z2. */
    if (nArity >= 1 && !z1) {
	Th8_SetResultStatic(
	    interp, "math function: missing first argument", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (nArity >= 2 && !z2) {
	Th8_SetResultStatic(
	    interp, "math function: missing second argument", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (z1 && TH8_OK != Th8_ToDouble(interp, z1, n1, &d1)) return TH8_ERROR;
    if (z2 && TH8_OK != Th8_ToDouble(interp, z2, n2, &d2)) return TH8_ERROR;

    {
	const Th8_Platform *pPlat = Th8_GetPlatform(interp);

	/* Bug 26: Th8_GetPlatform can return NULL during teardown.
	 * Split per Finding 005 sec. 5b: pPlat==NULL is intrinsic-
	 * dead from the test corpus (teardown happens after the
	 * suite completes); xMathFunc==NULL is the live arm. */
	if (!pPlat) {
	    Th8_SetResultStatic(
	        interp, "math function not available", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (!pPlat->xMathFunc) {
	    Th8_SetResultStatic(
	        interp, "math function not available", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (TH8_OK !=
	    pPlat->xMathFunc(interp, pPlat->pCtx, op, &result, d1, d2)) {
	    Th8_SetResultStatic(interp, "domain error", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }
    /*
     * Classification and signbit functions return integer results.
     */

    if (op >= TH8_MATH_SIGNBIT) {
	return Th8_SetResultInt(interp, (int)result);
    }
    Th8_SetResultDouble(interp, result);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MathEpsilon --
 *
 *	Return the machine epsilon for IEEE 754 double precision:
 *	the smallest value e such that 1.0 + e != 1.0.
 *	This is 2^-52 = 2.220446049250313e-16.
 *
 * Why / How:
 *	Implements the [expr epsilon()] math function.  The constant
 *	is hardcoded as the IEEE 754 double epsilon.  Takes no
 *	arguments.
 *
 * Results:
 *	TH8_OK with the epsilon value in the interp result.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathEpsilon(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    (void)ctx;
    (void)z1;
    (void)n1;
    (void)z2;
    (void)n2;
    Th8_SetResultDouble(interp, 2.2204460492503131e-16);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MathPi --
 *
 *	Return the value of Pi to the maximum precision available
 *	in a 64-bit IEEE 754 double (15-17 significant digits).
 *	The constant is specified as a long double literal and
 *	truncated to double to ensure all representable digits
 *	are correct.
 *
 * Why / How:
 *	Implements the [expr pi()] math function.  The constant is
 *	specified as a long double literal and implicitly truncated
 *	to double at compile time.  Takes no arguments.
 *
 * Results:
 *	TH8_OK with the value of Pi in the interp result.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathPi(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    (void)ctx;
    (void)z1;
    (void)n1;
    (void)z2;
    (void)n2;
    Th8_SetResultDouble(interp, 3.141592653589793238462643383279502884L);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MathRandom --
 *
 *	Return a cryptographically random 64-bit signed integer
 *	via the platform's xRandomBytes callback.  Returns
 *	TH8_ERROR if the callback is unavailable.
 *
 *	This is distinct from rand() which returns a pseudo-random
 *	double in [0,1) using a seeded LCG.  random() provides
 *	cryptographic-quality entropy suitable for key generation,
 *	nonces, and similar security-sensitive uses.
 *
 * Why / How:
 *	Implements the [expr random()] math function.  Fills a
 *	64-bit integer with bytes from the platform's xRandomBytes
 *	callback (which sources from /dev/urandom, CryptGenRandom,
 *	or equivalent).  Returns TH8_ERROR if no entropy source is
 *	available.
 *
 * Results:
 *	TH8_OK with a random wide integer in the interp result.
 *	TH8_ERROR if the platform lacks xRandomBytes.
 *
 * Side effects:
 *	Consumes entropy from the platform random source.
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathRandom(
    Th8_Interp *interp,
    void *ctx,
    const char *z1,
    size_t n1,
    const char *z2,
    size_t n2)
{
    th8_int64_t val;
    (void)ctx;
    (void)z1;
    (void)n1;
    (void)z2;
    (void)n2;

    if (Th8_RandomBytes(interp, &val, sizeof(val)) != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "cryptographic random not available", TH8_NOLEN);
	return TH8_ERROR;
    }
    return Th8_SetResultWideInt(interp, val);
}


/*
 * Registration table for built-in math functions.
 */

typedef struct {
    const char *zName;
    int nArg;
    Th8_MathFuncProc xProc;
    int opcode; /* For transcendentals: TH8_MATH_* opcode. */
} Th8_MathFuncInit;

static const Th8_MathFuncInit th8BuiltinMathFuncs[] =
    {{"abs", 1, th8MathAbs, 0},
     {"bool", 1, th8MathBool, 0},
     {"double", 1, th8MathDouble, 0},
     {"entier", 1, th8MathEntier, 0},
     {"epsilon", 0, th8MathEpsilon, 0},
     {"int", 1, th8MathInt, 0},
     {"isqrt", 1, th8MathIsqrt, 0},
     {"max", 2, th8MathMax, 0},
     {"min", 2, th8MathMin, 0},
     {"pi", 0, th8MathPi, 0},
     {"random", 0, th8MathRandom, 0},
     {"round", 1, th8MathRound, 0},
     {"typeof", 1, th8MathTypeof, 0},
     {"wide", 1, th8MathWide, 0},
     /* Transcendental functions -- dispatched via xMathFunc. */
     {"acos", 1, 0, TH8_MATH_ACOS},
     {"asin", 1, 0, TH8_MATH_ASIN},
     {"atan", 1, 0, TH8_MATH_ATAN},
     {"atan2", 2, 0, TH8_MATH_ATAN2},
     {"ceil", 1, 0, TH8_MATH_CEIL},
     {"cos", 1, 0, TH8_MATH_COS},
     {"cosh", 1, 0, TH8_MATH_COSH},
     {"exp", 1, 0, TH8_MATH_EXP},
     {"floor", 1, 0, TH8_MATH_FLOOR},
     {"fmod", 2, 0, TH8_MATH_FMOD},
     {"hypot", 2, 0, TH8_MATH_HYPOT},
     {"log", 1, 0, TH8_MATH_LOG},
     {"log10", 1, 0, TH8_MATH_LOG10},
     {"pow", 2, 0, TH8_MATH_POW},
     {"rand", 0, 0, TH8_MATH_RAND},
     {"sin", 1, 0, TH8_MATH_SIN},
     {"sinh", 1, 0, TH8_MATH_SINH},
     {"sqrt", 1, 0, TH8_MATH_SQRT},
     {"srand", 1, 0, TH8_MATH_SRAND},
     {"tan", 1, 0, TH8_MATH_TAN},
     {"tanh", 1, 0, TH8_MATH_TANH},
     /* TIP #745: C99 math functions (via xMathFunc). */
     {"acosh", 1, 0, TH8_MATH_ACOSH},
     {"asinh", 1, 0, TH8_MATH_ASINH},
     {"atanh", 1, 0, TH8_MATH_ATANH},
     {"cbrt", 1, 0, TH8_MATH_CBRT},
     {"copysign", 2, 0, TH8_MATH_COPYSIGN},
     {"dim", 2, 0, TH8_MATH_FDIM},
     {"erf", 1, 0, TH8_MATH_ERF},
     {"erfc", 1, 0, TH8_MATH_ERFC},
     {"exp2", 1, 0, TH8_MATH_EXP2},
     {"expm1", 1, 0, TH8_MATH_EXPM1},
     {"gamma", 1, 0, TH8_MATH_TGAMMA},
     {"ldexp", 2, 0, TH8_MATH_LDEXP},
     {"lgamma", 1, 0, TH8_MATH_LGAMMA},
     {"log1p", 1, 0, TH8_MATH_LOG1P},
     {"log2", 1, 0, TH8_MATH_LOG2},
     {"logb", 1, 0, TH8_MATH_LOGB},
     {"nextafter", 2, 0, TH8_MATH_NEXTAFTER},
     {"remainder", 2, 0, TH8_MATH_REMAINDER},
     {"signbit", 1, 0, TH8_MATH_SIGNBIT},
     {"trunc", 1, 0, TH8_MATH_TRUNC},
     /* TIP #521: Float classification (via xMathFunc).
     * fpclassify returns a string, so it uses a special callback. */
     {"fpclassify", 1, th8MathClassify, TH8_MATH_FPCLASSIFY},
     {"isfinite", 1, 0, TH8_MATH_ISFINITE},
     {"isinf", 1, 0, TH8_MATH_ISINF},
     {"isnan", 1, 0, TH8_MATH_ISNAN},
     {"isnormal", 1, 0, TH8_MATH_ISNORMAL},
     {"issubnormal", 1, 0, TH8_MATH_ISSUBNORMAL},
     {"isunordered", 2, 0, TH8_MATH_ISUNORDERED},
     {0, 0, 0, 0}};


/*
 *----------------------------------------------------------------------
 *
 * th8MathListCallback --
 *
 *	Hash iteration callback used by Th8_ListAppendMathFunctions
 *	to collect math function names.
 *
 * Why / How:
 *	Called once per hash entry by Th8_HashIterate.  If no glob
 *	pattern is set, or if the entry's key matches the pattern,
 *	the name is appended to the list being built.  The context
 *	array carries the interpreter, list pointers, and pattern.
 *
 * Results:
 *	TH8_OK (always continues iteration).
 *
 * Side effects:
 *	May append to the list via Th8_ListAppend.
 *
 *----------------------------------------------------------------------
 */

static int
th8MathListCallback(Th8_HashEntry *pEntry, void *pCtx)
{
    void **aCtx = (void **)pCtx;
    Th8_Interp *interp = (Th8_Interp *)aCtx[0];
    char **pzList = (char **)aCtx[1];
    size_t *pnList = (size_t *)aCtx[2];
    const char *zPat = (const char *)aCtx[3];
    size_t nPat = (size_t)(th8_int64_t)aCtx[4];

    if (!zPat ||
        Th8_GlobMatch(interp, zPat, nPat, pEntry->zKey, pEntry->nKey)) {
	Th8_ListAppend(interp, pzList, pnList, pEntry->zKey, pEntry->nKey);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendMathFunctions --
 *
 *	Append the names of registered math functions to a Tcl list
 *	string, optionally filtered by a glob pattern.
 *
 * Why / How:
 *	Iterates the math function hash table via Th8_HashIterate,
 *	using th8MathListCallback to filter and collect entries.
 *	Used by [info functions] to enumerate available math functions.
 *
 * Results:
 *	None (output is via the pzList/pnList out-parameters).
 *
 * Side effects:
 *	Appends to *pzList, may reallocate the list buffer.
 *
 *----------------------------------------------------------------------
 */

void
Th8_ListAppendMathFunctions(
    Th8_Interp *interp,
    char **pzList,
    size_t *pnList,
    const char *zPat,
    size_t nPat)
{
    if (Th8_GetMathFuncHash(interp)) {
	void *aCtx[5];

	aCtx[0] = interp;
	aCtx[1] = pzList;
	aCtx[2] = pnList;
	aCtx[3] = (void *)zPat;
	aCtx[4] = TH8_INT2PTR(nPat);
	Th8_HashIterate(
	    interp, Th8_GetMathFuncHash(interp), th8MathListCallback, aCtx);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8RegisterMathFuncEntry --
 *
 *	Register one built-in math function from its th8BuiltinMathFuncs[] entry.
 *
 * Why / How:
 *	Shared by th8RegisterMathFuncs (all) and th8RegisterOneMathFunc (by name,
 *	for named command subsets).  Assigns the direct callback or the generic
 *	th8MathTranscendental dispatcher (with the opcode+arity packed as
 *	context), matching the original inline loop body.
 *
 * Results:
 *	TH8_OK, or TH8_ERROR on a real registration failure (e.g. OOM).
 *
 * Side effects:
 *	Creates a math-function entry in the interpreter's hash table.
 *
 *----------------------------------------------------------------------
 */

static int
th8RegisterMathFuncEntry(Th8_Interp *interp, const Th8_MathFuncInit *p)
{
    Th8_MathFuncProc xProc = p->xProc;
    void *pCtx = 0;

    if (p->opcode) {
	/* Pass opcode (low 16 bits) + arity (next 8 bits) as context for
	 * dispatcher callbacks (Bug 22 fix 2026-06-07: arity is needed so the
	 * dispatcher can reject NULL operands when arity > 0, instead of
	 * silently treating them as 0.0). */
	pCtx = TH8_INT2PTR(TH8_MATH_CTX_PACK(p->nArg, p->opcode));
    }
    if (!xProc) {
	/* No direct callback: use transcendental dispatcher. */
	xProc = th8MathTranscendental;
    }
    return Th8_CreateMathFunc(
        interp, p->zName, Th8_Strlen(interp, p->zName), p->nArg, xProc, pCtx);
}

/*
 *----------------------------------------------------------------------
 *
 * th8RegisterMathFuncs --
 *
 *	Register all built-in math functions from the static
 *	th8BuiltinMathFuncs[] table into the interpreter.
 *
 * Why / How:
 *	Called once per interpreter from Th8_RegisterLanguage (and from the
 *	"expressions" plugin subset).  Iterates the init table via
 *	th8RegisterMathFuncEntry.
 *
 * Results:
 *	TH8_OK if every built-in math function is registered; TH8_ERROR
 *	on a real registration failure (e.g. OOM), propagated so the
 *	language is not reported complete with missing math functions
 *	(TH8K-006).
 *
 * Side effects:
 *	Creates math function entries in the interpreter's hash table.
 *
 *----------------------------------------------------------------------
 */

int
th8RegisterMathFuncs(Th8_Interp *interp)
{
    const Th8_MathFuncInit *p;

    for (p = th8BuiltinMathFuncs; p->zName; p++) {
	if (th8RegisterMathFuncEntry(interp, p) != TH8_OK) {
	    return TH8_ERROR;
	}
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8FindMathFunc --
 *
 *	Return 1 if zName is a built-in math function (pure lookup, no
 *	mutation), else 0.  Used by the subset resolver to validate a FUNCTION
 *	member up front, so an unknown member registers nothing.
 *
 * Why / How:
 *	Linear scan of th8BuiltinMathFuncs[] comparing the name.
 *
 * Results:
 *	1 if found, else 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8FindMathFunc(Th8_Interp *interp, const char *zName, size_t nName)
{
    const Th8_MathFuncInit *p;

    for (p = th8BuiltinMathFuncs; p->zName; p++) {
	if (Th8_Strlen(interp, p->zName) == nName &&
	    Th8_Memcmp(interp, p->zName, zName, nName) == 0) {
	    return 1;
	}
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8RegisterOneMathFunc --
 *
 *	Register one built-in math function by name (for a FUNCTION subset
 *	member), assuming the caller validated it with th8FindMathFunc.
 *
 * Why / How:
 *	Linear scan of th8BuiltinMathFuncs[] for the name, then
 *	th8RegisterMathFuncEntry on the match.
 *
 * Results:
 *	TH8_OK if registered (or the name is unknown -- a no-op), TH8_ERROR on
 *	OOM.
 *
 * Side effects:
 *	Creates a math-function entry in the interpreter's hash table.
 *
 *----------------------------------------------------------------------
 */

int
th8RegisterOneMathFunc(Th8_Interp *interp, const char *zName, size_t nName)
{
    const Th8_MathFuncInit *p;

    for (p = th8BuiltinMathFuncs; p->zName; p++) {
	if (Th8_Strlen(interp, p->zName) == nName &&
	    Th8_Memcmp(interp, p->zName, zName, nName) == 0) {
	    return th8RegisterMathFuncEntry(interp, p);
	}
    }
    return TH8_OK; /* Unknown name: caller validated with th8FindMathFunc. */
}

#endif /* TH8_ENABLE_EXPRESSIONS */
