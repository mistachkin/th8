/*
 * th8_win32.c -- Win32 platform implementation for TH8.
 *
 * Provides the OS-specific Th8_Platform callbacks for
 * Win32/Win64 systems using the native Windows API.
 *
 * This file provides ONLY the Windows-specific callbacks:
 *   - xGetData / xDataExists (file I/O via CreateFileA/GetFileAttributesA)
 *   - xInput (ReadConsoleW / ReadFile from STD_INPUT_HANDLE)
 *   - xOutput / xOutputError (WriteConsoleW / WriteFile to STD handles)
 *   - xRandomBytes (RtlGenRandom / SystemFunction036)
 *   - xTimeMs (GetSystemTimeAsFileTime)
 *   - xGetPid (GetCurrentProcessId)
 *   - xGetParentPid (returns 0, not easily available)
 *   - xGetThreadId (GetCurrentThreadId)
 *   - xGetStackBounds (VirtualQuery walk of stack reservation)
 *   - xPanic (WriteFile to stderr + ExitProcess)
 *
 * Memory (xMalloc, xRealloc, xFree) and CRT mem-operations
 * (xMemcpy, xMemmove, xMemset, xMemcmp) are left as NULL
 * and provided by th8_libc.c via Th8_MergePlatform().
 *
 * NOTE: xGetData uses the native Win32 API (CreateFileA with
 * narrow paths).  For Unicode path support, ConvertUTF_v2
 * should be used to convert UTF-8 to UTF-16 and call
 * CreateFileW directly.  This is a future enhancement.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_meta_msvc.h"
#include "th8_meta_win32.h"
#include "th8.h"
#include "th8_int.h"

#if defined(TH8_PLATFORM_WIN32)

#  include "th8_mem.h"
#  include "ConvertUTF_v2.h"

#  if defined(TH8_ENABLE_UNBOUND)
#    include "th8_unbound.h"
#  endif

#  if !defined(TH8_MODULE_NAME)
#    if defined(TH8_DEBUG)
#      define TH8_MODULE_NAME "th8d.dll"
#    else
#      define TH8_MODULE_NAME "th8.dll"
#    endif
#  endif

/*
 * Authenticode verification headers for DLL signature checking.
 * Required libraries: wintrust.lib, crypt32.lib.
 */

/* <wintrust.h>, <softpub.h>, <mscat.h> included via th8_meta_win32.h */

/*
 * File-scope base path state.  Shared between th8Win32GetBasePath
 * and Th8_SetBasePath so the latter can override the automatic
 * GetModuleHandleA-based detection.
 */

static char zBasePath[MAX_PATH];
static int bInitialized = 0;

extern Th8_Platform th8GlobalPlatform;

