/*
 * th8_strings.c -- Strings plugin for TH8.
 *
 * Implements the string manipulation commands: base64, concat, string.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8StringsGetCommands.
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

#if defined(TH8_PLUGIN_STRINGS)

/*
 * th8_string_aSub: non-static so th8_lang.c can access it
 * for info subcommands.
 */

const Th8_SubCommand *th8_string_aSub;

/*
 *----------------------------------------------------------------------
 *
 * string_length_command --
 *
 *	Return the character (code point) length of a string.
 *
 *	string length STRING
 *
 * Why / How:
 *	Implements [string length].  Delegates to Th8_Utf8Len to
 *	count Unicode code points, not bytes, so multi-byte UTF-8
 *	sequences are counted as single characters.
 *
 * Results:
 *	TH8_OK.  Result is the character count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_length_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "string length string");
    }
    Th8_SetResultInt(interp, Th8_Utf8Len(argv[2], TH8_LEN(argl[2])));
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_compare_command --
 *
 *	Compare two strings lexicographically.
 *
 *	string compare ?-nocase? ?-length N? STR1 STR2
 *
 *	Options:
 *	    -nocase    Case-insensitive comparison (ASCII).
 *	    -length N  Compare at most the first N characters.
 *
 * Why / How:
 *	Implements [string compare].  Parses -nocase and -length
 *	options, clamps comparison lengths, then performs byte-level
 *	comparison (with optional ASCII case folding).  Returns a
 *	normalized -1/0/1 result like Tcl.
 *
 * Results:
 *	TH8_OK.  Result is -1, 0, or 1.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_compare_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    size_t n1, n2, nMin;
    int iCmp;
    int bNoCase = 0;  /* True if -nocase was given. */
    int haveLen = 0;  /* True if -length was given. */
    int maxLen = -1;  /* Max chars to compare (-1 = all). */
    int iArg = 2;  /* Index of first string argument. */

    (void)ctx;

    /*
     * Parse options.
     */

    while (iArg < argc - 2) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-nocase")) {
	    bNoCase = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-length")) {
	    iArg++;
	    if (iArg >= argc - 2) {
		return Th8_WrongNumArgs(
		    interp, "string compare ?-nocase?"
		            " ?-length int? str1 str2");
	    }
	    if (Th8_ToInt(interp, argv[iArg], argl[iArg], &maxLen) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	    haveLen = 1;
	    iArg++;
	} else {
	    break;
	}
    }

    if (argc - iArg != 2) {
	return Th8_WrongNumArgs(
	    interp, "string compare ?-nocase?"
	            " ?-length int? str1 str2");
    }

    n1 = TH8_LEN(argl[iArg]);
    n2 = TH8_LEN(argl[iArg + 1]);

    /*
     * If -length was given with a non-negative value, clamp
     * the comparison lengths.
     */

    if (haveLen && maxLen >= 0) {
	if (n1 > (size_t)maxLen) n1 = (size_t)maxLen;
	if (n2 > (size_t)maxLen) n2 = (size_t)maxLen;
    }

    nMin = n1 < n2 ? n1 : n2;

    if (bNoCase) {
	/*
	 * Case-insensitive: compare byte-by-byte, folding
	 * ASCII uppercase to lowercase.
	 */

	size_t j;

	iCmp = 0;
	for (j = 0; j < nMin; j++) {
	    unsigned char a = (unsigned char)argv[iArg][j];
	    unsigned char b = (unsigned char)argv[iArg + 1][j];

	    if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
	    if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
	    if (a != b) {
		iCmp = (a < b) ? -1 : 1;
		break;
	    }
	}
	if (iCmp == 0) {
	    iCmp = (n1 < n2) ? -1 : (n1 > n2 ? 1 : 0);
	}
    } else {
	iCmp = Th8_Memcmp(interp, argv[iArg], argv[iArg + 1], nMin);
	if (iCmp == 0) {
	    iCmp = (n1 < n2) ? -1 : (n1 > n2 ? 1 : 0);
	}
    }
    Th8_SetResultInt(interp, iCmp < 0 ? -1 : (iCmp > 0 ? 1 : 0));
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_equal_command --
 *
 *	Test two strings for equality.
 *
 *	string equal ?-nocase? ?-length N? STR1 STR2
 *
 *	Returns 1 if the strings are equal, 0 otherwise.
 *	Same options as [string compare].
 *
 * Why / How:
 *	Implements [string equal].  Structurally similar to
 *	string_compare_command but optimized for equality testing:
 *	short-circuits on length mismatch before comparing bytes.
 *	Returns boolean 1/0 rather than -1/0/1.
 *
 * Results:
 *	TH8_OK.  Result is 1 if equal, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_equal_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t n1, n2, nMin;
    int bNoCase = 0;
    int haveLen = 0;
    int maxLen = -1;
    int iArg = 2;

    (void)ctx;

    while (iArg < argc - 2) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-nocase")) {
	    bNoCase = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-length")) {
	    iArg++;
	    if (iArg >= argc - 2) {
		return Th8_WrongNumArgs(
		    interp, "string equal ?-nocase?"
		            " ?-length int? str1 str2");
	    }
	    if (Th8_ToInt(interp, argv[iArg], argl[iArg], &maxLen) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	    haveLen = 1;
	    iArg++;
	} else {
	    break;
	}
    }

    if (argc - iArg != 2) {
	return Th8_WrongNumArgs(
	    interp, "string equal ?-nocase?"
	            " ?-length int? str1 str2");
    }

    n1 = TH8_LEN(argl[iArg]);
    n2 = TH8_LEN(argl[iArg + 1]);

    if (haveLen && maxLen >= 0) {
	if (n1 > (size_t)maxLen) n1 = (size_t)maxLen;
	if (n2 > (size_t)maxLen) n2 = (size_t)maxLen;
    }

    if (n1 != n2) {
	Th8_SetResultInt(interp, 0);
	return TH8_OK;
    }

    nMin = n1;
    if (bNoCase) {
	size_t j;

	for (j = 0; j < nMin; j++) {
	    unsigned char a = (unsigned char)argv[iArg][j];
	    unsigned char b = (unsigned char)argv[iArg + 1][j];

	    if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
	    if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
	    if (a != b) {
		Th8_SetResultInt(interp, 0);
		return TH8_OK;
	    }
	}
    } else {
	if (Th8_Memcmp(interp, argv[iArg], argv[iArg + 1], nMin) != 0) {
	    Th8_SetResultInt(interp, 0);
	    return TH8_OK;
	}
    }
    Th8_SetResultInt(interp, 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_index_command --
 *
 *	Return the character at a given index.
 *
 *	string index STRING INDEX
 *
 * Why / How:
 *	Implements [string index].  Supports "end" and "end-N"
 *	index syntax.  Uses Th8_Utf8Index and Th8_Utf8Decode
 *	for UTF-8-aware character extraction.  Returns empty
 *	string for out-of-range indices.
 *
 * Results:
 *	TH8_OK.  Result is the character at INDEX, or empty string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_index_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nStr;
    int iIndex;
    int nChars;
    const char *zPos;
    int nByte;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "string index string index");
    }
    nStr = TH8_LEN(argl[2]);
    nChars = Th8_Utf8Len(argv[2], nStr);

    if (th8StrEq(interp, argv[3], argl[3], "end")) {
	iIndex = nChars - 1;
    } else if (
        TH8_LEN(argl[3]) > 4 && 0 == Th8_Memcmp(interp, argv[3], "end-", 4)) {
	int offset;
	int rc =
	    Th8_ToInt(interp, &argv[3][4], TH8_LEN(argl[3]) - 4, &offset);

	if (rc != TH8_OK) return rc;
	iIndex = nChars - 1 - offset;
    } else {
	int rc = Th8_ToInt(interp, argv[3], argl[3], &iIndex);

	if (rc != TH8_OK) return rc;
    }

    if (iIndex < 0 || iIndex >= nChars) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    zPos = Th8_Utf8Index(argv[2], nStr, iIndex);
    if (!zPos) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    Th8_Utf8Decode(zPos, nStr - (size_t)(zPos - argv[2]), &nByte);
    Th8_SetResult(interp, zPos, (size_t)nByte);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_range_command --
 *
 *	Return a substring by character index range.
 *
 *	string range STRING FIRST LAST
 *
 * Why / How:
 *	Implements [string range].  Uses th8ParseIndex for "end"
 *	and "end-N" syntax, then Th8_Utf8Index to locate the
 *	byte positions of the character boundaries.  Returns a
 *	byte slice from FIRST to LAST inclusive.
 *
 * Results:
 *	TH8_OK.  Result is the substring, or empty string if
 *	FIRST > LAST.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 * th8ParseIndex is now in th8_util.c (th8_util.h).
 *
 * It parses an index argument that can be:
 *   - "end"     -> returns nCount - 1
 *   - "end-N"   -> returns nCount - 1 - N
 *   - integer   -> returns the integer value
 *
 * Used by string range, lrange, lreplace, and string case commands
 * for consistent index handling.
 */


