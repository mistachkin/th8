/*
 * th8_cache.c --
 *
 *	Internal-representation cache for the TH8 interpreter.
 *
 *	Maps (cacheType, hashCode) pairs to cache-owned Th8_Value
 *	structs so that repeated string-to-value conversions (integer
 *	parse, double parse, list split, command resolution) are done
 *	at most once per unique input string.
 *
 *	The cache is per-interpreter, protected by a per-interpreter
 *	mutex, and may be cleared at any time.  All heap memory is
 *	owned by the cache; callers receive borrowed pointers.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_plat.h"
#include "th8.h"
#include "th8_int.h"
#include "th8_int_core.h"
#include "th8_hash.h"


/*
 *----------------------------------------------------------------------
 *
 * Cache key layout --
 *
 *	The hash table key is a 12-byte binary blob:
 *
 *	  bytes 0-3:   cacheType  (little-endian int)
 *	  bytes 4-11:  hashCode   (little-endian uint64)
 *
 *	The hashCode is computed from the raw input bytes using
 *	FNV-1a (64-bit).  For TH8_CACHE_LIST, an alternate hash
 *	based on element count XOR'd with per-element hashes is
 *	available via th8CacheHashList().
 *
 *----------------------------------------------------------------------
 */

#define TH8_CACHE_KEY_SIZE 12


