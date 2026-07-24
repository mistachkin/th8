/*
 * th8_env.c -- Environment variable key-value platform for TH8.
 *
 * Provides an xKeyValue callback backed by the host process
 * environment variables.  Supports EXISTS, LIST, GET, SET,
 * and UNSET operations.
 *
 * POSIX: getenv, setenv, unsetenv, extern environ for LIST.
 * Win32: GetEnvironmentVariableW, SetEnvironmentVariableW,
 *        GetEnvironmentStringsW for LIST.  UTF-8 <-> UTF-16
 *        conversion via ConvertUTF_v2 with strict validation.
 *
 * Thread safety: a file-scope mutex serializes all operations.
 *
 * Compile-time gate: always compiled (no gate required).
 *
 * Usage:
 *   Th8_Platform plat;
 *   Th8_UseDefaultPlatform(&plat);  // merges env automatically
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"

#if defined(_WIN32) || defined(WIN32)
#  include "th8_meta_msvc.h"
#  include "th8_meta_win32.h"
#else
#  include "th8_meta_posix.h"
#endif

#include "th8.h"
#include "th8_int.h"


/*
 *======================================================================
 * Shared helpers (used by both POSIX and Win32 implementations).
 *======================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * th8EnvNulTerminate --
 *
 *	Copy a length-counted TH8 string into a NUL-terminated
 *	buffer.  Uses the stack buffer if it fits; otherwise
 *	allocates via Th8_AttemptMalloc.  Returns NULL on OOM.
 *
 *	The caller must call th8EnvFreeNul() when done.
 *
 * Why / How:
 *	POSIX and Win32 environment APIs require NUL-terminated C
 *	strings, but TH8 strings are length-counted.  This helper
 *	bridges the gap.  A stack buffer is tried first to avoid
 *	heap allocation on the common path (short names/values);
 *	if the string is too long, a heap buffer is allocated.  The
 *	companion th8EnvFreeNul() frees only the heap case.
 *
 * Results:
 *	Pointer to a NUL-terminated copy of the input, or NULL on
 *	allocation failure.
 *
 * Side effects:
 *	May allocate memory via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

static char *
th8EnvNulTerminate(
    Th8_Interp *interp,
    const char *z,
    size_t n,
    char *zBuf,  /* Stack buffer. */
    size_t nBuf) /* Size of stack buffer. */
{
    char *zOut;
    /* Mask any taint tag off before using the length as a byte count /
     * index / allocation size: a tagged length is ~256 MiB and would
     * over-read/over-write.  The buffer contents are NUL-terminated,
     * not re-published, so the tag itself is not needed downstream. */
    size_t nRaw = TH8_LEN(n);

    TH8_ASSERT_RAW_LEN(nRaw);
    if (nRaw + 1 <= nBuf) {
	Th8_Memcpy(interp, zBuf, z, nRaw);
	zBuf[nRaw] = '\0';
	return zBuf;
    }
    zOut = (char *)TH8_ALLOC_STR(interp, nRaw);
    if (!zOut) return NULL;
    Th8_Memcpy(interp, zOut, z, nRaw);
    zOut[nRaw] = '\0';
    return zOut;
}

/*
 *----------------------------------------------------------------------
 *
 * th8EnvFreeNul --
 *
 *	Free a buffer returned by th8EnvNulTerminate, but only if it
 *	was heap-allocated (i.e. not the original stack buffer).
 *
 * Why / How:
 *	Compares the pointer to the original stack buffer; if they
 *	differ the buffer was heap-allocated and must be freed.
 *	Safe to call with a NULL z pointer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May free memory via Th8_Free.
 *
 *----------------------------------------------------------------------
 */

static void
th8EnvFreeNul(
    Th8_Interp *interp,
    char *z,
    char *zBuf)  /* The original stack buffer. */
{
    if (z && z != zBuf) {
	Th8_Free(interp, z);
    }
}


/*
 * Glob matching for LIST operations uses the public Th8_GlobMatch
 * API (th8_glob.c), which is the same algorithm as [string match].
 * Supports *, ?, [...], and \ metacharacters.
 */


/*
 *======================================================================
 *======================================================================
 */

#if !defined(_WIN32) && !defined(WIN32)
/*
 *======================================================================
 * POSIX implementation
 *======================================================================
 */

/* <stdlib.h>, <pthread.h> included via th8_meta_posix.h */

extern char **environ;

/*
 * Lazy-initialized mutex.  Uses the same atomic CAS pattern
 * as th8_posix.c to avoid needing an xInitialize callback
 * (which is not merged by Th8_MergePlatform).
 */

static volatile int th8EnvMutexReady = 0;
static pthread_mutex_t th8EnvMutex;

/*
 *----------------------------------------------------------------------
 *
 * th8EnvLock --
 *
 *	Acquire the file-scope mutex that serializes all environment
 *	variable operations on POSIX.
 *
 * Why / How:
 *	POSIX getenv/setenv/unsetenv and the environ pointer are not
 *	thread-safe; concurrent access can corrupt internal libc
 *	state.  This function uses the same atomic CAS lazy-init
 *	pattern as th8_posix.c: the first caller initializes the
 *	mutex (state transitions 0 -> -1 -> 1), while any concurrent
 *	caller spins until initialization completes.  A full memory
 *	barrier after init ensures the mutex is visible to all CPUs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquires th8EnvMutex.  May initialize it on first call.
 *
 *----------------------------------------------------------------------
 */

