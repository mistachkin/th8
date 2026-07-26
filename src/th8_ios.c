/*
 * th8_ios.c -- iOS-specific platform implementation for TH8.
 *
 * iOS is a derivative of macOS that shares essentially all UNIX/POSIX
 * surface and the Apple malloc-zone APIs.  This file therefore
 * provides ONLY the iOS-specific deltas; everything else is inherited
 * via Th8_MergePlatform from:
 *
 *     th8_macos.c   (Apple private malloc zone, secure memset)
 *     th8_posix.c   (file I/O, time, threads, dlopen, stack bounds)
 *     th8_libc.c    (CRT bridge: strlen/memcpy/qsort/etc.)
 *
 * Usage:
 *     Th8_Platform plat = *Th8_GetIosPlatform();
 *     Th8_MergePlatform(&plat, Th8_GetMacOSPlatform());
 *     Th8_MergePlatform(&plat, Th8_GetPosixPlatform());
 *     Th8_MergePlatform(&plat, Th8_GetLibcPlatform());
 *     interp = Th8_CreateInterp(&plat);
 *
 * The iOS platform overrides:
 *   - xPanic       -> os_log_with_type(..., OS_LOG_TYPE_FAULT, ...)
 *   - xEmitTrace   -> os_log_with_type(..., OS_LOG_TYPE_DEBUG, ...)
 *   - xRandomBytes -> arc4random_buf
 *
 * Why these three:
 *   - xPanic / xEmitTrace: iOS apps typically have stderr redirected
 *     to /dev/null or to the unified logging system anyway.  Routing
 *     through os_log makes diagnostics show up in Console.app and
 *     in crash reports without further plumbing, with proper
 *     subsystem/category tagging that aids triage on devices.
 *   - xRandomBytes: arc4random_buf is unconditionally available on
 *     iOS, draws from the kernel's CSPRNG, and avoids any /dev/urandom
 *     fd open inside the iOS sandbox where syscall surface is more
 *     restricted than on macOS.
 *
 * Everything else (memory zone, file I/O, threading, dynamic loading
 * of in-bundle dylibs, time, stack bounds, math) inherits unchanged.
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

#if defined(TH8_PLATFORM_IOS)

#  include <os/log.h>
#  include <stdlib.h>             /* arc4random_buf */
#  include <string.h>             /* strlen */


/*
 * Lazily-initialised `os_log` handle for the TH8 iOS subsystem.
 * Process-global; the (subsystem, category) pair
 * `"com.mistachkin.th8"` / `"core"` lets Console.app filter TH8
 * messages.  Read and written exclusively through
 * `th8IosGetLog` below.
 */
static os_log_t th8IosLogHandle = NULL;

/*
 *----------------------------------------------------------------------
 *
 * th8IosGetLog --
 *
 *	Return the process-global `os_log_t` handle, creating
 *	it on the first call.  Lazy creation avoids the
 *	`os_log_create` cost in iOS apps that never trigger a
 *	trace.
 *
 *	Apple's documentation states that `os_log_t` handles
 *	are intentionally leaked process-wide; there is no
 *	matching destroy step, which is why this file owns no
 *	teardown function.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	A non-NULL `os_log_t` for the TH8 subsystem.
 *
 * Side effects:
 *	May mutate `th8IosLogHandle` on the first call.
 *
 *----------------------------------------------------------------------
 */
static os_log_t
th8IosGetLog(void)
{
    if (!th8IosLogHandle) {
	th8IosLogHandle = os_log_create("com.mistachkin.th8", "core");
    }
    return th8IosLogHandle;
}


/*
 *----------------------------------------------------------------------
 *
 * th8IosEmitTrace --
 *
 *	Implements Th8_Platform.xEmitTrace for iOS by routing the
 *	NUL-terminated message to the unified logging system at the
 *	OS_LOG_TYPE_DEBUG level.  Empty / NULL messages are suppressed.
 *
 * Why / How:
 *	iOS apps very commonly run with stderr redirected to /dev/null
 *	(no terminal exists), so the POSIX layer's syslog/stderr trace
 *	path produces silent output.  Routing through os_log_with_type
 *	makes traces visible in Console.app, in os_log_t streams, and
 *	in `log show --predicate 'subsystem == "com.mistachkin.th8"'`.
 *
 *	Format string is "%{public}s" so the message text isn't redacted
 *	by Apple's privacy-preserving logging defaults.
 *
 *----------------------------------------------------------------------
 */

static void
th8IosEmitTrace(Th8_Interp *interp, void *pCtx, const char *zMsg)
{
    (void)interp;
    (void)pCtx;
    if (!zMsg || !zMsg[0]) return;
    os_log_with_type(th8IosGetLog(), OS_LOG_TYPE_DEBUG, "%{public}s", zMsg);
}


/*
 *----------------------------------------------------------------------
 *
 * th8IosPanic --
 *
 *	Implements Th8_Platform.xPanic for iOS.  Emits the message at
 *	OS_LOG_TYPE_FAULT severity (which is captured into iOS crash
 *	reports automatically) before returning.
 *
 * Why / How:
 *	The xPanic contract is "report and stop"; the caller is
 *	expected to abort or otherwise terminate execution after this
 *	function returns.  We do NOT call abort() here -- the platform
 *	plat-merge layer in th8_plat.c handles the actual abort path
 *	uniformly across platforms.
 *
 *	OS_LOG_TYPE_FAULT is the correct severity for a panic: it is
 *	persisted to disk by the system and shows up in the device's
 *	diagnostic data.  The trailing 8-byte length is not part of
 *	NUL-terminated input (xPanic supplies an explicit nMsg), so
 *	we copy into a small stack buffer to NUL-terminate before
 *	handing to the os_log printf-formatter.
 *
 *	Messages longer than 1023 bytes are truncated; the truncation
 *	ASCII marker "..." is appended in that case so callers can
 *	see when they've exceeded the cap.
 *
 *----------------------------------------------------------------------
 */

