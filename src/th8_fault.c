/*
 * th8_fault.c -- Fault-injection platform layer for TH8.
 *
 * Provides a composable platform wrapper that intercepts platform
 * callbacks and selectively injects failures.  Designed for
 * systematic testing of error-handling paths.
 *
 * The fault layer can be dynamically installed on and removed from
 * a running interpreter:
 *
 *     Th8_FaultConfig cfg;
 *     Th8_FaultCtx ctx;
 *     Th8_FaultConfigInit(&cfg);
 *     cfg.nAllocFailAfter = 100;
 *
 *     Th8_FaultInstall(interp, &cfg, &ctx);
 *     // ... exercise the interpreter; allocation #100 will fail ...
 *     Th8_FaultUninstall(interp, &ctx);
 *
 * The Th8_FaultCtx must remain valid between Install and Uninstall.
 * Stack allocation is fine for single-function test sequences.
 *
 * Every platform callback is wrapped with a passthrough that
 * extracts the real pCtx from the fault context before delegating,
 * ensuring correct context routing for all callbacks.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_plat.h"
#include "th8.h"

#if defined(TH8_ENABLE_FAULT_INJECTION)

#  include "th8_int.h"
#  include "th8_int_core.h"


/*
 *----------------------------------------------------------------------
 *
 * Th8_FaultCtx -- Internal context for the fault layer.
 *
 * Th8_FaultConfig is declared in th8.h (public API).
 *
 *----------------------------------------------------------------------
 */

struct Th8_FaultCtx {
    Th8_Platform faultPlatform;     /* The fault-wrapping platform. */
    Th8_Platform *pSavedPlatform;   /* Original interp->pPlatform. */
    Th8_FaultConfig *pConfig;     /* Caller-owned configuration. */
};

/*
 * Currently-active fault config (process global).  Read by
 * th8_cache.c::Th8_FindInCache to consult nFailCacheLookupMask
 * without taking a platform-callback dispatch path.  The fault
 * layer is single-threaded per interp; tests serialize fault
 * eval calls so this global is safe in test context.  NULL
 * when no fault is installed.
 */
Th8_FaultConfig *th8FaultActiveCfg = NULL;


/*
 *----------------------------------------------------------------------
 * Fault decision helper.
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8FaultShouldFailAlloc --
 *
 *	Decide whether the current allocation request should be
 *	failed by the fault-injection layer.
 *
 * Why / How:
 *	Increments the running allocation counter, then checks two
 *	conditions: (1) has the counter reached nAllocFailAfter (the
 *	first fault point), and (2) for subsequent allocations, does
 *	the distance from nAllocFailAfter divide evenly by
 *	nAllocFailInterval (periodic re-fault).  This allows tests
 *	to trigger a single OOM at a precise allocation number, or a
 *	repeating pattern of OOMs for stress testing.
 *
 * Results:
 *	1 if the allocation should fail, 0 otherwise.
 *
 * Side effects:
 *	Increments pCfg->nAllocCount.  On failure, increments
 *	pCfg->nAllocFailCount.
 *
 *----------------------------------------------------------------------
 */

/*
 * th8FaultStrEqAscii --
 *	NUL-terminated byte-equality check used to compare a
 *	filter file name against the current allocation site's
 *	__FILE__ string.  Avoids the <string.h> dependency in
 *	core/fault code (project convention).
 */

int
th8FaultStrEqAscii(const char *a, const char *b)
{
    if (a == b) return 1;
    /* Bug 26: current callers gate with non-NULL checks, but a
     * future caller could legitimately pass NULL.  Use plain `if`
     * so the guard survives TH8_OMIT. */
    /* Split per Finding 005. */
    if (!a) return 0;
    if (!b) return 0;
    while (*a == *b) {
	if (*a == 0) return 1;
	a++;
	b++;
    }
    return 0;
}

/*
 * th8FaultPathMatchesBaseName --
 *	Returns non-zero when zPath ends with the path component
 *	zBase, where the boundary preceding zBase is either a path
 *	separator ('/' or '\\') or the start of the string.  Used so
 *	users can supply a bare basename like "th8_control.c" and
 *	match an __FILE__ value of "src/plugins/th8_control.c".
 *	An exact match (zPath equals zBase) also returns non-zero.
 *	Both arguments must be non-NULL.
 */

int
th8FaultPathMatchesBaseName(const char *zPath, const char *zBase)
{
    int nPath = 0, nBase = 0;
    int i;

    while (zPath[nPath] != 0)
	nPath++;
    while (zBase[nBase] != 0)
	nBase++;
    if (nBase == 0 || nBase > nPath) return 0;

    /* Compare trailing nBase bytes. */
    for (i = 0; i < nBase; i++) {
	if (zPath[nPath - nBase + i] != zBase[i]) return 0;
    }

    /* Boundary check: the byte preceding the matched suffix must
     * be a path separator, or the suffix must start at offset 0. */
    if (nPath == nBase) return 1;
    {
	char c = zPath[nPath - nBase - 1];

	/* Split per Finding 005 sec. 5b: C2 ('\\') intrinsic-
	 * dead on POSIX in the test corpus (no backslash path
	 * separators); preserved as defense-in-depth for Win32. */
	if (c == '/') return 1;
	if (c == '\\') return 1;
    }
    return 0;
}

/*
 * th8FaultMatchFilter --
 *	Return non-zero if the current allocation site (pCfg->zCurFile,
 *	pCfg->nCurLine) matches at least one entry in the filter list.
 *	If the filter list is empty (nFilter == 0), every allocation
 *	matches (back-compat with the count-only mode).  Increments
 *	the matching filter's nHit counter.
 */

