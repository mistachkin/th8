/*
 * th8_libc.c -- C runtime platform module for TH8.
 *
 * This is the ONLY file (besides platform-specific modules and
 * bestline) that directly calls C standard library functions.
 * All other TH8 code routes through Th8_Platform callbacks.
 *
 * Provides:
 *   - Memory: calloc, realloc, free (xMalloc, xRealloc, xFree)
 *   - Mem ops: memcpy, memmove, memset, memcmp
 *   - I/O: fgets (xInput), fwrite (xOutput, xOutputError)
 *   - Math: 21 transcendental functions via xMathFunc dispatch
 *   - String: strlen, strcmp, strchr, strncpy
 *   - Utility: atoi, qsort, vsnprintf
 *
 * Usage:
 *     Th8_Platform plat = *Th8_GetPosixPlatform();
 *     Th8_MergePlatform(&plat, Th8_GetLibcPlatform());
 *     interp = Th8_CreateInterp(&plat);
 *
 * The OS platform files (th8_posix.c, th8_win32.c) leave the
 * memory and CRT slots NULL, relying on Th8_MergePlatform to
 * fill them from this module.  This eliminates duplication and
 * keeps CRT usage in a single place.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"

#if defined(_WIN32) || defined(WIN32)
#  include "th8_meta_msvc.h"
#  include "th8_meta_win32.h"
#elif defined(__APPLE__)
#  include "th8_meta_posix.h"
#  include "th8_meta_macos.h"
#else
#  include "th8_meta_posix.h"
#endif

#include "th8.h"    /* For TH8_PLATFORM_LIBC auto-detection. */

#if defined(TH8_PLATFORM_LIBC)

#  include "th8_int.h"
#  include "th8_mem.h"


/*
 *----------------------------------------------------------------------
 *
 * th8LibcHeapCheck (macOS, debug) --
 *
 *	Validate the default malloc zone's internal consistency -- the
 *	system-malloc analogue of Win32 HeapValidate -- before each
 *	libc xMalloc / xRealloc / xFree.  Gated on TH8_HEAP_CHECKS
 *	(defined for debug builds).  Detects allocator-metadata
 *	corruption (free-list / chunk-header damage) at the earliest
 *	allocation boundary after it occurs; on failure it names the
 *	operation and aborts so the fault is caught near its source.
 *
 *	Cost is O(heap) per check, so the frequency is tunable at
 *	runtime via TH8_HEAP_CHECK_EVERY (check 1 in N calls; default
 *	1 = every call).  We pass malloc_default_zone(), not NULL:
 *	NULL checks ALL zones, but TH8's libc allocator lives in the
 *	default (initial) zone.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_HEAP_CHECKS) && defined(__APPLE__)

#    ifndef TH8_HEAP_CHECK_EVERY
#      define TH8_HEAP_CHECK_EVERY 1
#    endif

static void
th8LibcHeapCheck(const char *zWhere)
{
    static unsigned long nEvery = 0; /* 0 == not yet resolved */
    static unsigned long nCount = 0;

    if (nEvery == 0) {
	const char *zEnv = getenv("TH8_HEAP_CHECK_EVERY");
	unsigned long v = zEnv ? (unsigned long)strtoul(zEnv, NULL, 10) : 0;

	nEvery = v ? v : (unsigned long)TH8_HEAP_CHECK_EVERY;
    }
    if ((++nCount % nEvery) != 0) return;
    if (!malloc_zone_check(malloc_default_zone())) {
	fprintf(
	    stderr,
	    "TH8_HEAP_CHECKS: default-zone corruption detected before "
	    "%s (op #%lu)\n",
	    zWhere, nCount);
	fflush(stderr);
	abort();
    }
}
#    define TH8_LIBC_HEAP_CHECK(w) th8LibcHeapCheck((w))
#  else
#    define TH8_LIBC_HEAP_CHECK(w) ((void)0)
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8LibcMalloc --
 *
 *	Allocate zero-initialized memory via calloc.  Used as the
 *	default xMalloc callback for the libc platform.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMalloc callback.  Uses calloc
 *	rather than malloc to guarantee zero-initialized memory, which
 *	is part of the TH8 xMalloc contract.
 *
 * Results:
 *	Pointer to allocated memory, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory from the C heap.
 *
 *----------------------------------------------------------------------
 */

