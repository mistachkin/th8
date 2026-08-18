/*
 * th8_glob.c -- Glob-style pattern matching for TH8.
 *
 * Provides Th8_GlobMatch, a public API function that performs
 * glob-style pattern matching with the following metacharacters:
 *
 *   *      - match any sequence of zero or more characters
 *   ?      - match any single character
 *   [...]  - character class (! for inversion, a-z for ranges)
 *   \c     - escape: match literal c
 *
 * Used by: [string match], [info commands], [info procs],
 * [info vars], [info functions], [lsearch], [switch -glob],
 * and available to extensions via the public C API.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"

/*
 *----------------------------------------------------------------------
 *
 * th8GlobMatch2 --
 *
 *	Recursive glob-style pattern matching with depth limit and
 *	interpreter cancellation check.
 *
 *	Security: the depth parameter is incremented on each '*'
 *	recursion and capped at 50.  This prevents exponential
 *	backtracking from pathological patterns like *a*a*a*a*
 *	against long non-matching strings.
 *
 *	Th8_Ready is called at entry and once per match-loop
 *	iteration to allow cancellation of long-running matches.
 *
 * Why / How:
 *	Walks the pattern and string in parallel.  Literal characters
 *	and '?' are matched one-to-one.  '[...]' character classes scan
 *	for range matches.  '\' escapes the next metacharacter.  '*'
 *	collapses consecutive stars, then tries every possible suffix
 *	match via recursion with an incremented depth counter.  The
 *	depth cap at 50 prevents exponential blowup on adversarial
 *	patterns.
 *
 * Results:
 *	1 if the pattern matches the string, 0 otherwise.  Returns 0
 *	when the depth cap is exceeded or the interpreter signals
 *	cancellation.
 *
 * Side effects:
 *	None.  The function only reads the pattern and string; the
 *	Th8_Ready calls poll the interpreter for cancellation but this
 *	function changes no interpreter or global state.
 *
 *----------------------------------------------------------------------
 */

static int
th8GlobMatch2(
    Th8_Interp *interp, /* Interpreter (for cancel check). */
    const char *zPat,
    size_t nPat,
    const char *zStr,
    size_t nStr,
    int depth)   /* Recursion depth counter. */
{
    size_t iPat = 0;
    size_t iStr = 0;

    /*
     * Security: bound recursion depth.
     */

    if (depth > 50) {
	return 0;
    }

    /*
     * Security: unified readiness check.
     */

    if (interp) {
	if (Th8_Ready(interp) != TH8_OK) return 0;
    }

    while (iPat < nPat) {
	char c = zPat[iPat];

	/*
	 * Step counter: one step per match iteration.
	 */

	if (interp && Th8_Ready(interp) != TH8_OK) return 0;

	if (c == '*') {
	    /*
	     * Match any sequence.  Try advancing in string.
	     */

	    iPat++;
	    /* Collapse consecutive '*' */
	    while (iPat < nPat && zPat[iPat] == '*') {
		iPat++;
	    }
	    if (iPat == nPat) return 1;  /* trailing * */
	    while (iStr <= nStr) {
		if (th8GlobMatch2(
		        interp, &zPat[iPat], nPat - iPat, &zStr[iStr],
		        nStr - iStr, depth + 1)) {
		    return 1;
		}
		if (iStr == nStr) break;
		iStr++;
	    }
	    return 0;
	} else if (c == '?') {
	    if (iStr >= nStr) return 0;
	    iPat++;
	    iStr++;
	} else if (c == '[') {
	    /*
	     * Character class.
	     */

	    int invert = 0;
	    int matched = 0;

	    iPat++;
	    if (iStr >= nStr) return 0;
	    if (iPat < nPat && zPat[iPat] == '!') {
		invert = 1;
		iPat++;
	    }
	    while (iPat < nPat && zPat[iPat] != ']') {
		if (iPat + 2 < nPat && zPat[iPat + 1] == '-' &&
		    zPat[iPat + 2] != ']') {
		    /*
		     * Character range: [a-z].
		     */

		    unsigned char cLo = (unsigned char)zPat[iPat];
		    unsigned char cHi = (unsigned char)zPat[iPat + 2];
		    unsigned char cCh = (unsigned char)zStr[iStr];

		    if (cCh >= cLo && cCh <= cHi) {
			matched = 1;
		    }
		    iPat += 3;
		} else {
		    if (zPat[iPat] == zStr[iStr]) {
			matched = 1;
		    }
		    iPat++;
		}
	    }
	    if (iPat < nPat) iPat++;  /* skip ']' */
	    if (invert) matched = !matched;
	    if (!matched) return 0;
	    iStr++;
	} else if (c == '\\') {
	    iPat++;
	    if (iPat >= nPat) return 0;
	    if (iStr >= nStr) return 0;
	    if (zPat[iPat] != zStr[iStr]) return 0;
	    iPat++;
	    iStr++;
	} else {
	    if (iStr >= nStr) return 0;
	    if (c != zStr[iStr]) return 0;
	    iPat++;
	    iStr++;
	}
    }
    return iStr == nStr;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GlobMatch --
 *
 *	Public glob-style pattern matching.  Matches zStr against
 *	zPat using *, ?, [...], and \ metacharacters.
 *
 * Why / How:
 *	Thin wrapper that calls th8GlobMatch2 with an initial
 *	recursion depth of zero.  Exists as the public entry point
 *	so that extensions and internal callers do not need to know
 *	about the depth parameter.
 *
 * Results:
 *	1 if the pattern matches the string, 0 otherwise.
 *
 * Side effects:
 *	May call Th8_Ready for cancellation checks (if interp is
 *	non-NULL).
 *
 *----------------------------------------------------------------------
 */

int
Th8_GlobMatch(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *zPat,
    size_t nPat,
    const char *zStr,
    size_t nStr)
{
    return th8GlobMatch2(interp, zPat, nPat, zStr, nStr, 0);
}
