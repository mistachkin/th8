/*
 * th8_unwind.c --
 *
 *	Compiler-runtime platform layer for TH8: provides the
 *	Th8_Platform.xStackBackTrace callback via the compiler unwind
 *	runtime (_Unwind_Backtrace / _Unwind_GetIP from <unwind.h>).
 *
 *	This is a deliberately separate platform layer because the unwind
 *	runtime is NEITHER ANSI/ISO C (so it does not belong in th8_libc.c)
 *	NOR a POSIX or OS facility (so it does not belong in th8_posix.c /
 *	th8_macos.c).  It is part of the compiler's runtime support
 *	(libgcc_s / compiler-rt), declared in the compiler-provided header
 *	<unwind.h>, and available under GCC and Clang on every hosted
 *	target regardless of the operating system.  Keeping it in its own
 *	file preserves the layer discipline of the other platform files.
 *
 *	Only xStackBackTrace is provided; every other callback is NULL and
 *	is supplied by merging with the OS/libc platforms.  Windows has its
 *	own native stack walk (RtlCaptureStackBackTrace in th8_win32.c),
 *	which wins because th8_win32.c is merged before this layer; on a
 *	toolchain with no unwind runtime this layer reports zero frames and
 *	the tracker degrades to the immediate caller.
 *
 * Usage:
 *     Th8_MergePlatform(&plat, th8GetUnwindPlatform());
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8.h"
#include "th8_int.h"

/*
 * <unwind.h> supplies the compiler's stack-unwind runtime
 * (_Unwind_Backtrace / _Unwind_GetIP).  It is a compiler-runtime facility
 * (libgcc_s / compiler-rt), present under GCC and Clang on every hosted
 * target; on toolchains without it the callback simply reports zero frames.
 */
#if defined(__GNUC__) || defined(__clang__)
#  include <unwind.h>
#  define TH8_HAVE_UNWIND 1
#endif


#if defined(TH8_HAVE_UNWIND)

typedef struct th8UnwindState {
    void **apFrames; /* Caller's frame buffer. */
    int nMax; /* Capacity of apFrames. */
    int nSkip; /* Innermost frames to drop. */
    int nSeen; /* Frames walked so far. */
    int nStored; /* Frames recorded. */
} th8UnwindState;


/*
 *----------------------------------------------------------------------
 *
 * th8UnwindStep --
 *
 *	_Unwind_Backtrace trampoline: invoked once per stack frame during
 *	th8UnwindStackBackTrace's walk.
 *
 * Why / How:
 *	Reads the frame's instruction pointer via _Unwind_GetIP, drops the
 *	first nSkip frames, then records PCs into the caller-provided
 *	buffer until it is full or the stack ends.  Allocates nothing (all
 *	state lives in the caller's th8UnwindState on the stack) -- required,
 *	since it runs inside the allocation tracker.  A NULL IP or a full
 *	buffer ends the walk.
 *
 * Results:
 *	_URC_NO_REASON to continue the walk, _URC_END_OF_STACK to stop.
 *
 * Side effects:
 *	Mutates the th8UnwindState pointed to by pArg.
 *
 *----------------------------------------------------------------------
 */

static _Unwind_Reason_Code
th8UnwindStep(struct _Unwind_Context *pCtx, void *pArg)
{
    th8UnwindState *pState = (th8UnwindState *)pArg;
    void *pIp = (void *)_Unwind_GetIP(pCtx);

    if (pIp == NULL) {
	return _URC_END_OF_STACK;
    }
    if (pState->nSeen >= pState->nSkip) {
	if (pState->nStored >= pState->nMax) {
	    return _URC_END_OF_STACK;
	}
	pState->apFrames[pState->nStored++] = pIp;
    }
    pState->nSeen++;
    return _URC_NO_REASON;
}

#endif /* TH8_HAVE_UNWIND */


