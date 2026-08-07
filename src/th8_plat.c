/*
 * th8_plat.c -- Platform wrapper functions for TH8.
 *
 * Internal platform wrapper functions that delegate to
 * Th8_Platform callbacks.  Lives outside th8_core.c so the core
 * file can stay focused on the interpreter engine.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_meta_posix.h"
#include "th8_plat.h"
#include "th8.h"
#include "th8_int.h"
#include "th8_int_core.h"

/*
 * The global mutex state lives in th8_core.c.  We access it here via
 * extern declarations so that th8GlobalMutexEnter/Leave can
 * operate on it.
 */

extern Th8_Mutex th8GlobalMutex;
extern int th8GlobalMutexReady;
extern Th8_Platform th8GlobalPlatform;


/*
 *----------------------------------------------------------------------
 *
 * th8ResolveCtx --
 *
 *	Resolve the context pointer for a platform callback.  If a
 *	per-callback override has been set via Th8_SetPlatformContext,
 *	return that.  Otherwise return the platform's default pCtx.
 *
 * Why / How:
 *	Platform callbacks accept a void* context so that embedders
 *	can attach per-callback state (e.g. a different allocator
 *	context for xMalloc vs. xOutput).  This function implements
 *	the two-tier dispatch: first check the per-callback hash
 *	table keyed by the callback's function pointer, then fall
 *	back to the platform-wide default context.
 *
 * Results:
 *	The resolved context pointer (never NULL unless pDefault
 *	is NULL and no override exists).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void *
th8ResolveCtx(Th8_Interp *interp, Th8_PlatformFunc xCallback, void *pDefault)
{
    /* th8ResolveCtx is called only from the platform dispatch
     * wrappers below, all of which receive `interp` from a
     * caller already running inside an active interp.  C1 is
     * ALWAYS T at runtime. */
    if (ALWAYS(interp) && interp->paCallbackCtx) {
	Th8_HashEntry *pEntry = Th8_HashFind(
	    interp, interp->paCallbackCtx, (const char *)&xCallback,
	    sizeof(xCallback), 0);
	if (pEntry && ALWAYS(pEntry->pData)) {
	    return pEntry->pData;
	}
    }
    return pDefault;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetPlatformContext --
 *
 *	Associate a per-callback context pointer with a specific
 *	platform callback function.
 *
 * Why / How:
 *	Embedders may need different context state for different
 *	platform callbacks (e.g. a crypto library context for
 *	xRandomBytes vs. a file-system context for xGetData).
 *	This function stores the override in a hash table keyed
 *	by the callback function pointer.  Passing NULL for pCtx
 *	removes the override, reverting to the platform default.
 *	The hash table is lazily allocated on first use to avoid
 *	overhead when no overrides are needed.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if interp or xCallback is NULL
 *	or if hash allocation fails.
 *
 * Side effects:
 *	May allocate the per-interpreter callback context hash table.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetPlatformContext(
    Th8_Interp *interp,
    Th8_PlatformFunc xCallback,
    void *pCtx)
{
    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!xCallback) return TH8_ERROR;

    if (!pCtx) {
	/*
	 * NULL context: remove the override.
	 */

	if (interp->paCallbackCtx) {
	    Th8_HashRemove(
	        interp, interp->paCallbackCtx, (const char *)&xCallback,
	        sizeof(xCallback));
	}
	return TH8_OK;
    }

    /*
     * Lazy-create the hash table.
     */

    if (!interp->paCallbackCtx) {
	interp->paCallbackCtx = Th8_HashNew(interp);
	if (!interp->paCallbackCtx) return TH8_ERROR;
    }

    {
	Th8_HashEntry *pEntry = Th8_HashFind(
	    interp, interp->paCallbackCtx, (const char *)&xCallback,
	    sizeof(xCallback), 1);
	if (!pEntry) return TH8_ERROR;
	pEntry->pData = pCtx;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPlatformContext --
 *
 *	Retrieve the context pointer for a platform callback.
 *	Returns the per-callback override if one was set via
 *	Th8_SetPlatformContext, otherwise the platform default.
 *
 * Why / How:
 *	This is the public read counterpart to Th8_SetPlatformContext.
 *	It uses th8ResolveCtx to perform the two-tier dispatch
 *	(per-callback override, then platform default) and writes
 *	the result through the ppCtx out-parameter.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if interp or ppCtx is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetPlatformContext(
    Th8_Interp *interp,
    Th8_PlatformFunc xCallback,
    void **ppCtx)
{
    const Th8_Platform *p;

    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!ppCtx) return TH8_ERROR;

    p = Th8_GetPlatform(interp);
    *ppCtx = th8ResolveCtx(interp, xCallback, p ? p->pCtx : NULL);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EmitTrace --
 *
 *	Emit a formatted diagnostic trace message via the platform's
 *	xEmitTrace callback.  Accepts printf-style format and args.
 *	Formats into a 512-byte stack buffer (truncated if longer).
 *	No-op if the platform's xEmitTrace callback is NULL.
 *
 * Why / How:
 *	Diagnostic tracing needs to work even when the interpreter
 *	is partially initialized or NULL.  The function first tries
 *	the per-interpreter platform, then falls back to the global
 *	platform (protected by the global mutex).  It resolves
 *	xVsnprintf from the same platform source as xEmitTrace
 *	rather than calling th8Vsnprintf(), because that wrapper's
 *	NULL-callback fallback uses TH8_TRACE_ERR which would
 *	recurse back into Th8_EmitTrace.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May produce platform-specific diagnostic output (e.g.,
 *	OutputDebugStringA on Win32, syslog on POSIX).
 *
 *----------------------------------------------------------------------
 */

void
Th8_EmitTrace(Th8_Interp *interp, const char *zFmt, ...)
{
    const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;
    void (*xTrace)(Th8_Interp *, void *, const char *) = NULL;
    void *pTraceCtx = NULL;

    if (p && ALWAYS(p->xEmitTrace)) {
	xTrace = p->xEmitTrace;
	pTraceCtx =
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xEmitTrace, p->pCtx);
    } else {
	/*
	 * Global-platform fallback -- protect the read with the
	 * global mutex.  Copy pointers to locals while holding
	 * the mutex, then release before calling through them.
	 */
	th8MaybeGlobalMutexEnter(NULL);
	if (th8GlobalPlatform.xEmitTrace) {
	    xTrace = th8GlobalPlatform.xEmitTrace;
	    pTraceCtx = th8GlobalPlatform.pCtx;
	}
	th8MaybeGlobalMutexLeave(NULL);
    }

    /* Split per Finding 005 sec. 5b.  C1=F (xTrace==NULL after
     * both per-interp and global lookup) is intrinsic-dead in
     * the test corpus: the default platform always provides
     * xEmitTrace.  Nested single-condition `if`s remove the
     * dead C1-Pair from the MC/DC denominator. */
    if (xTrace) {
	if (zFmt) {
	    char zBuf[512];
	    va_list ap;

        /*
	 * Resolve xVsnprintf from the same platform source as
	 * xEmitTrace.  We call the callback directly rather than
	 * th8Vsnprintf() because that wrapper's NULL-callback
	 * fallback path uses TH8_TRACE_ERR -> Th8_EmitTrace,
	 * which would be recursive.
	 */
	    int (*xVsn)(
	        Th8_Interp *, void *, char *, size_t, const char *,
	        va_list) = NULL;
	    void *pVsnCtx = NULL;

	    if (p && ALWAYS(p->xVsnprintf)) {
		xVsn = p->xVsnprintf;
		pVsnCtx = th8ResolveCtx(
		    interp, (Th8_PlatformFunc)p->xVsnprintf, p->pCtx);
	    } else {
		th8MaybeGlobalMutexEnter(NULL);
		if (th8GlobalPlatform.xVsnprintf) {
		    xVsn = th8GlobalPlatform.xVsnprintf;
		    pVsnCtx = th8GlobalPlatform.pCtx;
		}
		th8MaybeGlobalMutexLeave(NULL);
	    }

	    if (!xVsn) {
		xTrace(interp, pTraceCtx, zFmt);
		return; /* cannot format without vsnprintf */
	    }

	    va_start(ap, zFmt);
	    xVsn(interp, pVsnCtx, zBuf, sizeof(zBuf), zFmt, ap);
	    va_end(ap);

	    zBuf[sizeof(zBuf) - 1] = '\0';
	    xTrace(interp, pTraceCtx, zBuf);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Memcmp --
 *
 *	Compare memory via the platform.  Public so that th8_lang.c
 *	can use it without accessing interpreter internals.
 *
 * Why / How:
 *	TH8 never calls libc directly; all memory operations go
 *	through the platform dispatch layer.  This function wraps
 *	the platform's xMemcmp callback so that any module with
 *	access to a Th8_Interp can compare memory without knowing
 *	which platform implementation is active.  Returns 0 when
 *	both pointers are NULL or when the callback is missing.
 *
 * Results:
 *	Negative, zero, or positive integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Memcmp(
    Th8_Interp *interp, /* Interpreter for platform access. */
    const void *a,  /* First buffer. */
    const void *b,  /* Second buffer. */
    size_t n)   /* Number of bytes. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return 0;
    pPlatform = Th8_GetPlatform(interp);

    if (n == 0) return 0;
    /* Split per Finding 005.  Both arms preserve the
     * a/b-asymmetric return convention. */
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    if (pPlatform->xMemcmp) {
	return pPlatform->xMemcmp(interp, pPlatform->pCtx, a, b, n);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Memcpy --
 *
 *	Copy memory via the platform.  Public so th8_lang.c can use
 *	it for proc body copying without accessing internals.
 *
 * Why / How:
 *	Delegates to the platform's xMemcpy callback, maintaining
 *	the invariant that TH8 never calls libc directly.  Guards
 *	against NULL pointers and zero-length copies by returning
 *	dst immediately, so callers need not check edge cases.
 *
 * Results:
 *	Pointer to dst.
 *
 * Side effects:
 *	Memory is copied.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_Memcpy(
    Th8_Interp *interp, /* Interpreter for platform access. */
    void *dst,   /* Destination buffer. */
    const void *src,  /* Source buffer. */
    size_t n)   /* Number of bytes. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return dst;
    pPlatform = Th8_GetPlatform(interp);

    if (n == 0 || !dst || !src) return dst;
    if (pPlatform->xMemcpy) {
	return pPlatform->xMemcpy(interp, pPlatform->pCtx, dst, src, n);
    }
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Memset --
 *
 *	Fill memory via the platform's xMemset callback.  When xMemset
 *	is not available, falls back to a volatile-write loop (which
 *	prevents the compiler from optimizing away zero-fills).
 *
 * Why / How:
 *	Unlike Th8_Memcmp and Th8_Memcpy, this function provides a
 *	built-in fallback when the platform callback is NULL, because
 *	secure zeroing of sensitive data (keys, tokens) must succeed
 *	even with a minimal platform.  The fallback uses a volatile
 *	read-XOR-write pattern: the read + XOR dependency chain
 *	prevents the compiler from proving the write is redundant,
 *	ensuring dead-store elimination cannot remove the zeroing.
 *
 * Results:
 *	Pointer to dst.
 *
 * Side effects:
 *	Memory is filled.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_Memset(
    Th8_Interp *interp, /* Interpreter for platform access. */
    void *dst,   /* Destination buffer. */
    int c,   /* Fill byte. */
    size_t n)   /* Number of bytes. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return dst;
    pPlatform = Th8_GetPlatform(interp);

    if (n == 0 || !dst) return dst;
    if (pPlatform->xMemset) {
	return pPlatform->xMemset(interp, pPlatform->pCtx, dst, c, n);
    }
    /*
     * Fallback: volatile read-XOR-write prevents dead-store
     * elimination.  The read + XOR dependency chain makes
     * it impossible for the optimizer to prove the write
     * is redundant.
     */
    {
	volatile unsigned char *vp = (volatile unsigned char *)dst;

	if (c == 0) {
	    while (n--) {
		unsigned char c2 = *vp;
		*vp++ = c2 ^ c2;
	    }
	} else {
	    while (n--)
		*vp++ = (unsigned char)c;
	}
    }
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Memmove --
 *
 *	Move memory via the platform's xMemmove (overlapping safe).
 *
 * Why / How:
 *	Wraps the platform's xMemmove callback to provide
 *	overlap-safe memory movement.  This is needed by the
 *	string and list manipulation code where source and
 *	destination buffers may overlap (e.g. in-place deletion
 *	of list elements).  Returns dst unchanged if the callback
 *	is missing, which is safe only if the caller can tolerate
 *	a no-op -- a missing xMemmove indicates a fatally
 *	incomplete platform.
 *
 * Results:
 *	Pointer to dst.
 *
 * Side effects:
 *	Memory is moved (overlapping regions handled correctly).
 *
 *----------------------------------------------------------------------
 */

void *
th8Memmove(
    Th8_Interp *interp, /* Interpreter for platform access. */
    void *dst,   /* Destination buffer. */
    const void *src,  /* Source buffer. */
    size_t n)   /* Number of bytes. */
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (n == 0 || !dst || !src) return dst;
    if (pPlatform->xMemmove) {
	return pPlatform->xMemmove(interp, pPlatform->pCtx, dst, src, n);
    }
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Strcmp --
 *
 *	Compare two NUL-terminated strings via the platform's xStrcmp.
 *
 * Why / How:
 *	Wraps the platform's xStrcmp callback for string comparison
 *	throughout the interpreter (command lookup, variable names,
 *	option parsing).  Handles NULL pointers defensively: NULL
 *	sorts before any non-NULL string, and two NULLs compare
 *	equal.  Returns 0 if the callback is missing, which treats
 *	all strings as equal -- a deliberately safe default that
 *	avoids crashes in a minimal platform.
 *
 * Results:
 *	Negative, zero, or positive integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8Strcmp(
    Th8_Interp *interp, /* Interpreter for platform access. */
    const char *s1,  /* First string. */
    const char *s2)  /* Second string. */
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    if (pPlatform->xStrcmp) {
	return pPlatform->xStrcmp(interp, pPlatform->pCtx, s1, s2);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Strchr --
 *
 *	Locate the first occurrence of byte c in string s via the
 *	platform's xStrchr.
 *
 * Why / How:
 *	Wraps the platform's xStrchr callback for forward byte
 *	search in strings.  Used by the parser and command dispatch
 *	to find delimiters, separators, and special characters.
 *	Returns NULL if s is NULL or if the callback is missing.
 *
 * Results:
 *	Pointer to the first occurrence of c in s, or NULL if
 *	not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
th8Strchr(
    Th8_Interp *interp, /* Interpreter for platform access. */
    const char *s,  /* String to search. */
    int c)   /* Byte to find. */
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (!s) return NULL;
    if (pPlatform->xStrchr) {
	return pPlatform->xStrchr(interp, pPlatform->pCtx, s, c);
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Strrchr --
 *
 *	Locate the last occurrence of byte c in NUL-terminated
 *	string s.  Implemented inline rather than via a platform
 *	callback.
 *
 * Why / How:
 *	No platform callback exists for reverse character search
 *	(xStrrchr is not part of Th8_Platform), so this function
 *	performs a simple forward scan keeping track of the last
 *	match.  The interp parameter is accepted but unused, for
 *	API uniformity with the other string wrapper functions.
 *
 * Results:
 *	Pointer to the last occurrence of c in s, or NULL if
 *	not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
th8Strrchr(
    Th8_Interp *interp, /* Interpreter (unused; for API uniformity). */
    const char *s,  /* String to search. */
    int c)   /* Byte to find. */
{
    const char *last = 0;
    (void)interp;
    if (!s) return 0;
    while (*s) {
	if (*s == c) last = s;
	s++;
    }
    return (char *)last;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Atoi --
 *
 *	Convert a NUL-terminated decimal string to int via the
 *	platform's xAtoi.
 *
 * Why / How:
 *	Wraps the platform's xAtoi callback for decimal string-to-
 *	integer conversion.  Used internally for parsing integer
 *	arguments in commands like [incr] and [string range].
 *	Returns 0 on NULL input or missing callback, which is a
 *	safe default for arithmetic contexts.
 *
 * Results:
 *	Integer value of the decimal string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8Atoi(
    Th8_Interp *interp, /* Interpreter for platform access. */
    const char *s)  /* String to convert. */
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (!s) return 0;
    if (pPlatform->xAtoi) {
	return pPlatform->xAtoi(interp, pPlatform->pCtx, s);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Qsort --
 *
 *	Sort an array via the platform's xQsort callback.
 *
 * Why / How:
 *	Wraps the platform's xQsort callback so that the
 *	interpreter's [lsort] command and internal sorted lookups
 *	use the platform-provided sort implementation.  Guards
 *	against NULL base, degenerate element counts (< 2), and
 *	NULL comparators by returning immediately.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The array pointed to by base is sorted in-place.
 *
 *----------------------------------------------------------------------
 */

void
th8Qsort(
    Th8_Interp *interp, /* Interpreter for platform access. */
    void *base,  /* Array base. */
    size_t nmemb,  /* Number of elements. */
    size_t size,  /* Element size. */
    int (*cmp)(const void *, const void *))
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (!base || nmemb < 2 || !cmp) return;
    if (pPlatform->xQsort) {
	pPlatform->xQsort(interp, pPlatform->pCtx, base, nmemb, size, cmp);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8Vsnprintf --
 *
 *	Formatted output into a sized buffer via the platform's
 *	xVsnprintf callback.
 *
 * Why / How:
 *	This is the core formatting primitive used by th8Snprintf
 *	and throughout the interpreter for error messages, result
 *	formatting, and diagnostics.  It delegates to the platform's
 *	xVsnprintf callback, ensuring TH8 never calls libc's
 *	vsnprintf directly.  If the callback is missing, the buffer
 *	is NUL-terminated and 0 is returned, preventing garbage
 *	output.
 *
 * Results:
 *	Number of characters written (excluding NUL), or 0 if the
 *	callback is missing or the buffer is empty/NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8Vsnprintf(
    Th8_Interp *interp, /* Interpreter for platform access. */
    char *buf,   /* Output buffer. */
    size_t size,  /* Buffer size. */
    const char *fmt,  /* Format string. */
    va_list ap)  /* Argument list. */
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (!buf || size == 0) return 0;
    if (pPlatform->xVsnprintf) {
	return pPlatform
	    ->xVsnprintf(interp, pPlatform->pCtx, buf, size, fmt, ap);
    }
    buf[0] = '\0';
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Snprintf --
 *
 *	Variadic convenience wrapper around th8Vsnprintf.
 *
 * Why / How:
 *	Most callers find variadic arguments more natural than
 *	passing a va_list.  This thin wrapper converts the variadic
 *	arguments to a va_list and delegates to th8Vsnprintf,
 *	keeping the actual formatting logic in one place.
 *
 * Results:
 *	Number of characters written (excluding NUL).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8Snprintf(
    Th8_Interp *interp, /* Interpreter for platform access. */
    char *buf,   /* Output buffer. */
    size_t size,  /* Buffer size. */
    const char *fmt,  /* Format string. */
    ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = th8Vsnprintf(interp, buf, size, fmt, ap);
    va_end(ap);
    return n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8TranslateCrLfToLf --
 *
 *	Convert \r\n sequences to \n in-place.  Returns the new
 *	length.  The buffer is modified in-place since \n is shorter
 *	than \r\n.
 *
 * Why / How:
 *	Windows and network protocols use \r\n line endings, but
 *	the TH8 interpreter operates on \n-terminated lines
 *	internally.  This function performs the translation using
 *	a read/write index pair that scans forward, collapsing
 *	each \r\n pair to a single \n.  Lone \r characters (not
 *	followed by \n) are preserved unchanged.  The buffer
 *	always shrinks or stays the same size, so no allocation
 *	is needed.
 *
 * Results:
 *	The new byte length of the translated buffer.
 *
 * Side effects:
 *	The buffer z is modified in-place.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8TranslateCrLfToLf(
    char *z,   /* Buffer to modify in-place. */
    size_t n)   /* Current byte length. */
{
    size_t r = 0; /* Read index. */
    size_t w = 0; /* Write index. */

    while (r < n) {
	if (z[r] == '\r' && r + 1 < n && z[r + 1] == '\n') {
	    z[w++] = '\n';
	    r += 2;
	} else {
	    z[w++] = z[r++];
	}
    }
    return w;
}


/*
 *----------------------------------------------------------------------
 *
 * th8TranslateLineEndings --
 *
 *	Verify that every newline in the buffer is preceded by a
 *	carriage return (\r\n), then translate all \r\n pairs to
 *	\n in-place.  The buffer is shortened; *pnBuf is updated.
 *
 *	If any bare \n (not preceded by \r) is found, the buffer
 *	is left unmodified and TH8_ERROR is returned.  This
 *	ensures the function only succeeds for buffers whose line
 *	endings are uniformly \r\n.
 *
 * Why / How:
 *	This is the strict variant of line-ending translation,
 *	used for data that is expected to have uniform \r\n endings
 *	(e.g. files read on Windows, or data from network
 *	protocols).  A two-pass approach is used: pass 1 validates
 *	that no bare \n exists, ensuring the buffer is unmodified
 *	on validation failure; pass 2 delegates to
 *	th8TranslateCrLfToLf for the actual in-place translation.
 *
 * Results:
 *	TH8_OK on success (*pnBuf updated).
 *	TH8_ERROR if the buffer contains bare \n.
 *
 * Side effects:
 *	The buffer is modified in-place on success.
 *
 *----------------------------------------------------------------------
 */

int
th8TranslateLineEndings(
    char *zBuf,   /* Buffer to modify in-place. */
    size_t *pnBuf)  /* IN/OUT: byte length. */
{
    size_t n = *pnBuf;
    size_t i;

    /*
     * Pass 1: verify all \n are preceded by \r.
     */

    for (i = 0; i < n; i++) {
	if (zBuf[i] == '\n') {
	    if (i == 0 || zBuf[i - 1] != '\r') {
		return TH8_ERROR;
	    }
	}
    }

    /*
     * Pass 2: translate \r\n to \n in-place.
     */

    *pnBuf = th8TranslateCrLfToLf(zBuf, n);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Input --
 *
 *	Read available data from the platform's xInput callback.
 *	This is the inverse of Th8_Output.  The host determines
 *	what "available data" means (a line, a block, a character).
 *	The caller must free *pzOut with Th8_Free.
 *
 * Why / How:
 *	Implements the [gets] command's data source.  First queries
 *	the current input channel via xGetInput (if available),
 *	then passes that channel handle to xInput.  When the
 *	TH8_TRANSLATE_EOL flag is set, \r\n sequences in the
 *	returned data are translated to \n via th8TranslateCrLfToLf
 *	so that scripts see uniform line endings regardless of the
 *	host platform.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on EOF/failure or if the
 *	callback is NULL.
 *
 * Side effects:
 *	Input is read from the host environment.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Input(
    Th8_Interp *interp, /* Interpreter. */
    char **pzOut,  /* OUT: data buffer (caller frees). */
    size_t *pnOut,  /* OUT: data length. */
    int flags)   /* TH8_TRANSLATE_EOL or 0. */
{
    const Th8_Platform *pPlatform;
    int rc;

    if (!interp) return TH8_ERROR;
    pPlatform = Th8_GetPlatform(interp);

    if (!pPlatform->xInput) {
	*pzOut = 0;
	*pnOut = 0;
	Th8_SetResultStatic(
	    interp, "channel \"stdin\" wasn't opened for reading", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Query the current input channel.  If xGetInput is NULL or
     * returns NULL, xInput will use its default (e.g., stdin).
     */
    {
	void *pChannel = 0;

	if (pPlatform->xGetInput) {
	    pPlatform->xGetInput(interp, pPlatform->pCtx, &pChannel);
	}
	rc = pPlatform
	         ->xInput(interp, pPlatform->pCtx, pzOut, pnOut, pChannel);
    }

    if (rc == TH8_OK && (flags & TH8_TRANSLATE_EOL) && *pzOut && *pnOut > 0) {
	*pnOut = th8TranslateCrLfToLf(*pzOut, *pnOut);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Output --
 *
 *	Route output through the platform's xOutput callback.
 *
 * Why / How:
 *	Implements the [puts] command's data sink.  First queries
 *	the current output channel via xGetOutput (if available),
 *	then routes the data through xOutput.  When the
 *	TH8_TRANSLATE_EOL flag is set, each \n in the output is
 *	expanded to \r\n by building a new buffer via
 *	Th8_StringAppend, since the output may grow larger than
 *	the input.  The temporary buffer is freed after the
 *	callback returns.
 *
 * Results:
 *	TH8_OK if callback exists, TH8_ERROR if NULL.
 *
 * Side effects:
 *	Output is sent to the host environment.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Output(
    Th8_Interp *interp, /* Interpreter. */
    const char *z,  /* Output string. */
    size_t n,   /* Byte length. */
    int flags)   /* TH8_TRANSLATE_EOL or 0. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return TH8_ERROR;
    pPlatform = Th8_GetPlatform(interp);

    if (!pPlatform->xOutput) {
	Th8_SetResultStatic(
	    interp, "channel \"stdout\" wasn't opened for writing",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Query the current output channel.
     */
    {
	void *pChannel = 0;

	if (pPlatform->xGetOutput) {
	    pPlatform->xGetOutput(interp, pPlatform->pCtx, &pChannel);
	}

	if (flags & TH8_TRANSLATE_EOL) {
	    /*
	     * Convert \n to \r\n.  Build a new buffer since the
	     * output may be larger than the input.
	     */

	    char *zOut = 0;
	    size_t nOut = 0;
	    size_t i;
	    int rc;

	    for (i = 0; i < n; i++) {
		if (z[i] == '\n') {
		    TH8_STR_APPEND(interp, &zOut, &nOut, "\r\n", 2);
		} else {
		    TH8_STR_APPEND(interp, &zOut, &nOut, &z[i], 1);
		}
	    }
	    rc = pPlatform->xOutput(
	        interp, pPlatform->pCtx, zOut ? zOut : "", nOut, pChannel);
	    Th8_Free(interp, zOut);
	    return rc;

oom:
	    Th8_Free(interp, zOut);
	    return TH8_ERROR;
	}

	return pPlatform->xOutput(interp, pPlatform->pCtx, z, n, pChannel);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_OutputError --
 *
 *	Route error output through the platform's xOutputError
 *	callback.
 *
 * Why / How:
 *	Provides a separate output path for error/diagnostic data,
 *	analogous to stderr.  Queries the error output channel via
 *	xGetErrorOutput (if available) and passes it to
 *	xOutputError.  Unlike Th8_Output, no EOL translation is
 *	performed -- error output is emitted verbatim.
 *
 * Results:
 *	TH8_OK if callback exists, TH8_ERROR if NULL.
 *
 * Side effects:
 *	Error output is sent to the host environment.
 *
 *----------------------------------------------------------------------
 */

int
Th8_OutputError(
    Th8_Interp *interp, /* Interpreter. */
    const char *z,  /* Output string. */
    size_t n)   /* Byte length. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return TH8_ERROR;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xOutputError) {
	void *pChannel = 0;

	if (pPlatform->xGetErrorOutput) {
	    pPlatform->xGetErrorOutput(interp, pPlatform->pCtx, &pChannel);
	}
	return pPlatform
	    ->xOutputError(interp, pPlatform->pCtx, z, n, pChannel);
    }
    Th8_SetResultStatic(
        interp, "channel \"stderr\" wasn't opened for writing", TH8_NOLEN);
    return TH8_ERROR;
}


#if defined(TH8_PLUGIN_IO)
/*
 *----------------------------------------------------------------------
 *
 * Th8_GetInput --
 *
 *	Query the current input channel handle via the platform's
 *	xGetInput callback.
 *
 * Why / How:
 *	Part of the plugin I/O API that allows embedders to
 *	inspect and redirect the interpreter's standard channels.
 *	Delegates to xGetInput; if the callback is NULL, sets
 *	*pChannel to 0 and returns TH8_OK (no channel configured).
 *
 * Results:
 *	TH8_OK always (the callback may update *pChannel).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetInput(Th8_Interp *interp, void **pChannel)
{
    const Th8_Platform *pPlat;

    if (!interp) return TH8_ERROR;
    pPlat = Th8_GetPlatform(interp);

    *pChannel = 0;
    if (pPlat->xGetInput) {
	return pPlat->xGetInput(interp, pPlat->pCtx, pChannel);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RedirectInput --
 *
 *	Redirect the interpreter's input channel to the given
 *	handle via the platform's xSetInput callback.
 *
 * Why / How:
 *	Allows embedders and the plugin I/O subsystem to replace
 *	the default stdin with a custom channel (e.g. a pipe, a
 *	GUI text widget).  Delegates to xSetInput; returns
 *	TH8_ERROR with a diagnostic message if the callback is
 *	not available.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if unsupported.
 *
 * Side effects:
 *	Subsequent Th8_Input calls will read from the new channel.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RedirectInput(Th8_Interp *interp, void *channel)
{
    const Th8_Platform *pPlat;

    if (!interp) return TH8_ERROR;
    pPlat = Th8_GetPlatform(interp);

    if (pPlat->xSetInput) {
	return pPlat->xSetInput(interp, pPlat->pCtx, channel);
    }
    Th8_SetResultStatic(
        interp, "input channel redirection not supported", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetOutput --
 *
 *	Query the current output channel handle via the platform's
 *	xGetOutput callback.
 *
 * Why / How:
 *	Part of the plugin I/O API.  Delegates to xGetOutput; if
 *	the callback is NULL, sets *pChannel to 0 and returns
 *	TH8_OK (no channel configured).
 *
 * Results:
 *	TH8_OK always (the callback may update *pChannel).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetOutput(Th8_Interp *interp, void **pChannel)
{
    const Th8_Platform *pPlat;

    if (!interp) return TH8_ERROR;
    pPlat = Th8_GetPlatform(interp);

    *pChannel = 0;
    if (pPlat->xGetOutput) {
	return pPlat->xGetOutput(interp, pPlat->pCtx, pChannel);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RedirectOutput --
 *
 *	Redirect the interpreter's output channel to the given
 *	handle via the platform's xSetOutput callback.
 *
 * Why / How:
 *	Allows embedders and the plugin I/O subsystem to replace
 *	the default stdout with a custom channel.  Delegates to
 *	xSetOutput; returns TH8_ERROR with a diagnostic message
 *	if the callback is not available.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if unsupported.
 *
 * Side effects:
 *	Subsequent Th8_Output calls will write to the new channel.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RedirectOutput(Th8_Interp *interp, void *channel)
{
    const Th8_Platform *pPlat;

    if (!interp) return TH8_ERROR;
    pPlat = Th8_GetPlatform(interp);

    if (pPlat->xSetOutput) {
	return pPlat->xSetOutput(interp, pPlat->pCtx, channel);
    }
    Th8_SetResultStatic(
        interp, "output channel redirection not supported", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetErrorOutput --
 *
 *	Query the current error output channel handle via the
 *	platform's xGetErrorOutput callback.
 *
 * Why / How:
 *	Part of the plugin I/O API.  Delegates to xGetErrorOutput;
 *	if the callback is NULL, sets *pChannel to 0 and returns
 *	TH8_OK (no channel configured).
 *
 * Results:
 *	TH8_OK always (the callback may update *pChannel).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetErrorOutput(Th8_Interp *interp, void **pChannel)
{
    const Th8_Platform *pPlat;

    if (!interp) return TH8_ERROR;
    pPlat = Th8_GetPlatform(interp);

    *pChannel = 0;
    if (pPlat->xGetErrorOutput) {
	return pPlat->xGetErrorOutput(interp, pPlat->pCtx, pChannel);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RedirectErrorOutput --
 *
 *	Redirect the interpreter's error output channel to the
 *	given handle via the platform's xSetErrorOutput callback.
 *
 * Why / How:
 *	Allows embedders and the plugin I/O subsystem to replace
 *	the default stderr with a custom channel.  Delegates to
 *	xSetErrorOutput; returns TH8_ERROR with a diagnostic
 *	message if the callback is not available.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if unsupported.
 *
 * Side effects:
 *	Subsequent Th8_OutputError calls will write to the new
 *	channel.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RedirectErrorOutput(Th8_Interp *interp, void *channel)
{
    const Th8_Platform *pPlat;

    if (!interp) return TH8_ERROR;
    pPlat = Th8_GetPlatform(interp);

    if (pPlat->xSetErrorOutput) {
	return pPlat->xSetErrorOutput(interp, pPlat->pCtx, channel);
    }
    Th8_SetResultStatic(
        interp, "error output channel redirection not supported", TH8_NOLEN);
    return TH8_ERROR;
}
#endif /* TH8_PLUGIN_IO */


/*
 *----------------------------------------------------------------------
 *
 * Th8_DataExists --
 *
 *	Test whether named data exists via the platform's xDataExists
 *	callback.  Returns 1 if it exists, 0 if not or if the callback
 *	is NULL.
 *
 * Why / How:
 *	Used by [file exists] and the [source] command to check
 *	whether a named data source (file, embedded resource, etc.)
 *	is available before attempting to read it.  The platform
 *	callback may also return file-type attributes via the
 *	pAttrs out-parameter (e.g. directory vs. regular file).
 *	Returns 0 (not found) when the callback is missing, which
 *	is the safest default for existence checks.
 *
 * Results:
 *	1 if the data exists, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DataExists(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName,  /* Data name to test. */
    size_t nName,  /* Byte length (TH8_NOLEN = NUL). */
    int *pAttrs)  /* OUT: file type attrs (may be NULL). */
{
    const Th8_Platform *pPlatform;

    if (!interp) return 0;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xDataExists) {
	return pPlatform
	    ->xDataExists(interp, pPlatform->pCtx, zName, nName, pAttrs);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NormalizePath --
 *
 *	Normalize a file path via the platform's xNormalizePath
 *	callback.  Returns a NUL-terminated string allocated via
 *	Th8_Malloc that the caller must free with Th8_Free.
 *	Returns NULL if the platform callback is not available
 *	or fails.
 *
 * Why / How:
 *	Path normalization is platform-specific (e.g. backslash vs.
 *	forward slash, case sensitivity, UNC paths on Win32).  This
 *	function delegates to the platform so that higher-level code
 *	(e.g. [file normalize], source path resolution) can work
 *	with canonical paths without platform-specific logic.
 *
 * Results:
 *	Allocated normalized path string, or NULL.
 *
 * Side effects:
 *	Allocates memory that the caller must free with Th8_Free.
 *
 *----------------------------------------------------------------------
 */

char *
Th8_NormalizePath(
    Th8_Interp *interp, /* Interpreter. */
    const char *zPath,  /* Path to normalize. */
    size_t nPath)  /* Byte length (TH8_NOLEN = NUL). */
{
    const Th8_Platform *pPlatform;

    if (!interp) return NULL;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xNormalizePath) {
	return pPlatform
	    ->xNormalizePath(interp, pPlatform->pCtx, zPath, nPath);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetCwd --
 *
 *	Return the current working directory via the platform's
 *	xGetCwd callback.  Returns a NUL-terminated string allocated
 *	via Th8_Malloc that the caller must free with Th8_Free.
 *	Returns NULL if the platform callback is not available
 *	or fails.
 *
 * Why / How:
 *	Used by [pwd] and path resolution logic.  Delegates to the
 *	platform's xGetCwd callback with a resolved per-callback
 *	context.  Returns NULL if the platform or callback is
 *	unavailable, which callers must handle gracefully.
 *
 * Results:
 *	Allocated string, or NULL.
 *
 * Side effects:
 *	Allocates memory that the caller must free with Th8_Free.
 *
 *----------------------------------------------------------------------
 */

char *
Th8_GetCwd(Th8_Interp *interp)
{
    const Th8_Platform *p;

    if (!interp) return NULL;
    p = Th8_GetPlatform(interp);
    if (ALWAYS(p) && p->xGetCwd) {
	return p->xGetCwd(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xGetCwd, p->pCtx));
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetCwd --
 *
 *	Change the current working directory via the platform's
 *	xSetCwd callback.  Returns TH8_OK on success or TH8_ERROR
 *	if the callback is NULL or rejects the path.
 *
 * Why / How:
 *	Used by [cd].  Delegates to the platform's xSetCwd callback
 *	with a resolved per-callback context.  Returns TH8_ERROR
 *	if the platform or callback is unavailable, so that [cd]
 *	can report an appropriate error to the script.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *
 * Side effects:
 *	May change the process working directory.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetCwd(Th8_Interp *interp, const char *zPath, size_t nPath)
{
    const Th8_Platform *p;

    if (!interp) return TH8_ERROR;
    p = Th8_GetPlatform(interp);
    if (ALWAYS(p) && p->xSetCwd) {
	return p->xSetCwd(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xSetCwd, p->pCtx),
	    zPath, nPath);
    }
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetLastError --
 *
 *	Retrieve the OS error code via the platform's xGetLastError.
 *	Returns -1 if interp is NULL or the callback is unavailable.
 *
 * Why / How:
 *	Provides script-level access to the most recent OS error code
 *	(e.g. errno on POSIX, GetLastError() on Win32).  The interp
 *	parameter is typed as void* so this function can be called
 *	from contexts that do not include the full interpreter header.
 *	Uses th8ResolveCtx for per-callback context dispatch.
 *
 * Results:
 *	The OS error code, or -1 if unavailable.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetLastError(void *interp)
{
    const Th8_Platform *p;

    if (!interp) return -1;
    p = Th8_GetPlatform((Th8_Interp *)interp);
    /* Bug 26: Th8_GetPlatform can return NULL during teardown. */
    if (!p || !p->xGetLastError) return -1;
    return p->xGetLastError(
        (Th8_Interp *)interp,
        th8ResolveCtx(
            (Th8_Interp *)interp, (Th8_PlatformFunc)p->xGetLastError,
            p->pCtx));
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetRealPath --
 *
 *	Resolve a path to its canonical absolute form via the
 *	platform's xGetRealPath callback.
 *
 * Why / How:
 *	Used by [file normalize] to resolve symlinks and relative
 *	path components to an absolute canonical path.  Delegates
 *	to the platform's xGetRealPath callback with a resolved
 *	per-callback context.  The result is written into a
 *	caller-provided buffer (zBuf/nBuf).
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the platform or callback
 *	is unavailable.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetRealPath(
    Th8_Interp *interp,
    const char *zPath,
    size_t nPath,
    char *zBuf,
    size_t nBuf)
{
    const Th8_Platform *p;

    if (!interp) return TH8_ERROR;
    p = Th8_GetPlatform(interp);
    if (ALWAYS(p) && p->xGetRealPath) {
	return p->xGetRealPath(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xGetRealPath, p->pCtx),
	    zPath, nPath, zBuf, nBuf);
    }
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetRootPath --
 *
 *	Return the filesystem root (mount point) for a given path
 *	via the platform's xGetRootPath callback.
 *
 * Why / How:
 *	Used by path classification logic to determine whether a
 *	path is absolute (rooted) and to extract the root prefix
 *	(e.g. "/" on POSIX, "C:\" on Win32).  Delegates to the
 *	platform's xGetRootPath callback with a resolved per-
 *	callback context.  The result is written into a caller-
 *	provided buffer (zBuf/nBuf).
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the platform or callback
 *	is unavailable.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetRootPath(
    Th8_Interp *interp,
    const char *zPath,
    size_t nPath,
    char *zBuf,
    size_t nBuf)
{
    const Th8_Platform *p;

    if (!interp) return TH8_ERROR;
    p = Th8_GetPlatform(interp);
    if (ALWAYS(p) && p->xGetRootPath) {
	return p->xGetRootPath(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xGetRootPath, p->pCtx),
	    zPath, nPath, zBuf, nBuf);
    }
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SameFile --
 *
 *	Test whether two paths refer to the same physical file via
 *	the platform's xSameFile callback.
 *
 * Why / How:
 *	String comparison is insufficient for file identity because
 *	hard links, symlinks, and case-insensitive filesystems can
 *	map different path strings to the same file.  This function
 *	delegates to the platform's xSameFile callback which uses
 *	OS-level identity (e.g. inode/device on POSIX, file index
 *	on Win32).  Returns 0 (not same) if the callback is missing.
 *
 * Results:
 *	1 if the paths refer to the same file, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SameFile(
    Th8_Interp *interp,
    const char *zName1,
    size_t nName1,
    const char *zName2,
    size_t nName2)
{
    const Th8_Platform *p;

    if (!interp) return 0;
    p = Th8_GetPlatform(interp);
    if (ALWAYS(p) && p->xSameFile) {
	return p->xSameFile(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xSameFile, p->pCtx),
	    zName1, nName1, zName2, nName2);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetData --
 *
 *	Retrieve data by opaque name via the platform's xGetData
 *	callback.  The caller must free *pzOut with Th8_Free.
 *
 * Why / How:
 *	This is the primary data-loading function used by [source]
 *	and [file read].  After retrieving raw data via xGetData,
 *	it invokes the PRE-phase policy callback (TH8_PHASE_READ)
 *	with the raw bytes, allowing signature verification or
 *	other security checks before the data is processed.  If
 *	TH8_TRANSLATE_EOL is set, \r\n is translated to \n via
 *	th8TranslateCrLfToLf.  Finally, the POST-phase policy
 *	callback is invoked with the translated data.  The two-
 *	phase policy design allows pre-translation verification
 *	(e.g. checking a signature over raw bytes) and post-
 *	translation auditing.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if callback is NULL or the
 *	data was not found.
 *
 * Side effects:
 *	Data is retrieved from the host environment.  Policy
 *	callbacks may be invoked.
 *
 *	IMPORTANT: under the signed-only policy the PRE-phase READ
 *	callback (th8PolicyVerifyData) populates ::th8_security
 *	with the verified file's algorithmName, dataName, policy,
 *	publicKeyToken, and any notBefore/notAfter annotations.
 *	Callers that evaluate the returned data (or otherwise care
 *	about the surrounding ::th8_security context) MUST bracket
 *	the Th8_GetData + Th8_Eval pair with Th8_SaveSystemVar /
 *	Th8_RestoreSystemVar -- exactly as Th8_EvalFile does (see
 *	src/th8_xlib.c).  The canonical file-evaluation API is
 *	Th8_EvalFile; only use raw Th8_GetData + Th8_Eval when
 *	Th8_EvalFile's semantics are unsuitable (e.g. silent skip
 *	on missing-file, mandatory for pkgIndex sourcing), and
 *	mirror Th8_EvalFile's save/restore pattern when you do.
 *	See th8SourcePkgIndex (th8_core.c) for a reference.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetData(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName,  /* Opaque name. */
    size_t nName,  /* Name length. */
    char **pzOut,  /* OUT: data buffer (caller frees). */
    size_t *pnOut,  /* OUT: data length. */
    int flags)   /* TH8_TRANSLATE_EOL or 0. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return TH8_ERROR;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xGetData) {
	int rc;

	/*
	 * Pass interp as pCtx so xGetData can allocate via
	 * Th8_Malloc.  The output buffer is owned by the
	 * caller and freed with Th8_Free.
	 */

	rc = pPlatform->xGetData(
	    interp, pPlatform->pCtx, zName, nName, pzOut, pnOut);
	if (rc != TH8_OK) {
	    *pzOut = 0;
	    *pnOut = 0;
	    Th8_ErrorMessage(interp, "couldn't retrieve \"", zName, nName);
	    return rc;
	}

	/*
	 * PRE-phase policy callback (READ): invoked with the raw
	 * file data before any EOL translation.  The callback can
	 * inspect or verify the raw bytes (e.g. signature
	 * verification).  If it returns non-TH8_OK, the read is
	 * aborted.
	 */

	{
	    Th8_PolicyProc xPolicyCb;
	    void *pPolicyCbCtx;

	    Th8_GetPolicyCallback(interp, &xPolicyCb, &pPolicyCbCtx);
	    if (xPolicyCb) {
		rc = xPolicyCb(
		    interp, TH8_PHASE_PRE | TH8_PHASE_READ, zName, nName,
		    *pzOut, *pnOut, 0, TH8_OK, pPolicyCbCtx);
		if (rc != TH8_OK) {
		    Th8_Free(interp, *pzOut);
		    *pzOut = 0;
		    *pnOut = 0;
		    return rc;
		}
	    }

	    if ((flags & TH8_TRANSLATE_EOL) && ALWAYS(*pzOut) && *pnOut > 0) {
		*pnOut = th8TranslateCrLfToLf(*pzOut, *pnOut);
	    }

	    /*
	     * POST-phase policy callback (READ).  The data has
	     * been read and translated.  The return value is
	     * ignored.
	     */

	    if (xPolicyCb) {
		xPolicyCb(
		    interp, TH8_PHASE_POST | TH8_PHASE_READ, zName, nName,
		    *pzOut, *pnOut, 0, rc, pPolicyCbCtx);
	    }
	}

	return rc;
    }
    *pzOut = 0;
    *pnOut = 0;
    Th8_SetResultStatic(interp, "source not available", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetTimeMs --
 *
 *	Get the current time in milliseconds via the platform.
 *	Returns 0 in *pMs if the callback is NULL (unsupported).
 *
 * Why / How:
 *	Used by [clock clicks] and internal timing (e.g. the
 *	[time] command).  Delegates to the platform's xTimeMs
 *	callback.  Returns 0 and TH8_OK when the callback is
 *	missing, so callers get a safe default rather than an
 *	error for optional timing.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetTimeMs(
    Th8_Interp *interp, /* Interpreter. */
    th8_int64_t *pMs)  /* OUT: milliseconds. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return TH8_ERROR;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xTimeMs) {
	return pPlatform->xTimeMs(interp, pPlatform->pCtx, pMs);
    }
    *pMs = 0;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetTimeUs --
 *
 *	Get the current monotonic time in microseconds via the
 *	platform's xTimeUs callback.  If xTimeUs is NULL, falls
 *	back to xTimeMs * 1000 (millisecond precision).
 *
 * Why / How:
 *	Provides higher-resolution timing for the [time] command
 *	and internal profiling.  Prefers the platform's xTimeUs
 *	callback for microsecond precision, but gracefully
 *	degrades to xTimeMs * 1000 when microsecond resolution
 *	is unavailable.  Returns 0 and TH8_OK if neither callback
 *	is available.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if no time source is available.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetTimeUs(
    Th8_Interp *interp, /* Interpreter. */
    th8_int64_t *pUs)  /* OUT: microseconds. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return TH8_ERROR;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xTimeUs) {
	return pPlatform->xTimeUs(interp, pPlatform->pCtx, pUs);
    }
    /* Fallback: milliseconds * 1000. */
    if (pPlatform->xTimeMs) {
	th8_int64_t ms = 0;
	int rc = pPlatform->xTimeMs(interp, pPlatform->pCtx, &ms);

	*pUs = ms * 1000;
	return rc;
    }
    *pUs = 0;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Sleep --
 *
 *	Sleep for nMs milliseconds via the platform's xSleep
 *	callback.  No-op if xSleep is NULL.
 *
 * Why / How:
 *	Used by the [after] command to suspend execution for a
 *	specified duration.  Delegates to the platform's xSleep
 *	callback.  Guards against negative and zero sleep times
 *	by requiring nMs > 0.  A missing callback silently does
 *	nothing, which is appropriate for environments where
 *	blocking sleep is not meaningful.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May block the calling thread for nMs milliseconds.
 *
 *----------------------------------------------------------------------
 */

void
Th8_Sleep(Th8_Interp *interp, int nMs)
{
    const Th8_Platform *pPlatform;

    if (!interp) return;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xSleep && nMs > 0) {
	pPlatform->xSleep(interp, pPlatform->pCtx, nMs);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPid --
 *
 *	Get the process ID via the platform.  Returns 0 if the
 *	callback is NULL (unsupported or unavailable).
 *
 * Why / How:
 *	Used by [pid] and the tcl_platform(pid) variable.
 *	Delegates to the platform's xGetPid callback.  Returns 0
 *	when the callback is missing, providing a safe default
 *	for environments where process identity is not meaningful.
 *
 * Results:
 *	Process ID, or 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetPid(Th8_Interp *interp) /* Interpreter. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return 0;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xGetPid) {
	return pPlatform->xGetPid(interp, pPlatform->pCtx);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetParentPid --
 *
 *	Get the parent process ID via the platform.  Returns 0 if the
 *	callback is NULL (unsupported or unavailable).
 *
 * Why / How:
 *	Delegates to the platform's xGetParentPid callback.
 *	Returns 0 when the callback is missing, providing a safe
 *	default for environments where parent process identity is
 *	not meaningful or not available.
 *
 * Results:
 *	Parent process ID, or 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetParentPid(Th8_Interp *interp) /* Interpreter. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return 0;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xGetParentPid) {
	return pPlatform->xGetParentPid(interp, pPlatform->pCtx);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetThreadId --
 *
 *	Get the CURRENT (calling) thread ID via the platform.  Returns 0
 *	if the callback is NULL (unsupported or unavailable).
 *
 * Why / How:
 *	Used for diagnostic tracing and thread-affinity assertions.
 *	Delegates to the platform's xGetThreadId callback.  Returns
 *	0 when the callback is missing, which is safe for single-
 *	threaded environments.
 *
 *	THREAD SAFETY: callable from any thread.  It reports the
 *	caller's own thread and reads only immutable interpreter state
 *	(the platform pointer), so it is exempt from the single-
 *	threaded-per-interpreter affinity contract -- this is how a
 *	foreign thread can compare itself against Th8_GetInterpThreadId.
 *
 * Results:
 *	The calling thread's ID, or 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

th8_uint64_t
Th8_GetThreadId(Th8_Interp *interp) /* Interpreter. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return 0;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xGetThreadId) {
	return pPlatform->xGetThreadId(interp, pPlatform->pCtx);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetEnv --
 *
 *	Return the value of the named environment variable as an
 *	allocated UTF-8 string.  Supports NULL interp (falls back
 *	to th8GlobalPlatform).  Returns NULL if the variable is
 *	not set or the callback is unavailable.
 *
 * Why / How:
 *	Environment variable access is needed during early
 *	initialization (before an interpreter exists) and during
 *	normal operation.  This function implements a two-tier
 *	dispatch: first try the per-interpreter platform's xGetEnv,
 *	then fall back to the global platform's xGetEnv (protected
 *	by the global mutex).  The global fallback copies the
 *	function pointer and context to locals under the mutex,
 *	then calls through them after releasing, to avoid holding
 *	the mutex during the callback.
 *
 * Results:
 *	Allocated UTF-8 string (caller frees with Th8_Free), or
 *	NULL if the variable is not set or unavailable.
 *
 * Side effects:
 *	May allocate memory.  May briefly hold the global mutex.
 *
 *----------------------------------------------------------------------
 */

char *
Th8_GetEnv(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *zName)  /* Variable name. */
{
    const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;

    if (p && p->xGetEnv) {
	return p->xGetEnv(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xGetEnv, p->pCtx),
	    zName);
    }
    {
	char *(*xEnv)(Th8_Interp *, void *, const char *) = NULL;
	void *pCtx = NULL;

	th8MaybeGlobalMutexEnter(NULL);
	if (th8GlobalPlatform.xGetEnv) {
	    xEnv = th8GlobalPlatform.xGetEnv;
	    pCtx = th8GlobalPlatform.pCtx;
	}
	th8MaybeGlobalMutexLeave(NULL);

	if (xEnv) {
	    return xEnv(interp, pCtx, zName);
	}
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_KeyValue --
 *
 *	Dispatch a key-value operation to the platform's xKeyValue
 *	callback.  Supports NULL interp (falls back to
 *	th8GlobalPlatform).  Returns TH8_ERROR if no callback is
 *	available.
 *
 * Why / How:
 *	Provides a generic key-value store interface used by the
 *	interpreter's metadata, configuration, and registry
 *	subsystems.  The op parameter selects the operation (GET,
 *	SET, UNSET, NAMES, etc.) and the callback implements the
 *	actual storage.  Like Th8_GetEnv, this uses a two-tier
 *	dispatch (per-interpreter then global platform) with
 *	mutex-protected copy-then-call for the global fallback,
 *	so the mutex is not held during the callback.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if no callback is available
 *	or the operation fails.
 *
 * Side effects:
 *	Depends on the operation (SET modifies storage, etc.).
 *	May briefly hold the global mutex during dispatch.
 *
 *----------------------------------------------------------------------
 */

int
Th8_KeyValue(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    int op,   /* TH8_KV_* operation code. */
    const char *zName,  /* Key name (or glob pattern). */
    size_t nName,  /* Length of zName. */
    const char *zValue,  /* Value (SET only; NULL otherwise). */
    size_t nValue)  /* Length of zValue. */
{
    const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;

    if (p && p->xKeyValue) {
	return p->xKeyValue(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xKeyValue, p->pCtx),
	    op, zName, nName, zValue, nValue);
    }
    {
	int (*xKv)(
	    Th8_Interp *, void *, int, const char *, size_t, const char *,
	    size_t) = NULL;
	void *pCtx = NULL;

	th8MaybeGlobalMutexEnter(NULL);
	if (th8GlobalPlatform.xKeyValue) {
	    xKv = th8GlobalPlatform.xKeyValue;
	    pCtx = th8GlobalPlatform.pCtx;
	}
	th8MaybeGlobalMutexLeave(NULL);

	if (xKv) {
	    return xKv(interp, pCtx, op, zName, nName, zValue, nValue);
	}
    }
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RandomBytes --
 *
 *	Fill a buffer with cryptographically random bytes via the
 *	platform's xRandomBytes callback.  Returns TH8_OK on success
 *	or TH8_ERROR if the callback is NULL or fails.
 *
 * Why / How:
 *	Cryptographic randomness is required for key generation,
 *	nonce creation, and other security-sensitive operations.
 *	This function delegates to the platform's xRandomBytes
 *	callback, which is expected to use an OS-level CSPRNG
 *	(e.g. arc4random on macOS/BSD, getrandom on Linux,
 *	BCryptGenRandom on Win32).  Returns TH8_ERROR when the
 *	callback is missing, since there is no safe fallback for
 *	cryptographic randomness.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *
 * Side effects:
 *	Fills pBuf with nByte random bytes.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RandomBytes(
    Th8_Interp *interp, /* Interpreter. */
    void *pBuf,   /* Output buffer. */
    size_t nByte)  /* Number of bytes to fill. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return TH8_ERROR;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xRandomBytes) {
	return pPlatform->xRandomBytes(interp, pPlatform->pCtx, pBuf, nByte);
    }
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DnsResolve --
 *
 *	Thin wrapper over the platform's xDnsResolve callback.
 *	Used by the libcurl integration to validate DNS via
 *	DNSSEC before pinning an IP address into curl's resolve
 *	list.
 *
 *	If interp or ppResult is NULL, or xDnsResolve is NULL,
 *	Th8_DnsResolve returns TH8_ERROR -- the caller is
 *	expected to fall back to a non-pinned resolution path or
 *	fail closed.
 *
 * Results:
 *	TH8_OK on success (check pResult->bogus for DNSSEC
 *	failure); TH8_ERROR if the platform has no resolver.
 *
 * Side effects:
 *	On success, allocates a Th8_DnsResult that the caller
 *	must release with Th8_DnsResolveFree.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DnsResolve(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    int eType,
    Th8_DnsResult **ppResult)
{
    const Th8_Platform *pPlatform;

    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!ppResult) return TH8_ERROR;
    *ppResult = NULL;
    pPlatform = Th8_GetPlatform(interp);
    if (!pPlatform || !pPlatform->xDnsResolve) return TH8_ERROR;
    return pPlatform
        ->xDnsResolve(interp, pPlatform->pCtx, zName, nName, eType, ppResult);
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_DnsResolveFree --
 *
 *	Public API: tear down a `Th8_DnsResult` previously
 *	returned by `Th8_DnsResolve`.  Dispatches to the
 *	platform's `xDnsResolveFree` callback so the same
 *	allocator that owns the result also frees it.
 *
 *	NULL `interp`, NULL `pResult`, missing platform, and
 *	missing platform callback each early-return safely
 *	so callers do not need defensive guards.  The
 *	individual `if`s are split into separate statements
 *	per FINDINGS.md Finding 005 sec. 5b so each guard's
 *	MC/DC C-pair is independently reachable.
 *
 * Parameters:
 *	interp  -- live interpreter.
 *	pResult -- result returned by `Th8_DnsResolve`, or
 *		NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Frees every resource owned by the result via the
 *	platform callback.
 *
 *----------------------------------------------------------------------
 */
void
Th8_DnsResolveFree(Th8_Interp *interp, Th8_DnsResult *pResult)
{
    const Th8_Platform *pPlatform;

    if (!interp) return;
    if (!pResult) return;
    pPlatform = Th8_GetPlatform(interp);
    if (!pPlatform || !pPlatform->xDnsResolveFree) return;
    pPlatform->xDnsResolveFree(interp, pPlatform->pCtx, pResult);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetExePath --
 *
 *	Return the path to the current executable via the platform's
 *	xGetExePath callback.  The caller must free the result with
 *	Th8_Free.  Returns NULL if the callback is not available.
 *
 * Why / How:
 *	Used to populate [info nameofexecutable] and for locating
 *	the script library relative to the executable.  Delegates
 *	to the platform's xGetExePath callback which uses OS-
 *	specific mechanisms (e.g. /proc/self/exe on Linux,
 *	_NSGetExecutablePath on macOS, GetModuleFileName on Win32).
 *
 * Results:
 *	Allocated path string (caller frees with Th8_Free), or NULL.
 *
 * Side effects:
 *	Allocates memory.
 *
 *----------------------------------------------------------------------
 */

char *
Th8_GetExePath(Th8_Interp *interp) /* Interpreter. */
{
    const Th8_Platform *pPlatform;

    if (!interp) return NULL;
    pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xGetExePath) {
	return pPlatform->xGetExePath(interp, pPlatform->pCtx);
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NotifyDeleteInterp --
 *
 *	Notify the platform that the interpreter is about to be
 *	deleted.  The platform should release any per-interpreter
 *	resources (e.g., dlclose loaded libraries, free cached state).
 *	pCtx is passed through to the platform callback.
 *
 * Why / How:
 *	Called from the interpreter teardown sequence in th8_core.c
 *	as one of the final steps before the interpreter struct is
 *	freed.  This gives the platform layer a chance to release
 *	resources that are tied to the interpreter's lifetime but
 *	managed by the platform (e.g. open file handles, loaded
 *	shared libraries, per-interpreter thread-local state).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Platform-specific resources may be released.
 *
 *----------------------------------------------------------------------
 */

void
th8NotifyDeleteInterp(Th8_Interp *interp, void *pCtx)
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xDeleteInterp) {
	pPlatform->xDeleteInterp(interp, pCtx);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8NotifyPreDeleteInterp --
 *
 *	Notify the platform that the interpreter is about to be
 *	deleted, BEFORE any core teardown has occurred.  The interp
 *	is still fully functional at this point.
 *
 * Why / How:
 *	Called from the very start of Th8_DeleteInterp so that the
 *	platform can invoke library _Unload entry points (which may
 *	call back into the interp via Th8_Eval, namespace deletion,
 *	math-func removal, etc.) while the interp's namespace,
 *	package registry, math-func registry, and other state are
 *	still intact.  Releasing platform-level handles (dlclose /
 *	FreeLibrary) is deferred to xDeleteInterp at the END of
 *	teardown so that any library xDel callbacks fired during
 *	namespace cleanup can still execute the library's text.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May invoke library _Unload callbacks, which can call back
 *	into the interpreter.
 *
 *----------------------------------------------------------------------
 */

void
th8NotifyPreDeleteInterp(Th8_Interp *interp, void *pCtx)
{
    const Th8_Platform *pPlatform = Th8_GetPlatform(interp);

    if (pPlatform->xPreDeleteInterp) {
	pPlatform->xPreDeleteInterp(interp, pCtx);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8GlobalMutexEnter --
 *
 *	Enter (acquire) the process-global mutex.  No-op if no
 *	mutex callbacks were provided (single-threaded mode).
 *
 * Why / How:
 *	Protects process-global state (e.g. loaded-library handle
 *	list, global platform struct) from concurrent access.
 *	Uses a two-tier dispatch: prefers the per-interpreter
 *	platform's xMutexEnter callback, falls back to the global
 *	platform's callback.  The th8GlobalMutexReady flag gates
 *	all mutex operations; it is set during Th8_Initialize
 *	after the mutex has been created.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The calling thread acquires the global mutex.
 *
 *----------------------------------------------------------------------
 */

void
th8GlobalMutexEnter(Th8_Interp *interp)
{
    if (th8GlobalMutexReady) {
	const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;

	if (p && p->xMutexEnter) {
	    p->xMutexEnter(
	        interp,
	        th8ResolveCtx(
	            interp, (Th8_PlatformFunc)p->xMutexEnter, p->pCtx),
	        &th8GlobalMutex);
	} else if (th8GlobalPlatform.xMutexEnter) {
	    th8GlobalPlatform
	        .xMutexEnter(interp, th8GlobalPlatform.pCtx, &th8GlobalMutex);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8GlobalMutexLeave --
 *
 *	Leave (release) the process-global mutex.  No-op if no
 *	mutex callbacks were provided (single-threaded mode).
 *
 * Why / How:
 *	The release counterpart to th8GlobalMutexEnter.  Uses the
 *	same two-tier dispatch (per-interpreter then global
 *	platform) and the same th8GlobalMutexReady gate.  Must
 *	be called exactly once for each th8GlobalMutexEnter call
 *	to avoid deadlock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The calling thread releases the global mutex.
 *
 *----------------------------------------------------------------------
 */

void
th8GlobalMutexLeave(Th8_Interp *interp)
{
    if (th8GlobalMutexReady) {
	const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;

	if (p && p->xMutexLeave) {
	    p->xMutexLeave(
	        interp,
	        th8ResolveCtx(
	            interp, (Th8_PlatformFunc)p->xMutexLeave, p->pCtx),
	        &th8GlobalMutex);
	} else if (th8GlobalPlatform.xMutexLeave) {
	    th8GlobalPlatform
	        .xMutexLeave(interp, th8GlobalPlatform.pCtx, &th8GlobalMutex);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemBarrier --
 *
 *	Full memory barrier / fence via the global platform's
 *	xMemBarrier callback.  Ensures that all memory writes
 *	issued before the barrier are visible to other threads
 *	before any reads or writes issued after the barrier.
 *	No-op if the callback is NULL (single-threaded assumption).
 *
 * Why / How:
 *	Required for the atomic CAS initialization pattern used
 *	by Th8_Initialize and other one-time-init code.  After
 *	a successful CAS, a memory barrier ensures that all
 *	stores to the initialized data are visible to other
 *	threads before they see the "initialized" flag.  Uses
 *	th8GlobalPlatform directly (not through an interp) since
 *	the barrier is needed during initialization before an
 *	interpreter exists.  Falls back to the global platform
 *	if no per-interpreter platform is available.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Issues a full memory fence.
 *
 *----------------------------------------------------------------------
 */

void
th8MemBarrier(Th8_Interp *interp)
{
    const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;

    if (p && p->xMemBarrier) {
	p->xMemBarrier(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xMemBarrier, p->pCtx));
    } else if (th8GlobalPlatform.xMemBarrier) {
	th8GlobalPlatform.xMemBarrier(interp, th8GlobalPlatform.pCtx);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IntCmpXchg --
 *
 *	Atomic integer compare-and-exchange via the global platform's
 *	xIntCmpXchg callback.  Compares *pTarget with iComparand; if
 *	equal, stores iExchange in *pTarget.  Returns the original
 *	value of *pTarget.  Falls back to a non-atomic operation if
 *	the callback is NULL (single-threaded assumption).
 *
 * Why / How:
 *	The CAS primitive is the foundation of TH8's thread-safe
 *	one-time initialization pattern: Th8_Initialize uses it
 *	to atomically transition global state from "uninitialized"
 *	to "initializing".  Uses th8GlobalPlatform directly (not
 *	through an interp) since it is needed during initialization
 *	before an interpreter exists.  The two-tier dispatch tries
 *	the per-interpreter platform first, then falls back to the
 *	global platform, then to a non-atomic implementation that
 *	is correct only under single-threaded assumptions.
 *
 * Results:
 *	The original value of *pTarget.
 *
 * Side effects:
 *	May atomically update *pTarget.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IntCmpXchg(
    Th8_Interp *interp,
    volatile int *pTarget,
    int iExchange,
    int iComparand)
{
    const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;

    if (p && p->xIntCmpXchg) {
	return p->xIntCmpXchg(
	    interp,
	    th8ResolveCtx(interp, (Th8_PlatformFunc)p->xIntCmpXchg, p->pCtx),
	    pTarget, iExchange, iComparand);
    }
    if (th8GlobalPlatform.xIntCmpXchg) {
	return th8GlobalPlatform.xIntCmpXchg(
	    interp, th8GlobalPlatform.pCtx, pTarget, iExchange, iComparand);
    }
    /* Fallback: non-atomic, single-threaded */
    {
	int old = *pTarget;
	if (old == iComparand) *pTarget = iExchange;
	return old;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Int64CmpXchg --
 *
 *	64-bit atomic integer compare-and-exchange via the platform's
 *	xIntCmpXchg64 callback.  Compares *pTarget with iComparand; if
 *	equal, stores iExchange in *pTarget.  Returns the original value
 *	of *pTarget.  Falls back to a non-atomic operation if the
 *	callback is NULL (single-threaded assumption).
 *
 * Why / How:
 *	The 64-bit sibling of Th8_IntCmpXchg.  It exists because the
 *	32-bit primitive is too narrow to hold a thread id: the owning-
 *	thread id (Th8_Interp.threadId) is read and published through
 *	this call so the affinity contract can be enforced without a
 *	lock.  The two-tier dispatch tries the per-interpreter platform
 *	first, then the global platform, then a non-atomic fallback that
 *	is correct only under single-threaded assumptions.
 *
 * Results:
 *	The original value of *pTarget.
 *
 * Side effects:
 *	May atomically update *pTarget.
 *
 *----------------------------------------------------------------------
 */

th8_uint64_t
Th8_Int64CmpXchg(
    Th8_Interp *interp,
    volatile th8_uint64_t *pTarget,
    th8_uint64_t iExchange,
    th8_uint64_t iComparand)
{
    const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;

    if (p && p->xIntCmpXchg64) {
	return p->xIntCmpXchg64(
	    interp,
	    th8ResolveCtx(
	        interp, (Th8_PlatformFunc)p->xIntCmpXchg64, p->pCtx),
	    pTarget, iExchange, iComparand);
    }
    if (th8GlobalPlatform.xIntCmpXchg64) {
	return th8GlobalPlatform.xIntCmpXchg64(
	    interp, th8GlobalPlatform.pCtx, pTarget, iExchange, iComparand);
    }
    /* Fallback: non-atomic, single-threaded */
    {
	th8_uint64_t old = *pTarget;
	if (old == iComparand) *pTarget = iExchange;
	return old;
    }
}


#if defined(__OpenBSD__)
#  include <unistd.h>
#endif

/*
 *----------------------------------------------------------------------
 *
 * Th8_Pledge --
 *
 *	Restrict the process to a set of allowed system calls via
 *	OpenBSD's pledge(2).  On all other platforms, this is a
 *	no-op that returns TH8_OK.
 *
 * Why / How:
 *	On OpenBSD, pledge(2) limits the system calls available to
 *	the process, providing defense-in-depth against code
 *	execution exploits.  This wrapper is called early in the
 *	shell startup to drop unnecessary privileges (e.g. after
 *	opening files and loading libraries).  The interp parameter
 *	is reserved for future use (e.g. per-interpreter pledge
 *	tracking) but currently unused.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if pledge(2) fails.
 *
 * Side effects:
 *	On OpenBSD, permanently restricts available system calls.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Pledge(
    Th8_Interp *interp,  /* Interpreter (reserved). */
    const char *zPromises, /* Pledge promises string. */
    const char *zExecPromises) /* Exec promises (or NULL). */
{
    (void)interp;
#if defined(__OpenBSD__)
    if (pledge(zPromises, zExecPromises) == -1) {
	return TH8_ERROR;
    }
#else
    (void)zPromises;
    (void)zExecPromises;
#endif
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Unveil --
 *
 *	Reveal (whitelist) a filesystem path with specific
 *	permissions via OpenBSD's unveil(2).  On all other
 *	platforms, this is a no-op that returns TH8_OK.
 *
 * Why / How:
 *	On OpenBSD, unveil(2) restricts filesystem visibility so
 *	that only explicitly revealed paths are accessible.  This
 *	wrapper is called during startup to whitelist the script
 *	library directory, working directory, and other necessary
 *	paths.  Passing NULL for both zPath and zPermissions locks
 *	the unveil set, preventing further reveals.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if unveil(2) fails.
 *
 * Side effects:
 *	On OpenBSD, modifies the process filesystem visibility.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Unveil(
    Th8_Interp *interp,  /* Interpreter (reserved). */
    const char *zPath,  /* Path to reveal (NULL to lock). */
    const char *zPermissions) /* "r", "w", "x", "c" (NULL to lock). */
{
    (void)interp;
#if defined(__OpenBSD__)
    if (unveil(zPath, zPermissions) == -1) {
	return TH8_ERROR;
    }
#else
    (void)zPath;
    (void)zPermissions;
#endif
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_UseDefaultPlatform --
 *
 *	Populate a Th8_Platform struct with the default layered
 *	configuration for the current OS.  The caller provides a
 *	stack- or heap-allocated struct; this function zeroes it
 *	and then merges the appropriate layers.
 *
 *	Layer order (first non-NULL wins per callback):
 *	  1. OS-specific allocator (mimalloc, macOS zone, or none)
 *	  2. OS platform (POSIX, Win32, Cosmopolitan)
 *	  3. libc (CRT wrappers for remaining NULL slots)
 *
 * Why / How:
 *	TH8's platform abstraction is designed as composable layers
 *	so that higher-priority implementations (e.g. a custom
 *	allocator) override lower-priority ones (e.g. libc).  This
 *	function selects the appropriate layers at compile time
 *	using preprocessor conditionals and merges them using
 *	Th8_MergePlatform, which only fills NULL callback slots.
 *	The struct is zeroed first via aggregate initialization
 *	(not Th8_Memset, since no platform exists yet at call
 *	time).  The environment variable platform
 *	(Th8_GetEnvPlatform) is intentionally NOT merged here --
 *	it must be explicitly opted into by the embedder.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if pPlatform is NULL or a
 *	merge operation fails.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_UseDefaultPlatform(Th8_Platform *pPlatform)
{
    if (!pPlatform) return TH8_ERROR;

    /*
     * Zero the struct via aggregate initialization.  We cannot
     * use Th8_Memset here because no interpreter or global
     * platform exists yet at the time this function is called.
     */
    {
	static const Th8_Platform zero = {0};
	*pPlatform = zero;
    }

#if defined(TH8_PLATFORM_COSMOPOLITAN)
    /*
     * Cosmopolitan Libc: cosmo base + POSIX + libc.
     */
    *pPlatform = *Th8_GetCosmopolitanPlatform();
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }

#elif defined(TH8_USE_MIMALLOC)
    /*
     * mimalloc: mimalloc base + OS layer + libc.
     *
     * On iOS the OS layer is iOS-deltas + macOS (private malloc
     * zone) + POSIX, in that merge order so iOS overrides win.
     * On Android the OS layer is Android-deltas + POSIX, with no
     * macOS layer (the Apple zone APIs are not on Bionic).
     */
    *pPlatform = *Th8_GetMimallocPlatform();
#  if defined(TH8_PLATFORM_IOS)
    if (Th8_MergePlatform(pPlatform, Th8_GetIosPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_MergePlatform(pPlatform, Th8_GetMacOSPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  elif defined(__APPLE__)
    if (Th8_MergePlatform(pPlatform, Th8_GetMacOSPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  elif defined(TH8_PLATFORM_ANDROID)
    if (Th8_MergePlatform(pPlatform, Th8_GetAndroidPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  elif !defined(_WIN32) && !defined(WIN32)
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  else
    if (Th8_MergePlatform(pPlatform, Th8_GetWin32Platform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  endif

#else /* !mimalloc, !cosmopolitan */
    /*
     * iOS / Android base layers go first so their xPanic /
     * xEmitTrace / xRandomBytes overrides win over the lower
     * macOS/POSIX layers.  See the per-platform getter doc-comments
     * in th8.h for the rationale on the merge order.
     */
#  if defined(TH8_PLATFORM_IOS)
    *pPlatform = *Th8_GetIosPlatform();
    if (Th8_MergePlatform(pPlatform, Th8_GetMacOSPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  elif defined(__APPLE__)
    *pPlatform = *Th8_GetMacOSPlatform();
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  elif defined(TH8_PLATFORM_ANDROID)
    *pPlatform = *Th8_GetAndroidPlatform();
    if (Th8_MergePlatform(pPlatform, Th8_GetPosixPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }
#  elif !defined(_WIN32) && !defined(WIN32)
    *pPlatform = *Th8_GetPosixPlatform();
#  else
    *pPlatform = *Th8_GetWin32Platform();
#  endif
#endif /* TH8_USE_MIMALLOC */

    /*
     * Compiler-runtime layer, merged AFTER the OS layers but BEFORE libc
     * (most-specific to least-specific).  It supplies xStackBackTrace via
     * _Unwind_Backtrace: a native OS stack walk (e.g. Win32
     * RtlCaptureStackBackTrace, merged with the OS layers above) already
     * won the slot where one exists; otherwise this fills it.  It is more
     * specific than libc (it provides a capability ANSI C cannot) but less
     * specific than the OS, so it sits just above libc, which remains the
     * final least-specific base.
     */
    if (Th8_MergePlatform(pPlatform, th8GetUnwindPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }

    if (Th8_MergePlatform(pPlatform, Th8_GetLibcPlatform()) != TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * NOTE: Th8_GetEnvPlatform() is NOT merged here.
     * Environment variable access is a host-system capability
     * that must be explicitly opted into by the embedder or
     * shell, not provided by default.
     */

    return TH8_OK;
}