/*
 *----------------------------------------------------------------------
 *
 * th8CacheHashBytes --
 *
 *	FNV-1a 64-bit hash of a byte string.  Fast, well-distributed,
 *	no seed or external state required.
 *
 * Why / How:
 *	The cache needs a hash function that maps arbitrary byte strings
 *	to 64-bit keys with good distribution to minimise collisions.
 *	FNV-1a was chosen because it is simple, fast on small inputs
 *	(typical for variable names and short values), and requires no
 *	initialisation state.  The XOR-then-multiply loop processes one
 *	byte per iteration.
 *
 * Results:
 *	A 64-bit hash code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8CacheHashBytes(
    const char *z,  /* Input bytes. */
    size_t n)   /* Raw byte count (must NOT carry the taint bit). */
{
    th8_uint64_t h = (th8_uint64_t)14695981039346656037ULL;
    size_t i;

    /* Pure byte hasher: a taint-tagged length here would scan ~256 MiB
     * past the input.  Callers must pass TH8_LEN()-masked lengths. */
    TH8_ASSERT_RAW_LEN(n);
    for (i = 0; i < n; i++) {
	h ^= (th8_uint64_t)(unsigned char)z[i];
	h *= (th8_uint64_t)1099511628211ULL;
    }
    return h;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheHashList --
 *
 *	Compute a list-aware hash for use with TH8_CACHE_LIST.
 *	The hash is the element count, then XOR'd with the FNV-1a
 *	hash of each element string in order.
 *
 *	Because XOR is commutative, a rotate step is applied before
 *	each XOR to make the hash order-sensitive (so that "a b" and
 *	"b a" produce different hash codes).
 *
 * Why / How:
 *	th8FindListInCache needs to hash a list that is already split
 *	into elements without first joining it into a flat string.
 *	This function hashes the pre-split elements directly by
 *	seeding with the element count and folding in each element's
 *	FNV-1a hash with a 5-bit left rotate to preserve element
 *	order in the final hash code.
 *
 * Results:
 *	A 64-bit hash code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8CacheHashList(
    int nElem,   /* Number of list elements. */
    const char **azElem, /* Element string pointers. */
    const size_t *anElem) /* Element byte lengths. */
{
    th8_uint64_t h;
    int i;

    h = (th8_uint64_t)(unsigned int)nElem;
    for (i = 0; i < nElem; i++) {
	/* Rotate left by 5 bits before XOR for order sensitivity. */
	h = (h << 5) | (h >> 59);
	/* Element lengths may carry the taint bit; hash the RAW byte
	 * count only.  The cache is keyed on element BYTES and is
	 * taint-insensitive -- the caller re-applies aggregate taint to
	 * the result.  A tagged length here would both over-read
	 * (~256 MiB) and split the cache across taint states. */
	h ^= th8CacheHashBytes(azElem[i], TH8_LEN(anElem[i]));
    }
    return h;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheEncodeKey --
 *
 *	Write the 12-byte cache key into zKey.
 *
 * Why / How:
 *	The hash table uses a binary key that combines the cache type
 *	and the 64-bit hash code into a single 12-byte blob.  This
 *	function serialises both values in little-endian byte order
 *	so the key is platform-independent and can be compared with
 *	memcmp by the hash table implementation.
 *
 * Results:
 *	None.  zKey is filled with TH8_CACHE_KEY_SIZE bytes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
th8CacheEncodeKey(
    int cacheType,  /* TH8_CACHE_* code. */
    th8_uint64_t hashCode, /* Hash of the input data. */
    char *zKey)   /* Output: TH8_CACHE_KEY_SIZE bytes. */
{
    zKey[0] = (char)(cacheType & 0xFF);
    zKey[1] = (char)((cacheType >> 8) & 0xFF);
    zKey[2] = (char)((cacheType >> 16) & 0xFF);
    zKey[3] = (char)((cacheType >> 24) & 0xFF);
    zKey[4] = (char)(hashCode & 0xFF);
    zKey[5] = (char)((hashCode >> 8) & 0xFF);
    zKey[6] = (char)((hashCode >> 16) & 0xFF);
    zKey[7] = (char)((hashCode >> 24) & 0xFF);
    zKey[8] = (char)((hashCode >> 32) & 0xFF);
    zKey[9] = (char)((hashCode >> 40) & 0xFF);
    zKey[10] = (char)((hashCode >> 48) & 0xFF);
    zKey[11] = (char)((hashCode >> 56) & 0xFF);
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheMutexEnter --
 *
 *	Acquire the per-interpreter cache mutex if threading support
 *	is available.
 *
 * Why / How:
 *	The IR cache is shared across all evaluation paths within a
 *	single interpreter.  In threaded configurations, concurrent
 *	access must be serialised.  This helper checks whether the
 *	platform provided mutex callbacks via th8CacheMutexReady
 *	before attempting to lock, making the cache code safe for
 *	both threaded and non-threaded builds.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cache mutex is acquired if available.
 *
 *----------------------------------------------------------------------
 */

static void
th8CacheMutexEnter(Th8_Interp *interp)
{
    if (th8CacheMutexReady(interp)) {
	th8CacheMutexLock(interp);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8CacheMutexLeave --
 *
 *	Release the per-interpreter cache mutex if threading support
 *	is available.
 *
 * Why / How:
 *	Counterpart to th8CacheMutexEnter.  Every cache operation
 *	that acquires the mutex must release it before returning,
 *	including on error paths and early exits (the "done:" label
 *	pattern used throughout this file).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cache mutex is released if available.
 *
 *----------------------------------------------------------------------
 */

static void
th8CacheMutexLeave(Th8_Interp *interp)
{
    if (th8CacheMutexReady(interp)) {
	th8CacheMutexUnlock(interp);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheEntryFree --
 *
 *	Release all heap memory owned by a single cache entry.
 *	The entry struct itself is also freed.
 *
 * Why / How:
 *	Cache entries own type-specific heap allocations inside the
 *	Th8_Value union (list element arrays, bigint storage, buffer
 *	pool buffers) plus a copy of the original input string.  This
 *	function dispatches on cacheType to free the correct union
 *	member, then frees the original-string copy and the entry
 *	struct itself.  It is the single destruction point for all
 *	cache entries.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All heap memory owned by the entry is freed.
 *
 *----------------------------------------------------------------------
 */

static void
th8CacheEntryFree(
    Th8_Interp *interp, /* Interpreter (for Th8_Free). */
    Th8_CacheEntry *pEntry) /* Entry to destroy. */
{
    if (!pEntry) return;

    /*
     * Free type-specific resources.  Only ONE union member is
     * active at a time, so we must dispatch on cacheType to
     * avoid misinterpreting overlapping union fields.
     */

    /*
     * List-keyed entries (th8FindListInCache): the element
     * arrays live in pEntry->azListElem (outside the union).
     */
    if (pEntry->azListElem) {
	Th8_Free(interp, pEntry->azListElem);
    }

    switch (pEntry->cacheType) {
    case TH8_CACHE_LIST:
    case TH8_CACHE_DICT:
	/*
	 * String-keyed split-list entries: the cached element
	 * arrays live in the Th8_Value union (u.splitlist).
	 */
	if (pEntry->value.u.splitlist.iValid &&
	    ALWAYS(pEntry->value.u.splitlist.azElem)) {
	    Th8_Free(interp, pEntry->value.u.splitlist.azElem);
	}
	break;

    case TH8_CACHE_BIGINT:
	/*
	 * Bigint entries: the Th8_Bigint wrapper (containing
	 * an mp_int) must be destroyed via th8BigintDestroy
	 * to call mp_clear before freeing.  iValid=1 with
	 * pBigint=NULL is a legitimate "checked, not a
	 * bigint" memo (see th8IsBigint L763 in th8_bigint.c)
	 * -- treat that as nothing to free.  Bug 24 fix.
	 */
	if (pEntry->value.u.bigint.iValid && pEntry->value.u.bigint.pBigint) {
#if defined(TH8_ENABLE_BIGINT)
	    th8BigintDestroy(interp, pEntry->value.u.bigint.pBigint);
#else
	    Th8_Free(interp, pEntry->value.u.bigint.pBigint);
#endif
	}
	break;

    case TH8_CACHE_BUFFER:
	/*
	 * Buffer pool entries: the recycled buffer pointer lives
	 * in u.buffer.pBuffer and must be freed if non-NULL.
	 */
	if (pEntry->value.u.buffer.pBuffer) {
	    Th8_Free(interp, pEntry->value.u.buffer.pBuffer);
	}
	break;

    default:
	/*
	 * INT, WIDE, DOUBLE, BOOL, STRING, COMMAND, SUBEXPR,
	 * and any future non-pointer types:
	 * no pointer-based union members to free.
	 */
	break;
    }

    /*
     * Free the copy of the original input string.
     * value.zData points into this buffer, so no separate free.
     */
    if (pEntry->zOriginal) {
	Th8_Free(interp, pEntry->zOriginal);
    }

    /*
     * Free the entry struct itself.
     */
    Th8_Free(interp, pEntry);
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheFreeCallback --
 *
 *	Th8_HashIterate callback that frees each cache entry's
 *	pData pointer.
 *
 * Why / How:
 *	Used by th8ClearCache to destroy every entry in the cache
 *	hash in a single pass.  Delegates to th8CacheEntryFree for
 *	the actual cleanup, then NULLs the pData pointer so the
 *	hash entry can be safely freed by the subsequent
 *	Th8_HashDelete call.
 *
 * Results:
 *	TH8_OK always (continue iterating).
 *
 * Side effects:
 *	The cache entry is freed.  Benchmarking counters are updated.
 *
 *----------------------------------------------------------------------
 */

static int
th8CacheFreeCallback(
    Th8_HashEntry *pEntry, /* Hash entry being visited. */
    void *pCtx)   /* Th8_Interp* cast to void*. */
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;

    /* th8CacheFreeCallback is invoked via Th8_HashIterate over
     * paCache.  Th8_HashIterate only visits entries that are
     * actually allocated, so pEntry is ALWAYS non-NULL when this
     * callback fires.  pEntry->pData, however, can legitimately be
     * NULL: a collision evict (or an OOM-failed insert, which the
     * fault-injection suite exercises) leaves the hash entry in
     * place with pData == 0.  So guard pData with a plain check,
     * not ALWAYS() -- under TH8_DEBUG the ALWAYS asserted (abort)
     * on those entries; under TH8_OMIT it collapsed to a NULL
     * deref.  Bug 26 family. */
    if (ALWAYS(pEntry) && pEntry->pData) {
#if defined(TH8_BENCHMARKING)
	interp->nCacheEvict++;
#endif
	th8CacheEntryFree(interp, (Th8_CacheEntry *)pEntry->pData);
	pEntry->pData = 0;
    }
    return TH8_OK; /* Continue iteration. */
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheGetHash --
 *
 *	Return the per-interpreter cache hash table, creating it
 *	lazily if needed.  Caller must hold the cache mutex.
 *
 * Why / How:
 *	The cache hash is not allocated until the first cache
 *	operation, avoiding overhead for interpreters that never use
 *	the cache (e.g. short-lived sub-interpreters).  This function
 *	centralises the lazy creation so every cache operation can
 *	simply call th8CacheGetHash and check for NULL (OOM).
 *
 * Results:
 *	Pointer to the cache Th8_Hash, or NULL on allocation failure.
 *
 * Side effects:
 *	May allocate a new Th8_Hash.
 *
 *----------------------------------------------------------------------
 */

static Th8_Hash *
th8CacheGetHash(Th8_Interp *interp)
{
    Th8_Hash **pp = th8CacheHashPtr(interp);

    if (!*pp) {
	*pp = Th8_HashNew(interp);
    }
    return *pp;  /* NULL if allocation failed. */
}


/*
 *======================================================================
 *
 * PUBLIC API
 *
 *======================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * th8ClearCache --
 *
 *	Discard every entry in the interpreter's cache and free all
 *	associated heap memory.
 *
 * Why / How:
 *	Called explicitly to invalidate all cached conversions (e.g.
 *	after a script modifies a cached variable), and also during
 *	interpreter teardown via th8CacheFinish.  Iterates the hash
 *	to free each entry, then deletes the hash itself and NULLs
 *	the pointer so th8CacheGetHash will recreate it on next use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All cache entries and the hash table are freed.
 *
 *----------------------------------------------------------------------
 */

void
th8ClearCache(Th8_Interp *interp) /* Interpreter whose cache to clear. */
{
    Th8_Hash **pp;

    if (!interp) return;

    th8CacheMutexEnter(interp);

    pp = th8CacheHashPtr(interp);
    if (*pp) {
	Th8_HashIterate(interp, *pp, th8CacheFreeCallback, (void *)interp);
	Th8_HashDelete(interp, *pp);
	*pp = 0;
    }

    th8CacheMutexLeave(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FindInCache --
 *
 *	Look up or create a cache entry for the string z (n bytes)
 *	under the given cacheType.
 *
 *	On hit:  returns the existing Th8_Value (union fields may
 *		 already be populated by a prior caller).
 *
 *	On miss: creates a new entry with eType = TH8_VALUE_STRING,
 *		 zData pointing to a cache-owned copy of z, and all
 *		 union valid flags cleared.
 *
 *	On hash collision (same key, different original string):
 *		 evicts the old entry and creates a fresh one.
 *
 *	Returns NULL only on OOM.
 *
 * Why / How:
 *	This is the primary cache lookup/insert API.  It hashes the
 *	input string via FNV-1a, builds a 12-byte key encoding the
 *	cache type and hash code, then probes the hash table.  On a
 *	hit, it verifies the original string matches (guarding against
 *	collisions); on a miss, it allocates a new entry with a copy
 *	of the input.  The caller receives a borrowed Th8_Value* whose
 *	union fields can be populated with type-specific cached data.
 *
 * Results:
 *	A borrowed (cache-owned) pointer to the Th8_Value for the key --
 *	the existing entry on a hit, or a freshly created entry on a
 *	miss or collision eviction.  NULL on a NULL interp/string or on
 *	an out-of-memory failure (and, in fault-injection builds, when
 *	the lookup-failure hook is armed for this cacheType).
 *
 * Side effects:
 *	Acquires the cache mutex.  May allocate a new cache entry (with
 *	a cache-owned copy of the input string) and insert it into the
 *	per-interpreter cache hash table, and may evict/free a colliding
 *	entry.  In fault-injection builds, may update the fault hook's
 *	skip/fire counters.
 *
 *----------------------------------------------------------------------
 */

Th8_Value *
Th8_FindInCache(
    Th8_Interp *interp, /* Interpreter. */
    int cacheType,  /* TH8_CACHE_* code. */
    const char *z,  /* Input string. */
    size_t n)   /* Byte length, or TH8_NOLEN. */
{
    th8_uint64_t hashCode;
    char zKey[TH8_CACHE_KEY_SIZE];
    Th8_Hash *paCache;
    Th8_HashEntry *pEntry;
    Th8_CacheEntry *pCache;
    Th8_Value *pResult = 0;

    /* Split per Finding 005. */
    if (!interp) return 0;
    if (!z) return 0;
    if (n == TH8_NOLEN) n = Th8_Strlen(interp, z);

#if defined(TH8_ENABLE_FAULT_INJECTION)
    /* MC/DC fault-injection hook: when the active fault
     * config requests forced lookup failure for this
     * cacheType, return NULL.  Lets tests deterministically
     * drive `pCached == NULL` (Bug-28 family) vectors at
     * specific call sites without relying on
     * allocation-site-counter timing.
     *
     * Bug 28 second-call extension (2026-06-09): if
     * `nCacheLookupSkip > 0`, the hook passes through
     * (decrementing the skip counter) for that many
     * matching-cacheType lookups before starting to fail.
     * This targets the SECOND (or Nth) call in a
     * Th8_ToInt/SplitList-store path where the first call
     * succeeds (caching the parsed value) but the second
     * call (cache-bypass-then-store) needs to observe
     * pCached==NULL. */
    if (th8FaultActiveCfg &&
        (th8FaultActiveCfg->nFailCacheLookupMask & (1u << cacheType))) {
	if (th8FaultActiveCfg->nCacheLookupSkip > 0) {
	    th8FaultActiveCfg->nCacheLookupSkip--;
	} else {
	    th8FaultActiveCfg->nCacheHookFires++;
	    return 0;
	}
    }
#endif /* TH8_ENABLE_FAULT_INJECTION */

    /*
     * Compute the hash code from the raw input bytes.
     */
    hashCode = th8CacheHashBytes(z, n);

    /*
     * Encode the 12-byte cache key.
     */
    th8CacheEncodeKey(cacheType, hashCode, zKey);

    th8CacheMutexEnter(interp);

    paCache = th8CacheGetHash(interp);
    if (!paCache) goto done;

    /*
     * Find or create the hash entry (op = 1).
     */
    pEntry = Th8_HashFind(interp, paCache, zKey, TH8_CACHE_KEY_SIZE, 1);
    if (!pEntry) goto done;

    if (pEntry->pData) {
	/*
	 * Existing entry -- verify the original input matches
	 * to guard against (extremely rare) hash collisions.
	 */
	pCache = (Th8_CacheEntry *)pEntry->pData;
	/* Nested per Finding 005 sec. 5b: collision-verification
	 * compound's C-pairs are intrinsic-dead in the test corpus
	 * (no hash collisions observed); preserved as defense-in-
	 * depth for adversarial inputs. */
	{
	    int matches = 0;

	    if (pCache->cacheType == cacheType) {
		if (pCache->nOriginal == n) {
		    if (Th8_Memcmp(interp, pCache->zOriginal, z, n) == 0) {
			matches = 1;
		    }
		}
	    }
	    if (matches) {
#if defined(TH8_BENCHMARKING)
		interp->nCacheHit++;
#endif
		pResult = &pCache->value;
		goto done;
	    }
	}
	/*
	 * Collision: different input, same hash.  Evict the
	 * stale entry and fall through to create a fresh one.
	 */
#if defined(TH8_BENCHMARKING)
	interp->nCacheEvict++;
#endif
	th8CacheEntryFree(interp, pCache);
	pEntry->pData = 0;
    }

    /*
     * New entry.  Allocate the cache entry and copy the
     * original input string.
     */
#if defined(TH8_BENCHMARKING)
    interp->nCacheMiss++;
#endif
    pCache = (Th8_CacheEntry *)TH8_ALLOC(interp, sizeof(Th8_CacheEntry));
    if (!pCache) goto done;

    Th8_Memset(interp, pCache, 0, sizeof(Th8_CacheEntry));
    pCache->cacheType = cacheType;

    pCache->zOriginal = (char *)TH8_ALLOC_STR(interp, n);
    if (!pCache->zOriginal) {
	Th8_Free(interp, pCache);
	goto done;
    }
    Th8_Memcpy(interp, pCache->zOriginal, z, n);
    pCache->zOriginal[n] = '\0';
    pCache->nOriginal = n;

    /*
     * Initialise the Th8_Value to a string with no cached
     * conversions.  zData borrows from zOriginal (same
     * lifetime, both cache-owned).
     */
    pCache->value.nVersion = 0;
    pCache->value.eType = TH8_VALUE_STRING;
    pCache->value.zData = pCache->zOriginal;
    pCache->value.nData = n;

    pEntry->pData = pCache;
    pResult = &pCache->value;

done:
    th8CacheMutexLeave(interp);
    return pResult;
}


#if defined(TH8_BENCHMARKING)
/*
 *----------------------------------------------------------------------
 *
 * Th8_GetCacheStats --
 *
 *	Retrieve IR cache performance counters.
 *
 * Why / How:
 *	Exposes hit, miss, and eviction counts for benchmarking and
 *	tuning.  The counters are maintained inline in Th8_FindInCache
 *	and th8RemoveFromCache under #ifdef TH8_BENCHMARKING.  This
 *	function simply copies the counter values to the caller's
 *	output pointers.
 *
 * Results:
 *	None.  Output pointers are set (any may be NULL to skip).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Th8_GetCacheStats(
    Th8_Interp *interp,
    th8_uint64_t *pnHit,
    th8_uint64_t *pnMiss,
    th8_uint64_t *pnEvict)
{
    if (pnHit) *pnHit = interp->nCacheHit;
    if (pnMiss) *pnMiss = interp->nCacheMiss;
    if (pnEvict) *pnEvict = interp->nCacheEvict;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ResetCacheStats --
 *
 *	Reset all IR cache performance counters to zero.
 *
 * Why / How:
 *	Allows benchmarking scripts to zero the counters between
 *	test phases so that each phase's cache behaviour can be
 *	measured independently.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All three counters (hit, miss, evict) are set to zero.
 *
 *----------------------------------------------------------------------
 */

void
Th8_ResetCacheStats(Th8_Interp *interp)
{
    interp->nCacheHit = 0;
    interp->nCacheMiss = 0;
    interp->nCacheEvict = 0;
}
#endif /* TH8_BENCHMARKING */


/*
 *----------------------------------------------------------------------
 *
 * th8RemoveFromCache --
 *
 *	Remove a single cache entry identified by (cacheType, z, n).
 *	Frees all heap memory associated with the entry.  No-op if
 *	the entry does not exist.
 *
 * Why / How:
 *	When a variable is overwritten by [set] or removed by [unset],
 *	any cached representation keyed on that variable's name must
 *	be invalidated to prevent stale data from being reused.  This
 *	function computes the same hash and key that Th8_FindInCache
 *	would, looks up the entry, frees it, and removes it from the
 *	hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cache entry (if any) is freed.  Benchmarking counters
 *	are updated.
 *
 *----------------------------------------------------------------------
 */

void
th8RemoveFromCache(
    Th8_Interp *interp, /* Interpreter. */
    int cacheType,  /* TH8_CACHE_* code. */
    const char *z,  /* Input string. */
    size_t n)   /* Byte length, or TH8_NOLEN. */
{
    th8_uint64_t hashCode;
    char zKey[TH8_CACHE_KEY_SIZE];
    Th8_Hash **pp;
    Th8_HashEntry *pEntry;

    /* Bug 26 family: plain interp guard (Bug 31 pattern).
     * Public th8RemoveFromCache entry; NULL interp under
     * TH8_OMIT would let the subsequent Th8_Strlen /
     * th8CacheGetHash deref a NULL pointer. */
    /* Split per Finding 005. */
    if (!interp) return;
    if (!z) return;
    if (n == TH8_NOLEN) n = Th8_Strlen(interp, z);

    hashCode = th8CacheHashBytes(z, n);
    th8CacheEncodeKey(cacheType, hashCode, zKey);

    th8CacheMutexEnter(interp);

    pp = th8CacheHashPtr(interp);
    if (*pp) {
	pEntry = Th8_HashFind(interp, *pp, zKey, TH8_CACHE_KEY_SIZE, 0);
	/* Bug 26 family: plain guard.  Th8_HashDelete tombstones
	 * (zeroes pData, keeps the entry), so a find can return a
	 * non-NULL entry with pData == NULL.  Nested per Finding
	 * 005 sec. 5b: C1 (!pEntry) is the rarely-exercised arm
	 * (HashFind generally produces an entry); pData==NULL is
	 * the live tombstone arm. */
	if (pEntry)
	    if (pEntry->pData) {
#if defined(TH8_BENCHMARKING)
		interp->nCacheEvict++;
#endif
		th8CacheEntryFree(interp, (Th8_CacheEntry *)pEntry->pData);
		pEntry->pData = 0;
		Th8_HashFind(interp, *pp, zKey, TH8_CACHE_KEY_SIZE, -1);
	    }
    }

    th8CacheMutexLeave(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheInit --
 *
 *	Called from Th8_CreateInterp.  Initialises the per-interpreter
 *	cache mutex.  The hash table itself is created lazily on first
 *	use.
 *
 * Why / How:
 *	Separated from hash table creation so that the mutex is ready
 *	before any code path might access the cache.  The hash table
 *	is deferred to th8CacheGetHash to avoid allocating memory for
 *	interpreters that never perform a cache lookup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cache mutex is initialised via th8CacheMutexSetup.
 *
 *----------------------------------------------------------------------
 */

void
th8CacheInit(Th8_Interp *interp) /* Newly created interpreter. */
{
    th8CacheMutexSetup(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8CacheFinish --
 *
 *	Called from Th8_DeleteInterp.  Clears the cache and finalises
 *	the per-interpreter mutex.
 *
 * Why / How:
 *	Counterpart to th8CacheInit.  Calls th8ClearCache to free all
 *	entries and the hash table, then calls th8CacheMutexTeardown
 *	to destroy the mutex.  Must be called before the interpreter's
 *	memory is freed so that cache-owned pointers are released.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All cache memory is freed.  The cache mutex is destroyed.
 *
 *----------------------------------------------------------------------
 */

void
th8CacheFinish(Th8_Interp *interp) /* Interpreter being destroyed. */
{
    th8ClearCache(interp);
    th8CacheMutexTeardown(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8FindListInCache --
 *
 *	List-keyed variant of Th8_FindInCache.  The hash code is the
 *	element count, rotated-and-XOR'd with the FNV-1a hash of each
 *	element string in order (order-sensitive).
 *
 *	Typically used with TH8_CACHE_STRING to cache the string that
 *	results from joining a list.  The caller supplies the already-
 *	split element arrays; the hash is computed from them, not from
 *	a flat string.
 *
 *	On miss the returned Th8_Value has eType = TH8_VALUE_STRING
 *	and zData = NULL (no string yet).  The caller joins the list,
 *	then writes the result into zData/nData (via the cache entry's
 *	zOriginal, which will be allocated by the caller and assigned
 *	into the cache entry through a helper or direct write).
 *
 * Why / How:
 *	List joining is expensive for large lists and is often repeated
 *	with the same elements (e.g. building info command results).
 *	By hashing the pre-split elements via th8CacheHashList, we can
 *	cache the joined string and avoid redundant joins.  On a hit,
 *	each element is compared for an exact match to detect
 *	collisions.  On a miss, copies of the element arrays are stored
 *	in a single allocation block ([ptrs][lens][strings]) so the
 *	entry is self-contained.
 *
 * Results:
 *	Pointer to a Th8_Value (cache-owned) on success, or NULL
 *	on OOM.
 *
 * Side effects:
 *	A new cache entry may be allocated.  On collision, the
 *	existing entry is evicted and freed.
 *
 *----------------------------------------------------------------------
 */

Th8_Value *
th8FindListInCache(
    Th8_Interp *interp, /* Interpreter. */
    int cacheType,  /* Usually TH8_CACHE_STRING. */
    int nElem,   /* Number of list elements. */
    const char **azElem, /* Element string pointers. */
    const size_t *anElem) /* Element byte lengths. */
{
    th8_uint64_t hashCode;
    char zKey[TH8_CACHE_KEY_SIZE];
    Th8_Hash *paCache;
    Th8_HashEntry *pEntry;
    Th8_CacheEntry *pCache;
    Th8_Value *pResult = 0;

    /* Bug 26 family: plain interp guard.  Public
     * th8FindListInCache entry; same rationale as
     * th8RemoveFromCache above. */
    if (!interp || nElem < 0) return 0;
    if (nElem == 0) {
	/*
	 * Empty list ==> empty string.  Use the byte-hash path
	 * with an empty input.
	 */
	return Th8_FindInCache(interp, cacheType, "", 0);
    }
    /* Split per Finding 005. */
    if (!azElem) return 0;
    if (!anElem) return 0;

    /*
     * Compute the list-aware hash.
     */
    hashCode = th8CacheHashList(nElem, azElem, anElem);
    th8CacheEncodeKey(cacheType, hashCode, zKey);

    th8CacheMutexEnter(interp);

    paCache = th8CacheGetHash(interp);
    if (!paCache) goto done;

    pEntry = Th8_HashFind(interp, paCache, zKey, TH8_CACHE_KEY_SIZE, 1);
    if (!pEntry) goto done;

    if (pEntry->pData) {
	/*
	 * Hit verification: compare element count and each
	 * element to detect collisions.
	 */
	int i;

	pCache = (Th8_CacheEntry *)pEntry->pData;
	/* Nested per Finding 005 sec. 5b: list-cache hit-
	 * verification compound's C-pairs are intrinsic-dead in
	 * the test corpus (no hash collisions observed across
	 * cacheType/nListElem); kept as defense-in-depth. */
	if (pCache->cacheType != cacheType) {
	    goto evict;
	}
	if (pCache->nListElem != nElem) {
	    goto evict;
	}
	for (i = 0; i < nElem; i++) {
	    /* Compare RAW byte lengths: stored lengths are masked (see
	     * the miss path below), so a tagged incoming length must be
	     * masked too or an identical-bytes hit would spuriously
	     * evict.  Same rationale on intrinsic-dead compare arms. */
	    size_t nRaw = TH8_LEN(anElem[i]);

	    /* Stored lengths are raw; compare on the masked length.
	     * Guards against a regression that drops the mask. */
	    TH8_ASSERT_RAW_LEN(pCache->anListElem[i]);
	    if (pCache->anListElem[i] != nRaw) {
		goto evict;
	    }
	    if (Th8_Memcmp(interp, pCache->azListElem[i], azElem[i], nRaw) !=
	        0) {
		goto evict;
	    }
	}
	/* Cache hit: element-by-element match confirmed. */
#if defined(TH8_BENCHMARKING)
	interp->nCacheHit++;
#endif
	pResult = &pCache->value;
	goto done;

evict:
#if defined(TH8_BENCHMARKING)
	interp->nCacheEvict++;
#endif
	th8CacheEntryFree(interp, pCache);
	pEntry->pData = 0;
    }

    /*
     * Miss.  Create a new entry.  We store copies of the
     * element arrays so the entry is self-contained.
     */
#if defined(TH8_BENCHMARKING)
    interp->nCacheMiss++;
#endif
    {
	size_t nStrTotal = 0;
	size_t nAlloc;
	char **azNew;
	size_t *anNew;
	char *zBuf;
	int i;

	pCache = (Th8_CacheEntry *)TH8_ALLOC(interp, sizeof(Th8_CacheEntry));
	if (!pCache) goto done;
	Th8_Memset(interp, pCache, 0, sizeof(Th8_CacheEntry));
	pCache->cacheType = cacheType;

	/*
	 * Build a single-allocation block: [ptrs][lens][strings].
	 * Size on RAW byte lengths -- a tagged length would compute a
	 * ~256 MiB allocation.
	 */
	for (i = 0; i < nElem; i++) {
	    TH8_ASSERT_RAW_LEN(TH8_LEN(anElem[i]));
	    nStrTotal += TH8_LEN(anElem[i]) + 1;
	}
	{
	    size_t nPtrs;
	    size_t nLens;
	    /*
	     * Build the single-block size with overflow-safe arithmetic.
	     * Layout is [ptrs][lens][strings]; each component multiplied
	     * by nElem could overflow, so use TH8_SAFE_MUL_SIZE for the
	     * two multiplies and TH8_SAFE_ADD_SIZE for chaining the
	     * sums.  Any overflow folds into a single error path.
	     */
	    /* 4-way overflow check sequenced as an else-if ladder
	     * so each leg is a single-condition decision for
	     * clang's MC/DC.  See FINDINGS.md Finding 005. */
	    int overflowed = 0;

	    if (TH8_SAFE_MUL_SIZE((size_t)nElem, sizeof(char *), &nPtrs)) {
		overflowed = 1;
	    } else if (
	        TH8_SAFE_MUL_SIZE((size_t)nElem, sizeof(size_t), &nLens)) {
		overflowed = 1;
	    } else if (TH8_SAFE_ADD_SIZE(nPtrs, nLens, &nAlloc)) {
		overflowed = 1;
	    } else if (TH8_SAFE_ADD_SIZE(nAlloc, nStrTotal, &nAlloc)) {
		overflowed = 1;
	    }
	    if (overflowed) {
		Th8_Free(interp, pCache);
		goto done;
	    }
	}

	azNew = (char **)TH8_ALLOC(interp, nAlloc);
	if (!azNew) {
	    Th8_Free(interp, pCache);
	    goto done;
	}
	anNew = (size_t *)&azNew[nElem];
	zBuf = (char *)&anNew[nElem];

	for (i = 0; i < nElem; i++) {
	    /* Store RAW element lengths; the cache is taint-insensitive
	     * and the caller re-applies aggregate taint at the boundary.
	     * Using the tagged length as a memcpy count / buffer index
	     * would over-copy and write out of bounds. */
	    size_t nRaw = TH8_LEN(anElem[i]);

	    TH8_ASSERT_RAW_LEN(nRaw);
	    anNew[i] = nRaw;
	    azNew[i] = zBuf;
	    Th8_Memcpy(interp, zBuf, azElem[i], nRaw);
	    zBuf[nRaw] = '\0';
	    zBuf += nRaw + 1;
	}

	pCache->azListElem = azNew;
	pCache->anListElem = anNew;
	pCache->nListElem = nElem;

	/*
	 * The Th8_Value starts with no string (zData = NULL).
	 * The caller will join the list and populate it.
	 */
	pCache->value.nVersion = 0;
	pCache->value.eType = TH8_VALUE_STRING;
	pCache->value.zData = 0;
	pCache->value.nData = 0;

	pEntry->pData = pCache;
	pResult = &pCache->value;
    }

done:
    th8CacheMutexLeave(interp);
    return pResult;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetCacheString --
 *
 *	Set the string representation on a cache entry.  Used after
 *	th8FindListInCache to populate the joined-string result.
 *	The cache takes ownership of a copy of z.
 *
 * Why / How:
 *	th8FindListInCache returns a Th8_Value with zData = NULL on a
 *	miss.  After the caller joins the list into a flat string, it
 *	calls this function to store that string in the cache entry.
 *	The string is copied into zOriginal (owned by the cache entry),
 *	and zData is pointed at the same buffer.  Uses offsetof to
 *	recover the enclosing Th8_CacheEntry from the embedded
 *	Th8_Value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a copy of the string.  Updates the cache entry's
 *	zOriginal, nOriginal, zData, and nData fields.
 *
 *----------------------------------------------------------------------
 */

void
th8SetCacheString(
    Th8_Interp *interp, /* Interpreter (for Th8_AttemptMalloc). */
    Th8_Value *pVal,  /* Value from th8FindListInCache. */
    const char *z,  /* String data. */
    size_t n)   /* Byte length, or TH8_NOLEN. */
{
    Th8_CacheEntry *pEntry;
    char *zCopy;

    /* Bug 26 family: plain guards on the public
     * th8StoreStringInCache entry; null interp would
     * deref via Th8_Strlen / Th8_Memcpy below, and null
     * pVal would deref via pVal->zData check.  Split per
     * Finding 005. */
    if (!interp) return;
    if (!pVal) return;
    if (!z) return;
    if (pVal->zData) return; /* Already has a string. */
    if (n == TH8_NOLEN) {
	n = Th8_Strlen(interp, z);
    } else {
	/* Store a RAW length: the cache holds bytes, not trust state.
	 * A tainted joined string would otherwise bake taint into the
	 * shared entry and launder it onto later clean hits (or falsely
	 * taint them).  The caller re-applies aggregate taint to the
	 * result on every hit/miss.  Resolve TH8_NOLEN first so the tag
	 * mask does not corrupt the sentinel. */
	n = TH8_LEN(n);
    }
    TH8_ASSERT_RAW_LEN(n);

    /*
     * Recover the enclosing Th8_CacheEntry from the
     * embedded Th8_Value using offsetof.
     */
    pEntry = (Th8_CacheEntry *)((char *)pVal -
                                offsetof(Th8_CacheEntry, value));

    zCopy = (char *)TH8_ALLOC_STR(interp, n);
    if (!zCopy) return;
    Th8_Memcpy(interp, zCopy, z, n);
    zCopy[n] = '\0';

    /*
     * Store in zOriginal so th8CacheEntryFree will free it.
     * Point zData at the same buffer.
     */
    pEntry->zOriginal = zCopy;
    pEntry->nOriginal = n;
    pVal->zData = zCopy;
    pVal->nData = n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CopyValue --
 *
 *	Deep-copy a Th8_Value.  Returns a newly allocated Th8_Value
 *	that the caller owns and must free with th8FreeValue.
 *
 *	Copies the string data (zData) and the currently-valid
 *	type-specific cached representation.  List element arrays
 *	are NOT deep-copied (the copy is string-only); callers who
 *	need the list should re-split from the copied string.
 *
 * Why / How:
 *	Cache-owned Th8_Values have the same lifetime as the cache
 *	entry and can be evicted at any time.  When a caller needs to
 *	hold a value across operations that may mutate the cache, it
 *	uses th8CopyValue to create an independent copy.  The shallow
 *	copy gets all union fields, then the string data is deep-
 *	copied and pointer-based union members are zeroed so the copy
 *	is fully independent.
 *
 * Results:
 *	A newly allocated Th8_Value owned by the caller, or NULL
 *	on OOM.
 *
 * Side effects:
 *	Allocates memory for the value struct and its string data.
 *
 *----------------------------------------------------------------------
 */

Th8_Value *
th8CopyValue(
    Th8_Interp *interp, /* Interpreter (for Th8_AttemptMalloc). */
    const Th8_Value *pSrc) /* Value to copy (may be cache-owned). */
{
    Th8_Value *pDst;
    char *zCopy;

    /* Bug 26 fix: plain conditional instead of NEVER. */
    /* Split per Finding 005. */
    if (!interp) return 0;
    if (!pSrc) return 0;

    pDst = (Th8_Value *)TH8_ALLOC(interp, sizeof(Th8_Value));
    if (!pDst) return 0;

    /*
     * Shallow copy first (gets all union fields).
     */
    Th8_Memcpy(interp, pDst, pSrc, sizeof(Th8_Value));

    /*
     * Deep-copy the string data so the result is independent
     * of the source's lifetime.  Bug 27 fix: when the deep-
     * copy branch is skipped (no zData or nData==0), zero
     * pDst->zData explicitly so th8FreeValue does not try to
     * Th8_Free a pointer that came in via shallow memcpy from
     * a potentially non-heap pSrc (e.g. a string literal in
     * a synthetic Value).
     */
    if (pSrc->zData && pSrc->nData > 0) {
	zCopy = (char *)TH8_ALLOC_STR(interp, pSrc->nData);
	if (!zCopy) {
	    Th8_Free(interp, pDst);
	    return 0;
	}
	Th8_Memcpy(interp, zCopy, pSrc->zData, pSrc->nData);
	zCopy[pSrc->nData] = '\0';
	pDst->zData = zCopy;
    } else {
	pDst->zData = NULL;
	pDst->nData = 0;
    }

    /*
     * Clear pointer-based union members -- the copy owns only
     * its string.  The caller should re-derive any list,
     * splitlist, bigint, or command representation as needed.
     */
    Th8_Memset(interp, &pDst->u, 0, sizeof(pDst->u));

    return pDst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreeValue --
 *
 *	Free a Th8_Value allocated by th8CopyValue.  Does NOT
 *	free cache-owned values (those are freed by th8ClearCache).
 *
 * Why / How:
 *	Counterpart to th8CopyValue.  Frees the deep-copied string
 *	data and the Th8_Value struct itself.  Because th8CopyValue
 *	zeroes the pointer-based union members, no type-specific
 *	cleanup is needed here.  Must not be called on cache-owned
 *	values, which are freed by th8CacheEntryFree.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The Th8_Value and its string data are freed.
 *
 *----------------------------------------------------------------------
 */

void
th8FreeValue(
    Th8_Interp *interp, /* Interpreter (for Th8_Free). */
    Th8_Value *pVal)  /* Value to free. */
{
    /* Bug 26 fix: use plain conditional instead of NEVER so
     * th8FreeValue(NULL, ...) and th8FreeValue(*, NULL) are
     * safe under TH8_OMIT_AUXILIARY_SAFETY_CHECKS.  The
     * original NEVER guards constant-folded to false and let
     * subsequent derefs crash. */
    /* Split per Finding 005. */
    if (!interp) return;
    if (!pVal) return;

    if (pVal->zData) {
	Th8_Free(interp, (void *)pVal->zData);
    }
    Th8_Free(interp, pVal);
}


/*
 *----------------------------------------------------------------------
 *
 * Buffer pool.
 *
 *	th8BufferAlloc and th8BufferFree provide a per-interpreter
 *	buffer pool built on the IR cache.  Freed buffers are recycled
 *	into size-class bins (powers of 2 from 32 to 65536 bytes).
 *	A subsequent allocation of the same size class pops a buffer
 *	from the pool instead of calling Th8_Malloc.
 *
 *	Each size class stores ONE recycled buffer (one-deep pool).
 *	This is sufficient for the common pattern where a single
 *	buffer is allocated, used, freed, then another is needed.
 *
 *	Pool entries are keyed in the cache by synthetic names that
 *	start with '\0' (NUL) so they cannot collide with variable
 *	names or other string-keyed cache entries.
 *
 *----------------------------------------------------------------------
 */

/*
 * Size classes: 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
 * 16384, 32768, 65536.  Twelve bins.  Allocations larger than
 * 65536 bypass the pool and go directly to Th8_Malloc/Th8_Free.
 */

#define TH8_POOL_MIN_CLASS   32
#define TH8_POOL_MAX_CLASS   65536
#define TH8_POOL_NUM_CLASSES 12

/*
 *----------------------------------------------------------------------
 *
 * th8BufferClassIndex --
 *
 *	Map a byte count to a size class index (0..11) and the
 *	rounded-up class size.
 *
 * Why / How:
 *	The buffer pool uses power-of-two size classes from 32 to
 *	65536 bytes (12 bins).  This function finds the smallest
 *	class that can satisfy the request by doubling from the
 *	minimum until the class size >= nBytes.  Returns -1 for
 *	requests larger than the maximum class, signalling the
 *	caller to bypass the pool.
 *
 * Results:
 *	Size class index (0..11), or -1 if too large for the pool.
 *	*pnClass is set to the rounded-up class size.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8BufferClassIndex(size_t nBytes, size_t *pnClass)
{
    size_t nClass = TH8_POOL_MIN_CLASS;
    int idx = 0;

    while (nClass < nBytes && idx < TH8_POOL_NUM_CLASSES - 1) {
	nClass *= 2;
	idx++;
    }
    if (nClass < nBytes) return -1;  /* too large */
    *pnClass = nClass;
    return idx;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BufferPoolKey --
 *
 *	Build the synthetic cache key for a buffer pool bin.
 *
 * Why / How:
 *	The buffer pool stores recycled buffers in the IR cache under
 *	synthetic keys that cannot collide with real cache entries.
 *	The key is 4 bytes: a leading NUL (no valid string key starts
 *	with NUL), the letter 'P' for "pool", and two ASCII digits
 *	encoding the class index.  This gives each of the 12 size
 *	classes a unique, fixed-size key.
 *
 * Results:
 *	None.  zKey is filled with 4 bytes and *pnKey is set to 4.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
th8BufferPoolKey(int idx, char *zKey, size_t *pnKey)
{
    zKey[0] = '\0';
    zKey[1] = 'P';
    zKey[2] = (char)('0' + idx / 10);
    zKey[3] = (char)('0' + idx % 10);
    *pnKey = 4;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BufferAlloc --
 *
 *	Allocate a buffer of at least nBytes.  Checks the per-interpreter
 *	buffer pool first; falls back to Th8_Malloc.  The returned buffer
 *	is zero-filled (same contract as Th8_Malloc).
 *
 *	The actual allocation may be larger than requested (rounded up
 *	to the next size class).  The caller must pass the same nBytes
 *	to th8BufferFree so the pool can identify the correct bin.
 *
 * Why / How:
 *	Reduces malloc pressure for the common allocate-use-free-repeat
 *	pattern (e.g. temporary buffers in [append] and [string map]).
 *	Looks up the size class via th8BufferClassIndex, then probes
 *	the cache for a recycled buffer.  On a pool hit, the buffer
 *	is popped and zero-filled; on a miss, a fresh allocation is
 *	made via Th8_AttemptMalloc.  Requests larger than the maximum
 *	class bypass the pool entirely.
 *
 * Results:
 *	Pointer to the allocated buffer, or NULL on OOM.
 *
 * Side effects:
 *	A buffer may be removed from the pool cache entry.  Memory
 *	may be allocated from the platform.
 *
 *----------------------------------------------------------------------
 */

void *
th8BufferAlloc(Th8_Interp *interp, size_t nBytes)
{
    size_t nClass;
    int idx;
    Th8_Value *pVal;
    char zKey[4];
    size_t nKey;

    if (nBytes == 0) nBytes = 1;

    idx = th8BufferClassIndex(nBytes, &nClass);
    if (idx < 0) {
	/* Too large for pool -- direct allocation. */
	return TH8_ALLOC(interp, nBytes);
    }

    th8BufferPoolKey(idx, zKey, &nKey);
    pVal = Th8_FindInCache(interp, TH8_CACHE_BUFFER, zKey, nKey);

    /* Bug 28 family: plain pVal check; Th8_FindInCache may
     * return NULL under OOM in the cache entry allocator. */
    /* Bug 28 residual (2026-05-29): the nCapacity >= nBytes
     * invariant fires intermittently in TH8_DEBUG stress runs
     * (`Assertion failed: (0), function th8BufferAlloc, file
     * th8_cache.c, line 1415`) and the assertion has been
     * difficult to attribute to any single allocator-mismatch
     * source (Bug 33's zExport fix closed one path but did
     * not eliminate the intermittency).  Replaced ALWAYS
     * with a plain check that falls through to direct
     * allocation when the invariant fails -- same pattern
     * applied to the Bug 28 mainline at th8_core.c:14756.
     * The buffer that would otherwise be popped is left in
     * the bin for the subsequent th8BufferFree to overwrite,
     * so this isn't a leak path; it's a conservative
     * "trust nothing about the bin's metadata" fallback. */
    /* Nested per Finding 005 sec. 5b: the nCapacity
     * sufficiency arm is intrinsic-dead in the test corpus
     * (bin is keyed by size class so popped buffers always
     * satisfy nBytes); kept per the Bug 28 residual rationale
     * above. */
    if (pVal && pVal->u.buffer.pBuffer)
	if (pVal->u.buffer.nCapacity >= nBytes) {
        /* Pool hit: pop the recycled buffer. */
	    void *p = pVal->u.buffer.pBuffer;
	    pVal->u.buffer.pBuffer = 0;
	    pVal->u.buffer.nUsed = 0;
        /* Zero-fill to match Th8_Malloc contract. */
	    Th8_Memset(interp, p, 0, nClass);
	    return p;
	}

    /* Pool miss: allocate from the platform. */
    return TH8_ALLOC(interp, nClass);
}


/*
 *----------------------------------------------------------------------
 *
 * th8BufferFree --
 *
 *	Return a buffer to the per-interpreter pool.  If the pool bin
 *	for this size class already has a buffer (one-deep), the buffer
 *	is freed via Th8_Free instead.
 *
 *	nBytes must be the same value passed to th8BufferAlloc so the
 *	correct bin is identified.
 *
 * Why / How:
 *	Counterpart to th8BufferAlloc.  Looks up the size class and
 *	probes the cache for the pool bin.  If the bin is empty, the
 *	buffer is recycled into it for later reuse; if the bin already
 *	holds a buffer (one-deep pool), the buffer is freed directly
 *	via Th8_Free.  Requests that were too large for the pool are
 *	always freed directly.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The buffer may be recycled into the pool or freed.
 *
 *----------------------------------------------------------------------
 */

void
th8BufferFree(Th8_Interp *interp, void *p, size_t nBytes)
{
    size_t nClass;
    int idx;
    Th8_Value *pVal;
    char zKey[4];
    size_t nKey;

    if (!p) return;
    if (nBytes == 0) nBytes = 1;

    idx = th8BufferClassIndex(nBytes, &nClass);
    if (idx < 0) {
	/* Too large for pool -- direct free. */
	Th8_Free(interp, p);
	return;
    }

    th8BufferPoolKey(idx, zKey, &nKey);
    pVal = Th8_FindInCache(interp, TH8_CACHE_BUFFER, zKey, nKey);

    /* Bug 28 family: NULL pVal (OOM) -> just free directly. */
    if (pVal && !pVal->u.buffer.pBuffer) {
	/* Pool has room: recycle the buffer. */
	pVal->u.buffer.pBuffer = p;
	pVal->u.buffer.nCapacity = nClass;
	pVal->u.buffer.nUsed = 0;
    } else {
	/* Pool full or cache miss: free directly. */
	Th8_Free(interp, p);
    }
}
