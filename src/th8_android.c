/*
 * th8_android.c -- Android (Bionic libc) platform implementation for TH8.
 *
 * Android is Linux+Bionic.  Most of the POSIX/UNIX surface that TH8
 * relies on is identical to glibc; the deltas are:
 *
 *   - Logging facility: stderr is wired to /dev/null in most Android
 *     processes (no terminal), and syslog goes nowhere; logcat is the
 *     correct destination.
 *
 *   - Memory introspection: Bionic does not provide POSIX/macOS
 *     malloc_size; the equivalent is malloc_usable_size() in <malloc.h>.
 *     Without this override, xMemorySize is NULL on Bionic and TH8's
 *     allocation accounting falls back to its caller-supplied-size
 *     bookkeeping (still correct, just less precise around realloc).
 *
 *   - Entropy: getrandom(2) is available on API 28+ (the libc symbol
 *     is exported only there); /dev/urandom is the universal fallback
 *     and is what the POSIX layer already does.  We override only
 *     when API 28+ to avoid the per-call open overhead.
 *
 * This file does NOT layer on top of th8_macos.c -- the Apple malloc
 * zone APIs do not exist on Bionic.  The merge order is:
 *
 *     th8_android.c -> th8_posix.c -> th8_libc.c
 *
 * Usage:
 *     Th8_Platform plat = *Th8_GetAndroidPlatform();
 *     Th8_MergePlatform(&plat, Th8_GetPosixPlatform());
 *     Th8_MergePlatform(&plat, Th8_GetLibcPlatform());
 *     interp = Th8_CreateInterp(&plat);
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_meta_posix.h"
#include "th8.h"

#if defined(TH8_PLATFORM_ANDROID)

#  include <android/log.h>
#  include <malloc.h>             /* malloc_usable_size */
#  include <stdlib.h>
#  include <string.h>
#  include <unistd.h>

#  if __ANDROID_API__ >= 28
#    include <sys/random.h>        /* getrandom */
#  endif


/*
 *----------------------------------------------------------------------
 *
 * Android log tag.
 *
 *	Single tag for all TH8 emissions; logcat already provides
 *	timestamp, pid, tid, and level-of-severity columns, so we
 *	don't need to embed them in the message.
 *
 *----------------------------------------------------------------------
 */

#  ifndef TH8_ANDROID_LOG_TAG
#    define TH8_ANDROID_LOG_TAG "th8"
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8AndroidEmitTrace --
 *
 *	Implements Th8_Platform.xEmitTrace for Android by routing the
 *	NUL-terminated message to logcat at ANDROID_LOG_DEBUG severity.
 *
 * Why / How:
 *	Android processes typically have stderr redirected to
 *	/dev/null; the unified logging path is logcat (Bionic's
 *	__android_log_print).  DEBUG severity is filtered out by
 *	default in production builds, which is the right behaviour
 *	for trace-level messages.
 *
 *----------------------------------------------------------------------
 */

static void
th8AndroidEmitTrace(Th8_Interp *interp, void *pCtx, const char *zMsg)
{
    (void)interp;
    (void)pCtx;
    if (!zMsg || !zMsg[0]) return;
    __android_log_print(ANDROID_LOG_DEBUG, TH8_ANDROID_LOG_TAG, "%s", zMsg);
}


/*
 *----------------------------------------------------------------------
 *
 * th8AndroidPanic --
 *
 *	Implements Th8_Platform.xPanic for Android.  Routes the message
 *	to logcat at ANDROID_LOG_FATAL severity.
 *
 * Why / How:
 *	ANDROID_LOG_FATAL is the correct severity for an unrecoverable
 *	error -- it is captured by the Android tombstone system and
 *	included in crash reports gathered by tools like Firebase
 *	Crashlytics.  We do NOT call abort() here; the caller in
 *	th8_plat.c is expected to terminate after this returns,
 *	uniformly across platforms.
 *
 *	xPanic supplies an explicit length (nMsg), so the input is not
 *	necessarily NUL-terminated.  Copy into a stack buffer with
 *	truncation marker if the message is over 1023 bytes -- logcat
 *	itself imposes a per-line cap around 4 KiB but our buffer cap
 *	keeps stack usage bounded.
 *
 *----------------------------------------------------------------------
 */

