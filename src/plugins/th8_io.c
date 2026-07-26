/*
 * th8_io.c -- I/O plugin for TH8.
 *
 * Implements the I/O commands: gets and puts.
 *
 * This file is part of the plugin architecture.  The commands
 * are registered via Th8_RegisterPlugin using the static
 * command table returned by th8IoGetCommands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_util.h"
#include "th8_plugin.h"

#if defined(TH8_PLUGIN_IO)


/*
 *----------------------------------------------------------------------
 *
 * gets_command --
 *
 *	Read one line of input.
 *
 *	gets CHANNEL ?VARNAME?
 *
 *	Reads a line from CHANNEL.  If VARNAME is given, the line is
 *	stored in VARNAME and the return value is the character count
 *	(or -1 on failure).  Without VARNAME, the return value is
 *	the data itself (failure is an error).
 *
 *	TH8 supports one input channel: "stdin", routed through
 *	the platform's xInput callback (the inverse of xOutput).
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure (without VARNAME)
 *	or if the channel is not available.
 *
 * Why / How:
 *	Implements the Tcl [gets] command.  First checks for a
 *	registered temporary-file channel, then falls back to
 *	"stdin" via the platform's xInput callback.  Trailing
 *	newlines are stripped from the raw input.  In variable mode
 *	failure returns -1 instead of raising an error, matching
 *	Tcl 8.x semantics where EOF on a channel is not an error.
 *
 * Side effects:
 *	May set a variable.  Reads from the host environment.
 *
 *----------------------------------------------------------------------
 */

static int
gets_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zData = 0;
    size_t nData = 0;
    int rc;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "gets channel ?varname?");
    }

