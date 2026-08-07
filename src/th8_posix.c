/*
 * th8_posix.c -- POSIX platform implementation for TH8.
 *
 * Provides the OS-specific Th8_Platform callbacks for POSIX-
 * compatible systems (Linux, macOS, *BSD, and other Unix-like
 * operating systems).
 *
 * This file provides ONLY the OS-specific callbacks:
 *   - xGetData / xDataExists (file I/O via open/read/stat)
 *   - xRandomBytes (/dev/urandom with zero-fill fallback)
 *   - xTimeMs (gettimeofday)
 *   - xGetPid (getpid)
 *   - xGetParentPid (getppid)
 *   - xGetThreadId (pthread_self)
 *   - xGetStackBounds (pthread APIs, platform-specific)
 *   - xPanic (stderr + abort)
 *
 * Memory (xMalloc, xRealloc, xFree) and most CRT mem-operations
 * (xMemcpy, xMemmove, xMemcmp) are left as NULL and provided by
 * th8_libc.c via Th8_MergePlatform().  xMemset is provided here
 * with secure-zeroing semantics (volatile writes when c==0).
 *
 * ConvertUTF_v2 should be used for any UTF-8 to wchar_t
 * conversion needed by the host (not needed by this file).
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_meta_posix.h"
#include "th8_plat.h"
#include "th8.h"
#include "th8_int.h" /* For TH8_PLATFORM_POSIX auto-detection + internals. */

#if defined(TH8_PLATFORM_POSIX)

extern Th8_Platform th8GlobalPlatform;

/*
 *----------------------------------------------------------------------
 *
 * POSIX syscall fault-injection wrappers (platform MC/DC).
 *
 *	Raw syscalls here (open, fstat, read, ...) essentially never
 *	fail in the test corpus, so their error arms are uncovered.
 *	POSIX_CALL(op, expr) forces the wrapped syscall to report
 *	failure (-1) when op's bit is armed in
 *	th8FaultActiveCfg->nFailPosixMask, WITHOUT invoking it, so the
 *	error arm runs.  POSIX_CALL_PTR is the NULL-returning variant.
 *	The wrapper forces the syscall's VALUE, not the surrounding
 *	decision's structure, so MC/DC counts stay honest, and both
 *	compile to the bare call when TH8_ENABLE_FAULT_INJECTION is
 *	off.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_FAULT_INJECTION)
extern struct Th8_FaultConfig *th8FaultActiveCfg;

/*
 *----------------------------------------------------------------------
 *
 * th8PosixSyscallTrip --
 *
 *	Report whether the fault-injection layer is currently armed to
 *	force the syscall identified by op to fail, consuming a
 *	one-shot arming in the process.
 *
 * Why / How:
 *	The POSIX_CALL / POSIX_CALL_PTR macros consult this predicate
 *	to decide whether to short-circuit a wrapped syscall.  Returns
 *	true only when a fault config is active and op's corresponding
 *	bit is set in th8FaultActiveCfg->nFailPosixMask.  When the
 *	config requests one-shot mode (nFailPosixOnce), a trip clears
 *	op's arming bit so the NEXT call to the same wrapper passes
 *	through -- letting a retry loop's error arm run exactly once
 *	instead of forcing every iteration to fail.
 *
 * Results:
 *	Nonzero if op's failure bit is armed; zero otherwise.
 *
 * Side effects:
 *	In one-shot mode, clears op's bit in nFailPosixMask on a trip.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixSyscallTrip(int op)
{
    if (th8FaultActiveCfg == NULL) {
	return 0;
    }
    if ((th8FaultActiveCfg->nFailPosixMask & ((th8_uint64_t)1 << op)) == 0) {
	return 0;
    }
    if (th8FaultActiveCfg->nFailPosixOnce) {
	th8FaultActiveCfg->nFailPosixMask &= ~((th8_uint64_t)1 << op);
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixFaultErrno --
 *
 *	Return the errno value a forced POSIX_CALL failure should set.
 *
 * Why / How:
 *	Defaults to EIO -- a deterministic NON-EINTR value that mimics
 *	a real syscall error and avoids a stale errno accidentally
 *	reading as EINTR (which would send a `nRead < 0 && errno ==
 *	EINTR` retry loop spinning forever).  A test overrides it via a
 *	POSITIVE nFailPosixErrno to drive an error arm that inspects
 *	errno itself -- e.g. forcing EINTR (with one-shot mode) to run
 *	a retry branch, or a non-EEXIST value for an `errno != EEXIST`
 *	discriminator.  (A NEGATIVE nFailPosixErrno instead requests a
 *	short read; see th8PosixFaultShort -- it is not an errno.)
 *
 * Results:
 *	The configured nFailPosixErrno if POSITIVE, else EIO.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixFaultErrno(void)
{
    if (th8FaultActiveCfg != NULL && th8FaultActiveCfg->nFailPosixErrno > 0) {
	return th8FaultActiveCfg->nFailPosixErrno;
    }
    return EIO;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixFaultShort --
 *
 *	Report whether a forced POSIX_CALL failure should be a SHORT
 *	result (return 0) rather than an error (return -1).
 *
 * Why / How:
 *	A NEGATIVE nFailPosixErrno is the sentinel for short-result
 *	mode: the wrapper returns 0 with errno untouched, mimicking a
 *	premature end-of-file / zero-byte read().  This drives the
 *	`nRead == 0` side of a read loop's `nRead < 0 && errno ==
 *	EINTR` decision (the C1=F, break arm) -- unreachable with the
 *	-1/error faults.  Only meaningful for read-like ops (0 is a
 *	distinguished return there); do not arm it for open/PTR ops.
 *
 * Results:
 *	Nonzero if short-result mode is armed; zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixFaultShort(void)
{
    return th8FaultActiveCfg != NULL &&
           th8FaultActiveCfg->nFailPosixErrno < 0;
}

#    define POSIX_CALL(op, expr)                                             \
	(th8PosixSyscallTrip(op)                                             \
	     ? (th8PosixFaultShort() ? 0                                     \
	                             : (errno = th8PosixFaultErrno(), -1))   \
	     : (expr))
#    define POSIX_CALL_PTR(op, expr)                                         \
	(th8PosixSyscallTrip(op) ? (errno = th8PosixFaultErrno(), (void *)0) \
	                         : (expr))
#  else
#    define POSIX_CALL(op, expr)     (expr)
#    define POSIX_CALL_PTR(op, expr) (expr)
#  endif

/*
 *----------------------------------------------------------------------
 *
 * Data retrieval -- reads files from the filesystem.
 *
 *	The "name" is interpreted as a filesystem path.
 *	Other host environments may override this with Fossil
 *	artifact lookup, HTTP fetch, etc.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8PosixStrlen --
 *
 *	Compute the length of a NUL-terminated string without
 *	depending on the C runtime's strlen().
 *
 * Why / How:
 *	Platform files must only use APIs from the POSIX/libc
 *	abstraction layer, not the merged CRT wrappers.  This
 *	avoids a bootstrap dependency on th8_libc.c during early
 *	initialization (before Th8_MergePlatform has run).
 *	Walks the string byte-by-byte until NUL.
 *
 * Results:
 *	The number of bytes before the NUL terminator.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static size_t
th8PosixStrlen(const char *z)
{
    size_t n = 0;
    if (z) {
	while (z[n])
	    n++;
    }
    return n;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixStrcmp --
 *
 *	Compare two NUL-terminated strings lexicographically without
 *	depending on the C runtime's strcmp().
 *
 * Why / How:
 *	Same bootstrap rationale as th8PosixStrlen -- this file
 *	cannot rely on the merged CRT wrappers.  Compares via
 *	unsigned-char subtraction for correct ordering of non-ASCII
 *	bytes.
 *
 * Results:
 *	Negative if a < b, zero if equal, positive if a > b.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixStrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
	a++;
	b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixStrrchr --
 *
 *	Find the last occurrence of byte c in a NUL-terminated string
 *	without depending on the C runtime's strrchr().
 *
 * Why / How:
 *	Same bootstrap rationale as th8PosixStrlen -- avoids a
 *	dependency on the merged CRT wrappers.  Walks the entire
 *	string, recording the most recent match.
 *
 * Results:
 *	Pointer to the last occurrence of c, or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
th8PosixStrrchr(const char *s, int c)
{
    const char *last = 0;
    while (*s) {
	if (*s == c) last = s;
	s++;
    }
    return (char *)last;
}

/* Forward declarations (defined after base path section). */
static const char *th8PosixGetBasePath(void);
int th8PosixIsPathUnderBase(const char *zPath);

