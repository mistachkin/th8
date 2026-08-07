/*
 * th8_macos.c -- macOS-specific platform implementation for TH8.
 *
 * Provides a private malloc zone via malloc_create_zone for
 * reduced fragmentation and accurate memory tracking via
 * malloc_size.  All other callbacks are inherited from the
 * POSIX platform via Th8_MergePlatform.
 *
 * Usage:
 *     Th8_Platform plat = *Th8_GetMacOSPlatform();
 *     Th8_MergePlatform(&plat, Th8_GetPosixPlatform());
 *     Th8_MergePlatform(&plat, Th8_GetLibcPlatform());
 *     interp = Th8_CreateInterp(&plat);
 *
 * The macOS platform provides:
 *   - xMalloc / xRealloc / xFree (private zone)
 *   - xMemorySize (malloc_size on the zone)
 *
 * Everything else (I/O, time, stack bounds, CRT ops, math)
 * comes from the POSIX and libc platforms via merge.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_meta_posix.h"
#include "th8_meta_macos.h"
#include "th8.h"

#if defined(TH8_PLATFORM_MACOS)


/*
 *----------------------------------------------------------------------
 *
 * Private malloc zone.
 *
 *	Created lazily on first use.  All TH8 allocations go
 *	through this zone, isolating them from the rest of the
 *	process and reducing fragmentation.
 *
 *	malloc_create_zone is the macOS equivalent of Win32's
 *	HeapCreate.  It provides per-zone allocation, deallocation,
 *	reallocation, and size query.
 *
 *----------------------------------------------------------------------
 */

static malloc_zone_t *th8MacOSZone = NULL;
static volatile int th8MacOSZoneReady = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8MacOSGetZone --
 *
 *	Return the private malloc zone for TH8, creating it lazily
 *	on first use via malloc_create_zone.  Uses atomic CAS to
 *	ensure only one thread creates the zone.
 *
 * Why / How:
 *	Uses __sync_val_compare_and_swap on a static flag so that
 *	concurrent first-time callers race safely: only the winner
 *	creates the zone, and the loser spins until the pointer is
 *	visible.  The zone is named "TH8" for identification in
 *	Instruments/leaks.
 *
 * Results:
 *	Pointer to the malloc zone, or NULL on failure.
 *
 * Side effects:
 *	May create the zone on first call.
 *
 *----------------------------------------------------------------------
 */

