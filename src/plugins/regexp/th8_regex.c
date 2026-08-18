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
 * Why / How:
 *	The engine reaches this via its MALLOC macro (regcustom_th8.h)
 *	and has no interp of its own, so the allocation is charged to
 *	the thread-local th8_regex_interp published by th8RegexSetup.
 *	Short-circuits when th8_regex_oom_flag is set (a prior
 *	cancel/limit was seen) so the engine unwinds through its
 *	out-of-memory path, and when Th8_Ready reports a
 *	cancel/limit it raises that flag first.  The NULL-interp
 *	guard is the Bug 26 family fix: a NEVER-style assert would
 *	collapse under TH8_OMIT_AUXILIARY_SAFETY_CHECKS and deref a
 *	NULL interp when the engine is invoked outside a setup window.
 *
 * Results:
 *	Pointer to allocated memory, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory via the platform allocator.  May set
 *	th8_regex_oom_flag when the interpreter is not ready.
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
 * Why / How:
 *	The engine reaches this via its FREE macro and has no interp
 *	of its own, so the block is returned to the thread-local
 *	th8_regex_interp that allocated it.  When no interp is set
 *	(the Bug 26 window: engine invoked outside a setup/teardown
 *	pair) it is a no-op, mirroring the standard free(NULL) shape
 *	and avoiding the TH8_OMIT NULL-deref.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory via the platform allocator (no-op when no
 *	interpreter is set).
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
 * Why / How:
 *	The engine reaches this via its REALLOC macro against the
 *	thread-local th8_regex_interp.  The NULL-interp guard is the
 *	Bug 26 family fix (see th8_regex_malloc).  A cancel/limit
 *	seen via Th8_Ready raises th8_regex_oom_flag and returns
 *	NULL; because ANSI realloc leaves the original block valid
 *	on failure, the engine's own cleanup frees it -- so this
 *	must not free p itself.
 *
 * Results:
 *	Pointer to reallocated memory, or NULL on failure (original
 *	block left intact).
 *
 * Side effects:
 *	Reallocates memory via the platform allocator.  May set
 *	th8_regex_oom_flag when the interpreter is not ready.
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
 * Why / How:
 *	The Spencer engine has no direct notion of TH8 cancellation
 *	or resource limits, so this bridge polls Th8_Ready on the
 *	thread-local th8_regex_interp from inside the engine's inner
 *	loops.  Rather than aborting mid-loop, it converts a not-ready
 *	condition into the engine's existing out-of-memory unwind by
 *	raising th8_regex_oom_flag, which makes every later allocation
 *	fail cleanly.  A no-op when no interpreter is set.
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
 * Why / How:
 *	The engine's recursive matcher guards against C-stack
 *	exhaustion by asking the host whether the stack is nearly
 *	full; TH8 answers using th8CheckStack on the thread-local
 *	th8_regex_interp, which applies TH8's configured stack limit.
 *	A no-op returning 0 (not too deep) when no interpreter is
 *	set, so the engine keeps its own default behavior.
 *
 * Results:
 *	Non-zero if the stack is too deep to recurse further, 0
 *	otherwise (including when no interpreter is set).
 *
 * Side effects:
 *	None.
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
 * Why / How:
 *	The engine's bracket-expression and locale code classify
 *	code points through this bridge; TH8 answers from its own
 *	th8IsAlnum table so behavior does not depend on the C
 *	library's locale.  Only the ASCII range (c < 128) is defined
 *	so far, so higher code points deliberately return 0 rather
 *	than a locale-dependent guess.
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
 * Why / How:
 *	Same design as th8_regex_isalnum: the engine classifies code
 *	points through TH8's own th8IsAlpha table (not the C library
 *	locale), and only the ASCII range (c < 128) is defined, so
 *	higher code points return 0.
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
 * Why / How:
 *	Same design as th8_regex_isalnum: the engine classifies code
 *	points through TH8's own th8IsDigit table (not the C library
 *	locale), and only the ASCII range (c < 128) is defined, so
 *	higher code points return 0.
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
 * Why / How:
 *	Same design as th8_regex_isalnum: the engine classifies code
 *	points through TH8's own th8IsSpace table (not the C library
 *	locale), and only the ASCII range (c < 128) is defined, so
 *	higher code points return 0.
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
 * Why / How:
 *	TH8's layer discipline forbids the engine from calling the C
 *	library directly, so regcustom_th8.h redirects its memcpy to
 *	this bridge, which dispatches to the platform's memcpy
 *	callback via the thread-local th8_regex_interp.  With no
 *	interpreter set there is nothing to dispatch through, so it
 *	returns dst untouched.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Copies n bytes from src to dst (no-op when no interpreter is
 *	set).
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_memcpy: the
 *	engine's memcmp is redirected here and dispatched to the
 *	platform callback via the thread-local th8_regex_interp.
 *	With no interpreter set it returns 0 (compare-equal) since
 *	there is nothing to dispatch through.
 *
 * Results:
 *	Negative, zero, or positive integer indicating comparison
 *	(0 when no interpreter is set).
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_memcpy: the
 *	engine's memset is redirected here.  It calls the platform's
 *	xMemset callback directly (via Th8_GetPlatform on the
 *	thread-local th8_regex_interp) only when both the interp and
 *	the callback are present; otherwise it leaves dst unchanged.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Fills n bytes of dst with value c (no-op when no interpreter
 *	or no xMemset callback is available).
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_memcpy: the
 *	engine's strlen is redirected here and dispatched to the
 *	platform's xStrlen callback.  Because strlen is a pure query
 *	that can be needed even outside a setup window (or on a
 *	platform without the callback), it also carries a
 *	self-contained byte-counting fallback so it always returns a
 *	correct length.
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_strlen: the
 *	engine's strcmp is redirected here and dispatched to the
 *	platform's xStrcmp callback, with a self-contained
 *	byte-by-byte fallback so a missing interp or callback still
 *	yields a correct ordering.
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_memcpy: the
 *	engine's strcpy is redirected here.  Rather than call the C
 *	library it is composed from the other bridges -- it measures
 *	src with th8_regex_strlen and copies length + 1 bytes (to
 *	include the NUL) with th8_regex_memcpy -- so it inherits their
 *	platform routing automatically.
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_strlen: the
 *	engine's strchr is redirected here and dispatched to the
 *	platform's xStrchr callback, with a self-contained linear
 *	scan fallback that (like C strchr) also matches a search for
 *	the terminating NUL.
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_memcpy: the
 *	engine's sprintf is redirected here.  The engine only uses it
 *	to format short error/diagnostic strings, so this collects
 *	the varargs and dispatches to the platform's bounded
 *	xVsnprintf with a fixed 256-byte cap -- turning an unbounded
 *	sprintf into a safe snprintf.  With no interp or callback it
 *	writes nothing.
 *
 * Results:
 *	Number of characters written, or 0 when no interpreter or no
 *	xVsnprintf callback is available.
 *
 * Side effects:
 *	Writes formatted output into buf (up to 256 bytes).
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_strlen: the
 *	engine's atoi is redirected here and dispatched to the
 *	platform's xAtoi callback, with a self-contained fallback
 *	that skips leading blanks, honors an optional +/- sign, and
 *	accumulates decimal digits so a missing interp or callback
 *	still parses correctly.
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
 * Why / How:
 *	Same layer-discipline rationale as th8_regex_memcpy: the
 *	engine's qsort is redirected here and dispatched to the
 *	platform's xQsort callback via the thread-local
 *	th8_regex_interp.  There is no in-tree fallback sort, so with
 *	no interp or callback the array is simply left unsorted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sorts the array in place (no-op when no interpreter or no
 *	xQsort callback is available).
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
 * Why / How:
 *	The Spencer engine matches over fixed-width 32-bit code
 *	points, so TH8's UTF-8 command arguments must be widened at
 *	the API boundary.  A worst-case buffer of one UTF-32 unit per
 *	input byte (plus a NUL) is allocated, then ConvertUTF8toUTF32
 *	runs first in strictConversion mode; if the input is
 *	malformed it re-runs in lenientConversion mode so bad bytes
 *	become U+FFFD instead of failing the command.  The decoded
 *	length is reported through *pnChr.
 *
 * Results:
 *	Pointer to a newly allocated, NUL-terminated UTF-32 array
 *	(caller frees with Th8_Free), with *pnChr set to the code
 *	point count; NULL on allocation failure, with *pnChr set to 0.
 *
 * Side effects:
 *	Allocates memory on the interpreter's allocator.
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
 * Why / How:
 *	The inverse of th8Utf8ToChr: match results and extracted
 *	substrings leave the engine as UTF-32 and must be narrowed
 *	back to UTF-8 before being returned as TH8 results.  A
 *	non-positive nChr is handled up front by returning a fresh
 *	empty string, guarding both the multiply and the converter
 *	against a zero/negative count; otherwise a worst-case buffer
 *	of four bytes per code point (plus a NUL) is allocated and
 *	ConvertUTF32toUTF8 fills it in strictConversion mode.  The
 *	byte length is reported through *pnUtf8.
 *
 * Results:
 *	Pointer to a newly allocated, NUL-terminated UTF-8 string
 *	(caller frees with Th8_Free), with *pnUtf8 set to its byte
 *	length; NULL on allocation failure, with *pnUtf8 set to 0.
 *
 * Side effects:
 *	Allocates memory on the interpreter's allocator.
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
 * th8RegexSetup --
 *
 *	Set up the global regex bridge state before a regex
 *	operation.
 *
 * Why / How:
 *	Acquires the global regex mutex (the engine relies on
 *	file-scope state that is not re-entrant), then publishes the
 *	active interpreter pointer and clears the OOM flag so the
 *	engine's allocation hooks resolve to this interp.  Paired
 *	with th8RegexTeardown, which must run on every exit path.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Enters the global mutex and sets th8_regex_interp /
 *	th8_regex_oom_flag.
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
 * Why / How:
 *	The bridge's file-scope state (th8_regex_interp,
 *	th8_regex_oom_flag) and the global mutex are shared serially
 *	across regex operations, so each operation must publish them
 *	on entry (th8RegexSetup) and retract them here on every exit
 *	path.  Clearing the interp pointer prevents a later,
 *	setup-less engine invocation from charging work to a stale
 *	interp (the Bug 26 window), and releasing the mutex last
 *	keeps subsequent callers from deadlocking.
 *
 * Parameters:
 *	(none)
 *
 * Results:
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
 * Why / How:
 *	A pathological pattern can compile into an enormous NFA and
 *	drive catastrophic match times (a regex-bomb), so before any
 *	pattern is executed its state count is measured with
 *	th8_regex_analyze and compared against TH8_REGEX_MAX_STATES.
 *	Rejecting at compile time -- rather than relying only on the
 *	per-operation cancel/limit checks -- caps worst-case cost up
 *	front.
 *
 * Results:
 *	TH8_OK if the pattern is within the state limit; TH8_ERROR
 *	(with an interpreter result of "regular expression too
 *	complex") if it exceeds TH8_REGEX_MAX_STATES.
 *
 * Side effects:
 *	On rejection, sets the interpreter result message.
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
 *	Implements the Tcl [regexp] command: match a string against a
 *	regular expression.
 *
 *	regexp ?SWITCHES? PATTERN STRING ?MATCHVAR? ?SUBVAR ...?
 *
 *	Supports -nocase, -expanded, -line, -linestop, -lineanchor,
 *	-all, -inline, -indices, -start, -about, and -- switches.
 *
 * Why / How:
 *	Parses the switch prefix into REG_* compile/exec flags, then
 *	widens the pattern and subject to UTF-32 with th8Utf8ToChr,
 *	compiles via th8_regcomp, and rejects regex-bombs with
 *	th8RegexCheckComplexity before executing.  th8_regexec is run
 *	in a loop (advancing past each match, and past zero-length
 *	matches by one, so -all cannot spin) collecting results.  The
 *	-inline form builds a result list of matches/submatches; the
 *	variable form stores each (sub)match into the caller's
 *	MATCHVAR/SUBVAR arguments; -indices reports "start end" pairs
 *	instead of text; -about returns compiled-pattern metadata
 *	without matching.  All setup/teardown of the shared engine
 *	state is bracketed by th8RegexSetup/th8RegexTeardown on every
 *	exit path.
 *
 * Results:
 *	TH8_OK on success.  The interpreter result is the match count
 *	for -all, the match list for -inline, or 1/0 for a plain
 *	match; -about yields a metadata dictionary.  TH8_ERROR (with
 *	an interpreter result message) on a wrong argument count, a
 *	bad switch, a compile error, an over-complex pattern, out of
 *	memory, or interpreter cancellation/limit.
 *
 * Side effects:
 *	Allocates and frees working buffers; may set caller variables
 *	(when TH8_ENABLE_VARIABLES); sets the interpreter result.
 *	Enters and leaves the global regex mutex.
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
			    TH8_STR_APPEND(interp, &zIdx, &nIdx, zBuf, nBuf);
			    TH8_STR_APPEND(interp, &zIdx, &nIdx, " ", 1);
			    Th8_SetResultInt(
			        interp, (int)pmatch[k].rm_eo - 1);
			    zBuf = Th8_GetResult(interp, &nBuf);
			    TH8_STR_APPEND(interp, &zIdx, &nIdx, zBuf, nBuf);
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
			    TH8_STR_APPEND(interp, &zIdx, &nIdx, zBuf, nBuf);
			    TH8_STR_APPEND(interp, &zIdx, &nIdx, " ", 1);
			    Th8_SetResultInt(
			        interp, (int)pmatch[k].rm_eo - 1);
			    zBuf = Th8_GetResult(interp, &nBuf);
			    TH8_STR_APPEND(interp, &zIdx, &nIdx, zBuf, nBuf);
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

	rc = TH8_OK;
	goto cleanup;

oom:
	Th8_Free(interp, zInlineList);
	rc = TH8_ERROR;
    }

    /*
     * Cleanup.
     */