static void
th8IosPanic(Th8_Interp *interp, void *pCtx, const char *zMsg, size_t nMsg)
{
    char buf[1024];
    size_t n;
    (void)interp;
    (void)pCtx;
    if (!zMsg) {
	os_log_with_type(
	    th8IosGetLog(), OS_LOG_TYPE_FAULT, "th8: panic (no message)");
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
    os_log_with_type(
        th8IosGetLog(), OS_LOG_TYPE_FAULT, "th8: panic: %{public}s", buf);
}


/*
 *----------------------------------------------------------------------
 *
 * th8IosRandomBytes --
 *
 *	Implements Th8_Platform.xRandomBytes for iOS via arc4random_buf.
 *
 * Why / How:
 *	arc4random_buf has been part of the iOS public API since iOS 4
 *	and is backed by the kernel's CSPRNG (chacha20-based on modern
 *	versions).  It is unconditionally available, requires no fd
 *	open, and works inside the iOS sandbox where /dev/urandom may
 *	be reachable but is not the recommended path.  Cannot fail in
 *	a way that matters at this scale, so we always return TH8_OK.
 *
 *----------------------------------------------------------------------
 */

static int
th8IosRandomBytes(Th8_Interp *interp, void *pCtx, void *pBuf, size_t nByte)
{
    (void)interp;
    (void)pCtx;
    if (nByte == 0) return TH8_OK;
    if (!pBuf) return TH8_ERROR;
    arc4random_buf(pBuf, nByte);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Platform table -- only the iOS-specific overrides are filled.
 * Everything else stays NULL and will be supplied by merging with
 * Th8_GetMacOSPlatform, Th8_GetPosixPlatform, and Th8_GetLibcPlatform.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8IosPlatformData = {
    5,    /* nVersion */
    0,    /* xInitialize  (macOS provides) */
    0,    /* xFinalize    (macOS provides) */

    /* Interpreter deletion notifications */
    0,    /* xPreDeleteInterp */
    0,    /* xDeleteInterp */

    /* Memory -- macOS provides via private malloc zone */
    0,    /* xMalloc */
    0,    /* xRealloc */
    0,    /* xFree */
    0,    /* xMemorySize */
    0,    /* xNeedMemory */

    /* C runtime mem ops */
    0,    /* xMemcpy */
    0,    /* xMemmove */
    0,    /* xMemset      (macOS provides bzero variant) */
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

    /* File system (POSIX merge) */
    0,    /* xNormalizePath */
    0,    /* xGetCwd */
    0,    /* xSetCwd */

    /* Executable path (POSIX merge -- _NSGetExecutablePath works
     * on iOS and returns the .app bundle's binary, which is the
     * canonical executable identity for an iOS process) */
    0,    /* xGetExePath */
    0,    /* xGetRealPath */
    0,    /* xGetRootPath */
    0,    /* xSameFile */

    /* Data retrieval (POSIX merge) */
    0,    /* xGetData */
    0,    /* xDataExists */

    /* Binary loading (POSIX merge -- dlopen works for in-bundle
     * dylibs on iOS; App Store sandbox restrictions bound which
     * paths are reachable but the API is unchanged) */
    0,    /* xLoad */
    0,    /* xUnload */

    /* Time (POSIX merge) */
    0,    /* xTimeMs */
    0,    /* xTimeUs */
    0,    /* xSleep */

    /* Process / host (POSIX merge -- getpid works; getlogin
     * returns "mobile" on iOS but is rarely useful; getenv
     * works but env is normally empty) */
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

    /* Trace + Panic -- IOS OVERRIDES (route to os_log) */
    th8IosEmitTrace,  /* xEmitTrace */
    th8IosPanic,  /* xPanic */

    /* Math (libc merge) */
    0,    /* xMathFunc */

    /* Random bytes -- IOS OVERRIDE (arc4random_buf, no /dev/urandom) */
    th8IosRandomBytes,  /* xRandomBytes */

    /* DNS (libunbound not available on iOS) */
    0, 0,   /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics (nVersion 5) -- the th8_unwind (compiler-runtime) layer supplies xStackBackTrace. */
    0, /* xStackBackTrace */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetIosPlatform --
 *
 *	Return a pointer to the iOS platform implementation.  Merge
 *	with Th8_GetMacOSPlatform(), Th8_GetPosixPlatform(), and
 *	Th8_GetLibcPlatform() before passing to Th8_CreateInterp.
 *
 *	The macOS layer must be merged before the POSIX layer so the
 *	private malloc zone wins over POSIX's NULL slots; both must
 *	come after the iOS layer so iOS's xPanic / xEmitTrace /
 *	xRandomBytes win over the lower layers.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetIosPlatform(void)
{
    return &th8IosPlatformData;
}

#endif /* TH8_PLATFORM_IOS */
