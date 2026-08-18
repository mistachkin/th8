/*
 * th8_bigint.c -- Arbitrary precision integer support for TH8.
 *
 * Bridges libtommath to the TH8 expression evaluator.  All values
 * flow through the interpreter result as decimal strings, so bigint
 * support is transparent to the script layer.
 *
 * Compile-time gate: TH8_ENABLE_BIGINT
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"

#if defined(TH8_ENABLE_BIGINT)

#  include "th8_bigint.h"


/*
 *----------------------------------------------------------------------
 *
 * Bigint memory bridge --
 *
 *	Routes libtommath allocations through the TH8 platform
 *	allocator, exactly like the Spencer regex bridge.
 *
 *	A global interpreter pointer (th8_bigint_interp) is set
 *	before each bigint operation and cleared afterward.  When
 *	the interpreter is cancelled or exceeds resource limits,
 *	the malloc bridge returns NULL, causing libtommath to
 *	abort gracefully via its MP_MEM error path.
 *
 *----------------------------------------------------------------------
 */

/* TH8_THREAD_LOCAL is defined in th8_int.h. */

static TH8_THREAD_LOCAL Th8_Interp *volatile th8_bigint_interp = 0;


/*
 *----------------------------------------------------------------------
 *
 * th8_bigint_malloc --
 *
 *	libtommath `XMALLOC` allocator bridge.  Routes a raw
 *	`n`-byte request through TH8's `TH8_ALLOC` using the
 *	thread-local `th8_bigint_interp` captured by the
 *	bracketing `th8BigintSetup` call.
 *
 * Why / How:
 *	libtommath is compiled with LTM_ALLOC_FUNCS pointing at
 *	this family of callbacks, which take no user-data pointer;
 *	the interp is therefore threaded in via the thread-local.
 *	A NULL `th8_bigint_interp` (bridge not set up) or a
 *	non-ready interpreter (cancelled / over its memory limit)
 *	makes this return NULL, which libtommath maps to MP_MEM
 *	and unwinds gracefully.  Plain guards, NOT `NEVER()`, per
 *	the Bug 26 family rule.
 *
 * Results:
 *	Pointer to `n` bytes on success; NULL on any failure.
 *
 * Side effects:
 *	Allocates via the per-interp allocator (which counts the
 *	request against the interpreter's memory limit).
 *
 *----------------------------------------------------------------------
 */

void *
th8_bigint_malloc(size_t n)
{
    /* Bug 26 family: plain guard, not NEVER().  libtommath can be
     * driven (e.g. the Th8_ToDouble exact-comparison fallback via
     * th8BignumDecideRound) without th8BigintSetup having run, so
     * th8_bigint_interp may legitimately be NULL here.  NEVER()
     * asserts (abort) under TH8_DEBUG and collapses to a NULL
     * deref under TH8_OMIT_AUXILIARY_SAFETY_CHECKS; a plain return
     * degrades gracefully to MP_MEM in all build configurations. */
    if (!th8_bigint_interp) return NULL;

    /*
     * Check for cancellation.  Th8_AttemptMalloc already enforces
     * the memory limit via nAllocLimit, so allocations are
     * counted against the interpreter's configured limit.
     * Cancellation makes us return NULL, which libtommath
     * handles as MP_MEM.
     */

    if (Th8_Ready(th8_bigint_interp) != TH8_OK) {
	return NULL;
    }
    return TH8_ALLOC(th8_bigint_interp, n);
}

/*
 *----------------------------------------------------------------------
 *
 * th8_bigint_calloc --
 *
 *	libtommath `XCALLOC` allocator bridge.  Routes through
 *	TH8's `TH8_ALLOC_MUL` (which performs the
 *	`nmemb * size` overflow check internally) using the
 *	thread-local `th8_bigint_interp` captured by the
 *	bracketing `th8BigintSetup` call.
 *
 *	The pre-call overflow check at line 107 is the same
 *	defensive belt-and-suspenders pattern the rest of the
 *	bigint allocator family uses:
 *	  *  NULL `th8_bigint_interp` -- bridge not set up;
 *	     libtommath sees `MP_MEM`.  Plain guard, NOT
 *	     `NEVER()`, per the Bug 26 family rule (collapsing
 *	     `NEVER(!ptr)` under `TH8_OMIT` would crash here).
 *	  *  Interpreter not ready (cancelled / frozen) --
 *	     libtommath sees `MP_MEM`.
 *	  *  `nmemb * size` overflow -- libtommath sees
 *	     `MP_MEM`.
 *
 * Why / How:
 *	libtommath's XCALLOC hook takes no user-data pointer, so the
 *	interp is threaded in through the thread-local
 *	th8_bigint_interp set by th8BigintSetup.  The explicit
 *	nmemb/size overflow test is redundant with TH8_ALLOC_MUL's
 *	internal check but kept as a belt-and-suspenders guard;
 *	together with the NULL-interp and not-ready guards every
 *	failure funnels to a NULL return that libtommath reads as
 *	MP_MEM.  Plain guards, not NEVER(), per the Bug 26 family.
 *
 * Parameters:
 *	nmemb -- element count.
 *	size  -- bytes per element.
 *
 * Results:
 *	Zeroed allocation of `nmemb * size` bytes on success;
 *	NULL on any failure (libtommath maps to `MP_MEM`).
 *
 * Side effects:
 *	Allocates via the per-interp allocator.
 *
 *----------------------------------------------------------------------
 */
void *
th8_bigint_calloc(size_t nmemb, size_t size)
{
    if (!th8_bigint_interp) return NULL;
    if (Th8_Ready(th8_bigint_interp) != TH8_OK) {
	return NULL;
    }
    if (nmemb != 0 && size > (size_t)-1 / nmemb) return NULL;
    return TH8_ALLOC_MUL(th8_bigint_interp, nmemb, size);
}

/*
 *----------------------------------------------------------------------
 *
 * th8_bigint_realloc --
 *
 *	libtommath `XREALLOC` allocator bridge.  Routes
 *	through TH8's `TH8_ATTEMPT_REALLOC` so allocation
 *	failure returns NULL without aborting; libtommath's
 *	`mp_grow` keeps the original buffer (`a->dp`) valid
 *	on failure and `mp_clear` frees it later, matching
 *	the ANSI C `realloc` contract.
 *
 *	The `oldsize` parameter is part of libtommath's
 *	allocator signature but unused by TH8 (the per-interp
 *	allocator tracks size internally).
 *
 *	NULL `th8_bigint_interp` and Th8_Ready failures
 *	return NULL the same way as `th8_bigint_malloc` /
 *	`th8_bigint_calloc`; same Bug 26 reasoning applies.
 *
 * Why / How:
 *	Bridges libtommath's XREALLOC to TH8_ATTEMPT_REALLOC so a
 *	failed grow returns NULL without aborting: libtommath's
 *	mp_grow keeps the original a->dp valid on failure and
 *	mp_clear frees it later, matching the ANSI C realloc
 *	contract.  oldsize is part of the hook signature but unused
 *	(the per-interp allocator tracks sizes itself).  NULL-interp
 *	and not-ready guards return NULL the same Bug 26 way as the
 *	sibling allocators.
 *
 * Parameters:
 *	mem     -- existing buffer (may be NULL on first call).
 *	oldsize -- previous buffer size (unused).
 *	newsize -- requested new size.
 *
 * Results:
 *	The (possibly relocated) buffer on success;
 *	NULL on failure (caller retains ownership of `mem`).
 *
 * Side effects:
 *	May allocate / relocate via the per-interp allocator.
 *
 *----------------------------------------------------------------------
 */