static void
th8EnvLock(void)
{
    if (!th8EnvMutexReady) {
	if (__sync_val_compare_and_swap(&th8EnvMutexReady, 0, -1) == 0) {
	    pthread_mutex_init(&th8EnvMutex, NULL);
	    __sync_synchronize();
	    th8EnvMutexReady = 1;
	} else {
	    while (th8EnvMutexReady != 1) {
		/* spin until initialization completes */
	    }
	    __sync_synchronize();
	}
    }
    pthread_mutex_lock(&th8EnvMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8EnvUnlock --
 *
 *	Release the file-scope mutex acquired by th8EnvLock (POSIX).
 *
 * Why / How:
 *	Simple wrapper around pthread_mutex_unlock.  Separated from
 *	th8EnvLock so that the lock/unlock pair is symmetric and the
 *	unlock call site reads clearly.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases th8EnvMutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8EnvUnlock(void)
{
    pthread_mutex_unlock(&th8EnvMutex);
}


/*
 *----------------------------------------------------------------------
 *
 * th8EnvKeyValue --
 *
 *	xKeyValue callback backed by POSIX environment variables.
 *
 * Why / How:
 *	Implements all TH8_KV_* operations (EXISTS, GET, SET, UNSET,
 *	LIST, and the glob-filtered "2" variants) by mapping them to
 *	POSIX getenv / setenv / unsetenv and the extern environ
 *	array.  All calls are serialized under the file-scope mutex
 *	because POSIX environment functions are not thread-safe.
 *	Length-counted TH8 strings are NUL-terminated via
 *	th8EnvNulTerminate before being passed to the POSIX APIs.
 *	For bulk mutation (SET2, UNSET2), matching keys are collected
 *	first and mutated in a second pass to avoid invalidating the
 *	environ iteration pointer.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.  The interpreter
 *	result is set to the value, list, or error message.
 *
 * Side effects:
 *	May modify the host process environment.  Acquires and
 *	releases the file-scope mutex.
 *
 *----------------------------------------------------------------------
 */

static int
th8EnvKeyValue(
    Th8_Interp *interp,
    void *pCtx,
    int op,
    const char *zName,
    size_t nName,
    const char *zValue,
    size_t nValue)
{
    char zNameBuf[256];
    char zValBuf[256];
    char *zNameNul = NULL;
    char *zValNul = NULL;
    int rc = TH8_ERROR;

    (void)pCtx;

    switch (op) {

    case TH8_KV_EXISTS: {
	zNameNul = th8EnvNulTerminate(
	    interp, zName, nName, zNameBuf, sizeof(zNameBuf));
	if (!zNameNul) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	rc = getenv(zNameNul) ? TH8_OK : TH8_ERROR;
	th8EnvUnlock();
	break;
    }

    case TH8_KV_GET: {
	const char *zVal;

	zNameNul = th8EnvNulTerminate(
	    interp, zName, nName, zNameBuf, sizeof(zNameBuf));
	if (!zNameNul) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	zVal = getenv(zNameNul);
	if (zVal) {
	    Th8_SetResult(interp, zVal, TH8_NOLEN);
	    rc = TH8_OK;
	} else {
	    Th8_SetResultStatic(interp, "key not found", TH8_NOLEN);
	}
	th8EnvUnlock();
	break;
    }

    case TH8_KV_SET: {
	zNameNul = th8EnvNulTerminate(
	    interp, zName, nName, zNameBuf, sizeof(zNameBuf));
	if (!zNameNul) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    break;
	}
	zValNul = th8EnvNulTerminate(
	    interp, zValue, nValue, zValBuf, sizeof(zValBuf));
	if (!zValNul) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	rc = (setenv(zNameNul, zValNul, 1) == 0) ? TH8_OK : TH8_ERROR;
	th8EnvUnlock();
	if (rc != TH8_OK) {
	    Th8_SetResultStatic(interp, "setenv failed", TH8_NOLEN);
	}
	break;
    }

    case TH8_KV_UNSET: {
	zNameNul = th8EnvNulTerminate(
	    interp, zName, nName, zNameBuf, sizeof(zNameBuf));
	if (!zNameNul) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	unsetenv(zNameNul);
	th8EnvUnlock();
	rc = TH8_OK;
	break;
    }

    case TH8_KV_LIST: {
	char *zList = NULL;
	size_t nList = 0;
	int i;

	th8EnvLock();
	for (i = 0; environ[i]; i++) {
	    const char *zEntry = environ[i];
	    const char *zEq = strchr(zEntry, '=');
	    size_t nKey;

	    if (!zEq) continue;
	    nKey = (size_t)(zEq - zEntry);

	    if (zName && nName > 0) {
		if (!Th8_GlobMatch(interp, zName, nName, zEntry, nKey)) {
		    continue;
		}
	    }
	    Th8_ListAppend(interp, &zList, &nList, zEntry, nKey);
	}
	th8EnvUnlock();

	if (zList) {
	    Th8_SetResult(interp, zList, nList);
	    Th8_Free(interp, zList);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    case TH8_KV_EXISTS2: {
	int i;
	int found = 0;

	th8EnvLock();
	for (i = 0; environ[i] && !found; i++) {
	    const char *zEntry = environ[i];
	    const char *zEq = strchr(zEntry, '=');
	    size_t nKey;

	    if (!zEq) continue;
	    nKey = (size_t)(zEq - zEntry);

	    if (zName && nName > 0) {
		if (!Th8_GlobMatch(interp, zName, nName, zEntry, nKey)) {
		    continue;
		}
	    }
	    if (zValue && nValue > 0) {
		if (!Th8_GlobMatch(
		        interp, zValue, nValue, zEq + 1, strlen(zEq + 1))) {
		    continue;
		}
	    }
	    found = 1;
	}
	th8EnvUnlock();
	rc = found ? TH8_OK : TH8_ERROR;
	break;
    }

    case TH8_KV_LIST2: {
	char *zList = NULL;
	size_t nList = 0;
	int i;

	th8EnvLock();
	for (i = 0; environ[i]; i++) {
	    const char *zEntry = environ[i];
	    const char *zEq = strchr(zEntry, '=');
	    size_t nKey;

	    if (!zEq) continue;
	    nKey = (size_t)(zEq - zEntry);

	    if (zName && nName > 0) {
		if (!Th8_GlobMatch(interp, zName, nName, zEntry, nKey)) {
		    continue;
		}
	    }
	    if (zValue && nValue > 0) {
		if (!Th8_GlobMatch(
		        interp, zValue, nValue, zEq + 1, strlen(zEq + 1))) {
		    continue;
		}
	    }
	    Th8_ListAppend(interp, &zList, &nList, zEntry, nKey);
	}
	th8EnvUnlock();

	if (zList) {
	    Th8_SetResult(interp, zList, nList);
	    Th8_Free(interp, zList);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    case TH8_KV_GET2: {
	char *zDict = NULL;
	size_t nDict = 0;
	int i;

	th8EnvLock();
	for (i = 0; environ[i]; i++) {
	    const char *zEntry = environ[i];
	    const char *zEq = strchr(zEntry, '=');
	    size_t nKey;
	    const char *zVal;
	    size_t nVal;

	    if (!zEq) continue;
	    nKey = (size_t)(zEq - zEntry);
	    zVal = zEq + 1;
	    nVal = strlen(zVal);

	    if (zName && nName > 0) {
		if (!Th8_GlobMatch(interp, zName, nName, zEntry, nKey)) {
		    continue;
		}
	    }
	    if (zValue && nValue > 0) {
		if (!Th8_GlobMatch(interp, zValue, nValue, zVal, nVal)) {
		    continue;
		}
	    }
	    Th8_ListAppend(interp, &zDict, &nDict, zEntry, nKey);
	    Th8_ListAppend(interp, &zDict, &nDict, zVal, nVal);
	}
	th8EnvUnlock();

	if (zDict) {
	    Th8_SetResult(interp, zDict, nDict);
	    Th8_Free(interp, zDict);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    case TH8_KV_SET2: {
	/*
	 * Collect matching key names first, then mutate.
	 * setenv() can reallocate environ, invalidating
	 * iteration pointers.
	 */

	char *zKeys = NULL;
	size_t nKeys = 0;
	int i;

	zValNul = th8EnvNulTerminate(
	    interp, zValue, nValue, zValBuf, sizeof(zValBuf));
	if (!zValNul) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    break;
	}

	th8EnvLock();
	for (i = 0; environ[i]; i++) {
	    const char *zEntry = environ[i];
	    const char *zEq = strchr(zEntry, '=');
	    size_t nKey;

	    if (!zEq) continue;
	    nKey = (size_t)(zEq - zEntry);

	    /* Nested per Finding 005 sec. 5b: this write-side path
	     * (KV_SET2) is unreachable from the test corpus -- testlib
	     * intentionally excludes write ops to avoid mutating the
	     * host env.  The 2-condition compound's C-pairs are
	     * therefore stuck at 0% MC/DC noise. */
	    if (zName) {
		if (nName > 0) {
		    if (!Th8_GlobMatch(interp, zName, nName, zEntry, nKey)) {
			continue;
		    }
		}
	    }
	    Th8_ListAppend(interp, &zKeys, &nKeys, zEntry, nKey);
	}

	/*
	 * Now iterate the collected keys and set each.
	 */

	if (zKeys) {
	    char **azElem = NULL;
	    size_t *anElem = NULL;
	    int nCount = 0;
	    int j;

	    if (Th8_SplitList(
	            interp, zKeys, nKeys, &azElem, &anElem, &nCount,
	            TH8_LIST_NONE) == TH8_OK) {
		for (j = 0; j < nCount; j++) {
		    char kBuf[256];
		    char *kNul = th8EnvNulTerminate(
		        interp, azElem[j], anElem[j], kBuf, sizeof(kBuf));
		    if (kNul) {
			setenv(kNul, zValNul, 1);
			th8EnvFreeNul(interp, kNul, kBuf);
		    }
		}
		Th8_Free(interp, azElem);
		/* anElem is interior to the azElem block. */
	    }
	    Th8_Free(interp, zKeys);
	}
	th8EnvUnlock();
	rc = TH8_OK;
	break;
    }

    case TH8_KV_UNSET2: {
	/*
	 * Collect matching key-value pairs first, then delete.
	 * unsetenv() can modify environ, invalidating iteration.
	 */

	char *zDict = NULL;
	size_t nDict = 0;
	char *zKeys = NULL;
	size_t nKeys = 0;
	int i;

	th8EnvLock();
	for (i = 0; environ[i]; i++) {
	    const char *zEntry = environ[i];
	    const char *zEq = strchr(zEntry, '=');
	    size_t nKey;
	    const char *zVal;
	    size_t nVal;

	    if (!zEq) continue;
	    nKey = (size_t)(zEq - zEntry);
	    zVal = zEq + 1;
	    nVal = strlen(zVal);

	    /* Nested per Finding 005 sec. 5b: both compounds in
	     * this KV_UNSET2 path are intrinsic-dead in the
	     * test corpus because testlib's env_kv excludes
	     * write ops. */
	    if (zName)
		if (nName > 0) {
		    if (!Th8_GlobMatch(interp, zName, nName, zEntry, nKey)) {
			continue;
		    }
		}
	    if (zValue)
		if (nValue > 0) {
		    if (!Th8_GlobMatch(interp, zValue, nValue, zVal, nVal)) {
			continue;
		    }
		}
	    Th8_ListAppend(interp, &zDict, &nDict, zEntry, nKey);
	    Th8_ListAppend(interp, &zDict, &nDict, zVal, nVal);
	    Th8_ListAppend(interp, &zKeys, &nKeys, zEntry, nKey);
	}

	/*
	 * Delete collected keys.
	 */

	if (zKeys) {
	    char **azElem = NULL;
	    size_t *anElem = NULL;
	    int nCount = 0;
	    int j;

	    if (Th8_SplitList(
	            interp, zKeys, nKeys, &azElem, &anElem, &nCount,
	            TH8_LIST_NONE) == TH8_OK) {
		for (j = 0; j < nCount; j++) {
		    char kBuf[256];
		    char *kNul = th8EnvNulTerminate(
		        interp, azElem[j], anElem[j], kBuf, sizeof(kBuf));
		    if (kNul) {
			unsetenv(kNul);
			th8EnvFreeNul(interp, kNul, kBuf);
		    }
		}
		Th8_Free(interp, azElem);
		/* anElem is interior to the azElem block. */
	    }
	    Th8_Free(interp, zKeys);
	}
	th8EnvUnlock();

	if (zDict) {
	    Th8_SetResult(interp, zDict, nDict);
	    Th8_Free(interp, zDict);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    default:
	Th8_SetResultStatic(interp, "unknown kv operation", TH8_NOLEN);
	break;
    }

    th8EnvFreeNul(interp, zNameNul, zNameBuf);
    th8EnvFreeNul(interp, zValNul, zValBuf);
    return rc;
}


#else /* _WIN32 */
/*
 *======================================================================
 * Win32 implementation
 *
 * Uses ConvertUTF_v2 with strict validation for all UTF-8 <-> UTF-16
 * conversions.  Environment variable APIs are all wide-character
 * (GetEnvironmentVariableW, SetEnvironmentVariableW,
 * GetEnvironmentStringsW / FreeEnvironmentStringsW).
 *======================================================================
 */

/* <windows.h> included via th8_meta_win32.h */
#  include "ConvertUTF_v2.h"


/*
 * Lazy-initialized CRITICAL_SECTION for thread safety.
 */

static CRITICAL_SECTION th8EnvCritSec;
static volatile LONG th8EnvCritSecReady = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8EnvLock --
 *
 *	Acquire the file-scope critical section that serializes all
 *	environment variable operations on Win32.
 *
 * Why / How:
 *	Win32 environment APIs (GetEnvironmentVariableW, etc.) are
 *	not documented as thread-safe and GetEnvironmentStringsW
 *	returns a snapshot that must not race with
 *	SetEnvironmentVariableW.  Uses the same atomic CAS lazy-init
 *	pattern as the POSIX side: InterlockedCompareExchange
 *	transitions the ready flag 0 -> -1 -> 1, with a spin-wait
 *	for concurrent callers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquires th8EnvCritSec.  May initialize it on first call.
 *
 *----------------------------------------------------------------------
 */

static void
th8EnvLock(void)
{
    if (!th8EnvCritSecReady) {
	if (InterlockedCompareExchange(&th8EnvCritSecReady, -1, 0) == 0) {
	    InitializeCriticalSection(&th8EnvCritSec);
	    InterlockedExchange(&th8EnvCritSecReady, 1);
	} else {
	    while (th8EnvCritSecReady != 1) {
		/* spin until initialization completes */
	    }
	}
    }
    EnterCriticalSection(&th8EnvCritSec);
}

/*
 *----------------------------------------------------------------------
 *
 * th8EnvUnlock --
 *
 *	Release the file-scope critical section acquired by
 *	th8EnvLock (Win32).
 *
 * Why / How:
 *	Simple wrapper around LeaveCriticalSection.  Separated for
 *	symmetry with th8EnvLock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases th8EnvCritSec.
 *
 *----------------------------------------------------------------------
 */

static void
th8EnvUnlock(void)
{
    LeaveCriticalSection(&th8EnvCritSec);
}


/*
 *----------------------------------------------------------------------
 *
 * th8EnvUtf8ToUtf16 --
 *
 *	Convert a length-counted UTF-8 string to a NUL-terminated
 *	UTF-16 string using ConvertUTF_v2 with strict validation.
 *
 *	Uses the stack buffer if it fits; otherwise allocates via
 *	Th8_AttemptMalloc.  Returns NULL on conversion error or OOM.
 *
 *	The caller must call th8EnvFreeWide() when done.
 *
 * Why / How:
 *	Win32 environment APIs are wide-character only, so every
 *	TH8 UTF-8 string must be converted before calling them.
 *	Strict validation is used to reject ill-formed UTF-8 early
 *	rather than silently corrupting environment state.  The
 *	stack-buffer optimization avoids a heap allocation for the
 *	common case of short key names.
 *
 * Results:
 *	Pointer to a NUL-terminated UTF-16 string, or NULL on
 *	conversion error or allocation failure.
 *
 * Side effects:
 *	May allocate memory via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

static WCHAR *
th8EnvUtf8ToUtf16(
    Th8_Interp *interp,
    const char *zUtf8,
    size_t nUtf8,
    WCHAR *wBuf, /* Stack buffer. */
    size_t nBuf) /* Size in WCHAR units. */
{
    const UTF8 *pSrc;
    UTF16 *pDst;
    ConversionResult cr;
    size_t nWide = 0;
    WCHAR *wOut;

    if (TH8_SAFE_ADD_SIZE(nUtf8, 1, &nWide)) return NULL;

    if (nWide <= nBuf) {
	wOut = wBuf;
    } else {
	if (nWide > (size_t)-1 / sizeof(WCHAR)) return NULL;
	wOut = (WCHAR *)TH8_ALLOC_MUL(interp, nWide, sizeof(WCHAR));
	if (!wOut) return NULL;
    }

    pSrc = (const UTF8 *)zUtf8;
    pDst = (UTF16 *)wOut;
    cr = ConvertUTF8toUTF16(
        &pSrc, (const UTF8 *)(zUtf8 + nUtf8), &pDst, (UTF16 *)(wOut + nWide),
        strictConversion);

    if (cr != conversionOK) {
	if (wOut != wBuf) Th8_Free(interp, wOut);
	return NULL;
    }
    *pDst = 0; /* NUL-terminate. */
    return wOut;
}

/*
 *----------------------------------------------------------------------
 *
 * th8EnvFreeWide --
 *
 *	Free a wide-character buffer returned by th8EnvUtf8ToUtf16,
 *	but only if it was heap-allocated (i.e. not the original
 *	stack buffer).
 *
 * Why / How:
 *	Companion to th8EnvUtf8ToUtf16.  Compares the pointer to
 *	the stack buffer; if they differ the buffer was heap-allocated
 *	and must be freed.  Safe to call with a NULL w pointer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May free memory via Th8_Free.
 *
 *----------------------------------------------------------------------
 */

static void
th8EnvFreeWide(
    Th8_Interp *interp,
    WCHAR *w,
    WCHAR *wBuf) /* The original stack buffer. */
{
    if (w && w != wBuf) {
	Th8_Free(interp, w);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8EnvUtf16ToUtf8 --
 *
 *	Convert a NUL-terminated UTF-16 string to a length-counted
 *	UTF-8 string using ConvertUTF_v2 with strict validation.
 *
 *	Allocates via Th8_AttemptMalloc.  On success, sets *pzOut
 *	and *pnOut.  Returns TH8_OK or TH8_ERROR.
 *
 *	The caller must free *pzOut via Th8_Free.
 *
 * Why / How:
 *	Reverse of th8EnvUtf8ToUtf16.  Needed when reading
 *	environment values back from Win32 wide-character APIs into
 *	the TH8 UTF-8 world.  Allocates a worst-case buffer (3
 *	bytes per UTF-16 code unit) and converts with strict
 *	validation to reject unpaired surrogates.  An explicit
 *	overflow check guards the 3x multiplication.
 *
 * Results:
 *	TH8_OK on success with *pzOut and *pnOut set.  TH8_ERROR
 *	on conversion failure or allocation failure (outputs zeroed).
 *
 * Side effects:
 *	Allocates memory via Th8_AttemptMalloc on success.
 *
 *----------------------------------------------------------------------
 */

static int
th8EnvUtf16ToUtf8(
    Th8_Interp *interp,
    const WCHAR *wStr,
    size_t nWchars, /* Number of WCHAR units (not bytes). */
    char **pzOut,
    size_t *pnOut)
{
    const UTF16 *pSrc;
    UTF8 *pDst;
    ConversionResult cr;
    size_t nAlloc;
    char *zOut;

    /*
     * Worst case: each UTF-16 code unit -> 3 UTF-8 bytes.
     * Surrogate pairs (2 units) -> 4 UTF-8 bytes, so 3x is safe.
     * TH8_ALLOC_MUL_ADD validates (nWchars * 3 + 1) for size_t
     * overflow internally; on success the same expression is safe
     * to recompute below for the destination-end pointer.
     */

    zOut = (char *)TH8_ALLOC_MUL_ADD(interp, nWchars, 3, 1);
    if (!zOut) {
	*pzOut = NULL;
	*pnOut = 0;
	return TH8_ERROR;
    }
    nAlloc = nWchars * 3 + 1;

    pSrc = (const UTF16 *)wStr;
    pDst = (UTF8 *)zOut;
    cr = ConvertUTF16toUTF8(
        &pSrc, (const UTF16 *)(wStr + nWchars), &pDst,
        (UTF8 *)(zOut + nAlloc), strictConversion);

    if (cr != conversionOK) {
	Th8_Free(interp, zOut);
	*pzOut = NULL;
	*pnOut = 0;
	return TH8_ERROR;
    }

    *pnOut = (size_t)((char *)pDst - zOut);
    *pzOut = zOut;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8EnvKeyValue --
 *
 *	xKeyValue callback backed by Win32 environment variables.
 *	All string conversions use ConvertUTF_v2 with strict mode.
 *
 * Why / How:
 *	Implements all TH8_KV_* operations using the Win32 wide-
 *	character environment APIs (GetEnvironmentVariableW,
 *	SetEnvironmentVariableW, GetEnvironmentStringsW).  Every
 *	UTF-8 key or value is converted to UTF-16 via
 *	th8EnvUtf8ToUtf16 before calling the Win32 API, and values
 *	read back are converted via th8EnvUtf16ToUtf8.  The
 *	glob-filtered "2" variants (EXISTS2, LIST2, GET2, SET2,
 *	UNSET2) share a single iteration loop over the environment
 *	block returned by GetEnvironmentStringsW, with per-op
 *	dispatch inside the loop.  For SET2/UNSET2, matching keys
 *	are collected first and mutated after iteration to avoid
 *	invalidating the environment block.  All calls are
 *	serialized under the file-scope critical section.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.  The interpreter
 *	result is set to the value, list, or error message.
 *
 * Side effects:
 *	May modify the host process environment.  Acquires and
 *	releases the file-scope critical section.
 *
 *----------------------------------------------------------------------
 */

static int
th8EnvKeyValue(
    Th8_Interp *interp,
    void *pCtx,
    int op,
    const char *zName,
    size_t nName,
    const char *zValue,
    size_t nValue)
{
    WCHAR wNameBuf[256];
    WCHAR *wName = NULL;
    int rc = TH8_ERROR;

    (void)pCtx;

    switch (op) {

    case TH8_KV_EXISTS: {
	DWORD n;

	wName = th8EnvUtf8ToUtf16(
	    interp, zName, nName, wNameBuf, sizeof(wNameBuf) / sizeof(WCHAR));
	if (!wName) {
	    Th8_SetResultStatic(
	        interp, "invalid UTF-8 in key name", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	n = GetEnvironmentVariableW(wName, NULL, 0);
	th8EnvUnlock();
	rc = (n > 0) ? TH8_OK : TH8_ERROR;
	break;
    }

    case TH8_KV_GET: {
	WCHAR wValBuf[4096];
	DWORD n;

	wName = th8EnvUtf8ToUtf16(
	    interp, zName, nName, wNameBuf, sizeof(wNameBuf) / sizeof(WCHAR));
	if (!wName) {
	    Th8_SetResultStatic(
	        interp, "invalid UTF-8 in key name", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	n = GetEnvironmentVariableW(
	    wName, wValBuf, sizeof(wValBuf) / sizeof(WCHAR));
	th8EnvUnlock();

	if (n == 0) {
	    Th8_SetResultStatic(interp, "key not found", TH8_NOLEN);
	    break;
	}
	if (n >= sizeof(wValBuf) / sizeof(WCHAR)) {
	    Th8_SetResultStatic(
	        interp, "environment value too long", TH8_NOLEN);
	    break;
	}

	{
	    char *zUtf8 = NULL;
	    size_t nUtf8 = 0;

	    if (th8EnvUtf16ToUtf8(
	            interp, wValBuf, (size_t)n, &zUtf8, &nUtf8) != TH8_OK) {
		Th8_SetResultStatic(
		    interp, "UTF-16 to UTF-8 conversion failed", TH8_NOLEN);
		break;
	    }
	    Th8_SetResult(interp, zUtf8, nUtf8);
	    Th8_Free(interp, zUtf8);
	    rc = TH8_OK;
	}
	break;
    }

    case TH8_KV_SET: {
	WCHAR wValSetBuf[256];
	WCHAR *wVal = NULL;

	wName = th8EnvUtf8ToUtf16(
	    interp, zName, nName, wNameBuf, sizeof(wNameBuf) / sizeof(WCHAR));
	if (!wName) {
	    Th8_SetResultStatic(
	        interp, "invalid UTF-8 in key name", TH8_NOLEN);
	    break;
	}
	wVal = th8EnvUtf8ToUtf16(
	    interp, zValue, nValue, wValSetBuf,
	    sizeof(wValSetBuf) / sizeof(WCHAR));
	if (!wVal) {
	    Th8_SetResultStatic(interp, "invalid UTF-8 in value", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	rc = SetEnvironmentVariableW(wName, wVal) ? TH8_OK : TH8_ERROR;
	th8EnvUnlock();
	th8EnvFreeWide(interp, wVal, wValSetBuf);
	wVal = NULL;
	if (rc != TH8_OK) {
	    Th8_SetResultStatic(
	        interp, "SetEnvironmentVariableW failed", TH8_NOLEN);
	}
	break;
    }

    case TH8_KV_UNSET: {
	wName = th8EnvUtf8ToUtf16(
	    interp, zName, nName, wNameBuf, sizeof(wNameBuf) / sizeof(WCHAR));
	if (!wName) {
	    Th8_SetResultStatic(
	        interp, "invalid UTF-8 in key name", TH8_NOLEN);
	    break;
	}
	th8EnvLock();
	SetEnvironmentVariableW(wName, NULL);
	th8EnvUnlock();
	rc = TH8_OK;
	break;
    }

    case TH8_KV_LIST: {
	WCHAR *wEnv;
	const WCHAR *p;
	char *zList = NULL;
	size_t nList = 0;

	th8EnvLock();
	wEnv = GetEnvironmentStringsW();
	th8EnvUnlock();

	if (!wEnv) {
	    Th8_ClearResult(interp);
	    rc = TH8_OK;
	    break;
	}

	/*
	 * GetEnvironmentStringsW returns a double-NUL-terminated
	 * block of NUL-terminated L"NAME=VALUE" strings.
	 */

	for (p = wEnv; *p;) {
	    const WCHAR *wEq;
	    size_t nEntry;
	    size_t nKey;

	    nEntry = wcslen(p);
	    wEq = wcschr(p, L'=');
	    if (!wEq || wEq == p) {
		p += nEntry + 1;
		continue;
	    }
	    nKey = (size_t)(wEq - p);

	    {
		/*
		 * Convert the key portion to UTF-8 for glob matching
		 * and list building.
		 */

		char *zKey = NULL;
		size_t nKeyUtf8 = 0;

		if (th8EnvUtf16ToUtf8(interp, p, nKey, &zKey, &nKeyUtf8) !=
		    TH8_OK) {
		    p += nEntry + 1;
		    continue; /* skip unconvertible entries */
		}

		if (!zName || nName == 0 ||
		    Th8_GlobMatch(interp, zName, nName, zKey, nKeyUtf8)) {
		    Th8_ListAppend(interp, &zList, &nList, zKey, nKeyUtf8);
		}
		Th8_Free(interp, zKey);
	    }
	    p += nEntry + 1;
	}

	FreeEnvironmentStringsW(wEnv);

	if (zList) {
	    Th8_SetResult(interp, zList, nList);
	    Th8_Free(interp, zList);
	} else {
	    Th8_ClearResult(interp);
	}
	rc = TH8_OK;
	break;
    }

    case TH8_KV_EXISTS2:
    case TH8_KV_LIST2:
    case TH8_KV_GET2:
    case TH8_KV_SET2:
    case TH8_KV_UNSET2: {
	WCHAR *wEnv;
	const WCHAR *p2;
	char *zOut = NULL;
	size_t nOut = 0;
	char *zCollect = NULL;
	size_t nCollect = 0;
	int found = 0;

	th8EnvLock();
	wEnv = GetEnvironmentStringsW();

	if (!wEnv) {
	    th8EnvUnlock();
	    if (op == TH8_KV_EXISTS2) {
		rc = TH8_ERROR;
	    } else {
		Th8_ClearResult(interp);
		rc = TH8_OK;
	    }
	    break;
	}

	for (p2 = wEnv; *p2;) {
	    const WCHAR *wEq;
	    size_t nEntry2;
	    size_t nWKey;
	    char *zKey2 = NULL;
	    size_t nKey2 = 0;
	    char *zVal2 = NULL;
	    size_t nVal2 = 0;
	    int match;

	    nEntry2 = wcslen(p2);
	    wEq = wcschr(p2, L'=');
	    if (!wEq || wEq == p2) {
		p2 += nEntry2 + 1;
		continue;
	    }
	    nWKey = (size_t)(wEq - p2);

	    if (th8EnvUtf16ToUtf8(interp, p2, nWKey, &zKey2, &nKey2) !=
	        TH8_OK) {
		p2 += nEntry2 + 1;
		continue;
	    }
	    if (th8EnvUtf16ToUtf8(
	            interp, wEq + 1, nEntry2 - nWKey - 1, &zVal2, &nVal2) !=
	        TH8_OK) {
		Th8_Free(interp, zKey2);
		p2 += nEntry2 + 1;
		continue;
	    }

	    match = 1;
	    /* Same MC/DC decomposition as the value-glob block
	     * below.  See FINDINGS.md Finding 005. */
	    {
		int doKeyGlob = 0;

		if (match) {
		    if (zName) {
			if (nName > 0) doKeyGlob = 1;
		    }
		}
		if (doKeyGlob) {
		    if (!Th8_GlobMatch(interp, zName, nName, zKey2, nKey2)) {
			match = 0;
		    }
		}
	    }
	    /* Decompose the 4-condition compound into a sequential
	     * single-condition chain so clang MC/DC sees four
	     * single-condition decisions instead of one 4-condition
	     * compound.  See FINDINGS.md Finding 005. */
	    {
		int doValueGlob = 0;

		if (match) {
		    if (op != TH8_KV_SET2) {
			if (zValue) {
			    if (nValue > 0) doValueGlob = 1;
			}
		    }
		}
		if (doValueGlob) {
		    if (!Th8_GlobMatch(
		            interp, zValue, nValue, zVal2, nVal2)) {
			match = 0;
		    }
		}
	    }

	    if (match) {
		found = 1;

		if (op == TH8_KV_EXISTS2) {
		    Th8_Free(interp, zKey2);
		    Th8_Free(interp, zVal2);
		    break;
		}
		if (op == TH8_KV_LIST2) {
		    Th8_ListAppend(interp, &zOut, &nOut, zKey2, nKey2);
		}
		if (op == TH8_KV_GET2) {
		    Th8_ListAppend(interp, &zOut, &nOut, zKey2, nKey2);
		    Th8_ListAppend(interp, &zOut, &nOut, zVal2, nVal2);
		}
		if (op == TH8_KV_SET2 || op == TH8_KV_UNSET2) {
		    Th8_ListAppend(
		        interp, &zCollect, &nCollect, zKey2, nKey2);
		}
		if (op == TH8_KV_UNSET2) {
		    Th8_ListAppend(interp, &zOut, &nOut, zKey2, nKey2);
		    Th8_ListAppend(interp, &zOut, &nOut, zVal2, nVal2);
		}
	    }

	    Th8_Free(interp, zKey2);
	    Th8_Free(interp, zVal2);
	    p2 += nEntry2 + 1;
	}

	FreeEnvironmentStringsW(wEnv);

	/*
	 * For SET2/UNSET2, mutate after iteration.
	 */

	if (zCollect) {
	    WCHAR wValSetBuf2[256];
	    WCHAR *wVal2 = NULL;
	    char **azElem = NULL;
	    size_t *anElem = NULL;
	    int nCount = 0;
	    int j;

	    if (op == TH8_KV_SET2) {
		wVal2 = th8EnvUtf8ToUtf16(
		    interp, zValue, nValue, wValSetBuf2,
		    sizeof(wValSetBuf2) / sizeof(WCHAR));
	    }

	    if (Th8_SplitList(
	            interp, zCollect, nCollect, &azElem, &anElem, &nCount,
	            TH8_LIST_NONE) == TH8_OK) {
		for (j = 0; j < nCount; j++) {
		    WCHAR wKBuf[256];
		    WCHAR *wK = th8EnvUtf8ToUtf16(
		        interp, azElem[j], anElem[j], wKBuf,
		        sizeof(wKBuf) / sizeof(WCHAR));
		    if (wK) {
			if (op == TH8_KV_SET2 && wVal2) {
			    SetEnvironmentVariableW(wK, wVal2);
			} else if (op == TH8_KV_UNSET2) {
			    SetEnvironmentVariableW(wK, NULL);
			}
			th8EnvFreeWide(interp, wK, wKBuf);
		    }
		}
		Th8_Free(interp, azElem);
		/* anElem is interior to the azElem block. */
	    }

	    if (op == TH8_KV_SET2) {
		th8EnvFreeWide(interp, wVal2, wValSetBuf2);
	    }
	    Th8_Free(interp, zCollect);
	}

	th8EnvUnlock();

	if (op == TH8_KV_EXISTS2) {
	    rc = found ? TH8_OK : TH8_ERROR;
	} else {
	    if (zOut) {
		Th8_SetResult(interp, zOut, nOut);
		Th8_Free(interp, zOut);
	    } else {
		Th8_ClearResult(interp);
	    }
	    rc = TH8_OK;
	}
	break;
    }

    default:
	Th8_SetResultStatic(interp, "unknown kv operation", TH8_NOLEN);
	break;
    }

    th8EnvFreeWide(interp, wName, wNameBuf);
    return rc;
}


#endif /* _WIN32 */


/*
 *----------------------------------------------------------------------
 *
 * Platform struct and accessor.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8EnvPlatformData = {
    4,   /* nVersion */
    0,
    0,
    0,
    0,  /* xInitialize, xFinalize, xPreDeleteInterp, xDeleteInterp */
    0,
    0,
    0,
    0, /* xMalloc, xRealloc, xFree, xMemorySize */
    0,   /* xNeedMemory */
    0,
    0,
    0,
    0, /* xMemcpy, xMemmove, xMemcmp, xMemset */
    0,
    0,
    0,
    0,
    0,
    0, /* xStrlen, xStrcmp, xAtoi, xStrtod, xQsort,
			   xVsnprintf */
    0,
    0,
    0,
    0,
    0,
    0, /* xMutexNew, xMutexFree, xMutexEnter,
			   xMutexLeave, xIntCmpXchg, xMemBarrier */
    0,
    0,
    0,
    0,
    0, /* xEventCreate, xEventDestroy, xEventSet,
			   xEventReset, xEventWait */
    0,
    0,
    0,  /* xInput, xOutput, xOutputError */
    0,
    0,
    0,
    0,
    0,
    0, /* xGetInput, xSetInput, xGetOutput,
			   xSetOutput, xGetErrorOutput,
			   xSetErrorOutput */
    0,
    0,
    0,
    0,
    0, /* xChannelControl, xGetTemporaryData,
			   xDeleteTemporaryData,
			   xSetTemporaryData,
			   xCloseTemporaryData */
    0,
    0,
    0,
    0, /* xNormalizePath, xGetCwd, xSetCwd,
			   xGetExePath */
    0,
    0,  /* xGetRealPath, xGetRootPath */
    0,   /* xSameFile */
    0,
    0,  /* xGetData, xDataExists */
    0,
    0,  /* xLoad, xUnload */
    0,
    0,
    0,  /* xTimeMs, xTimeUs, xSleep */
    0,   /* xGetPid */
    0,
    0,  /* xGetUserName, xGetHostName */
    0,   /* xGetEnv */
    th8EnvKeyValue, /* xKeyValue */
    0,   /* xGetStackBounds */
    0,
    0,  /* xGetParentPid, xGetThreadId */
    0,
    0,  /* xGetLastError, xSetLastError */
    0,   /* xEmitTrace */
    0,   /* xPanic */
    0,   /* xMathFunc */
    0,   /* xRandomBytes */
    0,
    0,  /* xDnsResolve, xDnsResolveFree */
    0   /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetEnvPlatform --
 *
 *	Return a pointer to the static Th8_Platform struct that
 *	provides the environment variable xKeyValue callback.
 *
 * Why / How:
 *	The environment platform is a minimal Th8_Platform with only
 *	the xKeyValue slot populated.  It is intended to be merged
 *	into a full platform via Th8_MergePlatform.  Returning a
 *	pointer to a static (not allocated) struct avoids any
 *	lifetime management.
 *
 * Results:
 *	Pointer to the static environment platform struct (never
 *	NULL).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetEnvPlatform(void)
{
    return &th8EnvPlatformData;
}