#  if !defined(TH8_ENABLE_VARIABLES)
    if (argc == 3) {
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    /*
     * Check for a registered channel (temp file).
     */
    {
	Th8_Channel *pChan = th8ChannelFind(interp, argv[1], argl[1]);

	if (pChan) {
	    rc = th8ChannelRead(interp, pChan, &zData, &nData);
	    goto got_data;
	}
    }

    /*
     * Standard channel: only "stdin" is supported.
     */

    if (!th8StrEq(interp, argv[1], argl[1], "stdin")) {
	Th8_ErrorMessage(
	    interp, "channel not available:", argv[1], TH8_LEN(argl[1]));
	return TH8_ERROR;
    }

    /*
     * Read available data via the platform.
     */

    rc = Th8_Input(interp, &zData, &nData, TH8_TRANSLATE_NONE);

    /*
     * Strip trailing newline if present (convenience for
     * hosts that return line-oriented data).
     */

    /* Th8_Input returns TH8_ERROR on zero-byte reads (see
     * th8PosixInput L2404), so rc == TH8_OK guarantees nData
     * > 0 here.  The nData > 0 sub-check is a belt-and-braces
     * defensive test, never F when reached. */
    if (rc == TH8_OK && ALWAYS(nData > 0) && zData[nData - 1] == '\n') {
	nData--;
	if (nData > 0 && zData[nData - 1] == '\r') {
	    nData--;
	}
    }

got_data:
    if (argc == 3) {
#  if defined(TH8_ENABLE_VARIABLES)
	/*
	 * Store in variable, return character count (or -1).
	 */

	if (rc == TH8_OK) {
	    Th8_SetVar(interp, argv[2], TH8_LEN(argl[2]), zData, nData);
	    Th8_SetResultInt(interp, Th8_Utf8Len(zData, nData));
	} else {
	    /*
	     * Failure: set variable to empty, return -1.
	     */

	    Th8_SetVar(interp, argv[2], TH8_LEN(argl[2]), "", 0);
	    Th8_SetResultInt(interp, -1);
	    rc = TH8_OK;  /* Not an error in varname mode */
	}
#  endif
    } else {
	/*
	 * No variable: return the data, or propagate error.
	 */

	if (rc == TH8_OK) {
	    Th8_SetResult(interp, zData, nData);
	}
	/* If rc is TH8_ERROR, the error message from
	 * Th8_Input is already set. */
    }

    Th8_Free(interp, zData);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * puts_command --
 *
 *	Write a string to output.
 *
 *	puts ?-nonewline? ?CHANNEL? STRING
 *
 *	Routes output through the platform's xOutput callback.
 *	Only "stdout" is accepted as a channel name.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *
 * Why / How:
 *	Implements the Tcl [puts] command.  Output is assembled into
 *	a single buffer (with optional trailing newline) and delivered
 *	through the platform's xOutput callback.  Channel routing
 *	checks for registered temporary-file channels first, then
 *	accepts "stdout" as the only standard channel.
 *
 * Side effects:
 *	Writes to the host environment.
 *
 *----------------------------------------------------------------------
 */

static int
puts_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int noNewline = 0;
    int iArg = 1;
    const char *zStr;
    size_t nStr;
    char *zOut = 0;
    size_t nOut = 0;

    if (argc < 2 || argc > 4) {
	return Th8_WrongNumArgs(interp, "puts ?-nonewline? ?channel? string");
    }

    if (th8StrEq(interp, argv[iArg], argl[iArg], "-nonewline")) {
	noNewline = 1;
	iArg++;
    }

    /*
     * If two args remain, the first is the channel name
     * and the second is the string.  Only "stdout" is
     * accepted as a channel name.
     */

    if (argc - iArg > 1) {
	/*
	 * Two args remain: first is channel, second is string.
	 * Check for registered temp file channels first,
	 * then "stdout", then reject.
	 */
	Th8_Channel *pChan = th8ChannelFind(interp, argv[iArg], argl[iArg]);

	if (pChan) {
	    /* Write to the temp file channel. */
	    int rc;

	    iArg++;
	    if (iArg >= argc) {
		return Th8_WrongNumArgs(
		    interp, "puts ?-nonewline? channel string");
	    }
	    zStr = argv[iArg];
	    nStr = TH8_LEN(argl[iArg]);

	    /*
	     * Sensitivity boundary: a sensitive value must never be
	     * written to a channel as plaintext.  Reject BEFORE any
	     * write with a fixed diagnostic that discloses no payload.
	     */

	    if (TH8_SENSITIVE(argl[iArg])) {
		Th8_SetResultStatic(
		    interp, "sensitive value cannot be written", TH8_NOLEN);
		return TH8_ERROR;
	    }

	    rc = th8ChannelWrite(interp, pChan, zStr, nStr);
	    if (rc != TH8_OK) return rc;
	    if (!noNewline) {
		rc = th8ChannelWrite(interp, pChan, "\n", 1);
		if (rc != TH8_OK) return rc;
	    }
	    Th8_ClearResult(interp);
	    return TH8_OK;
	}

	if (!th8StrEq(interp, argv[iArg], argl[iArg], "stdout")) {
	    Th8_ErrorMessage(
	        interp, "channel not available:", argv[iArg],
	        TH8_LEN(argl[iArg]));
	    return TH8_ERROR;
	}
	iArg++;
    }
    if (iArg >= argc) {
	return Th8_WrongNumArgs(interp, "puts ?-nonewline? ?channel? string");
    }

    zStr = argv[iArg];
    nStr = TH8_LEN(argl[iArg]);

    /*
     * Sensitivity boundary: a sensitive value must never be routed to
     * the host xOutput callback as plaintext.  Reject BEFORE buffer
     * assembly or Th8_Output with a fixed diagnostic that discloses no
     * payload.
     */

    if (TH8_SENSITIVE(argl[iArg])) {
	Th8_SetResultStatic(
	    interp, "sensitive value cannot be written", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Route through platform xOutput.
     */

    {
	int rc;

	TH8_STR_APPEND(interp, &zOut, &nOut, zStr, nStr);
	if (!noNewline) {
	    TH8_STR_APPEND(interp, &zOut, &nOut, "\n", 1);
	}
	rc = Th8_Output(interp, zOut, nOut, TH8_TRANSLATE_NONE);
	Th8_Free(interp, zOut);
	if (rc != TH8_OK) {
	    return TH8_ERROR;
	}
    }

    Th8_ClearResult(interp);
    return TH8_OK;

oom:
    Th8_Free(interp, zOut);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * seek_command --
 *
 *	Reposition the read/write offset of a channel.
 *
 *	seek CHANNEL OFFSET ?ORIGIN?
 *
 *	ORIGIN may be "start" (default), "current", or "end".
 *	Only registered temporary-file channels are supported.
 *
 * Why / How:
 *	Implements the Tcl [seek] command.  Resolves the channel
 *	name through th8ChannelFind, converts the origin keyword
 *	to a numeric whence value (0/1/2), and delegates to
 *	th8ChannelSeek.  Standard channels (stdin/stdout) cannot
 *	be seeked.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the channel is not found
 *	or the origin keyword is invalid.
 *
 * Side effects:
 *	Changes the file position of the channel.
 *
 *----------------------------------------------------------------------
 */

static int
seek_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Channel *pChan;
    th8_int64_t offset;
    int whence = 0;  /* start */

    (void)ctx;

    if (argc < 3 || argc > 4) {
	return Th8_WrongNumArgs(interp, "seek channel offset ?origin?");
    }

    pChan = th8ChannelFind(interp, argv[1], argl[1]);
    if (!pChan) {
	Th8_ErrorMessage(interp, "channel not available:", argv[1], argl[1]);
	return TH8_ERROR;
    }

    if (Th8_ToWideInt(interp, argv[2], argl[2], &offset) != TH8_OK) {
	return TH8_ERROR;
    }

    if (argc == 4) {
	if (th8StrEq(interp, argv[3], argl[3], "start")) {
	    whence = 0;  /* start */
	} else if (th8StrEq(interp, argv[3], argl[3], "current")) {
	    whence = 1;  /* current */
	} else if (th8StrEq(interp, argv[3], argl[3], "end")) {
	    whence = 2;  /* end */
	} else {
	    Th8_ErrorMessage(interp, "bad origin \"", argv[3], argl[3]);
	    return TH8_ERROR;
	}
    }

    return th8ChannelSeek(interp, pChan, (long)offset, whence);
}


/*
 *----------------------------------------------------------------------
 *
 * tell_command --
 *
 *	Return the current file position of a channel.
 *
 *	tell CHANNEL
 *
 * Why / How:
 *	Implements the Tcl [tell] command.  Resolves the channel
 *	name through th8ChannelFind and returns the byte offset
 *	via th8ChannelTell.  Only registered temporary-file
 *	channels are supported.
 *
 * Results:
 *	TH8_OK with the position as result; TH8_ERROR if the
 *	channel is not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
tell_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_Channel *pChan;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "tell channel");
    }

    pChan = th8ChannelFind(interp, argv[1], argl[1]);
    if (!pChan) {
	Th8_ErrorMessage(interp, "channel not available:", argv[1], argl[1]);
	return TH8_ERROR;
    }

    return Th8_SetResultWideInt(interp, (th8_int64_t)th8ChannelTell(pChan));
}


/*
 *----------------------------------------------------------------------
 *
 * close_command --
 *
 *	Close one or all channels.
 *
 *	close ?CHANNEL?
 *
 *	With a channel argument: close a specific temporary file
 *	channel.  The channel handle is closed, the file is deleted,
 *	and the channel is removed from the registry.  The platform's
 *	xCloseTemporaryData callback (if set) can veto the close.
 *
 *	Without arguments: close ALL temporary file channels.  This
 *	provides a clean "close everything" for fail-safe cleanup
 *	without needing loops or introspection.
 *
 *	Standard channels (stdin, stdout, stderr) are detached
 *	(redirected to NULL) rather than truly closed, matching
 *	Tcl 8.x behavior.
 *
 * Why / How:
 *	Implements the Tcl [close] command.  Three code paths:
 *	(1) no argument -- enumerates all channels via th8ChannelList
 *	and closes each non-standard one; (2) standard channel name --
 *	redirects the corresponding I/O callback to a sentinel so
 *	subsequent reads return EOF and writes fail; (3) temp-file
 *	channel -- delegates to th8ChannelClose which handles file
 *	deletion and the veto callback.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the channel is not found.
 *
 * Side effects:
 *	Closes file handles.  May delete temporary files.  Detaches
 *	standard I/O callbacks when closing stdin/stdout/stderr.
 *
 *----------------------------------------------------------------------
 */

static int
close_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc > 2) {
	return Th8_WrongNumArgs(interp, "close ?channel?");
    }

    if (argc == 1) {
	/*
	 * No argument: close all temporary channels.
	 * This is equivalent to th8ChannelCleanup but goes
	 * through the xCloseTemporaryData veto path for each.
	 */
	char *zList = 0;
	size_t nList = 0;
	char **azElem = 0;
	size_t *anElem = 0;
	int nCount = 0;
	int i;

	th8ChannelList(interp, &zList, &nList);
	/* th8ChannelList returns either zList=NULL (no
	 * channels) or a populated list with nList > 0; it
	 * never returns a non-NULL pointer with zero length.
	 * Wrap nList > 0 ALWAYS. */
	if (zList && ALWAYS(nList > 0)) {
	    Th8_SplitList(
	        interp, zList, nList, &azElem, &anElem, &nCount,
	        TH8_LIST_NONE);
	    for (i = 0; i < nCount; i++) {
		/* Skip stdin/stdout -- defensive.  th8ChannelList
		 * enumerates paChannels (the temp-channel hash) and
		 * th8ChannelCreate names temp channels
		 * "./tmp/<basename>", so this list "shouldn't" contain
		 * "stdin"/"stdout"/"stderr".
		 *
		 * Bug 26 (2026-06-07): plain check rather than NEVER --
		 * collapsing this under TH8_OMIT would close stdin/
		 * stdout/stderr if the enumeration ever changes.
		 * Split per Finding 005 sec. 5b: all three C-pairs
		 * are intrinsic-dead in the test corpus because
		 * th8ChannelCreate uses the "./tmp/" prefix for temp
		 * channels, so this list never contains stdin/stdout/
		 * stderr in practice. */
		if (th8StrEq(interp, azElem[i], anElem[i], "stdin")) {
		    continue;
		}
		if (th8StrEq(interp, azElem[i], anElem[i], "stdout")) {
		    continue;
		}
		if (th8StrEq(interp, azElem[i], anElem[i], "stderr")) {
		    continue;
		}
		th8ChannelClose(interp, azElem[i], anElem[i]);
	    }
	    Th8_Free(interp, azElem);
	    /* anElem is interior to the azElem block. */
	}
	Th8_Free(interp, zList);
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    /*
     * Standard channels: "closing" stdin/stdout/stderr detaches
     * them (redirects to NULL).  After close, gets stdin returns
     * -1 (EOF) and puts stdout fails.  This matches Tcl 8.x
     * behavior where close stdin is valid.
     */
    if (th8StrEq(interp, argv[1], argl[1], "stdin")) {
	Th8_RedirectInput(interp, TH8_INT2PTR(-1));
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (th8StrEq(interp, argv[1], argl[1], "stdout")) {
	Th8_RedirectOutput(interp, TH8_INT2PTR(-1));
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (th8StrEq(interp, argv[1], argl[1], "stderr")) {
	Th8_RedirectErrorOutput(interp, TH8_INT2PTR(-1));
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    return th8ChannelClose(interp, argv[1], argl[1]);
}


/*
 *----------------------------------------------------------------------
 *
 * flush_command --
 *
 *	Flush buffered output on a channel.
 *
 *	flush CHANNEL
 *
 * Why / How:
 *	Implements the Tcl [flush] command.  For registered
 *	temporary-file channels, delegates to th8ChannelFlush.
 *	For "stdout", sends a zero-length write through the
 *	platform's xOutput callback to trigger a flush.  Other
 *	channel names are rejected.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the channel is not found.
 *
 * Side effects:
 *	Flushes pending data to the underlying file or output device.
 *
 *----------------------------------------------------------------------
 */

static int
flush_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;
    Th8_Channel *pChan;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "flush channel");
    }

    pChan = th8ChannelFind(interp, argv[1], argl[1]);
    if (!pChan) {
	/* flush stdout: route through platform. */
	if (th8StrEq(interp, argv[1], argl[1], "stdout")) {
	    Th8_Output(interp, "", 0, 0);
	    Th8_ClearResult(interp);
	    return TH8_OK;
	}
	Th8_ErrorMessage(interp, "channel not available:", argv[1], argl[1]);
	return TH8_ERROR;
    }

    rc = th8ChannelFlush(interp, pChan);
    if (rc != TH8_OK) return rc;

    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * read_command --
 *
 *	Form 1: read ?-nonewline? channelId
 *	    Read all data from the channel until EOF.  With
 *	    -nonewline, a trailing newline is stripped.
 *
 *	Form 2: read channelId numChars
 *	    Read exactly numChars characters (or fewer if EOF
 *	    is reached before numChars are available).
 *
 *	Only "stdin" and registered temporary file channels
 *	are supported.
 *
 * Why / How:
 *	Implements the Tcl [read] command.  Disambiguates between
 *	the two Tcl forms by checking whether argv[1] is "-nonewline".
 *	For temp-file channels, reads line-by-line via th8ChannelRead
 *	and reassembles the data.  For stdin, loops on the platform's
 *	xInput callback until EOF or the character limit is reached.
 *	The -nonewline flag and numChars trimming are applied after
 *	all data has been collected.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the channel is not found
 *	or numChars is negative.
 *
 * Side effects:
 *	Reads from the host environment or a temporary file.
 *
 *----------------------------------------------------------------------
 */

static int
read_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zChan;
    size_t nChan;
    int bNoNewline = 0;
    th8_int64_t numChars = -1;  /* -1 = read all */
    int iChanArg;
    char *zAll = 0;
    size_t nAll = 0;
    char *zLine = 0;
    size_t nLine = 0;

    (void)ctx;

    if (argc < 2 || argc > 3) {
	return Th8_WrongNumArgs(
	    interp, "read ?-nonewline? channelId ?numChars?");
    }

    /*
     * Parse arguments.  Tcl has two forms:
     *   read ?-nonewline? channelId     (channel is last)
     *   read channelId numChars         (channel is first)
     *
     * Disambiguate: if argc==3 and argv[1] is "-nonewline",
     * it's Form 1.  Otherwise if argc==3, it's Form 2.
     */

    if (argc == 3 && argl[1] == 10 &&
        Th8_Memcmp(interp, argv[1], "-nonewline", 11) == 0) {
	/* Form 1: read -nonewline channelId */
	bNoNewline = 1;
	iChanArg = 2;
    } else if (argc == 3) {
	/* Form 2: read channelId numChars */
	iChanArg = 1;
	if (Th8_ToWideInt(interp, argv[2], argl[2], &numChars) != TH8_OK) {
	    return TH8_ERROR;
	}
	if (numChars < 0) {
	    Th8_SetResultStatic(
	        interp, "read: numChars must be non-negative", TH8_NOLEN);
	    return TH8_ERROR;
	}
    } else {
	/* Form 1: read channelId */
	iChanArg = 1;
    }

    zChan = argv[iChanArg];
    nChan = argl[iChanArg];

    /*
     * Check for registered temp file channel first.
     */
    {
	Th8_Channel *pChan = th8ChannelFind(interp, zChan, nChan);

	if (pChan) {
	    while (th8ChannelRead(interp, pChan, &zLine, &nLine) == TH8_OK &&
	           ALWAYS(zLine)) {
		TH8_STR_APPEND(interp, &zAll, &nAll, zLine, nLine);
		TH8_STR_APPEND(interp, &zAll, &nAll, "\n", 1);
		Th8_Free(interp, zLine);
		zLine = 0;

		if (numChars >= 0 && (th8_int64_t)nAll >= numChars) {
		    break;
		}
	    }

	    /* Trim to numChars if specified. */
	    if (numChars >= 0 && nAll > (size_t)numChars) {
		nAll = (size_t)numChars;
	    }

	    /* Strip trailing newline if -nonewline.  The line
	     * loop above unconditionally appends '\n' to every
	     * line read (L705-706), and bNoNewline is mutually
	     * exclusive with the numChars truncation at L717
	     * (Form 1 vs Form 2 argument parsing), so whenever
	     * bNoNewline=T and nAll>0 we are guaranteed the
	     * final byte is '\n' -- the third sub-condition is
	     * a defensive invariant, never F. */
	    if (bNoNewline && nAll > 0 && ALWAYS(zAll[nAll - 1] == '\n')) {
		nAll--;
	    }

	    Th8_SetResult(interp, zAll ? zAll : "", nAll);
	    Th8_Free(interp, zAll);
	    return TH8_OK;
	}
    }

    /*
     * Standard channel: only "stdin" is supported.
     */
    if (!th8StrEq(interp, zChan, nChan, "stdin")) {
	Th8_ErrorMessage(interp, "channel not available:", zChan, nChan);
	return TH8_ERROR;
    }

    /*
     * Read from stdin by calling Th8_Input in a loop.
     */
    {
	int rc;

	while (1) {
	    rc = Th8_Input(interp, &zLine, &nLine, 0);
	    if (rc != TH8_OK || !zLine || nLine == 0) {
		Th8_Free(interp, zLine);
		break;
	    }
	    TH8_STR_APPEND(interp, &zAll, &nAll, zLine, nLine);
	    Th8_Free(interp, zLine);
	    zLine = 0;

	    if (numChars >= 0 && (th8_int64_t)nAll >= numChars) {
		break;
	    }
	}

	/* Trim to numChars if specified. */
	if (numChars >= 0 && nAll > (size_t)numChars) {
	    nAll = (size_t)numChars;
	}

	/* Strip trailing newline if -nonewline. */
	if (bNoNewline && nAll > 0 && zAll[nAll - 1] == '\n') {
	    nAll--;
	}

	Th8_SetResult(interp, zAll ? zAll : "", nAll);
	Th8_Free(interp, zAll);
    }
    return TH8_OK;

oom:
    Th8_Free(interp, zLine);
    Th8_Free(interp, zAll);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8IoCommands[] = {
    {1, 0, "close", close_command}, {1, 0, "flush", flush_command},
    {1, 0, "gets", gets_command},   {1, 0, "puts", puts_command},
    {1, 0, "read", read_command},   {1, 0, "seek", seek_command},
    {1, 0, "tell", tell_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8IoGetCommands --
 *
 *	Return the command table for the I/O plugin.
 *
 * Why / How:
 *	Called by the plugin registration system to discover which
 *	commands this plugin provides.  On the first call pCommand
 *	is NULL and *pnCommand is set to the count; on the second
 *	call the entries are copied into the caller-provided array.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if pnCommand is NULL or the
 *	caller's buffer is too small.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8IoGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8IoCommands) / sizeof(th8IoCommands[0]));

    if (!pnCommand) return TH8_ERROR;
    if (!pCommand) {
	*pnCommand = n;
	return TH8_OK;
    }
    if (*pnCommand < n) return TH8_ERROR;
    *pnCommand = n;
    {
	int i;

	for (i = 0; i < n; i++) {
	    pCommand[i] = th8IoCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_IO */