static void *
th8LibcMalloc(Th8_Interp *interp, void *pCtx, size_t nByte)
{
    (void)interp;
    (void)pCtx;
    TH8_LIBC_HEAP_CHECK("xMalloc");
    return th8_calloc(1, nByte);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcRealloc --
 *
 *	Reallocate memory via the C realloc function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xRealloc callback.  Delegates
 *	directly to the C library realloc.
 *
 * Results:
 *	Pointer to reallocated memory, or NULL on failure.
 *
 * Side effects:
 *	Reallocates memory on the C heap.
 *
 *----------------------------------------------------------------------
 */

static void *
th8LibcRealloc(Th8_Interp *interp, void *pCtx, void *p, size_t nByte)
{
    (void)interp;
    (void)pCtx;
    TH8_LIBC_HEAP_CHECK("xRealloc");
    return th8_realloc(p, nByte);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcFree --
 *
 *	Free memory via the C free function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xFree callback.  Delegates
 *	directly to the C library free.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory on the C heap.
 *
 *----------------------------------------------------------------------
 */

static void
th8LibcFree(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;
    TH8_LIBC_HEAP_CHECK("xFree");
    th8_free(p);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemorySize --
 *
 *	Return the usable size of an allocated block.
 *	Platform-specific: uses malloc_size (macOS),
 *	malloc_usable_size (Linux/BSD), or _msize (Windows).
 *
 * Results:
 *	The usable size of the allocation, or 0 if p is NULL.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemorySize callback.  Each
 *	platform variant calls the appropriate OS-specific introspection
 *	function: malloc_size (macOS), malloc_usable_size (Linux/BSD),
 *	or _msize (Windows).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#  if defined(__APPLE__)
/* <malloc/malloc.h> included via th8_meta_libc.h */
/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemorySize (macOS) --
 *
 *	`Th8_Platform.xMemorySize` callback that returns the
 *	usable byte-size of an allocation produced by the
 *	system `malloc`.  Routes through Apple's
 *	`malloc_size(p)`.  NULL pointer reports 0 (consistent
 *	with the BSD / Win32 variants below).
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	p      -- allocation, or NULL.
 *
 * Returns:
 *	Allocation size in bytes, or 0 for NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static size_t
th8LibcMemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;
    return p ? malloc_size(p) : 0;
}
#  elif defined(__linux__) || defined(__FreeBSD__) ||                        \
      defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
/* <malloc.h> included via th8_meta_libc.h */
/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemorySize (Linux / BSD) --
 *
 *	`Th8_Platform.xMemorySize` callback that returns the
 *	usable byte-size of an allocation produced by the
 *	system `malloc`.  Routes through glibc /
 *	BSD `malloc_usable_size(p)`.  NULL pointer reports 0.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	p      -- allocation, or NULL.
 *
 * Returns:
 *	Allocation size in bytes, or 0 for NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static size_t
th8LibcMemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;
    return p ? malloc_usable_size(p) : 0;
}
#  elif defined(_MSC_VER) || defined(_WIN32)
/* <malloc.h> included via th8_meta_libc.h */
/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemorySize (Win32) --
 *
 *	`Th8_Platform.xMemorySize` callback that returns the
 *	usable byte-size of an allocation produced by the
 *	system `malloc`.  Routes through MSVC's `_msize(p)`.
 *	NULL pointer reports 0.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *	p      -- allocation, or NULL.
 *
 * Returns:
 *	Allocation size in bytes, or 0 for NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static size_t
th8LibcMemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;
    return p ? _msize(p) : 0;
}
#  else
#    error                                                                    \
	"No malloc_size / malloc_usable_size / _msize available on this platform"
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemcpy --
 *
 *	Wrapper for the C memcpy function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemcpy callback.  Delegates
 *	directly to the C library memcpy.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Copies n bytes from src to dst.
 *
 *----------------------------------------------------------------------
 */