/*
 *----------------------------------------------------------------------
 *
 * th8Win32ResolvePath --
 *
 *	Resolve a path to its canonical absolute form, expanding 8.3
 *	short names and (on Vista+) resolving symlinks and junctions.
 *
 *	On _WIN32_WINNT >= 0x0600, uses GetFinalPathNameByHandleA
 *	which resolves all reparse points, junctions, symlinks, and
 *	short names -- the Win32 equivalent of POSIX realpath(3).
 *
 *	On older SDKs, falls back to GetFullPathNameA + GetLongPathNameA
 *	which expands 8.3 short names but does NOT resolve symlinks.
 *
 *	zPath must be a NUL-terminated string.  zOut must point to a
 *	buffer of at least MAX_PATH bytes.  Returns non-zero on success,
 *	zero on failure.
 *
 * Why / How:
 *	Path canonicalization is essential for security: the base-path
 *	sandbox check in th8Win32IsUnderBase relies on comparing fully
 *	resolved paths.  Without resolving 8.3 aliases, symlinks, and
 *	junctions, an attacker could use alternate names to escape the
 *	sandbox.  On Vista+ the function opens a handle with zero access
 *	rights (no read/write needed) and FILE_FLAG_BACKUP_SEMANTICS
 *	(required for directories), then queries the final path.  The
 *	"\\?\" prefix returned by GetFinalPathNameByHandleA is stripped
 *	so the result matches the format used by the rest of the platform
 *	layer.  On pre-Vista systems the fallback resolves "." and ".."
 *	via GetFullPathNameA, then expands 8.3 names via
 *	GetLongPathNameA.
 *
 * Results:
 *	TH8_OK on success with the resolved path in zOut;
 *	TH8_ERROR on failure.
 *
 * Side effects:
 *	On Vista+, briefly opens a file handle (immediately closed).
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32ResolvePath(
    const char *zPath,  /* NUL-terminated path to resolve. */
    char *zOut,   /* Output buffer (>= MAX_PATH). */
    DWORD nOut)   /* Size of zOut in bytes. */
{
#  if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600)
    /*
     * Vista+ path: open a handle and use GetFinalPathNameByHandleA
     * to resolve everything (symlinks, junctions, 8.3, "..", etc).
     */

    HANDLE h;
    DWORD n;

    h = CreateFileA(
        zPath, 0,  /* no access needed */
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,  /* required for dirs */
        NULL);

    if (h == INVALID_HANDLE_VALUE) {
	/*
	 * Path doesn't exist.  Fall through to the non-handle
	 * path for best-effort resolution.
	 */

	goto fallback;
    }

    n = GetFinalPathNameByHandleA(
        h, zOut, nOut, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(h);

    if (n > 0 && n < nOut) {
	/*
	 * GetFinalPathNameByHandleA returns a "\\?\" prefixed
	 * path.  Strip the prefix if present.
	 */

	if (n >= 4 && zOut[0] == '\\' && zOut[1] == '\\' && zOut[2] == '?' &&
	    zOut[3] == '\\') {
	    memmove(zOut, zOut + 4, n - 4 + 1);
	}
	return TH8_OK;
    }

fallback:
#  endif /* _WIN32_WINNT >= 0x0600 */

    /*
     * Pre-Vista path (or Vista+ fallback for non-existent paths):
     * GetFullPathNameA resolves "." and ".." components.
     * GetLongPathNameA expands 8.3 short names.
     * Neither resolves symlinks or junctions.
     */

    {
	char zFull[MAX_PATH] = {0};
	DWORD nFull;
	DWORD nLong;

	nFull = GetFullPathNameA(zPath, sizeof(zFull), zFull, NULL);
	if (nFull == 0 || nFull >= sizeof(zFull)) {
	    TH8_TRACE_ERR(NULL, "GetFullPathNameA failed");
	    return TH8_ERROR;
	}

	nLong = GetLongPathNameA(zFull, zOut, nOut);
	if (nLong > 0 && nLong < nOut) {
	    return TH8_OK;
	}

	if (nFull < nOut) {
	    memcpy(zOut, zFull, nFull + 1);
	    return TH8_OK;
	}
    }

    TH8_TRACE_ERR(NULL, "GetLongPathNameA path too long");
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Memory subsystem -- Win32 HeapAlloc.
 *
 *	Uses a private heap created at first use via HeapCreate.
 *	This reduces fragmentation compared to the CRT allocator
 *	and provides HeapSize for accurate memory tracking.
 *
 *	The heap is serialized (no HEAP_NO_SERIALIZE) so it is
 *	safe for single-threaded use per interpreter.
 *
 *	Based on the SQLite Win32 memory allocator pattern.
 *
 *----------------------------------------------------------------------
 */

#  define TH8_WIN32_HEAP_FLAGS (0)

static HANDLE th8Win32Heap = NULL;


/*
 *----------------------------------------------------------------------
 *
 * Platform-level critical section for protecting static state.
 *
 *	This critical section guards lazy initialization of static
 *	data (e.g. th8Win32Heap, zBasePath) that is shared across
 *	threads.  It is separate from the per-interpreter mutexes
 *	exposed via the xMutexInit/xMutexFinal/xMutexEnter/
 *	xMutexLeave callbacks.
 *
 *----------------------------------------------------------------------
 */

static CRITICAL_SECTION th8Win32CritSec;
static volatile int th8Win32CritSecReady = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8Win32Initialize --
 *
 *	Platform xInitialize callback.  Lazily initializes the
 *	process-global CRITICAL_SECTION used to protect shared
 *	static state (the private heap handle, the base path buffer,
 *	and the loaded-library list).
 *
 * Why / How:
 *	Multiple threads may race into Th8_Initialize concurrently,
 *	so the CRITICAL_SECTION must be created exactly once.  An
 *	InterlockedCompareExchange atomic CAS on th8Win32CritSecReady
 *	ensures only the winning thread calls InitializeCriticalSection;
 *	all other threads see the flag already set and skip.  This
 *	avoids the need for a static initializer or DllMain, keeping
 *	the platform layer self-contained.
 *
 * Results:
 *	Always returns TH8_OK.
 *
 * Side effects:
 *	Creates the process-global CRITICAL_SECTION on first call.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32Initialize(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    /*
     * Use InterlockedCompareExchange to ensure only one thread
     * initializes the CRITICAL_SECTION, even if multiple threads
     * race into this function concurrently.
     */

    if (InterlockedCompareExchange(
            (volatile LONG *)&th8Win32CritSecReady, 1, 0) == 0) {
	InitializeCriticalSection(&th8Win32CritSec);
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32Finalize --
 *
 *	Platform xFinalize callback.  Tears down the process-global
 *	CRITICAL_SECTION created by th8Win32Initialize.
 *
 * Why / How:
 *	Uses InterlockedCompareExchange to atomically transition
 *	th8Win32CritSecReady from 1 back to 0.  Only the thread
 *	that wins the CAS calls DeleteCriticalSection, preventing
 *	double-delete if multiple threads race into finalization.
 *	After this call, the platform layer is no longer thread-safe
 *	and must be re-initialized before reuse.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes the process-global CRITICAL_SECTION.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32Finalize(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    if (InterlockedCompareExchange(
            (volatile LONG *)&th8Win32CritSecReady, 0, 1) == 1) {
	DeleteCriticalSection(&th8Win32CritSec);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32Lock --
 *
 *	Acquire the process-global CRITICAL_SECTION that protects
 *	shared static state (th8Win32Heap, zBasePath, library list).
 *
 * Why / How:
 *	Guards against concurrent access to file-scope mutable data.
 *	The ready flag is checked first: if th8Win32Initialize has
 *	not yet been called (or th8Win32Finalize has already run),
 *	the lock is silently skipped, which is safe because no
 *	concurrent access is possible before initialization or
 *	after finalization.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Blocks until the CRITICAL_SECTION is acquired.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32Lock(void)
{
    if (th8Win32CritSecReady) EnterCriticalSection(&th8Win32CritSec);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32Unlock --
 *
 *	Release the process-global CRITICAL_SECTION acquired by
 *	th8Win32Lock.
 *
 * Why / How:
 *	Mirrors th8Win32Lock: checks th8Win32CritSecReady before
 *	calling LeaveCriticalSection.  This symmetry ensures no
 *	unbalanced leave if the section was never entered (i.e.,
 *	the ready flag was clear when Lock was called).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases the CRITICAL_SECTION, allowing other threads to
 *	acquire it.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32Unlock(void)
{
    if (th8Win32CritSecReady) LeaveCriticalSection(&th8Win32CritSec);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetHeap --
 *
 *	Return the private Win32 heap handle, creating it lazily
 *	on first use via HeapCreate.
 *
 * Why / How:
 *	A private heap isolates TH8 allocations from the CRT and
 *	other libraries, reducing fragmentation and enabling accurate
 *	memory tracking via HeapSize.  The heap is created under the
 *	process-global lock to prevent races when multiple threads
 *	call this function concurrently.  HeapCreate(0, 0, 0) creates
 *	a growable, serialized heap (no HEAP_NO_SERIALIZE) so the
 *	heap itself is thread-safe even without the outer lock on
 *	subsequent calls.
 *
 * Results:
 *	Handle to the private heap, or NULL on failure.
 *
 * Side effects:
 *	May create the heap on first call.
 *
 *----------------------------------------------------------------------
 */

static HANDLE
th8Win32GetHeap(void)
{
    th8Win32Lock();
    if (!th8Win32Heap) {
	th8Win32Heap = HeapCreate(0, 0, 0);
    }
    th8Win32Unlock();
    return th8Win32Heap;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Malloc --
 *
 *	Allocate zero-initialized memory from the private Win32 heap
 *	via HeapAlloc with HEAP_ZERO_MEMORY.  Implements the
 *	Th8_Platform xMalloc callback.
 *
 * Why / How:
 *	All TH8 allocations go through this function so they land on
 *	the private heap (isolated from the CRT).  HEAP_ZERO_MEMORY
 *	ensures every allocation starts zeroed, which is a security
 *	measure (prevents information leaks from uninitialized memory)
 *	and matches the xMalloc contract that callers may assume a
 *	zeroed buffer.
 *
 * Results:
 *	Pointer to allocated memory, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory from the private heap.
 *
 *----------------------------------------------------------------------
 */

static void *
th8Win32Malloc(Th8_Interp *interp, void *pCtx, size_t nByte)
{
    HANDLE hHeap = th8Win32GetHeap();
    void *p;

    (void)interp;
    (void)pCtx;

    if (!hHeap) return NULL;
    p = HeapAlloc(
        hHeap, TH8_WIN32_HEAP_FLAGS | HEAP_ZERO_MEMORY, (SIZE_T)nByte);
    return p;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Realloc --
 *
 *	Reallocate memory in the private Win32 heap.  If pPrior is
 *	NULL, behaves like th8Win32Malloc.  Implements the
 *	Th8_Platform xRealloc callback.
 *
 * Why / How:
 *	HeapReAlloc does not accept a NULL pointer, so the NULL-pPrior
 *	case is handled explicitly by delegating to HeapAlloc with
 *	HEAP_ZERO_MEMORY.  For non-NULL pPrior, HeapReAlloc moves the
 *	block in-place or to a new location as needed.  The new tail
 *	bytes (if the block grew) are NOT zero-filled by HeapReAlloc;
 *	callers that need zeroed growth must handle that themselves.
 *
 * Results:
 *	Pointer to reallocated memory, or NULL on failure.
 *
 * Side effects:
 *	Reallocates memory in the private heap.
 *
 *----------------------------------------------------------------------
 */

static void *
th8Win32Realloc(Th8_Interp *interp, void *pCtx, void *pPrior, size_t nByte)
{
    HANDLE hHeap = th8Win32GetHeap();

    (void)interp;
    (void)pCtx;

    if (!hHeap) return NULL;
    if (!pPrior) {
	return HeapAlloc(
	    hHeap, TH8_WIN32_HEAP_FLAGS | HEAP_ZERO_MEMORY, (SIZE_T)nByte);
    }
    return HeapReAlloc(hHeap, TH8_WIN32_HEAP_FLAGS, pPrior, (SIZE_T)nByte);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Free --
 *
 *	Free memory allocated from the private Win32 heap.
 *	Implements the Th8_Platform xFree callback.
 *
 * Why / How:
 *	HeapFree returns the block to the private heap.  NULL pPrior
 *	is handled as a no-op (HeapFree does not tolerate NULL), and
 *	a NULL heap handle is also guarded to avoid calling HeapFree
 *	after the heap has been destroyed during finalization.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory in the private heap.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32Free(Th8_Interp *interp, void *pCtx, void *pPrior)
{
    HANDLE hHeap;

    (void)interp;
    (void)pCtx;

    if (!pPrior) return;
    hHeap = th8Win32GetHeap();
    if (!hHeap) return;
    HeapFree(hHeap, TH8_WIN32_HEAP_FLAGS, pPrior);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32MemorySize --
 *
 *	Return the usable size of a block allocated from the private
 *	heap via HeapSize.  Implements the Th8_Platform xMemorySize
 *	callback.
 *
 * Why / How:
 *	HeapSize returns the actual usable size of the allocation,
 *	which may be larger than the requested size due to alignment
 *	rounding.  The memory-tracking subsystem uses this to maintain
 *	accurate statistics.  A return of (SIZE_T)-1 from HeapSize
 *	indicates an invalid block; this is mapped to 0 for safety.
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
th8Win32MemorySize(Th8_Interp *interp, void *pCtx, void *p)
{
    HANDLE hHeap;
    SIZE_T n;

    (void)interp;
    (void)pCtx;

    if (!p) return 0;
    hHeap = th8Win32GetHeap();
    if (!hHeap) return 0;
    n = HeapSize(hHeap, TH8_WIN32_HEAP_FLAGS, p);
    if (n == (SIZE_T)-1) {
	return 0;
    }
    return (size_t)n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetBasePath --
 *
 *	Return the base path for the TH8 library.  The base path is
 *	the directory containing the TH8 DLL, or its parent directory
 *	if the immediate directory is named "bin" or "bin-static" or
 *	"lib".
 *
 *	Uses GetModuleHandleA(TH8_MODULE_NAME) + GetModuleFileNameA
 *	to find the library's own path.  The result is cached in a
 *	static buffer on first call.  Returns NULL if the base path
 *	cannot be determined.
 *
 * Why / How:
 *	The base path is the root of the filesystem sandbox.  All
 *	xGetData, xDataExists, xLoad, xSetCwd, and xSameFile calls
 *	are validated against it.  The resolved DLL directory is
 *	then examined: if its leaf is "bin", "bin-static", or "lib"
 *	the parent is used instead, matching the standard installation
 *	layout where scripts and data reside beside (not inside) those
 *	directories. Both forward-slash and backslash separators are
 *	handled when stripping the filename and detecting the leaf
 *	directory.  The result is cached under the process-global lock
 *	(bInitialized) so subsequent calls are a fast lock-check-unlock.
 *
 * Results:
 *	Pointer to the static base-path buffer, or NULL if the base
 *	path cannot be determined.
 *
 * Side effects:
 *	Populates zBasePath on first call.
 *
 *----------------------------------------------------------------------
 */

static const char *
th8Win32GetBasePath(void)
{
    th8Win32Lock();
    if (!bInitialized) {
	char zModule[MAX_PATH] = {0};
	HMODULE hMod;
	DWORD n;
	char *slash;

	bInitialized = 1;
	zBasePath[0] = '\0';

	/*
	 * Try to find the loaded TH8 DLL by name.  If the DLL
	 * is statically linked into the EXE, this will fail;
	 * that is fine -- the base path remains unset.
	 */

	hMod = GetModuleHandleA(TH8_MODULE_NAME);

	if (!hMod) {
	    wsprintf(zModule, "GetModuleHandleA(%s) failed", TH8_MODULE_NAME);
	    TH8_TRACE_ERR(NULL, zModule);
	}

	/*
	 * No separate DLL (static/amalgamation build).  Fallback to
	 * the executable's own path.
	 */

	n = GetModuleFileNameA(hMod, zModule, sizeof(zModule));
	if (n == 0 || n >= sizeof(zModule)) {
	    wsprintf(zModule, "GetModuleFileNameA(%p) failed", hMod);
	    TH8_TRACE_ERR(NULL, zModule);
	    th8Win32Unlock();
	    return NULL;
	}

	/*
	 * Resolve to a fully canonical path (expands 8.3 names,
	 * resolves symlinks/junctions on Vista+).
	 */

	if (th8Win32ResolvePath(zModule, zBasePath, sizeof(zBasePath)) !=
	    TH8_OK) {
	    zBasePath[0] = '\0';
	    th8Win32Unlock();
	    return NULL;
	}

	/*
	 * Strip the filename to get the directory.
	 * Handle both '/' and '\\' separators.
	 */

	slash = th8Strrchr(NULL, zBasePath, '\\');
	{
	    char *fslash = th8Strrchr(NULL, zBasePath, '/');

	    if (fslash > slash) slash = fslash;
	}

	if (slash && slash != zBasePath) {
	    *slash = '\0';

	    /*
	     * If the directory is named "bin" or "bin-static" or
	     * "lib", the library resides in a subdirectory of the
	     * base -- use the parent instead.
	     */

	    {
		char *tail = th8Strrchr(NULL, zBasePath, '\\');
		char *ftail = th8Strrchr(NULL, zBasePath, '/');

		if (ftail > tail) tail = ftail;

		if (tail) {
		    if (_stricmp(tail + 1, "bin") == 0 ||
		        _stricmp(tail + 1, "bin-afl") == 0 ||
		        _stricmp(tail + 1, "bin-static") == 0 ||
		        _stricmp(tail + 1, "lib") == 0) {
			*tail = '\0';
		    }
		}
	    }
	} else {
	    zBasePath[0] = '\0';
	}
    }
    th8Win32Unlock();

    return zBasePath[0] ? zBasePath : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32IsUnderBase --
 *
 *	Check whether the absolute path zAbs resides at or under the
 *	base path.  Uses case-insensitive comparison (Windows paths
 *	are case-insensitive).  Returns:
 *	   0  -- exact match (IS the base directory)
 *	   1  -- under the base directory
 *	  -1  -- outside the base directory (foreign)
 *
 * Why / How:
 *	This is the core sandbox gate.  A defense-in-depth pass first
 *	scans zAbs for "." and ".." path segments; their presence in
 *	a supposedly-resolved path indicates incomplete resolution
 *	and the path is rejected outright.  The main check uses
 *	_strnicmp (case-insensitive on Windows, where "C:\Foo" and
 *	"c:\foo" are the same path).  An exact-length match returns 0;
 *	a prefix match followed by a path separator returns 1
 *	(underneath).  Everything else is -1 (foreign).
 *
 * Results:
 *	0 for exact match, 1 for underneath, -1 for foreign.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32IsUnderBase(
    const char *zAbs,  /* Absolute path to check. */
    const char *zBase,  /* Base path. */
    size_t nBase)  /* Length of base path. */
{
    size_t nAbs;

    /*
     * Defense-in-depth: a fully resolved absolute path must not
     * contain "." or ".." segments.  Their presence indicates
     * incomplete resolution, which could defeat the base check.
     */
    {
	const char *p = zAbs;
	while (*p) {
	    const char *seg = p;
	    while (*p && *p != '/' && *p != '\\')
		p++;
	    {
		size_t segLen = (size_t)(p - seg);
		if ((segLen == 1 && seg[0] == '.') ||
		    (segLen == 2 && seg[0] == '.' && seg[1] == '.')) {
		    return -1;  /* suspicious path */
		}
	    }
	    while (*p == '/' || *p == '\\')
		p++;
	}
    }

    nAbs = Th8_Strlen(NULL, zAbs);

    if (nAbs == nBase && _strnicmp(zAbs, zBase, nBase) == 0) {
	return 0;  /* exact match */
    }
    if (nAbs > nBase && _strnicmp(zAbs, zBase, nBase) == 0 &&
        (zAbs[nBase] == '\\' || zAbs[nBase] == '/')) {
	return 1;  /* underneath */
    }
    return -1;  /* foreign */
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32IsPathUnderBase --
 *
 *	Check whether a NUL-terminated path, when fully resolved,
 *	resides at or under the base directory.  Returns non-zero
 *	if the path is allowed, zero if it resolves outside the
 *	base directory (access denied).
 *
 *	Absolute paths (drive-letter or UNC) are rejected outright.
 *	For relative paths, th8Win32ResolvePath resolves "." and
 *	".." components and the result is checked against the base.
 *
 *	When no base path is configured, all paths are allowed.
 *
 * Why / How:
 *	This convenience wrapper combines three checks in one call:
 *	(1) reject absolute paths (drive-letter "C:\" or UNC "\\")
 *	because they bypass the base-relative sandbox model,
 *	(2) resolve the path to canonical form via th8Win32ResolvePath,
 *	and (3) delegate to th8Win32IsUnderBase for the prefix
 *	comparison.  When no base path is configured (development/
 *	embedded mode), all paths pass through to allow unrestricted
 *	filesystem access.  Unresolvable paths are denied
 *	conservatively (fail-closed).
 *
 * Results:
 *	Non-zero if the path is allowed, zero if denied.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32IsPathUnderBase(const char *zPath)
{
    const char *zBase;
    size_t nBase;
    char zFull[MAX_PATH];

    if (!zPath || !zPath[0]) return 0;

    zBase = th8Win32GetBasePath();
    if (!zBase) return 1;  /* no sandbox configured */
    nBase = Th8_Strlen(NULL, zBase);

    /*
     * Reject absolute paths:
     *   - Drive letter:  "C:\" or "C:/"
     *   - UNC:           "\\\\" or "//"
     */
    if ((((zPath[0] >= 'A' && zPath[0] <= 'Z') ||
          (zPath[0] >= 'a' && zPath[0] <= 'z')) &&
         zPath[1] == ':' && (zPath[2] == '\\' || zPath[2] == '/')) ||
        (zPath[0] == '\\' && zPath[1] == '\\') ||
        (zPath[0] == '/' && zPath[1] == '/')) {
	return 0;
    }

    /*
     * Resolve the relative path to a canonical absolute form.
     * th8Win32ResolvePath uses GetFullPathNameA which handles
     * both existing and non-existing paths, resolving "." and
     * ".." components.
     */
    if (th8Win32ResolvePath(zPath, zFull, sizeof(zFull)) != TH8_OK) {
	return 0;  /* cannot resolve; deny conservatively */
    }

    return th8Win32IsUnderBase(zFull, zBase, nBase) >= 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetData --
 *
 *	Read the entire contents of a file into a newly allocated
 *	buffer.  Implements the Th8_Platform xGetData callback.
 *
 * Why / How:
 *	Uses native Win32 file I/O (CreateFileA with narrow paths).
 *	The path is first NUL-terminated into a stack buffer (or a
 *	heap buffer for paths longer than 1024 bytes), then validated
 *	against the base-path sandbox via th8Win32IsPathUnderBase.
 *	The file is opened via CreateFileA, sized via GetFileSize,
 *	then read in a single ReadFile call.  A 256 MB cap prevents
 *	pathological allocations.  The returned buffer is
 *	NUL-terminated for convenience but pnOut reflects the actual
 *	byte count (files may contain embedded NULs).
 *
 * Results:
 *	TH8_OK with *pzOut and *pnOut set; TH8_ERROR on failure.
 *
 * Side effects:
 *	Allocates a buffer via Th8_AttemptMalloc; caller must free.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetData(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    char **pzOut,
    size_t *pnOut)
{
    char zPathBuf[1024];
    char *zPath;
    HANDLE hFile;
    DWORD nSize;
    DWORD nRead;
    char *zBuf;

    (void)pCtx;

    if (nName < sizeof(zPathBuf)) {
	zPath = zPathBuf;
    } else {
	zPath = (char *)TH8_ALLOC_STR(interp, nName);
	if (!zPath) return TH8_ERROR;
    }
    memcpy(zPath, zName, nName);
    zPath[nName] = 0;

    /*
     * Validate that the path is under the base directory.
     */
    if (!th8Win32IsPathUnderBase(zPath)) {
	if (zPath != zPathBuf) {
	    Th8_Free(interp, zPath);
	}
	TH8_TRACE_ERR(NULL, "path outside base directory");
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    hFile = CreateFileA(
        zPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (zPath != zPathBuf) {
	Th8_Free(interp, zPath);
    }
    if (hFile == INVALID_HANDLE_VALUE) {
	TH8_TRACE_ERR(NULL, "CreateFileA failed");
	TH8_TRACE_ERR(NULL, zPath);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    nSize = GetFileSize(hFile, NULL);

    if (nSize == INVALID_FILE_SIZE || nSize > 0x0fffffff) {
	TH8_TRACE_ERR(NULL, "bad file size");
	CloseHandle(hFile);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    zBuf = (char *)TH8_ALLOC_STR(interp, (size_t)nSize);
    if (!zBuf) {
	TH8_TRACE_ERR(NULL, "out of memory");
	CloseHandle(hFile);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    if (!ReadFile(hFile, zBuf, nSize, &nRead, NULL) || nRead != nSize) {
	TH8_TRACE_ERR(NULL, "ReadFile failed");
	CloseHandle(hFile);
	Th8_Free(interp, zBuf);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }
    CloseHandle(hFile);
    zBuf[nSize] = 0;

    *pzOut = zBuf;
    *pnOut = (size_t)nSize;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32DataExists --
 *
 *	Test whether a file or directory exists, and optionally return
 *	its type attributes.  Implements the Th8_Platform xDataExists
 *	callback.
 *
 * Why / How:
 *	Uses GetFileAttributesA to query the filesystem without opening
 *	the file.  The path is NUL-terminated and validated against the
 *	base-path sandbox; paths outside the sandbox are reported as
 *	non-existing (fail-closed).  Attributes are mapped to the
 *	portable TH8_FILE_ATTR_* flags: FILE_ATTRIBUTE_DIRECTORY maps
 *	to TH8_FILE_ATTR_DIRECTORY, FILE_ATTRIBUTE_REPARSE_POINT maps
 *	to TH8_FILE_ATTR_SYMLINK, and everything else maps to
 *	TH8_FILE_ATTR_FILE.
 *
 * Results:
 *	Non-zero if the path exists, zero otherwise.  If pAttrs is
 *	non-NULL and the path exists, *pAttrs is set to the file type.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32DataExists(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    int *pAttrs) /* OUT: file type attrs (may be NULL). */
{
    char zPathBuf[1024];
    char *zPath;
    DWORD attr;

    (void)pCtx;

    if (nName < sizeof(zPathBuf)) {
	zPath = zPathBuf;
    } else {
	zPath = (char *)TH8_ALLOC_STR(interp, nName);
	if (!zPath) return 0;
    }
    memcpy(zPath, zName, nName);
    zPath[nName] = 0;

    /*
     * Validate that the path is under the base directory.
     * Paths outside the sandbox are reported as non-existing.
     */
    if (!th8Win32IsPathUnderBase(zPath)) {
	if (zPath != zPathBuf) {
	    Th8_Free(interp, zPath);
	}
	if (pAttrs) *pAttrs = 0;
	return 0;
    }

    attr = GetFileAttributesA(zPath);
    if (zPath != zPathBuf) {
	Th8_Free(interp, zPath);
    }
    if (pAttrs && attr != INVALID_FILE_ATTRIBUTES) {
	int symFlag = (attr & FILE_ATTRIBUTE_REPARSE_POINT)
	                ? TH8_FILE_ATTR_SYMLINK
	                : TH8_FILE_ATTR_NONE;

	if (attr & FILE_ATTRIBUTE_DIRECTORY) {
	    *pAttrs = TH8_FILE_ATTR_DIRECTORY | symFlag;
	} else {
	    *pAttrs = TH8_FILE_ATTR_FILE | symFlag;
	}
    }
    return (attr != INVALID_FILE_ATTRIBUTES);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32VerifyAuthenticode --
 *
 *	Verify the Authenticode digital signature of a DLL file
 *	before loading it.  Enforced on all non-DEBUG builds.
 *
 *	The verification uses the strictest WinVerifyTrust settings
 *	compatible with offline operation:
 *
 *	  - WINTRUST_ACTION_GENERIC_VERIFY_V2 (standard Authenticode)
 *	  - WTD_UI_NONE (no interactive UI prompts)
 *	  - WTD_REVOKE_WHOLECHAIN (verify entire certificate chain)
 *	  - WTD_CACHE_ONLY_URL_RETRIEVAL (no network fetches; use
 *	    only locally cached CRLs/OCSP responses)
 *	  - WTD_SAFER_FLAG (apply SAFER trust decisions)
 *	  - WTD_HASH_ONLY_FLAG is NOT set (full signature check,
 *	    not just hash)
 *	  - WTD_LIFETIME_SIGNING_FLAG (accept signatures with
 *	    expired certificates if timestamped before expiry)
 *
 *	Only ERROR_SUCCESS (S_OK, 0) is accepted as valid.  All
 *	other return codes (including TRUST_E_NOSIGNATURE,
 *	TRUST_E_SUBJECT_NOT_TRUSTED, CERT_E_EXPIRED, etc.)
 *	are treated as failures.
 *
 * Why / How:
 *	This is a critical trust-chain validation gate: it runs BEFORE
 *	LoadLibraryA so that unsigned or tampered DLLs are never mapped
 *	into the process address space.  The trust chain validation
 *	checks the Authenticode signature embedded in the PE file,
 *	verifies every certificate in the signing chain up to a
 *	trusted root, and checks revocation status using locally cached
 *	CRLs/OCSP responses (no network I/O to avoid hanging on
 *	air-gapped systems).  WTD_LIFETIME_SIGNING_FLAG ensures that
 *	a timestamped signature remains valid even after the signing
 *	certificate expires (the signature was created while the cert
 *	was live).  The UTF-8 DLL path is converted to UTF-16 via
 *	ConvertUTF_v2 because WinVerifyTrust only accepts wide strings.
 *	After verification, WTD_STATEACTION_CLOSE is called to release
 *	the trust provider's internal state.
 *
 * Results:
 *	TH8_OK if the file has a valid Authenticode signature;
 *	TH8_ERROR otherwise.
 *
 * Side effects:
 *	Opens and reads the DLL file; may query the local certificate
 *	store and CRL cache.
 *
 *----------------------------------------------------------------------
 */

#  if defined(NDEBUG)
static int
th8Win32VerifyAuthenticode(
    Th8_Interp *interp,
    const char *zPath,  /* NUL-terminated DLL path (UTF-8). */
    size_t nPath)  /* Byte length of zPath. */
{
    WINTRUST_FILE_INFO fileInfo;
    WINTRUST_DATA trustData;
    GUID actionId = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status;

    /*
     * Convert the UTF-8 path to UTF-16 for WinVerifyTrust.
     */

    WCHAR wPathBuf[1024];
    WCHAR *wPath;
    WCHAR *wAlloc = NULL;

    {
	const UTF8 *pSrc = (const UTF8 *)zPath;
	UTF16 *pDst;
	ConversionResult cr;

	if (nPath + 1 <= sizeof(wPathBuf) / sizeof(wPathBuf[0])) {
	    wPath = wPathBuf;
	} else {
	    wAlloc = (WCHAR *)TH8_ALLOC_STR_MUL(interp, nPath, sizeof(WCHAR));
	    if (!wAlloc) return TH8_ERROR;
	    wPath = wAlloc;
	}

	pDst = (UTF16 *)wPath;
	cr = ConvertUTF8toUTF16(
	    &pSrc, (const UTF8 *)zPath + nPath, &pDst,
	    (UTF16 *)wPath + nPath + 1, strictConversion);

	if (cr != conversionOK) {
	    Th8_Free(interp, wAlloc);
	    Th8_SetResult(
	        interp, "xLoad: cannot convert DLL path to UTF-16",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	*pDst = 0;  /* NUL-terminate */
    }

    /*
     * Set up the WINTRUST_FILE_INFO structure.
     */

    memset(&fileInfo, 0, sizeof(fileInfo));
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = wPath;
    fileInfo.hFile = NULL; /* Let WinVerifyTrust open the file. */
    fileInfo.pgKnownSubject = NULL;

    /*
     * Set up the WINTRUST_DATA structure with the strictest
     * offline-compatible settings.
     */

    memset(&trustData, 0, sizeof(trustData));
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.hWVTStateData = NULL;
    trustData.dwProvFlags = WTD_SAFER_FLAG | WTD_CACHE_ONLY_URL_RETRIEVAL |
                            WTD_LIFETIME_SIGNING_FLAG;

    /*
     * Call WinVerifyTrust.  Only ERROR_SUCCESS (== 0) is a
     * valid, trusted signature.  Every other value -- including
     * TRUST_E_NOSIGNATURE, TRUST_E_SUBJECT_NOT_TRUSTED,
     * TRUST_E_EXPLICIT_DISTRUST, CERT_E_EXPIRED, and
     * CERT_E_CHAINING -- is rejected.
     */

    status =
        WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &actionId, &trustData);

    /*
     * Clean up the state data from the verification.
     */

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &actionId, &trustData);

    Th8_Free(interp, wAlloc);

    if (status != ERROR_SUCCESS) {
	Th8_EmitTrace(
	    interp,
	    "xLoad: Authenticode verification failed for "
	    "\"%.*s\" (status 0x%08lx)\n",
	    (int)nPath, zPath, (unsigned long)status);
	Th8_ErrorMessage(
	    interp,
	    "xLoad: DLL is not signed or signature is "
	    "invalid: \"",
	    zPath, nPath);
	return TH8_ERROR;
    }

    return TH8_OK;
}
#  endif /* NDEBUG */


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Load --
 *
 *	Load a DLL via LoadLibraryA / GetProcAddress and call its
 *	init function.  Implements the Th8_Platform xLoad callback.
 *
 *	Name format:  <dllPath>:<symbolPrefix>_Init
 *
 *	Same semantics as the POSIX version but using Win32 APIs.
 *	On non-DEBUG builds, the DLL's Authenticode signature is
 *	verified before loading (see th8Win32VerifyAuthenticode).
 *
 * Why / How:
 *	The colon separator divides the library path from the symbol
 *	prefix.  The library path is validated against the base-path
 *	sandbox.  On non-DEBUG builds, th8Win32VerifyAuthenticode runs
 *	before LoadLibraryA to ensure only signed DLLs are mapped into
 *	the process.  The init function name is constructed by
 *	appending "_Init" to the symbol prefix (or using zProc
 *	verbatim if provided).  After a successful load, the HMODULE
 *	is saved in the per-interpreter tracking list via
 *	th8Win32SaveHandle for later unloading.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	Loads a DLL and calls its init function, which typically
 *	registers new commands.
 *
 *----------------------------------------------------------------------
 */

static void th8Win32SaveHandle(Th8_Interp *, const char *, size_t, HMODULE);

typedef int (*Th8_LoadInitProc)(Th8_Interp *);

static int
th8Win32Load(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    const char *zProc,
    size_t nProc)
{
    const char *zColon;
    size_t nLib;
    size_t nSym;
    char zLibBuf[1024];
    char zSymBuf[256];
    char *zLib;
    char *zSym;
    HMODULE hLib;
    Th8_LoadInitProc xInit;

    (void)pCtx;

    if (!interp || !zName || nName == 0) {
	return TH8_ERROR;
    }
    if (nName == TH8_NOLEN) {
	size_t k = 0;

	while (zName[k])
	    k++;
	nName = k;
    }

    /*
     * Find the colon separator.
     */

    zColon = 0;
    {
	size_t i;

	for (i = nName - 1; i >= 0; i--) {
	    if (zName[i] == ':') {
		zColon = &zName[i];
		break;
	    }
	}
    }
    if (!zColon) {
	Th8_SetResult(
	    interp, "xLoad: name must be \"library:symbol\"", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Extract DLL path (before colon).
     */

    nLib = (size_t)(zColon - zName);
    if (nLib < sizeof(zLibBuf)) {
	zLib = zLibBuf;
    } else {
	zLib = (char *)TH8_ALLOC_STR(interp, nLib);
	if (!zLib) return TH8_ERROR;
    }
    memcpy(zLib, zName, nLib);
    zLib[nLib] = 0;

    /*
     * The library path must be under the base directory.
     */
    if (!th8Win32IsPathUnderBase(zLib)) {
	if (zLib != zLibBuf) {
	    Th8_Free(interp, zLib);
	}
	Th8_SetResult(
	    interp, "xLoad: library path outside base directory", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Verify Authenticode signature (non-DEBUG builds only).
     * The check runs BEFORE LoadLibraryA so that unsigned or
     * tampered DLLs are never mapped into the process.
     */

#  if defined(NDEBUG)
    if (th8Win32VerifyAuthenticode(interp, zLib, nLib) != TH8_OK) {
	if (zLib != zLibBuf) Th8_Free(interp, zLib);
	return TH8_ERROR;
    }
#  endif

    /*
     * Build the init function name.
     */

    if (zProc && nProc > 0) {
	if (nProc == TH8_NOLEN) {
	    size_t k = 0;

	    while (zProc[k])
		k++;
	    nProc = k;
	}
	nSym = nProc;
    } else {
	zProc = zColon + 1;
	nProc = nName - nLib - 1;
	nSym = nProc + 5;  /* + "_Init" */
    }

    if (nSym < sizeof(zSymBuf)) {
	zSym = zSymBuf;
    } else {
	zSym = (char *)TH8_ALLOC_STR(interp, nSym);
	if (!zSym) {
	    if (zLib != zLibBuf) Th8_Free(interp, zLib);
	    return TH8_ERROR;
	}
    }

    memcpy(zSym, zProc, nProc);
    if (nSym > nProc) {
	memcpy(&zSym[nProc], "_Init", 5);
    }
    zSym[nSym] = 0;

    /*
     * LoadLibrary.
     */

    hLib = LoadLibraryA(zLib);
    if (!hLib) {
	Th8_ErrorMessage(
	    interp, "xLoad: LoadLibrary failed for \"", zLib, nLib);
	TH8_TRACE_ERR(NULL, "LoadLibraryA failed");
	TH8_TRACE_ERR(NULL, zLib);
	if (zLib != zLibBuf) Th8_Free(interp, zLib);
	if (zSym != zSymBuf) Th8_Free(interp, zSym);
	return TH8_ERROR;
    }

    /*
     * GetProcAddress.
     */

    xInit = (Th8_LoadInitProc)(void *)GetProcAddress(hLib, zSym);
    if (!xInit) {
	Th8_ErrorMessage(interp, "xLoad: symbol not found \"", zSym, nSym);
	TH8_TRACE_ERR(NULL, "GetProcAddress failed");
	TH8_TRACE_ERR(NULL, zSym);
	FreeLibrary(hLib);
	if (zLib != zLibBuf) Th8_Free(interp, zLib);
	if (zSym != zSymBuf) Th8_Free(interp, zSym);
	return TH8_ERROR;
    }

    /*
     * Save the LoadLibrary handle for potential later unloading.
     */

    th8Win32SaveHandle(interp, zName, nName, hLib);

    if (zLib != zLibBuf) Th8_Free(interp, zLib);
    if (zSym != zSymBuf) Th8_Free(interp, zSym);

    /*
     * Call the init function.
     */

    return xInit(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Win32 LoadLibrary handle tracking --
 *
 *	A process-global linked list mapping library names to their
 *	LoadLibrary handles.  Protected by th8Win32Lock/Unlock;
 *	entries are reference-counted to prevent use-after-free
 *	when one thread looks up a handle while another unloads it.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_Win32LibHandle Th8_Win32LibHandle;
struct Th8_Win32LibHandle {
    char *zName;  /* Full load name ("lib:sym"), owned. */
    size_t nName;  /* Byte length of zName. */
    HMODULE hLib;  /* LoadLibrary handle. */
    int nRef;   /* Reference count. */
    Th8_Win32LibHandle *pNext; /* Next in global list. */
};

/* Library list is now per-interpreter via th8GetPlatformLibs. */

/*
 *----------------------------------------------------------------------
 *
 * th8Win32SaveHandle --
 *
 *	Record a LoadLibrary handle in the per-interpreter tracking
 *	list.  The name and handle are stored so that th8Win32Unload
 *	can look up and close the library later.
 *
 * Why / How:
 *	Each loaded DLL gets a Th8_Win32LibHandle node allocated via
 *	Th8_AttemptMalloc.  The node is prepended to the per-interpreter
 *	linked list (accessed via th8GetPlatformLibs/th8SetPlatformLibs)
 *	under the process-global lock.  The initial reference count is
 *	1; th8Win32FindHandle increments it when a lookup is in progress,
 *	and th8Win32ReleaseHandle decrements it, ensuring that a handle
 *	is not freed while another thread holds a reference.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory for the tracking node and its name copy.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32SaveHandle(
    Th8_Interp *interp, /* Interpreter (for Th8_AttemptMalloc). */
    const char *zName,  /* Library name. */
    size_t nName,  /* Byte length of zName. */
    HMODULE hLib)  /* LoadLibrary handle. */
{
    Th8_Win32LibHandle *p;

    p = (Th8_Win32LibHandle *)TH8_ALLOC(interp, sizeof(Th8_Win32LibHandle));
    if (!p) return;
    p->zName = (char *)TH8_ALLOC_STR(interp, nName);
    if (!p->zName) {
	Th8_Free(interp, p);
	return;
    }
    memcpy(p->zName, zName, nName);
    p->zName[nName] = 0;
    p->nName = nName;
    p->hLib = hLib;
    p->nRef = 1;

    th8Win32Lock();
    {
	Th8_Win32LibHandle *pList;
	pList = (Th8_Win32LibHandle *)th8GetPlatformLibs(interp);
	p->pNext = pList;
	th8SetPlatformLibs(interp, p);
    }
    th8Win32Unlock();
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32FindHandle --
 *
 *	Look up a loaded library by name in the per-interpreter
 *	tracking list.  If found, increments the reference count
 *	before returning.
 *
 * Why / How:
 *	Traverses the per-interpreter linked list under the
 *	process-global lock, comparing names by length and byte
 *	content.  When a match is found, nRef is incremented while
 *	still under the lock so that a concurrent th8Win32RemoveHandle
 *	cannot free the node before the caller is done with it.  The
 *	caller must call th8Win32ReleaseHandle when finished.
 *
 * Results:
 *	Pointer to the matching Th8_Win32LibHandle, or NULL if not
 *	found.
 *
 * Side effects:
 *	Increments the reference count of the returned node.
 *
 *----------------------------------------------------------------------
 */

static Th8_Win32LibHandle *
th8Win32FindHandle(
    Th8_Interp *interp, /* Interpreter (for memcmp). */
    const char *zName,  /* Library name. */
    size_t nName)  /* Byte length of zName. */
{
    Th8_Win32LibHandle *p;
    Th8_Win32LibHandle *result = NULL;

    (void)interp;
    th8Win32Lock();
    for (p = (Th8_Win32LibHandle *)th8GetPlatformLibs(interp); p;
         p = p->pNext) {
	if (p->nName == nName && 0 == memcmp(p->zName, zName, nName)) {
	    p->nRef++;
	    result = p;
	    break;
	}
    }
    th8Win32Unlock();
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32ReleaseHandle --
 *
 *	Decrement the reference count on a Th8_Win32LibHandle.  When
 *	the count reaches zero, close the library and free the node.
 *
 * Why / How:
 *	The reference count is decremented under the process-global
 *	lock to prevent races.  If nRef drops to zero, the HMODULE
 *	is closed via FreeLibrary and the node's name and struct are
 *	freed.  The unlock-before-free pattern avoids holding the
 *	lock during potentially slow FreeLibrary/heap operations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May call FreeLibrary and free the tracking node.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32ReleaseHandle(Th8_Interp *interp, Th8_Win32LibHandle *p)
{
    if (!p) return;
    th8Win32Lock();
    p->nRef--;
    if (p->nRef <= 0) {
	/*
	 * No more references. Close the library and free.
	 */
	if (p->hLib) {
	    if (!FreeLibrary(p->hLib)) {
		TH8_TRACE_ERR(NULL, "FreeLibrary failed in unload");
	    }
	    p->hLib = 0;
	}
	th8Win32Unlock();
	Th8_Free(interp, p->zName);
	Th8_Free(interp, p);
	return;
    }
    th8Win32Unlock();
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32RemoveHandle --
 *
 *	Remove a library tracking node from the per-interpreter list
 *	by name.  Decrements the reference count and, if it reaches
 *	zero, closes the library and frees the node.
 *
 * Why / How:
 *	Traverses the per-interpreter linked list under the
 *	process-global lock, using a pointer-to-pointer to unlink the
 *	matching node in one pass.  After unlinking, the list head is
 *	updated via th8SetPlatformLibs and the lock is released before
 *	the potentially slow FreeLibrary and Th8_Free calls.  The
 *	reference count is decremented after removal: if another thread
 *	holds a reference (via th8Win32FindHandle), the node survives
 *	until that thread calls th8Win32ReleaseHandle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unlinks the node from the list; may call FreeLibrary and free
 *	the node.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32RemoveHandle(
    Th8_Interp *interp, /* Interpreter (for Th8_Free). */
    const char *zName,  /* Library name. */
    size_t nName)  /* Byte length of zName. */
{
    Th8_Win32LibHandle **pp;

    th8Win32Lock();
    {
	Th8_Win32LibHandle *pList;

	pList = (Th8_Win32LibHandle *)th8GetPlatformLibs(interp);
	for (pp = &pList; *pp; pp = &(*pp)->pNext) {
	    Th8_Win32LibHandle *p = *pp;

	    if (p->nName == nName && 0 == memcmp(p->zName, zName, nName)) {
		*pp = p->pNext;
		p->pNext = 0;
		th8SetPlatformLibs(interp, pList);
		th8Win32Unlock();
		p->nRef--;
		if (p->nRef <= 0) {
		    if (p->hLib) {
			if (!FreeLibrary(p->hLib)) {
			    TH8_TRACE_ERR(
			        NULL, "FreeLibrary failed in unload");
			}
			p->hLib = 0;
		    }
		    Th8_Free(interp, p->zName);
		    Th8_Free(interp, p);
		}
		return;
	    }
	}
    }
    th8Win32Unlock();
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32UnloadLibs --
 *
 *	Close all LoadLibrary handles in the global library list and
 *	free the associated memory.  Called from xDeleteInterp to
 *	clean up per-interpreter library state.
 *
 * Why / How:
 *	Detaches the entire per-interpreter list under the lock, then
 *	walks it without the lock held to close each HMODULE and free
 *	each node.  This bulk-close approach is simpler and faster
 *	than unloading libraries one by one, and avoids holding the
 *	lock during FreeLibrary calls that could block.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls FreeLibrary on every tracked handle and frees all
 *	tracking nodes.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32UnloadLibs(Th8_Interp *interp)
{
    Th8_Win32LibHandle *pList;

    th8Win32Lock();
    pList = (Th8_Win32LibHandle *)th8GetPlatformLibs(interp);
    th8SetPlatformLibs(interp, NULL);
    th8Win32Unlock();

    while (pList) {
	Th8_Win32LibHandle *p = pList;

	pList = p->pNext;
	if (p->hLib) {
	    /* _Unload moved to th8Win32PreDeleteInterp (called early
	     * in Th8_DeleteInterp, while the interp is still fully
	     * functional).  Here we only free the handle, after
	     * namespace cleanup has completed and any remaining
	     * library xDel callbacks have run. */
	    if (!FreeLibrary(p->hLib)) {
		TH8_TRACE_ERR(NULL, "FreeLibrary failed in unload");
	    }
	}
	Th8_Free(interp, p->zName);
	Th8_Free(interp, p);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32DeleteInterp --
 *
 *	Platform xDeleteInterp callback.  Notified when an
 *	interpreter is about to be deleted.  Releases all loaded
 *	library handles.
 *
 * Why / How:
 *	When an interpreter is destroyed, any DLLs it loaded must be
 *	closed to avoid resource leaks (HMODULE handles and the memory
 *	they pin).  This callback delegates to th8Win32UnloadLibs,
 *	which closes all tracked handles for this interpreter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls FreeLibrary for all libraries loaded by this interpreter.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32DeleteInterp(Th8_Interp *interp, void *pCtx)
{
    (void)pCtx;
    th8Win32UnloadLibs(interp);
}


typedef int (*Th8_Win32UnloadProc)(Th8_Interp *, int);

/*
 *----------------------------------------------------------------------
 *
 * th8Win32CallUnloadProc --
 *
 *	Look up "<Prefix>_Unload" in hLib via GetProcAddress and, if
 *	present, invoke it.  Prefix is derived from the part of zName
 *	after a ':' (matching th8Win32Load's symbol convention).  No
 *	colon means no derivable suffix and the call is a no-op.
 *
 * Why / How:
 *	Centralizes the _Unload entry-point invocation that two paths
 *	share: th8Win32Unload (the explicit [unload] command) and
 *	th8Win32UnloadLibs (the implicit cleanup when an interpreter
 *	is being deleted).  Without this hook firing on the implicit
 *	path, a library's process-global state -- e.g. SQLite's
 *	allocator caches and date/time formatter freed only via
 *	sqlite3_shutdown() inside its _Unload callback -- leaks until
 *	process exit.  Mirrors the POSIX th8PosixCallUnloadProc fix.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May call the library's _Unload function.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32CallUnloadProc(
    Th8_Interp *interp,
    HMODULE hLib,
    const char *zName,
    size_t nName,
    int cbFlags)
{
    const char *zColon = 0;
    const char *zProc;
    size_t nProc;
    size_t nSym;
    char zSymBuf[256];
    char *zSym;
    Th8_Win32UnloadProc xUnloadFn;
    size_t i;

    if (!interp || !hLib || !zName || nName == 0) {
	return;
    }

    for (i = 0; i < nName; i++) {
	if (zName[i] == ':') {
	    zColon = &zName[i];
	    break;
	}
    }
    if (!zColon) {
	return;
    }
    zProc = zColon + 1;
    nProc = nName - (size_t)(zColon - zName) - 1;
    nSym = nProc + 7;  /* "_Unload" */

    if (nSym < sizeof(zSymBuf)) {
	zSym = zSymBuf;
    } else {
	zSym = (char *)TH8_ALLOC_STR(interp, nSym);
	if (!zSym) {
	    return;
	}
    }
    memcpy(zSym, zProc, nProc);
    memcpy(&zSym[nProc], "_Unload", 7);
    zSym[nSym] = 0;

    xUnloadFn = (Th8_Win32UnloadProc)(void *)GetProcAddress(hLib, zSym);
    if (xUnloadFn) {
	xUnloadFn(interp, cbFlags);
    }

    if (zSym != zSymBuf) Th8_Free(interp, zSym);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32PreDeleteInterp --
 *
 *	Platform xPreDeleteInterp callback.  Called at the start of
 *	Th8_DeleteInterp, while the interpreter is still fully
 *	functional.  Walks the per-interpreter loaded library list
 *	and invokes each library's _Unload entry point.
 *
 * Why / How:
 *	Without this early notification, libraries that publish
 *	process-global state via _Unload leak that state for the
 *	rest of the process.  Calling _Unload from the late
 *	xDeleteInterp would crash because by then the global
 *	namespace, packages, and math-func registry have all been
 *	freed -- Th8_Eval would dereference a NULL pGlobalNs.  The
 *	FreeLibrary itself stays in xDeleteInterp (called late) so
 *	that any library xDel callbacks invoked during namespace
 *	cleanup can still execute the library's text segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May invoke each loaded library's _Unload entry point.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32PreDeleteInterp(Th8_Interp *interp, void *pCtx)
{
    Th8_Win32LibHandle *p;

    (void)pCtx;

    th8Win32Lock();
    p = (Th8_Win32LibHandle *)th8GetPlatformLibs(interp);
    th8Win32Unlock();

    while (p) {
	if (p->hLib) {
	    th8Win32CallUnloadProc(
	        interp, p->hLib, p->zName, p->nName,
	        TH8_UNLOAD_DETACH_FROM_PROCESS);
	}
	p = p->pNext;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Unload --
 *
 *	Unload (finalize and optionally close) a DLL.  Implements
 *	the Th8_Platform xUnload callback.
 *
 *	Looks up the LoadLibrary handle from the process-global list.
 *	Derives the _Unload symbol name from the library name (same
 *	colon convention as th8Win32Load).  If the _Unload symbol
 *	exists, calls it.  If bClose is non-zero, calls FreeLibrary
 *	and removes the handle from the tracking list.
 *
 * Why / How:
 *	The library is first looked up via th8Win32FindHandle (which
 *	bumps the refcount).  The _Unload entry point name is built
 *	from the symbol prefix (after the colon) + "_Unload", or from
 *	zProc if provided.  The _Unload function, if present, is called
 *	with TH8_UNLOAD_DETACH_FROM_PROCESS or
 *	TH8_UNLOAD_DETACH_FROM_INTERPRETER depending on bClose.  Not
 *	finding _Unload is not an error (the library may not need
 *	cleanup).  After calling _Unload, th8Win32RemoveHandle unlinks
 *	the tracking node (one refcount decrement), and
 *	th8Win32ReleaseHandle drops our FindHandle reference (another
 *	decrement).  When all references are gone, FreeLibrary runs.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the library is not found.
 *
 * Side effects:
 *	May call a finalization function and/or close the library.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32Unload(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,  /* Library name ("lib:sym" format). */
    size_t nName,  /* Byte length of zName. */
    const char *zProc,  /* Unload proc name, or NULL. */
    size_t nProc,  /* Byte length of zProc. */
    int bClose)   /* 0 = keeplibrary, 1 = FreeLibrary. */
{
    Th8_Win32LibHandle *pHandle;
    HMODULE hLib;
    const char *zColon = 0;
    size_t nPrefix;
    size_t nSym;
    char zSymBuf[256];
    char *zSym;
    Th8_Win32UnloadProc xUnloadFn;
    size_t i;

    (void)pCtx;

    if (!interp || !zName || nName == 0) {
	return TH8_ERROR;
    }

    /*
     * Find the handle (increments the reference count).
     */

    pHandle = th8Win32FindHandle(interp, zName, nName);
    if (!pHandle) {
	Th8_ErrorMessage(
	    interp, "xUnload: library not loaded: \"", zName, nName);
	return TH8_ERROR;
    }
    hLib = pHandle->hLib;

    /*
     * Find the colon separator.
     */

    for (i = 0; i < nName; i++) {
	if (zName[i] == ':') {
	    zColon = &zName[i];
	    break;
	}
    }

    /*
     * Build the _Unload symbol name.
     */

    if (zProc && nProc > 0) {
	nSym = (nProc == TH8_NOLEN) ? 0 : nProc;
	if (nSym == 0) {
	    size_t k = 0;

	    while (zProc[k])
		k++;
	    nSym = k;
	}
    } else if (zColon) {
	nPrefix = nName - (size_t)(zColon - zName) - 1;
	nSym = nPrefix + 7;  /* "_Unload" */
	zProc = zColon + 1;
	nProc = nPrefix;
    } else {
	nSym = 0;
    }

    /*
     * Look up and call the _Unload entry point (if it exists).
     */

    if (nSym > 0) {
	if (nSym < sizeof(zSymBuf)) {
	    zSym = zSymBuf;
	} else {
	    zSym = (char *)TH8_ALLOC_STR(interp, nSym);
	    if (!zSym) {
		th8Win32ReleaseHandle(interp, pHandle);
		return TH8_ERROR;
	    }
	}
	memcpy(zSym, zProc, nProc);
	memcpy(&zSym[nProc], "_Unload", 7);
	zSym[nSym] = 0;

	xUnloadFn = (Th8_Win32UnloadProc)(void *)GetProcAddress(hLib, zSym);
	if (xUnloadFn) {
	    int cbFlags = bClose ? TH8_UNLOAD_DETACH_FROM_PROCESS
	                         : TH8_UNLOAD_DETACH_FROM_INTERPRETER;

	    xUnloadFn(interp, cbFlags);
	}
	/* Not finding _Unload is not an error. */
	if (zSym != zSymBuf) Th8_Free(interp, zSym);
    }

    /*
     * Remove the tracking entry (decrements one reference).
     * If bClose was not requested, the library remains loadable
     * but the tracking entry is still removed.
     */

    th8Win32RemoveHandle(interp, zName, nName);

    /*
     * Release our FindHandle reference (decrements another
     * reference).  When this is the last reference, the
     * library is closed and the entry is freed.
     */

    th8Win32ReleaseHandle(interp, pHandle);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32WriteHandle --
 *
 *	Write UTF-8 data to a Win32 handle.  If the handle is a
 *	console, convert to UTF-16 via ConvertUTF_v2 and use
 *	WriteConsoleW for correct Unicode rendering.  Otherwise
 *	write raw bytes via WriteFile (pipes, redirected files).
 *
 * Why / How:
 *	WriteFile to a console handle interprets bytes in the console's
 *	current code page, which is typically not UTF-8.  To render
 *	Unicode correctly, console output must go through WriteConsoleW
 *	with UTF-16 data.  GetConsoleMode is used to detect whether the
 *	handle is a real console or a pipe/file redirect.  For consoles,
 *	the UTF-8 input is converted to UTF-16 via ConvertUTF8toUTF16
 *	(from ConvertUTF_v2.c), using a stack buffer for small writes
 *	or a heap buffer for large ones.  For non-console handles, raw
 *	UTF-8 bytes are written directly via WriteFile, preserving the
 *	encoding for downstream consumers (e.g., piped processes).
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	Writes data to the handle.  May allocate a temporary UTF-16
 *	buffer for console output.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32WriteHandle(Th8_Interp *interp, HANDLE h, const char *z, size_t n)
{
    DWORD dwMode;
    DWORD nWritten;

    if (GetConsoleMode(h, &dwMode)) {
	/*
	 * Console -- convert UTF-8 to UTF-16 and use
	 * WriteConsoleW for proper Unicode output.
	 *
	 * Worst case: each UTF-8 byte becomes one UTF-16
	 * code unit (BMP), or a surrogate pair for
	 * supplementary characters.  n+1 UTF-16 units
	 * is always sufficient.
	 */

	UTF16 wBuf[4096];
	UTF16 *wAlloc = NULL;
	UTF16 *wData;
	const UTF8 *pSrc;
	UTF16 *pDst;
	ConversionResult cr;

	if (n + 1 <= sizeof(wBuf) / sizeof(wBuf[0])) {
	    wData = wBuf;
	} else {
	    wAlloc = (UTF16 *)TH8_ALLOC_STR_MUL(interp, n, sizeof(UTF16));
	    if (!wAlloc) return TH8_ERROR;
	    wData = wAlloc;
	}

	pSrc = (const UTF8 *)z;
	pDst = wData;
	cr = ConvertUTF8toUTF16(
	    &pSrc, (const UTF8 *)z + n, &pDst, wData + n + 1,
	    lenientConversion);

	if (cr != conversionOK && cr != targetExhausted) {
	    Th8_Free(interp, wAlloc);
	    TH8_TRACE_ERR(NULL, "ConvertUTF8toUTF16 failed");
	    return TH8_ERROR;
	}

	if (!WriteConsoleW(
	        h, wData, (DWORD)(pDst - wData), &nWritten, NULL)) {
	    Th8_Free(interp, wAlloc);
	    TH8_TRACE_ERR(NULL, "WriteConsoleW failed");
	    return TH8_ERROR;
	}
	Th8_Free(interp, wAlloc);
    } else {
	/*
	 * Pipe or redirected file -- write raw UTF-8 bytes.
	 */

	if (!WriteFile(h, z, (DWORD)n, &nWritten, NULL)) {
	    TH8_TRACE_ERR(NULL, "WriteFile failed");
	    return TH8_ERROR;
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Input --
 *
 *	Read a line of input from stdin or a redirected channel.
 *	Implements the Th8_Platform xInput callback.
 *
 * Why / How:
 *	Uses ReadConsoleW when stdin is a real console (proper
 *	Unicode input), converting the result from UTF-16 to
 *	UTF-8 via ConvertUTF16toUTF8 (from ConvertUTF_v2.c).
 *	Falls back to byte-at-a-time ReadFile for pipes and
 *	redirected input, stopping at newline or EOF.  The
 *	console vs. pipe distinction is made via GetConsoleMode:
 *	a real console supports Unicode reading, while pipes carry
 *	raw bytes (assumed UTF-8 by convention).  The result is
 *	allocated via Th8_AttemptMalloc and NUL-terminated.  If
 *	pChannel is non-NULL it is used as the input handle;
 *	otherwise STD_INPUT_HANDLE is used.
 *
 * Results:
 *	TH8_OK with *pzOut and *pnOut set; TH8_ERROR on failure.
 *
 * Side effects:
 *	Blocks until a line of input is available.  Allocates a
 *	buffer via Th8_AttemptMalloc; caller must free.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32Input(
    Th8_Interp *interp, /* Interpreter. */
    void *pCtx,  /* Platform's pCtx (unused). */
    char **pzOut,
    size_t *pnOut,
    void *pChannel) /* Channel (unused). */
{
    HANDLE h;
    DWORD dwMode;
    char *zResult;

    (void)pCtx;

    if (!interp) {
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    /*
     * Use the redirected channel if provided, otherwise
     * fall back to the standard input handle.
     */
    if (pChannel) {
	h = (HANDLE)pChannel;
    } else {
	h = GetStdHandle(STD_INPUT_HANDLE);
    }
    if (h == INVALID_HANDLE_VALUE) {
	TH8_TRACE_ERR(NULL, "GetStdHandle failed");
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    if (GetConsoleMode(h, &dwMode)) {
	/*
	 * Console -- read UTF-16 via ReadConsoleW, then
	 * convert to UTF-8 via ConvertUTF_v2.
	 *
	 * ReadConsoleW returns a full line including \r\n.
	 */

	WCHAR wBuf[4096];
	DWORD nWchars = 0;
	const UTF16 *pSrc;
	UTF8 *pDst;
	UTF8 zUtf8[4096 * 3]; /* worst case: 3 UTF-8 bytes per BMP char */
	ConversionResult cr;
	size_t nUtf8;

	if (!ReadConsoleW(
	        h, wBuf, (sizeof(wBuf) / sizeof(wBuf[0])) - 1, &nWchars,
	        NULL) ||
	    nWchars == 0) {
	    *pzOut = 0;
	    *pnOut = 0;
	    return TH8_ERROR;
	}

	pSrc = (const UTF16 *)wBuf;
	pDst = zUtf8;
	cr = ConvertUTF16toUTF8(
	    &pSrc, (const UTF16 *)wBuf + nWchars, &pDst,
	    zUtf8 + sizeof(zUtf8), lenientConversion);

	if (cr != conversionOK && cr != targetExhausted) {
	    TH8_TRACE_ERR(NULL, "ConvertUTF16toUTF8 failed");
	    *pzOut = 0;
	    *pnOut = 0;
	    return TH8_ERROR;
	}

	nUtf8 = (size_t)(pDst - zUtf8);
	zResult = (char *)TH8_ALLOC_STR(interp, nUtf8);
	if (!zResult) {
	    TH8_TRACE_ERR(NULL, "out of memory");
	    *pzOut = 0;
	    *pnOut = 0;
	    return TH8_ERROR;
	}
	memcpy(zResult, zUtf8, nUtf8);
	zResult[nUtf8] = '\0';
	*pzOut = zResult;
	*pnOut = nUtf8;
    } else {
	/*
	 * Pipe or redirected file -- read raw bytes one at
	 * a time until newline, EOF, or buffer full.  The
	 * caller is responsible for ensuring the input is
	 * valid UTF-8.
	 */

	char zBuf[4096];
	DWORD nTotal = 0;

	while (nTotal < sizeof(zBuf) - 1) {
	    DWORD nRead = 0;

	    if (!ReadFile(h, &zBuf[nTotal], 1, &nRead, NULL) || nRead == 0) {
		break; /* EOF or error */
	    }
	    nTotal++;
	    if (zBuf[nTotal - 1] == '\n') {
		break;
	    }
	}

	if (nTotal == 0) {
	    TH8_TRACE_ERR(NULL, "no bytes read");
	    *pzOut = 0;
	    *pnOut = 0;
	    return TH8_ERROR;
	}
	zBuf[nTotal] = '\0';

	zResult = (char *)TH8_ALLOC_STR(interp, (size_t)nTotal);
	if (!zResult) {
	    TH8_TRACE_ERR(NULL, "out of memory");
	    *pzOut = 0;
	    *pnOut = 0;
	    return TH8_ERROR;
	}
	memcpy(zResult, zBuf, (size_t)nTotal + 1);
	*pzOut = zResult;
	*pnOut = (size_t)nTotal;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Output --
 *
 *	Write UTF-8 data to stdout or a redirected output channel.
 *	Implements the Th8_Platform xOutput callback.
 *
 * Why / How:
 *	Delegates to th8Win32WriteHandle, which handles console vs.
 *	pipe detection and UTF-8 to UTF-16 conversion automatically.
 *	If pChannel is non-NULL it is used as the output handle;
 *	otherwise STD_OUTPUT_HANDLE is used.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	Writes data to the output handle.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32Output(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx,  /* Platform's pCtx (unused). */
    const char *z,
    size_t n,
    void *pChannel)
{
    HANDLE h;

    (void)interp;
    (void)pCtx;

    h = pChannel ? (HANDLE)pChannel : GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
	TH8_TRACE_ERR(NULL, "GetStdHandle failed");
	return TH8_ERROR;
    }
    return th8Win32WriteHandle(interp, h, z, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32OutputError --
 *
 *	Write UTF-8 data to stderr or a redirected error channel.
 *	Implements the Th8_Platform xOutputError callback.
 *
 * Why / How:
 *	Identical to th8Win32Output except it defaults to
 *	STD_ERROR_HANDLE instead of STD_OUTPUT_HANDLE.  Delegates
 *	to th8Win32WriteHandle for console/pipe detection and
 *	UTF-8 to UTF-16 conversion.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	Writes data to the error output handle.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32OutputError(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx,  /* Platform's pCtx (unused). */
    const char *z,
    size_t n,
    void *pChannel)
{
    HANDLE h;

    (void)interp;
    (void)pCtx;

    h = pChannel ? (HANDLE)pChannel : GetStdHandle(STD_ERROR_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
	TH8_TRACE_ERR(NULL, "GetStdHandle failed");
	return TH8_ERROR;
    }
    return th8Win32WriteHandle(interp, h, z, n);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32FillPrng --
 *
 *	Fill a buffer with pseudo-random bytes using a linear
 *	congruential generator.  Seeded from GetTickCount +
 *	GetCurrentProcessId on first call.  Not cryptographic,
 *	but produces non-zero bytes suitable for hash seeding
 *	when RtlGenRandom is unavailable.
 *
 * Why / How:
 *	This is a last-resort fallback for th8Win32RandomBytes when
 *	advapi32.dll or SystemFunction036 (RtlGenRandom) cannot be
 *	loaded.  The LCG constants (1103515245, 12345) are the
 *	classic glibc values.  Bits 16..23 of the state are extracted
 *	for each output byte (the middle bits of an LCG have the
 *	longest period).  The seed combines GetTickCount (time) with
 *	GetCurrentProcessId (process) to reduce predictability across
 *	runs, though this is NOT suitable for cryptographic use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes nByte pseudo-random bytes into pBuf.  Updates the
 *	static seed state.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32FillPrng(void *pBuf, size_t nByte)
{
    static unsigned long seed = 0;
    static int seeded = 0;
    unsigned char *p = (unsigned char *)pBuf;
    size_t i;

    if (!seeded) {
	seed = (unsigned long)GetTickCount() ^
	       ((unsigned long)GetCurrentProcessId() << 16);
	if (seed == 0) seed = 1;
	seeded = 1;
    }

    for (i = 0; i < nByte; i++) {
	seed = seed * 1103515245 + 12345;
	p[i] = (unsigned char)(seed >> 16);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Memset --
 *
 *	Platform xMemset callback.  When the fill byte is zero,
 *	uses SecureZeroMemory (which is guaranteed not to be
 *	optimized away by the compiler).  Non-zero fills use
 *	a plain byte loop.
 *
 * Why / How:
 *	Standard memset with c==0 can be optimized away by the
 *	compiler if the buffer is not subsequently read (dead store
 *	elimination).  This is dangerous for security-sensitive data
 *	(keys, passwords, intermediate crypto state).
 *	SecureZeroMemory is a Win32 intrinsic guaranteed to survive
 *	optimization, so all zero-fills are routed through it.
 *	Non-zero fills use a byte loop (the platform layer avoids
 *	depending on the CRT memset).
 *
 * Results:
 *	Returns dst.
 *
 * Side effects:
 *	Writes n bytes into dst.
 *
 *----------------------------------------------------------------------
 */

static void *
th8Win32Memset(Th8_Interp *interp, void *pCtx, void *dst, int c, size_t n)
{
    (void)interp;
    (void)pCtx;
    if (c == 0) {
	SecureZeroMemory(dst, n);
    } else {
	unsigned char *p = (unsigned char *)dst;

	while (n--)
	    *p++ = (unsigned char)c;
    }
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32RandomBytes --
 *
 *	Fill a buffer with cryptographically secure random bytes.
 *	Implements the Th8_Platform xRandomBytes callback.
 *
 * Why / How:
 *	Dynamically loads advapi32.dll and resolves SystemFunction036
 *	(the documented alias for RtlGenRandom).  Dynamic loading is
 *	used instead of import-linking because advapi32.dll may not
 *	be available on all minimal Windows environments (e.g.,
 *	Windows PE).  If the function is available and succeeds, the
 *	buffer is filled with OS-level entropy.  If it fails or
 *	cannot be loaded, th8Win32FillPrng provides a non-zero
 *	pseudo-random fallback suitable for hash seeding but NOT for
 *	cryptographic key generation.  FreeLibrary is called after
 *	each use to avoid pinning the DLL handle.
 *
 * Results:
 *	TH8_OK (always succeeds via the PRNG fallback).
 *
 * Side effects:
 *	Loads and unloads advapi32.dll.  Writes nByte random bytes
 *	into pBuf.
 *
 *----------------------------------------------------------------------
 */

typedef BOOLEAN(WINAPI *RtlGenRandomFunc)(PVOID, ULONG);

static int
th8Win32RandomBytes(Th8_Interp *interp, void *pCtx, void *pBuf, size_t nByte)
{
    HMODULE hLib;
    RtlGenRandomFunc pFunc;

    (void)interp;
    (void)pCtx;

    hLib = LoadLibraryA("advapi32.dll");
    if (hLib) {
	pFunc = (RtlGenRandomFunc)
	    GetProcAddress(hLib, "SystemFunction036");  /* RtlGenRandom */
	if (pFunc) {
	    if (pFunc(pBuf, (ULONG)nByte)) {
		FreeLibrary(hLib);
		return TH8_OK;
	    }
	    TH8_TRACE_ERR(NULL, "RtlGenRandom failed");
	}
	FreeLibrary(hLib);
    }
    /*
     * Fallback: PRNG fill (non-zero, better than memset).
     */

    th8Win32FillPrng(pBuf, nByte);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32TimeMs --
 *
 *	Return the current wall-clock time as milliseconds since the
 *	Unix epoch (1970-01-01 00:00:00 UTC).  Implements the
 *	Th8_Platform xTimeMs callback.
 *
 * Why / How:
 *	GetSystemTimeAsFileTime returns the time as 100-nanosecond
 *	intervals since the Windows epoch (1601-01-01).  The result
 *	is divided by 10000 to convert to milliseconds, then the
 *	Windows-to-Unix epoch offset (11644473600000 ms) is subtracted
 *	to produce a Unix timestamp.  This function is NOT monotonic;
 *	the value can jump backward if the system clock is adjusted.
 *	For monotonic timing, use th8Win32TimeUs instead.
 *
 * Results:
 *	TH8_OK with *pMs set to the current time in milliseconds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32TimeMs(Th8_Interp *interp, void *pCtx, th8_int64_t *pMs)
{
    FILETIME ft;
    ULARGE_INTEGER ul;

    (void)interp;
    (void)pCtx;

    GetSystemTimeAsFileTime(&ft);
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;

    /*
     * FILETIME is 100-ns intervals since 1601-01-01.
     * Unix epoch is 1970-01-01 = 11644473600 seconds later.
     */

    *pMs = (th8_int64_t)((ul.QuadPart / 10000) - 11644473600000LL);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32TimeUs --
 *
 *	Return a monotonic timestamp in microseconds via
 *	QueryPerformanceCounter.  Implements the Th8_Platform
 *	xTimeUs callback.
 *
 * Why / How:
 *	QPC is monotonic and typically has sub-microsecond resolution
 *	on modern Windows (100ns on most hardware).  The conversion
 *	to microseconds splits the counter into whole-seconds and
 *	remainder to avoid overflow when multiplying by 1000000: the
 *	whole-seconds part is multiplied directly, and the remainder
 *	is multiplied then divided by the frequency.  This preserves
 *	precision without requiring 128-bit arithmetic.  Returns
 *	TH8_ERROR if QPC is unavailable (very old hardware).
 *
 * Results:
 *	TH8_OK with *pUs set; TH8_ERROR if QPC is unavailable.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32TimeUs(Th8_Interp *interp, void *pCtx, th8_int64_t *pUs)
{
    LARGE_INTEGER freq, count;

    (void)interp;
    (void)pCtx;

    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0 ||
        !QueryPerformanceCounter(&count)) {
	*pUs = 0;
	return TH8_ERROR;
    }

    /*
     * Convert to microseconds: count * 1000000 / freq.
     * To avoid overflow on the multiply, split into
     * whole-seconds and remainder.
     */

    {
	th8_int64_t sec = count.QuadPart / freq.QuadPart;
	th8_int64_t rem = count.QuadPart % freq.QuadPart;

	*pUs = sec * 1000000 + (rem * 1000000) / freq.QuadPart;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Sleep --
 *
 *	Suspend the calling thread for the specified number of
 *	milliseconds.  Implements the Th8_Platform xSleep callback.
 *
 * Why / How:
 *	Uses SleepEx with bAlertable=TRUE so that queued APCs and
 *	I/O completion routines can wake the thread early.  This is
 *	preferable to Sleep(nMs) because it allows the thread to
 *	respond to cancellation or I/O completion without waiting
 *	for the full interval.  Negative values are ignored (a
 *	negative sleep is meaningless).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Blocks the calling thread for up to nMs milliseconds.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32Sleep(Th8_Interp *interp, void *pCtx, int nMs)
{
    (void)interp;
    (void)pCtx;

    if (nMs >= 0) {
	SleepEx((DWORD)nMs, TRUE);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetPid --
 *
 *	Return the current process ID.  Implements the Th8_Platform
 *	xGetPid callback.
 *
 * Why / How:
 *	Wraps GetCurrentProcessId, which returns a DWORD.  The value
 *	is cast to int for the platform callback signature.  Win32
 *	process IDs are always positive and fit in a 32-bit int.
 *
 * Results:
 *	The current process ID as an int.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetPid(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    return (int)GetCurrentProcessId();
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetParentPid --
 *
 *	Return the parent process ID.  Implements the Th8_Platform
 *	xGetParentPid callback.
 *
 * Why / How:
 *	Win32 has no documented public API for retrieving the parent
 *	process ID without NtQueryInformationProcess (ntdll), which
 *	is an internal API subject to change.  Rather than depending
 *	on an undocumented interface, this stub returns 0 to indicate
 *	"not available".  Callers must tolerate a zero return.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetParentPid(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetThreadId --
 *
 *	Return the current thread ID.  Implements the Th8_Platform
 *	xGetThreadId callback.
 *
 * Why / How:
 *	Wraps GetCurrentThreadId, which returns a DWORD.  The value
 *	is widened to th8_uint64_t for the platform callback signature,
 *	which must accommodate POSIX pthread_t values that can be
 *	64-bit on some platforms.
 *
 * Results:
 *	The current thread ID as a th8_uint64_t.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8Win32GetThreadId(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    return (th8_uint64_t)GetCurrentThreadId();
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetStackBounds --
 *
 *	Determine the base address and size of the current thread's
 *	stack.  Implements the Th8_Platform xGetStackBounds callback.
 *
 * Why / How:
 *	Uses VirtualQuery on the address of a stack-local variable to
 *	find the memory region containing the current stack frame.
 *	AllocationBase gives the bottom (lowest address) of the stack
 *	reservation.  The function then walks upward through
 *	contiguous regions with the same AllocationBase to find the
 *	top of the stack (the highest committed or reserved address).
 *	On Windows the stack grows downward, so the "base" returned
 *	to TH8 is the high address (where the stack starts) and the
 *	"size" is the full reservation (high - low).  The interpreter
 *	uses these values for stack-depth checking to prevent
 *	unbounded recursion from crashing the process.
 *
 * Results:
 *	TH8_OK with *ppBase and *pnSize set; TH8_ERROR if
 *	VirtualQuery fails.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetStackBounds(
    Th8_Interp *interp,
    void *pCtx,
    void **ppBase,
    size_t *pnSize)
{
    MEMORY_BASIC_INFORMATION mbi;
    volatile char marker;
    char *pStackLow;
    char *pStackHigh;

    (void)interp;
    (void)pCtx;

    /*
     * VirtualQuery returns information about the committed
     * region at the given address.  AllocationBase is the
     * bottom of the stack reservation (lowest address).
     */

    if (VirtualQuery((LPCVOID)&marker, &mbi, sizeof(mbi)) == 0) {
	return TH8_ERROR;
    }
    pStackLow = (char *)mbi.AllocationBase;

    /*
     * Walk upward from the current region to find the top
     * of the stack.  The stack reservation is contiguous:
     * committed regions followed by a reserved (guard) region.
     * The top of the last region in this allocation is the
     * stack ceiling.
     */

    pStackHigh = (char *)mbi.BaseAddress + mbi.RegionSize;
    while (VirtualQuery((LPCVOID)pStackHigh, &mbi, sizeof(mbi)) != 0) {
	if (mbi.AllocationBase != (PVOID)pStackLow) {
	    break;  /* Different allocation -- done. */
	}
	pStackHigh = (char *)mbi.BaseAddress + mbi.RegionSize;
    }

    /*
     * The stack grows downward on Windows.  The "base" for
     * TH8's stack checking is the high address (where the
     * stack starts).  The size is the full reservation.
     */

    *ppBase = (void *)pStackHigh;
    *pnSize = (size_t)(pStackHigh - pStackLow);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32Panic --
 *
 *	Emit a fatal error message and terminate the process.
 *	Implements the Th8_Platform xPanic callback.
 *
 * Why / How:
 *	Three output channels are used to maximize visibility of the
 *	panic message: (1) WriteFile to stderr (raw bytes, no console
 *	conversion -- in a panic, simplicity trumps Unicode
 *	correctness), (2) OutputDebugStringA for the Windows debugger
 *	(visible in Visual Studio or DbgView), and (3) DebugBreak to
 *	trigger a breakpoint if a debugger is attached.  ExitProcess(3)
 *	terminates the process with a non-zero exit code, bypassing
 *	CRT cleanup (atexit handlers, global destructors) to avoid
 *	cascading failures.
 *
 * Results:
 *	Does not return.
 *
 * Side effects:
 *	Terminates the process.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32Panic(Th8_Interp *interp, void *pCtx, const char *zMsg, size_t nMsg)
{
    HANDLE h;
    DWORD nWritten;

    (void)interp;
    (void)pCtx;

    h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != INVALID_HANDLE_VALUE && zMsg && nMsg > 0) {
	WriteFile(h, zMsg, (DWORD)nMsg, &nWritten, NULL);
	WriteFile(h, "\r\n", 2, &nWritten, NULL);
    }
    OutputDebugStringA(zMsg);
    DebugBreak();
    ExitProcess(3);
}


/*
 *----------------------------------------------------------------------
 *
 * Win32 mutex callbacks (CRITICAL_SECTION based).
 *
 *	These are the xMutexInit / xMutexFinal / xMutexEnter /
 *	xMutexLeave platform callbacks.  They initialize, destroy,
 *	enter, and leave a caller-provided Th8_Mutex (which is a
 *	CRITICAL_SECTION on Win32) in-place.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8Win32MutexInit --
 *
 *	Initialize a caller-provided Th8_Mutex (which maps to a
 *	CRITICAL_SECTION on Win32).  Implements the Th8_Platform
 *	xMutexInit callback.
 *
 * Why / How:
 *	These per-interpreter mutexes are separate from the
 *	process-global CRITICAL_SECTION used by th8Win32Lock/Unlock.
 *	The Th8_Mutex struct is large enough to hold a
 *	CRITICAL_SECTION; this function initializes it in-place via
 *	InitializeCriticalSection.  CRITICAL_SECTION is a user-mode
 *	synchronization primitive that avoids kernel transitions for
 *	uncontended locks, making it faster than a Win32 Mutex object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the CRITICAL_SECTION in pMutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32MutexInit(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    InitializeCriticalSection((LPCRITICAL_SECTION)pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32MutexFinal --
 *
 *	Destroy a Th8_Mutex (CRITICAL_SECTION) previously initialized
 *	by th8Win32MutexInit.  Implements the Th8_Platform
 *	xMutexFinal callback.
 *
 * Why / How:
 *	Calls DeleteCriticalSection to release the OS resources
 *	associated with the CRITICAL_SECTION.  The mutex must not
 *	be held by any thread when this is called.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The CRITICAL_SECTION in pMutex is no longer usable.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32MutexFinal(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    DeleteCriticalSection((LPCRITICAL_SECTION)pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32MutexEnter --
 *
 *	Acquire the Th8_Mutex (CRITICAL_SECTION).  Implements the
 *	Th8_Platform xMutexEnter callback.
 *
 * Why / How:
 *	EnterCriticalSection blocks until the calling thread acquires
 *	exclusive ownership.  CRITICAL_SECTION is recursive: the same
 *	thread can enter it multiple times without deadlocking, which
 *	matches the Th8_Mutex contract.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Blocks until the mutex is acquired.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32MutexEnter(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    EnterCriticalSection((LPCRITICAL_SECTION)pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32MutexLeave --
 *
 *	Release the Th8_Mutex (CRITICAL_SECTION).  Implements the
 *	Th8_Platform xMutexLeave callback.
 *
 * Why / How:
 *	LeaveCriticalSection releases one level of ownership.  If the
 *	thread entered the section recursively, it must leave the same
 *	number of times before other threads can acquire it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases the mutex; may unblock a waiting thread.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32MutexLeave(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    LeaveCriticalSection((LPCRITICAL_SECTION)pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32IntCmpXchg --
 *
 *	Atomic compare-and-swap on an integer.  Implements the
 *	Th8_Platform xIntCmpXchg callback.
 *
 * Why / How:
 *	Wraps InterlockedCompareExchange, which atomically compares
 *	*pTarget with iComparand and, if equal, replaces it with
 *	iExchange.  Returns the original value of *pTarget before
 *	the operation (regardless of whether the swap occurred).
 *	This is the building block for lock-free initialization
 *	patterns (e.g., the CAS-based lazy init used by
 *	th8Win32Initialize).
 *
 * Results:
 *	The previous value of *pTarget.
 *
 * Side effects:
 *	May atomically modify *pTarget.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32IntCmpXchg(
    Th8_Interp *interp,
    void *pCtx,
    volatile int *pTarget,
    int iExchange,
    int iComparand)
{
    (void)interp;
    (void)pCtx;
    return (int)InterlockedCompareExchange(
        (volatile LONG *)pTarget, (LONG)iExchange, (LONG)iComparand);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32IntCmpXchg64 --
 *
 *	64-bit atomic compare-and-swap.  Implements the Th8_Platform
 *	xIntCmpXchg64 callback.
 *
 * Why / How:
 *	The 64-bit sibling of th8Win32IntCmpXchg.  Wraps the Win32
 *	InterlockedCompareExchange64 intrinsic, which is wide enough to
 *	hold a thread id (the 32-bit xIntCmpXchg is not).  Used to read
 *	and publish the interpreter's owning-thread id.  Returns the
 *	original value of *pTarget before the operation.
 *
 * Results:
 *	The previous value of *pTarget.
 *
 * Side effects:
 *	May atomically modify *pTarget.  Implies a full memory barrier.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8Win32IntCmpXchg64(
    Th8_Interp *interp,
    void *pCtx,
    volatile th8_uint64_t *pTarget,
    th8_uint64_t iExchange,
    th8_uint64_t iComparand)
{
    (void)interp;
    (void)pCtx;
    return (th8_uint64_t)InterlockedCompareExchange64(
        (volatile LONG64 *)pTarget, (LONG64)iExchange, (LONG64)iComparand);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32MemBarrier --
 *
 *	Issue a full memory barrier (fence).  Implements the
 *	Th8_Platform xMemBarrier callback.
 *
 * Why / How:
 *	Wraps the Win32 MemoryBarrier() intrinsic, which issues a
 *	hardware memory fence preventing the CPU from reordering
 *	loads and stores across the barrier.  This is needed after
 *	lock-free writes to ensure that data written before the
 *	barrier is visible to other threads reading after it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Issues a hardware memory fence.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32MemBarrier(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
    MemoryBarrier();
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32EventCreate --
 *
 *	Create a Win32 manual-reset event handle for the
 *	per-interp event queue.  Mirror of
 *	`th8PosixEventCreate`.  Wraps `CreateEventW` with:
 *	  *  manual-reset = TRUE (signaled state persists
 *	     until an explicit `ResetEvent`, so concurrent
 *	     producers can signal-coalesce safely without
 *	     losing wakeups),
 *	  *  initial state = FALSE (unsignaled),
 *	  *  no security attributes / name.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- platform context (ignored).
 *
 * Returns:
 *	Non-NULL HANDLE on success; NULL on failure
 *	(`GetLastError` available to the caller).
 *
 * Side effects:
 *	Allocates a Win32 kernel-object handle.
 *
 *----------------------------------------------------------------------
 */
static void *
th8Win32EventCreate(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
    return (void *)CreateEventW(NULL, TRUE, FALSE, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32EventDestroy --
 *
 *	Close a Win32 manual-reset event handle returned by
 *	`th8Win32EventCreate`.  NULL `pEvent` is a no-op.
 *	Mirror of `th8PosixEventDestroy`.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- platform context (ignored).
 *	pEvent -- HANDLE returned by `th8Win32EventCreate`, or
 *		NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Closes the Win32 handle via `CloseHandle`.
 *
 *----------------------------------------------------------------------
 */
static void
th8Win32EventDestroy(Th8_Interp *interp, void *pCtx, void *pEvent)
{
    (void)interp;
    (void)pCtx;
    if (pEvent) {
	CloseHandle((HANDLE)pEvent);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32EventSet --
 *
 *	Transition the manual-reset event to the signaled
 *	state via `SetEvent`.  Idempotent.  NULL `pEvent` is a
 *	no-op.  Mirror of `th8PosixEventSet`.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- platform context (ignored).
 *	pEvent -- HANDLE, or NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Wakes every waiter blocked on the event.
 *
 *----------------------------------------------------------------------
 */
static void
th8Win32EventSet(Th8_Interp *interp, void *pCtx, void *pEvent)
{
    (void)interp;
    (void)pCtx;
    if (pEvent) {
	SetEvent((HANDLE)pEvent);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32EventReset --
 *
 *	Transition the manual-reset event back to the
 *	unsignaled state via `ResetEvent`.  Manual-reset
 *	events do not auto-reset on `Wait`; callers must
 *	explicitly reset them when one-shot semantics are
 *	desired.  NULL `pEvent` is a no-op.  Mirror of
 *	`th8PosixEventReset`.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- platform context (ignored).
 *	pEvent -- HANDLE, or NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	None visible to other threads (the new unsignaled
 *	state takes effect for subsequent `Wait` calls).
 *
 *----------------------------------------------------------------------
 */
static void
th8Win32EventReset(Th8_Interp *interp, void *pCtx, void *pEvent)
{
    (void)interp;
    (void)pCtx;
    if (pEvent) {
	ResetEvent((HANDLE)pEvent);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32EventWait --
 *
 *	Block via `WaitForSingleObjectEx` until the event is
 *	signaled or the timeout elapses.  The wait is
 *	**alertable** (`bAlertable = TRUE`) so a
 *	`QueueUserAPC` posted to the calling thread fires
 *	even while `[vwait]` is sleeping; in that case
 *	`th8Win32EventWait` returns `-1` so the caller re-polls
 *	(the APC body has already run).  Mirror of
 *	`th8PosixEventWait` (which uses `pthread_kill`
 *	signal-interruption to achieve the same effect).
 *
 *	Timeout encoding: negative means `INFINITE`; zero is a
 *	poll; positive is the millisecond bound.  NULL
 *	`pEvent` reports timeout.  `WAIT_FAILED` is treated as
 *	timeout (1) to preserve the calling-thread invariants
 *	the dispatcher expects on error paths.
 *
 * Parameters:
 *	interp     -- ignored.
 *	pCtx       -- platform context (ignored).
 *	pEvent     -- HANDLE, or NULL.
 *	nTimeoutMs -- timeout in milliseconds; negative for
 *		infinite, zero for poll, positive for bounded.
 *
 * Returns:
 *	0  -- event was signaled before the timeout.
 *	1  -- timeout reached, or `WAIT_FAILED`, or NULL event.
 *	-1 -- alertable wake (`WAIT_IO_COMPLETION`); APC ran.
 *
 * Side effects:
 *	May block the calling thread.  May dispatch one or
 *	more APCs via the alertable wait.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32EventWait(
    Th8_Interp *interp,
    void *pCtx,
    void *pEvent,
    int nTimeoutMs)
{
    DWORD ms;
    DWORD rc;

    (void)interp;
    (void)pCtx;
    if (!pEvent) return 1;
    ms = (nTimeoutMs < 0) ? INFINITE : (DWORD)nTimeoutMs;
    rc = WaitForSingleObjectEx((HANDLE)pEvent, ms, TRUE);
    switch (rc) {
    case WAIT_OBJECT_0:
	return 0;
    case WAIT_TIMEOUT:
	return 1;
    case WAIT_IO_COMPLETION:
	return -1;
    default:
	return 1; /* WAIT_FAILED -> treat as timeout */
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32EmitTrace --
 *
 *	Emit a diagnostic trace message via OutputDebugStringA.
 *	Implements the Th8_Platform xEmitTrace callback.
 *
 * Why / How:
 *	OutputDebugStringA sends the message to the Windows debug
 *	output stream, which can be captured by a debugger (Visual
 *	Studio, WinDbg) or by Sysinternals DbgView.  This provides
 *	a non-intrusive diagnostic channel that works even when
 *	stdout and stderr are unavailable or redirected.  NULL
 *	messages are silently ignored.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the debug output stream.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32EmitTrace(Th8_Interp *interp, void *pCtx, const char *zMsg)
{
    (void)interp;
    (void)pCtx;
    if (zMsg) {
	OutputDebugStringA(zMsg);
    }
}


typedef USHORT(
    WINAPI *RtlCaptureStackBackTraceFunc)(ULONG, ULONG, PVOID *, PULONG);

/*
 *----------------------------------------------------------------------
 *
 * th8Win32StackBackTrace --
 *
 *	Implements the Th8_Platform.xStackBackTrace callback on Windows:
 *	capture up to nMaxFrames return-address program counters into
 *	apFrames[], skipping the innermost nSkip frames.  Returns the
 *	number captured.
 *
 * Why / How:
 *	Uses RtlCaptureStackBackTrace (exported by name from kernel32.dll,
 *	forwarded to ntdll; available since Windows XP), the Windows
 *	analogue of the th8_unwind layer's _Unwind_Backtrace.  A native
 *	Win32 body is required because under MSVC the compiler unwind
 *	runtime that th8_unwind relies on is absent, so the merged Win32
 *	platform would otherwise inherit th8_unwind's no-op.  Win32 is
 *	merged (with the OS layers) BEFORE th8_unwind in
 *	Th8_UseDefaultPlatform, and MERGE_SLOT only fills a NULL slot, so
 *	this native entry wins over the compiler-runtime fallback.
 *
 *	The symbol is resolved dynamically (mirroring th8Win32RandomBytes'
 *	handling of RtlGenRandom) because RtlCaptureStackBackTrace is
 *	inconsistently prototyped across SDKs / MinGW headers.  Unlike the
 *	RtlGenRandom path this caches the resolved pointer in function-local
 *	statics and uses GetModuleHandleA rather than LoadLibrary/FreeLibrary:
 *	this callback runs on EVERY tracked allocation, so per-call loader
 *	work would be ruinous, and -- critically -- it must not allocate or
 *	take the loader lock repeatedly while executing inside the allocation
 *	tracker.  kernel32.dll is always resident, so GetModuleHandleA neither
 *	loads nor ref-counts it.  The one-time resolution races benignly
 *	across threads (idempotent pointer-sized writes of the same value);
 *	bResolved is latched so a failed lookup is not retried every call.
 *	The capture itself walks the stack without any debug-help library and
 *	allocates nothing.  Pre-Vista releases cap FramesToSkip +
 *	FramesToCapture at 62, so the request is clamped to that ceiling.
 *
 * Results:
 *	Number of frames stored (0 on bad arguments, if the symbol cannot be
 *	resolved, or if the skip count alone reaches the 62-frame ceiling).
 *
 * Side effects:
 *	Populates and latches function-local static caches on first call.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32StackBackTrace(
    Th8_Interp *interp,
    void *pCtx,
    void **apFrames,
    int nMaxFrames,
    int nSkip)
{
    static RtlCaptureStackBackTraceFunc pFunc = NULL;
    static int bResolved = 0;
    ULONG nSkipFrames;
    ULONG nCapFrames;
    USHORT nCaptured;

    (void)interp;
    (void)pCtx;
    if (apFrames == NULL || nMaxFrames <= 0) {
	return 0;
    }
    if (!bResolved) {
	HMODULE hK32 = GetModuleHandleA("kernel32.dll");
	if (hK32) {
	    pFunc = (RtlCaptureStackBackTraceFunc)
	        GetProcAddress(hK32, "RtlCaptureStackBackTrace");
	}
	bResolved = 1; /* Latch: do not re-probe on every allocation. */
    }
    if (pFunc == NULL) {
	return 0;
    }
    nSkipFrames = (nSkip < 0) ? 0 : (ULONG)nSkip;
    nCapFrames = (ULONG)nMaxFrames;

    /*
     * RtlCaptureStackBackTrace on pre-Vista releases limits the sum of
     * FramesToSkip and FramesToCapture to 62; clamp so the call stays
     * within that ceiling on every supported Windows version.
     */
    if (nSkipFrames >= 62) {
	return 0;
    }
    if (nSkipFrames + nCapFrames > 62) {
	nCapFrames = 62 - nSkipFrames;
    }
    nCaptured = pFunc(nSkipFrames, nCapFrames, apFrames, NULL);
    return (int)nCaptured;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetBasePath --
 *
 *	Explicitly set the base path for the TH8 platform layer.
 *	This overrides the automatic GetModuleHandleA-based detection.
 *	Must be called BEFORE Th8_Initialize.  The path is copied
 *	internally.  Pass "." to use the current working directory.
 *
 * Why / How:
 *	The embedder calls this to anchor the filesystem sandbox at
 *	a known location rather than relying on auto-detection from
 *	the DLL path.  The special value "." is resolved to the
 *	current working directory via GetCurrentDirectoryA.  All
 *	other paths are resolved via th8Win32ResolvePath to their
 *	canonical form (expanding 8.3 names, resolving symlinks).
 *	If resolution fails, the raw path is used as a fallback.
 *	The result is stored in the static zBasePath buffer under
 *	the process-global lock, and bInitialized is set to prevent
 *	th8Win32GetBasePath from overwriting it later.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the path is NULL or too long.
 *
 * Side effects:
 *	Sets the static zBasePath buffer and bInitialized flag.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetBasePath(const char *zPath, size_t nPath)
{
    char resolved[MAX_PATH];

    if (!zPath) return TH8_ERROR;
    if (nPath == TH8_NOLEN) nPath = Th8_Strlen(NULL, zPath);
    if (nPath >= sizeof(resolved)) return TH8_ERROR;

    /*
     * Copy to a local buffer for NUL termination, then
     * resolve via GetFullPathNameA to get the canonical path.
     */

    memcpy(resolved, zPath, nPath);
    resolved[nPath] = '\0';

    th8Win32Lock();

    /*
     * If ".", resolve to CWD.
     */

    if (nPath == 1 && zPath[0] == '.') {
	DWORD n = GetCurrentDirectoryA(sizeof(zBasePath), zBasePath);

	if (n == 0 || n >= sizeof(zBasePath)) {
	    th8Win32Unlock();
	    return TH8_ERROR;
	}
    } else {
	if (th8Win32ResolvePath(resolved, zBasePath, sizeof(zBasePath)) !=
	    TH8_OK) {
	    /*
	     * Resolve failed -- use the raw path as fallback.
	     */

	    if (nPath >= sizeof(zBasePath)) {
		th8Win32Unlock();
		return TH8_ERROR;
	    }
	    memcpy(zBasePath, resolved, nPath);
	    zBasePath[nPath] = '\0';
	}
    }
    bInitialized = 1;

    th8Win32Unlock();
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetBasePath --
 *
 *	Return the current base path, triggering auto-detection if
 *	it has not yet been set.
 *
 * Why / How:
 *	Calls th8Win32GetBasePath to ensure the base path has been
 *	computed (either via Th8_SetBasePath or auto-detection from
 *	the DLL location).  Returns the static buffer contents, or
 *	NULL if the base path could not be determined.  The returned
 *	pointer is valid for the lifetime of the process (the buffer
 *	is static).
 *
 * Results:
 *	Pointer to the NUL-terminated base path, or NULL.
 *
 * Side effects:
 *	May trigger base-path auto-detection on first call.
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_GetBasePath(void)
{
    (void)th8Win32GetBasePath();
    return zBasePath[0] ? zBasePath : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetCwd --
 *
 *	Return the current working directory relative to the base
 *	path.  Implements the Th8_Platform xGetCwd callback.
 *
 * Why / How:
 *	Scripts must never see absolute filesystem paths (they could
 *	leak information about the host or be used to craft path
 *	traversal attacks).  This function gets the real CWD via
 *	GetCurrentDirectoryA, resolves it through th8Win32ResolvePath
 *	to normalize 8.3 names and symlinks (matching how the base
 *	path was resolved), then checks it against the base path via
 *	th8Win32IsUnderBase.  If the CWD is the base directory, "."
 *	is returned; if underneath, "./relative" is returned with
 *	backslashes normalized to forward slashes for cross-platform
 *	consistency.  If the CWD is outside the sandbox, NULL is
 *	returned (foreign directory).  The result is allocated via
 *	Th8_AttemptMalloc; the caller frees it.
 *
 * Results:
 *	Allocated string with base-relative path, or NULL on error.
 *
 * Side effects:
 *	Allocates memory; caller must free.
 *
 *----------------------------------------------------------------------
 */

static char *
th8Win32GetCwd(
    Th8_Interp *interp, /* Interpreter (for Th8_AttemptMalloc). */
    void *pCtx)   /* Host context (unused). */
{
    const char *zBase;
    size_t nBase;
    char zCwd[MAX_PATH];
    DWORD n;
    int rel;

    (void)pCtx;

    zBase = th8Win32GetBasePath();
    if (!zBase) return NULL;
    nBase = Th8_Strlen(interp, zBase);

    /*
     * Get CWD and resolve it through th8Win32ResolvePath
     * to normalize 8.3 names and (on Vista+) symlinks,
     * matching how the base path was resolved.
     */

    {
	char zRaw[MAX_PATH];

	n = GetCurrentDirectoryA(sizeof(zRaw), zRaw);
	if (n == 0 || n >= sizeof(zRaw)) {
	    TH8_TRACE_ERR(NULL, "GetCurrentDirectoryA failed");
	    return NULL;
	}

	if (th8Win32ResolvePath(zRaw, zCwd, sizeof(zCwd)) != TH8_OK) {
	    /*
	     * Cannot resolve; use raw CWD.
	     */

	    if (n >= sizeof(zCwd)) return NULL;
	    memcpy(zCwd, zRaw, n + 1);
	}
    }

    rel = th8Win32IsUnderBase(zCwd, zBase, nBase);

    if (rel == 0) {
	char *z = (char *)TH8_ALLOC(interp, 2);

	if (z) {
	    z[0] = '.';
	    z[1] = '\0';
	}
	return z;
    } else if (rel == 1) {
	const char *zTail = zCwd + nBase + 1;
	size_t nTail = Th8_Strlen(interp, zTail);
	size_t i;
	char *z = (char *)TH8_ALLOC_STR_ADD(interp, 2, nTail);

	if (z) {
	    z[0] = '.';
	    z[1] = '/';
	    memcpy(z + 2, zTail, nTail);
	    z[2 + nTail] = '\0';

	    /*
	     * Normalize backslashes to forward slashes.
	     */

	    for (i = 2; i < 2 + nTail; i++) {
		if (z[i] == '\\') z[i] = '/';
	    }
	}
	return z;
    }

    TH8_TRACE_ERR(NULL, "current path is foreign");
    TH8_TRACE_ERR(NULL, zCwd);
    return NULL;  /* foreign directory */
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32NormalizePath --
 *
 *	Return the normalized, base-relative form of a path.
 *	Implements the Th8_Platform xNormalizePath callback.
 *
 * Why / How:
 *	Resolves the input path to its canonical absolute form via
 *	th8Win32ResolvePath (expanding 8.3 names, resolving symlinks
 *	on Vista+).  If the resolved path is at the base directory,
 *	"." is returned; if underneath, "./relative" is returned with
 *	backslashes normalized to forward slashes.  If the path is
 *	outside the sandbox, NULL is returned rather than leaking the
 *	absolute path to scripts.  If resolution fails, a verbatim
 *	copy of the input is returned (best effort).  When no base
 *	path is configured, the full resolved path is returned.
 *
 * Results:
 *	Allocated string, or NULL if the path is outside the sandbox.
 *
 * Side effects:
 *	Allocates memory; caller must free.
 *
 *----------------------------------------------------------------------
 */

static char *
th8Win32NormalizePath(
    Th8_Interp *interp, /* Interpreter (for Th8_AttemptMalloc). */
    void *pCtx,   /* Host context (unused). */
    const char *zPath,  /* Path to normalize. */
    size_t nPath)  /* Length, or (size_t)-1. */
{
    const char *zBase;
    size_t nBase;
    char zCopy[MAX_PATH];
    char zFull[MAX_PATH];
    DWORD n;
    int rel;

    (void)pCtx;
    if (!zPath) return NULL;

    if (nPath == (size_t)-1) nPath = Th8_Strlen(interp, zPath);
    if (nPath >= sizeof(zCopy)) return NULL;
    memcpy(zCopy, zPath, nPath);
    zCopy[nPath] = '\0';

    /*
     * Resolve to a fully canonical path.
     */

    if (th8Win32ResolvePath(zCopy, zFull, sizeof(zFull)) != TH8_OK) {
	/*
	 * Cannot resolve; return a copy verbatim.
	 */

	char *z = (char *)TH8_ALLOC_STR(interp, nPath);

	if (z) {
	    memcpy(z, zPath, nPath);
	    z[nPath] = '\0';
	}
	return z;
    }
    n = (DWORD)Th8_Strlen(interp, zFull);

    zBase = th8Win32GetBasePath();
    if (!zBase) {
	/*
	 * No base path; return the full path.
	 */

	char *z = (char *)TH8_ALLOC_STR(interp, n);

	if (z) {
	    memcpy(z, zFull, n + 1);
	}
	return z;
    }
    nBase = Th8_Strlen(interp, zBase);

    rel = th8Win32IsUnderBase(zFull, zBase, nBase);

    if (rel == 0) {
	char *z = (char *)TH8_ALLOC(interp, 2);

	if (z) {
	    z[0] = '.';
	    z[1] = '\0';
	}
	return z;
    } else if (rel == 1) {
	const char *zTail = zFull + nBase + 1;
	size_t nTail = Th8_Strlen(interp, zTail);
	size_t i;
	char *z = (char *)TH8_ALLOC_STR_ADD(interp, 2, nTail);

	if (z) {
	    z[0] = '.';
	    z[1] = '/';
	    memcpy(z + 2, zTail, nTail);
	    z[2 + nTail] = '\0';

	    for (i = 2; i < 2 + nTail; i++) {
		if (z[i] == '\\') z[i] = '/';
	    }
	}
	return z;
    }

    /*
     * Outside the base directory.  Return NULL -- scripts
     * must not see paths outside the sandbox.
     */

    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32SetCwd --
 *
 *	Change the current working directory.  Implements the
 *	Th8_Platform xSetCwd callback.
 *
 * Why / How:
 *	Accepts "." (which always means the base directory) or any
 *	relative/absolute path that, after full resolution, resides
 *	at or underneath the base directory.  All other paths are
 *	rejected (the script cannot chdir out of the sandbox).  The
 *	path is resolved via th8Win32ResolvePath and checked against
 *	the base path via th8Win32IsUnderBase.  If the path does not
 *	exist and cannot be resolved, it is rejected conservatively
 *	(fail-closed).  The actual directory change is performed via
 *	SetCurrentDirectoryA on the resolved canonical path.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure or rejection.
 *
 * Side effects:
 *	Changes the process-wide current working directory.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32SetCwd(
    Th8_Interp *interp, /* Interpreter. */
    void *pCtx,   /* Host context (unused). */
    const char *zPath,
    size_t nPath)
{
    const char *zBase;
    size_t nBase;
    char zCopy[MAX_PATH];
    char zFull[MAX_PATH];
    int rel;

    (void)pCtx;
    (void)interp;

    if (!zPath) {
	TH8_TRACE_ERR(NULL, "invalid new path");
	return TH8_ERROR;
    }
    if (nPath == (size_t)-1) nPath = Th8_Strlen(interp, zPath);

    zBase = th8Win32GetBasePath();
    if (!zBase) {
	TH8_TRACE_ERR(NULL, "invalid base path");
	return TH8_ERROR;
    }
    nBase = Th8_Strlen(interp, zBase);

    /*
     * Shortcut: "." always means the base directory.
     */

    if (nPath == 1 && zPath[0] == '.') {
	if (SetCurrentDirectoryA(zBase)) return TH8_OK;
	TH8_TRACE_ERR(NULL, "SetCurrentDirectoryA failed");
	return TH8_ERROR;
    }

    /*
     * Resolve the target path to its canonical form.
     * If resolution fails (path doesn't exist and can't be
     * resolved), reject it -- we cannot verify it is under
     * the base directory without full resolution.
     */

    if (nPath >= sizeof(zCopy)) {
	TH8_TRACE_ERR(NULL, "new path is too long");
	return TH8_ERROR;
    }
    memcpy(zCopy, zPath, nPath);
    zCopy[nPath] = '\0';

    if (th8Win32ResolvePath(zCopy, zFull, sizeof(zFull)) != TH8_OK) {
	TH8_TRACE_ERR(NULL, "could not resolve new path");
	return TH8_ERROR;
    }

    rel = th8Win32IsUnderBase(zFull, zBase, nBase);

    if (rel >= 0) {
	if (SetCurrentDirectoryA(zFull)) return TH8_OK;
	TH8_TRACE_ERR(NULL, "SetCurrentDirectoryA failed");
	return TH8_ERROR;
    }

    TH8_TRACE_ERR(NULL, "new path is foreign");
    TH8_TRACE_ERR(NULL, zFull);
    return TH8_ERROR;  /* rejected: foreign directory */
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetExePath --
 *
 *	Return the path to the running executable, relative to the
 *	base path.  Implements the Th8_Platform xGetExePath callback.
 *
 * Why / How:
 *	Uses GetModuleFileNameA(NULL, ...) to get the raw executable
 *	path, then resolves it via th8Win32ResolvePath for canonical
 *	form (expanding 8.3 names, resolving symlinks on Vista+).
 *	The resolved path is then checked against the base path: if
 *	the executable is at the base, "." is returned; if underneath,
 *	"./relative" is returned with backslashes normalized to
 *	forward slashes.  If the executable is outside the sandbox,
 *	NULL is returned rather than leaking the absolute path.  When
 *	no base path is configured (legacy/embedded mode), the full
 *	absolute path is returned.  The result is allocated via
 *	Th8_AttemptMalloc; the caller frees it.
 *
 * Results:
 *	Allocated base-relative path, or NULL if outside the sandbox.
 *
 * Side effects:
 *	Allocates memory; caller must free.
 *
 *----------------------------------------------------------------------
 */

static char *
th8Win32GetExePath(
    Th8_Interp *interp, /* Interpreter for Th8_AttemptMalloc. */
    void *pCtx)   /* Platform context (unused). */
{
    char zModule[MAX_PATH];
    char zResolved[MAX_PATH];
    DWORD n;
    size_t nLen;
    char *zResult;

    (void)pCtx;

    n = GetModuleFileNameA(NULL, zModule, sizeof(zModule));
    if (n == 0 || n >= sizeof(zModule)) {
	return 0;
    }

    /*
     * Resolve to a fully canonical path (expands 8.3 names,
     * resolves symlinks/junctions on Vista+).
     */
    if (th8Win32ResolvePath(zModule, zResolved, sizeof(zResolved)) !=
        TH8_OK) {
	/*
	 * Fallback: use the raw GetModuleFileName result.
	 */
	memcpy(zResolved, zModule, n + 1);
    }

    /*
     * Convert to base-relative form.  Scripts must not see
     * absolute filesystem paths.
     */
    {
	const char *zBase = th8Win32GetBasePath();

	if (zBase) {
	    size_t nBase = Th8_Strlen(interp, zBase);
	    int rel = th8Win32IsUnderBase(zResolved, zBase, nBase);

	    if (rel == 0) {
		zResult = (char *)TH8_ALLOC(interp, 2);
		if (zResult) {
		    zResult[0] = '.';
		    zResult[1] = '\0';
		}
		return zResult;
	    } else if (rel == 1) {
		const char *zTail = zResolved + nBase + 1;
		size_t nTail = Th8_Strlen(interp, zTail);
		size_t i;

		zResult = (char *)TH8_ALLOC_STR_ADD(interp, 2, nTail);
		if (zResult) {
		    zResult[0] = '.';
		    zResult[1] = '/';
		    memcpy(zResult + 2, zTail, nTail);
		    zResult[2 + nTail] = '\0';

		    for (i = 2; i < 2 + nTail; i++) {
			if (zResult[i] == '\\') zResult[i] = '/';
		    }
		}
		return zResult;
	    }

	    /*
	     * Executable is outside the base directory.
	     * Return NULL rather than leaking the absolute path.
	     */
	    return 0;
	}

	/* No base path configured -- return absolute (legacy). */
	nLen = Th8_Strlen(interp, zResolved);
	zResult = (char *)TH8_ALLOC_STR(interp, nLen);
	if (zResult) {
	    memcpy(zResult, zResolved, nLen + 1);
	}
	return zResult;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Win32Platform --
 *
 *	The complete platform table.
 *
 *----------------------------------------------------------------------
 */


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetUserName --
 *
 *	Return the current user name.  Implements the Th8_Platform
 *	xGetUserName callback.
 *
 * Why / How:
 *	Wraps the Win32 GetUserNameA function, which retrieves the
 *	name of the user associated with the current thread's access
 *	token.  If the call fails (e.g., insufficient buffer size),
 *	the buffer is set to an empty string and TH8_OK is still
 *	returned, treating an unavailable username as non-fatal.
 *
 * Results:
 *	TH8_OK with zBuf filled; always succeeds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetUserName(Th8_Interp *interp, void *pCtx, char *zBuf, size_t nBuf)
{
    DWORD nUser = (DWORD)nBuf;

    (void)interp;
    (void)pCtx;
    if (GetUserNameA(zBuf, &nUser)) {
	return TH8_OK;
    }
    zBuf[0] = 0;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetHostName --
 *
 *	Return the local machine (NetBIOS) name.  Implements the
 *	Th8_Platform xGetHostName callback.
 *
 * Why / How:
 *	Wraps GetComputerNameA, which retrieves the NetBIOS name of
 *	the local computer.  If the call fails, the buffer is set to
 *	an empty string and TH8_OK is returned (non-fatal).
 *
 * Results:
 *	TH8_OK with zBuf filled; always succeeds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetHostName(Th8_Interp *interp, void *pCtx, char *zBuf, size_t nBuf)
{
    DWORD nHost = (DWORD)nBuf;

    (void)interp;
    (void)pCtx;
    if (GetComputerNameA(zBuf, &nHost)) {
	return TH8_OK;
    }
    zBuf[0] = 0;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetEnv --
 *
 *	Return the value of an environment variable as an allocated
 *	UTF-8 string.  Implements the Th8_Platform xGetEnv callback.
 *
 * Why / How:
 *	Uses the wide (UTF-16) Win32 APIs to correctly handle
 *	environment variable names and values that contain non-ASCII
 *	characters.  The name is converted from UTF-8 to UTF-16 via
 *	MultiByteToWideChar, then GetEnvironmentVariableW retrieves
 *	the value in UTF-16, and finally WideCharToMultiByte converts
 *	the result back to UTF-8.  When called without an interpreter
 *	(interp == NULL), the function accesses the global platform's
 *	xMalloc/xFree under the global mutex for memory allocation.
 *	Returns NULL if the variable is not set, the name conversion
 *	fails, or the value is too long for the stack buffer.
 *
 * Results:
 *	Allocated UTF-8 string (caller frees), or NULL if not found.
 *
 * Side effects:
 *	Allocates memory; caller must free.
 *
 *----------------------------------------------------------------------
 */

static char *
th8Win32GetEnv(Th8_Interp *interp, void *pCtx, const char *zName)
{
    WCHAR wName[256];
    WCHAR wVal[4096];
    DWORD n;
    int nWide;
    int nUtf8;
    char *zResult;

    (void)pCtx;

    if (!zName) return NULL;

    /*
     * Convert the variable name from UTF-8 to UTF-16.
     */
    nWide = MultiByteToWideChar(
        CP_UTF8, 0, zName, -1, wName, sizeof(wName) / sizeof(WCHAR));
    if (nWide == 0) return NULL;

    /*
     * Query the environment variable (UTF-16).
     */
    n = GetEnvironmentVariableW(wName, wVal, sizeof(wVal) / sizeof(WCHAR));
    if (n == 0) return NULL;  /* not set or error */
    if (n >= sizeof(wVal) / sizeof(WCHAR)) return NULL;  /* too long */

    /*
     * Convert the UTF-16 value to UTF-8.
     */
    nUtf8 = WideCharToMultiByte(CP_UTF8, 0, wVal, -1, NULL, 0, NULL, NULL);
    if (nUtf8 <= 0) return NULL;

    if (interp) {
	zResult = (char *)TH8_ALLOC(interp, (size_t)nUtf8);
    } else {
	void *(*xMal)(Th8_Interp *, void *, size_t) = NULL;
	void *pMalCtx = NULL;

	th8MaybeGlobalMutexEnter(NULL);
	if (th8GlobalPlatform.xMalloc) {
	    xMal = th8GlobalPlatform.xMalloc;
	    pMalCtx = th8GlobalPlatform.pCtx;
	}
	th8MaybeGlobalMutexLeave(NULL);

	if (!xMal) return NULL;
	zResult = (char *)xMal(NULL, pMalCtx, (size_t)nUtf8);
    }
    if (!zResult) return NULL;

    if (WideCharToMultiByte(
            CP_UTF8, 0, wVal, -1, zResult, nUtf8, NULL, NULL) != nUtf8) {
	if (interp) {
	    Th8_Free(interp, zResult);
	} else {
	    void (*xFr)(Th8_Interp *, void *, void *) = NULL;
	    void *pFrCtx = NULL;

	    th8MaybeGlobalMutexEnter(NULL);
	    if (th8GlobalPlatform.xFree) {
		xFr = th8GlobalPlatform.xFree;
		pFrCtx = th8GlobalPlatform.pCtx;
	    }
	    th8MaybeGlobalMutexLeave(NULL);
	    if (xFr) xFr(NULL, pFrCtx, zResult);
	}
	return NULL;
    }
    return zResult;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetLastError --
 *
 *	Return the Win32 thread-local error code.  Implements the
 *	Th8_Platform xGetLastError callback.
 *
 * Why / How:
 *	Wraps GetLastError, which returns the DWORD error code set
 *	by the most recent Win32 API call on this thread.  The value
 *	is cast to int for the platform callback signature.  Scripts
 *	use this to retrieve OS-level error details after a failed
 *	platform call.
 *
 * Results:
 *	The current Win32 error code as an int.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetLastError(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
    return (int)GetLastError();
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32SetLastError --
 *
 *	Set the Win32 thread-local error code.  Implements the
 *	Th8_Platform xSetLastError callback.
 *
 * Why / How:
 *	Wraps SetLastError, which sets the DWORD error code for
 *	the current thread.  This is used to clear or set a known
 *	error code before a Win32 API call, so that the subsequent
 *	GetLastError result is unambiguous.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the thread-local Win32 error code.
 *
 *----------------------------------------------------------------------
 */

static void
th8Win32SetLastError(Th8_Interp *interp, void *pCtx, int nErr)
{
    (void)interp;
    (void)pCtx;
    SetLastError((DWORD)nErr);
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetTemporaryData --
 *
 *	Create a temporary file with the requested pre-allocated size
 *	and return its path and file handle.  Implements the
 *	Th8_Platform xGetTemporaryData callback.
 *
 * Why / How:
 *	Uses GetTempPathA + GetTempFileNameA to generate a unique
 *	temporary filename in the system temp directory, then opens
 *	it via CreateFileA with FILE_ATTRIBUTE_TEMPORARY (hint to the
 *	cache manager to keep data in memory) and
 *	FILE_FLAG_DELETE_ON_CLOSE (auto-delete when the handle is
 *	closed).  The file is pre-allocated to nSize bytes via
 *	SetFilePointerEx + SetEndOfFile to avoid fragmentation during
 *	subsequent writes, then sought back to the beginning.  The
 *	caller receives the file path (allocated string), its length,
 *	and the HANDLE as an opaque void* channel.
 *
 * Results:
 *	TH8_OK with *pzOut, *pnOut, *ppChannel set; TH8_ERROR on
 *	failure.
 *
 * Side effects:
 *	Creates a temporary file on disk.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetTemporaryData(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppChannel)
{
    char zTempDir[MAX_PATH];
    char zTempPath[MAX_PATH];
    HANDLE hFile;
    char *zPath;
    size_t nPath;
    LARGE_INTEGER li;

    (void)pCtx;

    if (!GetTempPathA(sizeof(zTempDir), zTempDir)) {
	TH8_TRACE_ERR(NULL, "GetTempPathA failed");
	return TH8_ERROR;
    }
    if (!GetTempFileNameA(zTempDir, "th8", 0, zTempPath)) {
	TH8_TRACE_ERR(NULL, "GetTempFileNameA failed");
	return TH8_ERROR;
    }

    hFile = CreateFileA(
        zTempPath, GENERIC_READ | GENERIC_WRITE, 0, /* no sharing */
        NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
	TH8_TRACE_ERR(NULL, "CreateFileA failed for temp file");
	TH8_TRACE_ERR(NULL, zTempPath);
	DeleteFileA(zTempPath);
	return TH8_ERROR;
    }

    li.QuadPart = (LONGLONG)nSize;
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) ||
        !SetEndOfFile(hFile)) {
	TH8_TRACE_ERR(NULL, "temp file pre-allocation failed");
	CloseHandle(hFile);
	return TH8_ERROR;
    }

    li.QuadPart = 0;
    SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);

    nPath = Th8_Strlen(interp, zTempPath);
    zPath = (char *)TH8_ALLOC_STR(interp, nPath);
    if (!zPath) {
	TH8_TRACE_ERR(NULL, "out of memory");
	CloseHandle(hFile);
	return TH8_ERROR;
    }
    memcpy(zPath, zTempPath, nPath + 1);

    *pzOut = zPath;
    *pnOut = nPath;
    *ppChannel = (void *)hFile;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32DeleteTemporaryData --
 *
 *	Delete a temporary file by path.  Implements the
 *	Th8_Platform xDeleteTemporaryData callback.
 *
 * Why / How:
 *	NUL-terminates the path into a stack buffer and calls
 *	DeleteFileA.  This is the explicit cleanup path; the file
 *	may already have been deleted by FILE_FLAG_DELETE_ON_CLOSE
 *	when the handle was closed, in which case DeleteFileA fails
 *	harmlessly (the error is traced but not propagated).
 *
 * Results:
 *	TH8_OK always (best-effort deletion).
 *
 * Side effects:
 *	Deletes the file from disk (if it still exists).
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32DeleteTemporaryData(
    Th8_Interp *interp,
    void *pCtx,
    const char *zPath,
    size_t nPath)
{
    char zBuf[MAX_PATH];

    (void)interp;
    (void)pCtx;

    if (!zPath || nPath == 0 || nPath >= sizeof(zBuf)) {
	return TH8_ERROR;
    }
    memcpy(zBuf, zPath, nPath);
    zBuf[nPath] = '\0';
    if (!DeleteFileA(zBuf)) {
	TH8_TRACE_ERR(NULL, "DeleteFileA failed in cleanup");
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32ChannelControl --
 *
 *	Perform seek, tell, flush, close, read, write, or open
 *	operations on a Win32 HANDLE channel.  Implements the
 *	Th8_Platform xChannelControl callback.
 *
 * Why / How:
 *	A single dispatch function handles all channel operations via
 *	the op parameter (TH8_CHANCTL_SEEK, _TELL, _FLUSH, _CLOSE,
 *	_WRITE, _READ, _OPEN).  Seek uses SetFilePointerEx with
 *	FILE_BEGIN/FILE_CURRENT/FILE_END.  Tell queries the current
 *	position via SetFilePointerEx with a zero displacement.
 *	Flush calls FlushFileBuffers.  Close calls CloseHandle.
 *	Write loops WriteFile until all bytes are written (handling
 *	partial writes).  Read calls ReadFile once.  Open uses
 *	CreateFileA with access and creation flags determined by
 *	nArg2 (1 = write/create, else read/existing).
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.  For tell, read,
 *	and write, *pnResult receives the result value.
 *
 * Side effects:
 *	Varies by operation (I/O, file creation, handle closing).
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32ChannelControl(
    Th8_Interp *interp,
    void *pCtx,
    void *pChannel,
    int op,
    th8_int64_t nArg1,
    int nArg2,
    th8_int64_t *pnResult,
    void *pBuf)
{
    HANDLE hFile = (HANDLE)pChannel;
    LARGE_INTEGER li;

    (void)interp;
    (void)pCtx;
    if (hFile == INVALID_HANDLE_VALUE) {
	TH8_TRACE_ERR(NULL, "invalid file handle");
	return TH8_ERROR;
    }

    switch (op) {
    case TH8_CHANCTL_SEEK: {
	DWORD method = FILE_BEGIN;

	if (nArg2 == 1)
	    method = FILE_CURRENT;
	else if (nArg2 == 2)
	    method = FILE_END;
	li.QuadPart = (LONGLONG)nArg1;
	if (!SetFilePointerEx(hFile, li, NULL, method)) {
	    TH8_TRACE_ERR(NULL, "SetFilePointerEx failed in seek");
	    return TH8_ERROR;
	}
    }
	return TH8_OK;

    case TH8_CHANCTL_TELL: {
	LARGE_INTEGER pos;

	li.QuadPart = 0;
	if (!SetFilePointerEx(hFile, li, &pos, FILE_CURRENT)) {
	    TH8_TRACE_ERR(NULL, "SetFilePointerEx failed in tell");
	    if (pnResult) *pnResult = -1;
	    return TH8_ERROR;
	}
	if (pnResult) *pnResult = (th8_int64_t)pos.QuadPart;
    }
	return TH8_OK;

    case TH8_CHANCTL_FLUSH:
	FlushFileBuffers(hFile);
	return TH8_OK;

    case TH8_CHANCTL_CLOSE:
	CloseHandle(hFile);
	return TH8_OK;

    case TH8_CHANCTL_WRITE: {
	DWORD nToWrite = (DWORD)nArg1;
	DWORD nWritten = 0;
	const char *p = (const char *)pBuf;

	while (nToWrite > 0) {
	    DWORD n = 0;

	    if (!WriteFile(hFile, p, nToWrite, &n, NULL)) {
		TH8_TRACE_ERR(NULL, "WriteFile failed in channel write");
		return TH8_ERROR;
	    }
	    p += n;
	    nToWrite -= n;
	    nWritten += n;
	}
	if (pnResult) *pnResult = (th8_int64_t)nWritten;
    }
	return TH8_OK;

    case TH8_CHANCTL_READ: {
	DWORD nToRead = (DWORD)nArg1;
	DWORD nRead = 0;

	if (!ReadFile(hFile, pBuf, nToRead, &nRead, NULL)) {
	    TH8_TRACE_ERR(NULL, "ReadFile failed in channel read");
	    if (pnResult) *pnResult = 0;
	    return TH8_ERROR;
	}
	if (pnResult) *pnResult = (th8_int64_t)nRead;
    }
	return TH8_OK;

    case TH8_CHANCTL_OPEN: {
	char zPath[MAX_PATH];
	DWORD access;
	DWORD creation;
	HANDLE hNew;

	if (!pBuf || nArg1 <= 0 || (size_t)nArg1 >= sizeof(zPath)) {
	    return TH8_ERROR;
	}
	memcpy(zPath, pBuf, (size_t)nArg1);
	zPath[(size_t)nArg1] = '\0';

	if (nArg2 == 1) {
	    access = GENERIC_WRITE;
	    creation = CREATE_ALWAYS;
	} else {
	    access = GENERIC_READ;
	    creation = OPEN_EXISTING;
	}
	hNew = CreateFileA(
	    zPath, access, 0, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hNew == INVALID_HANDLE_VALUE) {
	    TH8_TRACE_ERR(NULL, "CreateFileA failed in channel open");
	    return TH8_ERROR;
	}
	if (pnResult) {
	    *pnResult = (th8_int64_t)(intptr_t)hNew;
	}
    }
	return TH8_OK;
    }
    TH8_TRACE_ERR(NULL, "unrecognized channel control");
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * I/O channel redirection.
 *
 *	Static per-process channel overrides.  NULL means "use the
 *	default" (STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE).
 *	For per-interpreter channels, the embedder should provide
 *	custom callbacks with a pCtx struct.
 *
 *----------------------------------------------------------------------
 */

static void *th8Win32InputChannel = 0;
static void *th8Win32OutputChannel = 0;
static void *th8Win32ErrorChannel = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetInput --
 *
 *	Return the current input channel override.  Implements the
 *	Th8_Platform xGetInput callback.
 *
 * Why / How:
 *	Returns the static per-process input channel pointer.  NULL
 *	means "use the default STD_INPUT_HANDLE"; a non-NULL value
 *	is an opaque HANDLE set by th8Win32SetInput.  The xInput
 *	callback uses this value as its pChannel parameter.
 *
 * Results:
 *	0 (TH8_OK) with *pChannel set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetInput(Th8_Interp *interp, void *pCtx, void **pChannel)
{
    (void)interp;
    (void)pCtx;
    *pChannel = th8Win32InputChannel;
    return 0;  /* TH8_OK */
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32SetInput --
 *
 *	Set the input channel override.  Implements the Th8_Platform
 *	xSetInput callback.
 *
 * Why / How:
 *	Stores an opaque HANDLE in the static per-process input
 *	channel variable.  Subsequent th8Win32Input calls will use
 *	this handle instead of STD_INPUT_HANDLE.  Pass NULL to
 *	restore the default.
 *
 * Results:
 *	0 (TH8_OK).
 *
 * Side effects:
 *	Sets the static input channel pointer.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32SetInput(Th8_Interp *interp, void *pCtx, void *pChannel)
{
    (void)interp;
    (void)pCtx;
    th8Win32InputChannel = pChannel;
    return 0;  /* TH8_OK */
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetOutput --
 *
 *	Return the current output channel override.  Implements the
 *	Th8_Platform xGetOutput callback.
 *
 * Why / How:
 *	Returns the static per-process output channel pointer.  NULL
 *	means "use the default STD_OUTPUT_HANDLE".
 *
 * Results:
 *	0 (TH8_OK) with *pChannel set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetOutput(Th8_Interp *interp, void *pCtx, void **pChannel)
{
    (void)interp;
    (void)pCtx;
    *pChannel = th8Win32OutputChannel;
    return 0;  /* TH8_OK */
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32SetOutput --
 *
 *	Set the output channel override.  Implements the Th8_Platform
 *	xSetOutput callback.
 *
 * Why / How:
 *	Stores an opaque HANDLE in the static per-process output
 *	channel variable.  Subsequent th8Win32Output calls will use
 *	this handle instead of STD_OUTPUT_HANDLE.  Pass NULL to
 *	restore the default.
 *
 * Results:
 *	0 (TH8_OK).
 *
 * Side effects:
 *	Sets the static output channel pointer.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32SetOutput(Th8_Interp *interp, void *pCtx, void *pChannel)
{
    (void)interp;
    (void)pCtx;
    th8Win32OutputChannel = pChannel;
    return 0;  /* TH8_OK */
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetErrorOutput --
 *
 *	Return the current error output channel override.  Implements
 *	the Th8_Platform xGetErrorOutput callback.
 *
 * Why / How:
 *	Returns the static per-process error channel pointer.  NULL
 *	means "use the default STD_ERROR_HANDLE".
 *
 * Results:
 *	0 (TH8_OK) with *pChannel set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetErrorOutput(Th8_Interp *interp, void *pCtx, void **pChannel)
{
    (void)interp;
    (void)pCtx;
    *pChannel = th8Win32ErrorChannel;
    return 0;  /* TH8_OK */
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32SetErrorOutput --
 *
 *	Set the error output channel override.  Implements the
 *	Th8_Platform xSetErrorOutput callback.
 *
 * Why / How:
 *	Stores an opaque HANDLE in the static per-process error
 *	channel variable.  Subsequent th8Win32OutputError calls will
 *	use this handle instead of STD_ERROR_HANDLE.  Pass NULL to
 *	restore the default.
 *
 * Results:
 *	0 (TH8_OK).
 *
 * Side effects:
 *	Sets the static error channel pointer.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32SetErrorOutput(Th8_Interp *interp, void *pCtx, void *pChannel)
{
    (void)interp;
    (void)pCtx;
    th8Win32ErrorChannel = pChannel;
    return 0;  /* TH8_OK */
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetRealPath --
 *
 *	Resolve a path to its absolute form.  Implements the
 *	Th8_Platform xGetRealPath callback.
 *
 * Why / How:
 *	NUL-terminates the input path into a stack buffer, then calls
 *	GetFullPathNameA to resolve "." and ".." components and
 *	produce an absolute path.  Unlike th8Win32ResolvePath, this
 *	does NOT expand 8.3 names or resolve symlinks -- it provides
 *	the minimal "real path" needed for path comparisons.  The
 *	result is written into the caller-provided zBuf.
 *
 * Results:
 *	TH8_OK on success with the absolute path in zBuf;
 *	TH8_ERROR on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetRealPath(
    Th8_Interp *interp,
    void *pCtx,
    const char *zPath,
    size_t nPath,
    char *zBuf,
    size_t nBuf)
{
    char zCopy[4096];
    DWORD n;

    (void)interp;
    (void)pCtx;

    if (!zPath || !zBuf || nBuf == 0) return TH8_ERROR;
    if (nPath == (size_t)-1) nPath = Th8_Strlen(NULL, zPath);
    if (nPath >= sizeof(zCopy)) return TH8_ERROR;

    memcpy(zCopy, zPath, nPath);
    zCopy[nPath] = '\0';

    n = GetFullPathNameA(zCopy, (DWORD)nBuf, zBuf, NULL);
    if (n == 0 || n >= (DWORD)nBuf) return TH8_ERROR;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetRootPath --
 *
 *	Return the logical root for a given path.  Implements the
 *	Th8_Platform xGetRootPath callback.
 *
 * Why / How:
 *	In the sandboxed model, the "root" is not the volume root
 *	(e.g., "C:\") but the base directory.  This function checks
 *	whether the path resides under the base directory via
 *	th8Win32IsPathUnderBase; if so, "." is returned (the base
 *	directory IS the root from the script's perspective).  If
 *	the path is outside the sandbox, an empty string is returned.
 *	This prevents scripts from discovering the true volume root,
 *	which would leak filesystem layout information.
 *
 * Results:
 *	TH8_OK with "." in zBuf (under base) or "" (outside).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32GetRootPath(
    Th8_Interp *interp,
    void *pCtx,
    const char *zPath,
    size_t nPath,
    char *zBuf,
    size_t nBuf)
{
    char zCopy[4096];

    (void)interp;
    (void)pCtx;

    if (!zBuf || nBuf < 2) return TH8_ERROR;
    if (!zPath) {
	zBuf[0] = '\0';
	return TH8_OK;
    }
    if (nPath == (size_t)-1) nPath = Th8_Strlen(NULL, zPath);
    if (nPath >= sizeof(zCopy)) {
	zBuf[0] = '\0';
	return TH8_OK;
    }

    memcpy(zCopy, zPath, nPath);
    zCopy[nPath] = '\0';

    /*
     * Normalize the path and check whether it resides under
     * the base directory.  Return "." if it does, or "" if
     * the path is outside the sandbox.
     */
    if (th8Win32IsPathUnderBase(zCopy)) {
	zBuf[0] = '.';
	zBuf[1] = '\0';
    } else {
	zBuf[0] = '\0';
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Win32SameFile --
 *
 *	Test whether two paths refer to the same physical file.
 *	Implements the Th8_Platform xSameFile callback.
 *
 * Why / How:
 *	Opens both files with zero access rights (no read/write
 *	needed, just metadata access) and FILE_FLAG_BACKUP_SEMANTICS
 *	(required for directories).  GetFileInformationByHandle
 *	returns the volume serial number and 64-bit file index for
 *	each file; if all three values match, the paths refer to the
 *	same physical file (regardless of hard links, different path
 *	formats, or case differences).  Both paths are validated
 *	against the base-path sandbox; paths outside the sandbox are
 *	treated as non-matching (returning 0).  This is the Win32
 *	equivalent of comparing st_dev and st_ino from POSIX stat(2).
 *
 * Results:
 *	Non-zero if both paths refer to the same file, zero otherwise.
 *
 * Side effects:
 *	Briefly opens file handles (immediately closed).
 *
 *----------------------------------------------------------------------
 */

static int
th8Win32SameFile(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName1,
    size_t nName1,
    const char *zName2,
    size_t nName2)
{
    char zCopy1[4096];
    char zCopy2[4096];
    HANDLE h1;
    HANDLE h2;
    BY_HANDLE_FILE_INFORMATION info1;
    BY_HANDLE_FILE_INFORMATION info2;
    int result = 0;

    (void)interp;
    (void)pCtx;

    if (!zName1 || !zName2) return 0;
    if (nName1 == (size_t)-1) nName1 = Th8_Strlen(NULL, zName1);
    if (nName2 == (size_t)-1) nName2 = Th8_Strlen(NULL, zName2);
    if (nName1 >= sizeof(zCopy1)) return 0;
    if (nName2 >= sizeof(zCopy2)) return 0;

    memcpy(zCopy1, zName1, nName1);
    zCopy1[nName1] = '\0';
    memcpy(zCopy2, zName2, nName2);
    zCopy2[nName2] = '\0';

    /*
     * Both paths must be under the base directory.
     */
    if (!th8Win32IsPathUnderBase(zCopy1)) return 0;
    if (!th8Win32IsPathUnderBase(zCopy2)) return 0;

    h1 = CreateFileA(
        zCopy1, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h1 == INVALID_HANDLE_VALUE) return 0;

    h2 = CreateFileA(
        zCopy2, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h2 == INVALID_HANDLE_VALUE) {
	CloseHandle(h1);
	return 0;
    }

    if (GetFileInformationByHandle(h1, &info1) &&
        GetFileInformationByHandle(h2, &info2)) {
	result =
	    (info1.dwVolumeSerialNumber == info2.dwVolumeSerialNumber &&
	     info1.nFileIndexHigh == info2.nFileIndexHigh &&
	     info1.nFileIndexLow == info2.nFileIndexLow);
    } else {
	TH8_TRACE_ERR(NULL, "GetFileInformationByHandle failed");
    }

    CloseHandle(h1);
    CloseHandle(h2);
    return result;
}


#  if defined(TH8_ENABLE_UNBOUND)

/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetModuleAnchorPath --
 *
 *	Compose the path to a candidate trust anchor co-located
 *	with the currently-loaded TH8 module (`th8.dll`, or the
 *	host EXE for a statically-linked build): the directory
 *	containing the image, plus `\\root.key`.
 *
 *	Uses `GetModuleHandleExA` with
 *	`GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` on this
 *	function's own address so detection works regardless of
 *	how TH8 was linked -- conventionally-named DLL,
 *	custom-named DLL, or fully static EXE.
 *	`UNCHANGED_REFCOUNT` ensures the probe does not pin the
 *	module against unload.
 *
 *	Does NOT check whether the file actually exists --
 *	callers use the returned path with `ub_ctx_add_ta_file`
 *	or `th8Win32PathReadable` to determine that.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zBuf -- output buffer; receives the NUL-terminated path
 *		on success.
 *	nBuf -- size of `zBuf` in bytes.
 *
 * Returns:
 *	1 on success; 0 if `GetModuleHandleExA` failed,
 *	`GetModuleFileNameA` returned an error or full-buffer
 *	truncation indicator, the resolved path contained no
 *	separator, or the composed path would exceed `nBuf`.
 *
 * Side effects:
 *	On success, writes to `zBuf`.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32GetModuleAnchorPath(char *zBuf, size_t nBuf)
{
    HMODULE hMod = NULL;
    char zPath[MAX_PATH];
    DWORD n;
    char *slash;
    char *fslash;
    size_t nDir;
    static const char zAnchor[] = "\\root.key";

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)(uintptr_t)&th8Win32GetModuleAnchorPath, &hMod) ||
        !hMod) {
	return 0;
    }
    n = GetModuleFileNameA(hMod, zPath, sizeof(zPath));
    if (n == 0 || n >= sizeof(zPath)) return 0;
    slash = strrchr(zPath, '\\');
    fslash = strrchr(zPath, '/');
    if (fslash > slash) slash = fslash;
    if (!slash) return 0;
    nDir = (size_t)(slash - zPath);
    if (nDir + sizeof(zAnchor) > nBuf) return 0;
    memcpy(zBuf, zPath, nDir);
    memcpy(zBuf + nDir, zAnchor, sizeof(zAnchor));
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32PathReadable --
 *
 *	Predicate: does `zPath` name an existing file the
 *	calling process can open for reading?  Implemented via
 *	`GetFileAttributesA` -- cheaper than an actual `CreateFileA`
 *	probe and avoids the side-effect of bumping the file's
 *	last-access timestamp.
 *
 *	Returns 0 for directories, broken reparse points, or any
 *	other case where `GetFileAttributesA` reports
 *	`INVALID_FILE_ATTRIBUTES` or sets the `FILE_ATTRIBUTE_DIRECTORY`
 *	bit.  ACL-denied paths also return 0 (good -- the
 *	subsequent `CreateFileA` would have failed anyway).
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zPath -- NUL-terminated path to test.
 *
 * Returns:
 *	1 if `zPath` is an existing readable file; 0 otherwise.
 *
 * Side effects:
 *	One `GetFileAttributesA` syscall.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32PathReadable(const char *zPath)
{
    DWORD a = GetFileAttributesA(zPath);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    if (a & FILE_ATTRIBUTE_DIRECTORY) return 0;
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32TryComposeEnvPath --
 *
 *	Helper: look up `zEnvName` in the process environment;
 *	if set, append `zRelPath` (or use the value verbatim
 *	when `zRelPath` is NULL) and write the resulting path
 *	into `zBuf`.  Returns 1 on success.
 *
 *	Used by `th8Win32GetManagedAnchorPath` to compose
 *	candidate `%APPDATA%`-relative paths, and by
 *	`th8Win32FindStaticAnchorPath` (via wrapper) to compose
 *	candidate trust-anchor paths under the per-user /
 *	per-machine env-rooted directories.
 *
 *	The buffer-size check is the only failure source
 *	besides "env var not set" -- this helper does NOT
 *	check whether the resulting file exists; callers
 *	pair it with `th8Win32PathReadable`.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zBuf     -- output buffer.
 *	nBuf     -- size of `zBuf` in bytes.
 *	zEnvName -- environment-variable name to read.
 *	zRelPath -- relative-path suffix to append after the
 *		env-var value (e.g. `"\\Unbound\\root.key"`),
 *		or NULL to use the env-var value verbatim.
 *
 * Returns:
 *	1 on success; 0 if the env var is unset or empty, or
 *	the composed path would exceed `nBuf`.
 *
 * Side effects:
 *	On success, writes to `zBuf`.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32TryComposeEnvPath(
    char *zBuf,
    size_t nBuf,
    const char *zEnvName,
    const char *zRelPath)
{
    const char *zVal = getenv(zEnvName);
    size_t nVal;
    size_t nRel;

    if (!zVal || !*zVal) return 0;
    nVal = strlen(zVal);
    nRel = zRelPath ? strlen(zRelPath) : 0;
    if (nVal + nRel + 1 > nBuf) return 0;
    memcpy(zBuf, zVal, nVal);
    if (zRelPath) memcpy(zBuf + nVal, zRelPath, nRel);
    zBuf[nVal + nRel] = '\0';
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32FindStaticAnchorPath --
 *
 *	Locate a readable static IANA root-key file by walking
 *	the conventional Win32 install-location search list, in
 *	priority order:
 *	  1. The TH8 module directory (`th8.dll` neighbour).
 *	  2. `%PROGRAMDATA%\\Unbound\\root.key` (machine-wide).
 *	  3. `%APPDATA%\\Unbound\\root.key` (per-user roaming).
 *	  4. `%USERPROFILE%\\.unbound\\root.key`.
 *	  5. `C:\\Program Files\\Unbound\\root.key` (NLnet Labs
 *	     MSI default).
 *	  6. `C:\\ProgramData\\Unbound\\root.key` (literal
 *	     fallback when `PROGRAMDATA` is unset).
 *
 *	Each candidate is screened by `th8Win32PathReadable`
 *	before being returned, so the function never returns a
 *	path that the caller would fail to open.
 *
 *	The module-directory candidate wins over every
 *	system-wide / user-wide path because an anchor sitting
 *	next to the library was clearly intended for that
 *	library by whoever staged the install, versus a
 *	system-wide anchor that may have been installed by a
 *	different actor for a different purpose.
 *
 *	Used as the SOURCE for the bootstrap-then-auto-roll
 *	flow in `th8Win32SetupManagedAnchor`.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zBuf -- output buffer; receives the NUL-terminated
 *		anchor path on success.
 *	nBuf -- size of `zBuf` in bytes.
 *
 * Returns:
 *	1 if a readable static anchor was found; 0 if none of
 *	the candidates existed.
 *
 * Side effects:
 *	On success, writes to `zBuf`.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32FindStaticAnchorPath(char *zBuf, size_t nBuf)
{
    static const struct {
	const char *zEnv;
	const char *zRel;
    } aEnv[] = {
        {"PROGRAMDATA", "\\Unbound\\root.key"},
        {"APPDATA", "\\Unbound\\root.key"},
        {"USERPROFILE", "\\.unbound\\root.key"},
    };
    static const char *const azLiteral[] = {
        "C:\\Program Files\\Unbound\\root.key",
        "C:\\ProgramData\\Unbound\\root.key",
    };
    size_t i;

    if (th8Win32GetModuleAnchorPath(zBuf, nBuf) &&
        th8Win32PathReadable(zBuf)) {
	return 1;
    }
    for (i = 0; i < sizeof(aEnv) / sizeof(aEnv[0]); i++) {
	if (th8Win32TryComposeEnvPath(
	        zBuf, nBuf, aEnv[i].zEnv, aEnv[i].zRel) &&
	    th8Win32PathReadable(zBuf)) {
	    return 1;
	}
    }
    for (i = 0; i < sizeof(azLiteral) / sizeof(azLiteral[0]); i++) {
	size_t n = strlen(azLiteral[i]);
	if (n + 1 > nBuf) continue;
	if (!th8Win32PathReadable(azLiteral[i])) continue;
	memcpy(zBuf, azLiteral[i], n + 1);
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32GetManagedAnchorPath --
 *
 *	Compose the path to the per-user WRITABLE managed copy
 *	of the trust anchor.  This is the file libunbound's
 *	`ub_ctx_add_ta_autr` reads on every resolve and writes
 *	state-machine updates to as the RFC 5011 hold-down
 *	timer advances.
 *
 *	Directory selection follows the Windows per-user
 *	roaming-profile convention: `%APPDATA%\\TH8\\root.key`.
 *	`%APPDATA%` is set on every interactive logon (and on
 *	most service contexts via `RoamingAppData`) and its
 *	parent directory carries the default user-only ACL,
 *	so the managed copy is automatically protected from
 *	other local users.
 *
 *	If `%APPDATA%` is unset (typical only for stripped
 *	service contexts that don't load a user profile) the
 *	function fails -- there is no usable per-user location
 *	and the caller must fall back to static-anchor mode.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zBuf -- output buffer; receives the NUL-terminated path
 *		on success.
 *	nBuf -- size of `zBuf` in bytes.
 *
 * Returns:
 *	1 on success; 0 if `%APPDATA%` is unset or the composed
 *	path would exceed `nBuf`.
 *
 * Side effects:
 *	On success, writes to `zBuf`.  Does NOT create the
 *	directory (see `th8Win32EnsureParentDir`).
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32GetManagedAnchorPath(char *zBuf, size_t nBuf)
{
    return th8Win32TryComposeEnvPath(
        zBuf, nBuf, "APPDATA", "\\TH8\\root.key");
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32EnsureParentDir --
 *
 *	Ensure every parent directory of `zPath` exists.  Each
 *	intermediate component is created via `CreateDirectoryA`
 *	with `lpSecurityAttributes = NULL` so the new directory
 *	inherits the parent's ACL -- on the `%APPDATA%` chain
 *	this means the user-only ACL is propagated automatically
 *	and the managed copy is not exposed to other local users.
 *
 *	`ERROR_ALREADY_EXISTS` is benign (the directory exists);
 *	any other failure is fatal.
 *
 *	Equivalent to `mkdir -p $(dirname zPath)`.  Mutates a
 *	private copy of `zPath` rather than `zPath` itself so
 *	the caller's buffer is unchanged.
 *
 *	Accepts both `\\` and `/` as path separators so callers
 *	don't have to normalise.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zPath -- NUL-terminated path whose parent directories
 *		should be created.  The basename of `zPath` is
 *		ignored.
 *
 * Returns:
 *	1 on success; 0 on `CreateDirectoryA` failure other than
 *	`ERROR_ALREADY_EXISTS`, or on `zPath` being too long
 *	for the scratch buffer.
 *
 * Side effects:
 *	May create one or more directories on disk.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32EnsureParentDir(const char *zPath)
{
    char zScratch[MAX_PATH];
    size_t n = strlen(zPath);
    size_t i;

    if (n + 1 > sizeof(zScratch)) return 0;
    memcpy(zScratch, zPath, n + 1);
    /* Walk forward, creating each component up to (but not
     * including) the final basename.  Start at 3 so we skip
     * the drive prefix (`C:\\`); CreateDirectoryA on `C:` or
     * `C:\\` is always an error. */
    for (i = 3; i < n; i++) {
	char c = zScratch[i];
	if (c != '\\' && c != '/') continue;
	zScratch[i] = '\0';
	if (!CreateDirectoryA(zScratch, NULL) &&
	    GetLastError() != ERROR_ALREADY_EXISTS) {
	    return 0;
	}
	zScratch[i] = c;
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32CopyFileContents --
 *
 *	Copy the contents of `zSrc` to `zDst`.  Used by
 *	`th8Win32SetupManagedAnchor` to bootstrap the writable
 *	managed copy from a vendor-supplied static anchor on
 *	the first run after install.
 *
 *	Routed through `CopyFileA(..., FAIL_IF_EXISTS = FALSE)`,
 *	which is atomic relative to other readers (the
 *	destination becomes visible only after the full payload
 *	is written) and inherits the destination directory's
 *	default ACL.  Because the destination lives under
 *	`%APPDATA%\\TH8\\` (a user-only directory created via
 *	`th8Win32EnsureParentDir` with the user-only inherited
 *	ACL), the resulting `root.key` is automatically
 *	protected from other local users.
 *
 *	On failure the destination is `DeleteFileA`'d so a
 *	partial copy does not survive (libunbound would refuse
 *	to parse a truncated trust-anchor file and the next
 *	resolve would silently lose validation).
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zSrc -- NUL-terminated source path (readable).
 *	zDst -- NUL-terminated destination path (will be
 *		created or replaced).
 *
 * Returns:
 *	1 on success; 0 if `CopyFileA` failed.
 *
 * Side effects:
 *	Creates / overwrites `zDst`.  On failure, removes any
 *	partial `zDst`.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32CopyFileContents(const char *zSrc, const char *zDst)
{
    if (CopyFileA(zSrc, zDst, FALSE)) return 1;
    (void)DeleteFileA(zDst);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32UnboundOps --
 *
 *	Static `Th8_UnboundOps` instance handed to every call
 *	into the shared `th8_unbound.c` driver.  Populates the
 *	platform-operations vtable with the Win32-specific
 *	helpers defined above.
 *
 *	See `src/th8_unbound.h` for the per-field contract.
 *
 *----------------------------------------------------------------------
 */
static const Th8_UnboundOps th8Win32UnboundOps = {
    th8Win32FindStaticAnchorPath, th8Win32GetManagedAnchorPath,
    th8Win32PathReadable,         th8Win32EnsureParentDir,
    th8Win32CopyFileContents,
};

/*
 *----------------------------------------------------------------------
 *
 * th8Win32DnsResolve --
 *
 *	`Th8_Platform.xDnsResolve` callback.  Thin Win32-side
 *	adapter -- the entire libunbound integration
 *	(hardening, trust-anchor selection, RFC 5011 auto-roll
 *	lifecycle, synchronous resolve, result wrapping) lives
 *	in `src/th8_unbound.c`.  This function exists only to
 *	bind the platform-callback signature to the shared
 *	driver and to supply the Win32 `Th8_UnboundOps` vtable.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	interp   -- live interpreter (used for allocation in
 *		the shared driver).
 *	pCtx     -- unused platform context.
 *	zName    -- hostname.
 *	nName    -- hostname length.
 *	eType    -- DNS record type (libunbound
 *		`LDNS_RR_TYPE_*`).
 *	ppResult -- output: result pointer on success;
 *		NULL on failure.
 *
 * Returns:
 *	Whatever `th8UnboundResolve` returns: `TH8_OK` on
 *	success; `TH8_ERROR` on any failure.
 *
 * Side effects:
 *	See `th8UnboundResolve` -- network DNS query,
 *	allocations, possible first-run trust-anchor bootstrap.
 *
 *----------------------------------------------------------------------
 */
static int
th8Win32DnsResolve(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    int eType,
    Th8_DnsResult **ppResult)
{
    (void)pCtx;
    return th8UnboundResolve(
        interp, &th8Win32UnboundOps, zName, nName, eType, ppResult);
}

/*
 *----------------------------------------------------------------------
 *
 * th8Win32DnsResolveFree --
 *
 *	`Th8_Platform.xDnsResolveFree` callback.  Thin Win32-
 *	side adapter that forwards to the shared
 *	`th8UnboundResolveFree`.  The teardown logic does not
 *	depend on any Win32-specific behaviour, so no vtable
 *	is needed here.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	interp  -- live interpreter (used for `Th8_Free`).
 *	pCtx    -- unused platform context.
 *	pResult -- result returned by `th8Win32DnsResolve`,
 *		or NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	See `th8UnboundResolveFree` -- frees every resource
 *	owned by the result.
 *
 *----------------------------------------------------------------------
 */
static void
th8Win32DnsResolveFree(Th8_Interp *interp, void *pCtx, Th8_DnsResult *pResult)
{
    (void)pCtx;
    th8UnboundResolveFree(interp, pResult);
}


#  endif /* TH8_ENABLE_UNBOUND */


static Th8_Platform th8Win32PlatformData = {
    1,   /* nVersion */

    /* Lifecycle */
    th8Win32Initialize,  /* xInitialize */
    th8Win32Finalize,  /* xFinalize */
    th8Win32PreDeleteInterp, /* xPreDeleteInterp */
    th8Win32DeleteInterp, /* xDeleteInterp */

    /* Memory -- Win32 HeapAlloc (private heap) */
    th8Win32Malloc, /* xMalloc */
    th8Win32Realloc, /* xRealloc */
    th8Win32Free,  /* xFree */
    th8Win32MemorySize, /* xMemorySize */
    0,   /* xNeedMemory */

    /* Byte operations (xMemcpy, xMemmove provided by th8_libc.c) */
    0,   /* xMemcpy */
    0,   /* xMemmove */
    th8Win32Memset, /* xMemset */
    0,   /* xMemcmp */

    /* String / utility (provided by th8_libc.c via merge) */
    0, 0, 0, 0, 0, 0, /* xStrlen .. xVsnprintf */

    /* Threading (CRITICAL_SECTION) */
    th8Win32MutexInit, /* xMutexInit */
    th8Win32MutexFinal, /* xMutexFinal */
    th8Win32MutexEnter, /* xMutexEnter */
    th8Win32MutexLeave, /* xMutexLeave */
    th8Win32IntCmpXchg, /* xIntCmpXchg */
    th8Win32MemBarrier, /* xMemBarrier */

    /* Manual-reset event handle (alertable wait -- QueueUserAPC fires) */
    th8Win32EventCreate, /* xEventCreate */
    th8Win32EventDestroy, /* xEventDestroy */
    th8Win32EventSet, /* xEventSet */
    th8Win32EventReset, /* xEventReset */
    th8Win32EventWait, /* xEventWait */

    /* I/O core */
    th8Win32Input, /* xInput */
    th8Win32Output, /* xOutput */
    th8Win32OutputError, /* xOutputError */

    /* I/O channel redirection */
    th8Win32GetInput, /* xGetInput */
    th8Win32SetInput, /* xSetInput */
    th8Win32GetOutput, /* xGetOutput */
    th8Win32SetOutput, /* xSetOutput */
    th8Win32GetErrorOutput, /* xGetErrorOutput */
    th8Win32SetErrorOutput, /* xSetErrorOutput */

    /* Channel / temporary I/O */
    th8Win32ChannelControl, /* xChannelControl */
    th8Win32GetTemporaryData, /* xGetTemporaryData */
    th8Win32DeleteTemporaryData, /* xDeleteTemporaryData */
    0, /* xSetTemporaryData (no post-write hook) */
    0, /* xCloseTemporaryData */

    /* Filesystem */
    th8Win32NormalizePath, /* xNormalizePath */
    th8Win32GetCwd, /* xGetCwd */
    th8Win32SetCwd, /* xSetCwd */
    th8Win32GetExePath, /* xGetExePath */
    th8Win32GetRealPath, /* xGetRealPath */
    th8Win32GetRootPath, /* xGetRootPath */
    th8Win32SameFile, /* xSameFile */

    /* Data retrieval / binary loading */
    th8Win32GetData, /* xGetData */
    th8Win32DataExists, /* xDataExists */
    th8Win32Load, /* xLoad */
    th8Win32Unload, /* xUnload */

    /* Time */
    th8Win32TimeMs, /* xTimeMs */
    th8Win32TimeUs, /* xTimeUs */
    th8Win32Sleep, /* xSleep */

    /* Process / host information */
    th8Win32GetPid, /* xGetPid */
    th8Win32GetUserName, /* xGetUserName */
    th8Win32GetHostName, /* xGetHostName */
    th8Win32GetEnv, /* xGetEnv */
    0, /* xKeyValue */
    th8Win32GetStackBounds, /* xGetStackBounds */

    /* Parent PID, thread ID */
    th8Win32GetParentPid, /* xGetParentPid */
    th8Win32GetThreadId, /* xGetThreadId */

    /* Error / diagnostics */
    th8Win32GetLastError, /* xGetLastError */
    th8Win32SetLastError, /* xSetLastError */
    th8Win32EmitTrace, /* xEmitTrace */
    th8Win32Panic, /* xPanic */

    /* Math / entropy */
    0, /* xMathFunc (from libc) */
    th8Win32RandomBytes, /* xRandomBytes */

/* DNS -- libunbound recursive resolver with DNSSEC validation
     * when a trust anchor is available; same semantics as the
     * POSIX implementation.  Gated on TH8_ENABLE_UNBOUND -- with
     * the macro undefined the two slots stay NULL and the
     * th8_plat.c dispatcher reports TH8_ERROR for any DNS
     * resolution attempt. */
#  if defined(TH8_ENABLE_UNBOUND)
    th8Win32DnsResolve, th8Win32DnsResolveFree,
#  else
    0, 0,
#  endif

    /* Diagnostics (nVersion 5) -- native RtlCaptureStackBackTrace, since
       MSVC lacks the _Unwind_Backtrace runtime the th8_unwind layer uses.
       Win32 is merged before th8_unwind, so this native entry wins over the
       compiler-runtime no-op fallback. */
    th8Win32StackBackTrace, /* xStackBackTrace */

    /* 64-bit atomics */
    th8Win32IntCmpXchg64, /* xIntCmpXchg64 */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetWin32Platform --
 *
 *	Return a pointer to the Win32 platform implementation.
 *
 * Why / How:
 *	Returns a pointer to the static th8Win32PlatformData struct
 *	that contains all Win32-specific callbacks.  The caller
 *	(typically Th8_Initialize) passes this to the interpreter
 *	to wire up the platform layer.  The struct is const because
 *	it is never modified after compilation; the pCtx field is
 *	NULL (all state is file-scope static).
 *
 * Results:
 *	Pointer to the static Th8_Platform struct.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetWin32Platform(void)
{
    return &th8Win32PlatformData;
}

#endif /* TH8_PLATFORM_WIN32 */
