/*
 * th8_cosmopolitan.c --
 *
 *	Th8_Platform layer for Cosmopolitan Libc (Actually Portable
 *	Executables).  This platform provides a unified POSIX-like
 *	environment on Linux, macOS, Windows, FreeBSD, OpenBSD, and
 *	NetBSD from a single binary.
 *
 *	Cosmopolitan supplies pthreads, dlopen, POSIX I/O, and
 *	standard C library functions on all platforms.  This file
 *	only needs to override the few callbacks where Cosmopolitan
 *	diverges from the standard POSIX platform (e.g., executable
 *	path discovery).
 *
 *	Everything else is inherited from the POSIX platform via
 *	Th8_MergePlatform.
 *
 *	Gated on TH8_PLATFORM_COSMOPOLITAN.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#if defined(TH8_PLATFORM_COSMOPOLITAN)

#  include "th8_meta_defs.h"
#  include "th8_meta_libc.h"
#  include "th8_meta_posix.h"

#  include "th8.h"
#  include "th8_int.h"

#  include <cosmo.h>

/*
 *----------------------------------------------------------------------
 *
 * th8CosmopolitanGetExePath --
 *
 *	Return the path to the running executable.  Cosmopolitan
 *	provides GetProgramExecutableName() which works on all
 *	supported platforms (Linux, macOS, Windows, BSDs).
 *
 *----------------------------------------------------------------------
 */

static char *
th8CosmopolitanGetExePath(Th8_Interp *interp, void *pCtx)
{
    const char *zPath;
    char *zResult;
    size_t n;

    (void)pCtx;

    zPath = GetProgramExecutableName();
    if (!zPath || !zPath[0]) return 0;

    n = strlen(zPath);
    zResult = (char *)TH8_ALLOC_STR(interp, n);
    if (!zResult) return 0;
    memcpy(zResult, zPath, n + 1);
    return zResult;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CosmopolitanMemset --
 *
 *	Secure zeroing via explicit_bzero (Cosmopolitan provides
 *	this on all platforms).  Falls back to volatile write for
 *	non-zero fills.
 *
 *----------------------------------------------------------------------
 */

static void *
th8CosmopolitanMemset(
    Th8_Interp *interp,
    void *pCtx,
    void *dst,
    int c,
    size_t n)
{
    (void)interp;
    (void)pCtx;
    if (c == 0) {
	explicit_bzero(dst, n);
	return dst;
    }
    return memset(dst, c, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8CosmopolitanMemorySize --
 *
 *	Return the usable size of an allocated block.  Cosmopolitan
 *	provides malloc_usable_size on all platforms.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8CosmopolitanMemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;

    return malloc_usable_size(p);
}


/*
 *----------------------------------------------------------------------
 *
 * Platform structure.
 *
 *	We only populate the callbacks that differ from POSIX or
 *	that POSIX does not provide in a cross-platform way.
 *	Everything else is filled by merging with the POSIX and
 *	libc layers.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8CosmopolitanPlatformData = {
    5,   /* nVersion */
    0,   /* xInitialize (filled by POSIX merge) */
    0,   /* xFinalize (filled by POSIX merge) */

    /* Interpreter deletion notifications */
    0,   /* xPreDeleteInterp (filled by POSIX) */
    0,   /* xDeleteInterp (filled by POSIX) */

    /* Memory (filled by libc merge) */
    0,   /* xMalloc */
    0,   /* xRealloc */
    0,   /* xFree */
    th8CosmopolitanMemorySize, /* xMemorySize */
    0,   /* xNeedMemory */

    /* C runtime mem ops */
    0,   /* xMemcpy */
    0,   /* xMemmove */
    th8CosmopolitanMemset, /* xMemset -- secure zeroing on all platforms */
    0,   /* xMemcmp */

    /* C runtime string/utility (filled by libc merge) */
    0,   /* xStrlen */
    0,   /* xStrcmp */
    0,   /* xStrchr */
    0,   /* xAtoi */
    0,   /* xQsort */
    0,   /* xVsnprintf */

    /* Mutex callbacks (filled by POSIX merge) */
    0,   /* xMutexInit */
    0,   /* xMutexFinal */
    0,   /* xMutexEnter */
    0,   /* xMutexLeave */
    0,   /* xIntCmpXchg */
    0,   /* xMemBarrier */

    /* Manual-reset event handle (filled by POSIX merge) */
    0,   /* xEventCreate */
    0,   /* xEventDestroy */
    0,   /* xEventSet */
    0,   /* xEventReset */
    0,   /* xEventWait */

    /* Input (filled by POSIX merge) */
    0,   /* xInput */

    /* Output (filled by POSIX merge) */
    0,   /* xOutput */
    0,   /* xOutputError */

    /* I/O channel redirection (filled by POSIX merge) */
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
    th8CosmopolitanGetExePath, /* xGetExePath -- cosmo-specific */
    0,   /* xGetRealPath (filled by POSIX) */
    0,   /* xGetRootPath (filled by POSIX) */
    0,   /* xSameFile (filled by POSIX) */

    /* Data retrieval (filled by POSIX merge) */
    0,   /* xGetData */
    0,   /* xDataExists */

    /* Binary loading (filled by POSIX merge) */
    0,   /* xLoad */
    0,   /* xUnload */

    /* Time (filled by POSIX merge) */
    0,   /* xTimeMs */
    0,   /* xTimeUs */

    /* Sleep (filled by POSIX merge) */
    0,   /* xSleep */

    /* Process ID (filled by POSIX merge) */
    0,   /* xGetPid */

    /* User name, host name */
    0,   /* xGetUserName */
    0,   /* xGetHostName */
    0,   /* xGetEnv */
    0,   /* xKeyValue */

    /* Stack bounds (filled by POSIX merge) */
    0,   /* xGetStackBounds */

    /* Parent PID, thread ID (filled by POSIX merge) */
    0,   /* xGetParentPid */
    0,   /* xGetThreadId */

    /* Error code */
    0,   /* xGetLastError */
    0,   /* xSetLastError */

    /* Trace */
    0,   /* xEmitTrace */

    /* Panic (filled by POSIX merge) */
    0,   /* xPanic */

    /* Math functions (filled by libc merge) */
    0,   /* xMathFunc */

    /* Random bytes (filled by POSIX merge) */
    0,   /* xRandomBytes */

    /* DNS (filled by POSIX merge) */
    0, 0,  /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics (nVersion 5) -- the th8_unwind (compiler-runtime) layer supplies xStackBackTrace. */
    0, /* xStackBackTrace */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetCosmopolitanPlatform --
 *
 *	Return a pointer to the Cosmopolitan platform layer.
 *
 *	Typical composition for an Actually Portable Executable:
 *
 *	  Th8_Platform plat = *Th8_GetCosmopolitanPlatform();
 *	  Th8_MergePlatform(&plat, Th8_GetPosixPlatform());
 *	  Th8_MergePlatform(&plat, Th8_GetLibcPlatform());
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetCosmopolitanPlatform(void)
{
    return &th8CosmopolitanPlatformData;
}

#endif /* TH8_PLATFORM_COSMOPOLITAN */