void *
th8_bigint_realloc(void *mem, size_t oldsize, size_t newsize)
{
    (void)oldsize;
    if (!th8_bigint_interp) return NULL;

    /*
     * Check readiness before realloc.  On failure, return NULL
     * WITHOUT freeing the original block -- libtommath's mp_grow
     * keeps the original a->dp valid and mp_clear frees it later.
     * This matches the ANSI C realloc contract: on failure, the
     * original block is untouched.
     */

    if (Th8_Ready(th8_bigint_interp) != TH8_OK) {
	return NULL;
    }
    return TH8_ATTEMPT_REALLOC(th8_bigint_interp, mem, newsize);
}

/*
 *----------------------------------------------------------------------
 *
 * th8_bigint_free --
 *
 *	libtommath `XFREE` allocator bridge.  Routes through
 *	TH8's `Th8_Free` using the thread-local interp.  NULL
 *	`th8_bigint_interp` is a no-op (the buffer is leaked
 *	rather than crashing -- a known late-shutdown
 *	mode after `th8BigintTeardown`).  Plain guard, NOT
 *	`NEVER()`, per Bug 26.
 *
 *	The `size` parameter is part of libtommath's
 *	allocator signature but unused by TH8.
 *
 * Why / How:
 *	Bridges libtommath's XFREE to Th8_Free through the
 *	thread-local interp.  size is part of the hook signature but
 *	unused.  A NULL th8_bigint_interp makes this a no-op that
 *	leaks the buffer rather than crashing -- the known
 *	late-shutdown mode after th8BigintTeardown -- again a plain
 *	guard, not NEVER(), per Bug 26.
 *
 * Parameters:
 *	mem  -- buffer to free, or NULL.
 *	size -- buffer size (unused).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees via the per-interp allocator (or no-op when
 *	the bridge is unset).
 *
 *----------------------------------------------------------------------
 */
void
th8_bigint_free(void *mem, size_t size)
{
    (void)size;
    if (!th8_bigint_interp) return;
    Th8_Free(th8_bigint_interp, mem);
}

/*
 *----------------------------------------------------------------------
 *
 * th8BigintSetup --
 *
 *	Acquire the global bigint mutex (when one is needed
 *	by the build) and stamp `interp` into the
 *	thread-local `th8_bigint_interp` pointer used by the
 *	libtommath allocator bridge.  Every bigint operation
 *	must be bracketed by `th8BigintSetup` /
 *	`th8BigintTeardown` so the allocator callbacks have
 *	an interp to route allocation requests through
 *	(libtommath's allocator hooks take no user-data
 *	parameter, hence the thread-local convention).
 *
 * Why / How:
 *	libtommath's allocator hooks take no user-data argument, so
 *	every bigint operation must publish its interp somewhere the
 *	bridge callbacks can find it: this stamps it into the
 *	thread-local th8_bigint_interp.  On builds without native TLS
 *	it also takes the global bigint mutex first, so setup/teardown
 *	bracket every operation as a critical section.
 *
 * Parameters:
 *	interp -- interpreter to install.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquires the global bigint mutex on platforms without
 *	native TLS; sets `th8_bigint_interp`.
 *
 *----------------------------------------------------------------------
 */