static int
string_range_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nStr;
    int nChars;
    int iFirst, iLast;
    const char *zStart;
    const char *zEnd;

    if (argc != 5) {
	return Th8_WrongNumArgs(interp, "string range string first last");
    }
    nStr = TH8_LEN(argl[2]);
    nChars = Th8_Utf8Len(argv[2], nStr);

    if (th8ParseIndex(interp, argv[3], argl[3], nChars, &iFirst) != TH8_OK) {
	return TH8_ERROR;
    }
    if (th8ParseIndex(interp, argv[4], argl[4], nChars, &iLast) != TH8_OK) {
	return TH8_ERROR;
    }
    if (iFirst < 0) iFirst = 0;
    if (iLast >= nChars) iLast = nChars - 1;
    if (iFirst > iLast) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    zStart = Th8_Utf8Index(argv[2], nStr, iFirst);
    zEnd = Th8_Utf8Index(argv[2], nStr, iLast + 1);
    if (!zStart) zStart = argv[2] + nStr;
    if (!zEnd) zEnd = argv[2] + nStr;
    /* The substring retains bytes of the input, so it inherits the
     * input's taint. */
    Th8_SetResult(
        interp, zStart, (size_t)(zEnd - zStart) | (argl[2] & TH8_TAG_BITS));
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_first_command / string_last_command --
 *
 *	Find first/last occurrence of needle in haystack.
 *
 * Why / How:
 *	Implements [string first].  Performs byte-level search from
 *	an optional start index.  Uses Th8_Utf8Index to convert the
 *	character start index to a byte offset, then scans forward
 *	using Th8_Memcmp.  Calls Th8_Ready per position for
 *	cancellation support.  Converts the byte-level hit position
 *	back to a character index via Th8_Utf8Len.
 *
 * Results:
 *	TH8_OK.  Result is the character index, or -1 if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_first_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nNeedle, nHay;
    int iStart = 0;
    size_t i;
    int charIdx;

    if (argc != 4 && argc != 5) {
	return Th8_WrongNumArgs(
	    interp, "string first needle haystack ?startIndex?");
    }
    nNeedle = TH8_LEN(argl[2]);
    nHay = TH8_LEN(argl[3]);
    if (argc == 5) {
	int rc = Th8_ToInt(interp, argv[4], argl[4], &iStart);

	if (rc != TH8_OK) return rc;
    }

    /*
     * Byte-level search; translate hit to character index.
     */

    {
	const char *zStart;
	size_t nOff;

	if (iStart > 0) {
	    zStart = Th8_Utf8Index(argv[3], nHay, iStart);
	    if (!zStart) {
		Th8_SetResultInt(interp, -1);
		return TH8_OK;
	    }
	    nOff = (size_t)(zStart - argv[3]);
	} else {
	    nOff = 0;
	}
	for (i = nOff; i + nNeedle <= nHay; i++) {
	    if (Th8_Ready(interp) != TH8_OK) return TH8_ERROR;
	    if (0 == Th8_Memcmp(interp, &argv[3][i], argv[2], nNeedle)) {
		charIdx = Th8_Utf8Len(argv[3], i);
		Th8_SetResultInt(interp, charIdx);
		return TH8_OK;
	    }
	}
    }
    Th8_SetResultInt(interp, -1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_last_command --
 *
 *	Find the last occurrence of a needle in a haystack.
 *
 *	string last NEEDLE HAYSTACK ?STARTINDEX?
 *
 * Why / How:
 *	Implements [string last].  Scans forward through the
 *	haystack, recording the last matching position.  Limits
 *	the search range if STARTINDEX is given.  Calls Th8_Ready
 *	per position for cancellation support.
 *
 * Results:
 *	TH8_OK.  Result is the character index, or -1 if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_last_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nNeedle, nHay;
    size_t i;
    int lastHit = -1;

    if (argc != 4 && argc != 5) {
	return Th8_WrongNumArgs(
	    interp, "string last needle haystack ?startIndex?");
    }
    nNeedle = TH8_LEN(argl[2]);
    nHay = TH8_LEN(argl[3]);

    /*
     * If startIndex is given, limit search to positions
     * at or before that character index.
     */

    {
	size_t nLimit = nHay;

	if (argc == 5) {
	    int iStart;
	    int rc = Th8_ToInt(interp, argv[4], argl[4], &iStart);

	    if (rc != TH8_OK) return rc;
	    if (iStart >= 0) {
		const char *zAt = Th8_Utf8Index(argv[3], nHay, iStart);

		if (zAt) {
		    nLimit = (size_t)(zAt - argv[3]) + 1;
		    if (nLimit > nHay) nLimit = nHay;
		}
	    }
	}
	for (i = 0; i + nNeedle <= nLimit; i++) {
	    if (Th8_Ready(interp) != TH8_OK) return TH8_ERROR;
	    if (0 == Th8_Memcmp(interp, &argv[3][i], argv[2], nNeedle)) {
		lastHit = Th8_Utf8Len(argv[3], i);
	    }
	}
    }
    Th8_SetResultInt(interp, lastHit);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_repeat_command --
 *
 *	string repeat STRING COUNT
 *
 * Why / How:
 *	Implements [string repeat].  Appends STRING to the output
 *	COUNT times.  Includes a security check against both
 *	TH8_MX_STRLEN and the per-interpreter result size limit to
 *	prevent string-bomb denial-of-service attacks.
 *
 * Results:
 *	TH8_OK.  Result is the repeated string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_repeat_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iCount;
    char *zOut;
    size_t nLen;
    size_t nOut;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "string repeat string count");
    }
    if (Th8_ToInt(interp, argv[3], argl[3], &iCount) != TH8_OK) {
	return TH8_ERROR;
    }
    if (iCount < 0) iCount = 0;
    nLen = TH8_LEN(argl[2]);

    /*
     * Security: bound the output size against both the hard
     * maximum AND the per-interpreter result size limit.
     * The check is overflow-safe: dividing the limit by nLen
     * cannot overflow, and rejects any iCount that would push
     * iCount * nLen past the limit.
     */

    {
	size_t nLimit = Th8_GetResultLimit(interp);

	if (nLimit == 0) nLimit = TH8_MX_STRLEN;
	if (nLen > 0 && (size_t)iCount > nLimit / nLen) {
	    Th8_SetResultStatic(interp, "string too long", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /*
     * Compute the exact output size with an overflow-checking
     * macro.  The earlier limit check (iCount > nLimit / nLen)
     * already establishes that iCount * nLen <= nLimit, and
     * nLimit <= TH8_MX_STRLEN < SIZE_MAX, so this multiply cannot
     * actually overflow at this point -- but bare multiplication
     * of attacker-influenced sizes is a banned pattern, so we use
     * TH8_SAFE_MUL_SIZE as the documented overflow-safe primitive.
     *
     * Allocation then happens in one shot via TH8_ALLOC_MUL_ADD,
     * which performs its own overflow check on iCount*nLen+1 as
     * defence-in-depth before calling the underlying allocator.
     *
     * Single-allocation matters under valgrind in particular: the
     * previous append-with-growth pattern produced log(iCount)
     * realloc calls, each one forcing memcheck to invalidate and
     * re-shadow the entire buffer.  One alloc means one shadow-mark
     * pass.
     */

    if (TH8_SAFE_MUL_SIZE((size_t)iCount, nLen, &nOut)) {
	Th8_SetResultStatic(interp, "string too long", TH8_NOLEN);
	return TH8_ERROR;
    }
    zOut = (char *)TH8_ALLOC_MUL_ADD(interp, (size_t)iCount, nLen, 1);
    if (zOut == 0) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Fill via exponential doubling.  Copy the pattern once, then
     * each subsequent memcpy doubles the filled prefix until the
     * buffer is full (final memcpy may copy less than its source
     * length when only a tail remains).  This collapses iCount
     * memcpy calls down to ceil(log2(iCount)) + 1 calls without
     * changing the total bytes moved, letting platform memcpy do
     * its widest vector loads on each call.
     */

    if (nLen > 0 && iCount > 0) {
	size_t nFilled;
	size_t nCopy;

	Th8_Memcpy(interp, zOut, argv[2], nLen);
	nFilled = nLen;
	while (nFilled < nOut) {
	    nCopy = nFilled;
	    if (nCopy > nOut - nFilled) {
		nCopy = nOut - nFilled;
	    }
	    Th8_Memcpy(interp, zOut + nFilled, zOut, nCopy);
	    nFilled += nCopy;
	}
    }
    zOut[nOut] = 0;

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_trim_command --
 *
 *	string trim / trimleft / trimright STRING ?CHARS?
 *
 *	Which direction is determined by the sub-command name
 *	passed via ctx pointer.
 *
 * Why / How:
 *	Implements [string trim], [string trimleft], and
 *	[string trimright].  A single function handles all three
 *	variants by checking the sub-command name in argv[1] to
 *	determine the trim direction.  Default trim characters are
 *	whitespace (space, tab, newline, carriage return, form feed,
 *	vertical tab).
 *
 * Results:
 *	TH8_OK.  Result is the trimmed string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_trim_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zStr;
    size_t nStr;
    const char *zChars = " \t\n\r\f\013";
    size_t nChars = 6;
    size_t iLeft = 0;
    size_t iRight;
    int trimLeft = 1;
    int trimRight = 1;

    if (argc != 3 && argc != 4) {
	return Th8_WrongNumArgs(interp, "string trim string ?chars?");
    }
    zStr = argv[2];
    nStr = TH8_LEN(argl[2]);
    if (argc == 4) {
	zChars = argv[3];
	nChars = TH8_LEN(argl[3]);
    }

    /*
     * Determine direction from sub-command name.
     */

    if (th8StrEq(interp, argv[1], argl[1], "trimleft")) {
	trimRight = 0;
    } else if (th8StrEq(interp, argv[1], argl[1], "trimright")) {
	trimLeft = 0;
    }

    iRight = nStr;

    if (trimLeft) {
	while (iLeft < nStr) {
	    size_t j;
	    int found = 0;

	    for (j = 0; j < nChars; j++) {
		if (zStr[iLeft] == zChars[j]) {
		    found = 1;
		    break;
		}
	    }
	    if (!found) break;
	    iLeft++;
	}
    }
    if (trimRight) {
	while (iRight > iLeft) {
	    size_t j;
	    int found = 0;

	    for (j = 0; j < nChars; j++) {
		if (zStr[iRight - 1] == zChars[j]) {
		    found = 1;
		    break;
		}
	    }
	    if (!found) break;
	    iRight--;
	}
    }
    Th8_SetResult(interp, &zStr[iLeft], iRight - iLeft);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_is_command --
 *
 *	string is CLASS ?-strict? STRING
 *
 * Why / How:
 *	Implements [string is].  Tests whether STRING belongs to
 *	CLASS (integer, double, alnum, alpha, digit, space, list,
 *	boolean, true, false, ascii, xdigit, wideinteger, tainted).
 *	Per Tcl semantics, empty strings are valid without -strict.
 *	Uses class-specific validation: numeric classes delegate to
 *	Th8_ToInt/Th8_ToDouble, character classes iterate bytes,
 *	list class uses Th8_SplitList, and tainted class checks the
 *	TH8_TAINTED bit.
 *
 * Results:
 *	TH8_OK.  Result is 1 if STRING matches CLASS, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_is_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nStr;
    int iResult = 1;
    int bStrict = 0;
    int iClass;  /* argv index of the class name */
    int iString; /* argv index of the string value */

    /*
     * Syntax: string is class ?-strict? string
     *
     * Accept both "string is CLASS STRING" (argc==4) and
     * "string is CLASS -strict STRING" (argc==5).
     */

    if (argc == 5 && argl[3] >= 2 && argv[3][0] == '-' &&
        th8StrEq(interp, argv[3], argl[3], "-strict")) {
	bStrict = 1;
	iClass = 2;
	iString = 4;
    } else if (argc == 4) {
	iClass = 2;
	iString = 3;
    } else {
	return Th8_WrongNumArgs(interp, "string is class ?-strict? string");
    }
    nStr = TH8_LEN(argl[iString]);

    /*
     * Per Tcl standard, without -strict an empty string
     * is considered valid for any class.  With -strict,
     * an empty string is NOT valid.
     */

    if (nStr == 0) {
	return Th8_SetResultInt(interp, bStrict ? 0 : 1);
    }

    if (th8StrEq(interp, argv[iClass], argl[iClass], "integer")) {
	int dummy;

	iResult = (Th8_ToInt(0, argv[iString], nStr, &dummy) == TH8_OK);
    } else if (
        th8StrEq(interp, argv[iClass], argl[iClass], "wideinteger") ||
        th8StrEq(interp, argv[iClass], argl[iClass], "wide")) {
	th8_int64_t dummy;

	iResult = (Th8_ToWideInt(0, argv[iString], nStr, &dummy) == TH8_OK);
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "double")) {
	double dummy;

	iResult = (Th8_ToDouble(0, argv[iString], nStr, &dummy) == TH8_OK);
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "alnum")) {
	size_t i;

	for (i = 0; i < nStr; i++) {
	    if (!th8IsAlnum((unsigned char)argv[iString][i])) {
		iResult = 0;
		break;
	    }
	}
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "alpha")) {
	size_t i;

	for (i = 0; i < nStr; i++) {
	    if (!th8IsAlpha((unsigned char)argv[iString][i])) {
		iResult = 0;
		break;
	    }
	}
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "digit")) {
	size_t i;

	for (i = 0; i < nStr; i++) {
	    if (!th8IsDigit((unsigned char)argv[iString][i])) {
		iResult = 0;
		break;
	    }
	}
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "space")) {
	size_t i;

	for (i = 0; i < nStr; i++) {
	    if (!th8IsSpace((unsigned char)argv[iString][i])) {
		iResult = 0;
		break;
	    }
	}
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "list")) {
	int nCount;

	iResult =
	    (Th8_SplitList(
	         interp, argv[iString], nStr, 0, 0, &nCount, TH8_LIST_NONE) ==
	     TH8_OK);
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "ascii")) {
	size_t i;

	for (i = 0; i < nStr; i++) {
	    if ((unsigned char)argv[iString][i] > 127) {
		iResult = 0;
		break;
	    }
	}
    } else if (
        th8StrEq(interp, argv[iClass], argl[iClass], "boolean") ||
        th8StrEq(interp, argv[iClass], argl[iClass], "true") ||
        th8StrEq(interp, argv[iClass], argl[iClass], "false")) {
	int dummy;

	iResult = (Th8_ToBoolean(0, argv[iString], nStr, &dummy) == TH8_OK);
	/*
	 * "string is true" and "string is false" refine the
	 * boolean check: the value must be a valid boolean AND
	 * have the specified truthiness.
	 */
	if (iResult && th8StrEq(interp, argv[iClass], argl[iClass], "true")) {
	    iResult = (dummy != 0);
	} else if (
	    iResult &&
	    th8StrEq(interp, argv[iClass], argl[iClass], "false")) {
	    iResult = (dummy == 0);
	}
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "xdigit")) {
	size_t i;

	for (i = 0; i < nStr; i++) {
	    char c = argv[iString][i];

	    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
	          (c >= 'A' && c <= 'F'))) {
		iResult = 0;
		break;
	    }
	}
    } else if (th8StrEq(interp, argv[iClass], argl[iClass], "tainted")) {
	iResult = TH8_TAINTED(argl[iString]);
    } else {
	Th8_ErrorMessage(interp, "bad class \"", argv[iClass], argl[iClass]);
	return TH8_ERROR;
    }
    Th8_SetResultInt(interp, iResult);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_match_command --
 *
 *	Glob-style pattern match on a string.
 *
 *	string match ?-nocase? PATTERN STRING
 *
 * Why / How:
 *	Implements [string match].  Delegates to Th8_GlobMatch for
 *	the actual pattern matching.  When -nocase is specified,
 *	creates lowercase copies of both pattern and string, then
 *	matches on the lowercase versions.  Memory for the copies
 *	is freed after matching.
 *
 * Results:
 *	TH8_OK.  Result is 1 if the pattern matches, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_match_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iArg = 2;
    int bNoCase = 0;

    (void)ctx;

    if (argc > 3 && th8StrEq(interp, argv[2], argl[2], "-nocase")) {
	bNoCase = 1;
	iArg = 3;
    }
    if (argc - iArg != 2) {
	return Th8_WrongNumArgs(
	    interp, "string match ?-nocase? pattern string");
    }

    if (bNoCase) {
	/*
	 * Case-insensitive: fold both to lowercase, then match.
	 */

	size_t nPat = TH8_LEN(argl[iArg]);
	size_t nStr = TH8_LEN(argl[iArg + 1]);
	char *zLPat = (char *)TH8_ALLOC_STR(interp, nPat);
	char *zLStr = (char *)TH8_ALLOC_STR(interp, nStr);
	size_t j;
	int result;

	/* Split per Finding 005. */
	if (!zLPat) {
	    Th8_Free(interp, zLStr);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (!zLStr) {
	    Th8_Free(interp, zLPat);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	for (j = 0; j < nPat; j++) {
	    unsigned char c = (unsigned char)argv[iArg][j];

	    zLPat[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
	}
	zLPat[nPat] = '\0';
	for (j = 0; j < nStr; j++) {
	    unsigned char c = (unsigned char)argv[iArg + 1][j];

	    zLStr[j] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
	}
	zLStr[nStr] = '\0';
	result = Th8_GlobMatch(interp, zLPat, nPat, zLStr, nStr);
	Th8_Free(interp, zLPat);
	Th8_Free(interp, zLStr);
	Th8_SetResultInt(interp, result);
    } else {
	Th8_SetResultInt(
	    interp, Th8_GlobMatch(
	                interp, argv[iArg], TH8_LEN(argl[iArg]),
	                argv[iArg + 1], TH8_LEN(argl[iArg + 1])));
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MemcmpNoCase --
 *
 *	Case-insensitive byte comparison (ASCII only).
 *	Returns 0 if the first n bytes of a and b are equal
 *	when ASCII uppercase is folded to lowercase.
 *
 * Why / How:
 *	Helper for string_map_command's -nocase mode.  Compares
 *	bytes one at a time, folding ASCII uppercase to lowercase
 *	before comparison.  Returns memcmp-style result (-1, 0, 1).
 *
 * Results:
 *	0 if equal, negative if a < b, positive if a > b.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8MemcmpNoCase(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	unsigned char ca = (unsigned char)a[i];
	unsigned char cb = (unsigned char)b[i];

	if (ca >= 'A' && ca <= 'Z') ca += ('a' - 'A');
	if (cb >= 'A' && cb <= 'Z') cb += ('a' - 'A');
	if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * string_map_command --
 *
 *	Apply a character mapping to a string.
 *
 *	string map ?-nocase? MAPPING STRING
 *
 *	MAPPING is a list of key/value pairs: {from1 to1 from2 to2 ...}
 *
 * Why / How:
 *	Implements [string map].  Splits the MAPPING into key/value
 *	pairs, then performs a single-pass scan over STRING.  At each
 *	position, tries each key in order (first match wins).
 *	Includes security checks: Th8_Ready is called per position
 *	for cancellation, and output size is checked against the
 *	result size limit to prevent string-bomb attacks.
 *
 * Results:
 *	TH8_OK on success, or TH8_ERROR on mapping imbalance,
 *	cancellation, or output size overflow.
 *
 * Side effects:
 *	None.
 *
 *	Algorithm: per-character replacement loop.  For each position
 *	i in STRING:
 *	  1. Try each key in MAPPING (first match wins).
 *	  2. If a key of length nKey matches at position i, append
 *	     the corresponding replacement to the output and advance
 *	     i by nKey.
 *	  3. If no key matches, copy one byte from STRING to output
 *	     and advance i by 1.
 *
 *	Th8_Ready is called once per position for cancellation.
 *	Empty keys (nKey == 0) are silently skipped to avoid
 *	infinite loops.
 *
 *----------------------------------------------------------------------
 */

static int
string_map_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azMap = 0;
    size_t *anMap = 0;
    int nMap;
    char *zOut = 0;
    size_t nOut = 0;
    size_t nStr;
    size_t i;
    int rc;
    int bNoCase = 0;
    int iArg = 2;  /* Index of MAPPING argument. */

    (void)ctx;

    if (argc < 4 || argc > 5) {
	return Th8_WrongNumArgs(
	    interp, "string map ?-nocase? mapping string");
    }
    if (argc == 5) {
	if (th8StrEq(interp, argv[2], argl[2], "-nocase")) {
	    bNoCase = 1;
	    iArg = 3;
	} else {
	    return Th8_WrongNumArgs(
	        interp, "string map ?-nocase? mapping string");
	}
    }
    rc = Th8_SplitList(
        interp, argv[iArg], argl[iArg], &azMap, &anMap, &nMap, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;
    if (nMap % 2 != 0) {
	Th8_Free(interp, azMap);
	Th8_SetResultStatic(interp, "char map list unbalanced", TH8_NOLEN);
	return TH8_ERROR;
    }

    nStr = TH8_LEN(argl[iArg + 1]);
    i = 0;
    while (i < nStr) {
	int j;
	int found = 0;

	if (Th8_Ready(interp) != TH8_OK) {
	    Th8_Free(interp, zOut);
	    Th8_Free(interp, azMap);
	    return TH8_ERROR;
	}
	for (j = 0; j < nMap && ALWAYS(azMap); j += 2) {
	    size_t nKey = TH8_LEN(anMap[j]);

	    if (nKey > 0 && i + nKey <= nStr) {
		int match;

		if (bNoCase) {
		    match =
		        (0 ==
		         th8MemcmpNoCase(&argv[iArg + 1][i], azMap[j], nKey));
		} else {
		    match =
		        (0 ==
		         Th8_Memcmp(
		             interp, &argv[iArg + 1][i], azMap[j], nKey));
		}
		if (match) {
		    TH8_STR_APPEND(
		        interp, &zOut, &nOut, azMap[j + 1], anMap[j + 1]);
		    i += nKey;
		    found = 1;
		    break;
		}
	    }
	}
	if (!found) {
	    TH8_STR_APPEND(interp, &zOut, &nOut, &argv[iArg + 1][i], 1);
	    i++;
	}

	/*
	 * Security: check output size against the per-interpreter
	 * result size limit to prevent string-bomb attacks where
	 * a short key maps to a very long replacement value.
	 */
	{
	    size_t nLimit = Th8_GetResultLimit(interp);

	    if (nLimit == 0) nLimit = TH8_MX_STRLEN;
	    if (nOut > nLimit) {
		Th8_Free(interp, zOut);
		Th8_Free(interp, azMap);
		Th8_SetResultStatic(interp, "string too long", TH8_NOLEN);
		return TH8_ERROR;
	    }
	}
    }
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azMap);
    return TH8_OK;

oom:
    Th8_Free(interp, zOut);
    Th8_Free(interp, azMap);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * string_replace_command --
 *
 *	string replace STRING FIRST LAST ?NEWSTRING?
 *
 *	Replace the range of characters from FIRST to LAST
 *	(inclusive) with NEWSTRING.  If NEWSTRING is omitted,
 *	the characters are simply deleted.
 *
 * Why / How:
 *	Implements [string replace].  Uses th8ParseIndex for index
 *	parsing and Th8_Utf8Advance to locate byte boundaries for
 *	the character range.  Builds the result as prefix +
 *	replacement + suffix.
 *
 * Results:
 *	TH8_OK.  Result is the modified string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_replace_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iFirst, iLast, nChars;
    const char *zStr;
    size_t nStr;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc < 5 || argc > 6) {
	return Th8_WrongNumArgs(
	    interp, "string replace string first last ?newString?");
    }

    zStr = argv[2];
    nStr = argl[2];
    nChars = Th8_Utf8Len(zStr, nStr);

    if (th8ParseIndex(interp, argv[3], argl[3], nChars, &iFirst) != TH8_OK) {
	return TH8_ERROR;
    }
    if (th8ParseIndex(interp, argv[4], argl[4], nChars, &iLast) != TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * Clamp to valid range.  If first > last or first >= nChars,
     * return the original string unchanged.
     */

    if (iFirst < 0) iFirst = 0;
    if (iLast >= nChars) iLast = nChars - 1;
    /* Bug 26 (2026-06-07): plain check rather than NEVER -- the
     * clamping above keeps iFirst < nChars when iFirst <= iLast,
     * but a clamping refactor would otherwise let iFirst index
     * past the string.  Split per Finding 005 sec. 5b: C2
     * (iFirst >= nChars with iFirst <= iLast) is intrinsic-dead
     * given the L1372 clamping. */
    if (iFirst > iLast) {
	Th8_SetResult(interp, zStr, nStr);
	return TH8_OK;
    }
    if (iFirst >= nChars) {
	Th8_SetResult(interp, zStr, nStr);
	return TH8_OK;
    }

    /*
     * Build: prefix + replacement + suffix.
     */

    {
	const char *pFirst = Th8_Utf8Advance(zStr, nStr, iFirst);
	const char *pAfterLast = Th8_Utf8Advance(zStr, nStr, iLast + 1);

	/* Prefix: bytes before iFirst. */
	if (pFirst > zStr) {
	    TH8_STR_APPEND(
	        interp, &zOut, &nOut, zStr, (size_t)(pFirst - zStr));
	}
	/* Replacement (if given). */
	if (argc == 6) {
	    TH8_STR_APPEND(interp, &zOut, &nOut, argv[5], argl[5]);
	}
	/* Suffix: bytes after iLast. */
	if (pAfterLast) {
	    size_t nSuffix = nStr - (size_t)(pAfterLast - zStr);

	    if (nSuffix > 0) {
		TH8_STR_APPEND(interp, &zOut, &nOut, pAfterLast, nSuffix);
	    }
	}
    }
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;

oom:
    Th8_Free(interp, zOut);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * string_totitle_command --
 *
 *	string totitle STRING ?FIRST? ?LAST?
 *
 *	Convert the first character (or the character at FIRST) to
 *	title case (uppercase) and the remaining characters (or
 *	FIRST+1 through LAST) to lowercase.  Characters outside the
 *	range are left unchanged.
 *
 * Why / How:
 *	Implements [string totitle].  Copies the string, then
 *	iterates over the byte range, uppercasing the first
 *	character in the range and lowercasing the rest.  Currently
 *	ASCII-only; full Unicode support is deferred to the
 *	ConvertUTF_v2 integration phase.
 *
 * Results:
 *	TH8_OK.  Result is the modified string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_totitle_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zStr;
    size_t nStr;
    int nChars;
    int iFirst = 0;
    int iLast;
    char *zOut;
    size_t i;

    (void)ctx;

    if (argc < 3 || argc > 5) {
	return Th8_WrongNumArgs(
	    interp, "string totitle string ?first? ?last?");
    }

    zStr = argv[2];
    nStr = argl[2];
    nChars = Th8_Utf8Len(zStr, nStr);
    iLast = nChars - 1;

    if (argc >= 4) {
	if (th8ParseIndex(interp, argv[3], argl[3], nChars, &iFirst) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
    }
    if (argc >= 5) {
	if (th8ParseIndex(interp, argv[4], argl[4], nChars, &iLast) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
    }

    if (iFirst < 0) iFirst = 0;
    if (iLast >= nChars) iLast = nChars - 1;

    /*
     * Copy the string, converting the title range.
     */

    zOut = (char *)TH8_ALLOC_STR(interp, nStr);
    if (!zOut) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, zOut, zStr, nStr);

    for (i = 0; i < nStr; i++) {
	unsigned char c = (unsigned char)zOut[i];
	int iChar = Th8_Utf8Len(zStr, i);

	if (iChar == iFirst) {
	    /* Title case: uppercase the first char in range. */
	    if (c >= 'a' && c <= 'z') zOut[i] = (char)(c - ('a' - 'A'));
	} else if (iChar > iFirst && iChar <= iLast) {
	    /* Lowercase the rest of the range. */
	    if (c >= 'A' && c <= 'Z') zOut[i] = (char)(c + ('a' - 'A'));
	}
    }
    Th8_SetResult(interp, zOut, nStr);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_wordend_command / string_wordstart_command --
 *
 *	string wordend STRING INDEX
 *	string wordstart STRING INDEX
 *
 *	Return the index of the character just after (wordend) or
 *	just before (wordstart) the word containing the character
 *	at INDEX.  A "word" is a contiguous run of alphanumeric
 *	characters (ASCII).
 *
 * Why / How:
 *	Implements [string wordend] and [string wordstart].  Scans
 *	forward (wordend) or backward (wordstart) from the given
 *	index, stopping at the first non-word character (not
 *	alphanumeric and not underscore).  Uses th8ParseIndex for
 *	"end" and "end-N" syntax.
 *
 * Results:
 *	TH8_OK.  Result is the boundary character index.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_wordend_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int nChars, iIdx, i;

    (void)ctx;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "string wordend string index");
    }
    nChars = Th8_Utf8Len(argv[2], argl[2]);
    if (th8ParseIndex(interp, argv[3], argl[3], nChars, &iIdx) != TH8_OK) {
	return TH8_ERROR;
    }
    if (iIdx < 0) iIdx = 0;
    if (iIdx >= nChars) iIdx = nChars - 1;

    for (i = iIdx; i < nChars; i++) {
	const char *p = Th8_Utf8Index(argv[2], argl[2], i);

	if (ALWAYS(p) && !th8IsAlnum((unsigned char)*p) &&
	    ALWAYS(*p != '_')) {
	    break;
	}
    }
    return Th8_SetResultInt(interp, i);
}


/*
 * string_wordstart_command -- see string_wordend_command above.
 */

static int
string_wordstart_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int nChars, iIdx, i;

    (void)ctx;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "string wordstart string index");
    }
    nChars = Th8_Utf8Len(argv[2], argl[2]);
    if (th8ParseIndex(interp, argv[3], argl[3], nChars, &iIdx) != TH8_OK) {
	return TH8_ERROR;
    }
    if (iIdx < 0) iIdx = 0;
    if (iIdx >= nChars) iIdx = nChars - 1;

    for (i = iIdx; i > 0; i--) {
	const char *p = Th8_Utf8Index(argv[2], argl[2], i - 1);

	if (ALWAYS(p) && !th8IsAlnum((unsigned char)*p) &&
	    ALWAYS(*p != '_')) {
	    break;
	}
    }
    return Th8_SetResultInt(interp, i);
}