cleanup:
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
 *	Implements the Tcl [regsub] command: perform regular
 *	expression substitution.
 *
 *	regsub ?SWITCHES? PATTERN STRING REPLACEMENT ?VARNAME?
 *
 *	Supports -nocase, -all, -line, and -- switches.
 *
 * Why / How:
 *	Parses the switch prefix into REG_* flags, widens the pattern
 *	and subject to UTF-32 with th8Utf8ToChr, compiles via
 *	th8_regcomp, and rejects regex-bombs with
 *	th8RegexCheckComplexity.  It then walks matches with
 *	th8_regexec, appending the pre-match text, then the expanded
 *	replacement -- where & is the whole match, \1..\9 are
 *	submatches, and \\ or any escaped char is a literal -- and
 *	finally the trailing text.  Zero-length matches advance by
 *	one code point (copied through) so -all cannot spin.  When no
 *	match occurs the original string is emitted unchanged.  Shared
 *	engine state is bracketed by th8RegexSetup/th8RegexTeardown on
 *	every exit path.
 *
 * Results:
 *	TH8_OK on success: with a VARNAME the substituted string is
 *	stored in that variable and the result is the substitution
 *	count; without one the result is the substituted string.
 *	TH8_ERROR (with an interpreter result message) on a wrong
 *	argument count, a bad switch, a compile error, an over-complex
 *	pattern, or out of memory.
 *
 * Side effects:
 *	Allocates and frees working buffers; may set the caller's
 *	VARNAME variable (when TH8_ENABLE_VARIABLES); sets the
 *	interpreter result.  Enters and leaves the global regex mutex.
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
	    TH8_STR_APPEND(interp, &zOut, &nOut, zPre, nPre);
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
			    TH8_STR_APPEND(interp, &zOut, &nOut, zSub, nSub);
			    Th8_Free(interp, zSub);
			}
		    } else {
			TH8_STR_APPEND(interp, &zOut, &nOut, &zRepl[r], 1);
		    }
		} else if (zRepl[r] == '&') {
		    size_t nSub;
		    char *zSub;
		    int nSubChr;

		    nSubChr = (int)(pmatch[0].rm_eo - pmatch[0].rm_so);
		    zSub = th8ChrToUtf8(
		        interp, &aChrStr[pmatch[0].rm_so], nSubChr, &nSub);
		    TH8_STR_APPEND(interp, &zOut, &nOut, zSub, nSub);
		    Th8_Free(interp, zSub);
		} else {
		    TH8_STR_APPEND(interp, &zOut, &nOut, &zRepl[r], 1);
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
		TH8_STR_APPEND(interp, &zOut, &nOut, zOne, nOne);
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
	TH8_STR_APPEND(interp, &zOut, &nOut, zTail, nTail);
	Th8_Free(interp, zTail);
    } else if (nSubs == 0) {
	/*
	 * No match: output original string unchanged.
	 */

	TH8_STR_APPEND(interp, &zOut, &nOut, argv[iArg - 2], argl[iArg - 2]);
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

cleanup:
    Th8_Free(interp, zOut);
    th8_regfree(&re);
    Th8_Free(interp, pmatch);
    Th8_Free(interp, aChrPat);
    Th8_Free(interp, aChrStr);
    th8RegexTeardown();

    return rc;

oom:
    rc = TH8_ERROR;
    goto cleanup;
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
 * Why / How:
 *	The plugin loader discovers a plugin's commands with a
 *	two-call protocol: first a count-only call (NULL pCommand)
 *	to learn how many entries exist, then a second call with a
 *	buffer of that size to copy them out.  This implements that
 *	protocol over the static th8RegexpCommands table, refusing to
 *	write past a caller buffer that is too small so the loader
 *	never overflows.
 *
 * Parameters:
 *	pCommand  -- caller-supplied output buffer or NULL.
 *	pnCommand -- in/out count.
 *
 * Results:
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