/*
 *----------------------------------------------------------------------
 *
 * th8PosixRobustWrite --
 *
 *	Write exactly n bytes to a POSIX file descriptor, retrying
 *	on EINTR and partial writes.
 *
 * Why / How:
 *	POSIX write(2) may return fewer bytes than requested (short
 *	write) or fail with EINTR when interrupted by a signal.
 *	This helper loops until all bytes are written or a hard
 *	error occurs, ensuring atomic-looking output for panic
 *	messages and diagnostics.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes n bytes to fd.  Silently stops on hard errors.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixRobustWrite(int fd, const char *buf, size_t n)
{
    while (n > 0) {
	ssize_t nw = write(fd, buf, n);
	if (nw < 0) {
	    if (errno == EINTR) continue;
	    break;
	}
	buf += nw;
	n -= (size_t)nw;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetData --
 *
 *	Implements the Th8_Platform.xGetData callback.  Reads the
 *	contents of a file from the filesystem into an allocated
 *	buffer.
 *
 * Why / How:
 *	The interpreter's [source] and [package] commands need to
 *	read script files.  This callback opens the named file via
 *	open(2) with O_NOFOLLOW (rejecting symlinks at the final
 *	component for TOCTOU safety), stats it for size, reads
 *	the entire content into a Th8_AttemptMalloc'd buffer, and
 *	NUL-terminates it.  Short names use a stack buffer to
 *	avoid allocation for the path.  The path is validated
 *	against the base directory to enforce sandbox restrictions.
 *
 * Results:
 *	TH8_OK on success with *pzOut and *pnOut set; TH8_ERROR
 *	on failure with both set to 0.
 *
 * Side effects:
 *	Allocates memory for the file contents (caller frees via
 *	Th8_Free).  Opens and closes a file descriptor.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetData(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    char **pzOut,
    size_t *pnOut)
{
    char zPathBuf[1024];
    char *zPath;
    int fd;
    struct stat st;
    off_t nSize;
    char *zBuf;
    ssize_t nRead;

    (void)pCtx;

    /*
     * NUL-terminate the name for open(2).  Use a stack
     * buffer for short names to avoid allocation.
     */

    if (nName < sizeof(zPathBuf)) {
	zPath = zPathBuf;
    } else {
	zPath = (char *)TH8_ALLOC_STR(interp, nName);
	if (!zPath) {
	    TH8_TRACE_ERR(NULL, "out of memory");
	    return TH8_ERROR;
	}
    }
    memcpy(zPath, zName, nName);
    zPath[nName] = 0;

    /*
     * Validate that the path is under the base directory.
     * This prevents scripts from reading data outside the
     * sandbox via absolute paths or ".." traversal.
     */
    if (!th8PosixIsPathUnderBase(zPath)) {
	if (zPath != zPathBuf) {
	    Th8_Free(interp, zPath);
	}
	TH8_TRACE_ERR(NULL, "path outside base directory");
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    fd = POSIX_CALL(
        TH8_POSIX_OP_GETDATA_OPEN,
        open(zPath, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (zPath != zPathBuf) {
	Th8_Free(interp, zPath);
    }
    if (fd < 0) {
	TH8_TRACE_ERR(NULL, "open failed");
	TH8_TRACE_ERR(NULL, zPath);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    if (POSIX_CALL(TH8_POSIX_OP_GETDATA_FSTAT, fstat(fd, &st)) != 0 ||
        !S_ISREG(st.st_mode)) {
	TH8_TRACE_ERR(NULL, "fstat failed");
	close(fd);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }
    nSize = st.st_size;

    if (nSize < 0 || nSize > 0x0fffffff) {
	TH8_TRACE_ERR(NULL, "file too large");
	close(fd);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    /*
     * Allocate via Th8_AttemptMalloc so the caller can
     * free with Th8_Free (uses the correct allocator:
     * private zone on macOS, HeapAlloc on Win32, etc.).
     */

    zBuf = (char *)TH8_ALLOC_STR(interp, (size_t)nSize);
    if (!zBuf) {
	TH8_TRACE_ERR(NULL, "out of memory");
	close(fd);
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    {
	char *pRd = zBuf;
	size_t nLeft = (size_t)nSize;

	while (nLeft > 0) {
	    nRead =
	        POSIX_CALL(TH8_POSIX_OP_GETDATA_READ, read(fd, pRd, nLeft));
	    if (nRead > 0) {
		pRd += nRead;
		nLeft -= (size_t)nRead;
	    } else if (nRead < 0 && errno == EINTR) {
		continue;
	    } else {
		break;
	    }
	}
	if (nLeft > 0) {
	    TH8_TRACE_ERR(NULL, "read failed");
	    close(fd);
	    Th8_Free(interp, zBuf);
	    *pzOut = 0;
	    *pnOut = 0;
	    return TH8_ERROR;
	}
    }
    close(fd);
    zBuf[nSize] = 0;

    *pzOut = zBuf;
    *pnOut = (size_t)nSize;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixDataExists --
 *
 *	Implements the Th8_Platform.xDataExists callback.  Tests
 *	whether a named file exists and optionally returns its
 *	type attributes (regular file, directory, symlink, etc.).
 *
 * Why / How:
 *	The interpreter's [file exists] and [package] commands need
 *	to probe the filesystem without reading the file.  Uses
 *	lstat(2) to detect symlinks at the final component, then
 *	stat(2) to follow the symlink and determine the target
 *	type.  Dangling symlinks are reported as TH8_FILE_ATTR_BROKEN.
 *	Paths outside the sandbox silently report as non-existing
 *	to prevent information leakage about the host filesystem.
 *
 * Results:
 *	Non-zero if the path exists, zero otherwise.  When pAttrs
 *	is non-NULL, fills it with TH8_FILE_ATTR_* flags.
 *
 * Side effects:
 *	May allocate a temporary path buffer for long names.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixDataExists(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    int *pAttrs) /* OUT: file type attrs (may be NULL). */
{
    char zPathBuf[1024];
    char *zPath;
    struct stat lst;
    int rc;

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
     * Paths outside the sandbox are reported as non-existing
     * to prevent information leakage.
     */
    if (!th8PosixIsPathUnderBase(zPath)) {
	if (zPath != zPathBuf) {
	    Th8_Free(interp, zPath);
	}
	if (pAttrs) *pAttrs = 0;
	return 0;
    }

    /*
     * Use lstat first to detect symlinks, then stat to
     * follow the symlink and determine the target type.
     */
    rc = (lstat(zPath, &lst) == 0);
    if (rc && pAttrs) {
	if (S_ISLNK(lst.st_mode)) {
	    /*
	     * Symlink: follow it to determine target type.
	     */
	    struct stat st;

	    if (stat(zPath, &st) == 0) {
		if (S_ISREG(st.st_mode)) {
		    *pAttrs = TH8_FILE_ATTR_FILE | TH8_FILE_ATTR_SYMLINK;
		} else if (S_ISDIR(st.st_mode)) {
		    *pAttrs = TH8_FILE_ATTR_DIRECTORY | TH8_FILE_ATTR_SYMLINK;
		} else {
		    *pAttrs = TH8_FILE_ATTR_UNSUPPORTED;
		}
	    } else {
		/*
		 * Dangling symlink: target does not exist.
		 */
		*pAttrs = TH8_FILE_ATTR_BROKEN;
	    }
	} else if (S_ISREG(lst.st_mode)) {
	    *pAttrs = TH8_FILE_ATTR_FILE;
	} else if (S_ISDIR(lst.st_mode)) {
	    *pAttrs = TH8_FILE_ATTR_DIRECTORY;
	} else {
	    *pAttrs = TH8_FILE_ATTR_UNSUPPORTED;
	}
    }
    return rc;
}


/* <dlfcn.h> included via th8_meta_posix.h */

#  if !defined(TH8_FUZZ_STANDALONE)

static void th8PosixSaveHandle(Th8_Interp *, const char *, size_t, void *);

typedef int (*Th8_LoadInitProc)(Th8_Interp *);

/*
 *----------------------------------------------------------------------
 *
 * th8PosixLoad --
 *
 *	Implements the Th8_Platform.xLoad callback.  Load a shared
 *	library via dlopen/dlsym and call its init function.
 *
 *	Name format:  <libraryPath>:<symbolPrefix>_Init
 *
 *	The portion before the colon is passed to dlopen().
 *	The portion after the colon has "_Init" appended and
 *	is looked up via dlsym().  The init function receives
 *	the Th8_Interp* and should call Th8_CreateCommand()
 *	to register its commands.
 *
 *	If zProc/nProc is non-NULL, it overrides the symbol
 *	name derived from the name string (the part after the
 *	colon is ignored and zProc is used directly).
 *
 *	Example:
 *	  name = "./libfoo.so:Foo"
 *	  dlopen("./libfoo.so")
 *	  dlsym(handle, "Foo_Init")
 *	  call Foo_Init(interp)
 *
 * Why / How:
 *	The [load] command needs a platform-specific mechanism to
 *	bring native extensions into the interpreter.  On POSIX,
 *	this uses dlopen with RTLD_NOW|RTLD_LOCAL for eager symbol
 *	resolution without polluting the global namespace.  The
 *	library path is validated against the base directory to
 *	enforce sandbox restrictions.  The dlopen handle is saved
 *	via th8PosixSaveHandle for later unloading.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	Loads a shared library and calls its init function,
 *	which typically registers new commands.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixLoad(
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
    void *hLib;
    Th8_LoadInitProc xInit;

    (void)pCtx;

    if (!interp || !zName || nName == 0) {
	TH8_TRACE_ERR(NULL, "invalid arguments");
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

	for (i = 0; i < nName; i++) {
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
     * Extract library path (before colon).
     */

    nLib = (size_t)(zColon - zName);
    if (nLib < sizeof(zLibBuf)) {
	zLib = zLibBuf;
    } else {
	zLib = (char *)TH8_ALLOC_STR(interp, nLib);
	if (!zLib) {
	    TH8_TRACE_ERR(NULL, "out of memory");
	    return TH8_ERROR;
	}
    }
    Th8_Memcpy(interp, zLib, zName, nLib);
    zLib[nLib] = 0;

    /*
     * The library path must be under the base directory.
     */
    if (!th8PosixIsPathUnderBase(zLib)) {
	if (zLib != zLibBuf) {
	    Th8_Free(interp, zLib);
	}
	Th8_SetResult(
	    interp, "xLoad: library path outside base directory", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Build the init function name.
     *
     * If zProc is provided, use it directly.
     * Otherwise, take the part after the colon and append "_Init".
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
	nSym = nProc + 5; /* + "_Init" */
    }

    if (nSym < sizeof(zSymBuf)) {
	zSym = zSymBuf;
    } else {
	zSym = (char *)TH8_ALLOC_STR(interp, nSym);
	if (!zSym) {
	    TH8_TRACE_ERR(NULL, "out of memory");
	    if (zLib != zLibBuf) Th8_Free(interp, zLib);
	    return TH8_ERROR;
	}
    }

    Th8_Memcpy(interp, zSym, zProc, nProc);
    if (nSym > nProc) {
	Th8_Memcpy(interp, &zSym[nProc], "_Init", 5);
    }
    zSym[nSym] = 0;

    /*
     * dlopen the library.
     */

    hLib = dlopen(zLib, RTLD_NOW | RTLD_LOCAL);
    if (!hLib) {
	const char *zErr = dlerror();

	if (zErr) {
	    Th8_SetResult(interp, zErr, TH8_NOLEN);
	} else {
	    Th8_ErrorMessage(
	        interp, "xLoad: dlopen failed for \"", zLib, nLib);
	}
	TH8_TRACE_ERR(NULL, "dlopen failed");
	TH8_TRACE_ERR(NULL, zLib);
	if (zLib != zLibBuf) Th8_Free(interp, zLib);
	if (zSym != zSymBuf) Th8_Free(interp, zSym);
	return TH8_ERROR;
    }

    /*
     * dlsym the init function.
     */

    *(void **)&xInit = dlsym(hLib, zSym);
    if (!xInit) {
	Th8_ErrorMessage(interp, "xLoad: symbol not found \"", zSym, nSym);
	TH8_TRACE_ERR(NULL, "dlsym failed");
	TH8_TRACE_ERR(NULL, zSym);
	dlclose(hLib);
	if (zLib != zLibBuf) Th8_Free(interp, zLib);
	if (zSym != zSymBuf) Th8_Free(interp, zSym);
	return TH8_ERROR;
    }

    /*
     * Save the dlopen handle for potential later unloading.
     * Must happen before freeing zLib (we need the library path).
     */

    th8PosixSaveHandle(interp, zName, nName, hLib);

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
 * POSIX dlopen handle tracking --
 *
 *	A process-global linked list mapping library names to their
 *	dlopen handles.  This is needed because xLoad's dlopen handle
 *	would otherwise be lost after the init function returns.
 *	The xUnload callback uses these to call dlsym(_Unload) and
 *	optionally dlclose.
 *
 *	Process-global because dlopen/dlclose are process-scoped.
 *	Protected by th8PosixLock/Unlock; entries are reference-
 *	counted to prevent use-after-free when one thread looks
 *	up a handle while another unloads it.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_PosixLibHandle Th8_PosixLibHandle;
struct Th8_PosixLibHandle {
    char *zName; /* Full load name ("lib:sym"), owned. */
    size_t nName; /* Byte length of zName. */
    void *hLib; /* dlopen handle. */
    int nRef; /* Reference count. */
    Th8_PosixLibHandle *pNext; /* Next in global list. */
};

/*
 * Per-interpreter library list helpers.
 *
 * The library handle list is stored on the interpreter via
 * th8GetPlatformLibs / th8SetPlatformLibs.  Access is
 * serialized by the platform mutex (th8PosixLock/Unlock)
 * for thread safety.
 */

static void th8PosixLock(void);
static void th8PosixUnlock(void);

/*
 *----------------------------------------------------------------------
 *
 * th8PosixSaveHandle --
 *
 *	Record a dlopen handle in the per-interpreter library list.
 *
 * Why / How:
 *	After dlopen, the handle must be saved so that xUnload can
 *	later call dlsym for the _Unload entry point and optionally
 *	dlclose the library.  Allocates a Th8_PosixLibHandle struct,
 *	copies the load name, and prepends it to the per-interpreter
 *	linked list under the platform mutex.  The initial reference
 *	count is 1 (owned by the list).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory.  Acquires and releases the platform mutex.
 *	Modifies the per-interpreter library list.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixSaveHandle(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    void *hLib)
{
    Th8_PosixLibHandle *p;
    Th8_PosixLibHandle *pList;

    p = (Th8_PosixLibHandle *)TH8_ALLOC(interp, sizeof(Th8_PosixLibHandle));
    if (!p) return;
    p->zName = (char *)TH8_ALLOC_STR(interp, nName);
    if (!p->zName) {
	Th8_Free(interp, p);
	return;
    }
    Th8_Memcpy(interp, p->zName, zName, nName);
    p->zName[nName] = 0;
    p->nName = nName;
    p->hLib = hLib;
    p->nRef = 1;

    th8PosixLock();
    pList = (Th8_PosixLibHandle *)th8GetPlatformLibs(interp);
    p->pNext = pList;
    th8SetPlatformLibs(interp, p);
    th8PosixUnlock();
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixFindHandle --
 *
 *	Look up a library handle by name in the per-interpreter list.
 *
 * Why / How:
 *	xUnload needs to retrieve the dlopen handle for a previously
 *	loaded library.  Walks the per-interpreter linked list under
 *	the platform mutex, comparing names via Th8_Memcmp.  When
 *	found, increments the reference count before returning so
 *	the caller can safely use the handle even if another thread
 *	removes it from the list concurrently.
 *
 * Results:
 *	Pointer to the Th8_PosixLibHandle if found (with nRef
 *	incremented), or NULL if no match.
 *
 * Side effects:
 *	Increments the reference count on the found entry.
 *	Acquires and releases the platform mutex.
 *
 *----------------------------------------------------------------------
 */

static Th8_PosixLibHandle *
th8PosixFindHandle(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_PosixLibHandle *p;

    th8PosixLock();
    for (p = (Th8_PosixLibHandle *)th8GetPlatformLibs(interp); p;
         p = p->pNext) {
	if (p->nName == nName &&
	    0 == Th8_Memcmp(interp, p->zName, zName, nName)) {
	    p->nRef++;
	    th8PosixUnlock();
	    return p;
	}
    }
    th8PosixUnlock();
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixReleaseHandle --
 *
 *	Decrement the reference count on a library handle and free
 *	it when the count reaches zero.
 *
 * Why / How:
 *	th8PosixFindHandle increments the reference count so the
 *	caller can safely use the handle.  This function is called
 *	when that use is complete.  When nRef drops to zero, the
 *	library is dlclose'd and the handle struct and name are
 *	freed via Th8_Free.  This prevents use-after-free when
 *	one thread looks up a handle while another unloads it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May call dlclose and free memory when nRef reaches zero.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixReleaseHandle(Th8_Interp *interp, Th8_PosixLibHandle *p)
{
    if (!p) return;
    p->nRef--;
    if (p->nRef <= 0) {
	if (p->hLib) {
	    dlclose(p->hLib);
	    p->hLib = 0;
	}
	Th8_Free(interp, p->zName);
	Th8_Free(interp, p);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixRemoveHandle --
 *
 *	Remove a library handle from the per-interpreter list by name
 *	and decrement its reference count.
 *
 * Why / How:
 *	When a library is unloaded, its tracking entry must be
 *	removed from the per-interpreter linked list.  Walks the
 *	list under the platform mutex, unlinks the matching entry,
 *	and decrements its reference count.  If the reference count
 *	reaches zero (no other thread holds a reference from
 *	th8PosixFindHandle), the library is dlclose'd and freed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the per-interpreter library list.  May call dlclose
 *	and free memory.  Acquires and releases the platform mutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixRemoveHandle(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_PosixLibHandle **pp;
    Th8_PosixLibHandle *pList;

    th8PosixLock();
    pList = (Th8_PosixLibHandle *)th8GetPlatformLibs(interp);
    for (pp = &pList; *pp; pp = &(*pp)->pNext) {
	Th8_PosixLibHandle *p = *pp;

	if (p->nName == nName &&
	    0 == Th8_Memcmp(interp, p->zName, zName, nName)) {
	    *pp = p->pNext;
	    p->pNext = 0;
	    th8SetPlatformLibs(interp, pList);
	    th8PosixUnlock();
	    p->nRef--;
	    if (p->nRef <= 0) {
		if (p->hLib) {
		    dlclose(p->hLib);
		    p->hLib = 0;
		}
		Th8_Free(interp, p->zName);
		Th8_Free(interp, p);
	    }
	    return;
	}
    }
    th8PosixUnlock();
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixUnload --
 *
 *	Implements the Th8_Platform.xUnload callback.  Unload
 *	(finalize and optionally close) a shared library.
 *
 *	Looks up the dlopen handle from the process-global list.
 *	Derives the _Unload symbol name from the library name
 *	(same colon convention as th8PosixLoad).  If the _Unload
 *	symbol exists, calls it.  If bClose is non-zero, calls
 *	dlclose and removes the handle from the tracking list.
 *
 *	The _Unload function signature follows Tcl convention:
 *	  int Pkg_Unload(Th8_Interp *interp, int flags)
 *	where flags is a bitmask of TH8_UNLOAD_DETACH_FROM_*
 *	constants defined in th8.h.
 *
 * Why / How:
 *	The [unload] command needs to finalize and optionally close
 *	a previously loaded shared library.  This uses the reference-
 *	counted handle tracking list to look up the dlopen handle,
 *	then derives and calls the _Unload entry point (if present)
 *	to give the library a chance to clean up.  Not finding
 *	_Unload is not an error -- the library may not need cleanup.
 *	The reference counting ensures thread safety: FindHandle
 *	increments the count, and ReleaseHandle/RemoveHandle
 *	decrement it, with dlclose only occurring when the last
 *	reference is released.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	May call the library's _Unload function, call dlclose,
 *	and free the tracking entry.
 *
 *----------------------------------------------------------------------
 */

typedef int (*Th8_UnloadProc)(Th8_Interp *, int);

/*
 *----------------------------------------------------------------------
 *
 * th8PosixCallUnloadProc --
 *
 *	Look up "<Prefix>_Unload" in hLib and, if present, invoke it.
 *	Prefix is derived from the part of zName after a ':' (matching
 *	th8PosixLoad's symbol convention); if zName has no colon, no
 *	symbol is constructed and the call is a no-op.
 *
 * Why / How:
 *	Centralizes the _Unload entry-point invocation that two paths
 *	now share: th8PosixUnload (the explicit [unload] command) and
 *	th8PosixUnloadLibs (the implicit cleanup when an interpreter
 *	is being deleted).  Without this hook firing on the implicit
 *	path, a library's process-global state -- e.g. SQLite's
 *	allocator caches and date/time formatter freed only by
 *	sqlite3_shutdown -- leaks until process exit.  Found and fixed
 *	from a valgrind run that tied 76 KB of leaks to th8PosixLoad.
 *
 *	Failure to locate _Unload is not an error: many libraries
 *	have nothing to clean up.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May call the library's _Unload function (which may allocate,
 *	free, evaluate scripts in the interpreter, etc.).
 *
 *----------------------------------------------------------------------
 */

void
th8PosixCallUnloadProc(
    Th8_Interp *interp,
    void *hLib,
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
    Th8_UnloadProc xUnloadFn;
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
	/* No colon means no derivable suffix; nothing to call. */
	return;
    }
    zProc = zColon + 1;
    nProc = nName - (size_t)(zColon - zName) - 1;
    nSym = nProc + 7; /* "_Unload" */

    if (nSym < sizeof(zSymBuf)) {
	zSym = zSymBuf;
    } else {
	zSym = (char *)TH8_ALLOC_STR(interp, nSym);
	if (!zSym) {
	    TH8_TRACE_ERR(NULL, "out of memory");
	    return;
	}
    }
    Th8_Memcpy(interp, zSym, zProc, nProc);
    Th8_Memcpy(interp, &zSym[nProc], "_Unload", 7);
    zSym[nSym] = 0;

    *(void **)&xUnloadFn = dlsym(hLib, zSym);
    if (xUnloadFn) {
	xUnloadFn(interp, cbFlags);
    }

    if (zSym != zSymBuf) Th8_Free(interp, zSym);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixUnload --
 *
 *	`xUnload` platform-callback implementation: tear down a
 *	dynamic library previously loaded by
 *	`th8PosixLoad`.  Steps:
 *
 *	  1. Validate arguments and look up the cached handle
 *	     by name (`th8PosixFindHandle`), which also bumps
 *	     the reference count.
 *	  2. Derive the per-library `_Unload` symbol from the
 *	     trailing portion of `zName` after the first `:`
 *	     (matching the load convention).  Build either in
 *	     a small stack buffer or in a heap allocation.
 *	  3. `dlsym` the unload symbol; if present, invoke it
 *	     with the interpreter and caller-supplied flags
 *	     so the library can tear down its registrations.
 *	  4. If `bClose` is set and the last reference is now
 *	     gone, `dlclose` the library and remove its
 *	     handle from the cache.
 *
 *	Errors (unknown library, allocation failure, dlsym
 *	failure) emit a diagnostic into the interpreter result
 *	via `Th8_ErrorMessage` / `TH8_TRACE_ERR`.
 *
 * Parameters:
 *	interp -- live interpreter (receives diagnostics).
 *	pCtx   -- platform context (ignored).
 *	zName  -- handle name as passed at load time.
 *	nName  -- name length.
 *	zProc  -- caller-supplied init/unload procedure name
 *		fragment (preserved for `_Unload`-symbol
 *		derivation).
 *	nProc  -- length of `zProc`.
 *	bClose -- 1 to `dlclose` once refs reach zero;
 *		0 to keep the handle cached.
 *
 * Returns:
 *	`TH8_OK` on success.
 *	`TH8_ERROR` on bad arguments or look-up failure
 *	(interpreter result: diagnostic).
 *
 * Side effects:
 *	May `dlclose` the underlying shared library.  Mutates
 *	the per-interp library cache.  May allocate / free a
 *	scratch symbol-name buffer.
 *
 *----------------------------------------------------------------------
 */
static int
th8PosixUnload(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    const char *zProc,
    size_t nProc,
    int bClose)
{
    Th8_PosixLibHandle *pHandle;
    void *hLib;
    const char *zColon = 0;
    size_t nPrefix;
    size_t nSym;
    char zSymBuf[256];
    char *zSym;
    Th8_UnloadProc xUnloadFn;
    size_t i;

    (void)pCtx;

    if (!interp || !zName || nName == 0) {
	TH8_TRACE_ERR(NULL, "invalid arguments");
	return TH8_ERROR;
    }

    /*
     * Find the handle (increments the reference count).
     */

    pHandle = th8PosixFindHandle(interp, zName, nName);
    if (!pHandle) {
	Th8_ErrorMessage(
	    interp, "xUnload: library not loaded: \"", zName, nName);
	return TH8_ERROR;
    }
    hLib = pHandle->hLib;

    /*
     * Find the colon separator to derive the _Unload name.
     */

    for (i = 0; i < nName; i++) {
	if (zName[i] == ':') {
	    zColon = &zName[i];
	    break;
	}
    }

    /*
     * Build the _Unload symbol name.
     *
     * If zProc is provided, use it directly.
     * Otherwise derive from the part after the colon: Prefix_Unload.
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
	nSym = nPrefix + 7; /* "_Unload" */
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
		TH8_TRACE_ERR(NULL, "out of memory");
		th8PosixReleaseHandle(interp, pHandle);
		return TH8_ERROR;
	    }
	}
	Th8_Memcpy(interp, zSym, zProc, nProc);
	Th8_Memcpy(interp, &zSym[nProc], "_Unload", 7);
	zSym[nSym] = 0;

	*(void **)&xUnloadFn = dlsym(hLib, zSym);
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

    th8PosixRemoveHandle(interp, zName, nName);

    /*
     * Release our FindHandle reference (decrements another
     * reference).  When this is the last reference, the
     * library is closed and the entry is freed.
     */

    th8PosixReleaseHandle(interp, pHandle);
    return TH8_OK;
}

#  endif /* !TH8_FUZZ_STANDALONE -- end of load/unload block */


/*
 *----------------------------------------------------------------------
 *
 * th8PosixFillPrng --
 *
 *	Fill a buffer with pseudo-random bytes using a linear
 *	congruential generator.  Seeded from time + pid on first
 *	call.  Not cryptographic, but produces non-zero bytes
 *	suitable for hash seeding and security token generation
 *	when no better source is available.
 *
 *	Also used as the deterministic PRNG for AFL++ fuzzing
 *	(with a fixed seed) when TH8_FUZZ_STANDALONE is defined.
 *
 * Why / How:
 *	Serves as the fallback when /dev/urandom is unavailable
 *	or produces a short read.  Uses the glibc LCG constants
 *	(multiplier 1103515245, increment 12345) and extracts
 *	bits 16..23 of the state for each output byte, which are
 *	the highest-quality bits of a 32-bit LCG.  The seed mixes
 *	gettimeofday and getpid to provide per-process variation.
 *	Under TH8_FUZZ_STANDALONE the seed is fixed to 0x46555A5A
 *	("FUZZ") so fuzzer runs are reproducible.
 *
 * Results:
 *	None (output written in-place to pBuf).
 *
 * Side effects:
 *	Advances the static seed state.  On first non-fuzz call,
 *	reads the system clock and PID.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixFillPrng(void *pBuf, size_t nByte)
{
#  if defined(TH8_FUZZ_STANDALONE)
    static unsigned long seed = 0x46555A5A; /* "FUZZ" -- fixed seed */
#  else
    static unsigned long seed = 0;
    static int seeded = 0;

    if (!seeded) {
	{
	    struct timeval tv;
	    gettimeofday(&tv, NULL);
	    seed = (unsigned long)tv.tv_sec ^
	           ((unsigned long)tv.tv_usec << 12);
	}
	seed ^= ((unsigned long)getpid() << 16);
	if (seed == 0) seed = 1;  /* Never zero. */
	seeded = 1;
    }
#  endif
    {
	unsigned char *p = (unsigned char *)pBuf;
	size_t i;

	for (i = 0; i < nByte; i++) {
	    seed = seed * 1103515245 + 12345;
	    p[i] = (unsigned char)(seed >> 16);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixMemset --
 *
 *	Implements the Th8_Platform.xMemset callback.  When the
 *	fill byte is zero, uses the best available secure-zeroing
 *	primitive:
 *	  1. explicit_bzero (glibc 2.25+ / POSIX.1-2024)
 *	  2. bzero (POSIX.1-2001, removed in 2008 but still
 *	     universally available on BSDs; an external library
 *	     call not subject to dead-store elimination)
 *	  3. Volatile write loop (ultimate fallback)
 *
 *	Non-zero fills use a plain byte loop.
 *
 * Why / How:
 *	Cryptographic key material, passwords, and other secrets
 *	must be securely erased after use.  Compilers aggressively
 *	eliminate "dead" memset calls when the buffer is not
 *	subsequently read.  The volatile-pointer loop (c2 ^ c2)
 *	defeats dead-store elimination because the compiler cannot
 *	prove the volatile write is unobservable.  On BSDs, bzero
 *	is a real external function call that the optimizer cannot
 *	remove.  This callback is provided here (not in th8_libc.c)
 *	because the secure-zeroing strategy is OS-specific.
 *
 * Results:
 *	Returns dst (matching memset semantics).
 *
 * Side effects:
 *	Writes to dst.  The zero-fill path is guaranteed not to
 *	be optimized away.
 *
 *----------------------------------------------------------------------
 */

static void *
th8PosixMemset(Th8_Interp *interp, void *pCtx, void *dst, int c, size_t n)
{
    volatile unsigned char *p;

    (void)interp;
    (void)pCtx;

    if (c == 0) {
#  if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
      (defined(_POSIX_VERSION) && _POSIX_VERSION < 200809L)
	/*
	 * BSD systems and older POSIX: bzero is an external call,
	 * safe from dead-store elimination.
	 */
	bzero(dst, n);
#  else
	/* Fallback: volatile writes prevent dead-store elimination. */
	p = (volatile unsigned char *)dst;
	while (n--) {
	    unsigned char c2 = *p;
	    *p++ = c2 ^ c2;
	}
#  endif
    } else {
	p = (volatile unsigned char *)dst;
	while (n--)
	    *p++ = (unsigned char)c;
    }
    return dst;
}


#  if defined(TH8_ENABLE_UNBOUND)
#    include "th8_unbound.h"

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetModuleAnchorPath --
 *
 *	Compose the path to a candidate trust anchor co-located
 *	with the currently-loaded TH8 shared object (`.so` /
 *	`.dylib`, or the host EXE for a statically-linked build):
 *	the directory containing the image, plus `/root.key`.
 *	Uses `dladdr` on this function's own address, which
 *	works regardless of how TH8 was linked (named DLL,
 *	custom DLL name, fully static).  Does NOT check whether
 *	the file actually exists -- callers use the returned
 *	path with `ub_ctx_add_ta_file` /
 *	`th8PosixPathReadable` to determine that.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zBuf -- output buffer; receives the NUL-terminated path
 *		on success.
 *	nBuf -- size of `zBuf` in bytes.
 *
 * Returns:
 *	1 on success; 0 if `dladdr` failed, the resolved path
 *	contained no `/` (which cannot happen on POSIX), or the
 *	composed path would exceed `nBuf`.
 *
 * Side effects:
 *	On success, writes to `zBuf`.
 *
 *----------------------------------------------------------------------
 */
static int
th8PosixGetModuleAnchorPath(char *zBuf, size_t nBuf)
{
    Dl_info info;
    union {
	void *p;
	int (*f)(char *, size_t);
    } uAddr;
    char *slash;
    size_t nDir;
    static const char zAnchor[] = "/root.key";

    uAddr.f = th8PosixGetModuleAnchorPath;
    if (!dladdr(uAddr.p, &info) || !info.dli_fname) return 0;
    nDir = th8PosixStrlen(info.dli_fname);
    if (nDir + sizeof(zAnchor) > nBuf) return 0;
    memcpy(zBuf, info.dli_fname, nDir + 1);
    slash = th8PosixStrrchr(zBuf, '/');
    if (!slash) return 0;
    nDir = (size_t)(slash - zBuf);
    memcpy(zBuf + nDir, zAnchor, sizeof(zAnchor));
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixPathReadable --
 *
 *	Predicate: is `zPath` an existing file (or other
 *	stat-able object) that the calling process can `open`
 *	for reading?  Implemented via `access(zPath, R_OK)`,
 *	which checks the real (not effective) UID per POSIX so
 *	a setuid process tests against its login user's
 *	permissions -- safer than checking with elevated
 *	privileges.
 *
 *	Used by the trust-anchor search chain to skip non-
 *	existent fallback paths without hammering libunbound
 *	with `ub_ctx_add_ta_file` calls that would all fail.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zPath -- NUL-terminated path to test.
 *
 * Returns:
 *	1 if readable; 0 otherwise (including any errno set
 *	by `access`).
 *
 * Side effects:
 *	None visible (one `access` syscall).
 *
 *----------------------------------------------------------------------
 */
static int
th8PosixPathReadable(const char *zPath)
{
    return access(zPath, R_OK) == 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixFindStaticAnchorPath --
 *
 *	Locate a readable static IANA root-key file by walking
 *	the conventional install-location search list, in
 *	priority order:
 *	  1. The TH8 module directory (`libth8.so` / `.dylib`
 *	     neighbour).
 *	  2. `/etc/unbound/root.key` (NLnet Labs install).
 *	  3. `/usr/share/dns/root.key` (Debian / Ubuntu).
 *	  4. `/var/lib/unbound/root.key` (alternate install).
 *	  5. `/opt/homebrew/etc/unbound/root.key` (macOS / Homebrew).
 *
 *	Each candidate is screened by `th8PosixPathReadable`
 *	before being returned, so the function never returns a
 *	path that the caller would fail to open.
 *
 *	The module-directory candidate wins over the system
 *	paths because an anchor sitting next to the library was
 *	clearly intended for that library by whoever staged the
 *	install, versus a system-wide anchor that may have been
 *	installed by a different actor for a different purpose.
 *
 *	Used as the SOURCE for the bootstrap-then-auto-roll
 *	flow in `th8PosixSetupManagedAnchor`.
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
th8PosixFindStaticAnchorPath(char *zBuf, size_t nBuf)
{
    static const char *const azSystem[] = {
        "/etc/unbound/root.key",
        "/usr/share/dns/root.key",
        "/var/lib/unbound/root.key",
        "/opt/homebrew/etc/unbound/root.key",
    };
    size_t i;

    if (th8PosixGetModuleAnchorPath(zBuf, nBuf) &&
        th8PosixPathReadable(zBuf)) {
	return 1;
    }
    for (i = 0; i < sizeof(azSystem) / sizeof(azSystem[0]); i++) {
	size_t n = th8PosixStrlen(azSystem[i]);
	if (n + 1 > nBuf) continue;
	if (!th8PosixPathReadable(azSystem[i])) continue;
	memcpy(zBuf, azSystem[i], n + 1);
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetManagedAnchorPath --
 *
 *	Compose the path to the per-user WRITABLE managed copy
 *	of the trust anchor.  This is the file libunbound's
 *	`ub_ctx_add_ta_autr` reads on every resolve and writes
 *	state-machine updates to as the RFC 5011 hold-down
 *	timer advances.
 *
 *	Directory selection follows the XDG Base Directory
 *	convention:
 *	  *  `$XDG_DATA_HOME/th8/root.key` if `XDG_DATA_HOME`
 *	     is set in the environment.
 *	  *  `$HOME/.local/share/th8/root.key` otherwise.
 *	If neither variable is set the function fails -- there
 *	is no usable per-user location and the caller must
 *	fall back to static-anchor mode.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zBuf -- output buffer; receives the NUL-terminated path
 *		on success.
 *	nBuf -- size of `zBuf` in bytes.
 *
 * Returns:
 *	1 on success; 0 if no per-user directory is available
 *	(no `HOME`, no `XDG_DATA_HOME`) or the composed path
 *	would exceed `nBuf`.
 *
 * Side effects:
 *	On success, writes to `zBuf`.  Does NOT create the
 *	directory (see `th8PosixEnsureParentDir`).
 *
 *----------------------------------------------------------------------
 */
static int
th8PosixGetManagedAnchorPath(char *zBuf, size_t nBuf)
{
    const char *zBase = getenv("XDG_DATA_HOME");
    static const char zSuffixXdg[] = "/th8/root.key";
    static const char zSuffixHome[] = "/.local/share/th8/root.key";
    const char *zSuffix;
    size_t nBase;
    size_t nSuffix;

    if (zBase && *zBase) {
	zSuffix = zSuffixXdg;
	nSuffix = sizeof(zSuffixXdg);
    } else {
	zBase = getenv("HOME");
	if (!zBase || !*zBase) return 0;
	zSuffix = zSuffixHome;
	nSuffix = sizeof(zSuffixHome);
    }
    nBase = th8PosixStrlen(zBase);
    if (nBase + nSuffix > nBuf) return 0;
    memcpy(zBuf, zBase, nBase);
    memcpy(zBuf + nBase, zSuffix, nSuffix);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixEnsureParentDir --
 *
 *	Ensure every parent directory of `zPath` exists, with
 *	permissions `0700` (drwx------) so only the running
 *	user can list the contents.  Equivalent to `mkdir -p
 *	$(dirname zPath)`.  `EEXIST` on any intermediate
 *	component is benign (the directory already exists);
 *	any other `mkdir` errno is fatal.
 *
 *	The strict `0700` mode is the security-critical part:
 *	the managed trust anchor is per-user secret state
 *	(it tracks which keys libunbound trusts) and must not
 *	be readable by other local users on shared systems.
 *
 *	Mutates a private copy of `zPath` rather than `zPath`
 *	itself so the caller's buffer is unchanged.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zPath -- NUL-terminated path whose parent directories
 *		should be created.  The basename of `zPath` is
 *		ignored (only directory components are created).
 *
 * Returns:
 *	1 on success (every parent exists, was newly created,
 *	or already existed); 0 on `mkdir` failure other than
 *	`EEXIST`, or on `zPath` being too long for the scratch
 *	buffer.
 *
 * Side effects:
 *	May create one or more directories on disk.
 *
 *----------------------------------------------------------------------
 */
static int
th8PosixEnsureParentDir(const char *zPath)
{
    char zScratch[4096];
    size_t n = th8PosixStrlen(zPath);
    size_t i;

    if (n + 1 > sizeof(zScratch)) return 0;
    memcpy(zScratch, zPath, n + 1);
    /* Walk forward, creating each `/`-delimited component up to
     * (but not including) the final basename.  Skip the leading
     * `/` so the loop starts at the first real component. */
    for (i = 1; i < n; i++) {
	if (zScratch[i] != '/') continue;
	zScratch[i] = '\0';
	if (mkdir(zScratch, 0700) != 0 && errno != EEXIST) {
	    return 0;
	}
	zScratch[i] = '/';
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixCopyFileContents --
 *
 *	Copy the contents of `zSrc` to `zDst`, creating `zDst`
 *	with mode `0600` (rw-------).  Used by
 *	`th8PosixSetupManagedAnchor` to bootstrap the writable
 *	managed copy from a vendor-supplied static anchor on
 *	the first run after install.
 *
 *	After `open`, the helper calls `fchmod(0600)` to defeat
 *	the process umask -- the file is per-user secret state
 *	and must not inherit a wider mode from the environment.
 *
 *	On any write or read error the partial destination is
 *	`unlink`ed before returning to avoid leaving a
 *	truncated trust-anchor file in place (libunbound would
 *	refuse to parse it and the next resolve would
 *	silently lose validation).
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	zSrc -- NUL-terminated source path (readable).
 *	zDst -- NUL-terminated destination path (will be
 *		created or replaced).
 *
 * Returns:
 *	1 on success; 0 on any `open` / `read` / `write` /
 *	`fchmod` failure.
 *
 * Side effects:
 *	Creates / overwrites `zDst`.  On failure, removes any
 *	partial `zDst`.
 *
 *----------------------------------------------------------------------
 */
static int
th8PosixCopyFileContents(const char *zSrc, const char *zDst)
{
    int fdSrc = -1;
    int fdDst = -1;
    char zBuf[8192];
    ssize_t nRead;
    int bOk = 0;

    fdSrc = open(zSrc, O_RDONLY);
    if (fdSrc < 0) return 0;
    fdDst = open(zDst, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fdDst < 0) goto done;
    /* Defeat the process umask -- the file must end up 0600 even
     * if the inherited umask would have widened the mode. */
    if (fchmod(fdDst, 0600) != 0) goto done;
    while ((nRead = read(fdSrc, zBuf, sizeof(zBuf))) > 0) {
	ssize_t nWrote = 0;
	while (nWrote < nRead) {
	    ssize_t n = write(fdDst, zBuf + nWrote, (size_t)(nRead - nWrote));
	    if (n < 0) {
		if (errno == EINTR) continue;
		goto done;
	    }
	    nWrote += n;
	}
    }
    if (nRead < 0) goto done;
    bOk = 1;

done:
    if (fdSrc >= 0) close(fdSrc);
    if (fdDst >= 0) close(fdDst);
    if (!bOk) (void)unlink(zDst);
    return bOk;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixUnboundOps --
 *
 *	Static `Th8_UnboundOps` instance handed to every call
 *	into the shared `th8_unbound.c` driver.  Populates the
 *	platform-operations vtable with the POSIX-specific
 *	helpers defined above.
 *
 *	See `src/th8_unbound.h` for the per-field contract.
 *
 *----------------------------------------------------------------------
 */
static const Th8_UnboundOps th8PosixUnboundOps = {
    th8PosixFindStaticAnchorPath, th8PosixGetManagedAnchorPath,
    th8PosixPathReadable,         th8PosixEnsureParentDir,
    th8PosixCopyFileContents,
};

/*
 *----------------------------------------------------------------------
 *
 * th8PosixDnsResolve --
 *
 *	`Th8_Platform.xDnsResolve` callback.  Thin POSIX-side
 *	adapter -- the entire libunbound integration
 *	(hardening, trust-anchor selection, RFC 5011 auto-roll
 *	lifecycle, synchronous resolve, result wrapping) lives
 *	in `src/th8_unbound.c`.  This function exists only to
 *	bind the platform-callback signature to the shared
 *	driver and to supply the POSIX `Th8_UnboundOps` vtable.
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
th8PosixDnsResolve(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    int eType,
    Th8_DnsResult **ppResult)
{
    (void)pCtx;
    return th8UnboundResolve(
        interp, &th8PosixUnboundOps, zName, nName, eType, ppResult);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixDnsResolveFree --
 *
 *	`Th8_Platform.xDnsResolveFree` callback.  Thin POSIX-
 *	side adapter that forwards to the shared
 *	`th8UnboundResolveFree`.  The teardown logic does not
 *	depend on any POSIX-specific behaviour, so no vtable
 *	is needed here.
 *
 *	Gated on `TH8_ENABLE_UNBOUND`.
 *
 * Parameters:
 *	interp  -- live interpreter (used for `Th8_Free`).
 *	pCtx    -- unused platform context.
 *	pResult -- result returned by `th8PosixDnsResolve`,
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
th8PosixDnsResolveFree(Th8_Interp *interp, void *pCtx, Th8_DnsResult *pResult)
{
    (void)pCtx;
    th8UnboundResolveFree(interp, pResult);
}
#  endif /* TH8_ENABLE_UNBOUND */


/*
 *----------------------------------------------------------------------
 *
 * th8PosixRandomBytes --
 *
 *	Implements the Th8_Platform.xRandomBytes callback.  Fills
 *	a buffer with random bytes from /dev/urandom.
 *
 *	Falls back to th8PosixFillPrng (non-zero pseudo-random
 *	bytes) when /dev/urandom is unavailable or read fails.
 *	Never fills with zeros.
 *
 * Why / How:
 *	Hash table seeding, nonce generation, and security tokens
 *	all require high-quality random bytes.  /dev/urandom is the
 *	standard POSIX source of cryptographic randomness.  The read
 *	loop handles EINTR and short reads.  If /dev/urandom cannot
 *	be opened or produces a short read, the remaining bytes are
 *	filled via the LCG fallback, which is preferable to leaving
 *	the buffer zero-filled (which would produce predictable
 *	hash collisions and weak tokens).  Under TH8_FUZZ_STANDALONE,
 *	the LCG is used exclusively for reproducible fuzzer runs.
 *
 * Results:
 *	TH8_OK (always succeeds due to the fallback).
 *
 * Side effects:
 *	Opens and closes /dev/urandom (non-fuzz mode).
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixRandomBytes(Th8_Interp *interp, void *pCtx, void *pBuf, size_t nByte)
{
    (void)interp;
    (void)pCtx;

#  if defined(TH8_FUZZ_STANDALONE)
    th8PosixFillPrng(pBuf, nByte);
    return TH8_OK;
#  else
    {
	int fd;
	ssize_t nRead;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
	    TH8_TRACE_ERR(
	        NULL, "open /dev/urandom failed, using PRNG fallback");
	    th8PosixFillPrng(pBuf, nByte);
	    return TH8_OK;
	}
	{
	    unsigned char *pRd = (unsigned char *)pBuf;
	    size_t nLeft = nByte;

	    while (nLeft > 0) {
		nRead = POSIX_CALL(
		    TH8_POSIX_OP_RANDOM_READ, read(fd, pRd, nLeft));
		if (nRead > 0) {
		    pRd += nRead;
		    nLeft -= (size_t)nRead;
		} else if (nRead < 0 && errno == EINTR) {
		    continue;
		} else {
		    break;
		}
	    }
	    if (nLeft > 0) {
		TH8_TRACE_ERR(
		    NULL, "read /dev/urandom short, using PRNG fallback");
		th8PosixFillPrng(pRd, nLeft);
	    }
	}
	close(fd);
	return TH8_OK;
    }
#  endif /* TH8_FUZZ_STANDALONE */
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixTimeMs --
 *
 *	Implements the Th8_Platform.xTimeMs callback.  Returns the
 *	current wall-clock time as milliseconds since the Unix epoch.
 *
 * Why / How:
 *	The [clock] command and interpreter timeout logic need a
 *	millisecond-resolution wall-clock timestamp.  Uses POSIX
 *	gettimeofday(2) which is universally available on all
 *	target platforms.  Converts seconds * 1000 + microseconds
 *	/ 1000 into a single th8_int64_t value.
 *
 * Results:
 *	TH8_OK on success with *pMs set; TH8_ERROR if
 *	gettimeofday fails (extremely rare).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixTimeMs(Th8_Interp *interp, void *pCtx, th8_int64_t *pMs)
{
    struct timeval tv;

    (void)interp;
    (void)pCtx;

    if (gettimeofday(&tv, 0) != 0) {
	TH8_TRACE_ERR(NULL, "gettimeofday failed");
	*pMs = 0;
	return TH8_ERROR;
    }
    *pMs = (th8_int64_t)tv.tv_sec * 1000 + (th8_int64_t)tv.tv_usec / 1000;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixTimeUs --
 *
 *	Implements the Th8_Platform.xTimeUs callback.  Returns
 *	monotonic time in microseconds.
 *
 *	Uses CLOCK_MONOTONIC (nanosecond precision, immune to wall-
 *	clock adjustments) where available, with gettimeofday as a
 *	fallback (microsecond precision, wall-clock).
 *
 * Why / How:
 *	Performance measurement and elapsed-time calculations need
 *	a monotonic clock that is unaffected by NTP adjustments and
 *	leap seconds.  CLOCK_MONOTONIC provides this on Linux and
 *	modern BSDs.  The gettimeofday fallback handles older
 *	systems where CLOCK_MONOTONIC may not be defined.
 *	Microsecond resolution is sufficient for all TH8 timing
 *	uses and avoids the complexity of nanosecond overflow
 *	handling.
 *
 * Results:
 *	TH8_OK on success with *pUs set; TH8_ERROR if both
 *	clock_gettime and gettimeofday fail.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixTimeUs(Th8_Interp *interp, void *pCtx, th8_int64_t *pUs)
{
    (void)interp;
    (void)pCtx;

#  if defined(CLOCK_MONOTONIC)
    {
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
	    *pUs = (th8_int64_t)ts.tv_sec * 1000000 +
	           (th8_int64_t)ts.tv_nsec / 1000;
	    return TH8_OK;
	}
    }
#  endif
    /* Fallback: gettimeofday (microsecond precision). */
    {
	struct timeval tv;

	if (gettimeofday(&tv, 0) != 0) {
	    TH8_TRACE_ERR(NULL, "gettimeofday failed");
	    *pUs = 0;
	    return TH8_ERROR;
	}
	*pUs = (th8_int64_t)tv.tv_sec * 1000000 + (th8_int64_t)tv.tv_usec;
	return TH8_OK;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixSleep --
 *
 *	Implements the Th8_Platform.xSleep callback.  Sleeps for
 *	the specified number of milliseconds.  Resumes on EINTR
 *	(signal interruption) for the remaining time.
 *
 * Why / How:
 *	The [after] command and test harness need a platform sleep
 *	primitive.  Uses POSIX nanosleep(2) which provides the
 *	remaining time in the rem parameter when interrupted by a
 *	signal, allowing the loop to resume for exactly the
 *	remainder.  Negative values are silently ignored.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Blocks the calling thread for approximately nMs milliseconds.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixSleep(Th8_Interp *interp, void *pCtx, int nMs)
{
    struct timespec req, rem;

    (void)interp;
    (void)pCtx;

    if (nMs < 0) return;

    req.tv_sec = nMs / 1000;
    req.tv_nsec = (long)(nMs % 1000) * 1000000L;

    while (nanosleep(&req, &rem) != 0) {
	if (errno != EINTR) break;
	req = rem;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetUserName --
 *
 *	Implements the Th8_Platform.xGetUserName callback.  Returns
 *	the name of the current user.
 *
 * Why / How:
 *	The [info user] command and ::tcl_platform(user) variable
 *	expose the current user name to scripts.  Uses getpwuid(3)
 *	with getuid() for the authoritative answer from the system
 *	password database.  Falls back to the $USER environment
 *	variable if the password database lookup fails (e.g., in
 *	containers with no /etc/passwd).  Truncates to nBuf-1 to
 *	guarantee NUL termination.
 *
 * Results:
 *	TH8_OK always.  zBuf is filled with the user name or an
 *	empty string if unknown.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetUserName(Th8_Interp *interp, void *pCtx, char *zBuf, size_t nBuf)
{
    const char *zUser = 0;
    struct passwd *pw;

    (void)interp;
    (void)pCtx;
    pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
	zUser = pw->pw_name;
    }
    if (!zUser) {
	zUser = getenv("USER");
    }
    if (zUser) {
	size_t n = th8PosixStrlen(zUser);
	if (n >= nBuf) n = nBuf - 1;
	memcpy(zBuf, zUser, n);
	zBuf[n] = 0;
    } else {
	zBuf[0] = 0;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetHostName --
 *
 *	Implements the Th8_Platform.xGetHostName callback.  Returns
 *	the system host name.
 *
 * Why / How:
 *	The ::tcl_platform(host) variable and [info hostname] command
 *	expose the host name to scripts.  Uses POSIX gethostname(2).
 *	The buffer is explicitly NUL-terminated at nBuf-1 to handle
 *	the case where gethostname does not NUL-terminate when the
 *	name exactly fills the buffer (POSIX permits this).
 *
 * Results:
 *	TH8_OK always.  zBuf is filled with the host name or an
 *	empty string on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetHostName(Th8_Interp *interp, void *pCtx, char *zBuf, size_t nBuf)
{
    (void)interp;
    (void)pCtx;
    if (gethostname(zBuf, nBuf) == 0) {
	zBuf[nBuf - 1] = '\0';
    } else {
	TH8_TRACE_ERR(NULL, "gethostname failed");
	zBuf[0] = 0;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetEnv --
 *
 *	Implements the Th8_Platform.xGetEnv callback.  Return the
 *	value of an environment variable as an allocated copy.
 *	Uses POSIX getenv() and copies the result into a buffer
 *	allocated via Th8_AttemptMalloc (or the platform's xMalloc
 *	directly when interp is NULL).
 *
 * Why / How:
 *	The [info env] command and ::env array need to read
 *	environment variables.  The POSIX getenv(3) return value
 *	points into the process environment, which can be mutated
 *	by setenv/putenv from other threads.  Copying into a
 *	Th8_AttemptMalloc'd buffer makes the result stable and
 *	freeable by the caller via Th8_Free.  The pre-interp path
 *	(interp==NULL) uses the global platform's xMalloc directly
 *	under the global mutex, which is needed during th8sh
 *	argument parsing before any interpreter exists.
 *
 * Results:
 *	Allocated copy of the variable value, or NULL if the
 *	variable does not exist or allocation fails.
 *
 * Side effects:
 *	Allocates memory (caller frees via Th8_Free).  The
 *	pre-interp path acquires the global mutex.
 *
 *----------------------------------------------------------------------
 */

static char *
th8PosixGetEnv(Th8_Interp *interp, void *pCtx, const char *zName)
{
    const char *zVal;
    size_t n;
    char *zCopy;

    (void)pCtx;

    if (!zName) return NULL;
    zVal = getenv(zName);
    if (!zVal) return NULL;

    n = th8PosixStrlen(zVal);
    if (interp) {
	zCopy = (char *)TH8_ALLOC_STR(interp, n);
    } else {
	/*
	 * Pre-interp path: use the global platform's xMalloc
	 * directly (e.g., during th8sh argument parsing).
	 * Protected by the global mutex.
	 */
	void *(*xMal)(Th8_Interp *, void *, size_t) = NULL;
	void *pMalCtx = NULL;

	th8MaybeGlobalMutexEnter(NULL);
	if (th8GlobalPlatform.xMalloc) {
	    xMal = th8GlobalPlatform.xMalloc;
	    pMalCtx = th8GlobalPlatform.pCtx;
	}
	th8MaybeGlobalMutexLeave(NULL);

	if (!xMal) return NULL;
	zCopy = (char *)xMal(NULL, pMalCtx, n + 1);
    }
    if (zCopy) {
	memcpy(zCopy, zVal, n + 1);
    }
    return zCopy;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetLastError --
 *
 *	Implements the Th8_Platform.xGetLastError callback.  Return
 *	the current per-thread error code.
 *
 * Why / How:
 *	On POSIX, the "last error" is the thread-local errno
 *	variable.  This trivial wrapper lets the interpreter read
 *	errno across platform callback boundaries without directly
 *	referencing the POSIX errno macro.  This abstraction is
 *	essential because Win32 uses GetLastError instead of errno.
 *
 * Results:
 *	Returns the current errno value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetLastError(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
    return errno;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixSetLastError --
 *
 *	See th8PosixGetLastError above.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixSetLastError(Th8_Interp *interp, void *pCtx, int nErr)
{
    (void)interp;
    (void)pCtx;
    errno = nErr;
}


/*
 *----------------------------------------------------------------------
 *
 * I/O channel redirection.
 *
 *	Static per-process channel overrides.  NULL means "use the
 *	default" (stdin, stdout, stderr).  For per-interpreter
 *	channels, the embedder should provide custom callbacks with
 *	a pCtx struct that holds per-interpreter state.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetRealPath --
 *
 *	Implements the Th8_Platform.xGetRealPath callback.  Resolve
 *	a path to its canonical absolute form using the OS working
 *	directory.  No base-path restrictions.
 *
 * Why / How:
 *	Internal callers (e.g., base-path initialization, SetCwd)
 *	need the OS-level canonical path with all symlinks resolved.
 *	Uses POSIX realpath(3) for existing paths.  When the path
 *	does not exist (realpath fails), falls back to manually
 *	prepending the current working directory for relative paths.
 *	This callback does NOT enforce sandbox restrictions -- it is
 *	a raw OS wrapper; sandbox enforcement is done by the callers.
 *
 * Results:
 *	TH8_OK with the resolved path copied into zBuf; TH8_ERROR
 *	if the path cannot be resolved or exceeds the buffer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetRealPath(
    Th8_Interp *interp,
    void *pCtx,
    const char *zPath,
    size_t nPath,
    char *zBuf,
    size_t nBuf)
{
    char zCopy[4096];
    char zResolved[4096];

    (void)interp;
    (void)pCtx;

    if (!zPath || !zBuf || nBuf == 0) {
	TH8_TRACE_ERR(NULL, "invalid arguments");
	return TH8_ERROR;
    }
    if (nPath == (size_t)-1) nPath = th8PosixStrlen(zPath);
    if (nPath >= sizeof(zCopy)) {
	TH8_TRACE_ERR(NULL, "path too long");
	return TH8_ERROR;
    }

    memcpy(zCopy, zPath, nPath);
    zCopy[nPath] = '\0';

    if (realpath(zCopy, zResolved)) {
	size_t n = th8PosixStrlen(zResolved);
	if (n + 1 > nBuf) {
	    TH8_TRACE_ERR(NULL, "resolved path too long for buffer");
	    return TH8_ERROR;
	}
	memcpy(zBuf, zResolved, n + 1);
	return TH8_OK;
    }

    TH8_TRACE_ERR(NULL, "realpath failed");

    /*
     * realpath failed (file may not exist yet).  If the path is
     * relative, manually prepend the current working directory.
     */
    if (zCopy[0] != '/') {
	char zCwd[4096];

	if (getcwd(zCwd, sizeof(zCwd))) {
	    size_t nCwd = th8PosixStrlen(zCwd);

	    if (nCwd + 1 + nPath + 1 <= nBuf) {
		memcpy(zBuf, zCwd, nCwd);
		zBuf[nCwd] = '/';
		memcpy(zBuf + nCwd + 1, zCopy, nPath);
		zBuf[nCwd + 1 + nPath] = '\0';
		return TH8_OK;
	    }
	}
    }

    TH8_TRACE_ERR(NULL, "could not resolve path");
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetRootPath --
 *
 *	Implements the Th8_Platform.xGetRootPath callback.  Return
 *	the logical root for a given path within the sandbox.
 *
 * Why / How:
 *	Scripts need a portable way to determine the root of the
 *	file namespace (e.g., for [file pathtype]).  Rather than
 *	exposing the real filesystem root "/", this callback
 *	returns "." if the path is under the base directory, or ""
 *	if the path is outside the sandbox.  This ensures scripts
 *	cannot discover the absolute location of the TH8 sandbox
 *	on the host filesystem, reinforcing the sandboxing model.
 *
 * Results:
 *	TH8_OK always.  zBuf is filled with "." or "".
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetRootPath(
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

    if (!zBuf || nBuf < 2) {
	TH8_TRACE_ERR(NULL, "invalid arguments");
	return TH8_ERROR;
    }
    if (!zPath) {
	zBuf[0] = '\0';
	return TH8_OK;
    }
    if (nPath == (size_t)-1) nPath = th8PosixStrlen(zPath);
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
    if (th8PosixIsPathUnderBase(zCopy)) {
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
 * th8PosixSameFile --
 *
 *	Implements the Th8_Platform.xSameFile callback.  Test whether
 *	two paths refer to the same physical file by comparing
 *	st_dev and st_ino from stat().
 *
 * Why / How:
 *	The interpreter needs to detect when two different path
 *	strings refer to the same underlying file (e.g., via
 *	hard links or different relative paths).  Comparing inodes
 *	is the only reliable method on POSIX -- string comparison
 *	fails for hard links and different path representations.
 *	Both paths are validated against the base directory; foreign
 *	paths silently return "not same" to prevent cross-sandbox
 *	information leakage.
 *
 * Results:
 *	Non-zero if both paths exist, are under the base directory,
 *	and refer to the same inode on the same device.  Zero
 *	otherwise.
 *
 * Side effects:
 *	Calls stat(2) on both paths.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixSameFile(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName1,
    size_t nName1,
    const char *zName2,
    size_t nName2)
{
    char zCopy1[4096];
    char zCopy2[4096];
    struct stat st1;
    struct stat st2;

    (void)interp;
    (void)pCtx;

    if (!zName1 || !zName2) return 0;
    if (nName1 == (size_t)-1) nName1 = th8PosixStrlen(zName1);
    if (nName2 == (size_t)-1) nName2 = th8PosixStrlen(zName2);
    if (nName1 >= sizeof(zCopy1)) return 0;
    if (nName2 >= sizeof(zCopy2)) return 0;

    memcpy(zCopy1, zName1, nName1);
    zCopy1[nName1] = '\0';
    memcpy(zCopy2, zName2, nName2);
    zCopy2[nName2] = '\0';

    /*
     * Both paths must be under the base directory.
     */
    if (!th8PosixIsPathUnderBase(zCopy1)) return 0;
    if (!th8PosixIsPathUnderBase(zCopy2)) return 0;

    if (stat(zCopy1, &st1) != 0) return 0;
    if (stat(zCopy2, &st2) != 0) return 0;

    return (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetTemporaryData --
 *
 *	Implements the Th8_Platform.xGetTemporaryData callback.
 *	Create a temporary file via mkstemp(3), pre-allocate it
 *	to the requested size using ftruncate(2), and return the
 *	OS path and file descriptor.
 *
 * Why / How:
 *	Cryptographic signature verification and binary certificate
 *	handling need scratch storage that is automatically cleaned
 *	up.  mkstemp(3) creates the file with a unique name in /tmp
 *	and opens it atomically (no TOCTOU race).  ftruncate
 *	pre-allocates the space so subsequent writes cannot fail
 *	with ENOSPC.  The file descriptor is returned as pChannel
 *	via TH8_INT2PTR for later I/O via xChannelControl.
 *
 * Results:
 *	TH8_OK on success with *pzOut, *pnOut, and *ppChannel set;
 *	TH8_ERROR on failure.
 *
 * Side effects:
 *	Creates a file in /tmp.  Allocates memory for the path
 *	(caller frees via Th8_Free).
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetTemporaryData(
    Th8_Interp *interp,
    void *pCtx,
    size_t nSize,
    char **pzOut,
    size_t *pnOut,
    void **ppChannel)
{
    char zTemplate[] = "/tmp/th8_XXXXXX";
    int fd;
    char *zPath;
    size_t nPath;

    (void)pCtx;

    fd = mkstemp(zTemplate);
    if (fd < 0) {
	TH8_TRACE_ERR(NULL, "mkstemp failed");
	return TH8_ERROR;
    }

    /* Pre-allocate to the requested size (zero-filled). */
    if (ftruncate(fd, (off_t)nSize) != 0) {
	TH8_TRACE_ERR(NULL, "ftruncate failed");
	close(fd);
	unlink(zTemplate);
	return TH8_ERROR;
    }

    nPath = th8PosixStrlen(zTemplate);
    zPath = (char *)TH8_ALLOC_STR(interp, nPath);
    if (!zPath) {
	TH8_TRACE_ERR(NULL, "out of memory");
	close(fd);
	unlink(zTemplate);
	return TH8_ERROR;
    }
    memcpy(zPath, zTemplate, nPath + 1);

    *pzOut = zPath;
    *pnOut = nPath;
    *ppChannel = TH8_INT2PTR(fd);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixDeleteTemporaryData --
 *
 *	Implements the Th8_Platform.xDeleteTemporaryData callback.
 *	Remove a temporary file by path.
 *
 * Why / How:
 *	After temporary data has been consumed or is no longer
 *	needed, the file must be removed from disk to prevent
 *	leaking sensitive data.  Uses POSIX unlink(2) to remove
 *	the file.  The path is NUL-terminated into a stack buffer
 *	before calling unlink.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the path is invalid.
 *
 * Side effects:
 *	Removes a file from the filesystem.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixDeleteTemporaryData(
    Th8_Interp *interp,
    void *pCtx,
    const char *zPath,
    size_t nPath)
{
    char zBuf[4096];

    (void)interp;
    (void)pCtx;

    if (!zPath || nPath == 0 || nPath >= sizeof(zBuf)) {
	TH8_TRACE_ERR(NULL, "invalid arguments");
	return TH8_ERROR;
    }
    memcpy(zBuf, zPath, nPath);
    zBuf[nPath] = '\0';
    unlink(zBuf);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixChannelControl --
 *
 *	Implements the Th8_Platform.xChannelControl callback.
 *	Performs I/O operations on a POSIX file descriptor.
 *
 * Why / How:
 *	Temporary data and generic file I/O need a unified channel
 *	abstraction.  This callback multiplexes seek, tell, flush,
 *	close, read, write, and open operations through a single
 *	entry point keyed by the TH8_CHANCTL_* operation code.  The
 *	pChannel pointer is converted to an int fd via TH8_PTR2INT.
 *	Write operations retry on EINTR for robustness.  Open
 *	operations create new file descriptors and return them via
 *	pnResult.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure or unrecognized op.
 *
 * Side effects:
 *	Depends on the operation: may seek, read, write, sync, close,
 *	or open a file descriptor.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixChannelControl(
    Th8_Interp *interp,
    void *pCtx,
    void *pChannel,
    int op,
    th8_int64_t nArg1,
    int nArg2,
    th8_int64_t *pnResult,
    void *pBuf)
{
    int fd = (int)TH8_PTR2INT(pChannel);

    (void)interp;
    (void)pCtx;
    /*
     * A NULL pChannel maps to fd 0 (stdin) through TH8_PTR2INT, and a
     * bare `fd < 0` guard would let it through -- a READ would then
     * block forever on an interactive stdin, a WRITE/CLOSE would target
     * the process's standard streams.  Real TH8 channels are temp
     * files with fd >= 3, so reject fd <= 0 (NULL/stdin) as an invalid
     * channel for every op that USES the descriptor.  OPEN is exempt:
     * it creates a brand-new fd from a path and ignores the incoming
     * pChannel, so a NULL/0 fd is expected and valid there.
     */
    if (op != TH8_CHANCTL_OPEN && fd <= 0) {
	TH8_TRACE_ERR(NULL, "invalid channel file descriptor");
	return TH8_ERROR;
    }

    switch (op) {
    case TH8_CHANCTL_SEEK: {
	int whence = SEEK_SET;

	if (nArg2 == 1)
	    whence = SEEK_CUR;
	else if (nArg2 == 2)
	    whence = SEEK_END;
	if (lseek(fd, (off_t)nArg1, whence) == (off_t)-1) {
	    TH8_TRACE_ERR(NULL, "lseek failed");
	    return TH8_ERROR;
	}
    }
	return TH8_OK;

    case TH8_CHANCTL_TELL: {
	off_t pos = lseek(fd, 0, SEEK_CUR);

	if (pnResult) *pnResult = (th8_int64_t)pos;
    }
	return TH8_OK;

    case TH8_CHANCTL_FLUSH:
	fsync(fd);
	return TH8_OK;

    case TH8_CHANCTL_CLOSE:
	close(fd);
	return TH8_OK;

    case TH8_CHANCTL_WRITE: {
	ssize_t nWritten = 0;
	size_t nTotal = (size_t)nArg1;
	const char *p = (const char *)pBuf;

	while (nTotal > 0) {
	    ssize_t n = write(fd, p, nTotal);

	    if (n < 0) {
		if (errno == EINTR) continue;
		TH8_TRACE_ERR(NULL, "write failed in channel");
		return TH8_ERROR;
	    }
	    if (n == 0) {
		/*
		 * write() returning 0 with a non-zero count makes no
		 * forward progress; without this guard `nTotal` would
		 * never decrease and the loop would spin forever.  Treat
		 * a zero-length write as a failure rather than hanging.
		 */
		TH8_TRACE_ERR(NULL, "zero-length write in channel");
		return TH8_ERROR;
	    }
	    p += n;
	    nTotal -= (size_t)n;
	    nWritten += n;
	}
	if (pnResult) *pnResult = (th8_int64_t)nWritten;
    }
	return TH8_OK;

    case TH8_CHANCTL_READ: {
	ssize_t nRead;
	size_t nMax = (size_t)nArg1;

	nRead = read(fd, pBuf, nMax);
	if (nRead < 0) {
	    TH8_TRACE_ERR(NULL, "read failed in channel");
	    if (pnResult) *pnResult = 0;
	    return TH8_ERROR;
	}
	if (pnResult) *pnResult = (th8_int64_t)nRead;
    }
	return TH8_OK;

    case TH8_CHANCTL_OPEN: {
	char zPath[4096];
	int flags;
	int newFd;

	if (!pBuf || nArg1 <= 0 || (size_t)nArg1 >= sizeof(zPath)) {
	    TH8_TRACE_ERR(NULL, "invalid arguments for channel open");
	    return TH8_ERROR;
	}
	memcpy(zPath, pBuf, (size_t)nArg1);
	zPath[(size_t)nArg1] = '\0';

	if (nArg2 == 1) {
	    flags = O_WRONLY | O_CREAT | O_TRUNC;
	} else {
	    flags = O_RDONLY;
	}
	newFd = open(zPath, flags, 0666);
	if (newFd < 0) {
	    TH8_TRACE_ERR(NULL, "open failed in channel");
	    TH8_TRACE_ERR(NULL, zPath);
	    return TH8_ERROR;
	}
	if (pnResult) {
	    *pnResult = (th8_int64_t)newFd;
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
 * th8PosixInput --
 *
 *	Implements the Th8_Platform.xInput callback.  Read one line
 *	from a POSIX file descriptor.  When pChannel is NULL, reads
 *	from STDIN_FILENO.  When non-NULL, interprets pChannel as a
 *	POSIX fd via TH8_PTR2INT.
 *
 *	Reads byte-by-byte until newline, EOF, or buffer limit.
 *	The newline (if present) is included in the output, matching
 *	fgets() semantics.
 *
 * Why / How:
 *	The [gets] command needs to read one line from stdin or a
 *	redirected input channel.  Byte-by-byte reading is necessary
 *	because POSIX read(2) on a pipe/terminal does not have a
 *	"read until newline" mode -- we must detect the newline
 *	ourselves.  A 4096-byte stack buffer avoids allocation for
 *	the read loop; the final result is allocated to its exact
 *	size via Th8_AttemptMalloc so the caller can free it.
 *	EINTR is retried; EOF returns TH8_ERROR to signal end of
 *	input.
 *
 * Results:
 *	TH8_OK with *pzOut and *pnOut set on success; TH8_ERROR
 *	on EOF or failure.
 *
 * Side effects:
 *	Allocates memory for the result (caller frees via Th8_Free).
 *	Reads from the file descriptor.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixInput(
    Th8_Interp *interp, /* Interpreter. */
    void *pCtx, /* Platform's pCtx (unused). */
    char **pzOut,
    size_t *pnOut,
    void *pChannel) /* POSIX fd (or NULL for stdin). */
{
    char zBuf[4096];
    char *zResult;
    size_t nRead = 0;
    int fd;

    (void)pCtx;

    if (!interp) {
	TH8_TRACE_ERR(NULL, "NULL interp");
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    fd = pChannel ? (int)TH8_PTR2INT(pChannel) : STDIN_FILENO;

    while (nRead < sizeof(zBuf) - 1) {
	ssize_t n = read(fd, &zBuf[nRead], 1);

	if (n < 0) {
	    if (errno == EINTR) continue;
	    break;
	}
	if (n == 0) break; /* EOF */
	nRead++;
	if (zBuf[nRead - 1] == '\n') break;
    }

    if (nRead == 0) {
	TH8_TRACE_ERR(NULL, "no bytes read");
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }

    zResult = (char *)TH8_ALLOC_STR(interp, nRead);
    if (!zResult) {
	TH8_TRACE_ERR(NULL, "out of memory");
	*pzOut = 0;
	*pnOut = 0;
	return TH8_ERROR;
    }
    memcpy(zResult, zBuf, nRead);
    zResult[nRead] = '\0';
    *pzOut = zResult;
    *pnOut = nRead;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixOutput --
 *
 *	Implements the Th8_Platform.xOutput callback.  Write data to
 *	a POSIX file descriptor.  When pChannel is NULL, writes to
 *	STDOUT_FILENO.  When non-NULL, interprets pChannel as a
 *	POSIX fd via TH8_PTR2INT.
 *
 *	Retries on EINTR.  No user-space buffering; write() sends
 *	data directly to the kernel.
 *
 * Why / How:
 *	The [puts] command needs to write to stdout or a redirected
 *	output channel.  Uses a loop to handle short writes and
 *	EINTR, ensuring all bytes are delivered.  Unbuffered I/O
 *	is appropriate because TH8 scripts typically produce small
 *	amounts of output and buffering would introduce
 *	nondeterministic flushing behavior.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on write failure.
 *
 * Side effects:
 *	Writes to the file descriptor.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixOutput(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx, /* Platform's pCtx (unused). */
    const char *z,
    size_t n,
    void *pChannel) /* POSIX fd (or NULL for stdout). */
{
    int fd = pChannel ? (int)TH8_PTR2INT(pChannel) : STDOUT_FILENO;

    (void)interp;
    (void)pCtx;

    while (n > 0) {
	ssize_t nWritten = write(fd, z, n);

	if (nWritten < 0) {
	    if (errno == EINTR) continue;
	    TH8_TRACE_ERR(NULL, "write failed");
	    return TH8_ERROR;
	}
	z += nWritten;
	n -= (size_t)nWritten;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixOutputError --
 *
 *	Implements the Th8_Platform.xOutputError callback.  Write
 *	data to a POSIX file descriptor.  When pChannel is NULL,
 *	writes to STDERR_FILENO.  When non-NULL, interprets
 *	pChannel as a POSIX fd via TH8_PTR2INT.
 *
 * Why / How:
 *	Error messages (e.g., from [error], uncaught exceptions,
 *	and diagnostics) must go to a separate channel from normal
 *	output so they are not intermixed with [puts] data.  Uses
 *	the same robust write loop as th8PosixOutput, but defaults
 *	to STDERR_FILENO instead of STDOUT_FILENO.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on write failure.
 *
 * Side effects:
 *	Writes to the file descriptor.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixOutputError(
    Th8_Interp *interp, /* Interpreter (unused). */
    void *pCtx, /* Platform's pCtx (unused). */
    const char *z,
    size_t n,
    void *pChannel) /* POSIX fd (or NULL for stderr). */
{
    int fd = pChannel ? (int)TH8_PTR2INT(pChannel) : STDERR_FILENO;

    (void)interp;
    (void)pCtx;

    while (n > 0) {
	ssize_t nWritten = write(fd, z, n);

	if (nWritten < 0) {
	    if (errno == EINTR) continue;
	    TH8_TRACE_ERR(NULL, "write failed");
	    return TH8_ERROR;
	}
	z += nWritten;
	n -= (size_t)nWritten;
    }
    return TH8_OK;
}


static void *th8PosixInputChannel = 0;
static void *th8PosixOutputChannel = 0;
static void *th8PosixErrorChannel = 0;

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetInput --
 *
 *	Implements the Th8_Platform.xGetInput callback.  Return the
 *	current input channel override.
 *
 * Why / How:
 *	Per-process channel redirection allows the embedder to
 *	redirect stdin/stdout/stderr at the platform level.  A NULL
 *	channel means "use the default" (the real STDIN_FILENO).
 *	The value is stored in a static variable because channel
 *	redirection is process-global on POSIX (file descriptors
 *	are per-process, not per-interpreter).
 *
 * Results:
 *	TH8_OK always.  *pChannel is set to the current override
 *	(or NULL for the default).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetInput(Th8_Interp *interp, void *pCtx, void **pChannel)
{
    (void)interp;
    (void)pCtx;
    *pChannel = th8PosixInputChannel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixSetInput --
 *
 *	Implements the Th8_Platform.xSetInput callback.  Set the
 *	input channel override for the process.
 *
 * Why / How:
 *	See th8PosixGetInput.  Stores the new channel pointer in
 *	the static override variable.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	Modifies the process-global input channel override.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixSetInput(Th8_Interp *interp, void *pCtx, void *pChannel)
{
    (void)interp;
    (void)pCtx;
    th8PosixInputChannel = pChannel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetOutput --
 *
 *	Implements the Th8_Platform.xGetOutput callback.  Return the
 *	current output channel override.
 *
 * Why / How:
 *	See th8PosixGetInput.  Same pattern for the stdout channel.
 *
 * Results:
 *	TH8_OK always.  *pChannel is set to the current override
 *	(or NULL for the default).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetOutput(Th8_Interp *interp, void *pCtx, void **pChannel)
{
    (void)interp;
    (void)pCtx;
    *pChannel = th8PosixOutputChannel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixSetOutput --
 *
 *	Implements the Th8_Platform.xSetOutput callback.  Set the
 *	output channel override for the process.
 *
 * Why / How:
 *	See th8PosixGetInput.  Same pattern for the stdout channel.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	Modifies the process-global output channel override.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixSetOutput(Th8_Interp *interp, void *pCtx, void *pChannel)
{
    (void)interp;
    (void)pCtx;
    th8PosixOutputChannel = pChannel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetErrorOutput --
 *
 *	Implements the Th8_Platform.xGetErrorOutput callback.  Return
 *	the current error output channel override.
 *
 * Why / How:
 *	See th8PosixGetInput.  Same pattern for the stderr channel.
 *
 * Results:
 *	TH8_OK always.  *pChannel is set to the current override
 *	(or NULL for the default).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetErrorOutput(Th8_Interp *interp, void *pCtx, void **pChannel)
{
    (void)interp;
    (void)pCtx;
    *pChannel = th8PosixErrorChannel;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixSetErrorOutput --
 *
 *	Implements the Th8_Platform.xSetErrorOutput callback.  Set
 *	the error output channel override for the process.
 *
 * Why / How:
 *	See th8PosixGetInput.  Same pattern for the stderr channel.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	Modifies the process-global error output channel override.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixSetErrorOutput(Th8_Interp *interp, void *pCtx, void *pChannel)
{
    (void)interp;
    (void)pCtx;
    th8PosixErrorChannel = pChannel;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetPid --
 *
 *	Implements the Th8_Platform.xGetPid callback.  Return the
 *	current process ID.
 *
 * Why / How:
 *	The [pid] command and ::tcl_platform(pid) expose the process
 *	ID to scripts.  Uses POSIX getpid(2), which always succeeds.
 *
 * Results:
 *	The process ID as an int.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetPid(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    return (int)getpid();
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetParentPid --
 *
 *	Implements the Th8_Platform.xGetParentPid callback.  Return
 *	the parent process ID.
 *
 * Why / How:
 *	Scripts may need to detect whether they are running as a
 *	child process (e.g., for daemon detection or process tree
 *	analysis).  Uses POSIX getppid(2), which always succeeds.
 *
 * Results:
 *	The parent process ID as an int.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetParentPid(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    return (int)getppid();
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetThreadId --
 *
 *	Implements the Th8_Platform.xGetThreadId callback.  Return
 *	an opaque identifier for the current thread.
 *
 * Why / How:
 *	The interpreter uses thread IDs for ownership assertions
 *	and diagnostic messages.  Uses POSIX pthread_self(), which
 *	always succeeds and returns a value unique among active
 *	threads.  Cast to th8_uint64_t for a uniform integer
 *	representation across platforms.
 *
 * Results:
 *	The current thread ID as th8_uint64_t.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8PosixGetThreadId(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    return (th8_uint64_t)pthread_self();
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetStackBounds --
 *
 *	Implements the Th8_Platform.xGetStackBounds callback.  Return
 *	the base address and size of the current thread's stack.
 *
 *	macOS: pthread_get_stackaddr_np / pthread_get_stacksize_np
 *	Linux/BSD: pthread_attr_getstack
 *
 * Why / How:
 *	The interpreter's recursion-depth checker uses the stack
 *	bounds to detect imminent stack overflow and return an error
 *	rather than crashing.  The API is fragmented across POSIX
 *	platforms: macOS provides non-portable _np extensions,
 *	Linux requires pthread_getattr_np, and BSDs use the
 *	standard pthread_attr_getstack.  On Linux/BSD,
 *	pthread_attr_getstack returns the lowest address; ppBase
 *	is computed as pAddr + nSize (the highest address) for
 *	consistency with the macOS API and the interpreter's
 *	downward-growing-stack assumption.  The getrlimit fallback
 *	handles unknown POSIX platforms with approximate bounds.
 *
 * Results:
 *	TH8_OK with *ppBase and *pnSize set; TH8_ERROR if the
 *	stack bounds cannot be determined.
 *
 * Side effects:
 *	May allocate and destroy a pthread_attr_t.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixGetStackBounds(
    Th8_Interp *interp,
    void *pCtx,
    void **ppBase,
    size_t *pnSize)
{
    (void)interp;
    (void)pCtx;

#  if defined(__APPLE__)
    {
	pthread_t self = pthread_self();

	*ppBase = pthread_get_stackaddr_np(self);
	*pnSize = pthread_get_stacksize_np(self);
	return TH8_OK;
    }
#  elif defined(__linux__) || defined(__FreeBSD__) ||                        \
      defined(__OpenBSD__) || defined(__NetBSD__)
    {
	pthread_attr_t attr;
	void *pAddr;
	size_t nSize;

	if (pthread_attr_init(&attr) != 0) {
	    TH8_TRACE_ERR(NULL, "pthread_attr_init failed");
	    return TH8_ERROR;
	}
#    if defined(__linux__)
	if (pthread_getattr_np(pthread_self(), &attr) != 0) {
	    TH8_TRACE_ERR(NULL, "pthread_getattr_np failed");
	    pthread_attr_destroy(&attr);
	    return TH8_ERROR;
	}
#    endif
	if (pthread_attr_getstack(&attr, &pAddr, &nSize) != 0) {
	    TH8_TRACE_ERR(NULL, "pthread_attr_getstack failed");
	    pthread_attr_destroy(&attr);
	    return TH8_ERROR;
	}
	/*
	 * pthread_attr_getstack returns the LOWEST address.
	 * The base (highest address) is pAddr + nSize.
	 */

	*ppBase = (char *)pAddr + nSize;
	*pnSize = nSize;
	pthread_attr_destroy(&attr);
	return TH8_OK;
    }
#  else
    /*
     * Unknown platform: use getrlimit as fallback.
     */

    {
	struct rlimit rl;

	if (getrlimit(RLIMIT_STACK, &rl) == 0 &&
	    rl.rlim_cur != RLIM_INFINITY) {
	    volatile char marker;

	    *pnSize = (size_t)rl.rlim_cur;
	    *ppBase = (void *)&marker;
	    return TH8_OK;
	}
    }
    TH8_TRACE_ERR(NULL, "could not determine stack bounds");
    return TH8_ERROR;
#  endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixPanic --
 *
 *	Implements the Th8_Platform.xPanic callback.  Write the
 *	panic message to stderr and terminate the process.
 *
 * Why / How:
 *	Fatal internal errors (e.g., corrupted data structures,
 *	failed invariants) must terminate immediately rather than
 *	continuing with undefined behavior.  The message is written
 *	to stderr via th8PosixRobustWrite for signal-safe I/O.
 *	On platforms with SIGTRAP, raise(SIGTRAP) is used instead
 *	of abort() so that a debugger can catch the breakpoint;
 *	otherwise abort() produces a core dump for post-mortem
 *	analysis.
 *
 * Results:
 *	Does not return.
 *
 * Side effects:
 *	Writes to stderr.  Terminates the process.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixPanic(Th8_Interp *interp, void *pCtx, const char *zMsg, size_t nMsg)
{
    (void)interp;
    (void)pCtx;
    if (zMsg && nMsg > 0) {
	th8PosixRobustWrite(STDERR_FILENO, zMsg, nMsg);
	th8PosixRobustWrite(STDERR_FILENO, "\n", 1);
    }
#  if defined(SIGTRAP)
    raise(SIGTRAP);
#  else
    abort();
#  endif
}


/*
 * Static mutex state -- must be declared before the xMutex*
 * callbacks below can reference it.
 */

static pthread_mutex_t th8PosixMutex;
static volatile int th8PosixMutexReady = 0;


/*
 *----------------------------------------------------------------------
 *
 * th8PosixMutexInit --
 *
 *	Implements the Th8_Platform.xMutexInit callback.  Initialize
 *	a Th8_Mutex (which is typedef'd to pthread_mutex_t).
 *
 * Why / How:
 *	The interpreter and the library-handle tracking list need
 *	per-object mutexes for thread safety.  This is a thin
 *	wrapper around pthread_mutex_init with default attributes
 *	(non-recursive, no error checking), which is sufficient
 *	because TH8's locking discipline never requires recursive
 *	acquisition.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes the pthread_mutex_t inside pMutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixMutexInit(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    pthread_mutex_init(pMutex, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixMutexFinal --
 *
 *	Implements the Th8_Platform.xMutexFinal callback.  Destroy
 *	a Th8_Mutex previously initialized by xMutexInit.
 *
 * Why / How:
 *	POSIX requires that a pthread_mutex_t be destroyed when no
 *	longer needed to release any resources the implementation
 *	may have allocated internally.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys the pthread_mutex_t inside pMutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixMutexFinal(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    pthread_mutex_destroy(pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixMutexEnter --
 *
 *	Implements the Th8_Platform.xMutexEnter callback.  Acquire
 *	(lock) the mutex.
 *
 * Why / How:
 *	Wraps pthread_mutex_lock.  Blocks the calling thread until
 *	the mutex is available.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquires the mutex.  May block.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixMutexEnter(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    pthread_mutex_lock(pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixMutexLeave --
 *
 *	Implements the Th8_Platform.xMutexLeave callback.  Release
 *	(unlock) the mutex.
 *
 * Why / How:
 *	Wraps pthread_mutex_unlock.  Must be called from the same
 *	thread that called xMutexEnter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases the mutex.  May unblock other threads.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixMutexLeave(Th8_Interp *interp, void *pCtx, Th8_Mutex *pMutex)
{
    (void)interp;
    (void)pCtx;
    pthread_mutex_unlock(pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixIntCmpXchg --
 *
 *	Implements the Th8_Platform.xIntCmpXchg callback.  Atomic
 *	compare-and-swap on a volatile int.
 *
 * Why / How:
 *	The CAS lazy-init pattern used by th8PosixInitialize and
 *	th8PosixFinalize needs a lock-free atomic compare-and-swap
 *	to safely initialize shared state when multiple threads
 *	race into initialization.  Uses the GCC built-in
 *	__sync_val_compare_and_swap, which emits the appropriate
 *	hardware CAS instruction (e.g., LOCK CMPXCHG on x86,
 *	LDREX/STREX on ARM) and implies a full memory barrier.
 *	Returns the previous value of *pTarget so the caller can
 *	detect whether it won the race.
 *
 * Results:
 *	The value of *pTarget before the swap attempt.
 *
 * Side effects:
 *	Atomically replaces *pTarget with iExchange if *pTarget
 *	equals iComparand.  Implies a full memory barrier.
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixIntCmpXchg(
    Th8_Interp *interp,
    void *pCtx,
    volatile int *pTarget,
    int iExchange,
    int iComparand)
{
    (void)interp;
    (void)pCtx;
    return __sync_val_compare_and_swap(pTarget, iComparand, iExchange);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixIntCmpXchg64 --
 *
 *	64-bit atomic compare-and-swap.  Implements the Th8_Platform
 *	xIntCmpXchg64 callback.
 *
 * Why / How:
 *	The 64-bit sibling of th8PosixIntCmpXchg.  Used to read and
 *	publish the interpreter's owning-thread id (a 64-bit value that
 *	does not fit the 32-bit xIntCmpXchg).  Uses the GCC/Clang
 *	built-in __sync_val_compare_and_swap, which is 64-bit-capable on
 *	all POSIX targets TH8 supports and implies a full memory
 *	barrier.  Returns the previous value of *pTarget so the caller
 *	can detect whether it won the race.
 *
 * Results:
 *	The value of *pTarget before the swap attempt.
 *
 * Side effects:
 *	Atomically replaces *pTarget with iExchange if *pTarget equals
 *	iComparand.  Implies a full memory barrier.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8PosixIntCmpXchg64(
    Th8_Interp *interp,
    void *pCtx,
    volatile th8_uint64_t *pTarget,
    th8_uint64_t iExchange,
    th8_uint64_t iComparand)
{
    (void)interp;
    (void)pCtx;
    return __sync_val_compare_and_swap(pTarget, iComparand, iExchange);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixMemBarrier --
 *
 *	Implements the Th8_Platform.xMemBarrier callback.  Issue a
 *	full memory barrier (acquire + release).
 *
 * Why / How:
 *	After a CAS establishes ownership of shared state, a memory
 *	barrier ensures that subsequent reads see the most recent
 *	writes from other threads (and that our writes are visible
 *	to them).  Uses the GCC built-in __sync_synchronize, which
 *	emits a hardware fence (e.g., MFENCE on x86, DMB on ARM).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Issues a full hardware memory barrier.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixMemBarrier(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;
    __sync_synchronize();
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixEvent* --
 *
 *	Manual-reset event handle, built from a pthread_mutex_t,
 *	a pthread_cond_t, and an int "signaled" flag.  Implements
 *	the Th8_Platform xEvent* callbacks.
 *
 * Why / How:
 *	POSIX has no native manual-reset event primitive -- we
 *	build one from cond+mutex.  Set / Reset toggle the flag
 *	under the lock; Wait spins on `pthread_cond_timedwait`
 *	until the flag is set or the deadline passes.  An early
 *	wake (signal delivery causing pthread_cond_timedwait to
 *	return without the flag being set) maps to xEventWait
 *	returning -1, which the caller treats as a re-poll --
 *	this is the practical analog of Win32 alertable APC
 *	delivery (no native QueueUserAPC equivalent exists on
 *	POSIX, but pthread_kill(target, SIG...) achieves the same
 *	wake-the-wait effect via signal interruption of
 *	cond_timedwait).
 *
 *----------------------------------------------------------------------
 */

#  if !defined(TH8_FUZZ_STANDALONE)
typedef struct th8PosixEvent {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int signaled;
} th8PosixEvent;

/*
 *----------------------------------------------------------------------
 *
 * th8PosixEventCreate --
 *
 *	Allocate a manual-reset event object backed by a POSIX
 *	mutex + condition variable + signaled flag.  Matched
 *	pair with `th8Win32EventCreate`; both share the
 *	`xEventCreate` platform-callback signature.
 *
 *	Manual-reset semantics: once signaled, the event stays
 *	signaled until an explicit `Reset`; subsequent `Wait`
 *	calls return immediately.
 *
 *	On any allocation / init failure the partially-built
 *	event is unwound (mutex destroyed if `cond_init`
 *	failed, then `free(e)`).  The interp pointer is
 *	consulted only to satisfy the platform-callback
 *	contract; no error is emitted into the interpreter
 *	result.
 *
 * Parameters:
 *	interp -- live interpreter (unused beyond the contract).
 *	pCtx   -- platform context (ignored).
 *
 * Returns:
 *	Non-NULL opaque handle on success; NULL on allocation
 *	or pthread init failure.
 *
 * Side effects:
 *	Allocates one `th8PosixEvent` via `malloc`; initialises
 *	a pthread mutex and condition variable.
 *
 *----------------------------------------------------------------------
 */
static void *
th8PosixEventCreate(Th8_Interp *interp, void *pCtx)
{
    th8PosixEvent *e;

    (void)pCtx;
    e = (th8PosixEvent *)malloc(sizeof(*e));
    if (!e) {
	(void)interp;
	return NULL;
    }
    if (pthread_mutex_init(&e->lock, NULL) != 0) {
	free(e);
	return NULL;
    }
    if (pthread_cond_init(&e->cond, NULL) != 0) {
	pthread_mutex_destroy(&e->lock);
	free(e);
	return NULL;
    }
    e->signaled = 0;
    return (void *)e;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixEventDestroy --
 *
 *	Tear down a manual-reset event created by
 *	`th8PosixEventCreate`: destroy the pthread cond and
 *	mutex, then free the wrapper.  NULL `pEvent` is a
 *	no-op so callers do not need a separate "did create
 *	succeed" guard.  Matched pair with
 *	`th8Win32EventDestroy`.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- platform context (ignored).
 *	pEvent -- handle returned by `th8PosixEventCreate`, or
 *		NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Destroys the pthread synchronisation primitives and
 *	`free()`s the wrapper.
 *
 *----------------------------------------------------------------------
 */
static void
th8PosixEventDestroy(Th8_Interp *interp, void *pCtx, void *pEvent)
{
    th8PosixEvent *e = (th8PosixEvent *)pEvent;

    (void)interp;
    (void)pCtx;
    if (!e) return;
    pthread_cond_destroy(&e->cond);
    pthread_mutex_destroy(&e->lock);
    free(e);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixEventSet --
 *
 *	Transition the event to the signaled state and wake
 *	every waiter.  Idempotent: signaling a
 *	currently-signaled event is harmless.  Uses
 *	`pthread_cond_broadcast` so manual-reset semantics
 *	apply -- every `Wait` blocked on this event resumes.
 *	Matched pair with `th8Win32EventSet`.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- platform context (ignored).
 *	pEvent -- handle, or NULL (no-op on NULL).
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Acquires and releases `e->lock`; sets `e->signaled = 1`
 *	and broadcasts on `e->cond`.
 *
 *----------------------------------------------------------------------
 */
static void
th8PosixEventSet(Th8_Interp *interp, void *pCtx, void *pEvent)
{
    th8PosixEvent *e = (th8PosixEvent *)pEvent;

    (void)interp;
    (void)pCtx;
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->signaled = 1;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->lock);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixEventReset --
 *
 *	Transition the event back to the unsignaled state.
 *	Manual-reset events require this explicit call --
 *	`Wait` itself does not auto-reset.  No waiters are
 *	notified (resetting an event has nothing to wake).
 *	Matched pair with `th8Win32EventReset`.
 *
 * Parameters:
 *	interp -- ignored.
 *	pCtx   -- platform context (ignored).
 *	pEvent -- handle, or NULL (no-op on NULL).
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Acquires and releases `e->lock`; sets `e->signaled = 0`.
 *
 *----------------------------------------------------------------------
 */
static void
th8PosixEventReset(Th8_Interp *interp, void *pCtx, void *pEvent)
{
    th8PosixEvent *e = (th8PosixEvent *)pEvent;

    (void)interp;
    (void)pCtx;
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->signaled = 0;
    pthread_mutex_unlock(&e->lock);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixEventWait --
 *
 *	Block the calling thread until the event becomes
 *	signaled or the timeout elapses.  Three timeout modes:
 *	  *  `nTimeoutMs < 0` -- wait forever (interrupted by
 *	     `EINTR` so the loop retries; other pthread errors
 *	     break out as "timeout").
 *	  *  `nTimeoutMs == 0` -- poll: return immediately
 *	     reflecting the current `signaled` state.
 *	  *  `nTimeoutMs > 0` -- bounded wait using
 *	     `pthread_cond_timedwait` against a deadline
 *	     computed from `clock_gettime(CLOCK_REALTIME)`.
 *
 *	Returns 0 on signaled-wake, 1 on timeout (which is
 *	also the NULL / error fallback).  Manual-reset
 *	semantics: the event stays signaled after this call --
 *	the caller is responsible for `Reset` if one-shot
 *	semantics are desired.  Matched pair with
 *	`th8Win32EventWait`.
 *
 * Parameters:
 *	interp      -- ignored.
 *	pCtx        -- platform context (ignored).
 *	pEvent      -- handle, or NULL.
 *	nTimeoutMs  -- timeout in milliseconds; negative for
 *		infinite, zero for poll, positive for bounded.
 *
 * Returns:
 *	0 if the event was signaled before the timeout;
 *	1 otherwise (including NULL `pEvent` and
 *	pthread errors).
 *
 * Side effects:
 *	Acquires and releases `e->lock`; may block on
 *	`e->cond`.
 *
 *----------------------------------------------------------------------
 */
static int
th8PosixEventWait(
    Th8_Interp *interp,
    void *pCtx,
    void *pEvent,
    int nTimeoutMs)
{
    th8PosixEvent *e = (th8PosixEvent *)pEvent;
    int rc = 1; /* default: timeout */

    (void)interp;
    (void)pCtx;
    if (!e) return 1;
    pthread_mutex_lock(&e->lock);
    if (e->signaled) {
	rc = 0;
    } else if (nTimeoutMs < 0) {
	while (!e->signaled) {
	    int r = pthread_cond_wait(&e->cond, &e->lock);
	    if (r != 0 && r != EINTR) {
		break; /* spurious or error -> early-wake */
	    }
	    if (!e->signaled) {
		rc = -1; /* early-wake; let caller re-poll */
		break;
	    }
	}
	if (e->signaled) rc = 0;
    } else {
	struct timespec ts;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec + (nTimeoutMs / 1000);
	ts.tv_nsec = (long)(tv.tv_usec * 1000) +
	             (long)((nTimeoutMs % 1000) * 1000000);
	if (ts.tv_nsec >= 1000000000L) {
	    ts.tv_sec += 1;
	    ts.tv_nsec -= 1000000000L;
	}
	while (!e->signaled) {
	    int r = pthread_cond_timedwait(&e->cond, &e->lock, &ts);
	    if (r == ETIMEDOUT) {
		rc = 1;
		break;
	    }
	    if (r != 0 && r != EINTR) {
		rc = -1;
		break;
	    }
	    if (!e->signaled) {
		rc = -1; /* early-wake; let caller re-poll */
		break;
	    }
	}
	if (e->signaled && rc == 1) rc = 0;
    }
    pthread_mutex_unlock(&e->lock);
    return rc;
}
#  endif /* !TH8_FUZZ_STANDALONE */


/*
 *----------------------------------------------------------------------
 *
 * th8PosixEmitTrace --
 *
 *	Implements the Th8_Platform.xEmitTrace callback.  Emit a
 *	diagnostic trace message via syslog.
 *
 * Why / How:
 *	TH8_TRACE_ERR and other diagnostic macros need a platform-
 *	specific output sink.  On POSIX, syslog(3) is the standard
 *	system logging facility.  Messages are sent at LOG_DEBUG
 *	priority so they can be filtered by the system's syslog
 *	configuration.  Using syslog rather than stderr avoids
 *	intermixing diagnostics with script output.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the system log.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixEmitTrace(Th8_Interp *interp, void *pCtx, const char *zMsg)
{
    (void)interp;
    (void)pCtx;
    if (zMsg) {
	syslog(LOG_DEBUG, "%s", zMsg);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixInitialize --
 *
 *	Implements the Th8_Platform.xInitialize callback.  One-time
 *	platform initialization, called from Th8_Initialize.
 *
 * Why / How:
 *	The process-global platform mutex must be initialized
 *	exactly once, even when multiple threads call Th8_Initialize
 *	concurrently.  Uses the CAS lazy-init pattern: atomically
 *	swap th8PosixMutexReady from 0 to 1.  The thread that wins
 *	the CAS (gets back 0) performs pthread_mutex_init.  If init
 *	fails, the flag is CAS'd back to 0 so a subsequent attempt
 *	can retry.  Threads that lose the CAS (get back 1) skip
 *	initialization -- the mutex is already ready.  This pattern
 *	uses __sync_val_compare_and_swap with its implied memory
 *	barrier, so no separate fence is needed.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if pthread_mutex_init fails.
 *
 * Side effects:
 *	Initializes the process-global pthread_mutex_t (once).
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixInitialize(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    /*
     * Use __sync_val_compare_and_swap to ensure only one thread
     * initializes the mutex, even if multiple threads race into
     * this function concurrently.
     */

    if (__sync_val_compare_and_swap(&th8PosixMutexReady, 0, 1) == 0) {
	if (pthread_mutex_init(&th8PosixMutex, NULL) != 0) {
	    TH8_TRACE_ERR(NULL, "pthread_mutex_init failed");
	    __sync_val_compare_and_swap(&th8PosixMutexReady, 1, 0);
	    return TH8_ERROR;
	}
    }
    return TH8_OK;
}

#  if !defined(TH8_FUZZ_STANDALONE)
/*
 *----------------------------------------------------------------------
 *
 * th8PosixUnloadLibs --
 *
 *	Close all dlopen handles in the per-interpreter library
 *	list and free the associated memory.  Called from
 *	xDeleteInterp to clean up per-interpreter library state.
 *
 * Why / How:
 *	When an interpreter is deleted, any shared libraries it
 *	loaded must be closed to reclaim resources and avoid
 *	leaking dlopen handles.  The list is atomically detached
 *	from the interpreter under the platform mutex, then walked
 *	without holding the mutex (dlclose may take nontrivial
 *	time and must not block other threads).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls dlclose on each library handle.  Frees all list
 *	entries.  Acquires and releases the platform mutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixUnloadLibs(Th8_Interp *interp)
{
    Th8_PosixLibHandle *pList;

    th8PosixLock();
    pList = (Th8_PosixLibHandle *)th8GetPlatformLibs(interp);
    th8SetPlatformLibs(interp, NULL);
    th8PosixUnlock();

    while (pList) {
	Th8_PosixLibHandle *p = pList;

	pList = p->pNext;
	if (p->hLib) {
	    /* _Unload moved to th8PosixPreDeleteInterp (called earlier
	     * in Th8_DeleteInterp, while the interp is still fully
	     * functional).  Here we only dlclose the handle, after the
	     * namespace cleanup has completed and any remaining
	     * library xDel callbacks have run. */
	    dlclose(p->hLib);
	}
	Th8_Free(interp, p->zName);
	Th8_Free(interp, p);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixDeleteInterp --
 *
 *	Implements the Th8_Platform.xDeleteInterp callback.  Notified
 *	when an interpreter is about to be deleted.  Releases all
 *	loaded library handles.
 *
 * Why / How:
 *	The interpreter deletion sequence calls this callback so the
 *	platform layer can release interpreter-specific resources.
 *	On POSIX, the only such resource is the per-interpreter
 *	library handle list maintained by xLoad/xUnload.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls th8PosixUnloadLibs, which dlcloses all libraries.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixDeleteInterp(Th8_Interp *interp, void *pCtx)
{
    (void)pCtx;
    th8PosixUnloadLibs(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixPreDeleteInterp --
 *
 *	Implements the Th8_Platform.xPreDeleteInterp callback.  Called
 *	at the START of Th8_DeleteInterp, while the interpreter is
 *	still fully functional.  Walks the per-interpreter loaded
 *	library list and invokes each library's _Unload entry point
 *	so that libraries can clean up interpreter-resident state via
 *	Th8_Eval, Th8_DeleteMathFunc, namespace deletion, etc.
 *
 * Why / How:
 *	Without this early notification, libraries that publish
 *	process-global state via _Unload (SQLite's allocator caches
 *	and date-time formatter, freed only by sqlite3_shutdown())
 *	leak that state for the rest of the process.  Calling _Unload
 *	from the late xDeleteInterp would crash because by then the
 *	global namespace, packages, and math-func registry have all
 *	been freed -- Th8_Eval would dereference a NULL pGlobalNs.
 *	The dlclose itself stays in xDeleteInterp (called late) so
 *	that any library xDel callbacks invoked during namespace
 *	cleanup can still execute the library's text segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May invoke each loaded library's _Unload entry point, which
 *	can call back into the interpreter (eval, delete commands,
 *	free math funcs, etc.).
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixPreDeleteInterp(Th8_Interp *interp, void *pCtx)
{
    Th8_PosixLibHandle *p;

    (void)pCtx;

    /*
     * The list is read without taking the lock because xDeleteInterp
     * is called when no other thread can be using this interp.  A
     * library that registered a [load] handler during _Unload would
     * still be safe because xLoad checks for the same condition.
     */

    th8PosixLock();
    p = (Th8_PosixLibHandle *)th8GetPlatformLibs(interp);
    th8PosixUnlock();

    while (p) {
	if (p->hLib) {
	    th8PosixCallUnloadProc(
	        interp, p->hLib, p->zName, p->nName,
	        TH8_UNLOAD_DETACH_FROM_PROCESS);
	}
	p = p->pNext;
    }
}

#  endif /* !TH8_FUZZ_STANDALONE */


/*
 *----------------------------------------------------------------------
 *
 * th8PosixFinalize --
 *
 *	Implements the Th8_Platform.xFinalize callback.  Tear down
 *	the process-global platform state.
 *
 * Why / How:
 *	The inverse of th8PosixInitialize.  Uses the same CAS
 *	pattern to ensure only one thread destroys the mutex: swap
 *	th8PosixMutexReady from 1 to 0.  The thread that wins the
 *	CAS (gets back 1) performs pthread_mutex_destroy.  Threads
 *	that lose (get back 0) skip -- the mutex was already
 *	destroyed or never initialized.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys the process-global pthread_mutex_t (once).
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixFinalize(Th8_Interp *interp, void *pCtx)
{
    (void)interp;
    (void)pCtx;

    if (__sync_val_compare_and_swap(&th8PosixMutexReady, 1, 0) == 1) {
	pthread_mutex_destroy(&th8PosixMutex);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixLock --
 *
 *	Acquire the process-global platform mutex.
 *
 * Why / How:
 *	Guards access to the per-interpreter library handle lists,
 *	the base-path state, and the I/O channel overrides.  The
 *	th8PosixMutexReady check is necessary because this function
 *	may be called before th8PosixInitialize (e.g., from
 *	Th8_SetBasePath during early startup) or after
 *	th8PosixFinalize.  In those cases the lock is a no-op,
 *	which is safe because only one thread is active during
 *	startup and shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquires the platform mutex.  May block.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixLock(void)
{
    if (th8PosixMutexReady) pthread_mutex_lock(&th8PosixMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8PosixUnlock --
 *
 *	Release the process-global platform mutex.
 *
 * Why / How:
 *	See th8PosixLock.  Same th8PosixMutexReady guard for
 *	symmetry and safety.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases the platform mutex.  May unblock other threads.
 *
 *----------------------------------------------------------------------
 */

static void
th8PosixUnlock(void)
{
    if (th8PosixMutexReady) pthread_mutex_unlock(&th8PosixMutex);
}


/*
 * File-scope base path state.  Shared between th8PosixGetBasePath
 * and Th8_SetBasePath so the latter can override the automatic
 * dladdr-based detection.
 */

static char zBasePath[4096];
static int bInitialized = 0;


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetBasePath --
 *
 *	Explicitly set the base path for the TH8 platform layer.
 *	This overrides the automatic dladdr-based detection.
 *	Must be called BEFORE Th8_Initialize.  The path is copied
 *	internally.  Pass "." to use the current working directory.
 *
 * Why / How:
 *	The base path defines the root of the TH8 sandbox -- all
 *	file operations are restricted to paths at or below it.
 *	Automatic detection via dladdr works for typical library
 *	installations, but the embedder may need a different root
 *	(e.g., a test directory, or the CWD for th8sh).  The path
 *	is resolved via realpath(3) to normalize symlinks and
 *	relative components, then stored in the static zBasePath
 *	buffer under the platform mutex.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the path is NULL, too long,
 *	or getcwd fails when "." is specified.
 *
 * Side effects:
 *	Sets the global zBasePath and bInitialized flag.  Acquires
 *	and releases the platform mutex.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetBasePath(const char *zPath, size_t nPath)
{
    char resolved[4096];

    if (!zPath) {
	TH8_TRACE_ERR(NULL, "NULL path");
	return TH8_ERROR;
    }
    if (nPath == TH8_NOLEN) nPath = th8PosixStrlen(zPath);
    if (nPath >= sizeof(resolved)) {
	TH8_TRACE_ERR(NULL, "path too long");
	return TH8_ERROR;
    }

    /*
     * Copy to a local buffer for NUL termination, then
     * resolve via realpath to get the canonical absolute path.
     */

    memcpy(resolved, zPath, nPath);
    resolved[nPath] = '\0';

    th8PosixLock();

    /*
     * If ".", resolve to CWD.
     */

    if (nPath == 1 && zPath[0] == '.') {
	char cwd[4096];

	if (!getcwd(cwd, sizeof(cwd))) {
	    TH8_TRACE_ERR(NULL, "getcwd failed");
	    th8PosixUnlock();
	    return TH8_ERROR;
	}
	{
	    size_t nCwd = th8PosixStrlen(cwd);
	    if (nCwd >= sizeof(zBasePath)) {
		TH8_TRACE_ERR(NULL, "CWD path too long");
		th8PosixUnlock();
		return TH8_ERROR;
	    }
	    memcpy(zBasePath, cwd, nCwd + 1);
	}
    } else {
	char rp[4096];

	if (realpath(resolved, rp)) {
	    size_t nRp = th8PosixStrlen(rp);
	    if (nRp >= sizeof(zBasePath)) {
		TH8_TRACE_ERR(NULL, "resolved path too long");
		th8PosixUnlock();
		return TH8_ERROR;
	    }
	    memcpy(zBasePath, rp, nRp + 1);
	} else {
	    if (nPath >= sizeof(zBasePath)) {
		TH8_TRACE_ERR(NULL, "path too long for base");
		th8PosixUnlock();
		return TH8_ERROR;
	    }
	    memcpy(zBasePath, resolved, nPath + 1);
	}
    }
    bInitialized = 1;

    th8PosixUnlock();
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetBasePath --
 *
 *	Return the current base path, or NULL if not determined.
 *	The returned pointer is a static buffer; the caller must
 *	not free it.
 *
 * Why / How:
 *	Public API for the embedder to inspect the configured
 *	sandbox root.  Forces lazy initialization on first call
 *	(via th8PosixGetBasePath) so the dladdr-based detection
 *	runs if Th8_SetBasePath was never called.
 *
 * Results:
 *	Pointer to the static zBasePath buffer, or NULL if the
 *	base path could not be determined.
 *
 * Side effects:
 *	May trigger base-path auto-detection on first call.
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_GetBasePath(void)
{
    /*
     * Force initialization if not yet done (triggers dladdr
     * detection on first call).
     */
    (void)th8PosixGetBasePath();

    return zBasePath[0] ? zBasePath : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetBasePath --
 *
 *	Return the base path for the TH8 library.  The base path is
 *	the directory containing the TH8 shared library, or its
 *	parent directory if the immediate directory is named "bin".
 *
 *	The result is cached in a static buffer on first call.
 *	Returns NULL if the base path cannot be determined.
 *
 * Why / How:
 *	All sandbox checks ultimately call this function to obtain
 *	the base path.  Uses dladdr(3) with the address of this
 *	function itself as the probe to find the path of the TH8
 *	shared library.  A union is used for the function-to-object
 *	pointer cast to avoid undefined behavior under strict ISO C
 *	(POSIX guarantees the cast works but the C standard does
 *	not).  The resulting path is resolved via realpath(3), then
 *	stripped to its directory component.  If the directory is
 *	named "bin", "bin-static", or "lib", the parent directory
 *	is used instead, so the base path points to the installation
 *	prefix (e.g., /opt/th8) rather than a subdirectory.  The
 *	result is cached in the static zBasePath buffer and
 *	protected by the platform mutex.
 *
 * Results:
 *	Pointer to the static zBasePath buffer, or NULL if the
 *	base path could not be determined.
 *
 * Side effects:
 *	On first call, calls dladdr and realpath.  Acquires and
 *	releases the platform mutex.
 *
 *----------------------------------------------------------------------
 */

static const char *
th8PosixGetBasePath(void)
{
    th8PosixLock();
    if (!bInitialized) {
	Dl_info info;
	char resolved[4096];

	bInitialized = 1;
	zBasePath[0] = '\0';

	/*
	 * Use dladdr to find the path of the TH8 library itself.
	 * We pass the address of this function as the probe.
	 * The union avoids a function-to-object pointer cast that
	 * is technically undefined in strict ISO C (but guaranteed
	 * by POSIX).
	 */

	{
	    union {
		void *p;
		const char *(*f)(void);
	    } uAddr;
	    uAddr.f = th8PosixGetBasePath;
	    if (dladdr(uAddr.p, &info) && info.dli_fname) {
		if (realpath(info.dli_fname, resolved)) {
		    /*
		     * Strip the filename to get the directory.
		     */

		    char *slash = th8PosixStrrchr(resolved, '/');

		    if (slash && slash != resolved) {
			*slash = '\0';

			/*
			 * If the directory is named "bin" or "lib",
			 * the library resides in a subdirectory of
			 * the base -- use the parent instead.
			 */

			{
			    char *tail = th8PosixStrrchr(resolved, '/');

			    if (tail &&
			        (th8PosixStrcmp(tail + 1, "bin") == 0 ||
			         th8PosixStrcmp(tail + 1, "bin-afl") == 0 ||
			         th8PosixStrcmp(tail + 1, "bin-static") ==
			             0 ||
			         th8PosixStrcmp(tail + 1, "lib") == 0)) {
				*tail = '\0';
			    }
			}

			{
			    size_t nResolved = th8PosixStrlen(resolved);
			    if (nResolved < sizeof(zBasePath)) {
				memcpy(zBasePath, resolved, nResolved + 1);
			    }
			}
		    }
		}
	    }
	}

	/*
	 * No fallback.  If dladdr failed, leave zBasePath empty
	 * so that subsequent attempts to use it fail gracefully.
	 */

	if (!zBasePath[0]) {
	    TH8_TRACE_ERR(NULL, "dladdr failed");
	}
    }
    th8PosixUnlock();

    return zBasePath[0] ? zBasePath : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixIsUnderBase --
 *
 *	Check whether the absolute path zAbs resides at or under the
 *	base path.  Returns:
 *	   0  -- exact match (IS the base directory)
 *	   1  -- under the base directory
 *	  -1  -- outside the base directory (foreign)
 *
 * Why / How:
 *	Core sandbox enforcement primitive.  The check is a simple
 *	string prefix comparison: if zAbs starts with zBase and the
 *	next character is '/' (or end-of-string for exact match),
 *	the path is under the base.  As a defense-in-depth measure,
 *	the function also scans for "." and ".." segments in zAbs.
 *	These should never appear in a fully resolved path (realpath
 *	eliminates them), but if they do, they indicate incomplete
 *	resolution that could defeat the prefix check.  Rejecting
 *	them ensures that even a buggy caller cannot bypass the
 *	sandbox via path traversal.
 *
 * Results:
 *	0 (exact match), 1 (underneath), or -1 (foreign).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8PosixIsUnderBase(
    const char *zAbs, /* Absolute path to check. */
    const char *zBase, /* Base path. */
    size_t nBase) /* Length of base path. */
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
	    while (*p && *p != '/')
		p++;
	    {
		size_t segLen = (size_t)(p - seg);
		if ((segLen == 1 && seg[0] == '.') ||
		    (segLen == 2 && seg[0] == '.' && seg[1] == '.')) {
		    return -1; /* suspicious path */
		}
	    }
	    while (*p == '/')
		p++;
	}
    }

    nAbs = th8PosixStrlen(zAbs);

    if (nAbs == nBase && memcmp(zAbs, zBase, nBase) == 0) {
	return 0; /* exact match */
    }
    if (nAbs > nBase && memcmp(zAbs, zBase, nBase) == 0 &&
        zAbs[nBase] == '/') {
	return 1; /* underneath */
    }
    return -1; /* foreign */
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixIsPathUnderBase --
 *
 *	Check whether a NUL-terminated path, when fully resolved,
 *	resides at or under the base directory.  Returns non-zero
 *	if the path is allowed, zero if it resolves outside the
 *	base directory (access denied).
 *
 *	Absolute paths are rejected outright -- all file operations
 *	must use paths relative to the base directory.
 *
 *	For existing paths, realpath() resolves the full chain of
 *	symlinks and yields the canonical path for checking.
 *
 *	For non-existing paths, the relative path is manually
 *	resolved against CWD (which is itself validated to be
 *	under the base) with "." and ".." segments applied.
 *
 *	When no base path is configured, all paths are allowed.
 *
 * Why / How:
 *	This is the primary sandbox gatekeeper called by xGetData,
 *	xDataExists, xLoad, xSameFile, and the path functions.
 *	It enforces three security properties: (1) absolute paths
 *	are always rejected, forcing scripts to use relative paths;
 *	(2) symlinks are fully resolved via realpath(3) so a
 *	symlink-to-outside-the-sandbox attack is caught; (3) for
 *	paths that do not yet exist on disk, manual ".." resolution
 *	prevents speculative traversal attacks.  The manual
 *	resolution splits both CWD and the relative path into
 *	components, applies "." (skip) and ".." (pop) semantics,
 *	then reassembles the absolute path for the prefix check.
 *
 * Results:
 *	Non-zero if allowed, zero if denied.
 *
 * Side effects:
 *	May call realpath(3) and getcwd(3).
 *
 *----------------------------------------------------------------------
 */

int
th8PosixIsPathUnderBase(const char *zPath)
{
    const char *zBase;
    size_t nBase;
    char resolved[4096];

    /* Bug 26: zPath is from caller -- caller could pass NULL. */
    if (!zPath || !zPath[0]) return 0;

    zBase = th8PosixGetBasePath();
    if (!zBase) return 1; /* no sandbox configured */
    nBase = th8PosixStrlen(zBase);

    /*
     * Absolute paths are not allowed -- all file operations
     * must use paths relative to the base directory.
     */
    if (zPath[0] == '/') return 0;

    /*
     * For existing paths, resolve via realpath and verify
     * the canonical result is at or under the base.
     */
    if (realpath(zPath, resolved)) {
	return th8PosixIsUnderBase(resolved, zBase, nBase) >= 0;
    }

    /*
     * Path does not exist on disk.  Build the would-be
     * absolute path by prepending CWD to the relative path,
     * then manually resolve "." and ".." segments to get a
     * canonical result for base-path checking.
     *
     * This prevents ".." traversal attacks on non-existing
     * paths (e.g. "../../etc/shadow") from leaking
     * information about the filesystem outside the sandbox.
     */
    {
	char cwd[4096];
	const char *seg[256];
	size_t segLen[256];
	int depth = 0;
	const char *p;
	char norm[4096];
	size_t nNorm;
	int i;

	if (!getcwd(cwd, sizeof(cwd))) return 0;

	/* Parse CWD into path components. */
	p = cwd;
	while (*p == '/')
	    p++;
	while (*p) {
	    const char *s = p;

	    while (*p && *p != '/')
		p++;
	    if (depth < 256) {
		seg[depth] = s;
		segLen[depth] = (size_t)(p - s);
		depth++;
	    }
	    while (*p == '/')
		p++;
	}

	/* Parse relative path, applying "." and ".." */
	p = zPath;
	while (*p) {
	    const char *s = p;
	    size_t n;

	    while (*p && *p != '/')
		p++;
	    n = (size_t)(p - s);
	    if (n == 1 && s[0] == '.') {
		/* current directory -- skip */
	    } else if (n == 2 && s[0] == '.' && s[1] == '.') {
		if (depth > 0) depth--;
	    } else if (n > 0 && depth < 256) {
		seg[depth] = s;
		segLen[depth] = n;
		depth++;
	    }
	    while (*p == '/')
		p++;
	}

	/* Reassemble as absolute path "/a/b/c". */
	nNorm = 0;
	for (i = 0; i < depth; i++) {
	    if (nNorm + 1 + segLen[i] >= sizeof(norm)) return 0;
	    norm[nNorm++] = '/';
	    memcpy(norm + nNorm, seg[i], segLen[i]);
	    nNorm += segLen[i];
	}
	if (nNorm == 0) {
	    norm[0] = '/';
	    nNorm = 1;
	}
	norm[nNorm] = '\0';

	return th8PosixIsUnderBase(norm, zBase, nBase) >= 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetCwd --
 *
 *	Implements the Th8_Platform.xGetCwd callback.  Return the
 *	current working directory relative to the base path.  If
 *	the actual CWD is the base directory, returns ".".  If it's
 *	underneath the base directory, returns "./relative".
 *	Otherwise returns NULL (error -- foreign directory).
 *
 * Why / How:
 *	The [pwd] command and [file normalize] need to know the
 *	current directory, but must not expose absolute filesystem
 *	paths to scripts.  CWD is obtained via getcwd(3) and
 *	resolved through realpath(3) to match how the base path
 *	was resolved (important when CWD contains symlinks).  The
 *	result is then expressed relative to the base: "." for the
 *	base itself, or "./sub/dir" for a subdirectory.  Returning
 *	NULL for foreign directories ensures scripts cannot discover
 *	the host filesystem layout.
 *
 * Results:
 *	Allocated string (caller frees via Th8_Free), or NULL if
 *	CWD is outside the sandbox or cannot be determined.
 *
 * Side effects:
 *	Allocates memory.  Calls getcwd and realpath.
 *
 *----------------------------------------------------------------------
 */

static char *
th8PosixGetCwd(
    Th8_Interp *interp, /* Interpreter (for Th8_AttemptMalloc). */
    void *pCtx) /* Host context (unused). */
{
    const char *zBase;
    size_t nBase;
    char zCwd[4096];
    int rel;

    (void)pCtx;

    zBase = th8PosixGetBasePath();
    if (!zBase) return NULL;
    nBase = th8PosixStrlen(zBase);

    /*
     * Resolve CWD through realpath to handle symlinked
     * components, matching how the base path was resolved.
     */

    {
	char zRaw[4096];

	if (!getcwd(zRaw, sizeof(zRaw))) {
	    TH8_TRACE_ERR(NULL, "getcwd failed");
	    return NULL;
	}
	if (!realpath(zRaw, zCwd)) {
	    size_t nRaw = th8PosixStrlen(zRaw);
	    if (nRaw >= sizeof(zCwd)) return NULL;
	    memcpy(zCwd, zRaw, nRaw + 1);
	}
    }

    rel = th8PosixIsUnderBase(zCwd, zBase, nBase);

    if (rel == 0) {
	/*
	 * CWD is the base directory.
	 */

	char *z = (char *)TH8_ALLOC(interp, 2);

	if (z) {
	    z[0] = '.';
	    z[1] = '\0';
	}
	return z;
    } else if (rel == 1) {
	/*
	 * CWD is underneath the base directory.
	 * Return "./relative".
	 */

	const char *zTail = zCwd + nBase + 1;
	size_t nTail = th8PosixStrlen(zTail);
	char *z = (char *)TH8_ALLOC_STR_ADD(interp, 2, nTail);

	if (z) {
	    z[0] = '.';
	    z[1] = '/';
	    memcpy(z + 2, zTail, nTail);
	    z[2 + nTail] = '\0';
	}
	return z;
    }

    /*
     * Foreign directory -- return NULL to signal error.
     */

    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixSetCwd --
 *
 *	Implements the Th8_Platform.xSetCwd callback.  Change the
 *	current working directory.  Accepts "." (the base directory)
 *	or any path that resolves to a location at or underneath
 *	the base directory.  Rejects all other paths.
 *
 * Why / How:
 *	The [cd] command changes the process CWD, which affects all
 *	subsequent relative path operations.  The sandbox must
 *	ensure the CWD never moves outside the base directory.
 *	The target path is resolved via realpath(3) to follow
 *	symlinks, then checked against the base.  Paths that do
 *	not exist or cannot be resolved are rejected, because
 *	without full symlink resolution we cannot guarantee the
 *	target is inside the sandbox.  "." is special-cased to
 *	chdir directly to the base directory without realpath.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure or if the target
 *	is outside the sandbox.
 *
 * Side effects:
 *	Changes the process CWD via chdir(2).
 *
 *----------------------------------------------------------------------
 */

static int
th8PosixSetCwd(
    Th8_Interp *interp,
    void *pCtx,
    const char *zPath,
    size_t nPath)
{
    const char *zBase;
    size_t nBase;
    char resolved[4096];
    char zCopy[4096];
    int rel;

    (void)interp;
    (void)pCtx;

    if (!zPath) {
	TH8_TRACE_ERR(NULL, "invalid new path");
	return TH8_ERROR;
    }
    if (nPath == (size_t)-1) nPath = th8PosixStrlen(zPath);

    zBase = th8PosixGetBasePath();
    if (!zBase) {
	TH8_TRACE_ERR(NULL, "invalid base path");
	return TH8_ERROR;
    }
    nBase = th8PosixStrlen(zBase);

    /*
     * Shortcut: "." always means the base directory.
     */

    if (nPath == 1 && zPath[0] == '.') {
	if (chdir(zBase) == 0) return TH8_OK;
	TH8_TRACE_ERR(NULL, "chdir failed");
	return TH8_ERROR;
    }

    /*
     * Resolve the target path to absolute form.
     */

    if (nPath >= sizeof(zCopy)) {
	TH8_TRACE_ERR(NULL, "new path is too long");
	return TH8_ERROR;
    }
    memcpy(zCopy, zPath, nPath);
    zCopy[nPath] = '\0';

    if (!realpath(zCopy, resolved)) {
	/*
	 * Path doesn't exist or can't be resolved.
	 * Reject it -- we cannot verify that it is under
	 * the base directory without resolving symlinks.
	 */

	TH8_TRACE_ERR(NULL, "realpath failed");
	return TH8_ERROR;
    }

    /*
     * Check if the resolved path is at or under the base.
     */

    rel = th8PosixIsUnderBase(resolved, zBase, nBase);

    if (rel >= 0) {
	if (chdir(resolved) == 0) return TH8_OK;
	TH8_TRACE_ERR(NULL, "chdir failed");
	return TH8_ERROR;
    }

    TH8_TRACE_ERR(NULL, "new path is foreign");
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixResolveAbsolute --
 *
 *	Internal helper.  Resolve zPath to a canonical absolute
 *	path.  Uses realpath(3) for existing paths; otherwise
 *	prepends CWD and resolves "." / ".." manually.  The caller
 *	must free the result with Th8_Free.  Returns NULL on failure.
 *
 * Why / How:
 *	th8PosixNormalizePath and th8PosixGetExePath need to convert
 *	an arbitrary relative or absolute path into a fully resolved
 *	canonical form.  For existing paths, realpath(3) handles all
 *	symlink resolution and ".." elimination.  For non-existing
 *	paths (e.g., a path that will be created), realpath fails, so
 *	this function manually prepends the realpath-resolved CWD and
 *	splits the combined path into components, applying "." (skip)
 *	and ".." (pop) rules.  The result is always an absolute path
 *	starting with "/" with no "." or ".." segments, suitable for
 *	prefix comparison against the base path.  Up to 512 path
 *	components are supported; deeper paths are rejected.
 *
 * Results:
 *	Allocated canonical absolute path (caller frees via Th8_Free),
 *	or NULL on failure.
 *
 * Side effects:
 *	Allocates memory.  May call realpath(3) and getcwd(3).
 *
 *----------------------------------------------------------------------
 */

static char *
th8PosixResolveAbsolute(
    Th8_Interp *pInterp, /* Interpreter (for Th8_AttemptMalloc). */
    const char *zPath, /* Path to resolve. */
    size_t nPath) /* Length, or (size_t)-1. */
{
    char *zCopy;
    char resolved[4096];

    if (!zPath) return NULL;

    if (nPath == (size_t)-1) {
	nPath = th8PosixStrlen(zPath);
    }
    zCopy = (char *)TH8_ALLOC_STR(pInterp, nPath);
    if (!zCopy) return NULL;
    memcpy(zCopy, zPath, nPath);
    zCopy[nPath] = '\0';

    /*
     * Try realpath first (works for existing paths and
     * resolves symlinks).
     */

    if (realpath(zCopy, resolved)) {
	size_t n = th8PosixStrlen(resolved);
	char *z = (char *)TH8_ALLOC_STR(pInterp, n);

	Th8_Free(pInterp, zCopy);
	if (z) {
	    memcpy(z, resolved, n + 1);
	}
	return z;
    }

    /*
     * Path does not exist.  Prepend CWD if relative,
     * then resolve "." and ".." by splitting into
     * components and building a clean result.
     */

    {
	char *zAbs;
	const char *comp[512];
	size_t compLen[512];
	int depth = 0;
	const char *p;
	char *zOut;
	size_t nOut;
	int i;

	if (zCopy[0] != '/') {
	    char cwdRaw[4096];
	    char cwd[4096];

	    if (!getcwd(cwdRaw, sizeof(cwdRaw))) {
		TH8_TRACE_ERR(NULL, "getcwd failed");
		Th8_Free(pInterp, zCopy);
		return NULL;
	    }

	    /*
	     * Resolve CWD through realpath to match how the
	     * base path was resolved.
	     */

	    if (!realpath(cwdRaw, cwd)) {
		size_t nCwdRaw = th8PosixStrlen(cwdRaw);
		if (nCwdRaw >= sizeof(cwd)) {
		    Th8_Free(pInterp, zCopy);
		    return NULL;
		}
		memcpy(cwd, cwdRaw, nCwdRaw + 1);
	    }

	    {
		size_t nCwd = th8PosixStrlen(cwd);

		if (nCwd > 100000 || nPath > 100000) {
		    Th8_Free(pInterp, zCopy);
		    return NULL;
		}
		/*
		 * Allocate nCwd + nPath + 2 bytes (one '/' separator,
		 * one NUL).  Expressed as (1*nCwd) + (1*nPath) + 2 so
		 * Th8_SafeAllocMulAdd2 validates each step explicitly.
		 */
		zAbs = (char *)Th8_SafeAllocMulAdd2(
		    pInterp, 1, nCwd, 1, nPath, 2, __FILE__, __LINE__);
	    }
	    if (!zAbs) {
		Th8_Free(pInterp, zCopy);
		return NULL;
	    }
	    {
		size_t nc = th8PosixStrlen(cwd);
		size_t np = th8PosixStrlen(zCopy);
		memcpy(zAbs, cwd, nc);
		zAbs[nc] = '/';
		memcpy(zAbs + nc + 1, zCopy, np + 1);
	    }
	    Th8_Free(pInterp, zCopy);
	} else {
	    zAbs = zCopy;
	}

	/*
	 * Split into components and resolve "." / "..".
	 */

	p = zAbs + 1;
	while (*p) {
	    const char *start;
	    size_t len;

	    while (*p == '/')
		p++;
	    if (*p == '\0') break;
	    start = p;
	    while (*p && *p != '/')
		p++;
	    len = (size_t)(p - start);

	    if (len == 1 && start[0] == '.') {
		/* current dir -- no-op */
	    } else if (len == 2 && start[0] == '.' && start[1] == '.') {
		if (depth > 0) depth--;
	    } else if (depth < 512) {
		comp[depth] = start;
		compLen[depth] = len;
		depth++;
	    } else {
		/*
		 * Too many path components.  Reject the path
		 * rather than silently truncating.
		 */

		Th8_Free(pInterp, zAbs);
		return NULL;
	    }
	}

	/*
	 * Build result: "/" + components joined by "/".
	 */

	nOut = 1; /* leading "/" */
	for (i = 0; i < depth; i++) {
	    if (i > 0) nOut++; /* separator */
	    nOut += compLen[i];
	}
	zOut = (char *)TH8_ALLOC_STR(pInterp, nOut);
	if (zOut) {
	    char *dst = zOut;

	    *dst++ = '/';
	    for (i = 0; i < depth; i++) {
		if (i > 0) *dst++ = '/';
		memcpy(dst, comp[i], compLen[i]);
		dst += compLen[i];
	    }
	    *dst = '\0';
	}

	Th8_Free(pInterp, zAbs);
	return zOut;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixNormalizePath --
 *
 *	Implements the Th8_Platform.xNormalizePath callback.  Return
 *	the normalized path for zPath.  The security-aware
 *	normalization policy is:
 *
 *	  1. Resolve zPath to its canonical absolute form.
 *
 *	  2. Determine the "base path" -- the current working
 *	     directory (which represents the TH8 library directory
 *	     or its parent if that directory is named "bin"
 *	     or "bin-static").
 *
 *	  3. If the resolved path starts with the base path, return
 *	     it as "./relative/from/base" (a dot-prefixed relative
 *	     path).
 *
 *	  4. Otherwise, return NULL (path is outside the sandbox).
 *
 *	The result is allocated via Th8_AttemptMalloc; the caller
 *	frees with Th8_Free.
 *
 * Why / How:
 *	[file normalize] must produce a canonical, unambiguous path
 *	that scripts can compare for equality.  The path is first
 *	resolved to its absolute form via th8PosixResolveAbsolute
 *	(which follows symlinks and eliminates "." / ".."), then
 *	converted to sandbox-relative form.  Returning NULL for
 *	paths outside the sandbox prevents scripts from discovering
 *	the host filesystem layout.  The "./relative" prefix makes
 *	the result a valid relative path that can be passed back to
 *	xGetData et al.
 *
 * Results:
 *	Allocated normalized path (caller frees via Th8_Free), or
 *	NULL if the path is outside the sandbox.
 *
 * Side effects:
 *	Allocates memory.  Calls th8PosixResolveAbsolute.
 *
 *----------------------------------------------------------------------
 */

static char *
th8PosixNormalizePath(
    Th8_Interp *interp, /* Interpreter (for Th8_AttemptMalloc). */
    void *pCtx, /* Host context (unused). */
    const char *zPath, /* Path to normalize. */
    size_t nPath) /* Length of zPath, or (size_t)-1. */
{
    char *zAbs;
    const char *zBase;
    size_t nBase;
    int rel;

    (void)pCtx;

    /*
     * Step 1: Resolve to absolute.
     */

    zAbs = th8PosixResolveAbsolute(interp, zPath, nPath);
    if (!zAbs) return NULL;

    /*
     * Step 2: Determine the base path.
     */

    zBase = th8PosixGetBasePath();
    if (!zBase) {
	return zAbs; /* cannot determine base; return absolute */
    }
    nBase = th8PosixStrlen(zBase);

    /*
     * Step 3: If zAbs is at or under the base, make it relative.
     */

    rel = th8PosixIsUnderBase(zAbs, zBase, nBase);

    if (rel == 0) {
	/*
	 * Exact match: the path IS the base directory.
	 */

	char *zOut;

	Th8_Free(interp, zAbs);
	zOut = (char *)TH8_ALLOC(interp, 2);
	if (zOut) {
	    zOut[0] = '.';
	    zOut[1] = '\0';
	}
	return zOut;
    } else if (rel == 1) {
	/*
	 * Under the base directory.  Build "./relative".
	 */

	const char *zTail = zAbs + nBase + 1;
	size_t nTail = th8PosixStrlen(zAbs) - nBase - 1;
	char *zOut = (char *)TH8_ALLOC_STR_ADD(interp, 2, nTail);

	if (zOut) {
	    zOut[0] = '.';
	    zOut[1] = '/';
	    memcpy(zOut + 2, zTail, nTail);
	    zOut[2 + nTail] = '\0';
	}
	Th8_Free(interp, zAbs);
	return zOut;
    }

    /*
     * Step 4: Path is outside the base directory.
     * Return NULL -- scripts must not see paths outside
     * the sandbox.
     */

    Th8_Free(interp, zAbs);
    return NULL;
}


#  if defined(__APPLE__)
#    include <mach-o/dyld.h> /* _NSGetExecutablePath */
#  endif
#  if defined(__FreeBSD__)
#    include <sys/types.h>
#    include <sys/sysctl.h>
#  endif

/*
 *----------------------------------------------------------------------
 *
 * th8PosixGetExePath --
 *
 *	Implements the Th8_Platform.xGetExePath callback.  Return
 *	the fully qualified path to the running executable.  The
 *	result is allocated via Th8_AttemptMalloc; the caller
 *	frees it.
 *
 *	Linux:   readlink("/proc/self/exe")
 *	macOS:   _NSGetExecutablePath() + realpath()
 *	FreeBSD: sysctl(CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1)
 *
 * Why / How:
 *	The ::th8_nameofexecutable variable and [info nameofexecutable]
 *	command need the executable path.  Each platform has a different
 *	mechanism: Linux uses /proc/self/exe (a kernel-maintained
 *	symlink), macOS uses the Mach-O runtime function
 *	_NSGetExecutablePath, and FreeBSD uses a sysctl.  The raw
 *	path is canonicalized via realpath(3) to resolve symlinks.
 *	The result is then converted to sandbox-relative form
 *	("./relative") if it resides under the base path, or NULL
 *	if outside the base (to prevent leaking the absolute host
 *	path).  On platforms with no portable mechanism, NULL is
 *	returned and the host is expected to set the variable
 *	manually.
 *
 * Results:
 *	Allocated path (caller frees via Th8_Free), or NULL if the
 *	path cannot be determined or is outside the sandbox.
 *
 * Side effects:
 *	Allocates memory.  On Linux, reads from /proc.
 *
 *----------------------------------------------------------------------
 */

static char *
th8PosixGetExePath(
    Th8_Interp *interp, /* Interpreter for Th8_AttemptMalloc. */
    void *pCtx) /* Platform context (unused). */
{
    char buf[4096];
    char resolved[4096];
    char *zResult = 0;

    (void)pCtx;

#  if defined(__linux__)
    /*
     * Linux: /proc/self/exe is a symlink to the executable.
     */
    {
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);

	if (n > 0) {
	    buf[n] = '\0';
	} else {
	    return 0;
	}
    }
#  elif defined(__APPLE__)
    /*
     * macOS: _NSGetExecutablePath fills a buffer with the path.
     * The path may be relative or contain symlinks, so we
     * canonicalize it with realpath() below.
     */
    {
	uint32_t size = sizeof(buf);

	if (_NSGetExecutablePath(buf, &size) != 0) {
	    return 0;
	}
    }
#  elif defined(__FreeBSD__)
    /*
     * FreeBSD: sysctl with KERN_PROC_PATHNAME returns the
     * canonical path to the executable.
     */
    {
	int mib[4];
	size_t cb = sizeof(buf);

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PATHNAME;
	mib[3] = -1; /* Current process. */
	if (sysctl(mib, 4, buf, &cb, 0, 0) != 0) {
	    return 0;
	}
    }
#  else
    /*
     * Other POSIX: no portable mechanism.  Return NULL and
     * let the host set ::th8_nameofexecutable.
     */
    return 0;
#  endif

    /*
     * Canonicalize: resolve symlinks and produce an absolute path.
     */
    if (!realpath(buf, resolved)) {
	return 0;
    }

    /*
     * Convert to base-relative form.  Scripts must not see
     * absolute filesystem paths.
     */
    {
	const char *zBase = th8PosixGetBasePath();

	if (zBase) {
	    size_t nBase = th8PosixStrlen(zBase);
	    int rel = th8PosixIsUnderBase(resolved, zBase, nBase);

	    if (rel == 0) {
		/* Executable IS the base directory (unusual). */
		zResult = (char *)TH8_ALLOC(interp, 2);
		if (zResult) {
		    zResult[0] = '.';
		    zResult[1] = '\0';
		}
		return zResult;
	    } else if (rel == 1) {
		/* Executable is under the base -- "./relative". */
		const char *zTail = resolved + nBase + 1;
		size_t nTail = th8PosixStrlen(resolved) - nBase - 1;

		zResult = (char *)TH8_ALLOC_STR_ADD(interp, 2, nTail);
		if (zResult) {
		    zResult[0] = '.';
		    zResult[1] = '/';
		    memcpy(zResult + 2, zTail, nTail);
		    zResult[2 + nTail] = '\0';
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
	{
	    size_t n = th8PosixStrlen(resolved);

	    zResult = (char *)TH8_ALLOC_STR(interp, n);
	    if (zResult) {
		memcpy(zResult, resolved, n + 1);
	    }
	}
	return zResult;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8PosixPlatformData --
 *
 *	The complete POSIX platform callback table.  Returned by
 *	Th8_GetPosixPlatform() and passed to Th8_CreateInterp.
 *
 * Why / How:
 *	Th8_Platform is a struct of function pointers that the
 *	interpreter calls for all OS-specific operations.  This
 *	static instance fills in the POSIX-specific callbacks
 *	defined in this file.  Slots left as 0 (xMalloc, xRealloc,
 *	xFree, xMemcpy, etc.) are provided by th8_libc.c via
 *	Th8_MergePlatform(), which overlays the CRT wrappers
 *	without overwriting the POSIX-specific callbacks.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8PosixPlatformData = {
    1, /* nVersion */
    th8PosixInitialize, /* xInitialize */
    th8PosixFinalize, /* xFinalize */

/* Interpreter deletion notifications */
#  if !defined(TH8_FUZZ_STANDALONE)
    th8PosixPreDeleteInterp, /* xPreDeleteInterp */
    th8PosixDeleteInterp, /* xDeleteInterp */
#  else
    0,    /* xPreDeleteInterp */
    0,    /* xDeleteInterp */
#  endif

    /* Memory (provided by th8_libc.c via merge) */
    0, /* xMalloc */
    0, /* xRealloc */
    0, /* xFree */
    0, /* xMemorySize */
    0, /* xNeedMemory */

    /* C runtime mem ops */
    0, /* xMemcpy */
    0, /* xMemmove */
    th8PosixMemset, /* xMemset */
    0, /* xMemcmp */

    /* C runtime string/utility (provided by th8_libc.c via merge) */
    0, /* xStrlen */
    0, /* xStrcmp */
    0, /* xStrchr */
    0, /* xAtoi */
    0, /* xQsort */
    0, /* xVsnprintf */

    /* Mutex callbacks */
    th8PosixMutexInit, /* xMutexInit */
    th8PosixMutexFinal, /* xMutexFinal */
    th8PosixMutexEnter, /* xMutexEnter */
    th8PosixMutexLeave, /* xMutexLeave */
    th8PosixIntCmpXchg, /* xIntCmpXchg */
    th8PosixMemBarrier, /* xMemBarrier */

/* Manual-reset event handle (cond+mutex shim) */
#  if defined(TH8_FUZZ_STANDALONE)
    0, 0, 0, 0, 0, /* xEvent* (disabled in fuzz standalone) */
#  else
    th8PosixEventCreate, /* xEventCreate */
    th8PosixEventDestroy, /* xEventDestroy */
    th8PosixEventSet,  /* xEventSet */
    th8PosixEventReset,  /* xEventReset */
    th8PosixEventWait,  /* xEventWait */
#  endif

    /* Input -- POSIX read() on file descriptors */
    th8PosixInput, /* xInput */

    /* Output -- POSIX write() on file descriptors */
    th8PosixOutput, /* xOutput */
    th8PosixOutputError, /* xOutputError */

    /* I/O channel redirection */
    th8PosixGetInput, /* xGetInput */
    th8PosixSetInput, /* xSetInput */
    th8PosixGetOutput, /* xGetOutput */
    th8PosixSetOutput, /* xSetOutput */
    th8PosixGetErrorOutput, /* xGetErrorOutput */
    th8PosixSetErrorOutput, /* xSetErrorOutput */

    /* Channel control */
    th8PosixChannelControl, /* xChannelControl */

    /* Temporary data */
    th8PosixGetTemporaryData, /* xGetTemporaryData */
    th8PosixDeleteTemporaryData, /* xDeleteTemporaryData */
    0, /* xSetTemporaryData (no post-write hook) */
    0, /* xCloseTemporaryData */

    /* File system */
    th8PosixNormalizePath, /* xNormalizePath */
    th8PosixGetCwd, /* xGetCwd */
    th8PosixSetCwd, /* xSetCwd */

    /* Executable path */
    th8PosixGetExePath, /* xGetExePath */
    th8PosixGetRealPath, /* xGetRealPath */
    th8PosixGetRootPath, /* xGetRootPath */
    th8PosixSameFile, /* xSameFile */

    /* Data retrieval */
    th8PosixGetData, /* xGetData */
    th8PosixDataExists, /* xDataExists */

/* Binary loading (dlopen / dlsym / dlclose) */
#  if !defined(TH8_FUZZ_STANDALONE)
    th8PosixLoad, /* xLoad */
    th8PosixUnload, /* xUnload */
#  else
    0,   /* xLoad */
    0,   /* xUnload */
#  endif

    /* Time */
    th8PosixTimeMs, /* xTimeMs */
    th8PosixTimeUs, /* xTimeUs */

    /* Sleep */
    th8PosixSleep, /* xSleep */

    /* Process ID */
    th8PosixGetPid, /* xGetPid */

    /* User name, host name */
    th8PosixGetUserName, /* xGetUserName */
    th8PosixGetHostName, /* xGetHostName */
    th8PosixGetEnv, /* xGetEnv */
    0, /* xKeyValue */

    /* Stack bounds */
    th8PosixGetStackBounds, /* xGetStackBounds */

    /* Parent PID, thread ID */
    th8PosixGetParentPid, /* xGetParentPid */
    th8PosixGetThreadId, /* xGetThreadId */

    /* Error code */
    th8PosixGetLastError, /* xGetLastError */
    th8PosixSetLastError, /* xSetLastError */

    /* Trace */
    th8PosixEmitTrace, /* xEmitTrace */

    /* Panic */
    th8PosixPanic, /* xPanic */

    /* Math functions (provided by th8_libc.c via merge) */
    0, /* xMathFunc */

    /* Random bytes */
    th8PosixRandomBytes, /* xRandomBytes */

/* DNSSEC-validating DNS resolver (libunbound) */
#  if defined(TH8_ENABLE_UNBOUND)
    th8PosixDnsResolve, /* xDnsResolve */
    th8PosixDnsResolveFree, /* xDnsResolveFree */
#  else
    0,    /* xDnsResolve */
    0,    /* xDnsResolveFree */
#  endif

    /* Diagnostics -- xStackBackTrace is deliberately left
       NULL here.  It is supplied by the compiler-runtime th8_unwind layer
       (_Unwind_Backtrace), merged after the OS layers.  _Unwind_Backtrace is
       a compiler-runtime facility (not POSIX), so it lives in th8_unwind.c;
       the only POSIX-adjacent alternative (backtrace() in <execinfo.h>) is
       glibc-only and not musl-safe, so POSIX adds no native override. */
    0, /* xStackBackTrace */

    /* 64-bit atomics */
    th8PosixIntCmpXchg64, /* xIntCmpXchg64 */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPosixPlatform --
 *
 *	Return a pointer to the POSIX platform implementation.
 *
 * Why / How:
 *	Public API entry point for the embedder to obtain the
 *	POSIX platform table.  Returns a pointer to the static
 *	th8PosixPlatformData struct, which the embedder passes
 *	to Th8_CreateInterp (possibly after merging with
 *	th8_libc.c callbacks via Th8_MergePlatform).
 *
 * Results:
 *	Non-NULL pointer to the static Th8_Platform struct.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetPosixPlatform(void)
{
    return &th8PosixPlatformData;
}

#endif /* TH8_PLATFORM_POSIX */