/*
 *----------------------------------------------------------------------
 *
 * string_bytelength_command --
 *
 *	string bytelength STRING
 *
 *	Return the number of bytes in the UTF-8 representation.
 *
 * Why / How:
 *	Implements [string bytelength].  Returns argl[2] directly,
 *	which is the byte length of the string as maintained by the
 *	interpreter's argument passing infrastructure.
 *
 * Results:
 *	TH8_OK.  Result is the byte count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_bytelength_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "string bytelength string");
    }
    return Th8_SetResultInt(interp, (int)argl[2]);
}


/*
 *----------------------------------------------------------------------
 *
 * string_reverse_command --
 *
 *	string reverse STRING
 *
 *	Return a string with all characters in reverse order.
 *	UTF-8 aware: reverses Unicode characters, not bytes.
 *
 * Why / How:
 *	Implements [string reverse].  Iterates from the last
 *	character index to the first, using Th8_Utf8Index to
 *	locate each character's byte position.  Appends each
 *	character's bytes in sequence to build the reversed string.
 *	Correctly handles multi-byte UTF-8 sequences.
 *
 * Results:
 *	TH8_OK.  Result is the reversed string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_reverse_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int nChars, i;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "string reverse string");
    }
    nChars = Th8_Utf8Len(argv[2], argl[2]);

    for (i = nChars - 1; i >= 0; i--) {
	const char *p = Th8_Utf8Index(argv[2], argl[2], i);
	const char *pNext = Th8_Utf8Index(argv[2], argl[2], i + 1);

	if (!pNext) pNext = argv[2] + argl[2];
	if (p) {
	    TH8_STR_APPEND(interp, &zOut, &nOut, p, (size_t)(pNext - p));
	}
    }
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;

oom:
    Th8_Free(interp, zOut);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * string_case_command --
 *
 *	Convert string case.  Direction determined by sub-command
 *	name ("tolower" or "toupper").
 *
 *	string tolower STRING ?FIRST? ?LAST?
 *	string toupper STRING ?FIRST? ?LAST?
 *
 *	ASCII fast-path.  Full Unicode case mapping will use
 *	ConvertUTF_v2 tables in a later phase.
 *
 * Why / How:
 *	Implements [string tolower] and [string toupper].  A single
 *	function handles both by checking argv[1] to determine the
 *	direction.  Copies the string, locates the byte range
 *	corresponding to the FIRST..LAST character range via
 *	Th8_Utf8Index, then transforms the bytes in-place.
 *
 * Results:
 *	TH8_OK.  Result is the case-converted string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
string_case_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    size_t nStr;
    int nChars;
    int iFirst = 0;
    int iLast;
    int bToLower;
    char *zOut;
    size_t i;
    const char *zFirst;
    const char *zLast;

    if (argc != 3 && argc != 4 && argc != 5) {
	return Th8_WrongNumArgs(
	    interp, "string tolower|toupper string ?first? ?last?");
    }
    nStr = TH8_LEN(argl[2]);
    nChars = Th8_Utf8Len(argv[2], nStr);
    iLast = nChars - 1;

    bToLower = th8StrEq(interp, argv[1], argl[1], "tolower");

    if (argc >= 4) {
	if (th8ParseIndex(interp, argv[3], argl[3], nChars, &iFirst) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
    }
    if (argc >= 5) {
	if (th8ParseIndex(interp, argv[4], argl[4], nChars, &iLast) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
    }
    if (iFirst < 0) iFirst = 0;
    if (iLast >= nChars) iLast = nChars - 1;

    /*
     * Convert: copy the string, transforming characters in
     * the specified range.  For ASCII, a simple byte-level
     * transformation is correct because case-mapping doesn't
     * change byte count.
     */

    zOut = (char *)TH8_ALLOC_STR(interp, nStr);
    if (!zOut) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, zOut, argv[2], nStr);
    zOut[nStr] = 0;

    zFirst = Th8_Utf8Index(argv[2], nStr, iFirst);
    zLast = Th8_Utf8Index(argv[2], nStr, iLast + 1);
    if (!zFirst) zFirst = argv[2];
    if (!zLast) zLast = argv[2] + nStr;

    for (i = (size_t)(zFirst - argv[2]); i < (size_t)(zLast - argv[2]); i++) {
	unsigned char c = (unsigned char)zOut[i];

	if (bToLower) {
	    if (c >= 'A' && c <= 'Z') {
		zOut[i] = (char)(c + ('a' - 'A'));
	    }
	} else {
	    if (c >= 'a' && c <= 'z') {
		zOut[i] = (char)(c - ('a' - 'A'));
	    }
	}
    }
    /* Case conversion retains the input's bytes, so the result
     * inherits the input's taint. */
    Th8_SetResult(interp, zOut, nStr | (argl[2] & TH8_TAG_BITS));
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * string_command --
 *
 *	Dispatcher for string sub-commands.  Uses the standard
 *	Th8_SubCommand table pattern with Th8_CallSubCommand.
 *
 *	Sub-commands: compare, first, index, is, last, length, map,
 *	match, range, repeat, tolower, toupper, trim, trimleft,
 *	trimright.
 *
 *	NOTE: tolower/toupper and trim/trimleft/trimright share
 *	implementation functions (string_case_command and
 *	string_trim_command respectively).  The sub-command name is
 *	passed via argv[1] so the shared function can differentiate
 *	at runtime.
 *
 * Why / How:
 *	Implements the Tcl [string] command ensemble.  Builds a
 *	static subcommand table and delegates to Th8_CallSubCommand,
 *	which handles subcommand lookup, abbreviation matching, and
 *	error reporting.  Also stores the table pointer in the
 *	th8_string_aSub global for [info subcommands] support.
 *
 * Results:
 *	Return code from the sub-command.
 *
 * Side effects:
 *	Determined by the sub-command.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand th8StringSub[] =
    {{0, "compare", string_compare_command},
     {0, "equal", string_equal_command},
     {0, "first", string_first_command},
     {0, "index", string_index_command},
     {0, "is", string_is_command},
     {0, "last", string_last_command},
     {0, "length", string_length_command},
     {0, "map", string_map_command},
     {0, "match", string_match_command},
     {0, "range", string_range_command},
     {0, "repeat", string_repeat_command},
     {0, "replace", string_replace_command},
     {0, "tolower", string_case_command},
     {0, "totitle", string_totitle_command},
     {0, "toupper", string_case_command},
     {0, "trim", string_trim_command},
     {0, "trimleft", string_trim_command},
     {0, "trimright", string_trim_command},
     {0, "wordend", string_wordend_command},
     {0, "wordstart", string_wordstart_command},
     {0, "bytelength", string_bytelength_command},
     {0, "reverse", string_reverse_command},
     {0, 0, 0}};

