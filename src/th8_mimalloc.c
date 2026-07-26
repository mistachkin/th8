/*
 * th8_mimalloc.c --
 *
 *	Th8_Platform layer backed by Microsoft's mimalloc allocator.
 *	Provides xMalloc, xRealloc, xFree, and xMemorySize only.
 *	All other callbacks are NULL and must be filled by merging
 *	with other platform layers (e.g., libc for byte ops, POSIX
 *	for I/O).
 *
 *	A dedicated mimalloc heap is created PER THREAD on first
 *	use (or via Th8_ThreadInit) and torn down via Th8_ThreadDone
 *	(at thread exit) using mi_heap_delete -- the lazy variant
 *	that lets blocks already allocated outlive the heap they
 *	came from.  All allocations are routed through the calling
 *	thread's heap, giving mimalloc's per-page free-list fast
 *	path (~7 instructions for malloc) without tripping the
 *	thread-id assertion that mi_heap_new heaps enforce.
 *
 *	Gated on TH8_USE_MIMALLOC.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#if defined(TH8_USE_MIMALLOC)

#  include "th8.h"
#  include "th8_int.h"
#  include <mimalloc.h>


/*
 *----------------------------------------------------------------------
 *
 * th8MiHeap --
 *
 *	The per-thread mimalloc heap.  Each thread that allocates
 *	via the mimalloc xMalloc / xRealloc paths gets its OWN
 *	heap, established via Th8_ThreadInit (or lazily on first
 *	allocation).  Per-thread ownership is required by mimalloc:
 *	a heap created by mi_heap_new() may only be used for
 *	*allocations* by its creating thread.  Frees, however, are
 *	cross-thread-safe -- mimalloc routes freed blocks back to
 *	their owner heap automatically.  Th8_ThreadDone uses
 *	mi_heap_delete (not mi_heap_destroy) so blocks allocated by
 *	the exiting thread that haven't been freed yet remain valid
 *	until the consumer drains them.
 *
 *----------------------------------------------------------------------
 */

static TH8_TLS mi_heap_t *th8MiHeap = 0;


/*
 *----------------------------------------------------------------------
 *
 * th8MiHeapInit --
 *
 *	Create the calling thread's dedicated mimalloc heap, if not
 *	already created.  Idempotent.  Called by Th8_ThreadInit and
 *	(as a safety net) lazily by the malloc/realloc paths when
 *	they observe a NULL th8MiHeap on entry.
 *
 * Results:
 *	None.  Failure of mi_heap_new is silent; subsequent malloc
 *	calls will fall back to the calling thread's default heap.
 *
 * Side effects:
 *	On first call per thread, creates an mi_heap_t and assigns
 *	it to the thread-local th8MiHeap pointer.
 *
 *----------------------------------------------------------------------
 */

