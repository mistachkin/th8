/*
 * th8_regex.c -- TH8 regex integration.
 *
 * This file is compiled only when TH8_ENABLE_REGEXP is defined.
 *
 * Provides three layers:
 *
 *   1. BRIDGE FUNCTIONS -- Route memory allocation, C runtime
 *      calls, and interrupt/stack checks from the Spencer regex
 *      engine through the TH8 platform abstraction.  The engine
 *      calls these via #define macros in regcustom_th8.h.
 *
 *   2. UTF-8 CONVERSION -- The Spencer engine operates on
 *      fixed-width 32-bit chr arrays internally.  th8Utf8ToChr
 *      and th8ChrToUtf8 convert between TH8's UTF-8 strings
 *      and the engine's chr[] arrays at the API boundary.
 *
 *   3. TCL COMMANDS -- [regexp] and [regsub] with full Tcl 8.4
 *      switch support including -all, -inline, -indices, -nocase,
 *      -expanded, -line, -linestop, -lineanchor, and -start.
 *
 * ARCHITECTURE NOTE: The Spencer engine is not thread-safe.
 * A global interpreter pointer (th8_regex_interp) is set before
 * each regex operation and cleared afterward.  This is safe
 * because TH8 is single-threaded per interpreter.  The OOM
 * flag (th8_regex_oom_flag) is the mechanism for converting
 * TH8 cancellation/limit errors into the engine's out-of-memory
 * abort path.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * The regex engine itself is Copyright (c) 1998, 1999 Henry Spencer,
 * with PostgreSQL modifications.  See externals/regex/COPYRIGHT.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_util.h"
#include "th8_plugin.h"
#include "ConvertUTF_v2.h"

#if defined(TH8_ENABLE_REGEXP)

/*
 * Forward declarations for bridge functions.  These are called by
 * the Spencer engine via macros in regcustom_th8.h.  We declare
 * them here so this file compiles standalone (the Spencer engine
 * files are compiled separately).
 */

void *th8_regex_malloc(size_t n);
void th8_regex_free(void *p);
void *th8_regex_realloc(void *p, size_t n);
int th8_regex_interrupted(void);
int th8_regex_stack_too_deep(void);
int th8_regex_isalnum(unsigned int c);
int th8_regex_isalpha(unsigned int c);
int th8_regex_isdigit(unsigned int c);
int th8_regex_isspace(unsigned int c);

/* CRT bridge functions (also declared in regcustom_th8.h). */
void *th8_regex_memcpy(void *dst, const void *src, size_t n);
int th8_regex_memcmp(const void *a, const void *b, size_t n);
void *th8_regex_memset(void *dst, int c, size_t n);
size_t th8_regex_strlen(const char *s);
int th8_regex_strcmp(const char *s1, const char *s2);
char *th8_regex_strcpy(char *dst, const char *src);
char *th8_regex_strchr(const char *s, int c);
int th8_regex_sprintf(char *buf, const char *fmt, ...);
int th8_regex_atoi(const char *s);
void th8_regex_qsort(
    void *base,
    size_t nmemb,
    size_t size,
    int (*cmp)(const void *, const void *));

/*
 * Include the TH8 regex interface for th8_regex_t etc.
 */

#  include "regex_th8.h"

/*
 * Rename pg_ functions to th8_ (must match regcustom_th8.h).
 */

#  define pg_regcomp  th8_regcomp
#  define pg_regexec  th8_regexec
#  define pg_regfree  th8_regfree
#  define pg_regerror th8_regerror

/*
 * Forward declaration for the analyze function injected into
 * regexec.c by the vendoring script.
 */

extern void th8_regex_analyze(
    const regex_t *re,
    int *pnStates,
    int *pnSubre,
    int *pnLacons,
    int *pbMatchAll,
    int *pbBackref,
    int *pbLookaround);

/* th8StrEq is now in th8_util.c */

/*
 * The current interpreter for the regex engine's MALLOC/FREE/INTERRUPT
 * hooks.  Set before each regex operation.  TH8_THREAD_LOCAL ensures
 * each thread has its own copy; th8MaybeGlobalMutexEnter/Leave
 * protect platforms without real TLS.
 *
 * Both are static -- the Spencer regex engine accesses them only
 * through the public API functions declared in regcustom_th8.h
 * (th8_regex_malloc, th8_regex_free, th8_regex_realloc,
 * th8_regex_interrupted).
 */

static TH8_THREAD_LOCAL Th8_Interp *volatile th8_regex_interp = 0;

/*
 * When th8_regex_interrupted() detects a limit/cancel/stack
 * condition, it sets this flag.  th8_regex_malloc checks it
 * and returns NULL for all subsequent calls, causing the
 * Spencer engine to abort via its out-of-memory error path.
 */

static TH8_THREAD_LOCAL volatile int th8_regex_oom_flag = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8_regex_malloc --
 *
 *	Allocate memory on behalf of the Spencer regex engine.
 *	Routes through Th8_AttemptMalloc using the global regex
 *	interpreter.  Returns NULL if no interpreter is set or if
 *	the OOM flag has been raised.
 *
 * Results:
 *	Pointer to allocated memory, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory via the platform allocator.
 *
 *----------------------------------------------------------------------
 */

