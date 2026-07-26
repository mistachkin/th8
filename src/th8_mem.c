/*
 * th8_mem.c -- Memory recovery platform layer for TH8.
 *
 * Provides a single platform callback: xNeedMemory.  When the
 * primary allocator (xMalloc) returns NULL, the TH8 core calls
 * xNeedMemory as a second-chance recovery.  This implementation:
 *
 *   1. Clears the interpreter's internal-representation cache,
 *      releasing all cached type conversions and list splits.
 *   2. Validates the requested size against TH8_MX_ALLOC.
 *   3. Retries the allocation via the interpreter's current
 *      xMalloc callback.
 *
 * All other platform callbacks are NULL.  Merge this layer with
 * a primary platform (POSIX, Win32, etc.) via Th8_MergePlatform:
 *
 *     Th8_MergePlatform(&plat, Th8_GetMemPlatform());
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"


/*
 *----------------------------------------------------------------------
 *
 * th8MemNeedMemory --
 *
 *	Second-chance allocator callback.  Called by Th8_SafeAlloc
 *	when the primary xMalloc returns NULL.
 *
 * Why / How:
 *	The IR cache can hold a significant amount of memory
 *	(cached type conversions, list splits, buffer pool entries).
 *	Clearing it releases that memory back to the platform,
 *	which may allow the retry to succeed.  The size check
 *	against TH8_MX_ALLOC rejects obviously unreasonable
 *	requests before touching the allocator.
 *
 * Results:
 *	Pointer to nByte bytes of memory on success, or NULL if
 *	the retry also fails.
 *
 * Side effects:
 *	The IR cache is fully cleared.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MemNeedMemory(Th8_Interp *interp, size_t nByte)
{
    const Th8_Platform *pPlat;

    if (!interp) return NULL;

    /*
     * Step 1: Clear the IR cache.  This releases all cached
     * type conversions, list splits, and buffer pool entries.
     */

    th8ClearCache(interp);

    /*
     * Step 2: Sanity check.  Reject requests that exceed the
     * maximum allocation size.  This prevents a corrupted size_t
     * from causing a massive allocation attempt.
     */

    if (nByte == 0 || nByte > TH8_MX_ALLOC) {
	return NULL;
    }

    /*
     * Step 3: Retry the allocation via the interpreter's
     * current xMalloc callback.  This goes through the real
     * platform allocator (not through Th8_Malloc, which would
     * recurse back into Th8_SafeAlloc and xNeedMemory).
     */

    pPlat = Th8_GetPlatform(interp);
    if (ALWAYS(pPlat) && pPlat->xMalloc) {
	return pPlat->xMalloc(interp, pPlat->pCtx, nByte);
    }

    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Platform struct -- field order matches Th8_Platform in th8.h
 * exactly.  Use th8_nullio.c as the authoritative reference.
 *
 *----------------------------------------------------------------------
 */

static const Th8_Platform th8MemPlatformData = {
    5,    /* nVersion */
    0,    /* xInitialize */
    0,    /* xFinalize */

    /* Interpreter deletion */
    0,    /* xPreDeleteInterp */
    0,    /* xDeleteInterp */

    /* Memory */
    0,    /* xMalloc */
    0,    /* xRealloc */
    0,    /* xFree */
    0,    /* xMemorySize */
    th8MemNeedMemory,  /* xNeedMemory */

    /* C runtime mem ops */
    0,    /* xMemcpy */
    0,    /* xMemmove */
    0,    /* xMemset */
    0,    /* xMemcmp */

    /* C runtime string/utility */
    0,    /* xStrlen */
    0,    /* xStrcmp */
    0,    /* xStrchr */
    0,    /* xAtoi */
    0,    /* xQsort */
    0,    /* xVsnprintf */

    /* Mutex callbacks */
    0,    /* xMutexInit */
    0,    /* xMutexFinal */
    0,    /* xMutexEnter */
    0,    /* xMutexLeave */
    0,    /* xIntCmpXchg */
    0,    /* xMemBarrier */

    /* Manual-reset event handle */
    0,    /* xEventCreate */
    0,    /* xEventDestroy */
    0,    /* xEventSet */
    0,    /* xEventReset */
    0,    /* xEventWait */

    /* Input */
    0,    /* xInput */

    /* Output */
    0,    /* xOutput */
    0,    /* xOutputError */

    /* I/O channel redirection */
    0,    /* xGetInput */
    0,    /* xSetInput */
    0,    /* xGetOutput */
    0,    /* xSetOutput */
    0,    /* xGetErrorOutput */
    0,    /* xSetErrorOutput */

    /* Channel control */
    0,    /* xChannelControl */

    /* Temporary data */
    0,    /* xGetTemporaryData */
    0,    /* xDeleteTemporaryData */
    0,    /* xSetTemporaryData */
    0,    /* xCloseTemporaryData */

    /* File system */
    0,    /* xNormalizePath */
    0,    /* xGetCwd */
    0,    /* xSetCwd */

    /* Executable path */
    0,    /* xGetExePath */
    0,    /* xGetRealPath */
    0,    /* xGetRootPath */
    0,    /* xSameFile */

    /* Data retrieval */
    0,    /* xGetData */
    0,    /* xDataExists */

    /* Binary loading */
    0,    /* xLoad */
    0,    /* xUnload */

    /* Time */
    0,    /* xTimeMs */
    0,    /* xTimeUs */

    /* Sleep */
    0,    /* xSleep */

    /* Process ID */
    0,    /* xGetPid */

    /* User name, host name */
    0,    /* xGetUserName */
    0,    /* xGetHostName */
    0,    /* xGetEnv */
    0,    /* xKeyValue */

    /* Stack bounds */
    0,    /* xGetStackBounds */

    /* Parent PID, thread ID */
    0,    /* xGetParentPid */
    0,    /* xGetThreadId */

    /* Error code */
    0,    /* xGetLastError */
    0,    /* xSetLastError */

    /* Trace */
    0,    /* xEmitTrace */

    /* Panic */
    0,    /* xPanic */

    /* Math functions */
    0,    /* xMathFunc */

    /* Random bytes */
    0,    /* xRandomBytes */

    /* DNS */
    0, 0,   /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics (nVersion 5) -- the th8_unwind (compiler-runtime) layer supplies xStackBackTrace. */
    0, /* xStackBackTrace */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetMemPlatform --
 *
 *	Return the static memory-recovery platform struct.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetMemPlatform(void)
{
    return &th8MemPlatformData;
}
