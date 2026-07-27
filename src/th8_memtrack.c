/*
 * th8_memtrack.c --
 *
 *	Allocation-site memory tracker for TH8.  Under a TH8_MEM_DEBUG
 *	build, every allocation that passes through the central funnel
 *	(Th8_Malloc / Th8_Realloc / Th8_Free and their internal callers)
 *	is recorded in a process-global side-table keyed by address,
 *	storing the block size and a captured native stack trace.
 *	th8MemTrackDump() writes every currently-live allocation to a
 *	file -- address, size, and the symbolized stack trace, grouped
 *	by unique call stack -- the "memory active" equivalent from
 *	native Tcl, upgraded from one call site to a full stack trace,
 *	and working with or without mimalloc and with or without
 *	Valgrind.
 *
 *	Design contract: docs/internal/design_notes_memtrack.md.
 *
 *	Robustness (see the design note for the full R1..R11 rationale):
 *	  * Zero production footprint -- the collector is entirely behind
 *	    TH8_MEM_DEBUG; a normal build compiles only the fail-soft
 *	    th8MemTrackDump stub and the funnel is byte-for-byte unchanged.
 *	  * No allocation on the capture path -- the stack walk writes into
 *	    a fixed stack buffer; symbolization (which allocates) happens
 *	    only at dump time.
 *	  * Re-entrancy safety -- the tracker's own bookkeeping (side-table
 *	    nodes, interned traces) is allocated through the STABLE
 *	    process-global platform's xMalloc callback DIRECTLY, below the
 *	    tracked funnel (th8MallocCommon), so it never recurses into the
 *	    tracker and never depends on a transient per-interp platform; plus
 *	    a thread-local recursion guard; plus capturing the trace BEFORE
 *	    taking the table lock (so the compiler unwind runtime's lazy
 *	    first-call dlopen/malloc never runs under the lock).
 *	  * Deadlock safety -- the table is serialized by TH8's process-global
 *	    mutex (th8GlobalMutexEnter/Leave, a blocking pthread_mutex /
 *	    CRITICAL_SECTION behind the platform xMutex* callbacks), never a
 *	    spinlock; on the allocation hot path it is a leaf lock, held only
 *	    for the brief table mutation and never across the capture callback.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_meta_posix.h"
#include "th8_plat.h" /* real Th8_Mutex typedef; must precede th8.h */
#include "th8.h"
#include "th8_int.h"
/* th8_int_core.h defines struct Th8_Interp; the tracker reads
 * interp->pPlatform on the interp's own thread from the allocation funnel. */
#include "th8_int_core.h"

#if !defined(TH8_MEM_DEBUG)

/*
 *----------------------------------------------------------------------
 *
 * th8MemTrackDump --
 *
 *	Fail-soft stub used when TH8 is not built with TH8_MEM_DEBUG.
 *	The symbol exists unconditionally so the internal-stubs table
 *	(and thus the test-library dump command) always resolves; it
 *	simply reports that a debug build is required.
 *
 * Results:
 *	TH8_ERROR, with an explanatory interpreter result.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
th8MemTrackDump(Th8_Interp *interp, const char *zPath, size_t nPath)
{
    (void)zPath;
    (void)nPath;
    Th8_SetResultStatic(
        interp, "memory dump requires a TH8_MEM_DEBUG build", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemTrackReset --
 *
 *	Fail-soft stub used when TH8 is not built with TH8_MEM_DEBUG.
 *	There is no tracker state to clear in a normal build, so this is
 *	a successful no-op (unlike the dump, a reset is meaningful as
 *	"ensure the tracker holds nothing", which is trivially true here).
 *	The symbol exists unconditionally so the internal-stubs table
 *	(and thus the test-library reset command) always resolves.
 *
 * Results:
 *	TH8_OK; interpreter result set to "0" (blocks cleared).
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
th8MemTrackReset(Th8_Interp *interp)
{
    Th8_SetResultStatic(interp, "0", TH8_NOLEN);
    return TH8_OK;
}

#else /* TH8_MEM_DEBUG */

/*
 * All system facilities used below come from the platform meta headers
 * included above -- never direct libc/POSIX/Win32 headers.  Allocation,
 * byte operations, and locking all go through the stable process-global
 * platform's callbacks (xMalloc / xFree for bookkeeping, xMemcpy / xMemset
 * / xMemcmp via the th8MemCopy/Zero/Cmp helpers, and th8GlobalMutexEnter /
 * th8GlobalMutexLeave for the lock), so the meta headers supply only:
 *   th8_meta_libc.h  -> size_t / NULL (stddef)
 *   th8_meta_posix.h -> dladdr (dump-time symbolization, POSIX/macOS only;
 *                       empty on Windows, where the raw PC is written)
 */

