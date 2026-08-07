/*
 * th8_nullio.c -- Null I/O platform implementation for TH8.
 *
 * Provides a "safe null" platform layer where all I/O callbacks are
 * implemented but produce no side effects:
 *
 *   - xGetData:       returns zero bytes of data for all names.
 *   - xDataExists:    returns false for all names.
 *   - xLoad:          returns an error (loading forbidden).
 *   - xUnload:        returns an error (unloading forbidden).
 *   - xInput:         returns zero bytes successfully.
 *   - xOutput:        returns success but emits nothing.
 *   - xOutputError:   returns success but emits nothing.
 *   - xNormalizePath: returns the original path verbatim.
 *   - xGetCwd:        returns ".".
 *
 * This gives embedders a zero-capability baseline: scripts can run
 * without errors on I/O commands (gets, puts, source) but cannot
 * read, write, or load anything.  Real capabilities can be added
 * selectively by merging in specific callbacks via Th8_MergePlatform.
 *
 * Memory, CRT operations, time, PID, stack bounds, panic, and math
 * callbacks are left as NULL -- the embedder must merge in at least
 * Th8_GetLibcPlatform() for those.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"    /* For TH8_PLATFORM_NULLIO auto-detection. */
#include "th8_int.h"

#if defined(TH8_PLATFORM_NULLIO)