/*
 *----------------------------------------------------------------------
 *
 * string_command --
 *
 *	Implements the script-visible `[string ...]` ensemble
 *	(`bytelength`, `compare`, `equal`, `first`, `index`,
 *	`is`, `last`, `length`, `map`, `match`, `range`,
 *	`repeat`, `replace`, `reverse`, `tolower`, `totitle`,
 *	`toupper`, `trim`, `trimleft`, `trimright`,
 *	`wordstart`, `wordend`, ...).  Thin dispatcher into
 *	`th8StringSub` via `Th8_CallSubCommand`.
 *
 *	Diagnostics for unknown / ambiguous subcommands are
 *	emitted by `Th8_CallSubCommand`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- command context (forwarded).
 *	argc   -- argument count.
 *	argv   -- argument vector.
 *	argl   -- argument byte-length vector.
 *
 * Returns:
 *	The selected subcommand's return code, or `TH8_ERROR`
 *	with a diagnostic if the subcommand name is unknown.
 *
 * Side effects:
 *	Whatever the dispatched subcommand performs.
 *
 *----------------------------------------------------------------------
 */
static int
string_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    return Th8_CallSubCommand(interp, ctx, argc, argv, argl, th8StringSub);
}


/*
 *----------------------------------------------------------------------
 *
 * base64_command --
 *
 *	base64 encode STRING
 *	base64 decode STRING
 *
 *	Delegates to shared th8Base64Encode / th8Base64Decode
 *	(implemented in th8_base64.c).
 *
 * Why / How:
 *	Implements the [base64] command.  A thin wrapper that
 *	dispatches to th8Base64Encode or th8Base64Decode based on
 *	the subcommand name.  The actual encoding/decoding logic
 *	lives in th8_base64.c for reuse by other subsystems.
 *
 * Results:
 *	TH8_OK on success, or TH8_ERROR on invalid subcommand or
 *	decode failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
base64_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "base64 encode|decode string");
    }

    if (th8StrEq(interp, argv[1], argl[1], "encode")) {
	return th8Base64Encode(
	    interp, (const unsigned char *)argv[2], TH8_LEN(argl[2]));
    } else if (th8StrEq(interp, argv[1], argl[1], "decode")) {
	unsigned char *zDec = NULL;
	size_t nDec = 0;
	int rc;

	rc = th8Base64Decode(interp, argv[2], TH8_LEN(argl[2]), &zDec, &nDec);
	if (rc == TH8_OK) {
	    Th8_SetResult(interp, (const char *)zDec, nDec);
	    Th8_Free(interp, zDec);
	}
	return rc;
    } else {
	Th8_ErrorMessage(interp, "bad subcommand \"", argv[1], argl[1]);
	return TH8_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * concat_command --
 *
 *	Concatenate arguments with whitespace trimming.
 *
 *	concat ?arg ...?
 *
 *	Each argument is trimmed of leading and trailing whitespace,
 *	then the trimmed arguments are joined with single spaces.
 *	This is the standard Tcl [concat] command used for merging
 *	list fragments.
 *
 * Why / How:
 *	Implements the Tcl [concat] command.  Trims each argument,
 *	skips empty results, and joins non-empty fragments with
 *	single spaces.  This is the correct list-concatenation
 *	primitive that preserves list structure, unlike string
 *	concatenation.
 *
 * Results:
 *	TH8_OK.  Result is the concatenated string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
concat_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zResult = 0;
    size_t nResult = 0;
    int i;

    (void)ctx;

    for (i = 1; i < argc; i++) {
	const char *z = argv[i];
	size_t n = TH8_LEN(argl[i]);
	size_t start, end;

	/*
	 * Trim leading whitespace.
	 */
	for (start = 0; start < n && th8IsSpace((unsigned char)z[start]);
	     start++) {
	    /* empty */
	}

	/*
	 * Trim trailing whitespace.
	 */
	for (end = n; end > start && th8IsSpace((unsigned char)z[end - 1]);
	     end--) {
	    /* empty */
	}

	/*
	 * Skip entirely empty (after trimming) arguments.
	 */
	if (start >= end) continue;

	/*
	 * Append a space separator if the result is non-empty.
	 */
	if (nResult > 0) {
	    TH8_STR_APPEND(interp, &zResult, &nResult, " ", 1);
	}
	TH8_STR_APPEND(interp, &zResult, &nResult, &z[start], end - start);
    }

    Th8_SetResult(interp, zResult ? zResult : "", nResult);
    Th8_Free(interp, zResult);
    return TH8_OK;

oom:
    Th8_Free(interp, zResult);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8StringsCommands[] = {
    {1, 0, "base64", base64_command},
    {1, 0, "concat", concat_command},
    {1, 0, "string", string_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8StringsGetCommands --
 *
 *	Return the command table for the strings plugin.
 *
 * Why / How:
 *	Plugin registration entry point.  Called by Th8_RegisterPlugin
 *	to discover the commands provided by this plugin.  Uses the
 *	two-call pattern: first call with pCommand==NULL to query the
 *	count, second call to copy entries.
 *
 * Results:
 *	TH8_OK on success.  TH8_ERROR if pnCommand is NULL or if
 *	the provided buffer is too small.
 *
 * Side effects:
 *	Copies command entries to the caller's buffer.
 *
 *----------------------------------------------------------------------
 */

int
th8StringsGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8StringsCommands) / sizeof(th8StringsCommands[0]));

    th8_string_aSub = th8StringSub;

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
	    pCommand[i] = th8StringsCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_STRINGS */