/*
 *----------------------------------------------------------------------
 *
 * th8UnwindStackBackTrace --
 *
 *	Implements the Th8_Platform.xStackBackTrace callback: capture up
 *	to nMaxFrames return-address program counters into apFrames[],
 *	skipping the innermost nSkip frames.  Returns the number captured.
 *
 * Why / How:
 *	Uses the compiler unwind runtime (_Unwind_Backtrace), correct
 *	without frame pointers (it consults the unwind tables), driving the
 *	th8UnwindStep trampoline over an on-stack th8UnwindState so it
 *	allocates nothing -- mandatory, because it runs inside the
 *	allocation tracker.  On a toolchain without the unwind runtime
 *	(the #else arm) it reports zero frames and the tracker degrades to
 *	the immediate caller.
 *
 * Results:
 *	Number of frames stored (0 if unsupported or on bad arguments).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8UnwindStackBackTrace(
    Th8_Interp *interp,
    void *pCtx,
    void **apFrames,
    int nMaxFrames,
    int nSkip)
{
#if defined(TH8_HAVE_UNWIND)
    th8UnwindState state;

    (void)interp;
    (void)pCtx;
    if (apFrames == NULL || nMaxFrames <= 0) {
	return 0;
    }
    state.apFrames = apFrames;
    state.nMax = nMaxFrames;
    state.nSkip = (nSkip < 0) ? 0 : nSkip;
    state.nSeen = 0;
    state.nStored = 0;
    _Unwind_Backtrace(th8UnwindStep, &state);
    return state.nStored;
#else
    (void)interp;
    (void)pCtx;
    (void)apFrames;
    (void)nMaxFrames;
    (void)nSkip;
    return 0; /* No compiler unwind runtime available. */
#endif
}


/*
 * Partial platform table: only xStackBackTrace is filled; every other
 * slot is NULL and supplied by merging with the OS/libc platforms.
 */

static Th8_Platform th8UnwindPlatformData = {
    5, /* nVersion */
    0, 0, 0, 0, /* xInitialize, xFinalize, xPreDeleteInterp, xDeleteInterp */

    /* Memory */
    0, 0, 0, 0, /* xMalloc, xRealloc, xFree, xMemorySize */
    0, /* xNeedMemory */

    /* Byte operations */
    0, 0, 0, 0, /* xMemcpy, xMemmove, xMemset, xMemcmp */

    /* String / utility */
    0, 0, 0, 0, 0, 0, /* xStrlen .. xVsnprintf */

    /* Threading */
    0, 0, 0, 0, /* xMutexInit .. xMutexLeave */
    0, 0, /* xIntCmpXchg, xMemBarrier */

    /* Manual-reset event handle */
    0, 0, 0, 0,
    0, /* xEventCreate, xEventDestroy, xEventSet, xEventReset, xEventWait */

    /* I/O */
    0, 0, 0, /* xInput, xOutput, xOutputError */
    0, 0, 0, 0, 0, 0, /* xGet/SetInput, xGet/SetOutput, xGet/SetErrorOutput */

    /* Channel / temporary I/O */
    0, 0, 0, 0,
    0, /* xChannelControl, xGetTemporaryData, xDeleteTemporaryData, xSetTemporaryData, xCloseTemporaryData */

    /* Filesystem */
    0, 0, 0, 0, 0, /* xNormalizePath .. xGetRealPath */
    0, 0, /* xGetRootPath, xSameFile */

    /* Data retrieval / binary loading */
    0, /* xGetData */
    0, /* xDataExists */
    0, 0, /* xLoad, xUnload */

    /* Time */
    0, 0, 0, /* xTimeMs, xTimeUs, xSleep */

    /* Process / host */
    0, 0, 0, 0, 0,
    0, /* xGetPid, xGetUserName, xGetHostName, xGetEnv, xKeyValue, xGetStackBounds */
    0, 0, /* xGetParentPid, xGetThreadId */

    /* Error / diagnostics */
    0, 0, 0, 0, /* xGetLastError, xSetLastError, xEmitTrace, xPanic */

    /* Math / entropy */
    0, 0, /* xMathFunc, xRandomBytes */

    /* DNS */
    0, 0, /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics (nVersion 5) -- the compiler unwind runtime. */
    th8UnwindStackBackTrace, /* xStackBackTrace */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * th8GetUnwindPlatform --
 *
 *	Return the compiler-runtime (unwind) platform implementation.
 *	Internal: it is merged by Th8_UseDefaultPlatform to supply
 *	xStackBackTrace on GCC/Clang targets that lack a native OS stack
 *	walk.  Not part of the public platform-getter surface.
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
th8GetUnwindPlatform(void)
{
    return &th8UnwindPlatformData;
}