static void *
th8LibcMemcpy(
    Th8_Interp *interp,
    void *pCtx,
    void *dst,
    const void *src,
    size_t n)
{
    (void)interp;
    (void)pCtx;
    return memcpy(dst, src, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemmove --
 *
 *	Wrapper for the C memmove function.  Handles overlapping
 *	source and destination regions.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemmove callback.  Delegates
 *	directly to the C library memmove, which is safe for
 *	overlapping regions unlike memcpy.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Copies n bytes from src to dst.
 *
 *----------------------------------------------------------------------
 */

static void *
th8LibcMemmove(
    Th8_Interp *interp,
    void *pCtx,
    void *dst,
    const void *src,
    size_t n)
{
    (void)interp;
    (void)pCtx;
    return memmove(dst, src, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemset --
 *
 *	Wrapper for the C memset function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemset callback.  On glibc 2.25+,
 *	uses explicit_bzero for zero-fills to prevent dead-store
 *	elimination of security-sensitive memory clears.  Otherwise
 *	delegates to the C library memset.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Fills n bytes of dst with value c.
 *
 *----------------------------------------------------------------------
 */

static void *
th8LibcMemset(Th8_Interp *interp, void *pCtx, void *dst, int c, size_t n)
{
    (void)interp;
    (void)pCtx;
#  if defined(__GLIBC__) && defined(__GLIBC_MINOR__) &&                      \
      (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
    if (c == 0) {
	explicit_bzero(dst, n);
	return dst;
    }
#  endif
    return memset(dst, c, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcMemcmp --
 *
 *	Wrapper for the C memcmp function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemcmp callback.  Delegates
 *	directly to the C library memcmp.
 *
 * Results:
 *	Negative, zero, or positive integer indicating comparison.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcMemcmp(
    Th8_Interp *interp,
    void *pCtx,
    const void *a,
    const void *b,
    size_t n)
{
    (void)interp;
    (void)pCtx;
    return memcmp(a, b, n);
}


/*
 * Thread-local storage qualifier (same logic as th8_core.c).
 */

#  if defined(_MSC_VER)
#    define TH8_LIBC_TLS     __declspec(thread)
#    define TH8_LIBC_HAS_TLS 0  /* MSVC TLS needs special DLL handling */
#  elif defined(__GNUC__) || defined(__clang__)
#    define TH8_LIBC_TLS     __thread
#    define TH8_LIBC_HAS_TLS 1
#  else
#    define TH8_LIBC_TLS
#    define TH8_LIBC_HAS_TLS 0
#  endif

#  if TH8_LIBC_HAS_TLS
#    define th8MaybeGlobalMutexEnter(interp)
#    define th8MaybeGlobalMutexLeave(interp)
#  else
#    define th8MaybeGlobalMutexEnter(interp) th8GlobalMutexEnter(interp)
#    define th8MaybeGlobalMutexLeave(interp) th8GlobalMutexLeave(interp)
#  endif

/*
 * Per-thread random state.  Uses a linear congruential generator
 * (LCG) instead of the non-reentrant C library rand()/srand().
 * When TLS is available, each thread has its own state and no
 * locking is needed.  Without TLS, the global mutex is used.
 * Adequate for scripting use but not cryptographic.
 */

static TH8_LIBC_TLS unsigned long th8RandState = 0;
static TH8_LIBC_TLS int th8RandSeeded = 0;


/*
 *----------------------------------------------------------------------
 *
 * th8LibcMathFunc --
 *
 *	Dispatch a math operation to the corresponding libm
 *	function.  Unary ops use 'a'; binary ops use 'a' and 'b'.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMathFunc callback.  A single
 *	switch dispatches all 40+ TH8_MATH_* opcodes to their
 *	corresponding libm functions.  Domain checks (e.g., acos
 *	input range, sqrt negativity) are performed before calling
 *	the libm function to return TH8_ERROR instead of NaN.
 *	rand/srand use a thread-local LCG to avoid the non-reentrant
 *	C library rand().
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on domain error.
 *
 * Side effects:
 *	rand/srand modify static LCG state under the global mutex.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcMathFunc(
    Th8_Interp *interp,  /* Interpreter (unused). */
    void *pCtx,   /* Not used. */
    int op,   /* TH8_MATH_* operation code. */
    double *pResult,  /* OUT: result value. */
    double a,   /* First argument. */
    double b)   /* Second argument (if binary). */
{
    (void)interp;
    (void)pCtx;

    switch (op) {
    case TH8_MATH_ACOS:
	if (a < -1.0 || a > 1.0) return TH8_ERROR;
	*pResult = acos(a);
	break;
    case TH8_MATH_ASIN:
	if (a < -1.0 || a > 1.0) return TH8_ERROR;
	*pResult = asin(a);
	break;
    case TH8_MATH_ATAN:
	*pResult = atan(a);
	break;
    case TH8_MATH_ATAN2:
	*pResult = atan2(a, b);
	break;
    case TH8_MATH_CEIL:
	*pResult = ceil(a);
	break;
    case TH8_MATH_COS:
	*pResult = cos(a);
	break;
    case TH8_MATH_COSH:
	*pResult = cosh(a);
	break;
    case TH8_MATH_EXP:
	*pResult = exp(a);
	break;
    case TH8_MATH_FLOOR:
	*pResult = floor(a);
	break;
    case TH8_MATH_FMOD:
	if (b == 0.0) return TH8_ERROR;
	*pResult = fmod(a, b);
	break;
    case TH8_MATH_HYPOT:
	*pResult = hypot(a, b);
	break;
    case TH8_MATH_LOG:
	if (a <= 0.0) return TH8_ERROR;
	*pResult = log(a);
	break;
    case TH8_MATH_LOG10:
	if (a <= 0.0) return TH8_ERROR;
	*pResult = log10(a);
	break;
    case TH8_MATH_POW:
	*pResult = pow(a, b);
	break;
    case TH8_MATH_RAND:
	th8MaybeGlobalMutexEnter(NULL);
	if (!th8RandSeeded) {
	    th8RandState = 1;
	    th8RandSeeded = 1;
	}
	th8RandState = th8RandState * 1103515245UL + 12345UL;
	*pResult = (double)((th8RandState >> 16) & 0x7fff) / 32768.0;
	th8MaybeGlobalMutexLeave(NULL);
	break;
    case TH8_MATH_SIN:
	*pResult = sin(a);
	break;
    case TH8_MATH_SINH:
	*pResult = sinh(a);
	break;
    case TH8_MATH_SQRT:
	if (a < 0.0) return TH8_ERROR;
	*pResult = sqrt(a);
	break;
    case TH8_MATH_SRAND:
	th8MaybeGlobalMutexEnter(NULL);
	th8RandState = (unsigned long)(unsigned int)a;
	th8RandSeeded = 1;
	th8MaybeGlobalMutexLeave(NULL);
	*pResult = 0.0;
	break;
    case TH8_MATH_TAN:
	*pResult = tan(a);
	break;
    case TH8_MATH_TANH:
	*pResult = tanh(a);
	break;

    /* TIP #745: C99 math functions. */
    case TH8_MATH_ACOSH:
	*pResult = acosh(a);
	break;
    case TH8_MATH_ASINH:
	*pResult = asinh(a);
	break;
    case TH8_MATH_ATANH:
	if (a <= -1.0 || a >= 1.0) return TH8_ERROR;
	*pResult = atanh(a);
	break;
    case TH8_MATH_CBRT: {
	double r = cbrt(a);
	    /*
	     * Snap to exact integer if r^3 == a.  Some libm
	     * implementations (glibc) return results with a
	     * 1-ULP error for perfect cubes (e.g., cbrt(27)
	     * returns 3.0000000000000004 instead of 3.0).
	     */
	double ri = (r >= 0.0) ? floor(r + 0.5) : ceil(r - 0.5);
	if (ri * ri * ri == a) r = ri;
	*pResult = r;
    } break;
    case TH8_MATH_COPYSIGN:
	*pResult = copysign(a, b);
	break;
    case TH8_MATH_ERF:
	*pResult = erf(a);
	break;
    case TH8_MATH_ERFC:
	*pResult = erfc(a);
	break;
    case TH8_MATH_EXP2:
	*pResult = exp2(a);
	break;
    case TH8_MATH_EXPM1:
	*pResult = expm1(a);
	break;
    case TH8_MATH_FDIM:
	*pResult = fdim(a, b);
	break;
    case TH8_MATH_LGAMMA:
	*pResult = lgamma(a);
	break;
    case TH8_MATH_LOG1P:
	if (a <= -1.0) return TH8_ERROR;
	*pResult = log1p(a);
	break;
    case TH8_MATH_LOG2:
	if (a <= 0.0) return TH8_ERROR;
	*pResult = log2(a);
	break;
    case TH8_MATH_LOGB:
	*pResult = logb(a);
	break;
    case TH8_MATH_NEXTAFTER:
	*pResult = nextafter(a, b);
	break;
    case TH8_MATH_REMAINDER:
	if (b == 0.0) return TH8_ERROR;
	*pResult = remainder(a, b);
	break;
    case TH8_MATH_TGAMMA:
	*pResult = tgamma(a);
	break;
    case TH8_MATH_TRUNC:
	*pResult = trunc(a);
	break;
    case TH8_MATH_LDEXP:
	*pResult = ldexp(a, (int)b);
	break;
    case TH8_MATH_SIGNBIT:
	*pResult = signbit(a) ? 1.0 : 0.0;
	break;

    /* TIP #521: Float classification. */
    case TH8_MATH_ISFINITE:
	*pResult = isfinite(a) ? 1.0 : 0.0;
	break;
    case TH8_MATH_ISINF:
	*pResult = isinf(a) ? 1.0 : 0.0;
	break;
    case TH8_MATH_ISNAN:
	*pResult = isnan(a) ? 1.0 : 0.0;
	break;
    case TH8_MATH_ISNORMAL:
	*pResult = isnormal(a) ? 1.0 : 0.0;
	break;
    case TH8_MATH_ISSUBNORMAL:
	*pResult = (fpclassify(a) == FP_SUBNORMAL) ? 1.0 : 0.0;
	break;
    case TH8_MATH_ISUNORDERED:
	*pResult = (isnan(a) || isnan(b)) ? 1.0 : 0.0;
	break;
    case TH8_MATH_FPCLASSIFY:
	/* Encode: 0=zero, 1=subnormal, 2=normal, 3=infinite, 4=nan */
	switch (fpclassify(a)) {
	case FP_ZERO:
	    *pResult = 0.0;
	    break;
	case FP_SUBNORMAL:
	    *pResult = 1.0;
	    break;
	case FP_NORMAL:
	    *pResult = 2.0;
	    break;
	case FP_INFINITE:
	    *pResult = 3.0;
	    break;
	case FP_NAN:
	    *pResult = 4.0;
	    break;
	default:
	    *pResult = -1.0;
	    break;
	}
	break;

    default:
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcStrlen --
 *
 *	Wrapper for the C strlen function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xStrlen callback.  Delegates
 *	directly to the C library strlen.
 *
 * Results:
 *	Length of the NUL-terminated string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8LibcStrlen(Th8_Interp *interp, void *pCtx, const char *s)
{
    (void)interp;
    (void)pCtx;
    return strlen(s);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcStrcmp --
 *
 *	Wrapper for the C strcmp function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xStrcmp callback.  Delegates
 *	directly to the C library strcmp.
 *
 * Results:
 *	Negative, zero, or positive integer indicating comparison.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcStrcmp(Th8_Interp *interp, void *pCtx, const char *s1, const char *s2)
{
    (void)interp;
    (void)pCtx;
    return strcmp(s1, s2);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcStrchr --
 *
 *	Wrapper for the C strchr function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xStrchr callback.  Delegates
 *	directly to the C library strchr.
 *
 * Results:
 *	Pointer to the first occurrence of c in s, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
th8LibcStrchr(Th8_Interp *interp, void *pCtx, const char *s, int c)
{
    (void)interp;
    (void)pCtx;
    return strchr(s, c);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcAtoi --
 *
 *	Wrapper for the C atoi function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xAtoi callback.  Delegates
 *	directly to the C library atoi.
 *
 * Results:
 *	Integer value of the string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcAtoi(Th8_Interp *interp, void *pCtx, const char *s)
{
    (void)interp;
    (void)pCtx;
    return atoi(s);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcQsort --
 *
 *	Wrapper for the C qsort function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xQsort callback.  Delegates
 *	directly to the C library qsort.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sorts the array in place.
 *
 *----------------------------------------------------------------------
 */

static void
th8LibcQsort(
    Th8_Interp *interp,
    void *pCtx,
    void *base,
    size_t nmemb,
    size_t size,
    int (*cmp)(const void *, const void *))
{
    (void)interp;
    (void)pCtx;
    qsort(base, nmemb, size, cmp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcVsnprintf --
 *
 *	Wrapper for the C vsnprintf function.
 *
 * Why / How:
 *	Implements the Th8_Platform.xVsnprintf callback.  Delegates
 *	directly to the C library vsnprintf.
 *
 * Results:
 *	Number of characters written (excluding NUL).
 *
 * Side effects:
 *	Writes formatted output into buf.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcVsnprintf(
    Th8_Interp *interp,
    void *pCtx,
    char *buf,
    size_t size,
    const char *fmt,
    va_list ap)
{
    (void)interp;
    (void)pCtx;
    return vsnprintf(buf, size, fmt, ap);
}


/*
 *----------------------------------------------------------------------
 *
 * Standard I/O callbacks -- ANSI C stdio for stdin, stdout, stderr.
 *
 * These use fgets / fwrite / fflush, which are portable across all
 * hosted C99 environments.  OS-specific platform files may override
 * the output callbacks with native APIs (e.g. WriteFile on Win32).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8LibcInput --
 *
 *	Read a line of input via the C library fgets function,
 *	returning a heap-allocated copy.
 *
 * Why / How:
 *	Implements the Th8_Platform.xInput callback.  Reads up to
 *	4096 bytes from the given channel (defaulting to stdin) using
 *	fgets, then allocates a copy via Th8_AttemptMalloc so the
 *	caller owns the buffer.
 *
 * Results:
 *	TH8_OK on success with *pzOut and *pnOut set.
 *	TH8_ERROR on EOF or allocation failure.
 *
 * Side effects:
 *	Reads from the file stream.  Allocates memory.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcInput(
    Th8_Interp *interp, /* Interpreter. */
    void *pCtx,  /* Platform's pCtx (unused). */
    char **pzOut,
    size_t *pnOut,
    void *pChannel) /* Channel (unused). */
{
    char zBuf[4096];
    char *zResult;
    size_t n;

    (void)pCtx;

    if (!interp ||
        !fgets(zBuf, sizeof(zBuf), pChannel ? (FILE *)pChannel : stdin)) {
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }
    n = strlen(zBuf);
    zResult = (char *)TH8_ALLOC_STR(interp, n);
    if (!zResult) {
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }
    memcpy(zResult, zBuf, n + 1);
    *pzOut = zResult;
    *pnOut = n;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcOutput --
 *
 *	Write data to the standard output stream via fwrite/fflush.
 *
 * Why / How:
 *	Implements the Th8_Platform.xOutput callback.  Writes the
 *	given buffer to the channel (defaulting to stdout) using
 *	fwrite, then flushes immediately with fflush to ensure
 *	output is visible in interactive use.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if fwrite fails.
 *
 * Side effects:
 *	Writes to the file stream.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcOutput(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx,  /* Platform's pCtx (unused). */
    const char *z,
    size_t n,
    void *pChannel)
{
    FILE *fp = pChannel ? (FILE *)pChannel : stdout;

    (void)interp;
    (void)pCtx;

    if (fwrite(z, 1, n, fp) != n) {
	return TH8_ERROR;
    }
    fflush(fp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8LibcOutputError --
 *
 *	Write data to the standard error stream via fwrite/fflush.
 *
 * Why / How:
 *	Implements the Th8_Platform.xOutputError callback.  Identical
 *	to th8LibcOutput except the default channel is stderr rather
 *	than stdout.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if fwrite fails.
 *
 * Side effects:
 *	Writes to the file stream.
 *
 *----------------------------------------------------------------------
 */

static int
th8LibcOutputError(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx,  /* Platform's pCtx (unused). */
    const char *z,
    size_t n,
    void *pChannel)
{
    FILE *fp = pChannel ? (FILE *)pChannel : stderr;

    (void)interp;
    (void)pCtx;

    if (fwrite(z, 1, n, fp) != n) {
	return TH8_ERROR;
    }
    fflush(fp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetLibcPlatform --
 *
 *	Return a platform struct with C runtime callbacks for
 *	math, string, and utility functions.  Merge this into
 *	an OS platform to enable full CRT routing.
 *
 * Why / How:
 *	The libc platform is the single place where C standard library
 *	functions are bound to Th8_Platform callbacks.  OS platforms
 *	(POSIX, Win32) leave CRT slots NULL, relying on
 *	Th8_MergePlatform to fill them from this module.  The static
 *	struct is initialized once (not thread-safe; call before
 *	creating interpreters on other threads).
 *
 * Results:
 *	Pointer to a static Th8_Platform.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetLibcPlatform(void)
{
    static Th8_Platform sLibc;
    static int bInit = 0;

    if (!bInit) {
	sLibc.nVersion = 5;

	/* Memory allocation */
	sLibc.xMalloc = th8LibcMalloc;
	sLibc.xRealloc = th8LibcRealloc;
	sLibc.xFree = th8LibcFree;
	sLibc.xMemorySize = th8LibcMemorySize;

	/* Memory operations */
	sLibc.xMemcpy = th8LibcMemcpy;
	sLibc.xMemmove = th8LibcMemmove;
	sLibc.xMemset = th8LibcMemset;
	sLibc.xMemcmp = th8LibcMemcmp;

	/* String / utility */
	sLibc.xStrlen = th8LibcStrlen;
	sLibc.xStrcmp = th8LibcStrcmp;
	sLibc.xStrchr = th8LibcStrchr;
	sLibc.xAtoi = th8LibcAtoi;
	sLibc.xQsort = th8LibcQsort;
	sLibc.xVsnprintf = th8LibcVsnprintf;

	/* Standard I/O */
	sLibc.xInput = th8LibcInput;
	sLibc.xOutput = th8LibcOutput;
	sLibc.xOutputError = th8LibcOutputError;

	/* Math */
	sLibc.xMathFunc = th8LibcMathFunc;

	/* nVersion 5 adds xStackBackTrace, but that is a compiler-runtime
	 * facility (not ANSI C), so it lives in th8_unwind.c and is supplied
	 * by merging th8GetUnwindPlatform(); libc leaves the slot NULL. */

	bInit = 1;
    }
    return &sLibc;
}

#endif /* TH8_PLATFORM_LIBC */