static void
th8AndroidPanic(Th8_Interp *interp, void *pCtx, const char *zMsg, size_t nMsg)
{
    char buf[1024];
    size_t n;
    (void)interp;
    (void)pCtx;
    if (!zMsg) {
	__android_log_print(
	    ANDROID_LOG_FATAL, TH8_ANDROID_LOG_TAG, "panic (no message)");
	return;
    }
    n = nMsg;
    if (n >= sizeof(buf) - 1) {
	n = sizeof(buf) - 4;
	memcpy(buf, zMsg, n);
	buf[n] = '.';
	buf[n + 1] = '.';
	buf[n + 2] = '.';
	buf[n + 3] = '\0';
    } else {
	memcpy(buf, zMsg, n);
	buf[n] = '\0';
    }
    __android_log_print(
        ANDROID_LOG_FATAL, TH8_ANDROID_LOG_TAG, "panic: %s", buf);
}


/*
 *----------------------------------------------------------------------
 *
 * th8AndroidMemorySize --
 *
 *	Implements Th8_Platform.xMemorySize for Android via Bionic's
 *	malloc_usable_size().
 *
 * Why / How:
 *	macOS provides malloc_size; glibc provides malloc_usable_size;
 *	Bionic exposes malloc_usable_size via <malloc.h>.  Both return
 *	the actual usable size (typically rounded up by the allocator's
 *	bin-size policy), which is what xMemorySize expects.  Pure-
 *	POSIX has no portable equivalent, so the POSIX layer leaves
 *	this slot NULL.  Filling it on Android gives TH8's allocation
 *	accounting the same precision it has on macOS.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8AndroidMemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    (void)interp;
    (void)pCtx;
    if (!p) return 0;
    return malloc_usable_size(p);
}


/*
 *----------------------------------------------------------------------
 *
 * th8AndroidRandomBytes --
 *
 *	Implements Th8_Platform.xRandomBytes for Android.  On API 28+
 *	uses the getrandom(2) syscall directly; on older API levels we
 *	leave xRandomBytes NULL in the platform table so the POSIX
 *	layer's /dev/urandom path takes over via merge.
 *
 * Why / How:
 *	getrandom(2) avoids opening /dev/urandom on every call, blocks
 *	correctly when the entropy pool is not yet seeded (during very
 *	early boot, never during normal app runtime), and is the
 *	recommended modern API.  It returned EINTR-restartable behavior
 *	on Linux 3.17+; on Android, the Bionic libc wrapper has been
 *	publicly exported since API 28.
 *
 *	When a partial read happens (which is rare but spec-permitted
 *	for buffers > 256 bytes), the loop continues until the buffer
 *	is filled or an error other than EINTR occurs.
 *
 *----------------------------------------------------------------------
 */