static int
th8FaultMatchFilter(Th8_FaultConfig *pCfg)
{
    int i;

    /* Bug 26 (2026-06-07): plain check -- public struct, external init
     * could leave nFilter>0 with aFilter==NULL.  NEVER would SEGV.
     * Split per Finding 005 sec. 5b: C2 (aFilter==NULL with
     * nFilter>0) intrinsic-dead in test corpus (testlib initialises
     * both together). */
    if (pCfg->nFilter <= 0) return 1;
    if (pCfg->aFilter == NULL) return 1;
    for (i = 0; i < pCfg->nFilter; i++) {
	Th8_FaultFilter *pF = &pCfg->aFilter[i];
	if (pF->zFile && pCfg->zCurFile == NULL) {
	    continue;
	}
	if (pF->zFile && !th8FaultStrEqAscii(pCfg->zCurFile, pF->zFile) &&
	    !th8FaultPathMatchesBaseName(pCfg->zCurFile, pF->zFile)) {
	    continue;
	}
	if (pF->nLineFrom > 0) {
	    int lineLo = pF->nLineFrom;
	    int lineHi = (pF->nLineTo > 0) ? pF->nLineTo : pF->nLineFrom;
	    if (pCfg->nCurLine < lineLo || pCfg->nCurLine > lineHi) {
		continue;
	    }
	}
	pF->nHit++;
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8FaultShouldFailAlloc --
 *
 *	Allocator-side fault-injection oracle: consults the
 *	current `Th8_FaultConfig` and reports whether the
 *	allocation now being attempted should be made to
 *	fail.  Drives `th8FaultMallocPanic` and the
 *	non-panic allocator wrappers.
 *
 *	The decision uses three knobs:
 *	  *  `Th8_FaultFilter` (optional) -- only matching
 *	     call sites count toward the budget.  Non-matching
 *	     allocations pass through unaffected and do NOT
 *	     increment `nAllocCount`, so the filter is
 *	     transparent on its non-matching arm.
 *	  *  `nAllocFailAfter` -- the 1-based index of the
 *	     first allocation to fail.  `<= 0` disables.
 *	  *  `nAllocFailInterval` -- after the first failure,
 *	     fail every Nth subsequent matching allocation.
 *	     `<= 0` disables the recurring fault.
 *
 *	On every triggered failure the helper bumps
 *	`pCfg->nAllocFailCount` so script-side tests can
 *	verify the fault actually fired.
 *
 * Parameters:
 *	pCfg -- live fault config.
 *
 * Returns:
 *	1 if the caller should treat this allocation as
 *	failed; 0 otherwise.
 *
 * Side effects:
 *	May increment `pCfg->nAllocCount` and
 *	`pCfg->nAllocFailCount`.
 *
 *----------------------------------------------------------------------
 */
static int
th8FaultShouldFailAlloc(Th8_FaultConfig *pCfg)
{
    th8_int64_t n;

    if (!th8FaultMatchFilter(pCfg)) {
	return 0;
    }

    pCfg->nAllocCount++;
    n = pCfg->nAllocCount;

    if (pCfg->nAllocFailAfter <= 0) return 0;

    if (n == pCfg->nAllocFailAfter) {
	pCfg->nAllocFailCount++;
	return 1;
    }

    if (n > pCfg->nAllocFailAfter && pCfg->nAllocFailInterval > 0) {
	th8_int64_t nSince = n - pCfg->nAllocFailAfter;
	if ((nSince % pCfg->nAllocFailInterval) == 0) {
	    pCfg->nAllocFailCount++;
	    return 1;
	}
    }

    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Passthrough generation macros.
 *
 * Every callback must be wrapped so that the real platform's pCtx
 * is passed instead of the Th8_FaultCtx*.  These macros generate
 * thin passthroughs for common callback signatures.
 *
 *----------------------------------------------------------------------
 */

#  define REAL(ctx_) (((Th8_FaultCtx *)(ctx_))->pSavedPlatform)
#  define RCTX(ctx_) (REAL(ctx_)->pCtx)

/* (interp, pCtx) -> rettype */
#  define PT_0(name, R)                                                      \
      static R pt_##name(Th8_Interp *i, void *c)                             \
      {                                                                      \
	  return REAL(c)->name ? REAL(c)->name(i, RCTX(c)) : (R)0;           \
      }

#  define PT_V0(name)                                                        \
      static void pt_##name(Th8_Interp *i, void *c)                          \
      {                                                                      \
	  if (REAL(c)->name) REAL(c)->name(i, RCTX(c));                      \
      }

/* (interp, pCtx, a1) -> rettype */
#  define PT_1(name, R, T1)                                                  \
      static R pt_##name(Th8_Interp *i, void *c, T1 a1)                      \
      {                                                                      \
	  return REAL(c)->name ? REAL(c)->name(i, RCTX(c), a1) : (R)0;       \
      }

#  define PT_V1(name, T1)                                                    \
      static void pt_##name(Th8_Interp *i, void *c, T1 a1)                   \
      {                                                                      \
	  if (REAL(c)->name) REAL(c)->name(i, RCTX(c), a1);                  \
      }

/* (interp, pCtx, a1, a2) -> rettype */
#  define PT_2(name, R, T1, T2)                                              \
      static R pt_##name(Th8_Interp *i, void *c, T1 a1, T2 a2)               \
      {                                                                      \
	  return REAL(c)->name ? REAL(c)->name(i, RCTX(c), a1, a2) : (R)0;   \
      }

#  define PT_V2(name, T1, T2)                                                \
      static void pt_##name(Th8_Interp *i, void *c, T1 a1, T2 a2)            \
      {                                                                      \
	  if (REAL(c)->name) REAL(c)->name(i, RCTX(c), a1, a2);              \
      }

/* (interp, pCtx, a1, a2, a3) -> rettype */
#  define PT_3(name, R, T1, T2, T3)                                          \
      static R pt_##name(Th8_Interp *i, void *c, T1 a1, T2 a2, T3 a3)        \
      {                                                                      \
	  return REAL(c)->name ? REAL(c)->name(i, RCTX(c), a1, a2, a3)       \
	                       : (R)0;                                       \
      }

#  define PT_V3(name, T1, T2, T3)                                            \
      static void pt_##name(Th8_Interp *i, void *c, T1 a1, T2 a2, T3 a3)     \
      {                                                                      \
	  if (REAL(c)->name) REAL(c)->name(i, RCTX(c), a1, a2, a3);          \
      }

/* (interp, pCtx, a1, a2, a3, a4) -> rettype */
#  define PT_4(name, R, T1, T2, T3, T4)                                      \
      static R pt_##name(Th8_Interp *i, void *c, T1 a1, T2 a2, T3 a3, T4 a4) \
      {                                                                      \
	  return REAL(c)->name ? REAL(c)->name(i, RCTX(c), a1, a2, a3, a4)   \
	                       : (R)0;                                       \
      }

#  define PT_V4(name, T1, T2, T3, T4)                                        \
      static void                                                            \
      pt_##name(Th8_Interp *i, void *c, T1 a1, T2 a2, T3 a3, T4 a4)          \
      {                                                                      \
	  if (REAL(c)->name) REAL(c)->name(i, RCTX(c), a1, a2, a3, a4);      \
      }

/* (interp, pCtx, a1, a2, a3, a4, a5) -> rettype */
#  define PT_5(name, R, T1, T2, T3, T4, T5)                                  \
      static R                                                               \
      pt_##name(Th8_Interp *i, void *c, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5)   \
      {                                                                      \
	  return REAL(c)->name                                               \
	           ? REAL(c)->name(i, RCTX(c), a1, a2, a3, a4, a5)           \
	           : (R)0;                                                   \
      }

/* (interp, pCtx, a1, a2, a3, a4, a5, a6) -> rettype */
#  define PT_6(name, R, T1, T2, T3, T4, T5, T6)                              \
      static R pt_##name(                                                    \
	  Th8_Interp *i, void *c, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6)  \
      {                                                                      \
	  return REAL(c)->name                                               \
	           ? REAL(c)->name(i, RCTX(c), a1, a2, a3, a4, a5, a6)       \
	           : (R)0;                                                   \
      }


/*
 *----------------------------------------------------------------------
 * Generate ALL passthroughs.
 *----------------------------------------------------------------------
 */

/* Lifecycle */
PT_0(xInitialize, int)
PT_V0(xFinalize)
PT_V0(xPreDeleteInterp)
PT_V0(xDeleteInterp)

/* Memory -- handled by fault-injecting wrappers below, not passthroughs */

/* Byte operations */
PT_3(xMemcpy, void *, void *, const void *, size_t)
PT_3(xMemmove, void *, void *, const void *, size_t)
PT_3(xMemset, void *, void *, int, size_t)
PT_3(xMemcmp, int, const void *, const void *, size_t)

/* String/utility */
PT_1(xStrlen, size_t, const char *)
PT_2(xStrcmp, int, const char *, const char *)
PT_2(xStrchr, char *, const char *, int)
PT_1(xAtoi, int, const char *)

/*
 *----------------------------------------------------------------------
 *
 * pt_xQsort --
 *
 *	Passthrough wrapper for the xQsort platform callback.
 *
 * Why / How:
 *	xQsort has a complex signature (function pointer argument)
 *	that cannot be expressed by the PT_* generation macros, so
 *	it is written as a manual passthrough.  Extracts the real
 *	platform's pCtx from the fault context and delegates to the
 *	real xQsort if present; otherwise does nothing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the real xQsort does (sorts the array in place).
 *
 *----------------------------------------------------------------------
 */

static void
pt_xQsort(
    Th8_Interp *i,
    void *c,
    void *base,
    size_t nmemb,
    size_t size,
    int (*cmp)(const void *, const void *))
{
    if (REAL(c)->xQsort) REAL(c)->xQsort(i, RCTX(c), base, nmemb, size, cmp);
}

/*
 *----------------------------------------------------------------------
 *
 * pt_xVsnprintf --
 *
 *	Passthrough wrapper for the xVsnprintf platform callback.
 *
 * Why / How:
 *	xVsnprintf takes a va_list argument that cannot be expressed
 *	by the PT_* generation macros, so it is written as a manual
 *	passthrough.  Extracts the real platform's pCtx from the
 *	fault context and delegates to the real xVsnprintf if
 *	present; returns 0 if not.
 *
 * Results:
 *	Number of characters written, or 0 if no real callback.
 *
 * Side effects:
 *	Writes formatted output to the provided buffer.
 *
 *----------------------------------------------------------------------
 */

static int
pt_xVsnprintf(
    Th8_Interp *i,
    void *c,
    char *buf,
    size_t size,
    const char *fmt,
    va_list ap)
{
    if (REAL(c)->xVsnprintf)
	return REAL(c)->xVsnprintf(i, RCTX(c), buf, size, fmt, ap);
    return 0;
}

/* Mutex / sync */
PT_V1(xMutexInit, Th8_Mutex *)
PT_V1(xMutexFinal, Th8_Mutex *)
PT_V1(xMutexEnter, Th8_Mutex *)
PT_V1(xMutexLeave, Th8_Mutex *)
PT_3(xIntCmpXchg, int, volatile int *, int, int)
PT_V0(xMemBarrier)

/* I/O core */
PT_3(xInput, int, char **, size_t *, void *)
PT_3(xOutput, int, const char *, size_t, void *)
PT_3(xOutputError, int, const char *, size_t, void *)

/* I/O redirection */
PT_1(xGetInput, int, void **)
PT_1(xSetInput, int, void *)
PT_1(xGetOutput, int, void **)
PT_1(xSetOutput, int, void *)
PT_1(xGetErrorOutput, int, void **)
PT_1(xSetErrorOutput, int, void *)

/*
 *----------------------------------------------------------------------
 *
 * pt_xChannelControl --
 *
 *	Passthrough wrapper for the xChannelControl platform callback.
 *
 * Why / How:
 *	xChannelControl has six arguments beyond interp/pCtx, which
 *	exceeds the highest-arity PT_* macro.  Written as a manual
 *	passthrough.  Extracts the real pCtx from the fault context
 *	and delegates; returns TH8_ERROR if no real callback.
 *
 * Results:
 *	Return code from the real callback, or TH8_ERROR.
 *
 * Side effects:
 *	Whatever the real xChannelControl does.
 *
 *----------------------------------------------------------------------
 */

static int
pt_xChannelControl(
    Th8_Interp *i,
    void *c,
    void *pCh,
    int op,
    th8_int64_t a1,
    int a2,
    th8_int64_t *pR,
    void *pB)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    /* F3 fault-injection: per-op failure flags.  Check before
     * dispatching to the real callback so we never let the
     * filesystem see the operation when the test is meant to
     * exercise the error arm. */
    if (op == TH8_CHANCTL_READ) {
	if (pF->pConfig->bFailChannelRead) {
	    if (pR) *pR = 0;
	    return TH8_ERROR;
	}
	if (pF->pConfig->bFailChannelEOF) {
	    /* Synthesise premature EOF: success rc, 0 bytes read. */
	    if (pR) *pR = 0;
	    return TH8_OK;
	}
    } else if (op == TH8_CHANCTL_WRITE && pF->pConfig->bFailChannelWrite) {
	if (pR) *pR = 0;
	return TH8_ERROR;
    } else if (op == TH8_CHANCTL_OPEN && pF->pConfig->bFailChannelOpen) {
	if (pR) *pR = 0;
	return TH8_ERROR;
    }
    if (REAL(c)->xChannelControl)
	return REAL(c)->xChannelControl(i, RCTX(c), pCh, op, a1, a2, pR, pB);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * pt_xGetTemporaryData --
 *
 *	Passthrough wrapper for the xGetTemporaryData platform callback.
 *
 * Why / How:
 *	xGetTemporaryData has four arguments beyond interp/pCtx,
 *	which exceeds the PT_4 macro's return-type arity.  Written
 *	as a manual passthrough.  Extracts the real pCtx and
 *	delegates; returns TH8_ERROR if no real callback.
 *
 * Results:
 *	Return code from the real callback, or TH8_ERROR.
 *
 * Side effects:
 *	Whatever the real xGetTemporaryData does.
 *
 *----------------------------------------------------------------------
 */

static int
pt_xGetTemporaryData(
    Th8_Interp *i,
    void *c,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppCh)
{
    if (REAL(c)->xGetTemporaryData)
	return REAL(c)
	    ->xGetTemporaryData(i, RCTX(c), nSize, pzOut, pnOut, ppCh);
    return TH8_ERROR;
}

PT_2(xDeleteTemporaryData, int, const char *, size_t)

/*
 *----------------------------------------------------------------------
 *
 * pt_xSetTemporaryData --
 *
 *	Passthrough wrapper for the xSetTemporaryData platform callback.
 *
 * Why / How:
 *	xSetTemporaryData has four arguments beyond interp/pCtx with
 *	mixed types that do not fit the PT_4 macro cleanly.  Written
 *	as a manual passthrough.  Extracts the real pCtx and
 *	delegates; returns TH8_ERROR if no real callback.
 *
 * Results:
 *	Return code from the real callback, or TH8_ERROR.
 *
 * Side effects:
 *	Whatever the real xSetTemporaryData does.
 *
 *----------------------------------------------------------------------
 */

static int
pt_xSetTemporaryData(
    Th8_Interp *i,
    void *c,
    const char *zN,
    size_t nN,
    th8_uint64_t nO,
    th8_uint64_t nL)
{
    if (REAL(c)->xSetTemporaryData)
	return REAL(c)->xSetTemporaryData(i, RCTX(c), zN, nN, nO, nL);
    return TH8_ERROR;
}

PT_3(xCloseTemporaryData, int, const char *, size_t, void *)

/* Filesystem -- xGetCwd is handled by the fi_xGetCwd wrapper below,
 * so no PT_0 passthrough is generated for it. */
PT_2(xNormalizePath, char *, const char *, size_t)
PT_2(xSetCwd, int, const char *, size_t)
PT_0(xGetExePath, char *)
PT_4(xGetRealPath, int, const char *, size_t, char *, size_t)
PT_4(xGetRootPath, int, const char *, size_t, char *, size_t)
PT_4(xSameFile, int, const char *, size_t, const char *, size_t)

/* Data retrieval -- xGetData and xDataExists handled by fault wrappers below */

/* Binary loading */
PT_4(xLoad, int, const char *, size_t, const char *, size_t)
PT_5(xUnload, int, const char *, size_t, const char *, size_t, int)

/* Time -- xTimeMs is handled by the fi_xTimeMs wrapper below, so no
 * PT_1 passthrough is generated for it. */
PT_1(xTimeUs, int, th8_int64_t *)
PT_V1(xSleep, int)

/* Process/host info */
PT_0(xGetPid, int)
PT_2(xGetUserName, int, char *, size_t)
PT_2(xGetHostName, int, char *, size_t)
/* xGetEnv is handled by the fi_xGetEnv wrapper below, so no PT_1
 * passthrough is generated for it. */
PT_5(xKeyValue, int, int, const char *, size_t, const char *, size_t)
PT_2(xGetStackBounds, int, void **, size_t *)
PT_0(xGetParentPid, int)
PT_0(xGetThreadId, th8_uint64_t)

/* Error/diagnostics */
PT_0(xGetLastError, int)
PT_V1(xSetLastError, int)
PT_V1(xEmitTrace, const char *)

/*
 *----------------------------------------------------------------------
 *
 * pt_xPanic --
 *
 *	Passthrough wrapper for the xPanic platform callback.
 *
 * Why / How:
 *	xPanic takes a message string and length (2 data args after
 *	interp/pCtx), which does not match the void-returning PT_V2
 *	macro signature (PT_V2 takes typed args, not const char* +
 *	size_t).  Written as a manual passthrough.  Extracts the
 *	real pCtx and delegates if the real callback is present.
 *
 * Results:
 *	None (xPanic does not return in normal operation).
 *
 * Side effects:
 *	Whatever the real xPanic does (typically terminates the
 *	process).
 *
 *----------------------------------------------------------------------
 */

static void
pt_xPanic(Th8_Interp *i, void *c, const char *zMsg, size_t nMsg)
{
    if (REAL(c)->xPanic) REAL(c)->xPanic(i, RCTX(c), zMsg, nMsg);
}

/* Diagnostics -- stack backtrace capture (nVersion 5).  Pure passthrough;
 * not fault-injected (a failed capture simply yields fewer frames). */
PT_3(xStackBackTrace, int, void **, int, int)

/* Math/entropy -- xMathFunc and xRandomBytes are fault-injecting
 * (wrappers defined below).  Their passthrough behaviour is the
 * default; bFailMathFunc / bFailRandomBytes flip them to always
 * fail. */


/*
 *----------------------------------------------------------------------
 *
 * Fault-injecting callback wrappers.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * fi_xMalloc --
 *
 *	Fault-injecting wrapper for the xMalloc platform callback.
 *
 * Why / How:
 *	Consults th8FaultShouldFailAlloc to decide whether this
 *	allocation should return NULL (simulating OOM).  If the
 *	fault fires, NULL is returned immediately without calling
 *	the real allocator.  Otherwise the real xMalloc is invoked
 *	via the passthrough pattern (extracting the real pCtx from
 *	the fault context).
 *
 * Results:
 *	Pointer to allocated memory, or NULL on injected fault or
 *	real allocation failure.
 *
 * Side effects:
 *	May allocate memory via the real xMalloc.  Updates the
 *	fault config counters.
 *
 *----------------------------------------------------------------------
 */

static void *
fi_xMalloc(Th8_Interp *i, void *c, size_t n)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (th8FaultShouldFailAlloc(pF->pConfig)) return NULL;
    if (REAL(c)->xMalloc) return REAL(c)->xMalloc(i, RCTX(c), n);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xRealloc --
 *
 *	Fault-injecting wrapper for the xRealloc platform callback.
 *
 * Why / How:
 *	Same pattern as fi_xMalloc: consults th8FaultShouldFailAlloc
 *	and returns NULL on injected fault.  Realloc shares the
 *	same allocation counter as malloc so that the Nth allocation
 *	(regardless of whether it is a fresh alloc or a realloc) can
 *	be targeted precisely.
 *
 * Results:
 *	Pointer to reallocated memory, or NULL on injected fault or
 *	real reallocation failure.
 *
 * Side effects:
 *	May reallocate memory via the real xRealloc.  Updates the
 *	fault config counters.
 *
 *----------------------------------------------------------------------
 */

static void *
fi_xRealloc(Th8_Interp *i, void *c, void *p, size_t n)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (th8FaultShouldFailAlloc(pF->pConfig)) return NULL;
    if (REAL(c)->xRealloc) return REAL(c)->xRealloc(i, RCTX(c), p, n);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xFree --
 *
 *	Passthrough (non-faulting) wrapper for the xFree platform
 *	callback.
 *
 * Why / How:
 *	Faulting xFree (i.e. silently dropping a free) would leak
 *	memory, which is not a useful failure mode for testing.
 *	Therefore this wrapper always delegates to the real xFree
 *	unconditionally.  Uses the same passthrough pattern as the
 *	other wrappers for pCtx routing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory via the real xFree.
 *
 *----------------------------------------------------------------------
 */

static void
fi_xFree(Th8_Interp *i, void *c, void *p)
{
    /* Never fault -- leaking is not a useful test. */
    if (REAL(c)->xFree) REAL(c)->xFree(i, RCTX(c), p);
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xMemorySize --
 *
 *	Passthrough (non-faulting) wrapper for the xMemorySize
 *	platform callback.
 *
 * Why / How:
 *	xMemorySize queries the size of an existing allocation and
 *	does not allocate or free.  There is no useful failure mode,
 *	so this wrapper always delegates to the real callback.  Uses
 *	the same passthrough pattern for pCtx routing.
 *
 * Results:
 *	Size of the allocation in bytes, or 0 if no real callback.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static size_t
fi_xMemorySize(Th8_Interp *i, void *c, void *p)
{
    if (REAL(c)->xMemorySize) return REAL(c)->xMemorySize(i, RCTX(c), p);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xGetData --
 *
 *	Fault-injecting wrapper for the xGetData platform callback.
 *
 * Why / How:
 *	If bFailGetData is set in the fault config, every xGetData
 *	call is failed immediately (outputs zeroed, TH8_ERROR
 *	returned) without calling the real callback.  This allows
 *	tests to exercise code paths that handle missing or
 *	inaccessible data files.  The call counter and fail counter
 *	are always updated for test introspection.  Otherwise
 *	delegates to the real xGetData via the passthrough pattern.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on injected fault or real
 *	failure.
 *
 * Side effects:
 *	Increments pConfig->nGetDataCount.  On injected fault,
 *	increments pConfig->nGetDataFailCount.
 *
 *----------------------------------------------------------------------
 */

static int
fi_xGetData(
    Th8_Interp *i,
    void *c,
    const char *zN,
    size_t nN,
    char **pzO,
    size_t *pnO)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    pF->pConfig->nGetDataCount++;
    if (pF->pConfig->bFailGetData) {
	pF->pConfig->nGetDataFailCount++;
	*pzO = 0;
	*pnO = 0;
	return TH8_ERROR;
    }
    if (REAL(c)->xGetData)
	return REAL(c)->xGetData(i, RCTX(c), zN, nN, pzO, pnO);
    *pzO = 0;
    *pnO = 0;
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xDataExists --
 *
 *	Fault-injecting wrapper for the xDataExists platform callback.
 *
 * Why / How:
 *	If bFailDataExists is set in the fault config, returns 0
 *	immediately (data not found) without calling the real
 *	callback.  This allows tests to exercise code paths that
 *	handle absent data entries.  Otherwise delegates to the real
 *	xDataExists via the passthrough pattern.
 *
 * Results:
 *	Non-zero if the data entry exists, 0 if not (or on injected
 *	fault).
 *
 * Side effects:
 *	None beyond the real callback's side effects (if called).
 *
 *----------------------------------------------------------------------
 */

static int
fi_xDataExists(Th8_Interp *i, void *c, const char *zN, size_t nN, int *pA)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (pF->pConfig->bFailDataExists) return 0;
    if (REAL(c)->xDataExists)
	return REAL(c)->xDataExists(i, RCTX(c), zN, nN, pA);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xRandomBytes --
 *
 *	Fault-injecting wrapper for the xRandomBytes platform callback.
 *
 * Why / How:
 *	If bFailRandomBytes is set in the fault config, returns
 *	TH8_ERROR immediately without calling the real callback.
 *	This allows tests to exercise code paths that handle
 *	entropy source failures (e.g. the load-gate token
 *	generation in th8_load.c).  Otherwise delegates to the real
 *	xRandomBytes via the passthrough pattern.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on injected fault or real
 *	failure.
 *
 * Side effects:
 *	May fill pBuf with random bytes via the real callback.
 *
 *----------------------------------------------------------------------
 */

static int
fi_xRandomBytes(Th8_Interp *i, void *c, void *pBuf, size_t nByte)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (pF->pConfig->bFailRandomBytes) return TH8_ERROR;
    /* Bug 49 (2026-06-09): one-shot RNG-byte forcing for the
     * th8_load.c reserved-token retry vectors.  When the counter
     * is positive, copy aForceRandomBytes (truncated/zero-padded
     * to the caller's nByte) into the buffer and decrement.
     * Nested per Finding 005 sec. 5b: C2 (pBuf==NULL with
     * count>0) intrinsic-dead -- xRandomBytes is always called
     * with a valid buffer. */
    if (pF->pConfig->nForceRandomBytesCount > 0)
	if (pBuf != NULL) {
	    size_t n = nByte;
	    unsigned char *pOut = (unsigned char *)pBuf;
	    size_t i;
	    for (i = 0; i < n; i++) {
		pOut[i] = (i < sizeof(pF->pConfig->aForceRandomBytes))
		            ? pF->pConfig->aForceRandomBytes[i]
		            : (unsigned char)0;
	    }
	    pF->pConfig->nForceRandomBytesCount--;
	    return TH8_OK;
	}
    if (REAL(c)->xRandomBytes)
	return REAL(c)->xRandomBytes(i, RCTX(c), pBuf, nByte);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xMathFunc --
 *
 *	Fault-injecting wrapper for the xMathFunc platform callback.
 *	Returns TH8_ERROR unconditionally when bFailMathFunc is set;
 *	otherwise delegates to the real callback.  Used to drive
 *	the C3=F vector on `pPlat->xMathFunc(...) == TH8_OK` style
 *	compounds across the math/expr layer without nulling the
 *	slot (which would short-circuit at C2 instead).
 *
 *----------------------------------------------------------------------
 */

static int
fi_xMathFunc(
    Th8_Interp *i,
    void *c,
    int op,
    double *pResult,
    double a,
    double b)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (pF->pConfig->bFailMathFunc) return TH8_ERROR;
    if (REAL(c)->xMathFunc)
	return REAL(c)->xMathFunc(i, RCTX(c), op, pResult, a, b);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xGetCwd --
 *
 *	Fault-injecting wrapper for the xGetCwd platform callback.
 *	Returns NULL unconditionally when bFailGetCwd is set;
 *	otherwise delegates to the real callback.  Used to drive
 *	the `if (zCwd == NULL)` error arms in consumers of
 *	Th8_GetCwd (pwd_command and friends) without actually
 *	stat-failing the filesystem.
 *
 *----------------------------------------------------------------------
 */

static char *
fi_xGetCwd(Th8_Interp *i, void *c)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (pF->pConfig->bFailGetCwd) return NULL;
    if (REAL(c)->xGetCwd) return REAL(c)->xGetCwd(i, RCTX(c));
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xTimeMs --
 *
 *	Fault-injecting wrapper for the xTimeMs platform callback.
 *	Returns TH8_ERROR when bFailTimeMs is set; otherwise
 *	delegates.  Used to drive clock-failure error arms in
 *	[clock milliseconds] and other Th8_GetTimeMs consumers.
 *
 *----------------------------------------------------------------------
 */

static int
fi_xTimeMs(Th8_Interp *i, void *c, th8_int64_t *pMs)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (pF->pConfig->bFailTimeMs) return TH8_ERROR;
    if (REAL(c)->xTimeMs) return REAL(c)->xTimeMs(i, RCTX(c), pMs);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * fi_xGetEnv --
 *
 *	Fault-injecting wrapper for the xGetEnv platform callback.
 *	Returns NULL when bFailGetEnv is set; otherwise delegates.
 *	Used to drive `if (zVal == NULL)` error arms in Th8_GetEnv
 *	consumers (env-array initialization, [info env], etc.).
 *
 *----------------------------------------------------------------------
 */

static char *
fi_xGetEnv(Th8_Interp *i, void *c, const char *zName)
{
    Th8_FaultCtx *pF = (Th8_FaultCtx *)c;

    if (pF->pConfig->bFailGetEnv) return NULL;
    if (REAL(c)->xGetEnv) return REAL(c)->xGetEnv(i, RCTX(c), zName);
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FaultConfigInit --
 *
 *	Initialize fault config to safe defaults (no faults injected).
 *
 * Why / How:
 *	Zeroes every field in the config so the fault layer is a
 *	pure passthrough until the caller explicitly enables one or
 *	more fault triggers.  This is the required first step before
 *	calling Th8_FaultInstall.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Zeroes all fields of *pCfg.
 *
 *----------------------------------------------------------------------
 */

void
Th8_FaultConfigInit(Th8_FaultConfig *pCfg)
{
    if (!pCfg) return;
    pCfg->nAllocFailAfter = 0;
    pCfg->nAllocFailInterval = 0;
    pCfg->bFailGetData = 0;
    pCfg->bFailDataExists = 0;
    pCfg->bFailRandomBytes = 0;
    pCfg->bFailMathFunc = 0;
    pCfg->bFailGetCwd = 0;
    pCfg->bFailTimeMs = 0;
    pCfg->bFailGetEnv = 0;
    pCfg->bFailChannelRead = 0;
    pCfg->bFailChannelEOF = 0;
    pCfg->bFailChannelWrite = 0;
    pCfg->bFailChannelOpen = 0;
    pCfg->nFailEmbeddedKey0 = 0;
    pCfg->nFailEmbeddedKeyRoot = 0;
    pCfg->nFailEmbeddedKeyTest = 0;
    pCfg->nFailCacheLookupMask = 0;
    pCfg->nCacheHookFires = 0;
    pCfg->nCacheLookupSkip = 0;
    pCfg->nFailOsslMask = 0;
    pCfg->nFailPosixMask = 0;
    {
	int j;
	for (j = 0; j < (int)sizeof(pCfg->aForceRandomBytes); j++) {
	    pCfg->aForceRandomBytes[j] = 0;
	}
    }
    pCfg->nForceRandomBytesCount = 0;
    pCfg->aFilter = NULL;
    pCfg->nFilter = 0;
    pCfg->zCurFile = NULL;
    pCfg->nCurLine = 0;
    pCfg->azNullCallbacks = NULL;
    pCfg->nNullCallbacks = 0;
    pCfg->nAllocCount = 0;
    pCfg->nAllocFailCount = 0;
    pCfg->nGetDataCount = 0;
    pCfg->nGetDataFailCount = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FaultWireCallbacks --
 *
 *	Wire up ALL callbacks on the fault platform.  For each
 *	callback slot: if the real platform has it, install the
 *	passthrough (or fault-injecting) wrapper.  Otherwise NULL.
 *
 * Why / How:
 *	Iterates every callback slot in the platform struct.  Most
 *	slots get a simple passthrough (pt_*) that routes the real
 *	pCtx through.  The memory slots (xMalloc, xRealloc, xFree,
 *	xMemorySize), data slots (xGetData, xDataExists), and
 *	entropy slot (xRandomBytes) get fault-injecting wrappers
 *	(fi_*) that can selectively fail.  Slots that the real
 *	platform does not implement are left NULL so the interpreter
 *	sees the same capability surface as without the fault layer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Populates all callback slots in *pF.
 *
 *----------------------------------------------------------------------
 */

static void
th8FaultWireCallbacks(
    Th8_Platform *pF, /* Fault platform to populate. */
    const Th8_Platform *pR) /* Real platform (read-only). */
{
    pF->nVersion = pR->nVersion;

    /* Lifecycle */
    pF->xInitialize = pR->xInitialize ? pt_xInitialize : 0;
    pF->xFinalize = pR->xFinalize ? pt_xFinalize : 0;
    pF->xPreDeleteInterp = pR->xPreDeleteInterp ? pt_xPreDeleteInterp : 0;
    pF->xDeleteInterp = pR->xDeleteInterp ? pt_xDeleteInterp : 0;

    /* Memory -- fault-injecting wrappers */
    pF->xMalloc = pR->xMalloc ? fi_xMalloc : 0;
    pF->xRealloc = pR->xRealloc ? fi_xRealloc : 0;
    pF->xFree = pR->xFree ? fi_xFree : 0;
    pF->xMemorySize = pR->xMemorySize ? fi_xMemorySize : 0;

    /* Byte operations */
    pF->xMemcpy = pR->xMemcpy ? pt_xMemcpy : 0;
    pF->xMemmove = pR->xMemmove ? pt_xMemmove : 0;
    pF->xMemset = pR->xMemset ? pt_xMemset : 0;
    pF->xMemcmp = pR->xMemcmp ? pt_xMemcmp : 0;

    /* String/utility */
    pF->xStrlen = pR->xStrlen ? pt_xStrlen : 0;
    pF->xStrcmp = pR->xStrcmp ? pt_xStrcmp : 0;
    pF->xStrchr = pR->xStrchr ? pt_xStrchr : 0;
    pF->xAtoi = pR->xAtoi ? pt_xAtoi : 0;
    pF->xQsort = pR->xQsort ? pt_xQsort : 0;
    pF->xVsnprintf = pR->xVsnprintf ? pt_xVsnprintf : 0;

    /* Mutex / sync */
    pF->xMutexInit = pR->xMutexInit ? pt_xMutexInit : 0;
    pF->xMutexFinal = pR->xMutexFinal ? pt_xMutexFinal : 0;
    pF->xMutexEnter = pR->xMutexEnter ? pt_xMutexEnter : 0;
    pF->xMutexLeave = pR->xMutexLeave ? pt_xMutexLeave : 0;
    pF->xIntCmpXchg = pR->xIntCmpXchg ? pt_xIntCmpXchg : 0;
    pF->xMemBarrier = pR->xMemBarrier ? pt_xMemBarrier : 0;

    /* I/O core */
    pF->xInput = pR->xInput ? pt_xInput : 0;
    pF->xOutput = pR->xOutput ? pt_xOutput : 0;
    pF->xOutputError = pR->xOutputError ? pt_xOutputError : 0;

    /* I/O redirection */
    pF->xGetInput = pR->xGetInput ? pt_xGetInput : 0;
    pF->xSetInput = pR->xSetInput ? pt_xSetInput : 0;
    pF->xGetOutput = pR->xGetOutput ? pt_xGetOutput : 0;
    pF->xSetOutput = pR->xSetOutput ? pt_xSetOutput : 0;
    pF->xGetErrorOutput = pR->xGetErrorOutput ? pt_xGetErrorOutput : 0;
    pF->xSetErrorOutput = pR->xSetErrorOutput ? pt_xSetErrorOutput : 0;

    /* Channel / temporary data */
    pF->xChannelControl = pR->xChannelControl ? pt_xChannelControl : 0;
    pF->xGetTemporaryData = pR->xGetTemporaryData ? pt_xGetTemporaryData : 0;
    pF->xDeleteTemporaryData = pR->xDeleteTemporaryData
                                 ? pt_xDeleteTemporaryData
                                 : 0;
    pF->xSetTemporaryData = pR->xSetTemporaryData ? pt_xSetTemporaryData : 0;
    pF->xCloseTemporaryData = pR->xCloseTemporaryData ? pt_xCloseTemporaryData
                                                      : 0;

    /* Filesystem */
    pF->xNormalizePath = pR->xNormalizePath ? pt_xNormalizePath : 0;
    pF->xGetCwd = pR->xGetCwd ? fi_xGetCwd : 0;
    pF->xSetCwd = pR->xSetCwd ? pt_xSetCwd : 0;
    pF->xGetExePath = pR->xGetExePath ? pt_xGetExePath : 0;
    pF->xGetRealPath = pR->xGetRealPath ? pt_xGetRealPath : 0;
    pF->xGetRootPath = pR->xGetRootPath ? pt_xGetRootPath : 0;
    pF->xSameFile = pR->xSameFile ? pt_xSameFile : 0;

    /* Data retrieval -- fault-injecting wrappers */
    pF->xGetData = pR->xGetData ? fi_xGetData : 0;
    pF->xDataExists = pR->xDataExists ? fi_xDataExists : 0;

    /* Binary loading */
    pF->xLoad = pR->xLoad ? pt_xLoad : 0;
    pF->xUnload = pR->xUnload ? pt_xUnload : 0;

    /* Time */
    pF->xTimeMs = pR->xTimeMs ? fi_xTimeMs : 0;
    pF->xTimeUs = pR->xTimeUs ? pt_xTimeUs : 0;
    pF->xSleep = pR->xSleep ? pt_xSleep : 0;

    /* Process/host */
    pF->xGetPid = pR->xGetPid ? pt_xGetPid : 0;
    pF->xGetUserName = pR->xGetUserName ? pt_xGetUserName : 0;
    pF->xGetHostName = pR->xGetHostName ? pt_xGetHostName : 0;
    pF->xGetEnv = pR->xGetEnv ? fi_xGetEnv : 0;
    pF->xKeyValue = pR->xKeyValue ? pt_xKeyValue : 0;
    pF->xGetStackBounds = pR->xGetStackBounds ? pt_xGetStackBounds : 0;
    pF->xGetParentPid = pR->xGetParentPid ? pt_xGetParentPid : 0;
    pF->xGetThreadId = pR->xGetThreadId ? pt_xGetThreadId : 0;

    /* Error/diagnostics */
    pF->xGetLastError = pR->xGetLastError ? pt_xGetLastError : 0;
    pF->xSetLastError = pR->xSetLastError ? pt_xSetLastError : 0;
    pF->xEmitTrace = pR->xEmitTrace ? pt_xEmitTrace : 0;
    pF->xPanic = pR->xPanic ? pt_xPanic : 0;
    pF->xStackBackTrace = pR->xStackBackTrace ? pt_xStackBackTrace : 0;

    /* Math / entropy -- both xMathFunc and xRandomBytes are
     * fault-injecting (bFailMathFunc / bFailRandomBytes flags). */
    pF->xMathFunc = pR->xMathFunc ? fi_xMathFunc : 0;
    pF->xRandomBytes = pR->xRandomBytes ? fi_xRandomBytes : 0;

    /* DNS (passthrough; testlib overrides at Th8_Platform layer
     * by installing custom xDnsResolve directly). */
    pF->xDnsResolve = pR->xDnsResolve;
    pF->xDnsResolveFree = pR->xDnsResolveFree;

    /* Second-chance allocator (passthrough, not fault-injected) */
    pF->xNeedMemory = pR->xNeedMemory;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FaultCallbackSlots --
 *
 *	Lookup table mapping callback name -> byte offset within
 *	Th8_Platform.  Used by th8FaultApplyNullCallbacks to convert
 *	a name passed via Th8_FaultConfig.azNullCallbacks into the
 *	specific slot to overwrite with NULL.
 *
 *	Names match the slot identifiers exactly ("xMutexEnter",
 *	"xPanic", etc.) so script-side tests can use the same names
 *	that appear in source code.  Listed roughly in the same
 *	order as th8FaultWireCallbacks above for ease of audit.
 *
 *----------------------------------------------------------------------
 */

struct th8FaultSlotEntry {
    const char *zName;
    size_t off;
};

#  define TH8_FAULT_SLOT(name) {#name, offsetof(Th8_Platform, name)}

static const struct th8FaultSlotEntry th8FaultCallbackSlots[] =
    {TH8_FAULT_SLOT(xInitialize),
     TH8_FAULT_SLOT(xFinalize),
     TH8_FAULT_SLOT(xPreDeleteInterp),
     TH8_FAULT_SLOT(xDeleteInterp),
     TH8_FAULT_SLOT(xMalloc),
     TH8_FAULT_SLOT(xRealloc),
     TH8_FAULT_SLOT(xFree),
     TH8_FAULT_SLOT(xMemorySize),
     TH8_FAULT_SLOT(xMemcpy),
     TH8_FAULT_SLOT(xMemmove),
     TH8_FAULT_SLOT(xMemset),
     TH8_FAULT_SLOT(xMemcmp),
     TH8_FAULT_SLOT(xStrlen),
     TH8_FAULT_SLOT(xStrcmp),
     TH8_FAULT_SLOT(xStrchr),
     TH8_FAULT_SLOT(xAtoi),
     TH8_FAULT_SLOT(xQsort),
     TH8_FAULT_SLOT(xVsnprintf),
     TH8_FAULT_SLOT(xMutexInit),
     TH8_FAULT_SLOT(xMutexFinal),
     TH8_FAULT_SLOT(xMutexEnter),
     TH8_FAULT_SLOT(xMutexLeave),
     TH8_FAULT_SLOT(xIntCmpXchg),
     TH8_FAULT_SLOT(xMemBarrier),
     TH8_FAULT_SLOT(xInput),
     TH8_FAULT_SLOT(xOutput),
     TH8_FAULT_SLOT(xOutputError),
     TH8_FAULT_SLOT(xGetInput),
     TH8_FAULT_SLOT(xSetInput),
     TH8_FAULT_SLOT(xGetOutput),
     TH8_FAULT_SLOT(xSetOutput),
     TH8_FAULT_SLOT(xGetErrorOutput),
     TH8_FAULT_SLOT(xSetErrorOutput),
     TH8_FAULT_SLOT(xChannelControl),
     TH8_FAULT_SLOT(xGetTemporaryData),
     TH8_FAULT_SLOT(xDeleteTemporaryData),
     TH8_FAULT_SLOT(xSetTemporaryData),
     TH8_FAULT_SLOT(xCloseTemporaryData),
     TH8_FAULT_SLOT(xNormalizePath),
     TH8_FAULT_SLOT(xGetCwd),
     TH8_FAULT_SLOT(xSetCwd),
     TH8_FAULT_SLOT(xGetExePath),
     TH8_FAULT_SLOT(xGetRealPath),
     TH8_FAULT_SLOT(xGetRootPath),
     TH8_FAULT_SLOT(xSameFile),
     TH8_FAULT_SLOT(xGetData),
     TH8_FAULT_SLOT(xDataExists),
     TH8_FAULT_SLOT(xLoad),
     TH8_FAULT_SLOT(xUnload),
     TH8_FAULT_SLOT(xTimeMs),
     TH8_FAULT_SLOT(xTimeUs),
     TH8_FAULT_SLOT(xSleep),
     TH8_FAULT_SLOT(xGetPid),
     TH8_FAULT_SLOT(xGetUserName),
     TH8_FAULT_SLOT(xGetHostName),
     TH8_FAULT_SLOT(xGetEnv),
     TH8_FAULT_SLOT(xKeyValue),
     TH8_FAULT_SLOT(xGetStackBounds),
     TH8_FAULT_SLOT(xGetParentPid),
     TH8_FAULT_SLOT(xGetThreadId),
     TH8_FAULT_SLOT(xGetLastError),
     TH8_FAULT_SLOT(xSetLastError),
     TH8_FAULT_SLOT(xEmitTrace),
     TH8_FAULT_SLOT(xPanic),
     TH8_FAULT_SLOT(xStackBackTrace),
     TH8_FAULT_SLOT(xMathFunc),
     TH8_FAULT_SLOT(xRandomBytes),
     TH8_FAULT_SLOT(xNeedMemory)};

#  undef TH8_FAULT_SLOT


/*
 *----------------------------------------------------------------------
 *
 * th8FaultApplyNullCallbacks --
 *
 *	Walk pCfg->azNullCallbacks and overwrite each named slot in
 *	*pF with NULL.  Returns TH8_OK on success or TH8_ERROR if
 *	any name does not match a known slot.  When TH8_ERROR is
 *	returned, *pzBadName is set to point at the offending name
 *	(borrowed from the caller's array).
 *
 * Why / How:
 *	After th8FaultWireCallbacks has installed pt_/fi_ trampolines
 *	for every slot the real platform implements, this function
 *	selectively NULLs back specific slots.  The downstream
 *	consumer of the platform sees NULL and takes the false-vector
 *	branch of an `if (pPlat->xXxx)` style check -- the path that
 *	the wrapped (always-non-NULL) trampoline would otherwise hide.
 *
 *	The lookup is linear over a ~67-entry table, which is fine
 *	for the test-only use case.  If this list grows, consider
 *	sorting the table and switching to bsearch.
 *
 *----------------------------------------------------------------------
 */

static int
th8FaultApplyNullCallbacks(
    Th8_Platform *pF,
    const Th8_FaultConfig *pCfg,
    const char **pzBadName)
{
    int i, j;
    const size_t nSlot = sizeof(th8FaultCallbackSlots) /
                         sizeof(th8FaultCallbackSlots[0]);

    /* Bug 26 (2026-06-07): plain check -- public struct could be partially
     * initialized by the caller. */
    if (pCfg->nNullCallbacks <= 0 || pCfg->azNullCallbacks == NULL) {
	return TH8_OK;
    }

    for (i = 0; i < pCfg->nNullCallbacks; i++) {
	const char *zName = pCfg->azNullCallbacks[i];
	int found = 0;

	if (zName == NULL) continue;

	for (j = 0; j < (int)nSlot; j++) {
	    if (th8FaultStrEqAscii(zName, th8FaultCallbackSlots[j].zName)) {
		void **ppSlot = (void **)(void *)((char *)pF +
		                                  th8FaultCallbackSlots[j]
		                                      .off);
		*ppSlot = NULL;
		found = 1;
		break;
	    }
	}

	if (!found) {
	    if (pzBadName) *pzBadName = zName;
	    return TH8_ERROR;
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FaultInstall --
 *
 *	Dynamically install the fault-injection layer on a running
 *	interpreter.  Saves the current platform and swaps in the
 *	fault-wrapping platform.
 *
 *	pConfig and pCtx must remain valid until Th8_FaultUninstall.
 *	Stack allocation is fine for single-function test sequences.
 *
 * Why / How:
 *	Saves a pointer to the interpreter's current platform in the
 *	fault context, builds a complete fault platform (via
 *	th8FaultWireCallbacks) whose pCtx points back to the fault
 *	context, then atomically swaps the interpreter's pPlatform.
 *	After this, every platform callback the interpreter makes
 *	goes through the fault layer first.  The passthrough pattern
 *	ensures correct pCtx routing: the fault context wraps the
 *	real platform's pCtx and unwraps it before delegating.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if any argument is NULL.
 *
 * Side effects:
 *	Replaces interp->pPlatform with the fault platform.
 *
 *----------------------------------------------------------------------
 */

int
Th8_FaultInstall(
    Th8_Interp *interp,
    Th8_FaultConfig *pConfig,
    Th8_FaultCtx *pCtx)
{
    if (!interp || !pConfig || !pCtx) return TH8_ERROR;

    /* Save the current platform pointer. */
    pCtx->pSavedPlatform = interp->pPlatform;
    pCtx->pConfig = pConfig;

    /* Build the fault platform with proper passthroughs. */
    th8FaultWireCallbacks(&pCtx->faultPlatform, interp->pPlatform);
    pCtx->faultPlatform.pCtx = pCtx;

    /* Apply null-callback overrides (test-only).  If any name is
     * unknown, leave the saved-platform pointer untouched and
     * report which name was bad via the interp result so callers
     * can diagnose typos. */
    {
	const char *zBad = NULL;

	if (th8FaultApplyNullCallbacks(
	        &pCtx->faultPlatform, pConfig, &zBad) != TH8_OK) {
	    pCtx->pSavedPlatform = NULL;
	    if (zBad) {
		Th8_ErrorMessage(
		    interp, "fault: unknown null-callback name \"", zBad,
		    TH8_NOLEN);
	    } else {
		Th8_SetResultStatic(
		    interp, "fault: null-callback application failed",
		    TH8_NOLEN);
	    }
	    return TH8_ERROR;
	}
    }

    /* Swap the interpreter's platform pointer. */
    interp->pPlatform = &pCtx->faultPlatform;

    /* Publish this config as the active one so non-platform
     * fault hooks (e.g. th8_cache.c per-type lookup mask) can
     * consult it without dispatching through a callback. */
    th8FaultActiveCfg = pConfig;

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FaultUninstall --
 *
 *	Remove the fault-injection layer from a running interpreter.
 *	Restores the original platform.  After this call, pCtx may
 *	be freed or go out of scope.
 *
 * Why / How:
 *	Swaps interp->pPlatform back to the saved original platform
 *	pointer and zeroes pSavedPlatform in the fault context so a
 *	double-uninstall is detected as an error.  The fault context
 *	and config are no longer referenced after this call.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if arguments are NULL or if
 *	no saved platform exists (already uninstalled).
 *
 * Side effects:
 *	Restores interp->pPlatform to the pre-install value.
 *
 *----------------------------------------------------------------------
 */

int
Th8_FaultUninstall(Th8_Interp *interp, Th8_FaultCtx *pCtx)
{
    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!pCtx) return TH8_ERROR;
    if (!pCtx->pSavedPlatform) return TH8_ERROR;

    /* Restore the original platform. */
    interp->pPlatform = pCtx->pSavedPlatform;
    pCtx->pSavedPlatform = 0;

    /* Clear the active-config publication so post-uninstall
     * cache lookups behave normally. */
    if (th8FaultActiveCfg == pCtx->pConfig) {
	th8FaultActiveCfg = NULL;
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FaultCtxSize --
 *
 *	Return sizeof(Th8_FaultCtx) so external callers can allocate
 *	the context without including internal headers.
 *
 * Why / How:
 *	Th8_FaultCtx is an opaque struct defined only in this file.
 *	External test harnesses need to know the size to allocate
 *	storage (stack or heap) without exposing the internal struct
 *	layout.  This function provides that size as a runtime query.
 *
 * Results:
 *	sizeof(Th8_FaultCtx).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
Th8_FaultCtxSize(void)
{
    return sizeof(Th8_FaultCtx);
}


/*
 *----------------------------------------------------------------------
 *
 * th8FaultStashSite --
 *
 *	Record the call-site (zFile, nLine) of the upcoming
 *	allocation into the fault config attached to interp, if a
 *	fault layer is currently installed.  Called by the
 *	Th8_Safe* allocation wrappers immediately before they
 *	dispatch to the platform's xMalloc, so that the fault
 *	layer's filter check (th8FaultMatchFilter) can see which
 *	source location is allocating.
 *
 * Why / How:
 *	When fault injection is installed, interp->pPlatform is the
 *	embedded fault platform whose pCtx points back to its
 *	enclosing Th8_FaultCtx (set in Th8_FaultInstall).  We
 *	verify that self-reference before treating the pCtx as a
 *	Th8_FaultCtx*.  When no fault layer is installed (or this
 *	build has TH8_ENABLE_FAULT_INJECTION disabled), this is a
 *	no-op.
 *
 *	The site information is stashed into pConfig->zCurFile and
 *	pConfig->nCurLine, where it is read by
 *	th8FaultMatchFilter on the very next allocation dispatch
 *	(single-threaded eval per interp guarantees no
 *	intervening allocation can race in).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May update pConfig->zCurFile and pConfig->nCurLine on the
 *	active fault config.  Otherwise no-op.
 *
 *----------------------------------------------------------------------
 */

void
th8FaultStashSite(Th8_Interp *interp, const char *zFile, int nLine)
{
    Th8_FaultCtx *pCtx;

    /* Bug 26: public-API entry guard -- use plain `if`.
     * Split per Finding 005 sec. 5b: C2 (pPlatform==NULL with
     * non-NULL interp) intrinsic-dead in the test corpus
     * (interp always initialised with a platform). */
    if (!interp) return;
    if (!interp->pPlatform) return;
    pCtx = (Th8_FaultCtx *)interp->pPlatform->pCtx;
    if (!pCtx) return;
    /* Self-consistency: only the fault platform has its own pCtx
     * pointing to a Th8_FaultCtx whose embedded faultPlatform IS
     * interp->pPlatform.  Any other platform with a coincidentally
     * non-NULL pCtx will fail this check. */
    if (interp->pPlatform != &pCtx->faultPlatform) return;
    if (!pCtx->pConfig) return;
    pCtx->pConfig->zCurFile = zFile;
    pCtx->pConfig->nCurLine = nLine;
}


#endif /* TH8_ENABLE_FAULT_INJECTION */