/*
 *----------------------------------------------------------------------
 *
 * th8NullGetData --
 *
 *	Returns zero bytes of data successfully for any name.
 *	The script's [source] will succeed but evaluate an empty
 *	script.
 *
 * Why / How:
 *	Implements the Th8_Platform.xGetData callback for the null I/O
 *	platform.  Allocates a 1-byte buffer (empty string) so that
 *	the caller always gets a valid heap pointer, even though no
 *	real data is returned.
 *
 * Results:
 *	TH8_OK with an empty buffer in *pzData and 0 in *pnData.
 *
 * Side effects:
 *	Allocates a 1-byte buffer via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

static int
th8NullGetData(
    Th8_Interp *interp, /* Interpreter (for allocation). */
    void *pCtx,   /* Host context (unused). */
    const char *zName,  /* Data name (ignored). */
    size_t nName,  /* Name length (ignored). */
    char **pzData,  /* OUT: data buffer. */
    size_t *pnData)  /* OUT: data length. */
{
    (void)zName;
    (void)nName;
    (void)pCtx;

    *pzData = (char *)TH8_ALLOC(interp, 1);
    if (*pzData) {
	(*pzData)[0] = '\0';
    }
    *pnData = 0;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NullDataExists --
 *
 *	Returns false (0) for all names.  No data exists.
 *
 * Why / How:
 *	Implements the Th8_Platform.xDataExists callback for the null
 *	I/O platform.  Always returns 0 (not found) so that [file
 *	exists] always reports false in a sandboxed interpreter.
 *
 * Results:
 *	0 (false) unconditionally.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8NullDataExists(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx,   /* Host context (unused). */
    const char *zName,  /* Data name (ignored). */
    size_t nName,  /* Name length (ignored). */
    int *pAttrs)  /* OUT: file type attrs (may be NULL). */
{
    (void)interp;
    (void)pCtx;
    (void)zName;
    (void)nName;
    if (pAttrs) *pAttrs = 0;
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NullLoad --
 *
 *	Binary loading is forbidden.  Returns TH8_ERROR.
 *
 * Why / How:
 *	Implements the Th8_Platform.xLoad callback for the null I/O
 *	platform.  The null platform is a zero-capability sandbox, so
 *	loading shared libraries is unconditionally rejected with a
 *	descriptive error message.
 *
 * Results:
 *	TH8_ERROR unconditionally.
 *
 * Side effects:
 *	Sets the interpreter result to an error message.
 *
 *----------------------------------------------------------------------
 */

static int
th8NullLoad(
    Th8_Interp *interp, /* Interpreter. */
    void *pCtx,   /* Host context (unused). */
    const char *zName,  /* Library name (ignored). */
    size_t nName,  /* Name length (ignored). */
    const char *zProc,  /* Init proc (ignored). */
    size_t nProc)  /* Proc name length (ignored). */
{
    (void)pCtx;
    (void)zName;
    (void)nName;
    (void)zProc;
    (void)nProc;

    Th8_SetResult(
        interp, "binary loading is forbidden in this interpreter", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NullUnload --
 *
 *	Binary unloading is forbidden.  Returns TH8_ERROR.
 *
 * Why / How:
 *	Implements the Th8_Platform.xUnload callback for the null I/O
 *	platform.  Unconditionally rejected for the same reason as
 *	xLoad: no binary operations are permitted.
 *
 * Results:
 *	TH8_ERROR unconditionally.
 *
 * Side effects:
 *	Sets the interpreter result to an error message.
 *
 *----------------------------------------------------------------------
 */

static int
th8NullUnload(
    Th8_Interp *interp, /* Interpreter. */
    void *pCtx,   /* Host context (unused). */
    const char *zName,  /* Library name (ignored). */
    size_t nName,  /* Name length (ignored). */
    const char *zProc,  /* Unload proc (ignored). */
    size_t nProc,  /* Proc name length (ignored). */
    int bClose)   /* Close flag (ignored). */
{
    (void)pCtx;
    (void)zName;
    (void)nName;
    (void)zProc;
    (void)nProc;
    (void)bClose;

    Th8_SetResult(
        interp, "binary unloading is forbidden in this interpreter",
        TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NullInput --
 *
 *	Returns zero bytes of input successfully (immediate EOF).
 *
 * Why / How:
 *	Implements the Th8_Platform.xInput callback for the null I/O
 *	platform.  Returns an empty buffer so that [gets] returns an
 *	empty string rather than an error.  Scripts can probe for
 *	input without crashing.
 *
 * Results:
 *	TH8_OK with an empty buffer in *pzOut and 0 in *pnOut.
 *
 * Side effects:
 *	Allocates a 1-byte buffer via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

static int
th8NullInput(
    Th8_Interp *interp, /* Interpreter (for allocation). */
    void *pCtx,  /* Host context (unused). */
    char **pzOut,  /* OUT: input buffer. */
    size_t *pnOut,  /* OUT: input length. */
    void *pChannel)  /* Channel (unused). */
{
    (void)pChannel;
    (void)pCtx;

    *pzOut = (char *)TH8_ALLOC(interp, 1);
    if (*pzOut) {
	(*pzOut)[0] = '\0';
    }
    *pnOut = 0;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NullOutput --
 *
 *	Accept standard output but discard it.  Returns TH8_OK.
 *
 * Why / How:
 *	Implements the Th8_Platform.xOutput callback for the null I/O
 *	platform.  Accepts any data but silently discards it, making
 *	[puts] succeed without producing visible output.
 *
 * Results:
 *	TH8_OK unconditionally.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8NullOutput(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx,  /* Host context (unused). */
    const char *z,  /* Output text (ignored). */
    size_t n,   /* Output length (ignored). */
    void *pChannel)  /* Channel (unused). */
{
    (void)interp;
    (void)pCtx;
    (void)z;
    (void)n;
    (void)pChannel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8NullOutputError --
 *
 *	Accept error output but discard it.  Returns TH8_OK.
 *
 * Why / How:
 *	Implements the Th8_Platform.xOutputError callback for the
 *	null I/O platform.  Identical to th8NullOutput; separated
 *	so the platform struct has distinct function pointers for
 *	stdout and stderr channels.
 *
 * Results:
 *	TH8_OK unconditionally.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8NullOutputError(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx,  /* Host context (unused). */
    const char *z,  /* Error text (ignored). */
    size_t n,   /* Error length (ignored). */
    void *pChannel)  /* Channel (unused). */
{
    (void)interp;
    (void)pCtx;
    (void)z;
    (void)n;
    (void)pChannel;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NullNormalizePath --
 *
 *	Returns the original path verbatim (no normalization).
 *	Allocates a copy via Th8_Malloc.
 *
 * Why / How:
 *	Implements the Th8_Platform.xNormalizePath callback for the
 *	null I/O platform.  Since there is no real filesystem, no
 *	normalization is meaningful.  A heap-allocated copy is
 *	returned so the caller can free it uniformly.
 *
 * Results:
 *	A heap-allocated copy of the input path, or NULL on
 *	allocation failure.
 *
 * Side effects:
 *	Allocates memory via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

static char *
th8NullNormalizePath(
    Th8_Interp *interp, /* Interpreter (for allocation). */
    void *pCtx,   /* Host context (unused). */
    const char *zPath,  /* Path to normalize. */
    size_t nPath)  /* Path length, or (size_t)-1. */
{
    char *zCopy;

    (void)pCtx;
    if (!zPath) return 0;

    if (nPath == (size_t)-1) {
	size_t i = 0;
	while (zPath[i])
	    i++;
	nPath = i;
    }
    zCopy = (char *)TH8_ALLOC_STR(interp, nPath);
    if (zCopy) {
	Th8_Memcpy(interp, zCopy, zPath, nPath);
	zCopy[nPath] = '\0';
    }
    return zCopy;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NullGetCwd --
 *
 *	Returns "." as the current working directory.
 *	Allocates via Th8_Malloc.
 *
 * Why / How:
 *	Implements the Th8_Platform.xGetCwd callback for the null I/O
 *	platform.  Returns "." (the POSIX current-directory token) so
 *	that [pwd] has a valid return value even with no filesystem.
 *
 * Results:
 *	A heap-allocated "." string, or NULL on allocation failure.
 *
 * Side effects:
 *	Allocates a 2-byte buffer via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

static char *
th8NullGetCwd(
    Th8_Interp *interp, /* Interpreter (for allocation). */
    void *pCtx)   /* Host context (unused). */
{
    char *z;

    (void)pCtx;
    z = (char *)TH8_ALLOC(interp, 2);
    if (z) {
	z[0] = '.';
	z[1] = '\0';
    }
    return z;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NullIoPlatform --
 *
 *	The complete null I/O platform table.  Merge with
 *	Th8_GetLibcPlatform() to get a fully functional but
 *	completely sandboxed interpreter.
 *
 * Why / How:
 *	All I/O callbacks are wired to the th8Null* functions above.
 *	Memory, CRT, time, mutex, and math slots are left NULL so
 *	they must be filled by merging with Th8_GetLibcPlatform().
 *	The struct is declared static (not const) because some
 *	embedders may want to patch individual slots before use.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8NullIoPlatformData = {
    1,   /* nVersion */
    0,   /* xInitialize */
    0,   /* xFinalize */

    /* Interpreter deletion notifications */
    0,   /* xPreDeleteInterp (nothing to clean up) */
    0,   /* xDeleteInterp (nothing to clean up) */

    /* Memory (provided by th8_libc.c via merge) */
    0,   /* xMalloc */
    0,   /* xRealloc */
    0,   /* xFree */
    0,   /* xMemorySize */
    0,   /* xNeedMemory */

    /* C runtime mem ops (provided by th8_libc.c via merge) */
    0,   /* xMemcpy */
    0,   /* xMemmove */
    0,   /* xMemset */
    0,   /* xMemcmp */

    /* C runtime string/utility (provided by th8_libc.c via merge) */
    0,   /* xStrlen */
    0,   /* xStrcmp */
    0,   /* xStrchr */
    0,   /* xAtoi */
    0,   /* xQsort */
    0,   /* xVsnprintf */

    /* Mutex callbacks (none) */
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
    th8NullInput, /* xInput */

    /* Output */
    th8NullOutput, /* xOutput */
    th8NullOutputError, /* xOutputError */

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
    th8NullNormalizePath, /* xNormalizePath */
    th8NullGetCwd, /* xGetCwd */
    0,   /* xSetCwd (no file system access) */

    /* Executable path */
    0,   /* xGetExePath (not available) */
    0,   /* xGetRealPath */
    0,   /* xGetRootPath */
    0,   /* xSameFile */

    /* Data retrieval */
    th8NullGetData, /* xGetData */
    th8NullDataExists, /* xDataExists */

    /* Binary loading (forbidden) */
    th8NullLoad, /* xLoad */
    th8NullUnload, /* xUnload */

    /* Time (none) */
    0,   /* xTimeMs */
    0,   /* xTimeUs */

    /* Sleep (none) */
    0,   /* xSleep */

    /* Process ID (none) */
    0,   /* xGetPid */

    /* User name, host name */
    0,   /* xGetUserName */
    0,   /* xGetHostName */
    0,   /* xGetEnv */
    0,   /* xKeyValue */

    /* Stack bounds (none) */
    0,   /* xGetStackBounds */

    /* Parent PID, thread ID (none) */
    0,   /* xGetParentPid */
    0,   /* xGetThreadId */

    /* Error code */
    0,   /* xGetLastError */
    0,   /* xSetLastError */

    /* Trace (none) */
    0,   /* xEmitTrace */

    /* Panic (none -- must be provided by embedder or libc merge) */
    0,   /* xPanic */

    /* Math functions (provided by th8_libc.c via merge) */
    0,   /* xMathFunc */

    /* Random bytes (none) */
    0,   /* xRandomBytes */

    /* DNS (none) */
    0, 0,  /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics -- the th8_unwind (compiler-runtime) layer supplies xStackBackTrace. */
    0, /* xStackBackTrace */

    /* 64-bit atomics */
    0, /* xIntCmpXchg64 */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetNullIoPlatform --
 *
 *	Return a pointer to the null I/O platform implementation.
 *
 * Why / How:
 *	Returns the address of the module-level static platform
 *	struct.  The caller typically copies or merges this with
 *	other platform layers before passing to Th8_CreateInterp.
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
Th8_GetNullIoPlatform(void)
{
    return &th8NullIoPlatformData;
}

#endif /* TH8_PLATFORM_NULLIO */