#  if __ANDROID_API__ >= 28
static int
th8AndroidRandomBytes(
    Th8_Interp *interp,
    void *pCtx,
    void *pBuf,
    size_t nByte)
{
    unsigned char *p = (unsigned char *)pBuf;
    (void)interp;
    (void)pCtx;
    if (nByte == 0) return TH8_OK;
    if (!pBuf) return TH8_ERROR;
    while (nByte > 0) {
	ssize_t got = getrandom(p, nByte, 0);
	if (got < 0) {
	    if (errno == EINTR) continue;
	    return TH8_ERROR;
	}
	if (got == 0) return TH8_ERROR;
	p += (size_t)got;
	nByte -= (size_t)got;
    }
    return TH8_OK;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * Platform table -- only Android-specific overrides are filled.
 * Everything else stays NULL and will be supplied by merging with
 * Th8_GetPosixPlatform and Th8_GetLibcPlatform.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8AndroidPlatformData = {
    4,    /* nVersion */
    0,    /* xInitialize */
    0,    /* xFinalize */

    /* Interpreter deletion notifications */
    0,    /* xPreDeleteInterp */
    0,    /* xDeleteInterp */

    /* Memory -- POSIX merge provides malloc/free; we override
     * only xMemorySize because Bionic has malloc_usable_size while
     * pure-POSIX has no portable size-of-allocation primitive. */
    0,    /* xMalloc */
    0,    /* xRealloc */
    0,    /* xFree */
    th8AndroidMemorySize, /* xMemorySize -- Bionic delta */
    0,    /* xNeedMemory */

    /* C runtime mem ops (libc merge) */
    0,    /* xMemcpy */
    0,    /* xMemmove */
    0,    /* xMemset */
    0,    /* xMemcmp */

    /* C runtime string/utility (libc merge) */
    0,    /* xStrlen */
    0,    /* xStrcmp */
    0,    /* xStrchr */
    0,    /* xAtoi */
    0,    /* xQsort */
    0,    /* xVsnprintf */

    /* Mutex callbacks (POSIX merge) */
    0,    /* xMutexInit */
    0,    /* xMutexFinal */
    0,    /* xMutexEnter */
    0,    /* xMutexLeave */
    0,    /* xIntCmpXchg */
    0,    /* xMemBarrier */

    /* Manual-reset event handle (POSIX merge) */
    0,    /* xEventCreate */
    0,    /* xEventDestroy */
    0,    /* xEventSet */
    0,    /* xEventReset */
    0,    /* xEventWait */

    /* I/O (POSIX merge) */
    0,    /* xInput */
    0,    /* xOutput */
    0,    /* xOutputError */
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

    /* File system (POSIX merge -- works on Bionic) */
    0,    /* xNormalizePath */
    0,    /* xGetCwd */
    0,    /* xSetCwd */
    0,    /* xGetExePath */
    0,    /* xGetRealPath */
    0,    /* xGetRootPath */
    0,    /* xSameFile */

    /* Data retrieval (POSIX merge) */
    0,    /* xGetData */
    0,    /* xDataExists */

    /* Binary loading (POSIX merge -- dlopen works on Bionic; app
     * library paths are app-private (/data/app/.../<pkg>/lib/<arch>/)
     * but the API surface is unchanged) */
    0,    /* xLoad */
    0,    /* xUnload */

    /* Time (POSIX merge) */
    0,    /* xTimeMs */
    0,    /* xTimeUs */
    0,    /* xSleep */

    /* Process / host (POSIX merge) */
    0,    /* xGetPid */
    0,    /* xGetUserName */
    0,    /* xGetHostName */
    0,    /* xGetEnv */
    0,    /* xKeyValue */
    0,    /* xGetStackBounds */
    0,    /* xGetParentPid */
    0,    /* xGetThreadId */

    /* Error code */
    0,    /* xGetLastError */
    0,    /* xSetLastError */

    /* Trace + Panic -- ANDROID OVERRIDES (route to logcat) */
    th8AndroidEmitTrace, /* xEmitTrace */
    th8AndroidPanic,  /* xPanic */

    /* Math (libc merge) */
    0,    /* xMathFunc */

    /* Random bytes -- override only on API 28+ where getrandom(2)
     * is publicly exported by Bionic.  Older API levels fall back
     * to POSIX's /dev/urandom path via merge. */
#  if __ANDROID_API__ >= 28
    th8AndroidRandomBytes, /* xRandomBytes */
#  else
    0,    /* xRandomBytes (POSIX merge supplies) */
#  endif

    /* DNS (libunbound provided by POSIX merge when enabled) */
    0, 0,   /* xDnsResolve, xDnsResolveFree */

    /* Host context */
    0    /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetAndroidPlatform --
 *
 *	Return a pointer to the Android platform implementation.
 *	Merge with Th8_GetPosixPlatform() and Th8_GetLibcPlatform()
 *	before passing to Th8_CreateInterp.  Do NOT merge with
 *	Th8_GetMacOSPlatform() -- the Apple malloc zone APIs are not
 *	available on Bionic.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetAndroidPlatform(void)
{
    return &th8AndroidPlatformData;
}

#endif /* TH8_PLATFORM_ANDROID */