void
th8MiHeapInit(void)
{
    if (!th8MiHeap) {
	th8MiHeap = mi_heap_new();
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8MiHeapDone --
 *
 *	Tear down the calling thread's dedicated mimalloc heap.
 *	Uses mi_heap_delete (NOT mi_heap_destroy): blocks that were
 *	allocated from this heap but not yet freed are abandoned
 *	rather than eagerly released.  Each abandoned block remains
 *	valid until its eventual mi_free, at which point mimalloc
 *	reclaims it; once the heap is empty, the heap structure is
 *	released.  This is what makes the per-thread heap design
 *	safe for our event-queue pattern, where a worker thread
 *	may exit while its allocated Th8_Event nodes are still
 *	queued for drain on another thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Marks the calling thread's th8MiHeap for lazy reclamation
 *	and clears the TLS pointer.  Idempotent.
 *
 *----------------------------------------------------------------------
 */

void
th8MiHeapDone(void)
{
    if (th8MiHeap) {
	mi_heap_delete(th8MiHeap);
	th8MiHeap = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8MimallocInit --
 *
 *	No-op platform `xInitialize` hook for the mimalloc
 *	allocator layer.  Per-thread heap setup is handled by
 *	`Th8_ThreadInit` -> `th8MiHeapInit`, so the
 *	platform-level init hook has nothing to do.  Kept as
 *	a named no-op so any future platform-level setup has
 *	an obvious place to go (registering global tracing,
 *	tuning page-class options, etc.).
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *
 * Returns:
 *	0 (`TH8_OK`).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8MimallocInit(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
    return 0;  /* TH8_OK */
}

/*
 *----------------------------------------------------------------------
 *
 * th8MimallocFinal --
 *
 *	No-op platform `xFinalize` hook for the mimalloc
 *	allocator layer.  Per-thread heap teardown is handled
 *	by `Th8_ThreadDone` -> `th8MiHeapDone`, so the
 *	platform-level finalize hook has nothing to do.
 *	Kept as a named no-op for symmetry with
 *	`th8MimallocInit`.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- ignored.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static void
th8MimallocFinal(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MimallocMalloc --
 *
 *	Allocate nByte bytes from the calling thread's mimalloc
 *	heap.  Lazily creates the per-thread heap on first call
 *	(equivalent to th8MiHeapInit) so embedders that forget to
 *	call Th8_ThreadInit don't crash.
 *
 *	The returned memory is zero-initialized (calloc semantics)
 *	to match TH8's xMalloc contract.
 *
 * Results:
 *	Pointer to allocated memory, or NULL on failure.
 *
 * Side effects:
 *	May create the calling thread's mi_heap_t on first call.
 *	Allocates memory from that per-thread heap.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MimallocMalloc(Th8_Interp *interp, void *pCtx, size_t nByte)
{
    (void)interp;
    (void)pCtx;

    if (!th8MiHeap) th8MiHeapInit();
    if (!th8MiHeap) {
	/* Heap creation failed; fall back to the thread's default heap.
	 * mi_calloc allocates from that heap and is stable across mimalloc
	 * 2.x/3.x, whereas mi_heap_get_default() was removed in 3.x. */
	return mi_calloc(1, nByte);
    }
    return mi_heap_calloc(th8MiHeap, 1, nByte);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MimallocRealloc --
 *
 *	Resize a previously allocated block.  If p is NULL, behaves
 *	like malloc.  Like th8MimallocMalloc, lazily creates the
 *	calling thread's heap on first use.
 *
 *	NB: when p was allocated on a *different* thread (e.g. by a
 *	worker that has since exited), mimalloc handles the realloc
 *	correctly via its cross-thread free queue -- the new block
 *	comes from the calling thread's heap and the old one is
 *	released back to the original owner heap.
 *
 * Results:
 *	Pointer to reallocated memory, or NULL on failure.
 *
 * Side effects:
 *	Reallocates memory.  May create the calling thread's heap
 *	on first call.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MimallocRealloc(Th8_Interp *interp, void *pCtx, void *p, size_t nByte)
{
    (void)interp;
    (void)pCtx;

    if (!th8MiHeap) th8MiHeapInit();
    if (!th8MiHeap) {
	/* Heap creation failed; fall back to the thread's default heap via
	 * the top-level mi_realloc/mi_calloc (stable across mimalloc 2.x/3.x;
	 * mi_heap_get_default() was removed in 3.x). */
	return p ? mi_realloc(p, nByte) : mi_calloc(1, nByte);
    }
    if (!p) {
	return mi_heap_calloc(th8MiHeap, 1, nByte);
    }
    return mi_heap_realloc(th8MiHeap, p, nByte);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MimallocFree --
 *
 *	Free a block.  mi_free determines the owning heap
 *	automatically from the pointer.
 *
 * Why / How:
 *	Implements the Th8_Platform.xFree callback for the mimalloc
 *	platform.  mi_free is heap-agnostic: it finds the owning
 *	heap from the pointer's page metadata, so no heap handle
 *	is needed here.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory in the mimalloc heap.
 *
 *----------------------------------------------------------------------
 */

static void
th8MimallocFree(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;

    mi_free(p);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MimallocMemorySize --
 *
 *	Return the usable size of a previously allocated block.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemorySize callback for the
 *	mimalloc platform.  Delegates to mi_usable_size which reads
 *	the size from mimalloc's internal page metadata.
 *
 * Results:
 *	The usable size of the allocation.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8MimallocMemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;

    return mi_usable_size(p);
}


/*
 *----------------------------------------------------------------------
 *
 * Platform structure.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8MimallocPlatformData = {
    5,    /* nVersion */
    th8MimallocInit,  /* xInitialize */
    th8MimallocFinal,  /* xFinalize */

    /* Interpreter deletion notifications */
    0,   /* xPreDeleteInterp */
    0,   /* xDeleteInterp */

    /* Memory allocation */
    th8MimallocMalloc,  /* xMalloc */
    th8MimallocRealloc,  /* xRealloc */
    th8MimallocFree,  /* xFree */
    th8MimallocMemorySize, /* xMemorySize */
    0,   /* xNeedMemory */

    /* C runtime mem ops (filled by merging with other layers) */
    0,   /* xMemcpy */
    0,   /* xMemmove */
    0,   /* xMemset */
    0,   /* xMemcmp */

    /* C runtime string/utility (filled by merging with other layers) */
    0,   /* xStrlen */
    0,   /* xStrcmp */
    0,   /* xStrchr */
    0,   /* xAtoi */
    0,   /* xQsort */
    0,   /* xVsnprintf */

    /* Mutex callbacks */
    0,   /* xMutexInit */
    0,   /* xMutexFinal */
    0,   /* xMutexEnter */
    0,   /* xMutexLeave */
    0,   /* xIntCmpXchg */
    0,   /* xMemBarrier */

    /* Manual-reset event handle */
    0,   /* xEventCreate */
    0,   /* xEventDestroy */
    0,   /* xEventSet */
    0,   /* xEventReset */
    0,   /* xEventWait */

    /* Input */
    0,   /* xInput */

    /* Output */
    0,   /* xOutput */
    0,   /* xOutputError */

    /* I/O channel redirection */
    0,   /* xGetInput */
    0,   /* xSetInput */
    0,   /* xGetOutput */
    0,   /* xSetOutput */
    0,   /* xGetErrorOutput */
    0,   /* xSetErrorOutput */

    /* Channel control */
    0,   /* xChannelControl */

    /* Temporary data */
    0,   /* xGetTemporaryData */
    0,   /* xDeleteTemporaryData */
    0,   /* xSetTemporaryData */
    0,   /* xCloseTemporaryData */

    /* File system */
    0,   /* xNormalizePath */
    0,   /* xGetCwd */
    0,   /* xSetCwd */

    /* Executable path */
    0,   /* xGetExePath */
    0,   /* xGetRealPath */
    0,   /* xGetRootPath */
    0,   /* xSameFile */

    /* Data retrieval */
    0,   /* xGetData */
    0,   /* xDataExists */

    /* Binary loading */
    0,   /* xLoad */
    0,   /* xUnload */

    /* Time */
    0,   /* xTimeMs */
    0,   /* xTimeUs */

    /* Sleep */
    0,   /* xSleep */

    /* Process ID */
    0,   /* xGetPid */

    /* User name, host name */
    0,   /* xGetUserName */
    0,   /* xGetHostName */
    0,   /* xGetEnv */
    0,   /* xKeyValue */

    /* Stack bounds */
    0,   /* xGetStackBounds */

    /* Parent PID, thread ID */
    0,   /* xGetParentPid */
    0,   /* xGetThreadId */

    /* Error code */
    0,   /* xGetLastError */
    0,   /* xSetLastError */

    /* Trace */
    0,   /* xEmitTrace */

    /* Panic */
    0,   /* xPanic */

    /* Math functions */
    0,   /* xMathFunc */

    /* Random bytes */
    0,   /* xRandomBytes */

    /* DNS */
    0, 0,  /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics (nVersion 5) -- the th8_unwind (compiler-runtime) layer supplies xStackBackTrace. */
    0, /* xStackBackTrace */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetMimallocPlatform --
 *
 *	Return a pointer to the mimalloc platform layer.
 *
 * Why / How:
 *	Returns the address of the module-level static platform
 *	struct.  Only memory callbacks and lifecycle hooks are filled;
 *	all other slots are NULL and must be provided by merging with
 *	other platform layers.
 *
 * Results:
 *	Pointer to a static Th8_Platform struct.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetMimallocPlatform(void)
{
    return &th8MimallocPlatformData;
}

#endif /* TH8_USE_MIMALLOC */
