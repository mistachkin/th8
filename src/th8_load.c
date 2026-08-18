/*
 * th8_load.c -- Binary load/unload subsystem for TH8.
 *
 * This file implements the binary extension loading and unloading
 * facilities.  Each gate (load, unload, bigint) uses a random-token
 * pattern: enabling the gate regenerates a fresh 64-bit token from
 * the platform entropy source; the check function verifies the token
 * still matches.  This prevents scripts from enabling the gate
 * themselves -- only the C embedder can.
 *
 * The loaded-library tracking list (Th8_LoadedLib) is a simple
 * singly-linked list of library names maintained per interpreter.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_plat.h"
#include "th8.h"
#include "th8_int.h"
#include "th8_int_core.h"

#if defined(TH8_ENABLE_LOAD)


/*
 *----------------------------------------------------------------------
 *
 * Th8_EnableLoad --
 *
 *	Enable or disable the xLoad binary loading callback.
 *	When bEnable is non-zero, nLoadOk is set to nLoadToken
 *	(the per-interpreter random value).  When bEnable is zero,
 *	nLoadOk is cleared.
 *
 *	This must be called by the host BEFORE any [load]-like
 *	command can succeed.  A script cannot enable loading by
 *	itself -- only the C embedder can.
 *
 * Why / How:
 *	Binary loading (dlopen / LoadLibrary) is a security-critical
 *	operation: an attacker who can execute [load] with an
 *	arbitrary path can run native code.  The random-token gate
 *	pattern prevents scripts from enabling loading themselves.
 *	On each enable, a fresh 64-bit token is drawn from the
 *	platform entropy source (xRandomBytes); the token is
 *	rejected if it is 0, ~0, or 1 to avoid trivially guessable
 *	values.  If no entropy source is available, enable fails
 *	outright.  On disable, both nLoadToken and nLoadOk are
 *	zeroed, destroying the old token so it cannot be replayed.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if no entropy source is
 *	available (bEnable != 0 only).
 *
 * Side effects:
 *	Sets or clears interp->nLoadToken and interp->nLoadOk.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EnableLoad(Th8_Interp *interp, int bEnable)
{
    if (!interp) return TH8_ERROR;
    if (bEnable) {
	/*
	 * Regenerate the token on every enable call so that
	 * a previously observed token cannot be replayed.
	 * This makes enable/disable/enable cycles safe: each
	 * enable gets a fresh random value.
	 */

	th8_int64_t tok = 0;

	if (interp->pPlatform->xRandomBytes) {
	    int retries = 0;
	    unsigned char buf[8];

retry:
	    /* Bug 51 (2026-06-09): reset tok at the top of each retry --
	     * otherwise `tok |= ...` accumulates set bits across retries.
	     * If the prior tok was ~0 (all bits set), OR cannot clear
	     * them and the loop is stuck until the 100-retry panic. */
	    tok = 0;

	    if (++retries > 100) {
		TH8_TRACE_ERR(interp, "could not generate new load token");
		interp->pPlatform->xPanic(
		    interp, interp->pPlatform->pCtx,
		    "could not generate new load token", 33);
	    }

	    if (TH8_OK ==
	        interp->pPlatform
	            ->xRandomBytes(interp, interp->pPlatform->pCtx, buf, 8)) {
		size_t j;

		for (j = 0; j < 8; j++) {
		    tok |= ((th8_uint64_t)buf[j]) << (j * 8);
		}
	    }
	    /* Bug 26 (2026-06-07): the do-while was wrapped in
	     * NEVER which collapsed under TH8_OMIT, defeating the
	     * regen-on-reserved-value purpose of the retry path
	     * (a 3-in-2^64 RNG draw would silently install a
	     * reserved token).  Plain `if` -- the retry IS the
	     * handler for these cases. */
	    if (tok == 0 || tok == ~(th8_int64_t)0 || tok == 1) goto retry;
	} else {
	    /*
	     * NOTE: The loader subsystem cannot be used
	     *       without a "reasonably secure" source
	     *       of entropy.
	     */

	    return TH8_ERROR;
	}
	interp->nLoadToken = tok;
	interp->nLoadOk = tok;
    } else {
	interp->nLoadToken = 0;
	interp->nLoadOk = 0;
	/* Both zeroed -- old token is destroyed. */
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IsLoadEnabled --
 *
 *	Check whether binary loading is currently enabled.
 *	Returns non-zero if enabled, zero if disabled.
 *
 * Why / How:
 *	Verifies the random-token gate: nLoadOk must be non-zero
 *	and must equal nLoadToken.  If a script somehow zeroed one
 *	field or set them to different values, this check fails.
 *	The double-check prevents accidental or malicious enable.
 *
 * Results:
 *	Non-zero if binary loading is enabled, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsLoadEnabled(Th8_Interp *interp)
{
    if (!interp) return 0;
    return interp->nLoadOk != 0 && interp->nLoadOk == interp->nLoadToken;
}


/*
 *----------------------------------------------------------------------
 *
 * Loaded library tracking and unload gate --
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_LoadedLib Th8_LoadedLib;
struct Th8_LoadedLib {
    char *zName; /* Library name (same as passed to [load]). */
    size_t nName; /* Byte length of zName. */
    int nRef; /* Reference count (number of [load] calls
				 * not yet matched by [unload]).  Initial
				 * load creates entry with nRef = 1; each
				 * subsequent [load] increments, each
				 * [unload] decrements.  When nRef drops
				 * to 0, the entry is freed. */
    Th8_LoadedLib *pNext; /* Next in interpreter's loaded list. */
};