/*
 * Tunables.  TH8_MEM_MAX_FRAMES caps capture depth (R9 -- bounded
 * footprint).  TH8_MEM_SKIP_FRAMES drops the innermost frames that
 * belong to the tracker itself, so a captured trace begins at the
 * allocation funnel (th8MallocCommon / th8ReallocCommon) rather than
 * inside the tracker.  Those three frames are, innermost first:
 *   0  the platform capture function (th8UnwindStackBackTrace /
 *      th8Win32StackBackTrace), which calls the unwinder;
 *   1  th8MemCapture, the tracker's capture wrapper;
 *   2  th8MemTrackAlloc / th8MemTrackRealloc, the funnel hook.
 * memdebug is an -O0 build, so none of these are inlined and the skip
 * count is stable.  It is only a cosmetic tuning knob: an off-by-one
 * from unexpected inlining changes which frames show, never correctness.
 */

#  define TH8_MEM_MAX_FRAMES    16
#  define TH8_MEM_SKIP_FRAMES   3
#  define TH8_MEM_ADDR_BUCKETS  4093 /* prime */
#  define TH8_MEM_TRACE_BUCKETS 1021 /* prime */
#  define TH8_MEM_PATH_MAX      4096

/*
 * An interned stack trace.  Each unique captured stack is stored once
 * and referenced by many address nodes, so the tracker's footprint
 * stays bounded even under millions of allocations sharing call sites.
 */

typedef struct th8MemTrace {
    struct th8MemTrace *pNext; /* Chain within g_aTrace bucket. */
    unsigned int id; /* Small stable trace id (for the dump). */
    unsigned int hash; /* Cached hash of aFrames[0..nFrames). */
    int nFrames; /* Number of valid entries in aFrames. */
    void *aFrames[TH8_MEM_MAX_FRAMES]; /* Raw return-address PCs. */
    /* Transient dump accumulators (valid only during a dump). */
    unsigned int nDumpBlocks;
    th8_uint64_t nDumpBytes;
} th8MemTrace;

/*
 * A live-allocation record: address -> { size, interned trace }.
 */

typedef struct th8MemNode {
    struct th8MemNode *pNext; /* Chain within g_aAddr bucket. */
    void *pAddr; /* Allocated block (hash key). */
    size_t nByte; /* Recorded block size. */
    th8MemTrace *pTrace; /* Originating call stack. */
} th8MemNode;

static th8MemNode *g_aAddr[TH8_MEM_ADDR_BUCKETS];
static th8MemTrace *g_aTrace[TH8_MEM_TRACE_BUCKETS];
static th8_uint64_t g_nLiveBytes;
static th8_uint64_t g_nLiveBlocks;
static unsigned int g_nNextTraceId = 1;

/* Thread-local re-entrancy guard (R3): any nested entry short-circuits. */
static TH8_TLS int g_inHook = 0;

/*
 * The stable, process-global platform (a struct copy taken in
 * Th8_Initialize).  ALL tracker bookkeeping -- allocation, byte ops, and
 * stack capture -- goes through this rather than the per-allocation
 * interp's platform, which may be a transient stack local (see
 * th8MemRawAlloc).
 */
extern Th8_Platform th8GlobalPlatform;

/*
 * The side-table is serialized by TH8's own process-global mutex,
 * th8GlobalMutexEnter / th8GlobalMutexLeave (design R4).  This reuses the
 * platform threading abstraction (a blocking pthread_mutex / CRITICAL_SECTION
 * behind xMutexEnter / xMutexLeave) instead of a raw, compiler-specific
 * mutex, so th8_memtrack.c stays portable and audit-clean.  It is created
 * once during Th8_Initialize, and th8GlobalMutexEnter/Leave no-op until then
 * (fail-soft: the pre-init window is single-threaded).
 *
 * Deadlock freedom: every other holder of th8GlobalMutex uses the documented
 * "copy pointers under the lock, release, then call" discipline and never
 * allocates while holding it, so an allocation can never re-enter this lock
 * on the same thread.  The tracker's own bookkeeping allocations go to the
 * platform xMalloc, which does not take th8GlobalMutex.  The thread-local
 * g_inHook guard remains as belt-and-suspenders against tracker recursion.
 */


/*
 *----------------------------------------------------------------------
 *
 * th8MemAddrHash --
 *
 *	Return the g_aAddr bucket index for a block pointer.
 *
 * Why / How:
 *	Drops the low alignment bits, multiplies by the 64-bit golden-ratio
 *	constant to diffuse the surviving bits, and reduces modulo the
 *	(prime) bucket count.  Used to index the live-allocation table.
 *
 * Results:
 *	A bucket index in [0, TH8_MEM_ADDR_BUCKETS).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
th8MemAddrHash(void *pAddr)
{
    th8_uint64_t x = (th8_uint64_t)(size_t)pAddr;

    /* Drop the low bits (allocation alignment) then mix. */
    x >>= 4;
    x *= (th8_uint64_t)0x9E3779B97F4A7C15ULL;
    return (unsigned int)(x % TH8_MEM_ADDR_BUCKETS);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemTraceHash --
 *
 *	Return an FNV-1a hash over a captured frame array, used to intern
 *	identical call stacks in the trace table.
 *
 * Why / How:
 *	Feeds each frame pointer, byte by byte, through the FNV-1a mix so
 *	two identical stacks hash equal (and are then confirmed by an exact
 *	frame compare in th8MemInternTrace).  Not reduced modulo the bucket
 *	count here -- the caller does that -- so the full hash can also be
 *	cached on the trace for a fast pre-check.
 *
 * Results:
 *	A 32-bit hash of aFrames[0..nFrames).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
