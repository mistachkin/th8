/*
 * th8_hash.c -- Hash table implementation for TH8.
 *
 * Simple chaining hash table with SipHash-2-4 keyed hashing for
 * resistance to algorithmic complexity (HashDoS) attacks.
 *
 * When compiled with TH8_HASH_STANDALONE defined, this file does
 * not depend on th8.h or any TH8 infrastructure.  Instead it uses
 * the C standard library directly, making the hash table usable
 * by non-TH8 projects.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifdef TH8_HASH_STANDALONE

#  include "th8_hash.h"
#  include "th8_mem.h"

/*
 * Standalone shims: map TH8 APIs to the C runtime.
 * Th8_Malloc must return zero-filled memory (like TH8's allocator).
 */

#  define Th8_Malloc(interp, n) th8_calloc(1, (n))
#  define Th8_Free(interp, p)   th8_free((void *)(p))
/*
 * AUDIT-OK[direct-libc-memcmp]: these macros ARE the platform
 * AUDIT-OK[direct-libc-memcpy]: bridge for standalone-hash-test
 * AUDIT-OK[direct-libc-strlen]: mode where no Th8_Platform layer
 * exists.  Outside standalone mode the regular Th8_Memcmp /
 * Th8_Memcpy / Th8_Strlen functions defined in th8_plat.c are in
 * effect; these macros are activated only by the TH8_HASH_STANDALONE
 * compile-time gate so that the hash module can be exercised in
 * isolation by a freestanding test harness.
 */
#  define Th8_Memcmp(interp, a, b, n)      memcmp((a), (b), (n))
#  define Th8_Memcpy(interp, d, s, n)      memcpy((d), (s), (n))
#  define Th8_Strlen(interp, s)            strlen((s))
#  define Th8_Qsort(interp, b, n, sz, cmp) qsort((b), (n), (sz), (cmp))

/*
 * No global mutex in standalone mode.  The caller is responsible
 * for external synchronization if needed.
 */

#  define th8GlobalMutexEnter(interp)
#  define th8GlobalMutexLeave(interp)

#else /* !TH8_HASH_STANDALONE */

#  include "th8_meta_defs.h"
#  include "th8_meta_libc.h"
#  include "th8.h"
#  include "th8_int.h"

/*
 * In normal (non-standalone) mode, Th8_Qsort maps to the
 * C library qsort, matching the standalone shim above.
 */
#  define Th8_Qsort(interp, b, n, sz, cmp) qsort((b), (n), (sz), (cmp))

#endif /* TH8_HASH_STANDALONE */


/*
 *----------------------------------------------------------------------
 *
 * Hash table internals.
 *
 *----------------------------------------------------------------------
 */

/* Th8_Hash struct and TH8_HASH_SIZE are in th8_hash.h. */


/*
 * Per-process random hash key, initialized once.  This prevents
 * algorithmic complexity attacks where an adversary crafts keys
 * that all hash to the same bucket.
 */

static th8_uint64_t th8HashSeed0 = 0x736f6d6570736575ULL;
static th8_uint64_t th8HashSeed1 = 0x646f72616e646f6dULL;
static int th8HashSeeded = 0;


/*
 *----------------------------------------------------------------------
 *
 * th8SeedHash --
 *
 *	Initialize the per-process SipHash random seed from the
 *	platform's xRandomBytes callback.  Called once from
 *	Th8_CreateInterp.  Thread-safe (uses global mutex).
 *
 * Why / How:
 *	A random per-process hash seed is essential to prevent
 *	algorithmic complexity (HashDoS) attacks.  Without it, an
 *	adversary can craft keys that all hash to the same bucket,
 *	degrading O(1) lookups to O(n).  The seed is drawn from the
 *	platform entropy source for cryptographic quality; in
 *	standalone mode a time-based fallback is used instead.  The
 *	global mutex ensures exactly-once initialization even when
 *	multiple threads create interpreters concurrently.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the global hash seeds th8HashSeed0 and th8HashSeed1.
 *
 *----------------------------------------------------------------------
 */