void *
th8_regex_malloc(size_t n)
{
    if (th8_regex_oom_flag) return NULL;
    /* Bug 26 family: th8_regex_interp is set by
     * th8RegexSetup and cleared by th8RegexTeardown around
     * each regex operation; the engine can be invoked
     * transiently from contexts where setup hasn't run
     * (early init, post-teardown cleanup, the Bug 31-style
     * fault-timing windows).  NEVER would collapse under
     * TH8_OMIT_AUXILIARY_SAFETY_CHECKS and let the
     * subsequent TH8_ALLOC deref a NULL interp; plain
     * guard returns NULL per the standard malloc-OOM
     * contract.  Mirrors the Bug 31 fix on the libtommath
     * allocator bridge (th8_bigint_*). */
    if (!th8_regex_interp) return NULL;
    if (Th8_Ready(th8_regex_interp) != TH8_OK) {
	th8_regex_oom_flag = 1;
	return NULL;
    }
    return TH8_ALLOC(th8_regex_interp, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_free --
 *
 *	Free memory allocated by th8_regex_malloc.  Routes through
 *	Th8_Free using the global regex interpreter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory via the platform allocator.
 *
 *----------------------------------------------------------------------
 */

void
th8_regex_free(void *p)
{
    /* Bug 26 family: see th8_regex_malloc.  Free is a no-op
     * on NULL interp; matches the standard free(NULL) shape
     * and avoids the TH8_OMIT NULL-deref. */
    if (!th8_regex_interp) return;
    Th8_Free(th8_regex_interp, p);
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_realloc --
 *
 *	Reallocate memory on behalf of the Spencer regex engine.
 *	Routes through Th8_AttemptRealloc using the global regex
 *	interpreter.
 *
 *	On cancellation/limit failure, returns NULL WITHOUT freeing
 *	the original block -- the Spencer engine keeps the original
 *	pointer valid and frees it during cleanup (same ANSI C
 *	realloc contract as libtommath).
 *
 * Results:
 *	Pointer to reallocated memory, or NULL on failure.
 *
 * Side effects:
 *	Reallocates memory via the platform allocator.
 *
 *----------------------------------------------------------------------
 */

void *
th8_regex_realloc(void *p, size_t n)
{
    /* Bug 26 family: see th8_regex_malloc.  ANSI realloc
     * contract: returning NULL leaves the original block
     * valid; caller frees it during cleanup. */
    if (!th8_regex_interp) return NULL;
    if (Th8_Ready(th8_regex_interp) != TH8_OK) {
	th8_regex_oom_flag = 1;
	return NULL;
    }
    return TH8_ATTEMPT_REALLOC(th8_regex_interp, p, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_interrupted --
 *
 *	Check whether the interpreter has been cancelled or has
 *	exceeded resource limits.  Called by the Spencer engine via
 *	the INTERRUPT macro.  If interrupted, sets the OOM flag so
 *	all subsequent MALLOC calls return NULL, forcing the engine
 *	to abort via its out-of-memory error path.
 *
 * Results:
 *	1 if the interpreter is interrupted, 0 otherwise.
 *
 * Side effects:
 *	May set th8_regex_oom_flag.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_interrupted(void)
{
    if (th8_regex_interp) {
	if (Th8_Ready(th8_regex_interp) != TH8_OK) {
	    /*
	     * Set the OOM flag so that all subsequent MALLOC
	     * calls return NULL, forcing the Spencer engine to
	     * abort via its out-of-memory error path.
	     */

	    th8_regex_oom_flag = 1;
	    return 1;
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_stack_too_deep --
 *
 *	Called by the Spencer engine's stack_is_too_deep() inline.
 *	Routes to th8CheckStack.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_stack_too_deep(void)
{
    if (th8_regex_interp) {
	return th8CheckStack(th8_regex_interp) != TH8_OK;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_isalnum --
 *
 *	Character classification bridge for isalnum.  Routes through
 *	TH8's th8IsAlnum for ASCII characters.  Returns 0 for
 *	non-ASCII code points until Unicode tables are integrated.
 *
 * Results:
 *	Non-zero if the character is alphanumeric, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_isalnum(unsigned int c)
{
    return (c < 128) ? th8IsAlnum((int)c) : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_isalpha --
 *
 *	Character classification bridge for isalpha.  Returns 0 for
 *	non-ASCII code points.
 *
 * Results:
 *	Non-zero if the character is alphabetic, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_isalpha(unsigned int c)
{
    return (c < 128) ? th8IsAlpha((int)c) : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_isdigit --
 *
 *	Character classification bridge for isdigit.  Returns 0 for
 *	non-ASCII code points.
 *
 * Results:
 *	Non-zero if the character is a digit, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_isdigit(unsigned int c)
{
    return (c < 128) ? th8IsDigit((int)c) : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_isspace --
 *
 *	Character classification bridge for isspace.  Returns 0 for
 *	non-ASCII code points.
 *
 * Results:
 *	Non-zero if the character is whitespace, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_isspace(unsigned int c)
{
    return (c < 128) ? th8IsSpace((int)c) : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_memcpy --
 *
 *	Bridge memcpy for the Spencer engine.  Routes through
 *	Th8_Memcpy using the global regex interpreter.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Copies n bytes from src to dst.
 *
 *----------------------------------------------------------------------
 */

void *
th8_regex_memcpy(void *dst, const void *src, size_t n)
{
    if (th8_regex_interp) {
	return Th8_Memcpy(th8_regex_interp, dst, src, n);
    }
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_memcmp --
 *
 *	Bridge memcmp for the Spencer engine.  Routes through
 *	Th8_Memcmp using the global regex interpreter.
 *
 * Results:
 *	Negative, zero, or positive integer indicating comparison.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_memcmp(const void *a, const void *b, size_t n)
{
    if (th8_regex_interp) {
	return Th8_Memcmp(th8_regex_interp, a, b, n);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_memset --
 *
 *	Bridge memset for the Spencer engine.  Routes through the
 *	platform's xMemset callback.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Fills n bytes of dst with value c.
 *
 *----------------------------------------------------------------------
 */

void *
th8_regex_memset(void *dst, int c, size_t n)
{
    if (th8_regex_interp) {
	const Th8_Platform *p = Th8_GetPlatform(th8_regex_interp);

	if (p->xMemset) {
	    p->xMemset(th8_regex_interp, p->pCtx, dst, c, n);
	}
    }
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_strlen --
 *
 *	Bridge strlen for the Spencer engine.  Routes through the
 *	platform's xStrlen callback with a hand-rolled fallback.
 *
 * Results:
 *	Length of the NUL-terminated string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
th8_regex_strlen(const char *s)
{
    if (th8_regex_interp) {
	const Th8_Platform *p = Th8_GetPlatform(th8_regex_interp);

	if (p->xStrlen) {
	    return p->xStrlen(th8_regex_interp, p->pCtx, s);
	}
    }
    /* Fallback: count by hand. */
    {
	size_t n = 0;

	while (s[n])
	    n++;
	return n;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_strcmp --
 *
 *	Bridge strcmp for the Spencer engine.  Routes through the
 *	platform's xStrcmp callback with a byte-by-byte fallback.
 *
 * Results:
 *	Negative, zero, or positive integer indicating comparison.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_strcmp(const char *s1, const char *s2)
{
    if (th8_regex_interp) {
	const Th8_Platform *p = Th8_GetPlatform(th8_regex_interp);

	if (p->xStrcmp) {
	    return p->xStrcmp(th8_regex_interp, p->pCtx, s1, s2);
	}
    }
    /* Fallback: byte-by-byte. */
    while (*s1 && *s1 == *s2) {
	s1++;
	s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_strcpy --
 *
 *	Bridge strcpy for the Spencer engine.  Uses th8_regex_strlen
 *	and th8_regex_memcpy internally.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Copies src (including NUL) into dst.
 *
 *----------------------------------------------------------------------
 */

char *
th8_regex_strcpy(char *dst, const char *src)
{
    size_t n = th8_regex_strlen(src);

    th8_regex_memcpy(dst, src, n + 1);
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_strchr --
 *
 *	Bridge strchr for the Spencer engine.  Routes through the
 *	platform's xStrchr callback with a linear-scan fallback.
 *
 * Results:
 *	Pointer to the first occurrence of c in s, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
th8_regex_strchr(const char *s, int c)
{
    if (th8_regex_interp) {
	const Th8_Platform *p = Th8_GetPlatform(th8_regex_interp);

	if (p->xStrchr) {
	    return p->xStrchr(th8_regex_interp, p->pCtx, s, c);
	}
    }
    /* Fallback. */
    while (*s) {
	if (*s == (char)c) return (char *)s;
	s++;
    }
    return (c == 0) ? (char *)s : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_sprintf --
 *
 *	Bridge sprintf for the Spencer engine.  Routes through the
 *	platform's xVsnprintf callback with a 256-byte limit.
 *
 * Results:
 *	Number of characters written, or 0 if no interpreter.
 *
 * Side effects:
 *	Writes formatted output into buf.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_sprintf(char *buf, const char *fmt, ...)
{
    int ret = 0;

    if (th8_regex_interp) {
	const Th8_Platform *p = Th8_GetPlatform(th8_regex_interp);

	if (p->xVsnprintf) {
	    va_list ap;

	    va_start(ap, fmt);
	    ret = p->xVsnprintf(th8_regex_interp, p->pCtx, buf, 256, fmt, ap);
	    va_end(ap);
	    return ret;
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_atoi --
 *
 *	Bridge atoi for the Spencer engine.  Routes through the
 *	platform's xAtoi callback with a hand-rolled fallback.
 *
 * Results:
 *	Integer value of the string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_regex_atoi(const char *s)
{
    if (th8_regex_interp) {
	const Th8_Platform *p = Th8_GetPlatform(th8_regex_interp);

	if (p->xAtoi) {
	    return p->xAtoi(th8_regex_interp, p->pCtx, s);
	}
    }
    /* Fallback: hand-rolled atoi. */
    {
	int n = 0;
	int neg = 0;

	while (*s == ' ' || *s == '\t')
	    s++;
	if (*s == '-') {
	    neg = 1;
	    s++;
	} else if (*s == '+') {
	    s++;
	}
	while (*s >= '0' && *s <= '9') {
	    n = n * 10 + (*s - '0');
	    s++;
	}
	return neg ? -n : n;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8_regex_qsort --
 *
 *	Bridge qsort for the Spencer engine.  Routes through the
 *	platform's xQsort callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sorts the array in place.
 *
 *----------------------------------------------------------------------
 */

void
th8_regex_qsort(
    void *base,
    size_t nmemb,
    size_t size,
    int (*cmp)(const void *, const void *))
{
    if (th8_regex_interp) {
	const Th8_Platform *p = Th8_GetPlatform(th8_regex_interp);

	if (p->xQsort) {
	    p->xQsort(th8_regex_interp, p->pCtx, base, nmemb, size, cmp);
	    return;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * UTF-8 to chr[] conversion.
 *
 *	The Spencer engine operates on fixed-width chr arrays.
 *	These helpers convert between TH8's UTF-8 strings and
 *	the engine's chr[] arrays.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8Utf8ToChr --
 *
 *	Convert a UTF-8 string to a chr (UTF-32) array using
 *	ConvertUTF_v2 for strict UTF-8 validation.  Caller must
 *	free the result with Th8_Free.
 *
 *----------------------------------------------------------------------
 */

static unsigned int *
th8Utf8ToChr(Th8_Interp *interp, const char *z, size_t n, int *pnChr)
{
    const UTF8 *pSrc;
    UTF32 *aChr;
    UTF32 *pDst;
    ConversionResult result;

    /*
     * Allocate worst-case: one UTF-32 code point per byte,
     * plus NUL terminator.
     */

    aChr = (UTF32 *)
        TH8_ALLOC_MUL_ADD(interp, sizeof(UTF32), n, sizeof(UTF32));
    if (!aChr) {
	*pnChr = 0;
	return 0;
    }

    pSrc = (const UTF8 *)z;
    pDst = aChr;
    result = ConvertUTF8toUTF32(
        &pSrc, (const UTF8 *)z + n, &pDst, aChr + n, strictConversion);

    if (result != conversionOK) {
	/*
	 * Malformed UTF-8.  Fall back to replacing bad bytes
	 * with U+FFFD (replacement character).
	 */

	pSrc = (const UTF8 *)z;
	pDst = aChr;
	(void)ConvertUTF8toUTF32(
	    &pSrc, (const UTF8 *)z + n, &pDst, aChr + n, lenientConversion);
    }

    *pDst = 0;
    *pnChr = (int)(pDst - aChr);
    return (unsigned int *)aChr;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChrToUtf8 --
 *
 *	Convert a chr (UTF-32) array back to UTF-8 using
 *	ConvertUTF_v2.  Caller must free with Th8_Free.
 *
 *----------------------------------------------------------------------
 */

static char *
th8ChrToUtf8(
    Th8_Interp *interp,
    const unsigned int *aChr,
    int nChr,
    size_t *pnUtf8)
{
    const UTF32 *pSrc;
    UTF8 *zOut;
    UTF8 *pDst;

    /*
     * Worst case: 4 bytes per code point, plus NUL.
     * Guard against negative or zero nChr.
     */

    if (nChr <= 0) {
	zOut = (UTF8 *)TH8_ALLOC(interp, 1);
	if (!zOut) {
	    *pnUtf8 = 0;
	    return NULL;
	}
	zOut[0] = 0;
	*pnUtf8 = 0;
	return (char *)zOut;
    }

    zOut = (UTF8 *)TH8_ALLOC_MUL_ADD(interp, (size_t)nChr, 4, 1);
    if (!zOut) {
	*pnUtf8 = 0;
	return NULL;
    }

    pSrc = (const UTF32 *)aChr;
    pDst = zOut;
    (void)ConvertUTF32toUTF8(
        &pSrc, (const UTF32 *)aChr + nChr, &pDst, zOut + (size_t)nChr * 4,
        strictConversion);

    *pDst = 0;
    *pnUtf8 = (size_t)(pDst - zOut);
    return (char *)zOut;
}


/*
 *----------------------------------------------------------------------
 *
 * th8RegexSetup / th8RegexTeardown --
 *
 *	Set up and tear down the global regex bridge state before
 *	and after each regex operation.
 *
 *----------------------------------------------------------------------
 */

static void
th8RegexSetup(Th8_Interp *interp)
{
    th8MaybeGlobalMutexEnter(interp);
    th8_regex_interp = interp;
    th8_regex_oom_flag = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8RegexTeardown --
 *
 *	Inverse of `th8RegexSetup`: clear the thread-local
 *	regex-engine interpreter pointer and OOM flag, then
 *	release the global mutex.  Every regex operation
 *	must reach this helper on its way out (success or
 *	error) so the mutex never deadlocks subsequent
 *	callers.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Releases the global regex mutex; zeros the per-thread
 *	`th8_regex_interp` and `th8_regex_oom_flag`.
 *
 *----------------------------------------------------------------------
 */
static void
th8RegexTeardown(void)
{
    th8_regex_interp = 0;
    th8_regex_oom_flag = 0;
    th8MaybeGlobalMutexLeave(NULL);
}


/*
 * Maximum number of NFA states in a compiled regex pattern.
 * Patterns that exceed this are rejected as too complex,
 * preventing regex-bomb attacks.  The default (10000) is
 * generous enough for any practical pattern.
 */

#  ifndef TH8_REGEX_MAX_STATES
#    define TH8_REGEX_MAX_STATES 10000
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8RegexCheckComplexity --
 *
 *	After a successful regcomp, verify that the compiled pattern
 *	does not exceed the NFA state limit.  Returns TH8_OK if the
 *	pattern is acceptable, TH8_ERROR if it is too complex.
 *
 *----------------------------------------------------------------------
 */

static int
th8RegexCheckComplexity(Th8_Interp *interp, const regex_t *pRe)
{
    int nStates = 0;

    th8_regex_analyze(pRe, &nStates, 0, 0, 0, 0, 0);
    if (nStates > TH8_REGEX_MAX_STATES) {
	Th8_SetResultStatic(
	    interp, "regular expression too complex", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * regexp_command --
 *
 *	Match a string against a regular expression.
 *
 *	regexp ?SWITCHES? PATTERN STRING ?MATCHVAR? ?SUBVAR ...?
 *
 *	Stub: reports "not yet wired" until the command-level
 *	integration with th8_regcomp/th8_regexec is complete.
 *
 *----------------------------------------------------------------------
 */

static int
regexp_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iArg = 1;
    int cflags = REG_ADVANCED;
    int eflags = 0;
    int bAll = 0;
    int bInline = 0;
    int bIndices = 0;
    size_t startOffset = 0;
    th8_regex_t re;
    unsigned int *aChrPat = 0;
    int nChrPat;
    unsigned int *aChrStr = 0;
    int nChrStr;
    th8_regmatch_t *pmatch = 0;
    int rc;
    int matched;
    int errCode;

    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "regexp ?switches? pattern string"
	            " ?matchvar? ?subvar ...?");
    }

    /*
     * Parse switches.
     */

    while (iArg < argc) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-nocase")) {
	    cflags |= REG_ICASE;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-expanded")) {
	    cflags |= REG_EXPANDED;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-line")) {
	    cflags |= REG_NEWLINE;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-linestop")) {
	    cflags |= REG_NLSTOP;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-lineanchor")) {
	    cflags |= REG_NLANCH;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-all")) {
	    bAll = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-inline")) {
	    bInline = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-indices")) {
	    bIndices = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-start")) {
	    int iStart;

	    iArg++;
	    if (iArg >= argc) {
		return Th8_WrongNumArgs(interp, "regexp -start index ...");
	    }
	    if (Th8_ToInt(interp, argv[iArg], argl[iArg], &iStart) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	    startOffset = (size_t)(iStart > 0 ? iStart : 0);
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-about")) {
	    /*
	     * -about: compile the pattern and return
	     * complexity metadata instead of matching.
	     */

	    regex_t reAbout;
	    int errAbout;
	    unsigned int *aChrAbout;
	    int nChrAbout;
	    int nStates, nSubre, nLacons;
	    int bMatchAll, bBackref, bLookAr;
	    char *zRes = 0;
	    size_t nRes = 0;

	    iArg++;
	    if (iArg >= argc) {
		return Th8_WrongNumArgs(interp, "regexp -about pattern");
	    }
	    th8RegexSetup(interp);
	    aChrAbout =
	        th8Utf8ToChr(interp, argv[iArg], argl[iArg], &nChrAbout);
	    if (!aChrAbout) {
		th8RegexTeardown();
		return TH8_ERROR;
	    }
	    errAbout = th8_regcomp(
	        &reAbout, aChrAbout, (size_t)nChrAbout, cflags, 0);
	    Th8_Free(interp, aChrAbout);
	    if (errAbout != REG_OKAY) {
		char zErr[200];

		th8_regerror(errAbout, &reAbout, zErr, sizeof(zErr));
		th8RegexTeardown();
		Th8_SetResult(interp, zErr, TH8_NOLEN);
		return TH8_ERROR;
	    }
	    th8_regex_analyze(
	        &reAbout, &nStates, &nSubre, &nLacons, &bMatchAll, &bBackref,
	        &bLookAr);

	    Th8_ListAppend(interp, &zRes, &nRes, "nsub", TH8_NOLEN);
	    Th8_SetResultInt(interp, (int)reAbout.re_nsub);
	    Th8_ListAppend(
	        interp, &zRes, &nRes, Th8_GetResult(interp, 0), TH8_NOLEN);
	    Th8_ListAppend(interp, &zRes, &nRes, "nstates", TH8_NOLEN);
	    Th8_SetResultInt(interp, nStates);
	    Th8_ListAppend(
	        interp, &zRes, &nRes, Th8_GetResult(interp, 0), TH8_NOLEN);
	    Th8_ListAppend(interp, &zRes, &nRes, "nsubre", TH8_NOLEN);
	    Th8_SetResultInt(interp, nSubre);
	    Th8_ListAppend(
	        interp, &zRes, &nRes, Th8_GetResult(interp, 0), TH8_NOLEN);
	    Th8_ListAppend(interp, &zRes, &nRes, "nlacons", TH8_NOLEN);
	    Th8_SetResultInt(interp, nLacons);
	    Th8_ListAppend(
	        interp, &zRes, &nRes, Th8_GetResult(interp, 0), TH8_NOLEN);
	    Th8_ListAppend(interp, &zRes, &nRes, "matchall", TH8_NOLEN);
	    Th8_ListAppend(interp, &zRes, &nRes, bMatchAll ? "1" : "0", 1);
	    Th8_ListAppend(interp, &zRes, &nRes, "backref", TH8_NOLEN);
	    Th8_ListAppend(interp, &zRes, &nRes, bBackref ? "1" : "0", 1);
	    Th8_ListAppend(interp, &zRes, &nRes, "lookaround", TH8_NOLEN);
	    Th8_ListAppend(interp, &zRes, &nRes, bLookAr ? "1" : "0", 1);

	    th8_regfree(&reAbout);
	    th8RegexTeardown();
	    Th8_SetResult(interp, zRes, nRes);
	    Th8_Free(interp, zRes);
	    return TH8_OK;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "--")) {
	    iArg++;
	    break;
	} else if (TH8_LEN(argl[iArg]) > 0 && argv[iArg][0] == '-') {
	    Th8_ErrorMessage(interp, "bad switch \"", argv[iArg], argl[iArg]);
	    return TH8_ERROR;
	} else {
	    break;
	}
    }

    if (iArg + 2 > argc) {
	return Th8_WrongNumArgs(
	    interp, "regexp ?switches? pattern string"
	            " ?matchvar? ?subvar ...?");
    }

#  if !defined(TH8_ENABLE_VARIABLES)
    if (iArg + 2 != argc) {
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    th8RegexSetup(interp);

    /*
     * Convert pattern and string to chr arrays.
     */

    aChrPat = th8Utf8ToChr(interp, argv[iArg], TH8_LEN(argl[iArg]), &nChrPat);
    iArg++;
    aChrStr = th8Utf8ToChr(interp, argv[iArg], TH8_LEN(argl[iArg]), &nChrStr);
    iArg++;

    if (!aChrPat || !aChrStr) {
	Th8_Free(interp, aChrPat);
	Th8_Free(interp, aChrStr);
	th8RegexTeardown();
	Th8_SetResultStatic(interp, "out of memory in regexp", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Compile the pattern.
     */

    errCode = th8_regcomp(&re, aChrPat, (size_t)nChrPat, cflags, 0);
    if (errCode != REG_OKAY) {
	char zErr[256];

	th8_regerror(errCode, &re, zErr, sizeof(zErr));
	Th8_SetResult(interp, zErr, TH8_NOLEN);
	Th8_Free(interp, aChrPat);
	Th8_Free(interp, aChrStr);
	th8RegexTeardown();
	return TH8_ERROR;
    }

    /*
     * Security: reject excessively complex patterns.
     */

    if (th8RegexCheckComplexity(interp, &re) != TH8_OK) {
	th8_regfree(&re);
	Th8_Free(interp, aChrPat);
	Th8_Free(interp, aChrStr);
	th8RegexTeardown();
	return TH8_ERROR;
    }

    /*
     * Allocate match results.
     */

    {
	size_t nMatch = re.re_nsub + 1;
	int nTotal = 0;  /* Total match count for -all. */
	size_t searchPos = startOffset;
	char *zInlineList = 0; /* Accumulator for -inline. */
	size_t nInlineList = 0;

	pmatch = (th8_regmatch_t *)
	    TH8_ALLOC_MUL(interp, sizeof(th8_regmatch_t), nMatch);
	if (!pmatch) {
	    th8_regfree(&re);
	    Th8_Free(interp, aChrPat);
	    Th8_Free(interp, aChrStr);
	    th8RegexTeardown();
	    return TH8_ERROR;
	}

	/*
	 * Match loop (runs once unless -all is set).
	 */

	do {
	    errCode = th8_regexec(
	        &re, aChrStr, (size_t)nChrStr, searchPos, 0, nMatch, pmatch,
	        eflags);
	    matched = (errCode == REG_OKAY);
	    if (!matched) break;
	    nTotal++;

	    /*
	     * Extract this match's results.
	     */

	    if (bInline) {
		/*
		 * Append match (and submatches) to the
		 * inline result list.
		 */

		size_t k;

		for (k = 0; k < nMatch; k++) {
		    if (pmatch[k].rm_so >= 0) {
			if (bIndices) {
			    size_t nBuf = 0;
			    const char *zBuf;
			    char *zIdx = 0;
			    size_t nIdx = 0;

			    Th8_SetResultInt(interp, (int)pmatch[k].rm_so);
			    zBuf = Th8_GetResult(interp, &nBuf);
			    Th8_StringAppend(
			        interp, &zIdx, &nIdx, zBuf, nBuf);
			    Th8_StringAppend(interp, &zIdx, &nIdx, " ", 1);
			    Th8_SetResultInt(
			        interp, (int)pmatch[k].rm_eo - 1);
			    zBuf = Th8_GetResult(interp, &nBuf);
			    Th8_StringAppend(
			        interp, &zIdx, &nIdx, zBuf, nBuf);
			    Th8_ListAppend(
			        interp, &zInlineList, &nInlineList, zIdx,
			        nIdx);
			    Th8_Free(interp, zIdx);
			} else {
			    size_t nSub;
			    int nSubChr;
			    char *zSub;

			    nSubChr = (int)(pmatch[k].rm_eo -
			                    pmatch[k].rm_so);
			    zSub = th8ChrToUtf8(
			        interp, &aChrStr[pmatch[k].rm_so], nSubChr,
			        &nSub);
			    Th8_ListAppend(
			        interp, &zInlineList, &nInlineList, zSub,
			        nSub);
			    Th8_Free(interp, zSub);
			}
		    } else {
			Th8_ListAppend(
			    interp, &zInlineList, &nInlineList, "", 0);
		    }
		}
	    } else if (nTotal == 1) {
		/*
		 * Variable mode: store matches in variables
		 * (only on first match for -all).
		 */

		int k;
		int nVars = argc - iArg;

		for (k = 0; k < nVars; k++) {
		    if (k <= (int)re.re_nsub && pmatch[k].rm_so >= 0) {
			if (bIndices) {
			    size_t nBuf = 0;
			    const char *zBuf;
			    char *zIdx = 0;
			    size_t nIdx = 0;

			    Th8_SetResultInt(interp, (int)pmatch[k].rm_so);
			    zBuf = Th8_GetResult(interp, &nBuf);
			    Th8_StringAppend(
			        interp, &zIdx, &nIdx, zBuf, nBuf);
			    Th8_StringAppend(interp, &zIdx, &nIdx, " ", 1);
			    Th8_SetResultInt(
			        interp, (int)pmatch[k].rm_eo - 1);
			    zBuf = Th8_GetResult(interp, &nBuf);
			    Th8_StringAppend(
			        interp, &zIdx, &nIdx, zBuf, nBuf);
#  if defined(TH8_ENABLE_VARIABLES)
			    Th8_SetVar(
			        interp, argv[iArg + k],
			        TH8_LEN(argl[iArg + k]), zIdx, nIdx);
#  endif
			    Th8_Free(interp, zIdx);
			} else {
			    size_t nSub;
			    int nSubChr;
			    char *zSub;

			    nSubChr = (int)(pmatch[k].rm_eo -
			                    pmatch[k].rm_so);
			    zSub = th8ChrToUtf8(
			        interp, &aChrStr[pmatch[k].rm_so], nSubChr,
			        &nSub);
#  if defined(TH8_ENABLE_VARIABLES)
			    Th8_SetVar(
			        interp, argv[iArg + k],
			        TH8_LEN(argl[iArg + k]), zSub, nSub);
#  endif
			    Th8_Free(interp, zSub);
			}
		    } else {
#  if defined(TH8_ENABLE_VARIABLES)
			Th8_SetVar(
			    interp, argv[iArg + k], TH8_LEN(argl[iArg + k]),
			    "", 0);
#  endif
		    }
		}
	    }

	    /*
	     * Advance search position past this match.
	     * Prevent infinite loop on zero-length match.
	     */

	    if (pmatch[0].rm_eo > pmatch[0].rm_so) {
		searchPos = (size_t)pmatch[0].rm_eo;
	    } else {
		searchPos = (size_t)pmatch[0].rm_so + 1;
	    }

	    /*
	     * Step counter check for -all iterations.
	     */

	    if (bAll && Th8_Ready(interp) != TH8_OK) {
		matched = 0;
		break;
	    }

	} while (bAll && searchPos < (size_t)nChrStr);

	/*
	 * Set result.
	 */

	if (bInline) {
	    if (nTotal > 0) {
		Th8_SetResult(interp, zInlineList, nInlineList);
	    } else {
		Th8_ClearResult(interp);
	    }
	    Th8_Free(interp, zInlineList);
	} else if (bAll) {
	    Th8_SetResultInt(interp, nTotal);
	} else {
	    Th8_SetResultInt(interp, matched ? 1 : 0);
	}
    }

    /*
     * Cleanup.
     */

    rc = TH8_OK;
    th8_regfree(&re);
    Th8_Free(interp, pmatch);
    Th8_Free(interp, aChrPat);
    Th8_Free(interp, aChrStr);
    th8RegexTeardown();

    /*
     * Check if the engine was interrupted (OOM flag).  If so,
     * the interpreter result already has the real error.
     */

    if (th8_regex_oom_flag) {
	rc = TH8_ERROR;
    }

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * regsub_command --
 *
 *	Perform regular expression substitution.
 *
 *	regsub ?SWITCHES? PATTERN STRING REPLACEMENT ?VARNAME?
 *
 *----------------------------------------------------------------------
 */

static int
regsub_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iArg = 1;
    int cflags = REG_ADVANCED;
    int bAll = 0;
    th8_regex_t re;
    unsigned int *aChrPat = 0;
    int nChrPat;
    unsigned int *aChrStr = 0;
    int nChrStr;
    th8_regmatch_t *pmatch = 0;
    size_t nMatch;
    const char *zRepl;
    size_t nRepl;
    char *zOut = 0;
    size_t nOut = 0;
    int nSubs = 0;
    int errCode;
    int rc = TH8_OK;
    size_t searchStart = 0;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "regsub ?switches? pattern string"
	            " replacement ?varname?");
    }

    /*
     * Parse switches.
     */

    while (iArg < argc) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-nocase")) {
	    cflags |= REG_ICASE;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-all")) {
	    bAll = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-line")) {
	    cflags |= REG_NEWLINE;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "--")) {
	    iArg++;
	    break;
	} else if (TH8_LEN(argl[iArg]) > 0 && argv[iArg][0] == '-') {
	    Th8_ErrorMessage(interp, "bad switch \"", argv[iArg], argl[iArg]);
	    return TH8_ERROR;
	} else {
	    break;
	}
    }

    if (iArg + 3 > argc && iArg + 4 > argc) {
	return Th8_WrongNumArgs(
	    interp, "regsub ?switches? pattern string"
	            " replacement ?varname?");
    }

#  if !defined(TH8_ENABLE_VARIABLES)
    if (iArg + 3 != argc) {
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    th8RegexSetup(interp);

    /*
     * Convert pattern and string.
     */

    aChrPat = th8Utf8ToChr(interp, argv[iArg], TH8_LEN(argl[iArg]), &nChrPat);
    iArg++;
    aChrStr = th8Utf8ToChr(interp, argv[iArg], TH8_LEN(argl[iArg]), &nChrStr);
    iArg++;
    zRepl = argv[iArg];
    nRepl = TH8_LEN(argl[iArg]);
    iArg++;

    if (!aChrPat || !aChrStr) {
	Th8_Free(interp, aChrPat);
	Th8_Free(interp, aChrStr);
	th8RegexTeardown();
	Th8_SetResultStatic(interp, "out of memory in regsub", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Compile.
     */

    errCode = th8_regcomp(&re, aChrPat, (size_t)nChrPat, cflags, 0);
    if (errCode != REG_OKAY) {
	char zErr[256];

	th8_regerror(errCode, &re, zErr, sizeof(zErr));
	Th8_SetResult(interp, zErr, TH8_NOLEN);
	Th8_Free(interp, aChrPat);
	Th8_Free(interp, aChrStr);
	th8RegexTeardown();
	return TH8_ERROR;
    }

    /*
     * Security: reject excessively complex patterns.
     */

    if (th8RegexCheckComplexity(interp, &re) != TH8_OK) {
	th8_regfree(&re);
	Th8_Free(interp, aChrPat);
	Th8_Free(interp, aChrStr);
	th8RegexTeardown();
	return TH8_ERROR;
    }

    nMatch = re.re_nsub + 1;
    pmatch = (th8_regmatch_t *)
        TH8_ALLOC_MUL(interp, sizeof(th8_regmatch_t), nMatch);
    if (!pmatch) {
	th8_regfree(&re);
	Th8_Free(interp, aChrPat);
	Th8_Free(interp, aChrStr);
	th8RegexTeardown();
	return TH8_ERROR;
    }

    /*
     * Match and substitute loop.
     */

    do {
	errCode = th8_regexec(
	    &re, aChrStr, (size_t)nChrStr, searchStart, 0, nMatch, pmatch, 0);
	if (errCode != REG_OKAY) break;

	/*
	 * Copy text before the match.
	 */

	if (pmatch[0].rm_so > (th8_regoff_t)searchStart) {
	    size_t nPre;
	    char *zPre;

	    zPre = th8ChrToUtf8(
	        interp, &aChrStr[searchStart],
	        (int)(pmatch[0].rm_so - (th8_regoff_t)searchStart), &nPre);
	    Th8_StringAppend(interp, &zOut, &nOut, zPre, nPre);
	    Th8_Free(interp, zPre);
	}

	/*
	 * Process replacement string.
	 * & = whole match, \1-\9 = subexpressions, \\ = literal.
	 */

	{
	    size_t r;

	    for (r = 0; r < nRepl; r++) {
		if (zRepl[r] == '\\' && r + 1 < nRepl) {
		    r++;
		    if (zRepl[r] >= '1' && zRepl[r] <= '9') {
			int idx = zRepl[r] - '0';

			if (idx < (int)nMatch && pmatch[idx].rm_so >= 0) {
			    size_t nSub;
			    char *zSub;
			    int nSubChr;

			    nSubChr = (int)(pmatch[idx].rm_eo -
			                    pmatch[idx].rm_so);
			    zSub = th8ChrToUtf8(
			        interp, &aChrStr[pmatch[idx].rm_so], nSubChr,
			        &nSub);
			    Th8_StringAppend(
			        interp, &zOut, &nOut, zSub, nSub);
			    Th8_Free(interp, zSub);
			}
		    } else {
			Th8_StringAppend(interp, &zOut, &nOut, &zRepl[r], 1);
		    }
		} else if (zRepl[r] == '&') {
		    size_t nSub;
		    char *zSub;
		    int nSubChr;

		    nSubChr = (int)(pmatch[0].rm_eo - pmatch[0].rm_so);
		    zSub = th8ChrToUtf8(
		        interp, &aChrStr[pmatch[0].rm_so], nSubChr, &nSub);
		    Th8_StringAppend(interp, &zOut, &nOut, zSub, nSub);
		    Th8_Free(interp, zSub);
		} else {
		    Th8_StringAppend(interp, &zOut, &nOut, &zRepl[r], 1);
		}
	    }
	}

	nSubs++;
	searchStart = (size_t)pmatch[0].rm_eo;

	/*
	 * Prevent infinite loop on zero-length match.
	 */

	if (pmatch[0].rm_so == pmatch[0].rm_eo) {
	    if (searchStart < (size_t)nChrStr) {
		size_t nOne;
		char *zOne;

		zOne = th8ChrToUtf8(interp, &aChrStr[searchStart], 1, &nOne);
		Th8_StringAppend(interp, &zOut, &nOut, zOne, nOne);
		Th8_Free(interp, zOne);
		searchStart++;
	    } else {
		break;
	    }
	}
    } while (bAll && searchStart < (size_t)nChrStr);

    /*
     * Copy remaining text after last match.
     */

    if (searchStart < (size_t)nChrStr) {
	size_t nTail;
	char *zTail;

	zTail = th8ChrToUtf8(
	    interp, &aChrStr[searchStart], nChrStr - (int)searchStart,
	    &nTail);
	Th8_StringAppend(interp, &zOut, &nOut, zTail, nTail);
	Th8_Free(interp, zTail);
    } else if (nSubs == 0) {
	/*
	 * No match: output original string unchanged.
	 */

	Th8_StringAppend(
	    interp, &zOut, &nOut, argv[iArg - 2], argl[iArg - 2]);
    }

    /*
     * Store result.
     */

    if (iArg < argc) {
	/* varname mode: store in variable, return count */
#  if defined(TH8_ENABLE_VARIABLES)
	Th8_SetVar(interp, argv[iArg], TH8_LEN(argl[iArg]), zOut, nOut);
#  endif
	Th8_SetResultInt(interp, nSubs);
    } else {
	/* no varname: return substituted string */
	Th8_SetResult(interp, zOut, nOut);
    }

    /*
     * Cleanup.
     */

    Th8_Free(interp, zOut);
    th8_regfree(&re);
    Th8_Free(interp, pmatch);
    Th8_Free(interp, aChrPat);
    Th8_Free(interp, aChrStr);
    th8RegexTeardown();

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8RegexpGetCommands --
 *
 *	Plugin GetCommands callback for the regular_expressions
 *	plugin.  Returns the static command table for [regexp]
 *	and [regsub].
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8RegexpCommands[] = {
    {1, 0, "regexp", regexp_command},
    {1, 0, "regsub", regsub_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8RegexpGetCommands --
 *
 *	Plugin-registration entry point for the regex
 *	plugin: report (or copy out) the `th8RegexpCommands`
 *	table covering `[regexp]` and `[regsub]`.  Standard
 *	`Th8_CommandEntry` reporter contract: NULL `pCommand`
 *	for count-only; non-NULL `pCommand` of at least
 *	`*pnCommand` entries to copy.  Insufficient buffer
 *	returns `TH8_ERROR` without touching `pCommand`.
 *
 *	NULL `pnCommand` is always an error.
 *
 * Parameters:
 *	pCommand  -- caller-supplied output buffer or NULL.
 *	pnCommand -- in/out count.
 *
 * Returns:
 *	`TH8_OK` on success; `TH8_ERROR` on missing
 *	`pnCommand` or insufficient buffer.
 *
 * Side effects:
 *	May overwrite `*pnCommand` and `pCommand[0..n-1]`.
 *
 *----------------------------------------------------------------------
 */
int
th8RegexpGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8RegexpCommands) / sizeof(th8RegexpCommands[0]));

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
	    pCommand[i] = th8RegexpCommands[i];
	}
    }
    return TH8_OK;
}


#endif /* TH8_ENABLE_REGEXP */