/*
 *----------------------------------------------------------------------
 *
 * th8SplitLoadName --
 *
 *	Split a "library:symbol" load name into its library-path and
 *	symbol portions at the FIRST ':' (matching th8PosixLoad).
 *	If there is no ':', the whole string is the library and the
 *	symbol is empty.
 *
 * Why / How:
 *	Load names carry both the shared-library path and an optional
 *	entry-point symbol separated by a colon; every consumer that
 *	must reason about the two parts (matching, canonicalizing)
 *	needs them split identically, so the scan-to-first-colon logic
 *	is centralized here.  The output pointers alias into zName (no
 *	copy is made); the empty-symbol case points just past the end
 *	of the string with a length of 0.
 *
 * Results:
 *	None (void).  On return *pzLib and *pnLib describe the library
 *	portion and *pzSym and *pnSym the symbol portion, both
 *	aliasing the input buffer.
 *
 * Side effects:
 *	None.  Only the caller-supplied output pointers are written.
 *
 *----------------------------------------------------------------------
 */

static void
th8SplitLoadName(
    const char *zName,
    size_t nName,
    const char **pzLib,
    size_t *pnLib,
    const char **pzSym,
    size_t *pnSym)
{
    size_t i;

    for (i = 0; i < nName && zName[i] != ':'; i++) {
	/* scan to first colon */
    }
    *pzLib = zName;
    *pnLib = i;
    if (i < nName) {
	*pzSym = &zName[i + 1];
	*pnSym = nName - i - 1;
    } else {
	*pzSym = zName + nName;
	*pnSym = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8LoadNameMatch --
 *
 *	Return 1 if two "library:symbol" load names denote the same
 *	tracked library.  Two names match when their symbols are
 *	byte-equal AND their library portions refer to the same
 *	physical file.  "Same physical file" is decided by
 *	Th8_SameFile, which on POSIX compares the (st_dev, st_ino)
 *	pair -- the only TOTALLY-unique file identity (an inode
 *	number alone is unique only within one device, and distinct
 *	path strings -- symlinks, hardlinks, bind mounts, ./ vs
 *	absolute -- can name the same file).  A byte-identical
 *	library string is accepted as a fast path / fallback for
 *	when a file cannot be stat'd (e.g. removed between load and
 *	unload, or no xSameFile callback).
 *
 * Why / How:
 *	Th8_Unload must find the tracking entry created at load time
 *	even when the caller supplies a different-but-equivalent path
 *	(symlink, ./ prefix, relative vs absolute).  Both names are
 *	split into library/symbol; the symbols must be byte-equal, and
 *	the library parts match either by a fast byte-identical
 *	comparison or, failing that, by Th8_SameFile's (st_dev,
 *	st_ino) physical-file identity check.
 *
 * Results:
 *	1 if the two load names denote the same tracked library; 0
 *	otherwise.
 *
 * Side effects:
 *	None.  May invoke the platform xSameFile callback (which
 *	stat()s the two paths) but changes no interpreter state.
 *
 *----------------------------------------------------------------------
 */