void
th8SeedHash(Th8_Platform *pPlatform)
{
    th8GlobalMutexEnter(NULL);
    if (th8HashSeeded) {
	th8GlobalMutexLeave(NULL);
	return;
    }
    th8HashSeeded = 1;
#ifndef TH8_HASH_STANDALONE
    th8MemBarrier(NULL);
#endif

#ifdef TH8_HASH_STANDALONE
    /*
     * Standalone mode: use a simple time-based seed.
     * The caller can also call this with NULL to just
     * mark the hash as seeded with the default keys.
     */

    (void)pPlatform;
    {
	unsigned long t = (unsigned long)time(0);

	th8HashSeed0 ^= (th8_uint64_t)t;
	th8HashSeed1 ^= (th8_uint64_t)(t * 2654435761UL);
    }
#else
    /* Nested per Finding 005 sec. 5b: both arms intrinsic-
     * dead in the test corpus (default platform always
     * provides xRandomBytes; pPlatform always non-NULL at
     * this call site). */
    if (pPlatform)
	if (pPlatform->xRandomBytes) {
	    unsigned char buf[16];

	    if (TH8_OK == pPlatform->xRandomBytes(NULL, NULL, buf, 16)) {
		size_t j;

		th8HashSeed0 = 0;
		th8HashSeed1 = 0;
		for (j = 0; j < 8; j++) {
		    th8HashSeed0 |= ((th8_uint64_t)buf[j]) << (j * 8);
		    th8HashSeed1 |= ((th8_uint64_t)buf[8 + j]) << (j * 8);
		}
	    }
	}
#endif

#ifndef TH8_HASH_STANDALONE
    th8MemBarrier(NULL);
#endif
    th8GlobalMutexLeave(NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * th8HashKey --
 *
 *	SipHash-2-4 keyed hash function resistant to algorithmic
 *	complexity (HashDoS) attacks.  Used by Python, Perl, Rust.
 *	Reference: https://131002.net/siphash/
 *
 * Why / How:
 *	SipHash-2-4 was chosen because it provides a strong PRF
 *	guarantee with a 128-bit key (the per-process seeds), making
 *	bucket prediction infeasible without knowing the key.  The
 *	algorithm processes input in 8-byte blocks with two SipRound
 *	compression passes per block, then four finalization rounds.
 *	The result is reduced modulo TH8_HASH_SIZE to yield the
 *	bucket index.
 *
 * Results:
 *	Bucket index in [0, TH8_HASH_SIZE).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#define SIPROUND                                                             \
    do {                                                                     \
	v0 += v1;                                                            \
	v1 = (v1 << 13) | (v1 >> 51);                                        \
	v1 ^= v0;                                                            \
	v0 = (v0 << 32) | (v0 >> 32);                                        \
	v2 += v3;                                                            \
	v3 = (v3 << 16) | (v3 >> 48);                                        \
	v3 ^= v2;                                                            \
	v0 += v3;                                                            \
	v3 = (v3 << 21) | (v3 >> 43);                                        \
	v3 ^= v0;                                                            \
	v2 += v1;                                                            \
	v1 = (v1 << 17) | (v1 >> 47);                                        \
	v1 ^= v2;                                                            \
	v2 = (v2 << 32) | (v2 >> 32);                                        \
    } while (0)


/*
 *----------------------------------------------------------------------
 *
 * th8HashKey --
 *
 *	Compute the SipHash-2-4 keyed hash of `zKey[0..nKey)`
 *	folded down to an `unsigned int`.  Used by the
 *	in-process hash tables to scatter strings into
 *	buckets.  The keys `th8HashSeed0` / `th8HashSeed1`
 *	are randomised at interpreter creation
 *	(`th8InitGlobals`) so an attacker cannot pre-compute
 *	collision-producing strings -- a hash-DoS hardening
 *	the original Tcl 8.x hash table lacks.
 *
 *	The implementation is the standard SipHash-2-4
 *	specification: 8-byte little-endian blocks are XOR'd
 *	into the rolling `v0..v3` state with two
 *	`SIPROUND` mixing rounds per block; the trailing
 *	partial block is XOR'd in and the state is finalised
 *	with four rounds.  Result is the low bits of
 *	`v0 ^ v1 ^ v2 ^ v3` cast to `unsigned int`.
 *
 * Parameters:
 *	zKey -- key bytes (not necessarily NUL-terminated).
 *	nKey -- key length in bytes.
 *
 * Returns:
 *	The keyed hash, truncated to `unsigned int`.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static unsigned int
th8HashKey(
    const char *zKey,  /* Key bytes. */
    size_t nKey)  /* Number of key bytes. */
{
    th8_uint64_t v0, v1, v2, v3, m;
    const unsigned char *p = (const unsigned char *)zKey;
    size_t i;
    size_t nBlocks = nKey / 8;
    th8_uint64_t b;

    v0 = th8HashSeed0 ^ 0x736f6d6570736575ULL;
    v1 = th8HashSeed1 ^ 0x646f72616e646f6dULL;
    v2 = th8HashSeed0 ^ 0x6c7967656e657261ULL;
    v3 = th8HashSeed1 ^ 0x7465646279746573ULL;

    for (i = 0; i < nBlocks; i++) {
	size_t off = i * 8;

	m = (th8_uint64_t)p[off] | ((th8_uint64_t)p[off + 1] << 8) |
	    ((th8_uint64_t)p[off + 2] << 16) |
	    ((th8_uint64_t)p[off + 3] << 24) |
	    ((th8_uint64_t)p[off + 4] << 32) |
	    ((th8_uint64_t)p[off + 5] << 40) |
	    ((th8_uint64_t)p[off + 6] << 48) |
	    ((th8_uint64_t)p[off + 7] << 56);
	v3 ^= m;
	SIPROUND;
	SIPROUND;
	v0 ^= m;
    }

    b = ((th8_uint64_t)nKey) << 56;
    p += nBlocks * 8;
    switch (nKey & 7) {
    case 7:
	b |= ((th8_uint64_t)p[6]) << 48; /* fall through */
    case 6:
	b |= ((th8_uint64_t)p[5]) << 40; /* fall through */
    case 5:
	b |= ((th8_uint64_t)p[4]) << 32; /* fall through */
    case 4:
	b |= ((th8_uint64_t)p[3]) << 24; /* fall through */
    case 3:
	b |= ((th8_uint64_t)p[2]) << 16; /* fall through */
    case 2:
	b |= ((th8_uint64_t)p[1]) << 8;  /* fall through */
    case 1:
	b |= ((th8_uint64_t)p[0]);
    }

    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;

    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    return (unsigned int)((v0 ^ v1 ^ v2 ^ v3) % TH8_HASH_SIZE);
}

#undef SIPROUND


/*
 *----------------------------------------------------------------------
 *
 * Th8_HashNew --
 *
 *	Create a new empty hash table.
 *
 * Why / How:
 *	Allocates a zero-filled Th8_Hash via Th8_AttemptMalloc.
 *	Because the TH8 allocator returns zeroed memory, all bucket
 *	pointers are initialized to NULL without an explicit memset.
 *
 * Results:
 *	Pointer to the new hash table, or NULL on allocation failure.
 *
 * Side effects:
 *	Allocates memory from the interpreter's allocator.
 *
 *----------------------------------------------------------------------
 */

Th8_Hash *
Th8_HashNew(Th8_Interp *interp) /* Interpreter for memory. */
{
    return (Th8_Hash *)TH8_ALLOC(interp, sizeof(Th8_Hash));
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_HashDelete --
 *
 *	Destroy a hash table, freeing all entries and keys.
 *	Data pointers (pData) are NOT freed.
 *
 * Why / How:
 *	Walks every bucket chain and frees each entry's key copy and
 *	the entry struct itself.  The pData pointer is left to the
 *	caller because the hash table does not know the ownership
 *	semantics of the stored data.  Finally the hash table itself
 *	is freed.  Safe to call with a NULL pHash.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all memory owned by the hash table (keys and entries).
 *
 *----------------------------------------------------------------------
 */

void
Th8_HashDelete(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Hash *pHash)  /* Hash table to destroy. */
{
    int i;

    if (pHash) {
	for (i = 0; i < TH8_HASH_SIZE; i++) {
	    Th8_HashEntry *p = pHash->aBucket[i];
	    while (p) {
		Th8_HashEntry *pNext = p->pNext;
		Th8_Free(interp, p->zKey);
		Th8_Free(interp, p);
		p = pNext;
	    }
	}
	Th8_Free(interp, pHash);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_HashFind --
 *
 *	Find, insert, or delete a hash table entry.
 *
 *	op <  0 : delete the entry if found
 *	op == 0 : lookup only (return NULL if not found)
 *	op >  0 : create if not found
 *
 * Why / How:
 *	Combines lookup, insert, and delete into a single function to
 *	avoid redundant bucket-chain walks.  The caller passes an op
 *	code that selects the behavior after the key is (or is not)
 *	found.  On insert, a NUL-terminated copy of the key is
 *	allocated so the caller does not need to keep the original
 *	alive.  On delete, the entry and its key copy are freed; the
 *	predecessor pointer is patched via a pointer-to-pointer walk.
 *
 * Results:
 *	Pointer to the found or newly created entry, or NULL if not
 *	found (op==0), after deletion (op<0), or on allocation failure.
 *
 * Side effects:
 *	May allocate or free memory.  May modify the bucket chain.
 *
 *----------------------------------------------------------------------
 */

Th8_HashEntry *
Th8_HashFind(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Hash *pHash,  /* Hash table. */
    const char *zKey,  /* Key bytes. */
    size_t nKey,  /* Key length (TH8_NOLEN = NUL-term). */
    int op)   /* <0 delete, 0 find, >0 insert. */
{
    unsigned int iBucket;
    Th8_HashEntry *p;
    Th8_HashEntry **pp;

    if (nKey == TH8_NOLEN) {
	nKey = Th8_Strlen(interp, zKey);
    }
    iBucket = th8HashKey(zKey, nKey);
    pp = &pHash->aBucket[iBucket];

    for (p = *pp; p; pp = &p->pNext, p = p->pNext) {
	if (p->nKey == nKey && 0 == Th8_Memcmp(interp, p->zKey, zKey, nKey)) {
	    if (op < 0) {
		/* Delete */
		*pp = p->pNext;
		Th8_Free(interp, p->zKey);
		Th8_Free(interp, p);
		return NULL;
	    }
	    return p;
	}
    }
    if (op > 0) {
	/* Insert */
	p = (Th8_HashEntry *)TH8_ALLOC(interp, sizeof(Th8_HashEntry));
	if (!p) return NULL;
	p->zKey = (char *)TH8_ALLOC_STR(interp, nKey);
	if (!p->zKey) {
	    Th8_Free(interp, p);
	    return NULL;
	}
	Th8_Memcpy(interp, p->zKey, zKey, nKey);
	p->zKey[nKey] = 0;
	p->nKey = nKey;
	p->pData = 0;
	p->nInsertOrder = pHash->nNextOrder++;
	p->pNext = pHash->aBucket[iBucket];
	pHash->aBucket[iBucket] = p;
	return p;
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_HashRemove --
 *
 *	Remove a single entry from a hash table.  Does NOT free
 *	pData (caller must handle that before calling).
 *
 * Why / How:
 *	Convenience wrapper around Th8_HashFind with op<0.  Exists
 *	so callers that only need deletion do not have to remember
 *	the op-code convention.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the entry's key copy and entry struct if found.
 *
 *----------------------------------------------------------------------
 */

void
Th8_HashRemove(
    Th8_Interp *interp,
    Th8_Hash *pHash,
    const char *zKey,
    size_t nKey)
{
    Th8_HashFind(interp, pHash, zKey, nKey, -1);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_HashIterate --
 *
 *	Call a function for each entry in a hash table.  If the
 *	callback returns a value other than TH8_OK, iteration stops
 *	early.
 *
 * Why / How:
 *	Iterates all buckets sequentially, following each chain.
 *	A snapshot of pNext is taken before each callback invocation
 *	so the callback may safely delete the current entry without
 *	corrupting the walk.  The early-exit mechanism allows callers
 *	to implement search-and-stop patterns efficiently.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the callback does (may modify or delete entries).
 *
 *----------------------------------------------------------------------
 */

void
Th8_HashIterate(
    Th8_Interp *interp, /* Interpreter (unused). */
    Th8_Hash *pHash,  /* Hash table. */
    int (*xCb)(Th8_HashEntry *, void *),
                                /* Callback for each entry.
				 * Return TH8_OK to continue,
				 * any other value to stop. */
    void *pCtx)   /* Context for callback. */
{
    int i;

    (void)interp;

    for (i = 0; i < TH8_HASH_SIZE; i++) {
	Th8_HashEntry *p = pHash->aBucket[i];
	while (p) {
	    Th8_HashEntry *pNext = p->pNext;
	    if (xCb(p, pCtx) != TH8_OK) {
		return;
	    }
	    p = pNext;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8CompareInsertOrder --
 *
 *	qsort comparator for Th8_HashEntry pointers, ordering by
 *	nInsertOrder ascending.
 *
 *----------------------------------------------------------------------
 */

static int
th8CompareInsertOrder(const void *a, const void *b)
{
    const Th8_HashEntry *pa = *(const Th8_HashEntry *const *)a;
    const Th8_HashEntry *pb = *(const Th8_HashEntry *const *)b;

    if (pa->nInsertOrder < pb->nInsertOrder) return -1;
    if (pa->nInsertOrder > pb->nInsertOrder) return 1;
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_HashIterateOrdered --
 *
 *	Iterate over hash entries in insertion order.  Collects all
 *	entries into a temporary array, sorts by nInsertOrder, then
 *	calls the callback for each entry in order.
 *
 *	Used by dict commands to preserve key insertion order.
 *	Non-dict code should use Th8_HashIterate (faster, no sort).
 *
 *----------------------------------------------------------------------
 */

void
Th8_HashIterateOrdered(
    Th8_Interp *interp,
    Th8_Hash *pHash,
    int (*xCb)(Th8_HashEntry *, void *),
    void *pCtx)
{
    Th8_HashEntry **apEntry;
    int nEntry = 0;
    int nAlloc;
    int i;

    nAlloc = pHash->nNextOrder;
    if (nAlloc <= 0) return;

    apEntry = (Th8_HashEntry **)
        TH8_ALLOC_MUL(interp, (size_t)nAlloc, sizeof(Th8_HashEntry *));
    if (!apEntry) {
	Th8_HashIterate(interp, pHash, xCb, pCtx);
	return;
    }

    for (i = 0; i < TH8_HASH_SIZE; i++) {
	Th8_HashEntry *p;
	for (p = pHash->aBucket[i]; p; p = p->pNext) {
	    if (nEntry < nAlloc) {
		apEntry[nEntry++] = p;
	    }
	}
    }

    if (nEntry > 1) {
	Th8_Qsort(
	    interp, apEntry, (size_t)nEntry, sizeof(Th8_HashEntry *),
	    th8CompareInsertOrder);
    }

    for (i = 0; i < nEntry; i++) {
	if (xCb(apEntry[i], pCtx) != TH8_OK) break;
    }

    Th8_Free(interp, apEntry);
}
