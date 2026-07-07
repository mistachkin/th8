/*
 * th8_channel.c --
 *
 *	Sandboxed temporary file I/O channel subsystem.
 *
 *	Provides a channel registry that maps abstract channel names
 *	(e.g., "./tmp/foo.tmp") to pre-allocated, size-limited
 *	temporary files.  Scripts can read, write, seek, tell, and
 *	flush these channels via the standard I/O commands.
 *
 *	All I/O is routed through platform callbacks (xInput, xOutput,
 *	xChannelControl).  This file contains no platform-specific
 *	code and no direct C runtime I/O calls.
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
#include "th8_hash.h"


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelFind --
 *
 *	Look up a channel by its abstract name in the per-interpreter
 *	channel registry.
 *
 * Why / How:
 *	Every I/O command ([puts], [gets], [seek], etc.) must resolve
 *	a script-visible channel name to the internal Th8_Channel
 *	struct.  This function performs that lookup by searching the
 *	paChannels hash table keyed by the abstract name string.
 *
 * Results:
 *	Pointer to the Th8_Channel if found, NULL otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Th8_Channel *
th8ChannelFind(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_HashEntry *pEntry;

    if (!interp->paChannels) return NULL;
    if (nName == TH8_NOLEN) nName = Th8_Strlen(interp, zName);

    pEntry = Th8_HashFind(interp, interp->paChannels, zName, nName, 0);
    /* Bug 26: tombstoned entries may have pData==NULL even after a
     * successful find -- use plain `if`, not NEVER, to survive TH8_OMIT. */
    if (!pEntry || !pEntry->pData) return NULL;
    return (Th8_Channel *)pEntry->pData;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelCreate --
 *
 *	Create a new temporary file channel via the platform's
 *	xGetTemporaryData callback and register it.
 *
 * Why / How:
 *	Implements [file tempname ?size?].  Delegates to the platform's
 *	xGetTemporaryData to allocate a size-limited temporary file,
 *	constructs an abstract channel name ("./tmp/<basename>"),
 *	wraps the platform handle in a Th8_Channel struct, and
 *	registers it in the paChannels hash.  The abstract name
 *	is returned as the interpreter result so scripts can use it
 *	with subsequent I/O commands.
 *
 * Results:
 *	TH8_OK on success with the channel name in the interpreter
 *	result; TH8_ERROR if the platform does not support temporary
 *	files or allocation fails.
 *
 * Side effects:
 *	A temporary file is created on the platform.  The channel
 *	is registered in the interpreter's channel hash.
 *
 *----------------------------------------------------------------------
 */