void
th8BigintSetup(Th8_Interp *interp)
{
    th8MaybeGlobalMutexEnter(interp);
    th8_bigint_interp = interp;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BigintTeardown --
 *
 *	Inverse of `th8BigintSetup`: clear the thread-local
 *	`th8_bigint_interp` pointer and release the global
 *	bigint mutex.  Every bigint operation must reach this
 *	helper on its way out, even on error paths, so the
 *	mutex never deadlocks subsequent callers.
 *
 * Why / How:
 *	The inverse of th8BigintSetup: clears the thread-local interp
 *	pointer and releases the global bigint mutex.  Every bigint
 *	operation must reach it on the way out -- including error
 *	paths -- or a later caller would deadlock on the still-held
 *	mutex.
 *
 * Parameters:
 *	(none)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases the global bigint mutex (paired with
 *	`th8BigintSetup`).
 *
 *----------------------------------------------------------------------
 */
void
th8BigintTeardown(void)
{
    th8_bigint_interp = 0;
    th8MaybeGlobalMutexLeave(NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Bigint --
 *
 *	Opaque wrapper around libtommath's mp_int.  Stored in the
 *	internal-representation cache so that repeated bigint
 *	operations on the same string avoid re-parsing.
 *
 *----------------------------------------------------------------------
 */

struct Th8_Bigint {
    mp_int value;
};


/*
 * th8BigintCacheGet --
 *
 *	Look up a cached mp_int for the given string.  Returns the
 *	mp_int pointer (cache-owned, read-only borrow) or NULL on miss.
 *
 * Why / How:
 *	th8IsBigint memoises parsed values in the interpreter's
 *	TH8_CACHE_BIGINT internal-rep cache keyed by (z, n); this is
 *	the read side, letting the arithmetic/compare paths reuse an
 *	already-parsed mp_int instead of re-scanning the string.  A
 *	hit requires the slot to exist, be marked iValid, and carry a
 *	non-NULL pBigint (the "is a bigint" memo, versus the
 *	iValid+NULL "not a bigint" memo).  The NULL-interp guard and
 *	nested pCached test are plain guards (Bug 26/28 family), not
 *	NEVER/ALWAYS, so an OOM miss or omitted safety check degrades
 *	to a cache miss.
 *
 * Results:
 *	A borrowed, cache-owned mp_int pointer on a hit; NULL on a
 *	miss, a NULL interp, or an OOM cache lookup.  The caller must
 *	not free or retain it past the cache entry's lifetime.
 *
 * Side effects:
 *	No value is written by this function, but the underlying
 *	Th8_FindInCache probe may create or evict a slot in the
 *	interpreter's bigint cache.
 */

static mp_int *
th8BigintCacheGet(Th8_Interp *interp, const char *z, size_t n)
{
    Th8_Value *pCached;

    /* Bug 26 family: plain guard.  Static helper called
     * indirectly via the public Th8_IsBigint path; under
     * TH8_OMIT_AUXILIARY_SAFETY_CHECKS the NEVER collapses
     * and the Th8_FindInCache call below would deref a
     * NULL interp.  Plain check returns the miss sentinel. */
    if (!interp) return 0;
    pCached = Th8_FindInCache(interp, TH8_CACHE_BIGINT, z, n);
    /* Bug 28 family: plain conditional instead of ALWAYS.
     * Th8_FindInCache returns NULL under OOM.  Nested per
     * Finding 005 to keep the C1-Pair (pCached==NULL, OOM-
     * class) out of the MC/DC denominator; the prior form
     * was a 3-condition compound that also tripped clang's
     * MC/DC truth-table cap. */
    if (pCached) {
	if (pCached->u.bigint.iValid) {
	    if (pCached->u.bigint.pBigint) {
		return &pCached->u.bigint.pBigint->value;
	    }
	}
    }
    return 0;
}


/*
 * th8BigintCacheStore --
 *
 *	Store a parsed mp_int in the cache.  The cache takes ownership
 *	of a COPY of the mp_int.  Also sets iValid so th8IsBigint
 *	cache hits work.
 *
 * Why / How:
 *	The write side paired with th8BigintCacheGet: th8IsBigint
 *	calls it once it has confirmed a string is a bigint, so later
 *	arithmetic can reuse the value.  It finds (or creates) the
 *	(z, n) cache slot and, only when the slot is fresh (defensive
 *	ALWAYS guards on !iValid / !pBigint), allocates a Th8_Bigint
 *	and stores an mp_init_copy of pSrc -- the cache owns its own
 *	copy, not the caller's mp_int.  The copy is bracketed by
 *	th8BigintSetup/Teardown so libtommath allocates through the
 *	interp.  NULL interp/pSrc are plain guards (Bug 26 family).
 *
 * Results:
 *	None (void).  On OOM at any step it simply leaves the entry
 *	unpopulated.
 *
 * Side effects:
 *	May allocate a Th8_Bigint and an mp_int copy owned by the
 *	interpreter's bigint cache and populate the (z, n) slot
 *	(possibly creating/evicting it via Th8_FindInCache); briefly
 *	takes the bigint mutex.
 */

TH8_INTERNAL void
th8BigintCacheStore(
    Th8_Interp *interp,
    const char *z,
    size_t n,
    const void *pSrc)
{
    Th8_Value *pCached;

    /* Bug 26 family: plain guards.  See th8BigintCacheGet
     * for the rationale; pSrc gets the same treatment
     * because mp_copy() below would deref a NULL source. */
    /* Split per Finding 005. */
    if (!interp) return;
    if (!pSrc) return;

    pCached = Th8_FindInCache(interp, TH8_CACHE_BIGINT, z, n);
    if (!pCached) return;

    /* Th8_FindInCache returns a slot whose key-content was just
     * verified to match (z, n).  CacheStore is only called from
     * th8IsBigint *after* the L757 fast-return short-circuits
     * the iValid==1 path, so by the time we get here either the
     * slot is fresh (iValid==0) or it had iValid==1 with
     * pBigint==NULL (a "not a bigint" memo) -- but in that case
     * th8IsBigint already returned 0 at L758 without calling
     * CacheStore.  Therefore !iValid is ALWAYS T at this point;
     * !pBigint is also ALWAYS T (fresh slot has pBigint==NULL).
     * Both sub-conditions are defensive belt-and-braces tests. */
    if (ALWAYS(!pCached->u.bigint.iValid) ||
        ALWAYS(!pCached->u.bigint.pBigint)) {
	Th8_Bigint *pNew;

	pNew = (Th8_Bigint *)TH8_ALLOC(interp, sizeof(Th8_Bigint));
	if (!pNew) return;

	th8BigintSetup(interp);
	if (mp_init_copy(&pNew->value, (const mp_int *)pSrc) != MP_OKAY) {
	    th8BigintTeardown();
	    Th8_Free(interp, pNew);
	    return;
	}
	th8BigintTeardown();

	pCached->u.bigint.pBigint = pNew;
	pCached->u.bigint.iValid = 1;
    }
}


/*
 * th8BigintDestroy --
 *
 *	Free a Th8_Bigint and its internal mp_int.  Called from
 *	th8CacheEntryFree (in th8_cache.c) when a bigint cache
 *	entry is evicted.
 *
 * Why / How:
 *	The destructor for a cached Th8_Bigint.  It brackets mp_clear
 *	with th8BigintSetup/Teardown so libtommath frees the mp_int
 *	digits through the interp allocator, then frees the wrapper
 *	itself.  A NULL pBigint is a no-op.
 *
 * Results:
 *	None (void).
 *
 * Side effects:
 *	Frees the mp_int's digits and the Th8_Bigint wrapper via the
 *	per-interp allocator; briefly takes the bigint mutex.
 */

void
th8BigintDestroy(Th8_Interp *interp, Th8_Bigint *pBigint)
{
    if (!pBigint) return;
    th8BigintSetup(interp);
    mp_clear(&pBigint->value);
    th8BigintTeardown();
    Th8_Free(interp, pBigint);
}


/*
 * Expression operator constants.  Must match the definitions
 * in th8_core.c.
 */

#  define TH8_OP_UNARY_MINUS 2
#  define TH8_OP_UNARY_PLUS  3
#  define TH8_OP_BITWISE_NOT 4
#  define TH8_OP_LOGICAL_NOT 5
#  define TH8_OP_MULTIPLY    6
#  define TH8_OP_DIVIDE      7
#  define TH8_OP_MODULUS     8
#  define TH8_OP_ADD         9
#  define TH8_OP_SUBTRACT    10
#  define TH8_OP_LEFT_SHIFT  11
#  define TH8_OP_RIGHT_SHIFT 12
#  define TH8_OP_LT          13
#  define TH8_OP_GT          14
#  define TH8_OP_LE          15
#  define TH8_OP_GE          16
#  define TH8_OP_EQ          17
#  define TH8_OP_NE          18
#  define TH8_OP_BITWISE_AND 21
#  define TH8_OP_BITWISE_XOR 22
#  define TH8_OP_BITWISE_OR  24
#  define TH8_OP_EXPONENT    29


/*
 *----------------------------------------------------------------------
 *
 * th8BigintFromStr --
 *
 *	Parse a string as an mp_int.  Handles decimal, hex (0x),
 *	octal (0o), and binary (0b) prefixes.
 *
 * Why / How:
 *	The one string->mp_int parser shared by every bigint entry
 *	point.  It hand-scans leading whitespace and an optional sign,
 *	recognises 0x/0o/0b radix prefixes (defaulting to decimal),
 *	copies the remaining digits into a fixed 512-byte stack buffer
 *	so it can NUL-terminate for libtommath's mp_read_radix, then
 *	applies mp_neg for a negative sign.  Oversized inputs are
 *	truncated to the buffer, and the copy runs through the
 *	thread-local interp's Th8_Memcpy.
 *
 * Results:
 *	MP_OKAY on success; the mp_err from mp_read_radix or mp_neg on
 *	failure.
 *
 * Side effects:
 *	Writes the parsed value into *a (which the caller must have
 *	mp_init'd), growing it through libtommath's interp-routed
 *	allocator; uses a 512-byte stack buffer.
 *
 *----------------------------------------------------------------------
 */

static mp_err
th8BigintFromStr(mp_int *a, const char *z, size_t n)
{
    int radix = 10;
    char buf[512];
    const char *p = z;
    size_t len = n;
    int neg = 0;

    /* Skip leading whitespace. */
    while (len > 0 && (*p == ' ' || *p == '\t')) {
	p++;
	len--;
    }

    /* Sign. */
    if (len > 0 && *p == '-') {
	neg = 1;
	p++;
	len--;
    } else if (len > 0 && *p == '+') {
	p++;
	len--;
    }

    /* Radix prefix. */
    if (len > 2 && p[0] == '0') {
	if (p[1] == 'x' || p[1] == 'X') {
	    radix = 16;
	    p += 2;
	    len -= 2;
	} else if (p[1] == 'o' || p[1] == 'O') {
	    radix = 8;
	    p += 2;
	    len -= 2;
	} else if (p[1] == 'b' || p[1] == 'B') {
	    radix = 2;
	    p += 2;
	    len -= 2;
	}
    }

    /* NUL-terminate for mp_read_radix. */
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    Th8_Memcpy(th8_bigint_interp, buf, p, len);
    buf[len] = 0;

    {
	mp_err err = mp_read_radix(a, buf, radix);

	if (err != MP_OKAY) return err;
    }

    if (neg) {
	mp_err err = mp_neg(a, a);

	if (err != MP_OKAY) return err;
    }
    return MP_OKAY;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintToResult --
 *
 *	Convert an mp_int to a decimal string and set it as the
 *	interpreter result.
 *
 * Why / How:
 *	The shared render step: every arithmetic/scan path funnels
 *	its result mp_int through here.  It sizes a buffer from
 *	mp_count_bits/3 plus slop, renders base-10 with mp_to_radix,
 *	and publishes it through Th8_SetResult (which copies), then
 *	frees the scratch buffer.  Assumes the bracketing
 *	th8BigintSetup is already active so the allocation routes
 *	through the interp.
 *
 * Results:
 *	TH8_OK with the interpreter result set to the decimal string;
 *	TH8_ERROR (result set to an error message) on allocation
 *	failure or an mp_to_radix conversion error.
 *
 * Side effects:
 *	Allocates and frees a scratch buffer via the interp allocator
 *	and sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8BigintToResult(Th8_Interp *interp, const mp_int *a)
{
    int size;
    char *buf;
    mp_err err;

    size = mp_count_bits(a) / 3 + 3; /* rough decimal digits + sign + NUL */
    buf = (char *)TH8_ALLOC(interp, (size_t)size);
    if (!buf) return TH8_ERROR;

    {
	size_t written = 0;

	err = mp_to_radix(a, buf, (size_t)size, &written, 10);
    }
    if (err != MP_OKAY) {
	Th8_Free(interp, buf);
	Th8_SetResultStatic(interp, "bigint conversion error", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_SetResult(interp, buf, Th8_Strlen(interp, buf));
    Th8_Free(interp, buf);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ScanBignum --
 *
 *	Parse a (possibly signed and radix-prefixed) integer string as
 *	an arbitrary-precision value and set the interpreter result to
 *	its decimal representation.
 *
 * Why / How:
 *	The `scan` command's `ll` size modifier stores a value with no
 *	width limit (BigInt).  scan stages every converted value through
 *	the interpreter result and then reads it back, so this helper
 *	fits that model: it brackets the libtommath work with
 *	th8BigintSetup/Teardown, parses via th8BigintFromStr (which
 *	understands `0x`/`0o`/`0b` prefixes and a leading sign), and
 *	renders decimal via th8BigintToResult.
 *
 * Results:
 *	TH8_OK with the interpreter result set to the decimal string;
 *	TH8_ERROR (with an error result) if the value cannot be parsed.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
th8ScanBignum(Th8_Interp *interp, const char *zStr, size_t n)
{
    mp_int a;
    mp_err err;
    int rc;

    th8BigintSetup(interp);
    if (mp_init(&a) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint init failed", TH8_NOLEN);
	return TH8_ERROR;
    }
    err = th8BigintFromStr(&a, zStr, n);
    if (err != MP_OKAY) {
	mp_clear(&a);
	th8BigintTeardown();
	Th8_SetResultStatic(
	    interp, "scan: invalid arbitrary-precision integer", TH8_NOLEN);
	return TH8_ERROR;
    }
    rc = th8BigintToResult(interp, &a);
    mp_clear(&a);
    th8BigintTeardown();
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintArith --
 *
 *	Perform a bigint binary arithmetic operation.
 *
 * Why / How:
 *	The evaluator dispatches every bigint binary operator here.
 *	It mp_init_multi's three temporaries, loads each operand
 *	(reusing a cached mp_int from th8BigintCacheGet when present,
 *	else parsing via th8BigintFromStr, guarded by defensive
 *	ALWAYS since binary ops always have two operands), then a
 *	switch on eOp calls the matching libtommath primitive.
 *	Arithmetic/bitwise ops render their result with
 *	th8BigintToResult; the comparison ops (LT..NE) instead set a
 *	0/1 integer result from mp_cmp.  Divide/modulus by zero and
 *	negative shift/exponent counts are caught explicitly.  The
 *	whole body is bracketed by th8BigintSetup/Teardown and a
 *	single mp_clear_multi cleanup.
 *
 * Results:
 *	TH8_OK with the interpreter result set to the decimal result
 *	string (or "0"/"1" for a comparison); TH8_ERROR with an error
 *	message for a bad operand, divide-by-zero, negative
 *	shift/exponent, unsupported op, out-of-memory, or arithmetic
 *	error.
 *
 * Side effects:
 *	Sets the interpreter result; allocates and frees libtommath
 *	temporaries via the per-interp allocator; takes and releases
 *	the bigint mutex.
 *
 *----------------------------------------------------------------------
 */

int
th8BigintArith(
    Th8_Interp *interp,
    const char *zLeft,
    size_t nLeft,
    const char *zRight,
    size_t nRight,
    int eOp)
{
    mp_int a, b, c;
    mp_err err;
    int rc = TH8_OK;

    th8BigintSetup(interp);

    if (mp_init_multi(&a, &b, &c, NULL) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* th8BigintArith is called for binary ops only (unary ops
     * dispatch to th8BigintUnary); zLeft is always non-NULL at
     * entry, and the operand string is the literal text of a
     * recognized bigint token (length > 0).  Single-ALWAYS over
     * the compound so the MC/DC region collapses to (1) cleanly
     * under TH8_OMIT (multi-ALWAYS keeps the && as a decision). */
    if (ALWAYS(zLeft != NULL && nLeft > 0)) {
	mp_int *pCachedL = th8BigintCacheGet(interp, zLeft, nLeft);
	if (pCachedL) {
	    err = mp_copy(pCachedL, &a);
	} else {
	    err = th8BigintFromStr(&a, zLeft, nLeft);
	}
	if (err != MP_OKAY) goto bad_operand;
    }
    /* Same binary-op-only invariant as zLeft above: zRight is
     * always non-NULL with positive length when th8BigintArith
     * is entered. */
    if (ALWAYS(zRight != NULL && nRight > 0)) {
	mp_int *pCachedR = th8BigintCacheGet(interp, zRight, nRight);
	if (pCachedR) {
	    err = mp_copy(pCachedR, &b);
	} else {
	    err = th8BigintFromStr(&b, zRight, nRight);
	}
	if (err != MP_OKAY) goto bad_operand;
    }

    switch (eOp) {
    case TH8_OP_ADD:
	err = mp_add(&a, &b, &c);
	break;
    case TH8_OP_SUBTRACT:
	err = mp_sub(&a, &b, &c);
	break;
    case TH8_OP_MULTIPLY:
	err = mp_mul(&a, &b, &c);
	break;
    case TH8_OP_DIVIDE:
	if (mp_iszero(&b)) {
	    Th8_SetResultStatic(interp, "divide by zero", TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto done;
	}
	err = mp_div(&a, &b, &c, NULL);
	break;
    case TH8_OP_MODULUS:
	if (mp_iszero(&b)) {
	    Th8_SetResultStatic(interp, "divide by zero", TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto done;
	}
	err = mp_mod(&a, &b, &c);
	break;
    case TH8_OP_LEFT_SHIFT: {
	th8_int64_t shift;

	if (Th8_ToWideInt(interp, zRight, nRight, &shift) != TH8_OK ||
	    shift < 0) {
	    Th8_SetResultStatic(interp, "negative shift count", TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto done;
	}
	err = mp_mul_2d(&a, (int)shift, &c);
	break;
    }
    case TH8_OP_RIGHT_SHIFT: {
	th8_int64_t shift;

	if (Th8_ToWideInt(interp, zRight, nRight, &shift) != TH8_OK ||
	    shift < 0) {
	    Th8_SetResultStatic(interp, "negative shift count", TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto done;
	}
	err = mp_div_2d(&a, (int)shift, &c, NULL);
	break;
    }
    case TH8_OP_BITWISE_AND:
	err = mp_and(&a, &b, &c);
	break;
    case TH8_OP_BITWISE_OR:
	err = mp_or(&a, &b, &c);
	break;
    case TH8_OP_BITWISE_XOR:
	err = mp_xor(&a, &b, &c);
	break;
    case TH8_OP_EXPONENT: {
	th8_int64_t exp;

	if (Th8_ToWideInt(interp, zRight, nRight, &exp) != TH8_OK ||
	    exp < 0) {
	    Th8_SetResultStatic(interp, "negative exponent", TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto done;
	}
	err = mp_expt_n(&a, (int)exp, &c);
	break;
    }
    case TH8_OP_LT:
    case TH8_OP_GT:
    case TH8_OP_LE:
    case TH8_OP_GE:
    case TH8_OP_EQ:
    case TH8_OP_NE: {
	mp_ord ord = mp_cmp(&a, &b);
	int bResult = 0;

	switch (eOp) {
	case TH8_OP_LT:
	    bResult = (ord == MP_LT);
	    break;
	case TH8_OP_GT:
	    bResult = (ord == MP_GT);
	    break;
	case TH8_OP_LE:
	    bResult = (ord != MP_GT);
	    break;
	case TH8_OP_GE:
	    bResult = (ord != MP_LT);
	    break;
	case TH8_OP_EQ:
	    bResult = (ord == MP_EQ);
	    break;
	case TH8_OP_NE:
	    bResult = (ord != MP_EQ);
	    break;
	}
	Th8_SetResultInt(interp, bResult);
	err = MP_OKAY;
	goto done;
    }
    default:
	Th8_SetResultStatic(
	    interp, "unsupported bigint operation", TH8_NOLEN);
	rc = TH8_ERROR;
	goto done;
    }

    if (err != MP_OKAY) {
	Th8_SetResultStatic(interp, "bigint arithmetic error", TH8_NOLEN);
	rc = TH8_ERROR;
    } else {
	rc = th8BigintToResult(interp, &c);
    }
    goto done;

bad_operand:
    Th8_SetResultStatic(
        interp, "expected integer but got non-integer operand", TH8_NOLEN);
    rc = TH8_ERROR;

done:
    mp_clear_multi(&a, &b, &c, NULL);
    th8BigintTeardown();
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintUnary --
 *
 *	Perform a unary bigint operation.
 *
 * Why / How:
 *	The unary counterpart to th8BigintArith.  It loads the single
 *	operand (cache hit or th8BigintFromStr), switches on eOp for
 *	unary minus, unary plus, bitwise-not (mp_complement), and
 *	logical-not (1 iff the value is zero), then renders the
 *	result with th8BigintToResult.  Bracketed by
 *	th8BigintSetup/Teardown with an mp_clear_multi cleanup.
 *
 * Results:
 *	TH8_OK with the interpreter result set to the decimal result;
 *	TH8_ERROR with an error message for a non-integer operand, an
 *	unsupported op, out-of-memory, or a libtommath error.
 *
 * Side effects:
 *	Sets the interpreter result; allocates and frees libtommath
 *	temporaries via the per-interp allocator; takes and releases
 *	the bigint mutex.
 *
 *----------------------------------------------------------------------
 */

int
th8BigintUnary(Th8_Interp *interp, const char *zVal, size_t nVal, int eOp)
{
    mp_int a, c;
    mp_err err;
    int rc = TH8_OK;

    th8BigintSetup(interp);

    if (mp_init_multi(&a, &c, NULL) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    {
	mp_int *pCachedV = th8BigintCacheGet(interp, zVal, nVal);
	if (pCachedV) {
	    err = mp_copy(pCachedV, &a);
	} else {
	    err = th8BigintFromStr(&a, zVal, nVal);
	}
    }
    if (err != MP_OKAY) {
	Th8_SetResultStatic(interp, "expected integer", TH8_NOLEN);
	rc = TH8_ERROR;
	goto done;
    }

    switch (eOp) {
    case TH8_OP_UNARY_MINUS:
	err = mp_neg(&a, &c);
	break;
    case TH8_OP_UNARY_PLUS:
	err = mp_copy(&a, &c);
	break;
    case TH8_OP_BITWISE_NOT:
	err = mp_complement(&a, &c);
	break;
    case TH8_OP_LOGICAL_NOT:
	mp_set(&c, mp_iszero(&a) ? 1 : 0);
	err = MP_OKAY;
	break;
    default:
	Th8_SetResultStatic(
	    interp, "unsupported bigint unary operation", TH8_NOLEN);
	rc = TH8_ERROR;
	goto done;
    }

    if (err != MP_OKAY) {
	Th8_SetResultStatic(interp, "bigint unary error", TH8_NOLEN);
	rc = TH8_ERROR;
    } else {
	rc = th8BigintToResult(interp, &c);
    }

done:
    mp_clear_multi(&a, &c, NULL);
    th8BigintTeardown();
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintCompare --
 *
 *	Compare two bigint string values.
 *
 * Why / How:
 *	Provides a three-way comparison for callers that need an
 *	ordering rather than a rendered result.  It loads both
 *	operands (cache hit or th8BigintFromStr), calls mp_cmp, and
 *	maps MP_LT/MP_GT/MP_EQ to -1/1/0 in *pCmp.  Unlike the
 *	arith/unary helpers it does NOT set the interpreter result on
 *	success -- only on error.  Bracketed by
 *	th8BigintSetup/Teardown.
 *
 * Results:
 *	TH8_OK with *pCmp set to -1, 0, or 1; TH8_ERROR (interpreter
 *	result set to an error message) if either operand is not an
 *	integer or on out-of-memory.
 *
 * Side effects:
 *	Writes *pCmp; allocates and frees libtommath temporaries via
 *	the per-interp allocator; sets the interpreter result only on
 *	error; takes and releases the bigint mutex.
 *
 *----------------------------------------------------------------------
 */

int
th8BigintCompare(
    Th8_Interp *interp,
    const char *zLeft,
    size_t nLeft,
    const char *zRight,
    size_t nRight,
    int *pCmp)
{
    mp_int a, b;
    mp_err err;

    th8BigintSetup(interp);

    if (mp_init_multi(&a, &b, NULL) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    {
	mp_int *pCL = th8BigintCacheGet(interp, zLeft, nLeft);

	err = pCL ? mp_copy(pCL, &a) : th8BigintFromStr(&a, zLeft, nLeft);
    }
    if (err != MP_OKAY) goto bad;
    {
	mp_int *pCR = th8BigintCacheGet(interp, zRight, nRight);

	err = pCR ? mp_copy(pCR, &b) : th8BigintFromStr(&b, zRight, nRight);
    }
    if (err != MP_OKAY) goto bad;

    {
	mp_ord ord = mp_cmp(&a, &b);

	*pCmp = (ord == MP_LT) ? -1 : (ord == MP_GT) ? 1 : 0;
    }
    mp_clear_multi(&a, &b, NULL);
    th8BigintTeardown();
    return TH8_OK;

bad:
    mp_clear_multi(&a, &b, NULL);
    th8BigintTeardown();
    Th8_SetResultStatic(interp, "expected integer", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8IsBigint --
 *
 *	Check if a string represents a valid integer that exceeds
 *	th8_int64_t range.
 *
 * Why / How:
 *	The gate the evaluator uses to decide whether a token needs
 *	the bigint path at all: a value is a bigint only if it parses
 *	as an integer AND needs more than 63 bits.  It first consults
 *	the TH8_CACHE_BIGINT memo (iValid+pBigint = yes, iValid+NULL =
 *	no); on a miss it parses with th8BigintFromStr, checks
 *	mp_count_bits > 63, and caches the outcome either way -- a
 *	positive result stashes the parsed mp_int via
 *	th8BigintCacheStore so later arithmetic skips re-parsing, a
 *	negative result records the iValid+NULL "not a bigint" memo.
 *	The plain pCached guards and ALWAYS interp guard are Bug 26/28
 *	family (OOM-tolerant, TH8_OMIT-safe).
 *
 * Results:
 *	1 if the string is a valid integer exceeding 63 bits;
 *	0 otherwise (including non-integer strings and OOM).
 *
 * Side effects:
 *	Populates the interpreter's bigint internal-rep cache (a memo
 *	and, for a true result, a copied mp_int); allocates libtommath
 *	temporaries via the per-interp allocator; takes and releases
 *	the bigint mutex.  Does not set the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
th8IsBigint(Th8_Interp *interp, const char *z, size_t n)
{
    mp_int a;
    mp_err err;
    int result;

    if (n == TH8_NOLEN) n = Th8_Strlen(interp, z);
    n = TH8_LEN(n);

    /*
     * Consult the internal-representation cache.
     *
     *   iValid=1, pBigint!=NULL  ==>  is a bigint (mp_int cached)
     *   iValid=1, pBigint==NULL  ==>  not a bigint
     *   iValid=0                 ==>  not yet checked
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_BIGINT, z, n);
	/* Bug 28 family: plain `if` -- pCached may be NULL on
	 * OOM.  Nested per Finding 005 to keep the C1-Pair
	 * (pCached==NULL) out of MC/DC. */
	if (pCached) {
	    if (pCached->u.bigint.iValid) {
		return (pCached->u.bigint.pBigint != 0);
	    }
	}
    }

    th8BigintSetup(interp);

    if (mp_init(&a) != MP_OKAY) {
	th8BigintTeardown();
	return 0;
    }
    err = th8BigintFromStr(&a, z, n);
    if (err != MP_OKAY) {
	mp_clear(&a);
	th8BigintTeardown();
	result = 0;
	goto cache_result;
    }

    /* Check if it fits in int64. */
    {
	int bits = mp_count_bits(&a);

	if (bits > 63) {
	    result = 1;
	    /*
	     * Cache the actual parsed mp_int so arithmetic
	     * operations can reuse it without re-parsing.
	     */
	    th8BigintTeardown();
	    th8BigintCacheStore(interp, z, n, &a);
	    th8BigintSetup(interp);
	    mp_clear(&a);
	} else {
	    result = 0;
	    mp_clear(&a);
	}
	th8BigintTeardown();
    }

cache_result:
    /*
     * Mark the entry as checked even for non-bigint strings.
     *
     * Reaching cache_result implies the L757 early-return did
     * NOT fire, so pCached->u.bigint.iValid was 0 when we
     * looked it up; the slot is freshly created (or freshly
     * evicted) by Th8_FindInCache.  iValid is ALWAYS 0 here. */
    if (ALWAYS(interp) && !result) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_BIGINT, z, n);
	/* Bug 28 family: plain pCached check (NULL on OOM).
	 * Nested per Finding 005. */
	if (pCached) {
	    if (ALWAYS(!pCached->u.bigint.iValid)) {
		pCached->u.bigint.pBigint = 0;
		pCached->u.bigint.iValid = 1;
	    }
	}
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintToDouble --
 *
 *	Convert a bigint string to double.
 *
 * Why / How:
 *	Backs the exact-to-double coercion used when a bigint operand
 *	meets a floating-point one.  It loads the value (cache hit or
 *	th8BigintFromStr) and calls libtommath's mp_get_double,
 *	writing the (possibly rounded or overflowed-to-inf) result to
 *	*pVal.  Bracketed by th8BigintSetup/Teardown.
 *
 * Results:
 *	TH8_OK with *pVal set to the double approximation; TH8_ERROR
 *	(interpreter result set to an error message) if the string is
 *	not an integer or on out-of-memory.
 *
 * Side effects:
 *	Writes *pVal; allocates and frees a libtommath temporary via
 *	the per-interp allocator; sets the interpreter result only on
 *	error; takes and releases the bigint mutex.
 *
 *----------------------------------------------------------------------
 */

int
th8BigintToDouble(Th8_Interp *interp, const char *z, size_t n, double *pVal)
{
    mp_int a;
    mp_err err;

    th8BigintSetup(interp);

    if (mp_init(&a) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    {
	mp_int *pC = th8BigintCacheGet(interp, z, n);

	err = pC ? mp_copy(pC, &a) : th8BigintFromStr(&a, z, n);
    }
    if (err != MP_OKAY) {
	mp_clear(&a);
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "expected integer", TH8_NOLEN);
	return TH8_ERROR;
    }

    *pVal = mp_get_double(&a);
    mp_clear(&a);
    th8BigintTeardown();
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintToTwosComplement --
 *
 *	Encode an integer value (decimal string) into nBytes of
 *	two's-complement binary.  bBigEndian selects MSB-first
 *	(non-zero) vs LSB-first (zero) layout.
 *
 *	Returns TH8_ERROR with the "integer value too large for
 *	j/J field" message when the value does not fit in the
 *	field's representable range.
 *
 * Why / How:
 *	Implements the binary format j/J field writer.  It parses the
 *	value, builds mod = 2^(nBytes*8) and half = 2^(nBytes*8-1),
 *	range-checks against the signed interval [-half, half), and
 *	for negatives adds mod to form the unsigned two's-complement
 *	pattern.  It emits big-endian bytes right-aligned and
 *	zero-padded into pBuf via mp_to_ubin, then reverses them in
 *	place when little-endian is requested.  Bracketed by
 *	th8BigintSetup/Teardown.
 *
 * Results:
 *	TH8_OK with nBytes written into pBuf; TH8_ERROR (interpreter
 *	result set) for a NULL/zero-width field, a non-integer value,
 *	an out-of-range value ("integer value too large for j/J
 *	field"), or an out-of-memory / conversion error.
 *
 * Side effects:
 *	Writes nBytes into the caller's pBuf; allocates and frees
 *	libtommath temporaries via the per-interp allocator; sets the
 *	interpreter result on error; takes and releases the bigint
 *	mutex.
 *
 *----------------------------------------------------------------------
 */

int
th8BigintToTwosComplement(
    Th8_Interp *interp,
    const char *zVal,
    size_t nVal,
    unsigned char *pBuf,
    size_t nBytes,
    int bBigEndian)
{
    mp_int v, mod, half;
    mp_err err;
    size_t nUbin;
    size_t written = 0;
    int rc = TH8_ERROR;
    int bInitMulti = 0;

    if (!pBuf) {
	Th8_SetResultStatic(
	    interp, "j/J field width must be at least 1 byte", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (nBytes == 0) {
	Th8_SetResultStatic(
	    interp, "j/J field width must be at least 1 byte", TH8_NOLEN);
	return TH8_ERROR;
    }

    th8BigintSetup(interp);

    if (mp_init_multi(&v, &mod, &half, NULL) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    bInitMulti = 1;

    err = th8BigintFromStr(&v, zVal, nVal);
    if (err != MP_OKAY) {
	Th8_SetResultStatic(interp, "expected integer", TH8_NOLEN);
	goto done;
    }

    /* mod = 2^(nBytes * 8); half = 2^(nBytes * 8 - 1). */
    if (mp_2expt(&mod, (int)(nBytes * 8)) != MP_OKAY) {
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	goto done;
    }
    if (mp_2expt(&half, (int)(nBytes * 8 - 1)) != MP_OKAY) {
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	goto done;
    }

    if (mp_isneg(&v)) {
	/* v in range iff v >= -half (i.e. -v <= half). */
	mp_int negV;
	mp_ord cmp;

	if (mp_init(&negV) != MP_OKAY) {
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
	if (mp_neg(&v, &negV) != MP_OKAY) {
	    mp_clear(&negV);
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
	cmp = mp_cmp(&negV, &half);
	mp_clear(&negV);
	if (cmp == MP_GT) {
	    Th8_SetResultStatic(
	        interp, "integer value too large for j/J field", TH8_NOLEN);
	    goto done;
	}
	/* v += mod (two's-complement representation as unsigned). */
	if (mp_add(&v, &mod, &v) != MP_OKAY) {
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
    } else if (mp_cmp(&v, &half) != MP_LT) {
	/* v >= 2^(nBytes*8 - 1): cannot fit as signed. */
	Th8_SetResultStatic(
	    interp, "integer value too large for j/J field", TH8_NOLEN);
	goto done;
    }

    /* v is now unsigned in [0, mod).  Write its big-endian bytes
     * right-aligned into pBuf; zero-pad the leading bytes. */
    nUbin = mp_ubin_size(&v);
    if (nUbin > nBytes) {
	/* Defensive: shouldn't happen given the range checks above. */
	Th8_SetResultStatic(
	    interp, "integer value too large for j/J field", TH8_NOLEN);
	goto done;
    }
    Th8_Memset(interp, pBuf, 0, nBytes);
    if (nUbin > 0) {
	if (mp_to_ubin(&v, &pBuf[nBytes - nUbin], nUbin, &written) !=
	    MP_OKAY) {
	    Th8_SetResultStatic(
	        interp, "bigint: conversion error", TH8_NOLEN);
	    goto done;
	}
    }

    /* If little-endian requested, reverse the byte order in place. */
    if (!bBigEndian) {
	size_t i;

	for (i = 0; i < nBytes / 2; i++) {
	    unsigned char t = pBuf[i];

	    pBuf[i] = pBuf[nBytes - 1 - i];
	    pBuf[nBytes - 1 - i] = t;
	}
    }

    rc = TH8_OK;

done:
    if (bInitMulti) mp_clear_multi(&v, &mod, &half, NULL);
    th8BigintTeardown();
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintFromTwosComplement --
 *
 *	Decode nBytes of two's-complement binary (MSB-first when
 *	bBigEndian, else LSB-first) into a decimal integer string
 *	and set the interp result.
 *
 * Why / How:
 *	The inverse decoder: it reads nBytes of two's-complement
 *	binary (byte-reversing an LSB-first buffer into a scratch copy
 *	so the core always works MSB-first), reconstructs the
 *	magnitude with mp_from_ubin, and if the top sign bit is set
 *	subtracts 2^(nBytes*8) to recover the negative value, then
 *	renders it with th8BigintToResult.  A zero-width field decodes
 *	to "0".  Bracketed by th8BigintSetup/Teardown.
 *
 * Results:
 *	TH8_OK with the interpreter result set to the decoded decimal
 *	integer (or "0" for nBytes == 0); TH8_ERROR (result set) on
 *	out-of-memory or a libtommath conversion error.
 *
 * Side effects:
 *	Sets the interpreter result; may allocate and free a scratch
 *	byte buffer plus libtommath temporaries via the per-interp
 *	allocator; takes and releases the bigint mutex.
 *
 *----------------------------------------------------------------------
 */

int
th8BigintFromTwosComplement(
    Th8_Interp *interp,
    const unsigned char *pBuf,
    size_t nBytes,
    int bBigEndian)
{
    mp_int v, mod;
    int signBit;
    unsigned char *zBytes = NULL;
    const unsigned char *pSrc;
    int rc = TH8_ERROR;
    int bInitMulti = 0;

    if (nBytes == 0) {
	Th8_SetResultStatic(interp, "0", 1);
	return TH8_OK;
    }

    th8BigintSetup(interp);

    if (mp_init_multi(&v, &mod, NULL) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    bInitMulti = 1;

    if (bBigEndian) {
	pSrc = pBuf;
    } else {
	size_t i;

	zBytes = (unsigned char *)TH8_ALLOC(interp, nBytes);
	if (!zBytes) {
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
	for (i = 0; i < nBytes; i++)
	    zBytes[i] = pBuf[nBytes - 1 - i];
	pSrc = zBytes;
    }

    signBit = (pSrc[0] & 0x80) ? 1 : 0;

    if (mp_from_ubin(&v, pSrc, nBytes) != MP_OKAY) {
	Th8_SetResultStatic(interp, "bigint: conversion error", TH8_NOLEN);
	goto done;
    }

    if (signBit) {
	/* Subtract 2^(nBytes*8) to recover the signed value. */
	if (mp_2expt(&mod, (int)(nBytes * 8)) != MP_OKAY) {
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
	if (mp_sub(&v, &mod, &v) != MP_OKAY) {
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
    }

    rc = th8BigintToResult(interp, &v);

done:
    if (zBytes) Th8_Free(interp, zBytes);
    if (bInitMulti) mp_clear_multi(&v, &mod, NULL);
    th8BigintTeardown();
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BigintMinTwosComplementBytes --
 *
 *	Compute the minimum field width (in bytes) needed to
 *	represent the value (decimal string) as two's complement.
 *
 *	Rules:
 *	  v == 0     -> 1 byte
 *	  v > 0      -> ceil((mp_count_bits(v) + 1) / 8)
 *	  v < 0      -> if |v| is an exact power of 2, fits in
 *	               mp_count_bits(|v|) bits (e.g. -128 = 0x80);
 *	               else ceil((mp_count_bits(|v|) + 1) / 8).
 *
 * Why / How:
 *	Lets the binary formatter pick the smallest j/J field that
 *	holds a value.  It parses the value, then works from
 *	mp_count_bits(|v|): zero needs 1 byte; a positive value needs
 *	one extra sign bit (bits+1); a negative value needs bits when
 *	|v| is an exact power of two (detected via mp_cnt_lsb ==
 *	bits-1, e.g. -128 = 0x80) and bits+1 otherwise.  The bit count
 *	is rounded up to whole bytes.  Bracketed by
 *	th8BigintSetup/Teardown.
 *
 * Results:
 *	TH8_OK with *pNeeded set to the minimum byte width; TH8_ERROR
 *	(interpreter result set to an error message) if the string is
 *	not an integer or on out-of-memory.
 *
 * Side effects:
 *	Writes *pNeeded; allocates and frees libtommath temporaries
 *	via the per-interp allocator; sets the interpreter result only
 *	on error; takes and releases the bigint mutex.
 *
 *----------------------------------------------------------------------
 */

int
th8BigintMinTwosComplementBytes(
    Th8_Interp *interp,
    const char *zVal,
    size_t nVal,
    size_t *pNeeded)
{
    mp_int v;
    mp_err err;
    int bits;
    int neededBits;
    int rc = TH8_ERROR;
    int bInit = 0;

    th8BigintSetup(interp);

    if (mp_init(&v) != MP_OKAY) {
	th8BigintTeardown();
	Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    bInit = 1;

    err = th8BigintFromStr(&v, zVal, nVal);
    if (err != MP_OKAY) {
	Th8_SetResultStatic(interp, "expected integer", TH8_NOLEN);
	goto done;
    }

    if (mp_iszero(&v)) {
	*pNeeded = 1;
	rc = TH8_OK;
	goto done;
    }

    bits = mp_count_bits(&v);
    /* mp_count_bits returns the bit-length of |v|. */
    if (mp_isneg(&v)) {
	mp_int absV;

	if (mp_init(&absV) != MP_OKAY) {
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
	if (mp_abs(&v, &absV) != MP_OKAY) {
	    mp_clear(&absV);
	    Th8_SetResultStatic(interp, "bigint: out of memory", TH8_NOLEN);
	    goto done;
	}
	/* |v| is an exact power of 2 iff its lowest set bit
	 * equals (bits - 1), i.e. there is exactly one set bit
	 * at position (bits - 1). */
	if (mp_cnt_lsb(&absV) == bits - 1) {
	    neededBits = bits;
	} else {
	    neededBits = bits + 1;
	}
	mp_clear(&absV);
    } else {
	/* Positive: need an extra zero sign-bit on top. */
	neededBits = bits + 1;
    }

    *pNeeded = (size_t)((neededBits + 7) / 8);
    rc = TH8_OK;

done:
    if (bInit) mp_clear(&v);
    th8BigintTeardown();
    return rc;
}

#endif /* TH8_ENABLE_BIGINT */