static malloc_zone_t *
th8MacOSGetZone(void)
{
    if (!th8MacOSZoneReady) {
	if (__sync_val_compare_and_swap(&th8MacOSZoneReady, 0, 1) == 0) {
	    malloc_zone_t *zone = malloc_create_zone(0, 0);

	    if (zone) {
		/*
		 * Set a name for the zone so it can be identified
		 * in Instruments / leaks.
		 */

		malloc_set_zone_name(zone, "TH8");
		th8MacOSZone = zone;
	    } else {
		__sync_val_compare_and_swap(&th8MacOSZoneReady, 1, 0);
	    }
	}
    }
    return th8MacOSZone;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MacOSMalloc --
 *
 *	Allocate zero-initialized memory from the private malloc zone
 *	via malloc_zone_calloc.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMalloc callback for macOS.
 *	Uses malloc_zone_calloc on the private zone for heap
 *	isolation and zero-initialization per the xMalloc contract.
 *
 * Results:
 *	Pointer to allocated memory, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory from the private zone.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MacOSMalloc(Th8_Interp *interp, void *pCtx, size_t nByte)
{
    malloc_zone_t *zone = th8MacOSGetZone();
    void *p;

    (void)interp;
    (void)pCtx;

    if (!zone) return NULL;
    p = malloc_zone_calloc(zone, 1, nByte);
    return p;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MacOSRealloc --
 *
 *	Reallocate memory in the private malloc zone.  If pPrior is
 *	NULL, behaves like th8MacOSMalloc.
 *
 * Why / How:
 *	Implements the Th8_Platform.xRealloc callback for macOS.
 *	When pPrior is NULL, falls through to calloc for zero-init.
 *	Otherwise delegates to malloc_zone_realloc on the private zone.
 *
 * Results:
 *	Pointer to reallocated memory, or NULL on failure.
 *
 * Side effects:
 *	Reallocates memory in the private zone.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MacOSRealloc(Th8_Interp *interp, void *pCtx, void *pPrior, size_t nByte)
{
    malloc_zone_t *zone = th8MacOSGetZone();

    (void)interp;
    (void)pCtx;

    if (!zone) return NULL;
    if (!pPrior) {
	return malloc_zone_calloc(zone, 1, nByte);
    }
    return malloc_zone_realloc(zone, pPrior, nByte);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MacOSFree --
 *
 *	Free memory allocated from the private malloc zone.
 *
 * Why / How:
 *	Implements the Th8_Platform.xFree callback for macOS.
 *	Delegates to malloc_zone_free on the private zone.  NULL
 *	pointers are silently ignored.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory in the private zone.
 *
 *----------------------------------------------------------------------
 */

static void
th8MacOSFree(Th8_Interp *interp, void *pCtx, void *pPrior)
{
    malloc_zone_t *zone;

    (void)interp;
    (void)pCtx;

    if (!pPrior) return;
    zone = th8MacOSGetZone();
    if (!zone) return;
    malloc_zone_free(zone, pPrior);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MacOSMemorySize --
 *
 *	Return the usable size of a block allocated from the private
 *	zone via malloc_size.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemorySize callback for macOS.
 *	The macOS malloc_size function works on any zone's allocation,
 *	so no zone pointer is needed.
 *
 * Results:
 *	The usable size of the allocation, or 0 if p is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8MacOSMemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;

    if (!p) return 0;
    return malloc_size(p);
}


/*
 *----------------------------------------------------------------------
 *
 * th8MacOSInitialize --
 *
 *	Platform initialization callback.  Ensures the private
 *	malloc zone exists.
 *
 * Why / How:
 *	Implements the Th8_Platform.xInitialize callback for macOS.
 *	Delegates to th8MacOSGetZone which lazily creates the zone.
 *	Returns TH8_ERROR if zone creation fails (e.g., out of
 *	address space).
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *
 * Side effects:
 *	May create the private malloc zone on first call.
 *
 *----------------------------------------------------------------------
 */

static int
th8MacOSInitialize(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    return th8MacOSGetZone() ? TH8_OK : TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MacOSFinalize --
 *
 *	Platform finalization callback.  Destroys the private
 *	malloc zone.
 *
 * Why / How:
 *	Implements the Th8_Platform.xFinalize callback for macOS.
 *	Uses atomic CAS to clear the ready flag, then destroys the
 *	zone via malloc_destroy_zone.  After this call, all memory
 *	allocated from the zone is invalid.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys the private malloc zone and invalidates all its
 *	allocations.
 *
 *----------------------------------------------------------------------
 */

static void
th8MacOSFinalize(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    if (__sync_val_compare_and_swap(&th8MacOSZoneReady, 1, 0) == 1) {
	malloc_zone_t *zone = th8MacOSZone;

	th8MacOSZone = NULL;
	if (zone) {
	    malloc_destroy_zone(zone);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Platform table -- only memory callbacks filled in.
 * Everything else is NULL and will be filled by merging
 * with Th8_GetPosixPlatform and Th8_GetLibcPlatform.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8MacOSMemset --
 *
 *	macOS xMemset callback.  When the fill byte is zero,
 *	uses bzero which on macOS is an external library call
 *	(not a compiler builtin) and therefore not subject to
 *	dead-store elimination.
 *
 * Why / How:
 *	Implements the Th8_Platform.xMemset callback for macOS.
 *	For zero-fills, uses bzero (a real function call on macOS,
 *	not an intrinsic) which the compiler cannot optimize away,
 *	ensuring sensitive memory is actually cleared.  Non-zero
 *	fills use a simple byte loop.
 *
 * Results:
 *	The dst pointer.
 *
 * Side effects:
 *	Fills n bytes of dst.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MacOSMemset(Th8_Interp *interp, void *pCtx, void *dst, int c, size_t n)
{
    (void)interp;
    (void)pCtx;
    if (c == 0) {
	bzero(dst, n);
    } else {
	unsigned char *p = (unsigned char *)dst;

	while (n--)
	    *p++ = (unsigned char)c;
    }
    return dst;
}


static Th8_Platform th8MacOSPlatformData = {
    1,    /* nVersion */
    th8MacOSInitialize,  /* xInitialize */
    th8MacOSFinalize,  /* xFinalize */

    /* Interpreter deletion notifications */
    0,   /* xPreDeleteInterp */
    0,   /* xDeleteInterp */

    /* Memory -- private malloc zone */
    th8MacOSMalloc,  /* xMalloc */
    th8MacOSRealloc,  /* xRealloc */
    th8MacOSFree,  /* xFree */
    th8MacOSMemorySize,  /* xMemorySize */
    0,   /* xNeedMemory */

    /* C runtime mem ops */
    0,   /* xMemcpy */
    0,   /* xMemmove */
    th8MacOSMemset, /* xMemset */
    0,   /* xMemcmp */

    /* C runtime string/utility (provided by libc merge) */
    0,   /* xStrlen */
    0,   /* xStrcmp */
    0,   /* xStrchr */
    0,   /* xAtoi */
    0,   /* xQsort */
    0,   /* xVsnprintf */

    /* Mutex callbacks (provided by POSIX merge) */
    0,   /* xMutexInit */
    0,   /* xMutexFinal */
    0,   /* xMutexEnter */
    0,   /* xMutexLeave */
    0,   /* xIntCmpXchg */
    0,   /* xMemBarrier */

    /* Manual-reset event handle (provided by POSIX merge) */
    0,   /* xEventCreate */
    0,   /* xEventDestroy */
    0,   /* xEventSet */
    0,   /* xEventReset */
    0,   /* xEventWait */

    /* Input (provided by POSIX merge) */
    0,   /* xInput */

    /* Output (provided by POSIX merge) */
    0,   /* xOutput */
    0,   /* xOutputError */

    /* I/O channel redirection (provided by POSIX merge) */
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

    /* File system (provided by POSIX merge) */
    0,   /* xNormalizePath */
    0,   /* xGetCwd */
    0,   /* xSetCwd */

    /* Executable path */
    0,   /* xGetExePath */
    0,   /* xGetRealPath */
    0,   /* xGetRootPath (provided by POSIX merge) */
    0,   /* xSameFile (provided by POSIX merge) */

    /* Data retrieval (provided by POSIX merge) */
    0,   /* xGetData */
    0,   /* xDataExists */

    /* Binary loading (provided by POSIX merge) */
    0,   /* xLoad */
    0,   /* xUnload */

    /* Time (provided by POSIX merge) */
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

    /* Math functions (provided by libc merge) */
    0,   /* xMathFunc */

    /* Random bytes (provided by POSIX merge) */
    0,   /* xRandomBytes */

    /* DNS (provided by POSIX merge when TH8_ENABLE_UNBOUND) */
    0, 0,  /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics -- xStackBackTrace is deliberately left
       NULL here.  It is supplied by the compiler-runtime th8_unwind layer
       (_Unwind_Backtrace), merged after the OS layers.  It works uniformly
       on Apple targets; macOS's native backtrace() (<execinfo.h>) would be
       functionally equivalent but redundant, so no override is added here. */
    0, /* xStackBackTrace */

    /* 64-bit atomics */
    0, /* xIntCmpXchg64 */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetMacOSPlatform --
 *
 *	Return a pointer to the macOS platform implementation.
 *	Merge with Th8_GetPosixPlatform and Th8_GetLibcPlatform
 *	before passing to Th8_CreateInterp.
 *
 * Why / How:
 *	Returns the address of the module-level static platform
 *	struct.  Only the memory callbacks and xMemset are filled;
 *	all other slots are NULL and must be provided by merging.
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
Th8_GetMacOSPlatform(void)
{
    return &th8MacOSPlatformData;
}

#endif /* TH8_PLATFORM_MACOS */