int
th8ChannelCreate(Th8_Interp *interp, size_t nSize)
{
    const Th8_Platform *pPlat = Th8_GetPlatform(interp);
    char *zOsPath = NULL;
    size_t nOsPath = 0;
    void *pChannel = NULL;
    char *zName = NULL;
    size_t nName = 0;
    Th8_Channel *pChan;
    Th8_HashEntry *pEntry;
    int rc;

    if (!pPlat->xGetTemporaryData) {
	Th8_SetResultStatic(
	    interp,
	    "file tempname: platform does not support "
	    "temporary data",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = pPlat->xGetTemporaryData(
        interp, pPlat->pCtx, nSize, &zOsPath, &nOsPath, &pChannel);
    if (rc != TH8_OK || !zOsPath || !pChannel) {
	Th8_SetResultStatic(
	    interp, "file tempname: cannot create temporary data", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Build the abstract channel name: "./tmp/<basename>"
     */

    {
	const char *zBase = zOsPath;
	size_t nBase = nOsPath;
	size_t k;

	/* Path-sep scan.  Split per Finding 005 sec. 5b: '\\'
	 * arm intrinsic-dead on POSIX in the test corpus;
	 * preserved as Win32 defense. */
	for (k = 0; k < nOsPath; k++) {
	    int isSep = 0;

	    if (zOsPath[k] == '/')
		isSep = 1;
	    else if (zOsPath[k] == '\\')
		isSep = 1;
	    if (isSep) {
		zBase = zOsPath + k + 1;
		nBase = nOsPath - k - 1;
	    }
	}
	Th8_StringAppend(interp, &zName, &nName, "./tmp/", 6);
	Th8_StringAppend(interp, &zName, &nName, zBase, nBase);
    }

    /*
     * Create the channel struct.
     */

    pChan = (Th8_Channel *)TH8_ALLOC(interp, sizeof(Th8_Channel));
    if (!pChan) {
	if (pPlat->xChannelControl) {
	    pPlat->xChannelControl(
	        interp, pPlat->pCtx, pChannel, TH8_CHANCTL_CLOSE, 0, 0, 0, 0);
	}
	Th8_Free(interp, zOsPath);
	Th8_Free(interp, zName);
	return TH8_ERROR;
    }
    pChan->zName = zName;
    pChan->nName = nName;
    pChan->zOsPath = zOsPath;
    pChan->nOsPath = nOsPath;
    pChan->nMaxSize = nSize;
    pChan->nPos = 0;
    pChan->pChannel = pChannel;

    /*
     * Register in the channel hash.
     */

    if (!interp->paChannels) {
	interp->paChannels = Th8_HashNew(interp);
	if (!interp->paChannels) {
	    if (pPlat->xChannelControl) {
		pPlat->xChannelControl(
		    interp, pPlat->pCtx, pChannel, TH8_CHANCTL_CLOSE, 0, 0, 0,
		    0);
	    }
	    Th8_Free(interp, pChan->zOsPath);
	    Th8_Free(interp, pChan->zName);
	    Th8_Free(interp, pChan);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }
    pEntry = Th8_HashFind(interp, interp->paChannels, zName, nName, 1);
    if (!pEntry) {
	if (pPlat->xChannelControl) {
	    pPlat->xChannelControl(
	        interp, pPlat->pCtx, pChannel, TH8_CHANCTL_CLOSE, 0, 0, 0, 0);
	}
	Th8_Free(interp, pChan->zOsPath);
	Th8_Free(interp, pChan->zName);
	Th8_Free(interp, pChan);
	return TH8_ERROR;
    }
    pEntry->pData = pChan;

    Th8_SetResult(interp, zName, nName);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelWrite --
 *
 *	Write data to an open channel.
 *
 * Why / How:
 *	Implements the write path for [puts channelId string].  Checks
 *	the size limit before writing, then delegates to the platform's
 *	xOutput callback (preferred) or falls back to xChannelControl
 *	WRITE.  After a successful write, the channel is flushed and
 *	the platform is notified via xSetTemporaryData so it can track
 *	write regions.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the channel is closed, the
 *	write would exceed the size limit, or the platform write fails.
 *
 * Side effects:
 *	Data is written to the underlying temporary file.  The
 *	channel's position (nPos) is advanced by n bytes.
 *
 *----------------------------------------------------------------------
 */

int
th8ChannelWrite(
    Th8_Interp *interp,
    Th8_Channel *pChan,
    const char *z,
    size_t n)
{
    const Th8_Platform *pPlat = Th8_GetPlatform(interp);

    /* Bug 26: pChan->pChannel is legitimately set to NULL after close
     * (see th8ChannelFreeEntry, th8ChannelClose).  Use plain `if`. */
    if (!pChan || !pChan->pChannel) {
	Th8_SetResultStatic(interp, "channel closed", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (pChan->nPos + n > pChan->nMaxSize) {
	Th8_SetResultStatic(
	    interp, "channel write: would exceed size limit", TH8_NOLEN);
	return TH8_ERROR;
    }

    {
	size_t nOffset = pChan->nPos;
	int rc;

	/*
	 * Prefer xOutput for channel writes.  The POSIX xOutput
	 * interprets pChannel as a POSIX fd via TH8_PTR2INT,
	 * which is what channel handles are.  Fall back to
	 * xChannelControl WRITE if xOutput is not available.
	 */
	if (pPlat->xOutput) {
	    rc = pPlat->xOutput(interp, pPlat->pCtx, z, n, pChan->pChannel);
	} else if (pPlat->xChannelControl) {
	    rc = pPlat->xChannelControl(
	        interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_WRITE,
	        (th8_int64_t)n, 0, 0, (void *)z);
	} else {
	    Th8_SetResultStatic(
	        interp, "channel write: not supported", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (rc != TH8_OK) {
	    Th8_SetResultStatic(interp, "channel write failed", TH8_NOLEN);
	    return TH8_ERROR;
	}

	/* Flush after write. */
	if (pPlat->xChannelControl) {
	    pPlat->xChannelControl(
	        interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_FLUSH, 0, 0,
	        0, 0);
	}

	pChan->nPos += n;

	/* Post-write notification. */
	if (pPlat->xSetTemporaryData) {
	    rc = pPlat->xSetTemporaryData(
	        interp, pPlat->pCtx, pChan->zName, pChan->nName,
	        (th8_uint64_t)nOffset, (th8_uint64_t)n);
	    if (rc != TH8_OK) return rc;
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelRead --
 *
 *	Read one line from an open channel.
 *
 * Why / How:
 *	Implements the read path for [gets channelId].  Prefers the
 *	platform's xInput callback (which reads a complete line), or
 *	falls back to xChannelControl READ byte-by-byte until a
 *	newline or EOF is reached.  Trailing \r\n / \n line endings
 *	are stripped.  After reading, the channel position is updated
 *	by querying xChannelControl TELL.
 *
 * Results:
 *	TH8_OK on success with *pzOut and *pnOut set to the line
 *	data (caller-owned); TH8_ERROR if the channel is closed or
 *	the platform read fails.
 *
 * Side effects:
 *	Allocates memory for the output buffer.  The channel's
 *	position (nPos) is updated.
 *
 *----------------------------------------------------------------------
 */

int
th8ChannelRead(
    Th8_Interp *interp,
    Th8_Channel *pChan,
    char **pzOut,
    size_t *pnOut)
{
    const Th8_Platform *pPlat = Th8_GetPlatform(interp);
    char *zData = NULL;
    size_t nData = 0;
    int rc;

    /* Bug 26: pChan->pChannel is legitimately set to NULL after close. */
    if (!pChan || !pChan->pChannel || !pzOut ||
        !pnOut) { /* Bug 23 hardening. */
	return TH8_ERROR;
    }

    *pzOut = NULL;
    *pnOut = 0;

    /*
     * Prefer xInput for channel reads.  The POSIX xInput reads
     * one line using read() and interprets pChannel as a POSIX
     * fd.  Fall back to xChannelControl READ (byte-by-byte) if
     * xInput is not available.
     */

    if (pPlat->xInput) {
	char *zLine = 0;
	size_t nLine = 0;

	rc = pPlat->xInput(
	    interp, pPlat->pCtx, &zLine, &nLine, pChan->pChannel);
	/* Bug 26: a custom platform's xInput could in theory return
	 * TH8_OK without setting *zLine; use plain `if`.  Split per
	 * Finding 005 sec. 5b: C2 (!zLine with TH8_OK) intrinsic-
	 * dead in the test corpus -- default platform's xInput
	 * always sets *zLine when returning TH8_OK. */
	if (rc != TH8_OK) return TH8_ERROR;
	if (!zLine) return TH8_ERROR;
	/* Strip trailing \r\n or \n. */
	if (nLine > 0 && zLine[nLine - 1] == '\n') nLine--;
	if (nLine > 0 && zLine[nLine - 1] == '\r') nLine--;
	zLine[nLine] = '\0';
	zData = zLine;
	nData = nLine;
    } else if (pPlat->xChannelControl) {
	/*
	 * Fallback: read byte-by-byte until newline or EOF.
	 */
	char zBuf[4096];
	size_t nRead = 0;

	while (nRead < sizeof(zBuf) - 1) {
	    th8_int64_t nGot = 0;

	    rc = pPlat->xChannelControl(
	        interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_READ, 1, 0,
	        &nGot, &zBuf[nRead]);
	    if (rc != TH8_OK || nGot == 0) break;
	    if (zBuf[nRead] == '\n') break;
	    nRead++;
	}
	if (nRead == 0 && rc != TH8_OK) {
	    return TH8_ERROR;
	}
	if (nRead > 0 && zBuf[nRead - 1] == '\r') {
	    nRead--;
	}
	zData = (char *)TH8_ALLOC_STR(interp, nRead);
	if (!zData) return TH8_ERROR;
	Th8_Memcpy(interp, zData, zBuf, nRead);
	zData[nRead] = '\0';
	nData = nRead;
    } else {
	return TH8_ERROR;
    }

    /* Update position from platform. */
    if (pPlat->xChannelControl) {
	th8_int64_t pos = 0;

	pPlat->xChannelControl(
	    interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_TELL, 0, 0,
	    &pos, 0);
	pChan->nPos = (size_t)pos;
    }

    *pzOut = zData;
    *pnOut = nData;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelSeek --
 *
 *	Reposition the read/write pointer of an open channel.
 *
 * Why / How:
 *	Implements [seek channelId offset ?origin?].  Delegates to
 *	xChannelControl SEEK, then reads back the actual position
 *	via TELL.  If the resulting position exceeds the channel's
 *	size limit, it is clamped back to nMaxSize and an error is
 *	returned.  This prevents scripts from seeking past the
 *	sandbox boundary.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the channel is closed, seek
 *	is unsupported, the platform seek fails, or the position
 *	would exceed the size limit.
 *
 * Side effects:
 *	The channel's position (nPos) is updated.
 *
 *----------------------------------------------------------------------
 */

int
th8ChannelSeek(
    Th8_Interp *interp,
    Th8_Channel *pChan,
    long offset,
    int whence)
{
    const Th8_Platform *pPlat = Th8_GetPlatform(interp);
    th8_int64_t pos;

    /* Bug 26: pChan->pChannel is legitimately set to NULL after close. */
    if (!pChan || !pChan->pChannel) {
	Th8_SetResultStatic(interp, "channel closed", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!pPlat->xChannelControl) {
	Th8_SetResultStatic(interp, "seek not supported", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (pPlat->xChannelControl(
            interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_SEEK,
            (th8_int64_t)offset, whence, 0, 0) != TH8_OK) {
	Th8_SetResultStatic(interp, "seek failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Read back the position. */
    pos = 0;
    pPlat->xChannelControl(
        interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_TELL, 0, 0, &pos,
        0);
    pChan->nPos = (size_t)pos;

    if (pChan->nPos > pChan->nMaxSize) {
	/* Clamp to max. */
	pPlat->xChannelControl(
	    interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_SEEK,
	    (th8_int64_t)pChan->nMaxSize, 0 /*SEEK_SET*/, 0, 0);
	pChan->nPos = pChan->nMaxSize;
	Th8_SetResultStatic(
	    interp, "seek: would exceed size limit", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ChannelTell --
 *
 *	Return the current read/write position of a channel.
 *
 * Why / How:
 *	Implements [tell channelId].  Returns the cached nPos value
 *	which is kept in sync with the platform after every read,
 *	write, or seek operation.  No platform callback is needed
 *	because nPos is always up to date.
 *
 * Results:
 *	The current byte offset as a long, or -1 if pChan is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

long
th8ChannelTell(Th8_Channel *pChan)
{
    if (!pChan) return -1;
    return (long)pChan->nPos;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ChannelFlush --
 *
 *	Flush any buffered data on an open channel to the underlying
 *	storage.
 *
 * Why / How:
 *	Implements [flush channelId].  Delegates to the platform's
 *	xChannelControl FLUSH operation.  If the platform does not
 *	provide xChannelControl, the flush is a no-op (returns OK)
 *	since writes are already unbuffered in that case.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the channel is closed.
 *
 * Side effects:
 *	Platform-buffered data may be written to disk.
 *
 *----------------------------------------------------------------------
 */

int
th8ChannelFlush(Th8_Interp *interp, Th8_Channel *pChan)
{
    const Th8_Platform *pPlat = Th8_GetPlatform(interp);

    /* Bug 26: pChan->pChannel is legitimately set to NULL after close. */
    if (!pChan || !pChan->pChannel) {
	Th8_SetResultStatic(interp, "channel closed", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (pPlat->xChannelControl) {
	pPlat->xChannelControl(
	    interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_FLUSH, 0, 0, 0,
	    0);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelFreeEntry --
 *
 *	Hash iteration callback: close and free a single channel
 *	entry.
 *
 * Why / How:
 *	Used by th8ChannelCleanup to tear down all channels during
 *	interpreter deletion.  Closes the platform channel handle via
 *	xChannelControl CLOSE, deletes the temporary file via
 *	xDeleteTemporaryData, then frees the Th8_Channel struct and
 *	its owned strings.
 *
 * Results:
 *	TH8_OK always (continue iterating).
 *
 * Side effects:
 *	The underlying temporary file is closed and deleted.  All
 *	memory owned by the channel is freed.
 *
 *----------------------------------------------------------------------
 */

static int
th8ChannelFreeEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;
    Th8_Channel *pChan;

    /* Bug 26: xFree callback contract -- pEntry should never be NULL,
     * but tombstoning hash impls may invoke callbacks with pData==NULL;
     * use plain `if` so the guard survives TH8_OMIT.  Split per
     * Finding 005 sec. 5b: C1 (!pEntry) intrinsic-dead per the
     * iterator contract. */
    if (!pEntry) return TH8_OK;
    if (!pEntry->pData) return TH8_OK;
    pChan = (Th8_Channel *)pEntry->pData;

    if (pChan->pChannel) {
	const Th8_Platform *pPlat = Th8_GetPlatform(interp);

	if (pPlat->xChannelControl) {
	    pPlat->xChannelControl(
	        interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_CLOSE, 0, 0,
	        0, 0);
	}
	pChan->pChannel = NULL;
    }

    {
	const Th8_Platform *pPlat = Th8_GetPlatform(interp);

	if (pPlat->xDeleteTemporaryData) {
	    pPlat->xDeleteTemporaryData(
	        interp, pPlat->pCtx, pChan->zOsPath, pChan->nOsPath);
	}
    }

    Th8_Free(interp, pChan->zOsPath);
    Th8_Free(interp, pChan->zName);
    Th8_Free(interp, pChan);
    pEntry->pData = NULL;

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelCleanup --
 *
 *	Close all open channels and destroy the channel registry.
 *
 * Why / How:
 *	Called during interpreter deletion (Th8_DeleteInterp) to
 *	ensure no file handles or temporary files are leaked.
 *	Iterates the paChannels hash via th8ChannelFreeEntry to
 *	close and delete each channel, then frees the hash itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All channels are closed, all temporary files are deleted,
 *	and the paChannels hash is freed and NULLed.
 *
 *----------------------------------------------------------------------
 */

void
th8ChannelCleanup(Th8_Interp *interp)
{
    if (interp->paChannels) {
	Th8_HashIterate(
	    interp, interp->paChannels, th8ChannelFreeEntry, (void *)interp);
	Th8_HashDelete(interp, interp->paChannels);
	interp->paChannels = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelClose --
 *
 *	Explicitly close a single temporary channel.  Consults the
 *	platform's xCloseTemporaryData callback (if set) to allow
 *	the host to veto the close.  On success, the channel handle
 *	is closed, the temporary file is deleted, and the channel
 *	is removed from the registry.
 *
 * Why / How:
 *	Implements [close channelId].  Unlike th8ChannelCleanup (which
 *	is unconditional), this function first consults the platform's
 *	xCloseTemporaryData callback, allowing the host to veto the
 *	close (e.g. if the file is still being read by another
 *	component).  After approval, closes the handle, deletes the
 *	file, removes the entry from the hash, and frees all memory.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the channel is not found,
 *	already closed, or the platform vetoes the close.
 *
 * Side effects:
 *	The channel is closed, the temporary file is deleted, and
 *	the channel struct is freed and removed from the registry.
 *
 *----------------------------------------------------------------------
 */

int
th8ChannelClose(Th8_Interp *interp, const char *zName, size_t nName)
{
    const Th8_Platform *pPlat = Th8_GetPlatform(interp);
    Th8_HashEntry *pEntry;
    Th8_Channel *pChan;

    if (!interp->paChannels) {
	Th8_SetResultStatic(interp, "channel not found", TH8_NOLEN);
	return TH8_ERROR;
    }

    pEntry = Th8_HashFind(interp, interp->paChannels, zName, nName, 0);
    /* Bug 26: tombstoned entries may have pData==NULL even after a
     * successful find -- use plain `if`. */
    if (!pEntry || !pEntry->pData) {
	Th8_ErrorMessage(interp, "channel not found:", zName, nName);
	return TH8_ERROR;
    }

    pChan = (Th8_Channel *)pEntry->pData;

    if (!pChan->pChannel) {
	Th8_ErrorMessage(interp, "channel already closed:", zName, nName);
	return TH8_ERROR;
    }

    /*
     * Consult the platform: can we close this channel?
     */
    if (pPlat->xCloseTemporaryData) {
	int rc = pPlat->xCloseTemporaryData(
	    interp, pPlat->pCtx, pChan->zName, pChan->nName, pChan->pChannel);
	if (rc != TH8_OK) {
	    Th8_SetResultStatic(
	        interp, "close vetoed by platform", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /*
     * Close the channel handle.
     */
    if (pPlat->xChannelControl) {
	pPlat->xChannelControl(
	    interp, pPlat->pCtx, pChan->pChannel, TH8_CHANCTL_CLOSE, 0, 0, 0,
	    0);
    }
    pChan->pChannel = NULL;

    /*
     * Delete the temporary file.
     */
    if (pPlat->xDeleteTemporaryData) {
	pPlat->xDeleteTemporaryData(
	    interp, pPlat->pCtx, pChan->zOsPath, pChan->nOsPath);
    }

    /*
     * Remove from registry and free.
     */
    Th8_Free(interp, pChan->zOsPath);
    Th8_Free(interp, pChan->zName);
    Th8_Free(interp, pChan);
    pEntry->pData = NULL;

    Th8_HashRemove(interp, interp->paChannels, zName, nName);

    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ChannelListCallback --
 *
 *	Hash iteration callback: append a channel's abstract name
 *	to a Tcl list.
 *
 * Why / How:
 *	Used by th8ChannelList to collect all open channel names.
 *	Extracts the Th8_Channel from the hash entry's pData and
 *	appends its zName to the caller's list buffer via
 *	Th8_ListAppend.
 *
 * Results:
 *	TH8_OK always (continue iterating).
 *
 * Side effects:
 *	The list buffer may be extended.
 *
 *----------------------------------------------------------------------
 */

static int
th8ChannelListCallback(Th8_HashEntry *pEntry, void *pCtx)
{
    void **aCtx = (void **)pCtx;
    Th8_Interp *interp = (Th8_Interp *)aCtx[0];
    char **pz = (char **)aCtx[1];
    size_t *pn = (size_t *)aCtx[2];

    /* Bug 26 family: plain guard; channel close tombstones the
     * hash entry (pData = NULL), so a visited entry can have NULL
     * pData.  ALWAYS() asserted (debug) / NULL-deref'd (MC/DC).
     * Nested per Finding 005 sec. 5b: C1 (!pEntry) intrinsic-
     * dead per the iterator contract; pData-NULL is the live arm. */
    if (pEntry)
	if (pEntry->pData) {
	    Th8_Channel *pChan = (Th8_Channel *)pEntry->pData;

	    Th8_ListAppend(interp, pz, pn, pChan->zName, pChan->nName);
	}
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ChannelList --
 *
 *	Build a Tcl list of all open channel names.
 *
 * Why / How:
 *	Implements [file channels].  Iterates the paChannels hash
 *	via th8ChannelListCallback to append each channel's abstract
 *	name to the caller's list buffer.  Returns early if no
 *	channels have been created.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	The list buffer (*pz, *pn) is extended with channel names.
 *
 *----------------------------------------------------------------------
 */

int
th8ChannelList(Th8_Interp *interp, char **pz, size_t *pn)
{
    void *aCtx[3];

    if (!interp->paChannels) return TH8_OK;

    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(
        interp, interp->paChannels, th8ChannelListCallback, (void *)aCtx);
    return TH8_OK;
}