int
th8LoadNameMatch(
    Th8_Interp *interp,
    const char *zA,
    size_t nA,
    const char *zB,
    size_t nB)
{
    const char *zLibA, *zSymA, *zLibB, *zSymB;
    size_t nLibA, nSymA, nLibB, nSymB;

    th8SplitLoadName(zA, nA, &zLibA, &nLibA, &zSymA, &nSymA);
    th8SplitLoadName(zB, nB, &zLibB, &nLibB, &zSymB, &nSymB);

    if (nSymA != nSymB) return 0;
    if (nSymA > 0 && Th8_Memcmp(interp, zSymA, zSymB, nSymA) != 0) {
	return 0;
    }
    if (nLibA == nLibB && 0 == Th8_Memcmp(interp, zLibA, zLibB, nLibA)) {
	return 1;
    }
    return Th8_SameFile(interp, zLibA, nLibA, zLibB, nLibB) ? 1 : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8CanonLoadName --
 *
 *	Resolve the library portion of a "library:symbol" load name
 *	to its canonical absolute path (Th8_GetRealPath) and rebuild
 *	"<canonical-library>:symbol" into zBuf.  The first load of a
 *	given file therefore records the FULLY-RESOLVED path (so
 *	`info loaded` and the tracking list are stable regardless of
 *	which alias the caller used).  On any failure (no
 *	xGetRealPath, file missing, or buffer too small) the original
 *	name is used unchanged.  Sets *pzOut and *pnOut to either zBuf
 *	or the original zName.
 *
 * Why / How:
 *	So that a library is tracked under one stable identity no
 *	matter which alias first loaded it, the library portion is run
 *	through Th8_GetRealPath and the "<real>:symbol" string is
 *	rebuilt in the caller's zBuf.  Canonicalization is best-effort:
 *	if the real path cannot be obtained or would not fit in zBuf,
 *	the untouched original name is handed back so loading still
 *	proceeds.
 *
 * Results:
 *	None (void).  *pzOut and *pnOut point at either the
 *	canonicalized string in zBuf or, on any failure, the original
 *	zName.
 *
 * Side effects:
 *	May write the canonical "<real>:symbol" string into zBuf.  May
 *	invoke the platform xGetRealPath callback (which resolves the
 *	path on disk).  Changes no interpreter state.
 *
 *----------------------------------------------------------------------
 */

static void
th8CanonLoadName(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    char *zBuf,
    size_t nBuf,
    const char **pzOut,
    size_t *pnOut)
{
    const char *zLib, *zSym;
    size_t nLib, nSym;
    char zReal[2048];

    th8SplitLoadName(zName, nName, &zLib, &nLib, &zSym, &nSym);

    if (Th8_GetRealPath(interp, zLib, nLib, zReal, sizeof(zReal)) == TH8_OK) {
	size_t nReal = Th8_Strlen(interp, zReal);
	/* nReal + (':' + symbol) must fit, leaving room for NUL. */
	size_t nNeed = nReal + (nSym > 0 ? nSym + 1 : 0);

	if (nNeed < nBuf) {
	    Th8_Memcpy(interp, zBuf, zReal, nReal);
	    if (nSym > 0) {
		zBuf[nReal] = ':';
		Th8_Memcpy(interp, &zBuf[nReal + 1], zSym, nSym);
	    }
	    zBuf[nNeed] = 0;
	    *pzOut = zBuf;
	    *pnOut = nNeed;
	    return;
	}
    }
    *pzOut = zName;
    *pnOut = nName;
}

/*
 *----------------------------------------------------------------------
 *
 * th8AddLoaded --
 *
 *	Add a library name to the interpreter's loaded-library
 *	tracking list.
 *
 * Why / How:
 *	After a successful xLoad call, the library name is recorded
 *	so that (a) Th8_ListAppendLoaded can report it, (b)
 *	Th8_Unload can verify the library was actually loaded, and
 *	(c) th8FreeLoadedLibs can clean up at interpreter
 *	destruction.  A NUL-terminated copy of the name is stored
 *	in a freshly allocated Th8_LoadedLib node prepended to the
 *	singly-linked list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a Th8_LoadedLib node and a name copy.  Prepends
 *	the node to interp->pLoaded.
 *
 *----------------------------------------------------------------------
 */

static void
th8AddLoaded(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_LoadedLib *p;

    p = (Th8_LoadedLib *)TH8_ALLOC(interp, sizeof(*p));
    /* Bug 26: TH8_ALLOC returns NULL on OOM -- definitely reachable. */
    if (!p) return;
    p->zName = (char *)TH8_ALLOC_STR(interp, nName);
    if (!p->zName) {
	Th8_Free(interp, p);
	return;
    }
    Th8_Memcpy(interp, p->zName, zName, nName);
    p->zName[nName] = 0;
    p->nName = nName;
    p->nRef = 1;
    p->pNext = interp->pLoaded;
    interp->pLoaded = p;
}

/*
 *----------------------------------------------------------------------
 *
 * th8FindLoaded --
 *
 *	Search the interpreter's loaded-library list for a library
 *	with the given name.
 *
 * Why / How:
 *	Linear scan of the singly-linked list, comparing both length
 *	and content via Th8_Memcmp.  Linear scan is acceptable
 *	because the number of loaded libraries per interpreter is
 *	expected to be very small (typically single digits).
 *
 * Results:
 *	Pointer to the Th8_LoadedLib node if found, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Th8_LoadedLib *
th8FindLoaded(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_LoadedLib *p;

    for (p = interp->pLoaded; p; p = p->pNext) {
	if (th8LoadNameMatch(interp, zName, nName, p->zName, p->nName)) {
	    return p;
	}
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8RemoveLoaded --
 *
 *	Remove and free a library entry from the interpreter's
 *	loaded-library tracking list.
 *
 * Why / How:
 *	Uses a pointer-to-pointer walk to unlink the matching node
 *	without needing a separate "previous" variable.  Both the
 *	name copy and the node are freed.  If no match is found, the
 *	function is a no-op.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the matching Th8_LoadedLib node and its name.
 *	Modifies interp->pLoaded.
 *
 *----------------------------------------------------------------------
 */

static void
th8RemoveLoaded(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_LoadedLib **pp;

    for (pp = &interp->pLoaded; *pp; pp = &(*pp)->pNext) {
	Th8_LoadedLib *p = *pp;

	if (th8LoadNameMatch(interp, zName, nName, p->zName, p->nName)) {
	    *pp = p->pNext;
	    Th8_Free(interp, p->zName);
	    Th8_Free(interp, p);
	    return;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreeLoadedLibs --
 *
 *	Free all entries in the interpreter's loaded-library name list.
 *	Called by Th8_DeleteInterp during interpreter destruction.
 *
 * Why / How:
 *	Walks the singly-linked list, freeing each node's name copy
 *	and the node itself.  The list head is advanced before each
 *	free so the interp->pLoaded pointer is always valid (or
 *	NULL when done).  This function does NOT call xUnload --
 *	it only frees the tracking metadata.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all Th8_LoadedLib nodes.  Sets interp->pLoaded to
 *	NULL.
 *
 *----------------------------------------------------------------------
 */

void
th8FreeLoadedLibs(Th8_Interp *interp)
{
    while (interp->pLoaded) {
	Th8_LoadedLib *p = interp->pLoaded;

	interp->pLoaded = p->pNext;
	Th8_Free(interp, p->zName);
	Th8_Free(interp, p);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendLoaded --
 *
 *	Append the names of all loaded libraries to the list.
 *
 * Why / How:
 *	Walks the singly-linked loaded-library list and appends each
 *	library name via Th8_ListAppend.  Used by the [info loaded]
 *	command implementation to build the result list.
 *
 * Results:
 *	TH8_OK (always succeeds; allocation failures inside
 *	Th8_ListAppend are silent).
 *
 * Side effects:
 *	Appends elements to the caller-provided list (*pz, *pn).
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendLoaded(Th8_Interp *interp, char **pz, size_t *pn)
{
    Th8_LoadedLib *p;

    if (!interp) return TH8_ERROR;

    for (p = interp->pLoaded; p; p = p->pNext) {
	char *zSub = NULL;
	size_t nSub = 0;
	char zCount[16];
	int nCount;
	int n;
	int v = p->nRef;

	/*
	 * Emit each library as a 2-element sublist {name refCount}.
	 * Format the integer reference count manually to avoid pulling
	 * in vsnprintf for such a tiny value.
	 */

	nCount = 0;
	if (v == 0) {
	    zCount[nCount++] = '0';
	} else {
	    int neg = (v < 0);
	    int start;

	    if (neg) v = -v;
	    start = nCount;
	    while (v > 0) {
		zCount[nCount++] = (char)('0' + (v % 10));
		v /= 10;
	    }
	    if (neg) zCount[nCount++] = '-';
	    /* Reverse digits in place. */
	    for (n = 0; n < (nCount - start) / 2; n++) {
		char c = zCount[start + n];
		zCount[start + n] = zCount[nCount - 1 - n];
		zCount[nCount - 1 - n] = c;
	    }
	}

	Th8_ListAppend(interp, &zSub, &nSub, p->zName, p->nName);
	Th8_ListAppend(interp, &zSub, &nSub, zCount, (size_t)nCount);
	Th8_ListAppend(interp, pz, pn, zSub ? zSub : "", nSub);
	Th8_Free(interp, zSub);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EnableUnload --
 *
 *	Enable or disable the [unload] command.  The flags argument
 *	is a bitmask of TH8_UNLOAD_OK and TH8_UNLOAD_DANGEROUS.
 *	Passing 0 disables unloading entirely.  Each non-zero call
 *	regenerates the security token (same pattern as Th8_EnableLoad).
 *
 * Why / How:
 *	Unloading a shared library (dlclose / FreeLibrary) is even
 *	more dangerous than loading: it can unmap code that is still
 *	referenced by function pointers or vtables, causing
 *	use-after-free crashes.  The two-level flag design allows
 *	the embedder to permit "safe" unloads (call the _Unload
 *	entry point but keep the library mapped) separately from
 *	"dangerous" unloads (-nokeeplibrary, which actually unmaps).
 *	The random-token gate is identical to Th8_EnableLoad: a
 *	fresh 64-bit token from xRandomBytes, rejecting trivially
 *	guessable values, with both token and flags zeroed on
 *	disable.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if no entropy source is
 *	available (flags != 0 only).
 *
 * Side effects:
 *	Sets or clears interp->nUnloadToken, interp->nUnloadOk,
 *	and interp->nUnloadFlags.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EnableUnload(
    Th8_Interp *interp, /* Interpreter. */
    int flags) /* Bitmask: TH8_UNLOAD_OK, _DANGEROUS. */
{
    if (!interp) return TH8_ERROR;
    if (flags & TH8_UNLOAD_OK) {
	th8_int64_t tok = 0;

	if (interp->pPlatform->xRandomBytes) {
	    int retries = 0;
	    unsigned char buf[8];

retry:
	    /* Bug 51 (2026-06-09): reset tok at the top of each retry
	     * (matching the Th8_EnableLoad fix at L82) -- OR cannot
	     * clear bits, so a prior ~0 sticks forever otherwise. */
	    tok = 0;

	    if (++retries > 100) {
		TH8_TRACE_ERR(interp, "could not generate new unload token");
		interp->pPlatform->xPanic(
		    interp, interp->pPlatform->pCtx,
		    "could not generate new unload token", 35);
	    }
	    if (TH8_OK ==
	        interp->pPlatform
	            ->xRandomBytes(interp, interp->pPlatform->pCtx, buf, 8)) {
		size_t j;

		for (j = 0; j < 8; j++) {
		    tok |= ((th8_uint64_t)buf[j]) << (j * 8);
		}
	    }
	    /* Bug 26 (2026-06-07): plain `if` -- the retry IS the
	     * handler for reserved RNG values (see matching note at
	     * L100). */
	    if (tok == 0 || tok == ~(th8_int64_t)0 || tok == 1) {
		goto retry;
	    }
	} else {
	    return TH8_ERROR;
	}
	interp->nUnloadToken = tok;
	interp->nUnloadOk = tok;
	interp->nUnloadFlags = flags;
    } else {
	interp->nUnloadToken = 0;
	interp->nUnloadOk = 0;
	interp->nUnloadFlags = 0;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsUnloadEnabled --
 *
 *	Check whether library unloading is currently enabled.
 *
 * Why / How:
 *	Same random-token double-check as Th8_IsLoadEnabled, plus a
 *	flag check for TH8_UNLOAD_OK.  All three conditions must
 *	hold: non-zero token, token match, and flag set.
 *
 * Results:
 *	Non-zero if unloading is enabled, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8IsUnloadEnabled(Th8_Interp *interp)
{
    return interp->nUnloadOk != 0 &&
           interp->nUnloadOk == interp->nUnloadToken &&
           (interp->nUnloadFlags & TH8_UNLOAD_OK);
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsUnloadDangerous --
 *
 *	Check whether the dangerous (nokeeplibrary) unload mode is
 *	enabled.
 *
 * Why / How:
 *	Dangerous unloads actually unmap the shared library from the
 *	process address space, which can crash the process if any
 *	code pointers still reference it.  This requires both
 *	TH8_UNLOAD_OK and TH8_UNLOAD_DANGEROUS to be set, enforcing
 *	an explicit opt-in by the embedder for each privilege level.
 *
 * Results:
 *	Non-zero if dangerous unloading is enabled, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8IsUnloadDangerous(Th8_Interp *interp)
{
    return th8IsUnloadEnabled(interp) &&
           (interp->nUnloadFlags & TH8_UNLOAD_DANGEROUS);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Load --
 *
 *	Load a shared library into the interpreter via the platform's
 *	xLoad callback (dlopen on POSIX, LoadLibrary on Win32).
 *
 * Why / How:
 *	This is the security-critical entry point for binary loading.
 *	Multiple gates are checked in order: (1) the random-token
 *	gate (Th8_IsLoadEnabled), (2) the presence of the xLoad
 *	platform callback, and (3) the optional pre-load callback
 *	which can veto individual load attempts (e.g. for path
 *	validation or allowlisting).  Only after all gates pass is
 *	the platform xLoad called.  The library path (zName) is
 *	passed directly to the platform callback; it is the
 *	platform's responsibility to resolve the path and call
 *	dlopen / LoadLibrary.  On success, the library name is
 *	recorded in the tracking list for later unload and
 *	introspection.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on security gate failure,
 *	missing callback, pre-load veto, or platform load failure.
 *
 * Side effects:
 *	Loads a shared library into the process address space.
 *	Adds the library to the interpreter's tracking list.
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Load(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName, /* Library name (platform-specific). */
    size_t nName, /* Byte length of zName. */
    const char *zProc, /* Init proc name, or NULL. */
    size_t nProc) /* Byte length of zProc. */
{
    int rc;

    if (!interp) return TH8_ERROR;
    if (!Th8_IsLoadEnabled(interp)) {
	Th8_SetResult(interp, "binary loading is not enabled", TH8_NOLEN);
	return TH8_ERROR;
    }
    /* Bug 26: interp->pPlatform can be NULL during teardown.
     * Use Th8_SetResultStatic so the error path stays
     * allocation-free even when pPlatform is NULL (which
     * disables Th8_Malloc's platform-backed path); plain
     * Th8_SetResult would Memcpy via the platform during
     * teardown. */
    if (!interp->pPlatform || !interp->pPlatform->xLoad) {
	Th8_SetResultStatic(interp, "no load callback available", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Consult the pre-load callback if one is registered.
     * The callback can veto the load by returning TH8_ERROR.
     */

    if (interp->xPreLoad) {
	rc = interp->xPreLoad(interp, zName, nName, interp->pPreLoadCtx);
	if (rc != TH8_OK) {
	    if (Th8_GetResult(interp, 0)[0] == '\0') {
		Th8_SetResult(
		    interp, "load denied by pre-load callback", TH8_NOLEN);
	    }
	    return TH8_ERROR;
	}
    }

    /*
     * If the library is already in the tracking list, just bump
     * its reference count and return success without calling xLoad
     * again.  Calling xLoad twice on the same library would invoke
     * the library's _Init entry point a second time, which leads
     * to duplicate command registrations and inflated tracking
     * state.  Reference counting matches the OS-level dlopen/
     * LoadLibraryA semantics, where a redundant load is a no-op
     * other than incrementing a counter.
     */

    {
	char zCanonBuf[2048];
	const char *zTrack;
	size_t nTrack;
	Th8_LoadedLib *pExisting;

	/*
	 * Track by the CANONICAL (resolved, (st_dev,st_ino)-unique)
	 * library identity, not the literal name string.  This makes
	 * the same physical library loaded under different aliases
	 * (symlinks, ./ vs absolute, hardlinks) share one tracking
	 * entry and refcount -- so the library's _Unload runs only
	 * when the last reference drops (matching dlopen/Tcl).  The
	 * platform xLoad still receives the ORIGINAL name so the
	 * sandbox path check and dlopen behave exactly as before.
	 */
	th8CanonLoadName(
	    interp, zName, nName, zCanonBuf, sizeof(zCanonBuf), &zTrack,
	    &nTrack);

	pExisting = th8FindLoaded(interp, zTrack, nTrack);
	if (pExisting) {
	    pExisting->nRef++;
	    Th8_SetResult(interp, 0, 0);
	    return TH8_OK;
	}

	rc = interp->pPlatform->xLoad(
	    interp, interp->pPlatform->pCtx, zName, nName, zProc, nProc);
	if (rc == TH8_OK) {
	    th8AddLoaded(interp, zTrack, nTrack);
	    Th8_SetResult(interp, 0, 0);
	}
	return rc;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_Unload --
 *
 *	Unload a previously loaded shared library from the
 *	interpreter via the platform's xUnload callback.
 *
 * Why / How:
 *	Checks the unload security gate (th8IsUnloadEnabled) and, if
 *	the caller requests nokeeplibrary (bClose), additionally
 *	checks th8IsUnloadDangerous.  Verifies the library is
 *	actually in the tracking list (th8FindLoaded) to prevent
 *	unloading libraries that were never loaded through TH8.  If
 *	the platform has an xUnload callback, it is called; otherwise
 *	only the tracking entry is removed.  On success, the library
 *	is removed from the tracking list.  The platform xUnload is
 *	responsible for calling the library's _Unload entry point and
 *	optionally unmapping the library (dlclose / FreeLibrary) when
 *	bClose is set.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on security gate failure,
 *	unknown library, or platform unload failure.
 *
 * Side effects:
 *	May unload a shared library from the process address space.
 *	Removes the library from the interpreter's tracking list.
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Unload(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName, /* Library name (as passed to load). */
    size_t nName, /* Byte length of zName. */
    const char *zProc, /* Unload proc name, or NULL. */
    size_t nProc, /* Byte length of zProc. */
    int bClose) /* 0 = keeplibrary, 1 = nokeeplibrary. */
{
    int rc;
    char zCanonBuf[2048];
    const char *zTrack = zName;
    size_t nTrack = nName;

    if (!interp) return TH8_ERROR;
    if (!th8IsUnloadEnabled(interp)) {
	Th8_SetResult(interp, "unloading is not enabled", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (bClose && !th8IsUnloadDangerous(interp)) {
	Th8_SetResult(
	    interp, "-nokeeplibrary requires TH8_UNLOAD_DANGEROUS",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    /*
     * Resolve to the canonical ((st_dev,st_ino)-unique) tracking
     * identity so an unload via any alias of the library finds the
     * single shared entry created at first load.  The platform
     * xUnload still receives the ORIGINAL name.
     */
    th8CanonLoadName(
        interp, zName, nName, zCanonBuf, sizeof(zCanonBuf), &zTrack, &nTrack);

    {
	Th8_LoadedLib *pExisting = th8FindLoaded(interp, zTrack, nTrack);

	if (!pExisting) {
	    Th8_ErrorMessage(interp, "library not loaded: \"", zName, nName);
	    return TH8_ERROR;
	}

	/*
	 * If the reference count is still greater than 1 after this
	 * unload, the library remains loaded; just decrement the
	 * counter and return without calling xUnload.  The library's
	 * _Unload entry point is invoked only when the final
	 * outstanding [load] is being undone (nRef would reach 0).
	 */

	if (pExisting->nRef > 1) {
	    pExisting->nRef--;
	    Th8_SetResult(interp, 0, 0);
	    return TH8_OK;
	}
    }

    if (ALWAYS(interp->pPlatform) && interp->pPlatform->xUnload) {
	rc = interp->pPlatform->xUnload(
	    interp, interp->pPlatform->pCtx, zName, nName, zProc, nProc,
	    bClose);
    } else {
	/*
	 * No platform xUnload -- just remove from tracking.
	 * The _Unload entry point is not called.
	 */

	rc = TH8_OK;
    }
    if (rc == TH8_OK) {
	th8RemoveLoaded(interp, zTrack, nTrack);
	Th8_SetResult(interp, 0, 0);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetPreLoadCallback --
 *
 *	Register or remove a pre-load callback.  The callback is
 *	invoked before each [load] operation and can veto it by
 *	returning TH8_ERROR.
 *
 * Why / How:
 *	The pre-load callback gives the embedder a hook for policy
 *	enforcement (e.g. path allowlisting, signature verification)
 *	without requiring changes to the platform layer.  Passing
 *	NULL for xProc removes any previously registered callback.
 *	The callback and context are stored directly on the
 *	interpreter struct for O(1) access at load time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets interp->xPreLoad and interp->pPreLoadCtx.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetPreLoadCallback(
    Th8_Interp *interp, /* Interpreter. */
    Th8_PreLoadProc xProc, /* Callback (NULL to remove). */
    void *pCtx) /* Context for callback. */
{
    if (!interp) return;
    interp->xPreLoad = xProc;
    interp->pPreLoadCtx = pCtx;
}


/*
 * Th8_Interp load/unload-token field accessors (test-only).
 * Pulled out of the th8TestPerturb* helpers so the actual XOR
 * lives in src/test/th8_testlib.c (immune from MC/DC).  The
 * Bigint / Signed token equivalents are in th8_core.c.
 */

/*
 *----------------------------------------------------------------------
 *
 * th8XorInterpLoadToken --
 *
 *	Internal helper used by the dual-field random-token
 *	pattern that gates `[load]`.  XORs `mask` into the
 *	current value of `interp->nLoadToken`.  Pair with the
 *	matching `nLoadOk` XOR (in `th8_core.c`) to atomically
 *	toggle both fields between a known and an unknown state.
 *
 *	Exposed via the internal-stubs table so the test library
 *	can drive the gate from C without going through the
 *	public enable / disable wrappers.
 *
 * Why / How:
 *	`[load]` is gated by a pair of random-token fields; the gate
 *	is "open" only when the two fields agree.  Applying the same
 *	mask to `nLoadToken` here and to `nLoadOk` in th8_core.c flips
 *	the gate between the enabled and disabled states without
 *	storing a plaintext boolean an attacker could patch.  A plain
 *	XOR is used so the operation is its own inverse.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	mask   -- XOR mask to apply.
 *
 * Results:
 *	None (void).
 *
 * Side effects:
 *	Mutates `interp->nLoadToken`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8XorInterpLoadToken(Th8_Interp *interp, th8_int64_t mask)
{
    interp->nLoadToken ^= mask;
}

/*
 *----------------------------------------------------------------------
 *
 * th8XorInterpUnloadToken --
 *
 *	Internal helper used by the dual-field random-token
 *	pattern that gates `[unload]`.  XORs `mask` into the
 *	current value of `interp->nUnloadToken`.  Pair with
 *	the matching `nUnloadOk` XOR (in `th8_core.c`) to
 *	atomically toggle both fields between a known and an
 *	unknown state.
 *
 *	Exposed via the internal-stubs table so the test
 *	library can drive the gate from C without going
 *	through the public enable / disable wrappers.
 *
 * Why / How:
 *	`[unload]` uses the same dual-field random-token gate as
 *	`[load]`; applying the same mask to `nUnloadToken` here and to
 *	`nUnloadOk` in th8_core.c toggles the gate between enabled and
 *	disabled without a patchable plaintext boolean.  XOR is used so
 *	the operation is its own inverse.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	mask   -- XOR mask to apply.
 *
 * Results:
 *	None (void).
 *
 * Side effects:
 *	Mutates `interp->nUnloadToken`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8XorInterpUnloadToken(Th8_Interp *interp, th8_int64_t mask)
{
    interp->nUnloadToken ^= mask;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ClearInterpUnloadFlags --
 *
 *	Internal helper that zeroes the per-interpreter
 *	`nUnloadFlags` mask, withdrawing every previously
 *	granted unload-mode flag (`TH8_UNLOAD_OK`,
 *	`TH8_UNLOAD_DANGEROUS`, etc.).  Used by the test
 *	library to reset the gate between vectors so each
 *	test starts from a known state.
 *
 *	Exposed via the internal-stubs table; production
 *	callers should use `Th8_EnableUnload` /
 *	`Th8_DisableUnload`.
 *
 * Why / How:
 *	Test vectors need each run to start from a known unload-mode
 *	state; clearing the whole `nUnloadFlags` mask in one store
 *	withdraws every previously granted flag at once, which is
 *	simpler and more reliable than clearing them individually.
 *
 * Parameters:
 *	interp -- live interpreter.
 *
 * Results:
 *	None (void).
 *
 * Side effects:
 *	Zeros `interp->nUnloadFlags`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8ClearInterpUnloadFlags(Th8_Interp *interp)
{
    interp->nUnloadFlags = 0;
}

#endif /* TH8_ENABLE_LOAD */