th8MemTraceHash(void *const *aFrames, int nFrames)
{
    unsigned int h = 2166136261u; /* FNV-1a offset basis. */
    int i;
    size_t j;

    for (i = 0; i < nFrames; i++) {
	size_t v = (size_t)aFrames[i];
	for (j = 0; j < sizeof(v); j++) {
	    h ^= (unsigned int)(v & 0xFF);
	    h *= 16777619u;
	    v >>= 8;
	}
    }
    return h;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemCapture --
 *
 *	Capture the current native stack (up to TH8_MEM_MAX_FRAMES PCs,
 *	skipping TH8_MEM_SKIP_FRAMES tracker frames) into aFrames.  Uses
 *	the stable process-global platform's xStackBackTrace callback when
 *	present; otherwise degrades to the immediate caller (R5/R6 -- never
 *	fail).  Runs BEFORE the table lock is taken, so any lazy first-call
 *	unwind setup happens outside the lock.
 *
 * Results:
 *	Number of frames captured (0 if none available).
 *
 * Side effects:
 *	None (writes only the caller-provided buffer).
 *
 *----------------------------------------------------------------------
 */

static int
th8MemCapture(void **aFrames, int nMax)
{
    if (th8GlobalPlatform.xStackBackTrace != NULL) {
	int n = th8GlobalPlatform.xStackBackTrace(
	    NULL, th8GlobalPlatform.pCtx, aFrames, nMax, TH8_MEM_SKIP_FRAMES);
	if (n > 0) {
	    return n;
	}
    }
    /* Fallback: immediate caller only, best effort. */
#  if defined(__GNUC__) || defined(__clang__)
    if (nMax > 0) {
	aFrames[0] = __builtin_return_address(0);
	return (aFrames[0] != NULL) ? 1 : 0;
    }
#  endif
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemRawAlloc --
 *
 *	Allocate the tracker's own bookkeeping (side-table nodes, interned
 *	traces, the dump snapshot) through the STABLE process-global
 *	platform's xMalloc callback -- NOT the per-allocation interp's
 *	platform, and NOT Th8_Malloc / th8MallocCommon.
 *
 * Why / How:
 *	Two independent reasons:
 *
 *	  1. The re-entrant layer is the tracked funnel (th8MallocCommon,
 *	     which calls th8MemTrackAlloc), not the platform allocator
 *	     beneath it.  Going straight to xMalloc keeps the tracker's
 *	     bookkeeping out of the funnel (no recursion, no double-count)
 *	     and off the sandbox allocation limit.
 *
 *	  2. The side-table is PROCESS-GLOBAL and outlives individual
 *	     interpreters.  A per-interp platform pointer is NOT owned by
 *	     the interp (Th8_CreateInterp stores it by reference) and may
 *	     be a transient stack local -- e.g. test code that creates a
 *	     child interp from a `Th8_Platform` on its stack.  Allocating
 *	     process-lifetime bookkeeping through such a platform is
 *	     unsound: once it goes away, its xMalloc pointer is garbage.
 *	     th8GlobalPlatform is a stable copy taken in Th8_Initialize,
 *	     so it is always valid for the process lifetime and gives the
 *	     tracker a single, consistent allocator.  The thread-local
 *	     g_inHook guard remains as belt-and-suspenders.
 *
 * Results:
 *	Pointer to nByte zero-or-more bytes, or NULL on failure / before
 *	Th8_Initialize (when the global platform is not yet set).
 *
 * Side effects:
 *	Allocates from the process-global platform allocator.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MemRawAlloc(size_t nByte)
{
    if (th8GlobalPlatform.xMalloc == NULL) {
	return NULL;
    }
    return th8GlobalPlatform.xMalloc(NULL, th8GlobalPlatform.pCtx, nByte);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemRawFree --
 *
 *	Free bookkeeping obtained from th8MemRawAlloc, through the stable
 *	process-global platform's xFree callback (see th8MemRawAlloc for
 *	why the tracker never uses the per-interp platform or Th8_Free).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the block through the process-global platform allocator.
 *	NULL p is ignored.
 *
 *----------------------------------------------------------------------
 */

static void
th8MemRawFree(void *p)
{
    if (p == NULL || th8GlobalPlatform.xFree == NULL) {
	return;
    }
    th8GlobalPlatform.xFree(NULL, th8GlobalPlatform.pCtx, p);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemCopy --
 *
 *	Copy n bytes between the tracker's OWN structures via the stable
 *	process-global platform's xMemcpy (never a per-interp Th8_Memcpy,
 *	for the same stable-platform reason as th8MemRawAlloc).  A no-op if
 *	n is zero or the callback is unavailable (only possible before
 *	Th8_Initialize, where the tracker does not run).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes n bytes to dst.
 *
 *----------------------------------------------------------------------
 */

static void
th8MemCopy(void *dst, const void *src, size_t n)
{
    if (n != 0 && th8GlobalPlatform.xMemcpy != NULL) {
	th8GlobalPlatform.xMemcpy(NULL, th8GlobalPlatform.pCtx, dst, src, n);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemZero --
 *
 *	Zero n bytes of a tracker structure via the stable process-global
 *	platform's xMemset (see th8MemCopy).  A no-op if n is zero or the
 *	callback is unavailable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Zeroes n bytes at dst.
 *
 *----------------------------------------------------------------------
 */

static void
th8MemZero(void *dst, size_t n)
{
    if (n != 0 && th8GlobalPlatform.xMemset != NULL) {
	th8GlobalPlatform.xMemset(NULL, th8GlobalPlatform.pCtx, dst, 0, n);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemCmp --
 *
 *	Compare n bytes of two tracker structures via the stable
 *	process-global platform's xMemcmp (see th8MemCopy).  If the
 *	callback is unavailable it reports "equal" for a zero-length
 *	compare and "not equal" otherwise -- a safe default that only
 *	applies before Th8_Initialize, where the tracker does not run.
 *
 * Results:
 *	<0, 0, or >0 like memcmp.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8MemCmp(const void *a, const void *b, size_t n)
{
    if (th8GlobalPlatform.xMemcmp != NULL) {
	return th8GlobalPlatform
	    .xMemcmp(NULL, th8GlobalPlatform.pCtx, a, b, n);
    }
    return n == 0 ? 0 : 1;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemInternTrace --
 *
 *	Return the interned th8MemTrace for a captured frame array,
 *	creating a new one (via th8MemRawAlloc) if this stack is not yet
 *	known.  Must be called with the table lock held.  On allocation
 *	failure returns NULL (the caller still records the block,
 *	traceless).
 *
 *----------------------------------------------------------------------
 */

static th8MemTrace *
th8MemInternTrace(void *const *aFrames, int nFrames)
{
    unsigned int h = th8MemTraceHash(aFrames, nFrames);
    unsigned int bucket = h % TH8_MEM_TRACE_BUCKETS;
    th8MemTrace *pTr;
    size_t nBytes;

    /* nFrames is capped at TH8_MEM_MAX_FRAMES; the checked multiply is
       for an audit-clean size computation, not a real overflow risk. */
    if (TH8_SAFE_MUL_SIZE((size_t)nFrames, sizeof(void *), &nBytes)) {
	return NULL;
    }
    for (pTr = g_aTrace[bucket]; pTr != NULL; pTr = pTr->pNext) {
	if (pTr->hash == h && pTr->nFrames == nFrames &&
	    th8MemCmp(pTr->aFrames, aFrames, nBytes) == 0) {
	    return pTr;
	}
    }
    pTr = (th8MemTrace *)th8MemRawAlloc(sizeof(*pTr));
    if (pTr == NULL) {
	return NULL;
    }
    th8MemZero(pTr, sizeof(*pTr));
    pTr->id = g_nNextTraceId++;
    pTr->hash = h;
    pTr->nFrames = nFrames;
    if (nBytes > 0) {
	th8MemCopy(pTr->aFrames, aFrames, nBytes);
    }
    pTr->pNext = g_aTrace[bucket];
    g_aTrace[bucket] = pTr;
    return pTr;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemUnlink --
 *
 *	Remove and free the address node for pAddr, if present, updating
 *	the running live totals.  Must be called with the lock held.
 *	Returns non-zero if a node was removed.  Interned traces are NOT
 *	freed (they are shared and cheap to retain for the process life).
 *
 *----------------------------------------------------------------------
 */

static int
th8MemUnlink(void *pAddr)
{
    unsigned int bucket = th8MemAddrHash(pAddr);
    th8MemNode **ppN = &g_aAddr[bucket];
    th8MemNode *pN;

    while ((pN = *ppN) != NULL) {
	if (pN->pAddr == pAddr) {
	    *ppN = pN->pNext;
	    if (pN->nByte <= g_nLiveBytes) {
		g_nLiveBytes -= pN->nByte;
	    } else {
		g_nLiveBytes = 0;
	    }
	    if (g_nLiveBlocks > 0) {
		g_nLiveBlocks--;
	    }
	    th8MemRawFree(pN);
	    return 1;
	}
	ppN = &pN->pNext;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemTrackAlloc --
 *
 *	Record a freshly allocated block (pAddr, nByte) with the current
 *	captured stack.  Called from th8MallocCommon after a successful
 *	xMalloc.  No-op if pAddr is NULL or if already inside a tracker
 *	hook on this thread.
 *
 *----------------------------------------------------------------------
 */

void
th8MemTrackAlloc(Th8_Interp *interp, void *pAddr, size_t nByte)
{
    void *aFrames[TH8_MEM_MAX_FRAMES];
    int nFrames;
    th8MemNode *pN;
    unsigned int bucket;

    /* The per-allocation interp is intentionally ignored: the tracker
       operates entirely through the stable process-global platform (see
       th8MemRawAlloc) and locks via th8GlobalMutexEnter(NULL). */
    (void)interp;
    if (pAddr == NULL || g_inHook) {
	return;
    }
    g_inHook = 1;

    /* Capture outside the lock (leaf-lock discipline, R4). */
    nFrames = th8MemCapture(aFrames, TH8_MEM_MAX_FRAMES);

    pN = (th8MemNode *)th8MemRawAlloc(sizeof(*pN));
    if (pN != NULL) {
	th8GlobalMutexEnter(NULL);
	pN->pAddr = pAddr;
	pN->nByte = nByte;
	pN->pTrace = th8MemInternTrace(aFrames, nFrames);
	bucket = th8MemAddrHash(pAddr);
	pN->pNext = g_aAddr[bucket];
	g_aAddr[bucket] = pN;
	g_nLiveBytes += nByte;
	g_nLiveBlocks++;
	th8GlobalMutexLeave(NULL);
    }
    g_inHook = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemTrackRealloc --
 *
 *	Update the side-table for a successful realloc: drop the old
 *	block record (pOld, if any) and record the new block (pNew,
 *	nByte) with the realloc call site's stack.  Called from
 *	th8ReallocCommon after a successful xRealloc.  Handles the
 *	in-place case (pOld == pNew) correctly by unlinking then
 *	re-inserting.
 *
 *----------------------------------------------------------------------
 */

void
th8MemTrackRealloc(Th8_Interp *interp, void *pOld, void *pNew, size_t nByte)
{
    void *aFrames[TH8_MEM_MAX_FRAMES];
    int nFrames;
    th8MemNode *pN;
    unsigned int bucket;

    (void)interp; /* ignored -- see th8MemTrackAlloc. */
    if (pNew == NULL || g_inHook) {
	return;
    }
    g_inHook = 1;

    nFrames = th8MemCapture(aFrames, TH8_MEM_MAX_FRAMES);

    pN = (th8MemNode *)th8MemRawAlloc(sizeof(*pN));
    if (pN != NULL) {
	th8GlobalMutexEnter(NULL);
	if (pOld != NULL) {
	    (void)th8MemUnlink(pOld);
	}
	pN->pAddr = pNew;
	pN->nByte = nByte;
	pN->pTrace = th8MemInternTrace(aFrames, nFrames);
	bucket = th8MemAddrHash(pNew);
	pN->pNext = g_aAddr[bucket];
	g_aAddr[bucket] = pN;
	g_nLiveBytes += nByte;
	g_nLiveBlocks++;
	th8GlobalMutexLeave(NULL);
    }
    g_inHook = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemTrackFree --
 *
 *	Drop the side-table record for pAddr.  Called from Th8_Free
 *	before the underlying xFree.  Freeing an untracked address
 *	(e.g. one allocated via a direct xMalloc that bypassed the
 *	funnel) is a silent no-op -- never an error.
 *
 *----------------------------------------------------------------------
 */

void
th8MemTrackFree(Th8_Interp *interp, void *pAddr)
{
    (void)interp; /* ignored -- see th8MemTrackAlloc. */
    if (pAddr == NULL || g_inHook) {
	return;
    }
    g_inHook = 1;
    th8GlobalMutexEnter(NULL);
    (void)th8MemUnlink(pAddr);
    th8GlobalMutexLeave(NULL);
    g_inHook = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemTrackReset --
 *
 *	Free every side-table node and interned trace held in the
 *	tracker's static storage and reset the running counters, leaving
 *	the tracker empty (as if freshly armed).  Returns the number of
 *	live blocks that were being tracked at reset time.
 *
 * Why / How:
 *	The side-table nodes and interned traces are allocated from the
 *	process-global platform and are otherwise retained for the whole
 *	process lifetime (traces are never freed on the hot path, by
 *	design).  A test that wants to measure a bounded operation in
 *	isolation -- or that wants the tracker itself to hold nothing at
 *	shutdown so it is not reported as a leak by an outer tool -- needs
 *	a way to release that storage.  This walks both hash tables under
 *	the global mutex, frees each node via the platform xFree (matching
 *	how they were allocated), NULLs the buckets, and zeroes the byte /
 *	block totals and the trace-id counter.  It does NOT touch the
 *	application memory the nodes described -- that is owned by TH8 /
 *	the caller -- only the tracker's own bookkeeping.
 *
 *	The thread-local g_inHook guard is raised so any allocation that
 *	somehow occurred underneath (there is none -- xFree does not route
 *	through the tracked funnel) would short-circuit rather than mutate
 *	the tables mid-walk.
 *
 * Results:
 *	TH8_OK; interpreter result set to the live block count cleared.
 *
 * Side effects:
 *	Frees all tracker bookkeeping; empties the static tables; sets the
 *	interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
th8MemTrackReset(Th8_Interp *interp)
{
    char line[64];
    th8_uint64_t nCleared;
    int i;
    int n;

    if (g_inHook) {
	Th8_SetResultStatic(interp, "0", TH8_NOLEN);
	return TH8_OK;
    }
    g_inHook = 1;
    th8GlobalMutexEnter(NULL);

    for (i = 0; i < TH8_MEM_ADDR_BUCKETS; i++) {
	th8MemNode *pN = g_aAddr[i];
	while (pN != NULL) {
	    th8MemNode *pNext = pN->pNext;
	    th8MemRawFree(pN);
	    pN = pNext;
	}
	g_aAddr[i] = NULL;
    }
    for (i = 0; i < TH8_MEM_TRACE_BUCKETS; i++) {
	th8MemTrace *pTr = g_aTrace[i];
	while (pTr != NULL) {
	    th8MemTrace *pNext = pTr->pNext;
	    th8MemRawFree(pTr);
	    pTr = pNext;
	}
	g_aTrace[i] = NULL;
    }
    nCleared = g_nLiveBlocks;
    g_nLiveBytes = 0;
    g_nLiveBlocks = 0;
    g_nNextTraceId = 1;

    th8GlobalMutexLeave(NULL);
    g_inHook = 0;

    n = th8Snprintf(interp, line, sizeof(line), "%lld", (long long)nCleared);
    if (n > 0 && (size_t)n < sizeof(line)) {
	Th8_SetResult(interp, line, (size_t)n);
    } else {
	Th8_SetResultStatic(interp, "0", TH8_NOLEN);
    }
    return TH8_OK;
}


/*
 * A snapshot of one live-trace group, captured under the table lock so
 * the actual file writing can happen lock-free (see th8MemTrackDump).
 * pTrace stays valid after the lock is dropped because interned traces
 * are never freed for the process lifetime.
 */

typedef struct th8MemDumpGroup {
    th8MemTrace *pTrace; /* Immutable interned trace (frames, id). */
    unsigned int nBlocks; /* Live block count at snapshot time. */
    th8_uint64_t nBytes; /* Live byte total at snapshot time. */
} th8MemDumpGroup;


/*
 *----------------------------------------------------------------------
 *
 * th8MemChanWrite --
 *
 *	Write n bytes to an open platform channel via TH8_CHANCTL_WRITE.
 *
 * Why / How:
 *	The dump's file I/O goes through the platform channel layer -- never
 *	direct stdio -- so it honours the same xOutput / channel discipline
 *	as the rest of TH8.  A zero-length write is skipped.
 *
 * Results:
 *	None (write errors are ignored -- a partial dump is best-effort).
 *
 * Side effects:
 *	Writes to the channel.
 *
 *----------------------------------------------------------------------
 */

static void
th8MemChanWrite(
    Th8_Interp *interp,
    const Th8_Platform *pPlat,
    void *pChan,
    const char *z,
    size_t n)
{
    if (n == 0) {
	return;
    }
    (void)pPlat->xChannelControl(
        interp, pPlat->pCtx, pChan, TH8_CHANCTL_WRITE, (th8_int64_t)n, 0, 0,
        (void *)z);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemChanPuts --
 *
 *	NUL-terminated convenience form of th8MemChanWrite: write the
 *	C string z (excluding its terminator) to the channel.
 *
 * Why / How:
 *	Computes the length with its own loop rather than a direct libc
 *	strlen (keeping this file free of direct CRT calls), then delegates
 *	to th8MemChanWrite.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the channel.
 *
 *----------------------------------------------------------------------
 */

static void
th8MemChanPuts(
    Th8_Interp *interp,
    const Th8_Platform *pPlat,
    void *pChan,
    const char *z)
{
    size_t n = 0;

    while (z[n] != '\0') {
	n++;
    }
    th8MemChanWrite(interp, pPlat, pChan, z, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemWriteFrame --
 *
 *	Write one symbolized stack frame to the open channel.  Best
 *	effort (R6): on POSIX/macOS dladdr supplies "symbol+offset
 *	(module)"; static functions or failed lookups fall back to the
 *	raw PC.  On Windows the raw PC is written (dbghelp symbolization
 *	is deferred -- see the note in th8MemTrackDump).
 *
 *----------------------------------------------------------------------
 */

static void
th8MemWriteFrame(
    Th8_Interp *interp,
    const Th8_Platform *pPlat,
    void *pChan,
    void *pc)
{
    char line[256];
    int n;

#  if defined(_WIN32) || defined(WIN32)
    n = th8Snprintf(
        interp, line, sizeof(line), "      0x%llx\n",
        (unsigned long long)(size_t)pc);
#  else
    Dl_info info;

    if (dladdr(pc, &info) && info.dli_sname != NULL) {
	unsigned long off = (unsigned long)((char *)pc -
	                                    (char *)info.dli_saddr);
	n = th8Snprintf(
	    interp, line, sizeof(line), "      0x%llx %s+0x%lx (%s)\n",
	    (unsigned long long)(size_t)pc, info.dli_sname, off,
	    info.dli_fname ? info.dli_fname : "?");
    } else {
	n = th8Snprintf(
	    interp, line, sizeof(line), "      0x%llx <unknown>\n",
	    (unsigned long long)(size_t)pc);
    }
#  endif
    if (n > 0 && (size_t)n < sizeof(line)) {
	th8MemChanWrite(interp, pPlat, pChan, line, (size_t)n);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemTrackDump --
 *
 *	Write every currently-live allocation to zPath, grouped by
 *	unique call stack: for each distinct trace, one header line with
 *	the block count and byte total, followed by its symbolized
 *	frames.
 *
 *	Two robustness choices shape the structure:
 *
 *	  * Create-exclusive.  The path MUST NOT already exist.  The
 *	    platform channel OPEN exposes no O_EXCL mode, so existence is
 *	    probed with an open-for-read first; a benign TOCTOU window is
 *	    acceptable for a test-only diagnostic and still prevents a
 *	    test from clobbering an existing file.
 *
 *	  * Snapshot-then-write.  The live set is snapshotted into a
 *	    platform-allocated array while the table lock is held; the file
 *	    is then written WITHOUT the lock.  This is deliberate: writing
 *	    through the platform channel while holding the global mutex would
 *	    risk a self-deadlock if any write path allocated and re-entered
 *	    the tracker.  Interned traces are never freed, so the snapshot's
 *	    trace pointers stay valid after the lock is dropped.
 *
 *	NOTE: the mimalloc self-cross-check (design R10,
 *	mi_heap_visit_blocks) is not yet wired -- TH8's per-thread
 *	mimalloc heaps are not centrally enumerable from here.  The dump
 *	reports its own authoritative live totals; the test library's
 *	logical-list-vs-side-table comparison provides the near-term
 *	cross-check (design section 3.4).
 *
 * Results:
 *	TH8_OK on success (interpreter result = live block count);
 *	TH8_ERROR with a message if the path is unusable.
 *
 * Side effects:
 *	Creates and writes zPath through the platform channel layer.
 *	Sets the interpreter result.  Resets each trace's transient dump
 *	accumulators.
 *
 *----------------------------------------------------------------------
 */

int
th8MemTrackDump(Th8_Interp *interp, const char *zPath, size_t nPath)
{
    char zBuf[TH8_MEM_PATH_MAX];
    char line[128];
    const Th8_Platform *pPlat;
    void *pChan;
    th8_int64_t handle;
    th8MemDumpGroup *pGroups = NULL;
    int nGroups = 0;
    int iGroup;
    int i;
    int n;
    th8_uint64_t nLiveBytes;
    th8_uint64_t nLiveBlocks;

    if (interp == NULL) {
	return TH8_ERROR;
    }
    pPlat = interp->pPlatform;
    if (zPath == NULL || nPath == 0 || nPath >= sizeof(zBuf)) {
	Th8_SetResultStatic(
	    interp, "memory dump: invalid or too-long path", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (pPlat == NULL || pPlat->xChannelControl == NULL) {
	Th8_SetResultStatic(
	    interp, "memory dump: channel I/O unavailable", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, zBuf, zPath, nPath);
    zBuf[nPath] = '\0';

    /* Create-exclusive (approximated): reject an existing path. */
    handle = 0;
    if (pPlat->xChannelControl(
            interp, pPlat->pCtx, NULL, TH8_CHANCTL_OPEN, (th8_int64_t)nPath,
            0, &handle, zBuf) == TH8_OK) {
	(void)pPlat->xChannelControl(
	    interp, pPlat->pCtx, TH8_INT2PTR(handle), TH8_CHANCTL_CLOSE, 0, 0,
	    0, 0);
	Th8_SetResultStatic(
	    interp, "memory dump: file already exists", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* ---- Snapshot the live set under the lock (no I/O, no TH8
            allocation held across it -> deadlock-safe). ---- */
    th8GlobalMutexEnter(NULL);
    for (i = 0; i < TH8_MEM_TRACE_BUCKETS; i++) {
	th8MemTrace *pTr;
	for (pTr = g_aTrace[i]; pTr != NULL; pTr = pTr->pNext) {
	    pTr->nDumpBlocks = 0;
	    pTr->nDumpBytes = 0;
	}
    }
    for (i = 0; i < TH8_MEM_ADDR_BUCKETS; i++) {
	th8MemNode *pN;
	for (pN = g_aAddr[i]; pN != NULL; pN = pN->pNext) {
	    if (pN->pTrace != NULL) {
		pN->pTrace->nDumpBlocks++;
		pN->pTrace->nDumpBytes += pN->nByte;
	    }
	}
    }
    for (i = 0; i < TH8_MEM_TRACE_BUCKETS; i++) {
	th8MemTrace *pTr;
	for (pTr = g_aTrace[i]; pTr != NULL; pTr = pTr->pNext) {
	    if (pTr->nDumpBlocks > 0) {
		nGroups++;
	    }
	}
    }
    nLiveBytes = g_nLiveBytes;
    nLiveBlocks = g_nLiveBlocks;
    if (nGroups > 0) {
	size_t nBytes;
	if (TH8_SAFE_MUL_SIZE((size_t)nGroups, sizeof(*pGroups), &nBytes)) {
	    nBytes = 0; /* overflow -> fall through to totals-only dump */
	}
	pGroups = (nBytes != 0) ? (th8MemDumpGroup *)th8MemRawAlloc(nBytes)
	                        : NULL;
	if (pGroups != NULL) {
	    iGroup = 0;
	    for (i = 0; i < TH8_MEM_TRACE_BUCKETS && iGroup < nGroups; i++) {
		th8MemTrace *pTr;
		for (pTr = g_aTrace[i]; pTr != NULL && iGroup < nGroups;
		     pTr = pTr->pNext) {
		    if (pTr->nDumpBlocks > 0) {
			pGroups[iGroup].pTrace = pTr;
			pGroups[iGroup].nBlocks = pTr->nDumpBlocks;
			pGroups[iGroup].nBytes = pTr->nDumpBytes;
			iGroup++;
		    }
		}
	    }
	    nGroups = iGroup;
	} else {
	    nGroups = 0; /* Fall back to a totals-only dump. */
	}
    }
    th8GlobalMutexLeave(NULL);

    /* ---- Open for write and emit, lock-free. ---- */
    handle = 0;
    if (pPlat->xChannelControl(
            interp, pPlat->pCtx, NULL, TH8_CHANCTL_OPEN, (th8_int64_t)nPath,
            1, &handle, zBuf) != TH8_OK) {
	th8MemRawFree(pGroups);
	Th8_SetResultStatic(
	    interp, "memory dump: cannot create file", TH8_NOLEN);
	return TH8_ERROR;
    }
    pChan = TH8_INT2PTR(handle);

    th8MemChanPuts(interp, pPlat, pChan, "TH8 live allocations\n");
    n = th8Snprintf(
        interp, line, sizeof(line), "live-blocks %lld live-bytes %lld\n",
        (long long)nLiveBlocks, (long long)nLiveBytes);
    if (n > 0 && (size_t)n < sizeof(line)) {
	th8MemChanWrite(interp, pPlat, pChan, line, (size_t)n);
    }
    th8MemChanPuts(interp, pPlat, pChan, "----\n");

    for (iGroup = 0; iGroup < nGroups; iGroup++) {
	th8MemTrace *pTr = pGroups[iGroup].pTrace;
	int f;
	n = th8Snprintf(
	    interp, line, sizeof(line), "trace %u blocks %u bytes %lld\n",
	    pTr->id, pGroups[iGroup].nBlocks,
	    (long long)pGroups[iGroup].nBytes);
	if (n > 0 && (size_t)n < sizeof(line)) {
	    th8MemChanWrite(interp, pPlat, pChan, line, (size_t)n);
	}
	for (f = 0; f < pTr->nFrames; f++) {
	    th8MemWriteFrame(interp, pPlat, pChan, pTr->aFrames[f]);
	}
	if (pTr->nFrames == 0) {
	    th8MemChanPuts(
	        interp, pPlat, pChan, "      <no stack captured>\n");
	}
    }

    th8MemChanPuts(interp, pPlat, pChan, "end\n");
    (void)pPlat->xChannelControl(
        interp, pPlat->pCtx, pChan, TH8_CHANCTL_FLUSH, 0, 0, 0, 0);
    (void)pPlat->xChannelControl(
        interp, pPlat->pCtx, pChan, TH8_CHANCTL_CLOSE, 0, 0, 0, 0);
    th8MemRawFree(pGroups);

    n = th8Snprintf(
        interp, line, sizeof(line), "%lld", (long long)nLiveBlocks);
    if (n > 0 && (size_t)n < sizeof(line)) {
	Th8_SetResult(interp, line, (size_t)n);
    } else {
	Th8_SetResultStatic(interp, "0", TH8_NOLEN);
    }
    return TH8_OK;
}

#endif /* TH8_MEM_DEBUG */
