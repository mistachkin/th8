/*
 * th8_lists.c -- Lists plugin for TH8.
 *
 * Implements the list manipulation commands: dict, join, lappend,
 * lassign, lindex, list, llength, lrange, lremove, lreplace,
 * lreverse, lsearch, lsort, split.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8ListsGetCommands.
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

#if defined(TH8_PLUGIN_LISTS)

/*
 *======================================================================
 *
 * File-local type definitions
 *
 *======================================================================
 */

/*
 * TH8_SORT_* --
 *	Sort modes for [lsort].  TH8_SORT_COMMAND is reserved for a
 *	future qsort_r-style implementation; the current code does
 *	inline comparison inside the insertion-sort loop instead.
 */
#  define TH8_SORT_ASCII      0
#  define TH8_SORT_INTEGER    1
#  define TH8_SORT_REAL       2
#  define TH8_SORT_COMMAND    3
#  define TH8_SORT_DICTIONARY 4

/*
 * Th8_SortCtx --
 *	Sort context: comparison mode, direction, optional comparison
 *	command, and -index sub-element selector.  Used by lsort_command.
 */
typedef struct Th8_SortCtx Th8_SortCtx;
struct Th8_SortCtx {
    Th8_Interp *interp;
    int eMode;
    int bDecreasing;
    const char *zCommand; /* -command: comparison script */
    size_t nCommand;
    int iIndex;   /* -index N: sub-element index (-1=none) */
};


/*
 *----------------------------------------------------------------------
 *
 * join_command --
 *
 *	join LIST ?SEPARATOR?
 *
 * Why / How:
 *	Implements the Tcl [join] command.  Splits the list into
 *	elements via Th8_SplitList, then concatenates them with
 *	the separator (default single space) between each pair.
 *
 * Results:
 *	TH8_OK.  Result is the joined string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
join_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    char *zOut = 0;
    size_t nOut = 0;
    const char *zSep = " ";
    size_t nSep = 1;
    int i;
    int rc;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "join list ?separator?");
    }
    if (argc == 3) {
	zSep = argv[2];
	nSep = TH8_LEN(argl[2]);
    }
    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azElem, &anElem, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    for (i = 0; i < nCount; i++) {
	if (i > 0) {
	    TH8_STR_APPEND(interp, &zOut, &nOut, zSep, nSep);
	}
	if (azElem) {
	    TH8_STR_APPEND(interp, &zOut, &nOut, azElem[i], anElem[i]);
	}
    }
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;

oom:
    /* A TH8_STR_APPEND growth failed; "out of memory" already set.
     * Free the partial output and the split-list vector. */
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * lappend_command --
 *
 *	Append elements to a list variable.
 *
 *	lappend VARNAME VALUE ?VALUE ...?
 *
 * Why / How:
 *	Implements the Tcl [lappend] command.  Reads the current
 *	value of VARNAME (creating as empty if absent), appends
 *	each VALUE as a properly-quoted list element, then writes
 *	back.  Rejects modification of system variables.
 *
 * Results:
 *	TH8_OK.  Result is the new list value.
 *
 * Side effects:
 *	Modifies the variable named VARNAME.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_VARIABLES)
static int
lappend_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int i;
    char *zList = 0;
    size_t nList = 0;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "lappend varname ?value ...?");
    }
    if (argc >= 3 && Th8_IsSystemVar(interp, argv[1], argl[1])) {
	Th8_ErrorMessage(
	    interp, "can't modify system variable \"", argv[1], argl[1]);
	return TH8_ERROR;
    }

    /*
     * Get the current list value.  Create as empty if
     * the variable does not exist.  Validate that the
     * existing value's brace/bracket/quote structure is
     * balanced before extending it (R-18890-36310); a
     * malformed value would otherwise produce a
     * still-malformed concatenation silently.
     *
     * The validation is a structural walk (no allocation,
     * no cache, no interp-state touch) rather than a
     * full Th8_SplitList call -- the latter has been
     * observed to introduce a side-effect that breaks
     * subsequent `package require` operations during
     * testlib load (Bug 38 first-fix regression).  This
     * cheaper check covers the cases the R-marker calls
     * out (unmatched open brace / bracket / quote)
     * without invoking the heavier IR-cache machinery.
     */
    if (Th8_GetVar(interp, argv[1], argl[1]) == TH8_OK) {
	size_t nCur;
	const char *zCur = Th8_GetResult(interp, &nCur);
	/* nCur may carry the taint bit; mask it for byte traversal so
	 * the scan bound is the real length (a tagged length is
	 * ~256 MiB and would over-read).  The tagged nCur is preserved
	 * and passed to Th8_StringAppend below, which propagates the
	 * taint into the appended list. */
	size_t nRaw = TH8_LEN(nCur);
	int braceDepth = 0;
	int bracketDepth = 0;
	int inQuote = 0;
	size_t k;

	/* nRaw is the byte-scan bound; a leaked taint tag would over-read
	 * ~256 MiB (deterministically caught here on a debug build). */
	TH8_ASSERT_RAW_LEN(nRaw);
	for (k = 0; k < nRaw; k++) {
	    char c = zCur[k];
	    if (c == '\\' && k + 1 < nRaw) {
		k++;
		continue;
	    }
	    if (inQuote) {
		if (c == '"') inQuote = 0;
		continue;
	    }
	    if (c == '{')
		braceDepth++;
	    else if (c == '}')
		braceDepth--;
	    else if (c == '[')
		bracketDepth++;
	    else if (c == ']')
		bracketDepth--;
	    else if (c == '"')
		inQuote = 1;
	    if (braceDepth < 0 || bracketDepth < 0) break;
	}
	if (braceDepth != 0 || bracketDepth != 0 || inQuote) {
	    const char *zErr;
	    size_t nErr;
	    if (braceDepth > 0) {
		zErr = "unmatched open brace in list";
		nErr = 28;
	    } else if (braceDepth < 0) {
		zErr = "unmatched close brace in list";
		nErr = 29;
	    } else if (bracketDepth > 0) {
		zErr = "unmatched open bracket in list";
		nErr = 30;
	    } else if (bracketDepth < 0) {
		zErr = "unmatched close bracket in list";
		nErr = 31;
	    } else {
		zErr = "unmatched quote in list";
		nErr = 23;
	    }
	    Th8_SetResult(interp, zErr, nErr);
	    return TH8_ERROR;
	}
	TH8_STR_APPEND(interp, &zList, &nList, zCur, nCur);
    }

    for (i = 2; i < argc; i++) {
	Th8_ListAppend(interp, &zList, &nList, argv[i], argl[i]);
    }
    Th8_SetVar(interp, argv[1], argl[1], zList, nList);
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;

oom:
    /* A TH8_STR_APPEND growth failed; "out of memory" already set. */
    Th8_Free(interp, zList);
    return TH8_ERROR;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * lindex_command --
 *
 *	Return an element from a list by index.
 *
 *	lindex LIST ?INDEX ...?
 *
 *	Tcl 8.4 semantics:
 *	  - lindex LIST         -> LIST (identity)
 *	  - lindex LIST INDEX   -> element at INDEX
 *	  - lindex LIST I1 I2   -> nested: lindex [lindex LIST I1] I2
 *	  - lindex LIST {I1 I2} -> same as lindex LIST I1 I2
 *
 *	Indices support "end" and "end-N".  Out-of-range indices
 *	return the empty string.
 *
 * Why / How:
 *	Implements the Tcl [lindex] command.  Handles both multiple
 *	separate index arguments and a single index-list argument.
 *	For nested indexing, iterates through each index, splitting
 *	the intermediate result as a list at each step.  Uses
 *	heap-allocated copies to maintain valid pointers across
 *	splits.
 *
 * Results:
 *	TH8_OK.  Result is the extracted element, or empty string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
lindex_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int iIndex;
    int rc;
    const char *zList;
    size_t nList;
    char *zCopy = 0;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "lindex list ?index ...?");
    }

    /*
     * No index: return the list unchanged.
     */

    if (argc == 2) {
	Th8_SetResult(interp, argv[1], argl[1]);
	return TH8_OK;
    }

    zList = argv[1];
    nList = argl[1];

    /*
     * If exactly one index argument, check if it's an index
     * list (contains spaces).  If so, split it into individual
     * indices and apply them sequentially.
     */

    if (argc == 3) {
	char **azIdx = 0;
	size_t *anIdx = 0;
	int nIdx = 0;

	rc = Th8_SplitList(
	    interp, argv[2], argl[2], &azIdx, &anIdx, &nIdx, TH8_LIST_NONE);
	if (rc != TH8_OK) return rc;

	if (nIdx > 1) {
	    /*
	     * Index list: apply each index in sequence.
	     */

	    int j;

	    for (j = 0; j < nIdx; j++) {
		rc = Th8_SplitList(
		    interp, zList, nList, &azElem, &anElem, &nCount,
		    TH8_LIST_NONE);
		if (rc != TH8_OK) {
		    Th8_Free(interp, azIdx);
		    Th8_Free(interp, zCopy);
		    return rc;
		}
		rc = th8ParseIndex(
		    interp, azIdx[j], anIdx[j], nCount, &iIndex);
		if (rc != TH8_OK) {
		    Th8_Free(interp, azElem);
		    Th8_Free(interp, azIdx);
		    Th8_Free(interp, zCopy);
		    return rc;
		}
		Th8_Free(interp, zCopy);
		zCopy = 0;
		if (iIndex >= 0 && iIndex < nCount && ALWAYS(azElem)) {
		    /* Keep the taint bit for the result; raw length for
		     * allocation/copy/index. */
		    nList = anElem[iIndex];
		    zCopy = (char *)TH8_ALLOC_STR(interp, TH8_LEN(nList));
		    if (!zCopy) {
			Th8_Free(interp, azElem);
			Th8_Free(interp, azIdx);
			Th8_SetResultStatic(
			    interp, "out of memory", TH8_NOLEN);
			return TH8_ERROR;
		    }
		    Th8_Memcpy(interp, zCopy, azElem[iIndex], TH8_LEN(nList));
		    zCopy[TH8_LEN(nList)] = 0;
		    zList = zCopy;
		} else {
		    zList = "";
		    nList = 0;
		}
		Th8_Free(interp, azElem);
		azElem = 0;
	    }
	    Th8_Free(interp, azIdx);
	    Th8_SetResult(interp, zList, nList);
	    Th8_Free(interp, zCopy);
	    return TH8_OK;
	}
	Th8_Free(interp, azIdx);

	/* Single index: fall through to normal path. */
    }

    /*
     * Multiple separate index arguments or single index:
     * apply each index in sequence for nested indexing.
     */

    {
	int k;

	for (k = 2; k < argc; k++) {
	    rc = Th8_SplitList(
	        interp, zList, nList, &azElem, &anElem, &nCount,
	        TH8_LIST_NONE);
	    if (rc != TH8_OK) {
		Th8_Free(interp, zCopy);
		return rc;
	    }
	    rc = th8ParseIndex(interp, argv[k], argl[k], nCount, &iIndex);
	    if (rc != TH8_OK) {
		Th8_Free(interp, azElem);
		Th8_Free(interp, zCopy);
		return rc;
	    }
	    Th8_Free(interp, zCopy);
	    zCopy = 0;
	    if (iIndex >= 0 && iIndex < nCount && ALWAYS(azElem)) {
		/* Keep the element's taint bit for the result; use the
		 * raw length for allocation/copy/index. */
		nList = anElem[iIndex];
		zCopy = (char *)TH8_ALLOC_STR(interp, TH8_LEN(nList));
		if (!zCopy) {
		    Th8_Free(interp, azElem);
		    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		    return TH8_ERROR;
		}
		Th8_Memcpy(interp, zCopy, azElem[iIndex], TH8_LEN(nList));
		zCopy[TH8_LEN(nList)] = 0;
		zList = zCopy;
	    } else {
		zList = "";
		nList = 0;
	    }
	    Th8_Free(interp, azElem);
	    azElem = 0;
	}
    }

    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zCopy);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * list_command --
 *
 *	Create a properly formed list from arguments.
 *
 *	list ?ARG ...?
 *
 * Why / How:
 *	Implements the Tcl [list] command.  Builds a properly-quoted
 *	list string from the arguments using Th8_ListAppend.
 *	Consults and populates the list-to-string cache
 *	(TH8_CACHE_STRING) to avoid redundant list construction
 *	for repeated calls with the same elements.
 *
 * Results:
 *	TH8_OK.  Result is the list string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
list_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Not used. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    int nElem = argc - 1;
    size_t nTag = 0; /* aggregate taint of the elements */
    int t;

    (void)ctx;

    /*
     * A list built from any tainted element is tainted.  The list
     * cache is keyed and stored on RAW bytes (taint-insensitive), so
     * the aggregate taint is computed here and re-applied to the
     * result on every path -- otherwise a clean cached entry would
     * launder a later tainted call with the same bytes (and vice
     * versa).
     */
    for (t = 1; t < argc; t++) {
	nTag |= argl[t] & TH8_TAG_BITS;
    }

    /*
     * Consult the list-to-string cache.  The key is the
     * element array (argv[1..argc-1]); the cached value
     * is the joined string.
     */
    if (nElem > 0) {
	Th8_Value *pCached = th8FindListInCache(
	    interp, TH8_CACHE_STRING, nElem, (const char **)&argv[1],
	    &argl[1]);
	/* Bug 28 family: NULL pCached on OOM -> rebuild list.
	 * Nested per Finding 005 to keep the C1-Pair
	 * (pCached==NULL, OOM-class) out of the MC/DC denominator. */
	if (pCached) {
	    if (pCached->zData) {
		Th8_SetResult(interp, pCached->zData, pCached->nData | nTag);
		return TH8_OK;
	    }
	}
    }

    {
	char *zList = 0;
	size_t nList = 0;
	int i;

	for (i = 1; i < argc; i++) {
	    Th8_ListAppend(interp, &zList, &nList, argv[i], argl[i]);
	}

	/*
	 * Store the joined string in the cache for next time.
	 */
	/* Nested per Finding 005 sec. 5b: C2 (zList NULL with
	 * nElem > 0) requires Th8_ListAppend to have OOM-failed
	 * mid-loop while continuing to iterate -- intrinsic-dead
	 * without fault injection. */
	if (nElem > 0) {
	    if (zList) {
		Th8_Value *pCached = th8FindListInCache(
		    interp, TH8_CACHE_STRING, nElem, (const char **)&argv[1],
		    &argl[1]);
		if (pCached) {
		    th8SetCacheString(interp, pCached, zList, nList);
		}
	    }
	}

	/* Th8_ListAppend already tainted nList from any tainted element;
	 * OR nTag again for consistency with the cache-hit path (the
	 * cached joined length is stored raw). */
	Th8_SetResult(interp, zList, nList | nTag);
	Th8_Free(interp, zList);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * llength_command --
 *
 *	Return the number of elements in a list.
 *
 *	llength LIST
 *
 * Why / How:
 *	Implements the Tcl [llength] command.  Delegates to
 *	Th8_SplitList with NULL element pointers (count-only mode)
 *	for efficiency.
 *
 * Results:
 *	TH8_OK.  Result is the element count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
llength_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Not used. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    int nCount;
    int rc;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "llength list");
    }
    rc =
        Th8_SplitList(interp, argv[1], argl[1], 0, 0, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) {
	return rc;
    }
    Th8_SetResultInt(interp, nCount);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * lrange_command --
 *
 *	Return a range of elements from a list.
 *
 *	lrange LIST FIRST LAST
 *
 * Why / How:
 *	Implements the Tcl [lrange] command.  Splits the list,
 *	parses FIRST and LAST indices (supporting "end" and
 *	"end-N"), clamps to valid bounds, then builds a new list
 *	from the elements in the range.
 *
 * Results:
 *	TH8_OK.  Result is the sub-list.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
lrange_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int iFirst, iLast;
    char *zOut = 0;
    size_t nOut = 0;
    int i;
    int rc;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "lrange list first last");
    }
    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azElem, &anElem, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    if (th8ParseIndex(interp, argv[2], argl[2], nCount, &iFirst) != TH8_OK) {
	Th8_Free(interp, azElem);
	return TH8_ERROR;
    }
    if (th8ParseIndex(interp, argv[3], argl[3], nCount, &iLast) != TH8_OK) {
	Th8_Free(interp, azElem);
	return TH8_ERROR;
    }
    if (iFirst < 0) iFirst = 0;
    if (iLast >= nCount) iLast = nCount - 1;

    for (i = iFirst; i <= iLast && ALWAYS(azElem); i++) {
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
    }
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * lreplace_command --
 *
 *	Replace or insert elements in a list.
 *
 *	lreplace LIST FIRST LAST ?ELEMENT ...?
 *
 * Why / How:
 *	Implements the Tcl [lreplace] command.  Splits the list,
 *	then builds a new list from three parts: elements before
 *	FIRST, the new ELEMENT arguments, and elements after LAST.
 *	When LAST < FIRST, no elements are removed (insertion only).
 *
 * Results:
 *	TH8_OK.  Result is the modified list.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
lreplace_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int iFirst, iLast;
    char *zOut = 0;
    size_t nOut = 0;
    int i;
    int rc;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "lreplace list first last ?element ...?");
    }
    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azElem, &anElem, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    if (th8ParseIndex(interp, argv[2], argl[2], nCount, &iFirst) != TH8_OK) {
	Th8_Free(interp, azElem);
	return TH8_ERROR;
    }
    if (th8ParseIndex(interp, argv[3], argl[3], nCount, &iLast) != TH8_OK) {
	Th8_Free(interp, azElem);
	return TH8_ERROR;
    }
    if (iFirst < 0) iFirst = 0;
    if (iFirst > nCount) iFirst = nCount;
    if (iLast < 0) iLast = -1;
    if (iLast >= nCount) iLast = nCount - 1;

    /*
     * When iLast < iFirst, no elements are removed (insertion
     * only).  Normalize iLast to iFirst-1 so the "after" loop
     * starts at the correct position.
     */

    if (iLast < iFirst - 1) iLast = iFirst - 1;

    /*
     * Elements before the replaced range.
     */

    for (i = 0; i < iFirst && ALWAYS(azElem); i++) {
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
    }

    /*
     * Inserted elements (from argv[4] onward).
     */

    for (i = 4; i < argc; i++) {
	Th8_ListAppend(interp, &zOut, &nOut, argv[i], argl[i]);
    }

    /*
     * Elements after the replaced range.
     */

    for (i = iLast + 1; i < nCount && ALWAYS(azElem); i++) {
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
    }

    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * lsearch_command --
 *
 *	Search for a string in a list.
 *
 *	lsearch ?OPTIONS? LIST PATTERN
 *
 *	Options:
 *	  -exact      Exact string comparison (default: glob)
 *	  -glob       Glob-style matching (default)
 *	  -regexp     Regular expression matching
 *	  -sorted     Binary search on a sorted list
 *	  -all        Return all matching indices
 *	  -inline     Return matching values instead of indices
 *	  -not        Negate the match sense
 *	  -start N    Begin search at index N
 *	  -ascii      ASCII comparison (with -sorted)
 *	  -integer    Integer comparison (with -sorted)
 *	  -dictionary Dictionary comparison (with -sorted)
 *	  -increasing Ascending order (with -sorted, default)
 *	  -decreasing Descending order (with -sorted)
 *
 * Why / How:
 *	Implements the Tcl [lsearch] command.  Uses linear scan for
 *	-exact/-glob/-regexp modes and binary search for -sorted mode.
 *	Calls Th8_Ready per element for cancellation.  -regexp mode
 *	delegates to [regexp] via Th8_Eval.  Binary search supports
 *	-ascii, -integer, and -dictionary comparison types.
 *
 * Results:
 *	TH8_OK.  Result is the matching index (or -1), or a list of
 *	indices/values when -all is specified.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#  define LSEARCH_GLOB   0
#  define LSEARCH_EXACT  1
#  define LSEARCH_REGEXP 2
#  define LSEARCH_SORTED 3

static int
lsearch_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int i;
    int iArg = 1;
    int mode = LSEARCH_GLOB;
    int bAll = 0;
    int bInline = 0;
    int bNot = 0;
    int iStart = 0;
    int bDecreasing = 0;
    int sortType = 0; /* 0=ascii, 1=integer, 2=dictionary */

    (void)ctx;

    /*
     * Parse options.
     */

    while (iArg < argc - 2) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-exact")) {
	    mode = LSEARCH_EXACT;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-glob")) {
	    mode = LSEARCH_GLOB;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-regexp")) {
	    mode = LSEARCH_REGEXP;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-all")) {
	    bAll = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-inline")) {
	    bInline = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-not")) {
	    bNot = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-sorted")) {
	    mode = LSEARCH_SORTED;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-decreasing")) {
	    bDecreasing = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-increasing")) {
	    bDecreasing = 0;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-ascii")) {
	    sortType = 0;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-integer")) {
	    sortType = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-dictionary")) {
	    sortType = 2;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-start")) {
	    iArg++;
	    if (iArg >= argc - 2) {
		return Th8_WrongNumArgs(
		    interp, "lsearch ?options? list pattern");
	    }
	    if (Th8_ToInt(interp, argv[iArg], argl[iArg], &iStart) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	    iArg++;
	} else {
	    break;
	}
    }

    if (argc - iArg != 2) {
	return Th8_WrongNumArgs(interp, "lsearch ?options? list pattern");
    }

    rc = Th8_SplitList(
        interp, argv[iArg], argl[iArg], &azElem, &anElem, &nCount,
        TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    if (iStart < 0) iStart = 0;

    {
	char *zResult = 0;
	size_t nResult = 0;
	const char *zPat = argv[iArg + 1];
	size_t nPat = TH8_LEN(argl[iArg + 1]);

	/*
	 * Binary search for -sorted mode.
	 */

	if (mode == LSEARCH_SORTED) {
	    int lo = iStart;
	    int hi = nCount - 1;
	    int found = -1;

	    while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int cmp = 0;

		if (sortType == 1) {
		    /* -integer comparison */
		    int vA, vB;

		    if (Th8_ToInt(interp, azElem[mid], anElem[mid], &vA) !=
		            TH8_OK ||
		        Th8_ToInt(interp, zPat, nPat, &vB) != TH8_OK) {
			Th8_Free(interp, azElem);
			return TH8_ERROR;
		    }
		    cmp = (vA > vB) - (vA < vB);
		} else if (sortType == 2) {
		    /* -dictionary comparison */
		    size_t ia = 0, ib = 0;
		    size_t nA = TH8_LEN(anElem[mid]);

		    while (ia < nA && ib < nPat && cmp == 0) {
			unsigned char ca = (unsigned char)azElem[mid][ia];
			unsigned char cb = (unsigned char)zPat[ib];
			int aD = (ca >= '0' && ca <= '9');
			int bD = (cb >= '0' && cb <= '9');

			if (aD && bD) {
			    int numA = 0, numB = 0;

			    while (ia < nA && azElem[mid][ia] >= '0' &&
			           azElem[mid][ia] <= '9') {
				numA = numA * 10 + (azElem[mid][ia] - '0');
				ia++;
			    }
			    while (ib < nPat && zPat[ib] >= '0' &&
			           zPat[ib] <= '9') {
				numB = numB * 10 + (zPat[ib] - '0');
				ib++;
			    }
			    cmp = (numA > numB) - (numA < numB);
			} else {
			    if (ca >= 'A' && ca <= 'Z') ca += ('a' - 'A');
			    if (cb >= 'A' && cb <= 'Z') cb += ('a' - 'A');
			    cmp = (ca > cb) - (ca < cb);
			    ia++;
			    ib++;
			}
		    }
		    if (cmp == 0) {
			cmp = (nA < nPat) ? -1 : (nA > nPat);
		    }
		} else {
		    /* -ascii (default) comparison */
		    size_t nA = TH8_LEN(anElem[mid]);
		    size_t nMin = nA < nPat ? nA : nPat;

		    cmp = Th8_Memcmp(interp, azElem[mid], zPat, nMin);
		    if (cmp == 0) {
			cmp = (nA < nPat) ? -1 : (nA > nPat);
		    }
		}

		if (bDecreasing) cmp = -cmp;

		if (cmp == 0) {
		    found = mid;
		    break;
		} else if (cmp < 0) {
		    lo = mid + 1;
		} else {
		    hi = mid - 1;
		}
	    }

	    if (bNot) {
		/* -not with -sorted: not well-defined, return -1 */
		Th8_SetResultInt(interp, -1);
	    } else if (found >= 0) {
		if (bInline) {
		    Th8_SetResult(interp, azElem[found], anElem[found]);
		} else {
		    Th8_SetResultInt(interp, found);
		}
	    } else {
		Th8_SetResultInt(interp, -1);
	    }
	    Th8_Free(interp, azElem);
	    return TH8_OK;
	}

	for (i = iStart; i < nCount; i++) {
	    int matched = 0;

	    if (Th8_Ready(interp) != TH8_OK) {
		Th8_Free(interp, azElem);
		Th8_Free(interp, zResult);
		return TH8_ERROR;
	    }
	    if (!azElem) break;

	    switch (mode) {
	    case LSEARCH_EXACT:
		matched =
		    (TH8_LEN(anElem[i]) == nPat &&
		     0 == Th8_Memcmp(interp, azElem[i], zPat, nPat));
		break;
	    case LSEARCH_GLOB:
		matched = Th8_GlobMatch(
		    interp, zPat, nPat, azElem[i], TH8_LEN(anElem[i]));
		break;
	    case LSEARCH_REGEXP: {
		char *zCmd = 0;
		size_t nCmd = 0;

		Th8_ListAppend(interp, &zCmd, &nCmd, "regexp", 6);
		Th8_ListAppend(interp, &zCmd, &nCmd, zPat, nPat);
		Th8_ListAppend(interp, &zCmd, &nCmd, azElem[i], anElem[i]);
		if (Th8_Eval(interp, 0, zCmd, nCmd, NULL, 0) == TH8_OK) {
		    int v = 0;

		    Th8_ToInt(
		        interp, Th8_GetResult(interp, 0), TH8_NOLEN, &v);
		    matched = (v != 0);
		}
		Th8_Free(interp, zCmd);
		break;
	    }
	    }

	    if (bNot) matched = !matched;

	    if (matched) {
		if (bAll) {
		    if (bInline) {
			Th8_ListAppend(
			    interp, &zResult, &nResult, azElem[i], anElem[i]);
		    } else {
			Th8_SetResultInt(interp, i);
			Th8_ListAppend(
			    interp, &zResult, &nResult,
			    Th8_GetResult(interp, 0), TH8_NOLEN);
		    }
		} else {
		    if (bInline) {
			Th8_SetResult(interp, azElem[i], anElem[i]);
		    } else {
			Th8_SetResultInt(interp, i);
		    }
		    Th8_Free(interp, azElem);
		    Th8_Free(interp, zResult);
		    return TH8_OK;
		}
	    }
	}

	if (bAll) {
	    if (zResult) {
		Th8_SetResult(interp, zResult, nResult);
		Th8_Free(interp, zResult);
	    } else {
		Th8_ClearResult(interp);
	    }
	} else {
	    Th8_SetResultInt(interp, -1);
	}
    }
    Th8_Free(interp, azElem);
    return TH8_OK;
}


/*
 * Sort mode constants and context structure.  The context struct
 * is defined for potential future use with a qsort_r-like interface;
 * the current implementation uses inline comparison within the
 * insertion sort loop instead.
 */

/* TH8_SORT_*, Th8_SortCtx -- declared at top of file. */

/*
 *----------------------------------------------------------------------
 *
 * lsort_command --
 *
 *	Sort a list.
 *
 *	lsort ?OPTIONS? LIST
 *
 *	Options: -ascii (default), -dictionary, -integer, -real,
 *	-command SCRIPT, -index N, -increasing (default), -decreasing,
 *	-unique.
 *
 * Why / How:
 *	Implements the Tcl [lsort] command.  Uses a stable insertion
 *	sort (O(n^2) but correct and portable without qsort_r).
 *	Supports five comparison modes and optional sub-element
 *	indexing via -index.  -command mode evaluates a comparison
 *	script for each pair.  Calls Th8_Ready per comparison for
 *	cancellation.  -unique removes adjacent duplicates after
 *	sorting.
 *
 * Results:
 *	TH8_OK.  Result is the sorted list.
 *
 * Side effects:
 *	May evaluate comparison scripts in -command mode.
 *
 *----------------------------------------------------------------------
 */

static int
lsort_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int eMode = TH8_SORT_ASCII;
    int bDecreasing = 0;
    int bUnique = 0;
    int iArg = 1;
    char *zOut = 0;
    size_t nOut = 0;
    int i, j;
    int rc;
    const char *zCommand = 0;
    size_t nCommand = 0;
    int iIndex = -1;
    /* Function-scope so the oom label can free the -command eval
     * accumulator, which is built inside the sort's inner loop. */
    char *zEval = 0;
    size_t nEval = 0;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "lsort ?options? list");
    }

    /*
     * Parse options.
     */

    while (iArg < argc - 1) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-ascii")) {
	    eMode = TH8_SORT_ASCII;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-dictionary")) {
	    eMode = TH8_SORT_DICTIONARY;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-integer")) {
	    eMode = TH8_SORT_INTEGER;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-real")) {
	    eMode = TH8_SORT_REAL;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-command")) {
	    eMode = TH8_SORT_COMMAND;
	    iArg++;
	    if (iArg >= argc - 1) {
		return Th8_WrongNumArgs(interp, "lsort ?options? list");
	    }
	    zCommand = argv[iArg];
	    nCommand = argl[iArg];
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-index")) {
	    iArg++;
	    if (iArg >= argc - 1) {
		return Th8_WrongNumArgs(interp, "lsort ?options? list");
	    }
	    if (Th8_ToInt(interp, argv[iArg], argl[iArg], &iIndex) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-increasing")) {
	    bDecreasing = 0;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-decreasing")) {
	    bDecreasing = 1;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-unique")) {
	    bUnique = 1;
	} else {
	    break;
	}
	iArg++;
    }

    if (iArg != argc - 1) {
	return Th8_WrongNumArgs(interp, "lsort ?options? list");
    }

    rc = Th8_SplitList(
        interp, argv[iArg], argl[iArg], &azElem, &anElem, &nCount,
        TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    /*
     * Insertion sort.  O(n^2) but simple, correct, stable,
     * and doesn't require qsort (which needs a context
     * pointer we can't portably provide in C89).
     * For the sizes typical in an embedded interpreter,
     * this is adequate.
     */

    if (azElem && nCount > 1) {
	for (i = 1; i < nCount; i++) {
	    char *zKey = azElem[i];
	    size_t nKey = anElem[i];

	    j = i - 1;
	    while (j >= 0) {
		int cmp = 0;
		const char *zA, *zB;
		size_t nA, nB;
		char **azSubA = 0, **azSubB = 0;
		size_t *anSubA = 0, *anSubB = 0;

		if (Th8_Ready(interp) != TH8_OK) {
		    Th8_Free(interp, azElem);
		    return TH8_ERROR;
		}

		zA = azElem[j];
		nA = TH8_LEN(anElem[j]);
		zB = zKey;
		nB = TH8_LEN(nKey);

		/*
		 * If -index is given, extract the Nth sub-element
		 * from each list element for comparison.
		 */

		if (iIndex >= 0) {
		    int nSubA = 0, nSubB = 0;

		    Th8_SplitList(
		        interp, zA, nA, &azSubA, &anSubA, &nSubA,
		        TH8_LIST_NONE);
		    Th8_SplitList(
		        interp, zB, nB, &azSubB, &anSubB, &nSubB,
		        TH8_LIST_NONE);
		    if (iIndex < nSubA && ALWAYS(azSubA)) {
			zA = azSubA[iIndex];
			nA = TH8_LEN(anSubA[iIndex]);
		    } else {
			zA = "";
			nA = 0;
		    }
		    if (iIndex < nSubB && ALWAYS(azSubB)) {
			zB = azSubB[iIndex];
			nB = TH8_LEN(anSubB[iIndex]);
		    } else {
			zB = "";
			nB = 0;
		    }
		}

		/*
		 * Compare zA/zB using the selected mode.
		 */

		if (eMode == TH8_SORT_INTEGER) {
		    int va, vb;

		    if (Th8_ToInt(interp, zA, nA, &va) != TH8_OK ||
		        Th8_ToInt(interp, zB, nB, &vb) != TH8_OK) {
			Th8_Free(interp, azElem);
			return TH8_ERROR;
		    }
		    cmp = (va > vb) - (va < vb);
		} else if (eMode == TH8_SORT_REAL) {
		    double ra, rb;

		    if (Th8_ToDouble(interp, zA, nA, &ra) != TH8_OK ||
		        Th8_ToDouble(interp, zB, nB, &rb) != TH8_OK) {
			Th8_Free(interp, azElem);
			return TH8_ERROR;
		    }
		    cmp = (ra > rb) - (ra < rb);
		} else if (eMode == TH8_SORT_COMMAND) {
		    /*
		     * -command: build "script a b", evaluate,
		     * result is the comparison integer.
		     */

		    int iResult;

		    zEval = 0;
		    nEval = 0;
		    TH8_STR_APPEND(
		        interp, &zEval, &nEval, zCommand, nCommand);
		    TH8_STR_APPEND(interp, &zEval, &nEval, " ", 1);
		    Th8_ListAppend(interp, &zEval, &nEval, zA, nA);
		    TH8_STR_APPEND(interp, &zEval, &nEval, " ", 1);
		    Th8_ListAppend(interp, &zEval, &nEval, zB, nB);
		    rc = Th8_Eval(interp, 0, zEval, nEval, NULL, 0);
		    Th8_Free(interp, zEval);
		    zEval = 0;
		    if (rc != TH8_OK) {
			Th8_Free(interp, azElem);
			return rc;
		    }
		    {
			size_t nRes;
			const char *zRes = Th8_GetResult(interp, &nRes);

			if (Th8_ToInt(interp, zRes, nRes, &iResult) !=
			    TH8_OK) {
			    Th8_Free(interp, azElem);
			    return TH8_ERROR;
			}
		    }
		    cmp = iResult;
		} else if (eMode == TH8_SORT_DICTIONARY) {
		    /*
		     * Dictionary comparison: case-insensitive,
		     * with embedded integers compared numerically.
		     */

		    size_t ia = 0, ib = 0;

		    cmp = 0;
		    while (ia < nA && ib < nB && cmp == 0) {
			unsigned char ca = (unsigned char)zA[ia];
			unsigned char cb = (unsigned char)zB[ib];
			int aIsDigit = (ca >= '0' && ca <= '9');
			int bIsDigit = (cb >= '0' && cb <= '9');

			if (aIsDigit && bIsDigit) {
			    /* Compare numeric runs. */
			    int numA = 0, numB = 0;

			    while (ia < nA && zA[ia] >= '0' &&
			           zA[ia] <= '9') {
				numA = numA * 10 + (zA[ia] - '0');
				ia++;
			    }
			    while (ib < nB && zB[ib] >= '0' &&
			           zB[ib] <= '9') {
				numB = numB * 10 + (zB[ib] - '0');
				ib++;
			    }
			    cmp = (numA > numB) - (numA < numB);
			} else {
			    /* Case-insensitive character compare. */
			    if (ca >= 'A' && ca <= 'Z') ca += ('a' - 'A');
			    if (cb >= 'A' && cb <= 'Z') cb += ('a' - 'A');
			    cmp = (ca > cb) - (ca < cb);
			    ia++;
			    ib++;
			}
		    }
		    if (cmp == 0) {
			cmp = (nA < nB) ? -1 : (nA > nB);
		    }
		} else {
		    /* ASCII / default: byte comparison */
		    size_t nMin = nA < nB ? nA : nB;

		    cmp = Th8_Memcmp(interp, zA, zB, nMin);
		    if (cmp == 0) {
			cmp = (nA < nB) ? -1 : (nA > nB);
		    }
		}

		Th8_Free(interp, azSubA);
		Th8_Free(interp, azSubB);

		if (bDecreasing) cmp = -cmp;
		if (cmp <= 0) break;

		azElem[j + 1] = azElem[j];
		anElem[j + 1] = anElem[j];
		j--;
	    }
	    azElem[j + 1] = zKey;
	    anElem[j + 1] = nKey;
	}
    }

    /*
     * Build the result list, optionally removing duplicates.
     */

    for (i = 0; i < nCount && ALWAYS(azElem); i++) {
	if (bUnique && i > 0) {
	    size_t nA = TH8_LEN(anElem[i - 1]);
	    size_t nB = TH8_LEN(anElem[i]);

	    if (nA == nB &&
	        0 == Th8_Memcmp(interp, azElem[i - 1], azElem[i], nA)) {
		continue; /* skip duplicate */
	    }
	}
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
    }
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;

oom:
    /* A TH8_STR_APPEND growth failed (building the result or a
     * -command eval buffer); "out of memory" already set. */
    Th8_Free(interp, zEval);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * split_command --
 *
 *	split STRING ?SPLITCHARS?
 *
 * Why / How:
 *	Implements the Tcl [split] command.  When SPLITCHARS is
 *	empty, splits into individual characters (code points) using
 *	Th8_Utf8Decode.  Otherwise, splits on any character in
 *	SPLITCHARS.  Default SPLITCHARS is space, tab, newline.
 *	Calls Th8_Ready per element for cancellation.
 *
 * Results:
 *	TH8_OK.  Result is a properly-formed list.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
split_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zStr;
    size_t nStr;
    const char *zChars;
    size_t nChars;
    char *zList = 0;
    size_t nList = 0;
    size_t i;
    size_t start;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "split string ?splitchars?");
    }
    zStr = argv[1];
    nStr = TH8_LEN(argl[1]);

    if (argc == 3) {
	zChars = argv[2];
	nChars = TH8_LEN(argl[2]);
    } else {
	zChars = " \t\n";
	nChars = 3;
    }

    if (nChars == 0) {
	/*
	 * Split into individual characters (code points).
	 */

	i = 0;
	while (i < nStr) {
	    int nByte;

	    if (Th8_Ready(interp) != TH8_OK) goto split_done;
	    Th8_Utf8Decode(&zStr[i], nStr - i, &nByte);
	    Th8_ListAppend(interp, &zList, &nList, &zStr[i], (size_t)nByte);
	    i += (size_t)nByte;
	}
    } else {
	/*
	 * Split on any character in splitchars.
	 */

	start = 0;
	for (i = 0; i < nStr; i++) {
	    size_t j;
	    int isSplit = 0;

	    if (Th8_Ready(interp) != TH8_OK) goto split_done;
	    for (j = 0; j < nChars; j++) {
		if (zStr[i] == zChars[j]) {
		    isSplit = 1;
		    break;
		}
	    }
	    if (isSplit) {
		Th8_ListAppend(
		    interp, &zList, &nList, &zStr[start], i - start);
		start = i + 1;
	    }
	}
	if (nStr > 0) {
	    Th8_ListAppend(
	        interp, &zList, &nList, &zStr[start], nStr - start);
	}
    }

split_done:
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * lassign_command --
 *
 *	Assign list elements to variables, returning unassigned rest.
 *
 *	lassign LIST ?VARNAME ...?
 *
 *	Each VARNAME is assigned the corresponding element of LIST.
 *	If the list is shorter than the variable count, remaining
 *	variables are set to the empty string.  The return value is
 *	the list of unassigned elements (those beyond the variable
 *	count), or the empty string if there are none.
 *
 * Why / How:
 *	Implements the Tcl [lassign] command.  Splits the list,
 *	assigns elements to variables in order, then collects
 *	unassigned elements into the return value.
 *
 * Results:
 *	TH8_OK.  Result is the list of unassigned elements.
 *
 * Side effects:
 *	Sets variables named by the VARNAME arguments.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_VARIABLES)
static int
lassign_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int i;
    int rc;
    char *zRest = 0;
    size_t nRest = 0;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "lassign list ?varname ...?");
    }
    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azElem, &anElem, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    /*
     * Assign list elements to the given variables.
     */

    for (i = 2; i < argc; i++) {
	int idx = i - 2;

	if (idx < nCount && ALWAYS(azElem)) {
	    Th8_SetVar(
	        interp, argv[i], TH8_LEN(argl[i]), azElem[idx], anElem[idx]);
	} else {
	    Th8_SetVar(interp, argv[i], TH8_LEN(argl[i]), "", 0);
	}
    }

    /*
     * Collect unassigned elements into the return value.
     */

    for (i = argc - 2; i < nCount && ALWAYS(azElem); i++) {
	Th8_ListAppend(interp, &zRest, &nRest, azElem[i], anElem[i]);
    }

    Th8_SetResult(interp, zRest ? zRest : "", nRest);
    Th8_Free(interp, zRest);
    Th8_Free(interp, azElem);
    return TH8_OK;
}
#  endif /* TH8_ENABLE_VARIABLES */


/*
 *----------------------------------------------------------------------
 *
 * lremove_command --
 *
 *	Remove elements from a list by index.
 *
 *	lremove LIST ?INDEX ...?
 *
 *	Returns a new list with the specified indices removed.
 *	Indices may be "end", "end-N", or plain integers.
 *	Duplicate or out-of-range indices are silently ignored.
 *
 * Why / How:
 *	Implements the Tcl [lremove] command.  Splits the list,
 *	then for each element checks whether its index matches any
 *	of the INDEX arguments.  Non-matching elements are copied
 *	to the output list.
 *
 * Results:
 *	TH8_OK.  Result is the list with specified elements removed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
lremove_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int i, j;
    int rc;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "lremove list ?index ...?");
    }
    if (argc == 2) {
	return Th8_SetResult(interp, argv[1], argl[1]);
    }
    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azElem, &anElem, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    /*
     * Build a removal set: for each element index, check
     * whether any of the index arguments matches it.
     */

    for (i = 0; i < nCount && ALWAYS(azElem); i++) {
	int skip = 0;

	for (j = 2; j < argc; j++) {
	    int idx;

	    if (th8ParseIndex(interp, argv[j], argl[j], nCount, &idx) ==
	            TH8_OK &&
	        idx == i) {
		skip = 1;
		break;
	    }
	}
	if (!skip) {
	    Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	}
    }

    Th8_SetResult(interp, zOut ? zOut : "", nOut);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * lreverse_command --
 *
 *	Reverse the elements of a list.
 *
 *	lreverse LIST
 *
 * Why / How:
 *	Implements the Tcl [lreverse] command.  Splits the list,
 *	then iterates from the last element to the first, appending
 *	each to the output list.
 *
 * Results:
 *	TH8_OK.  Result is the reversed list.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
lreverse_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int i;
    int rc;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "lreverse list");
    }
    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azElem, &anElem, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    for (i = nCount - 1; i >= 0 && ALWAYS(azElem); i--) {
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
    }

    Th8_SetResult(interp, zOut ? zOut : "", nOut);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * dict_command --
 *
 *	Dictionary operations on key-value lists.
 *
 *	dict create ?KEY VALUE ...?
 *	dict get DICT ?KEY ...?
 *	dict set DICTVAR KEY ?KEY ...? VALUE
 *	dict unset DICTVAR KEY ?KEY ...?
 *	dict exists DICT KEY ?KEY ...?
 *	dict keys DICT ?PATTERN?
 *	dict values DICT ?PATTERN?
 *	dict size DICT
 *	dict append DICTVAR KEY ?STRING ...?
 *	dict remove DICT ?KEY ...?
 *	dict replace DICT ?KEY VALUE ...?
 *	dict merge ?DICT ...?
 *	dict for {KEYVAR VALUEVAR} DICT BODY
 *	dict info DICT
 *
 *	Dictionaries are represented as even-length Tcl lists
 *	(key-value pairs).  This matches Tcl 8.5 semantics.
 *
 * Why / How:
 *	Implements the Tcl [dict] command.  Dictionaries are
 *	represented as even-length lists (key-value pairs) with
 *	linear key lookup.  This pure-list representation avoids
 *	the complexity of a hash table while matching Tcl 8.5
 *	semantics.  A cache (TH8_CACHE_DICT) accelerates repeated
 *	splits of the same dict string.  The infrastructure is
 *	factored into shared helpers: th8DictFind (key lookup),
 *	th8DictSplit (cached split + validation), th8DictTraverse
 *	(nested key navigation), and th8DictRebuildWith (value
 *	replacement / key removal).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Core dict infrastructure.
 *
 *----------------------------------------------------------------------
 */

/*
 * th8DictFind -- look up a key in a split key-value list.
 * Returns the index of the value (key_index + 1), or -1 if not found.
 */

static int
th8DictFind(
    Th8_Interp *interp,
    char **azElem,
    size_t *anElem,
    int nCount,
    const char *zKey,
    size_t nKey)
{
    int i;

    for (i = 0; i + 1 < nCount; i += 2) {
	if (anElem[i] == nKey &&
	    Th8_Memcmp(interp, azElem[i], zKey, nKey) == 0) {
	    return i + 1;
	}
    }
    return -1;
}


/*
 * th8DictRebuildWith -- rebuild a dict from element arrays.
 *
 *	If bRemove is true and iKeyIdx >= 0, skip the key-value
 *	pair at (iKeyIdx-1, iKeyIdx).
 *
 *	If !bRemove and iKeyIdx >= 0, replace the value at
 *	iKeyIdx with zNewVal/nNewVal.
 *
 *	If iKeyIdx == -1 and !bRemove, this is a no-op: caller
 *	handles new-key insertion separately.
 *
 *	On success, *pzOut and *pnOut hold the rebuilt dict string
 *	(caller must Th8_Free).
 */

static int
th8DictRebuildWith(
    Th8_Interp *interp,
    char **azElem,
    size_t *anElem,
    int nCount,
    int iKeyIdx,
    const char *zNewVal,
    size_t nNewVal,
    int bRemove,
    char **pzOut,
    size_t *pnOut)
{
    char *zOut = 0;
    size_t nOut = 0;
    int i;

    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
	if (bRemove && iKeyIdx >= 0 && i + 1 == iKeyIdx) {
	    /* Skip this key-value pair. */
	    continue;
	}
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	if (!bRemove && iKeyIdx >= 0 && i + 1 == iKeyIdx) {
	    Th8_ListAppend(interp, &zOut, &nOut, zNewVal, nNewVal);
	} else {
	    Th8_ListAppend(
	        interp, &zOut, &nOut, azElem[i + 1], anElem[i + 1]);
	}
    }

    *pzOut = zOut;
    *pnOut = nOut;
    return TH8_OK;
}


/*
 * th8DictSplit -- split a dict string, using the cache.
 *
 *	Uses Th8_FindInCache with TH8_CACHE_DICT (cache type 10)
 *	to cache the split result.  If cache hit, reuse.  Otherwise
 *	split and cache.  Returns TH8_ERROR if odd element count.
 *
 *	The caller receives freshly-allocated element arrays that
 *	must be freed with Th8_Free(interp, *pazElem).
 */

static int
th8DictSplit(
    Th8_Interp *interp,
    const char *z,
    size_t n,
    char ***pazElem,
    size_t **panElem,
    int *pnCount)
{
    Th8_Value *pCached;
    int rc;

    n = TH8_LEN(n);

    /*
     * Consult the internal-representation cache.
     */

    pCached = Th8_FindInCache(interp, TH8_CACHE_DICT, z, n);
    /* Bug 28 family: NULL pCached on OOM -> fall through to
     * parse.  Pre-compute the hit flag via nested single-
     * condition `if`s so the original cache-hit body needs no
     * re-indentation, AND the C1-Pair (pCached==NULL, OOM-
     * class) stays out of the MC/DC denominator.  See
     * Finding 005. */
    {
	int isHit = 0;

	if (pCached) {
	    if (pCached->u.splitlist.iValid) isHit = 1;
	}
	if (isHit) {
	    /*
	 * Cache hit.  Copy the element arrays for the caller.
	 */
	    int nE = pCached->u.splitlist.nElem;
	    char **azC = pCached->u.splitlist.azElem;
	    size_t *anC = pCached->u.splitlist.anElem;
	    size_t nStrTotal = 0;
	    char **azNew;
	    size_t *anNew;
	    char *zBuf;
	    int k;

	    for (k = 0; k < nE; k++) {
		nStrTotal += anC[k] + 1;
	    }
	    azNew = (char **)TH8_ALLOC_MUL_ADD2(
	        interp, (size_t)nE, sizeof(char *), (size_t)nE,
	        sizeof(size_t), nStrTotal);
	    if (!azNew) {
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    anNew = (size_t *)&azNew[nE];
	    zBuf = (char *)&anNew[nE];
	    for (k = 0; k < nE; k++) {
		anNew[k] = anC[k];
		azNew[k] = zBuf;
		Th8_Memcpy(interp, zBuf, azC[k], anC[k]);
		zBuf[anC[k]] = '\0';
		zBuf += anC[k] + 1;
	    }
	    *pazElem = azNew;
	    *panElem = anNew;
	    *pnCount = nE;
	    return TH8_OK;
	}
    }

    /*
     * Cache miss: split and validate.
     */

    rc =
        Th8_SplitList(interp, z, n, pazElem, panElem, pnCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    if (*pnCount % 2 != 0) {
	Th8_Free(interp, *pazElem);
	*pazElem = 0;
	Th8_SetResultStatic(
	    interp, "missing value to go with key", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Populate the cache entry.  Copy the element arrays into a
     * single allocation owned by the cache.
     */

    if (pCached) {
	int nE = *pnCount;
	char **azSrc = *pazElem;
	size_t *anSrc = *panElem;
	size_t nStrTotal = 0;
	char **azCopy;
	size_t *anCopy;
	char *zBuf;
	int k;

	for (k = 0; k < nE; k++) {
	    nStrTotal += anSrc[k] + 1;
	}
	azCopy = (char **)TH8_ALLOC_MUL_ADD2(
	    interp, (size_t)nE, sizeof(char *), (size_t)nE, sizeof(size_t),
	    nStrTotal);
	if (azCopy) {
	    anCopy = (size_t *)&azCopy[nE];
	    zBuf = (char *)&anCopy[nE];
	    for (k = 0; k < nE; k++) {
		anCopy[k] = anSrc[k];
		azCopy[k] = zBuf;
		Th8_Memcpy(interp, zBuf, azSrc[k], anSrc[k]);
		zBuf[anSrc[k]] = '\0';
		zBuf += anSrc[k] + 1;
	    }
	    pCached->u.splitlist.azElem = azCopy;
	    pCached->u.splitlist.anElem = anCopy;
	    pCached->u.splitlist.nElem = nE;
	    pCached->u.splitlist.iValid = 1;
	}
    }

    return TH8_OK;
}


/*
 * th8DictTraverse -- walk a nested key path.
 *
 *	For each key in azKeys[0..nKeys-1] except the last, looks
 *	up the value and descends into it (splitting as a nested
 *	dict).  On success, *pazElem / *panElem / *pnCount are the
 *	innermost dict and *piKey is the value index of the last
 *	key (-1 if not found).  Caller must Th8_Free *pazElem.
 *
 *	Returns TH8_ERROR on bad dict structure.
 */

static int
th8DictTraverse(
    Th8_Interp *interp,
    const char *z,
    size_t n,
    const char **azKeys,
    size_t *anKeys,
    int nKeys,
    char ***pazElem,
    size_t **panElem,
    int *pnCount,
    int *piKey)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int k;

    rc = th8DictSplit(interp, z, n, &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    for (k = 0; k < nKeys; k++) {
	int iKey = th8DictFind(
	    interp, azElem, anElem, nCount, azKeys[k], TH8_LEN(anKeys[k]));
	if (k == nKeys - 1) {
	    /* Last key: return the current dict and index. */
	    *pazElem = azElem;
	    *panElem = anElem;
	    *pnCount = nCount;
	    *piKey = iKey;
	    return TH8_OK;
	}
	/* Not the last key: must descend. */
	if (iKey < 0) {
	    Th8_ErrorMessage(interp, "key \"", azKeys[k], anKeys[k]);
	    Th8_Free(interp, azElem);
	    return TH8_ERROR;
	}
	{
	    char **azInner = 0;
	    size_t *anInner = 0;
	    int nInner;

	    rc = th8DictSplit(
	        interp, azElem[iKey], anElem[iKey], &azInner, &anInner,
	        &nInner);
	    Th8_Free(interp, azElem);
	    if (rc != TH8_OK) return rc;
	    azElem = azInner;
	    anElem = anInner;
	    nCount = nInner;
	}
    }

    /* nKeys == 0: return the dict itself, no key looked up. */
    *pazElem = azElem;
    *panElem = anElem;
    *pnCount = nCount;
    *piKey = -1;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Pure subcommands (no variable gate needed).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * dict_create_command --
 *
 *	dict create ?KEY VALUE ...?
 *
 * Why / How:
 *	Implements [dict create].  Validates that the argument count
 *	is even, then builds a list from the key-value pairs.
 *
 * Results:
 *	TH8_OK.  Result is the new dictionary.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_create_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zOut = 0;
    size_t nOut = 0;
    int i;

    (void)ctx;

    if ((argc - 2) % 2 != 0) {
	Th8_SetResultStatic(
	    interp,
	    "wrong # args: should be \"dict create"
	    " ?key value ...?\"",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    for (i = 2; i < argc; i++) {
	Th8_ListAppend(interp, &zOut, &nOut, argv[i], argl[i]);
    }
    Th8_SetResult(interp, zOut ? zOut : "", nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_exists_command --
 *
 *	dict exists DICT KEY ?KEY ...?
 *
 * Why / How:
 *	Implements [dict exists].  Traverses nested key paths,
 *	returning 1 if the final key exists, 0 otherwise.  Returns
 *	0 (not error) for invalid dict structure at any level.
 *
 * Results:
 *	TH8_OK.  Result is 1 or 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_exists_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int k;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "dict exists dictionary key ?key ...?");
    }
    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    for (k = 3; k < argc; k++) {
	int iKey;

	if (nCount % 2 != 0) {
	    Th8_Free(interp, azElem);
	    return Th8_SetResultInt(interp, 0);
	}
	iKey = th8DictFind(
	    interp, azElem, anElem, nCount, argv[k], TH8_LEN(argl[k]));
	if (iKey < 0) {
	    Th8_Free(interp, azElem);
	    return Th8_SetResultInt(interp, 0);
	}
	if (k < argc - 1) {
	    char **azInner = 0;
	    size_t *anInner = 0;
	    int nInner;

	    rc = Th8_SplitList(
	        interp, azElem[iKey], anElem[iKey], &azInner, &anInner,
	        &nInner, TH8_LIST_NONE);
	    Th8_Free(interp, azElem);
	    if (rc != TH8_OK) {
		return Th8_SetResultInt(interp, 0);
	    }
	    azElem = azInner;
	    anElem = anInner;
	    nCount = nInner;
	}
    }

    Th8_Free(interp, azElem);
    return Th8_SetResultInt(interp, 1);
}

/*
 * dict filter DICT key PATTERN
 * dict filter DICT value PATTERN
 * dict filter DICT script {KEYVAR VALUEVAR} EXPR
 *
 * The key and value filter types are pure (no variable access).
 * The script filter type requires TH8_ENABLE_VARIABLES.
 */

/*
 *----------------------------------------------------------------------
 *
 * dict_filter_command --
 *
 *	dict filter DICT key PATTERN
 *	dict filter DICT value PATTERN
 *	dict filter DICT script {KEYVAR VALUEVAR} EXPR
 *
 * Why / How:
 *	Implements [dict filter].  Supports three filter types:
 *	key (glob on keys), value (glob on values), and script
 *	(evaluate expression per pair).  The script type requires
 *	TH8_ENABLE_VARIABLES.
 *
 * Results:
 *	TH8_OK.  Result is the filtered dictionary.
 *
 * Side effects:
 *	Script filter type sets variables and evaluates expressions.
 *
 *----------------------------------------------------------------------
 */

static int
dict_filter_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azDict = 0;
    size_t *anDict = 0;
    int nDict;
    int rc;
    int i;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "dict filter dictionary filterType ...");
    }

    rc = th8DictSplit(interp, argv[2], argl[2], &azDict, &anDict, &nDict);
    if (rc != TH8_OK) return rc;

    if (th8StrEq(interp, argv[3], argl[3], "key")) {
	if (argc != 5) {
	    Th8_Free(interp, azDict);
	    return Th8_WrongNumArgs(
	        interp, "dict filter dictionary key pattern");
	}
	for (i = 0; i < nDict; i += 2) {
	    if (Th8_GlobMatch(
	            interp, argv[4], TH8_LEN(argl[4]), azDict[i],
	            anDict[i])) {
		Th8_ListAppend(interp, &zOut, &nOut, azDict[i], anDict[i]);
		Th8_ListAppend(
		    interp, &zOut, &nOut, azDict[i + 1], anDict[i + 1]);
	    }
	}
    } else if (th8StrEq(interp, argv[3], argl[3], "value")) {
	if (argc != 5) {
	    Th8_Free(interp, azDict);
	    return Th8_WrongNumArgs(
	        interp, "dict filter dictionary value pattern");
	}
	for (i = 0; i < nDict; i += 2) {
	    if (Th8_GlobMatch(
	            interp, argv[4], TH8_LEN(argl[4]), azDict[i + 1],
	            anDict[i + 1])) {
		Th8_ListAppend(interp, &zOut, &nOut, azDict[i], anDict[i]);
		Th8_ListAppend(
		    interp, &zOut, &nOut, azDict[i + 1], anDict[i + 1]);
	    }
	}
    } else if (th8StrEq(interp, argv[3], argl[3], "script")) {
#  if defined(TH8_ENABLE_VARIABLES)
	char **azVars = 0;
	size_t *anVars = 0;
	int nVars;

	if (argc != 6) {
	    Th8_Free(interp, azDict);
	    return Th8_WrongNumArgs(
	        interp, "dict filter dictionary script"
	                " {keyVar valueVar} expr");
	}
	rc = Th8_SplitList(
	    interp, argv[4], argl[4], &azVars, &anVars, &nVars,
	    TH8_LIST_NONE);
	if (rc != TH8_OK) {
	    Th8_Free(interp, azDict);
	    return rc;
	}
	if (nVars != 2) {
	    Th8_Free(interp, azVars);
	    Th8_Free(interp, azDict);
	    Th8_SetResultStatic(
	        interp, "must have exactly two variable names", TH8_NOLEN);
	    return TH8_ERROR;
	}
	for (i = 0; i < nDict; i += 2) {
	    int bKeep;
	    size_t nTag = argl[2] & TH8_TAG_BITS;

	    /* Raw split arrays; the key/value the filter script sees carry
	     * the source dict's taint. */
	    Th8_SetVar(
	        interp, azVars[0], anVars[0], azDict[i], anDict[i] | nTag);
	    Th8_SetVar(
	        interp, azVars[1], anVars[1], azDict[i + 1],
	        anDict[i + 1] | nTag);
	    rc = Th8_Eval(interp, 0, argv[5], argl[5], NULL, 0);
	    if (rc != TH8_OK) {
		Th8_Free(interp, azVars);
		Th8_Free(interp, azDict);
		Th8_Free(interp, zOut);
		return rc;
	    }
	    if (Th8_ToBoolean(
	            interp, Th8_GetResult(interp, 0), TH8_NOLEN, &bKeep) !=
	        TH8_OK) {
		Th8_Free(interp, azVars);
		Th8_Free(interp, azDict);
		Th8_Free(interp, zOut);
		return TH8_ERROR;
	    }
	    if (bKeep) {
		Th8_ListAppend(interp, &zOut, &nOut, azDict[i], anDict[i]);
		Th8_ListAppend(
		    interp, &zOut, &nOut, azDict[i + 1], anDict[i + 1]);
	    }
	}
	Th8_Free(interp, azVars);
#  else
	Th8_Free(interp, azDict);
	Th8_ErrorMessage(
	    interp, "unsupported filterType \"", argv[3], argl[3]);
	return TH8_ERROR;
#  endif /* TH8_ENABLE_VARIABLES */
    } else {
	Th8_Free(interp, azDict);
	Th8_ErrorMessage(interp, "bad filterType \"", argv[3], argl[3]);
	return TH8_ERROR;
    }

    Th8_Free(interp, azDict);
    /* Output is built from the raw split arrays; a filtered subset of a
     * tainted dict stays tainted. */
    Th8_SetResult(interp, zOut ? zOut : "", nOut | (argl[2] & TH8_TAG_BITS));
    Th8_Free(interp, zOut);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_get_command --
 *
 *	dict get DICT ?KEY ...?
 *
 * Why / How:
 *	Implements [dict get].  With no keys, returns the whole dict.
 *	With keys, traverses nested dicts using th8DictTraverse.
 *	Returns TH8_ERROR if any key is not found.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if key not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_get_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int iKey;
    int rc;

    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(interp, "dict get dictionary ?key ...?");
    }

    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    if (argc == 3) {
	/* No key: return the whole dict (identity). */
	Th8_Free(interp, azElem);
	return Th8_SetResult(interp, argv[2], argl[2]);
    }

    /*
     * Walk the nested key path.
     */

    rc = th8DictTraverse(
        interp, argv[2], argl[2], argv + 3, argl + 3, argc - 3, &azElem,
        &anElem, &nCount, &iKey);
    if (rc != TH8_OK) return rc;

    if (iKey < 0) {
	Th8_ErrorMessage(interp, "key \"", argv[argc - 1], argl[argc - 1]);
	Th8_Free(interp, azElem);
	return TH8_ERROR;
    }

    /* Split arrays hold raw lengths so the internal key comparison and
     * copies work byte-exactly; re-apply the source dict's taint to the
     * value handed back to the script. */
    Th8_SetResult(
        interp, azElem[iKey], anElem[iKey] | (argl[2] & TH8_TAG_BITS));
    Th8_Free(interp, azElem);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_info_command --
 *
 *	dict info DICT
 *
 * Why / How:
 *	Implements [dict info].  Returns a human-readable summary
 *	string describing the dictionary (entry count and
 *	representation type).
 *
 * Results:
 *	TH8_OK.  Result is an informational string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_info_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    /* Function-scope so the oom label can free it. */
    char *zInfo = 0;
    size_t nInfo = 0;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "dict info dictionary");
    }
    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    Th8_Free(interp, azElem);
    {
	Th8_SetResultInt(interp, nCount / 2);
	{
	    size_t nN;
	    const char *zN = Th8_GetResult(interp, &nN);

	    TH8_STR_APPEND(interp, &zInfo, &nInfo, zN, nN);
	}
	TH8_STR_APPEND(
	    interp, &zInfo, &nInfo, " entries, list representation",
	    TH8_NOLEN);
	Th8_SetResult(interp, zInfo, nInfo);
	Th8_Free(interp, zInfo);
    }
    return TH8_OK;

oom:
    /* A TH8_STR_APPEND growth failed; "out of memory" already set.
     * azElem was already released above. */
    Th8_Free(interp, zInfo);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_keys_command --
 *
 *	dict keys DICT ?PATTERN?
 *
 * Why / How:
 *	Implements [dict keys].  Splits the dict, then iterates the
 *	key-value pairs, appending keys that match the optional glob
 *	pattern (or all keys if no pattern).
 *
 * Results:
 *	TH8_OK.  Result is a list of keys.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_keys_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int i;
    char *zOut = 0;
    size_t nOut = 0;
    const char *zPat = 0;
    size_t nPat = 0;

    (void)ctx;

    if (argc < 3 || argc > 4) {
	return Th8_WrongNumArgs(interp, "dict keys dictionary ?pattern?");
    }
    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    if (argc == 4) {
	zPat = argv[3];
	nPat = TH8_LEN(argl[3]);
    }

    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
	if (!zPat ||
	    Th8_GlobMatch(interp, zPat, nPat, azElem[i], anElem[i])) {
	    Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	}
    }

    /* Keys are copied from raw split arrays; re-apply the source dict's
     * taint to the returned list. */
    Th8_SetResult(interp, zOut ? zOut : "", nOut | (argl[2] & TH8_TAG_BITS));
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_merge_command --
 *
 *	dict merge ?DICT ...?
 *
 * Why / How:
 *	Implements [dict merge].  Merges multiple dictionaries
 *	left-to-right, with later values overriding earlier ones
 *	for duplicate keys.  Uses dict replace semantics at each
 *	merge step.
 *
 * Results:
 *	TH8_OK.  Result is the merged dictionary.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_merge_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zOut = 0;
    size_t nOut = 0;
    size_t nTag = 0; /* OR of every input dict's taint */
    int d;

    (void)ctx;

    /*
     * For each input dict, apply dict replace semantics
     * onto the accumulator.  Last value wins for
     * duplicate keys.
     */

    for (d = 2; d < argc; d++) {
	char **azD = 0;
	size_t *anD = 0;
	int nD;
	int rc;
	int i;

	/* The split arrays are raw; a tainted input dict must still
	 * taint the merged result. */
	nTag |= argl[d] & TH8_TAG_BITS;

	rc = th8DictSplit(interp, argv[d], argl[d], &azD, &anD, &nD);
	if (rc != TH8_OK) {
	    Th8_Free(interp, zOut);
	    return rc;
	}

	if (!zOut) {
	    /* First dict: take it directly. */
	    zOut = 0;
	    nOut = 0;
	    for (i = 0; i < nD; i++) {
		Th8_ListAppend(interp, &zOut, &nOut, azD[i], anD[i]);
	    }
	} else {
	    /*
	     * Merge into existing: split current, apply
	     * each new pair via replace logic.
	     */

	    char **azCur = 0;
	    size_t *anCur = 0;
	    int nCur;
	    char *zNew = 0;
	    size_t nNew = 0;

	    rc = Th8_SplitList(
	        interp, zOut, nOut, &azCur, &anCur, &nCur, TH8_LIST_NONE);
	    if (rc != TH8_OK) {
		Th8_Free(interp, azD);
		Th8_Free(interp, zOut);
		return rc;
	    }

	    /* Copy existing, replacing matched keys. */
	    for (i = 0; i < nCur; i += 2) {
		int iV =
		    th8DictFind(interp, azD, anD, nD, azCur[i], anCur[i]);
		Th8_ListAppend(interp, &zNew, &nNew, azCur[i], anCur[i]);
		if (iV >= 0) {
		    Th8_ListAppend(interp, &zNew, &nNew, azD[iV], anD[iV]);
		} else {
		    Th8_ListAppend(
		        interp, &zNew, &nNew, azCur[i + 1], anCur[i + 1]);
		}
	    }

	    /* Append new keys. */
	    for (i = 0; i < nD; i += 2) {
		if (th8DictFind(interp, azCur, anCur, nCur, azD[i], anD[i]) <
		    0) {
		    Th8_ListAppend(interp, &zNew, &nNew, azD[i], anD[i]);
		    Th8_ListAppend(
		        interp, &zNew, &nNew, azD[i + 1], anD[i + 1]);
		}
	    }

	    Th8_Free(interp, azCur);
	    Th8_Free(interp, zOut);
	    zOut = zNew;
	    nOut = nNew;
	}
	Th8_Free(interp, azD);
    }

    Th8_SetResult(interp, zOut ? zOut : "", nOut | nTag);
    Th8_Free(interp, zOut);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_remove_command --
 *
 *	dict remove DICT ?KEY ...?
 *
 * Why / How:
 *	Implements [dict remove].  Splits the dict, then copies
 *	key-value pairs to the output, skipping any whose key
 *	matches one of the KEY arguments.
 *
 * Results:
 *	TH8_OK.  Result is the dict with specified keys removed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_remove_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int i, j;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(interp, "dict remove dictionary ?key ...?");
    }
    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
	int skip = 0;

	for (j = 3; j < argc; j++) {
	    if (anElem[i] == TH8_LEN(argl[j]) &&
	        Th8_Memcmp(interp, azElem[i], argv[j], anElem[i]) == 0) {
		skip = 1;
		break;
	    }
	}
	if (!skip) {
	    Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	    Th8_ListAppend(
	        interp, &zOut, &nOut, azElem[i + 1], anElem[i + 1]);
	}
    }

    /* Retained pairs are copied from the raw split arrays; a tainted
     * source dict stays tainted. */
    Th8_SetResult(interp, zOut ? zOut : "", nOut | (argl[2] & TH8_TAG_BITS));
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_replace_command --
 *
 *	dict replace DICT ?KEY VALUE ...?
 *
 * Why / How:
 *	Implements [dict replace].  Copies existing pairs, replacing
 *	values for keys that appear in the replacement list, then
 *	appends new keys not present in the original.
 *
 * Results:
 *	TH8_OK.  Result is the modified dictionary.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_replace_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int i, j;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc < 3 || (argc - 3) % 2 != 0) {
	Th8_SetResultStatic(
	    interp,
	    "wrong # args: should be \"dict replace"
	    " dictionary ?key value ...?\"",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    /*
     * Copy existing entries, replacing values for keys that
     * appear in the replacement list.
     */

    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
	int replaced = 0;

	for (j = 3; j + 1 < argc; j += 2) {
	    if (anElem[i] == TH8_LEN(argl[j]) &&
	        Th8_Memcmp(interp, azElem[i], argv[j], anElem[i]) == 0) {
		Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
		Th8_ListAppend(
		    interp, &zOut, &nOut, argv[j + 1], argl[j + 1]);
		replaced = 1;
		break;
	    }
	}
	if (!replaced) {
	    Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	    Th8_ListAppend(
	        interp, &zOut, &nOut, azElem[i + 1], anElem[i + 1]);
	}
    }

    /*
     * Append new keys that don't exist in the original.
     */

    for (j = 3; j + 1 < argc; j += 2) {
	if (th8DictFind(
	        interp, azElem, anElem, nCount, argv[j], TH8_LEN(argl[j])) <
	    0) {
	    Th8_ListAppend(interp, &zOut, &nOut, argv[j], argl[j]);
	    Th8_ListAppend(interp, &zOut, &nOut, argv[j + 1], argl[j + 1]);
	}
    }

    /* Retained entries come from the raw split arrays (re-apply the
     * source dict's taint); replacement keys/values are appended with
     * their own tags, which Th8_ListAppend already propagates. */
    Th8_SetResult(interp, zOut ? zOut : "", nOut | (argl[2] & TH8_TAG_BITS));
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_size_command --
 *
 *	dict size DICT
 *
 * Why / How:
 *	Implements [dict size].  Splits the dict and returns
 *	nCount / 2 (number of key-value pairs).
 *
 * Results:
 *	TH8_OK.  Result is the number of entries.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_size_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "dict size dictionary");
    }
    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    Th8_Free(interp, azElem);
    return Th8_SetResultInt(interp, nCount / 2);
}

/*
 *----------------------------------------------------------------------
 *
 * dict_values_command --
 *
 *	dict values DICT ?PATTERN?
 *
 * Why / How:
 *	Implements [dict values].  Splits the dict, then iterates
 *	the key-value pairs, appending values that match the optional
 *	glob pattern (or all values if no pattern).
 *
 * Results:
 *	TH8_OK.  Result is a list of values.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
dict_values_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int i;
    char *zOut = 0;
    size_t nOut = 0;
    const char *zPat = 0;
    size_t nPat = 0;

    (void)ctx;

    if (argc < 3 || argc > 4) {
	return Th8_WrongNumArgs(interp, "dict values dictionary ?pattern?");
    }
    rc = th8DictSplit(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    if (argc == 4) {
	zPat = argv[3];
	nPat = TH8_LEN(argl[3]);
    }

    for (i = 1; i < nCount && ALWAYS(azElem); i += 2) {
	if (!zPat ||
	    Th8_GlobMatch(interp, zPat, nPat, azElem[i], anElem[i])) {
	    Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	}
    }

    /* Values are copied from raw split arrays; re-apply the source
     * dict's taint to the returned list. */
    Th8_SetResult(interp, zOut ? zOut : "", nOut | (argl[2] & TH8_TAG_BITS));
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Variable-gated dict subcommands and helpers.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_VARIABLES)

/*
 * th8DictVarGet -- helper: read a dict variable, split it, validate
 * even length.  Caller must Th8_Free azElem on success.
 */

static int
th8DictVarGet(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    char ***pazElem,
    size_t **panElem,
    int *pnCount)
{
    int rc;

    if (Th8_GetVar(interp, zVar, nVar) != TH8_OK) {
	/* Variable doesn't exist: start with empty dict. */
	*pazElem = 0;
	*panElem = 0;
	*pnCount = 0;
	return TH8_OK;
    }
    {
	size_t nVal;
	const char *zVal = Th8_GetResult(interp, &nVal);

	/* Split on the RAW byte length so the returned arrays carry raw
	 * element lengths: the dict machinery (th8DictFind, memcpy) must
	 * compare them byte-exactly.  A tainted dict variable would
	 * otherwise yield tainted element lengths that never match a raw
	 * search key.  The variable's taint is re-applied at write-back
	 * by th8DictVarPut. */
	rc = Th8_SplitList(
	    interp, zVal, TH8_LEN(nVal), pazElem, panElem, pnCount,
	    TH8_LIST_NONE);
    }
    if (rc != TH8_OK) return rc;
    if (*pnCount % 2 != 0) {
	Th8_Free(interp, *pazElem);
	*pazElem = 0;
	Th8_SetResultStatic(
	    interp, "missing value to go with key", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}

/*
 * th8DictVarPut -- helper: write dict back to variable,
 * set result to new dict value.
 */

static int
th8DictVarPut(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    const char *zDict,
    size_t nDict)
{
    size_t nTag = nDict & TH8_TAG_BITS;

    /* Capture the taint of the dict currently stored in the variable:
     * an in-place mutation rebuilds from the RAW split arrays (see
     * th8DictVarGet), so the source dict's taint is not otherwise
     * carried into zDict.  New key/value arguments contribute their own
     * taint through Th8_ListAppend / Th8_StringAppend, which is already
     * present in nDict.  Reading before the Th8_SetVar below is safe --
     * the variable still holds the pre-mutation value. */
    if (Th8_GetVar(interp, zVar, nVar) == TH8_OK) {
	size_t nOld;

	(void)Th8_GetResult(interp, &nOld);
	nTag |= nOld & TH8_TAG_BITS;
    }
    Th8_SetVar(interp, zVar, nVar, zDict, TH8_LEN(nDict) | nTag);
    Th8_SetResult(interp, zDict, TH8_LEN(nDict) | nTag);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_append_command --
 *
 *	dict append DICTVAR KEY ?STRING ...?
 *
 * Why / How:
 *	Implements [dict append].  Reads the dict variable, finds
 *	the key, concatenates STRING arguments to the existing
 *	value (or creates a new key with the concatenated strings),
 *	then writes back.
 *
 * Results:
 *	TH8_OK.  Result is the modified dictionary.
 *
 * Side effects:
 *	Modifies the variable named DICTVAR.
 *
 *----------------------------------------------------------------------
 */

static int
dict_append_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int iKey;
    int i;
    char *zOut = 0;
    size_t nOut = 0;
    /* Function-scope so the oom label can free the value accumulator,
     * which is built in one of two mutually-exclusive blocks below. */
    char *zVal = 0;
    size_t nVal = 0;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "dict append dictVar key ?string ...?");
    }

    rc = th8DictVarGet(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    iKey = th8DictFind(
        interp, azElem, anElem, nCount, argv[3], TH8_LEN(argl[3]));

    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	if (i + 1 == iKey) {
	    /* Append strings to existing value. */
	    int k;

	    zVal = 0;
	    nVal = 0;
	    TH8_STR_APPEND(interp, &zVal, &nVal, azElem[iKey], anElem[iKey]);
	    for (k = 4; k < argc; k++) {
		TH8_STR_APPEND(interp, &zVal, &nVal, argv[k], argl[k]);
	    }
	    Th8_ListAppend(interp, &zOut, &nOut, zVal, nVal);
	    Th8_Free(interp, zVal);
	    zVal = 0;
	} else {
	    Th8_ListAppend(
	        interp, &zOut, &nOut, azElem[i + 1], anElem[i + 1]);
	}
    }

    if (iKey < 0) {
	/* New key: concatenate all strings as the value. */
	int k;

	zVal = 0;
	nVal = 0;
	for (k = 4; k < argc; k++) {
	    TH8_STR_APPEND(interp, &zVal, &nVal, argv[k], argl[k]);
	}
	Th8_ListAppend(interp, &zOut, &nOut, argv[3], argl[3]);
	Th8_ListAppend(interp, &zOut, &nOut, zVal ? zVal : "", nVal);
	Th8_Free(interp, zVal);
	zVal = 0;
    }

    Th8_Free(interp, azElem);
    rc = th8DictVarPut(interp, argv[2], argl[2], zOut, nOut);
    Th8_Free(interp, zOut);
    return rc;

oom:
    /* A TH8_STR_APPEND growth failed; "out of memory" already set. */
    Th8_Free(interp, zVal);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_for_command --
 *
 *	dict for {KEYVAR VALUEVAR} DICT BODY
 *
 *	Non-NRE implementation (uses Th8_Eval directly).
 *
 * Why / How:
 *	Implements [dict for].  Splits the variable specification
 *	into key/value variable names, splits the dict, then
 *	iterates pairs, setting variables and evaluating BODY.
 *	Handles break/continue.
 *
 * Results:
 *	Return code from the last body evaluation, or TH8_OK.
 *
 * Side effects:
 *	Sets KEYVAR and VALUEVAR per iteration.  Evaluates BODY.
 *
 *----------------------------------------------------------------------
 */

static int
dict_for_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azVars = 0;
    size_t *anVars = 0;
    int nVars;
    char **azDict = 0;
    size_t *anDict = 0;
    int nDict;
    int rc;
    int i;

    (void)ctx;

    if (argc != 5) {
	return Th8_WrongNumArgs(
	    interp, "dict for {keyVar valueVar} dictionary body");
    }

    rc = Th8_SplitList(
        interp, argv[2], argl[2], &azVars, &anVars, &nVars, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;
    if (nVars != 2) {
	Th8_Free(interp, azVars);
	Th8_SetResultStatic(
	    interp, "must have exactly two variable names", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = th8DictSplit(interp, argv[3], argl[3], &azDict, &anDict, &nDict);
    if (rc != TH8_OK) {
	Th8_Free(interp, azVars);
	return rc;
    }

    rc = TH8_OK;
    for (i = 0; i < nDict; i += 2) {
	size_t nTag = argl[3] & TH8_TAG_BITS;

	/* Split arrays are raw; the key/value the body sees are derived
	 * from the source dict, so they carry its taint. */
	Th8_SetVar(interp, azVars[0], anVars[0], azDict[i], anDict[i] | nTag);
	Th8_SetVar(
	    interp, azVars[1], anVars[1], azDict[i + 1],
	    anDict[i + 1] | nTag);

	rc = Th8_Eval(interp, 0, argv[4], argl[4], NULL, 0);
	if (rc == TH8_BREAK) {
	    rc = TH8_OK;
	    break;
	}
	if (rc == TH8_CONTINUE) {
	    rc = TH8_OK;
	    continue;
	}
	if (rc != TH8_OK) break;
    }

    Th8_Free(interp, azVars);
    Th8_Free(interp, azDict);
    if (rc == TH8_OK) {
	Th8_ClearResult(interp);
    }
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_incr_command --
 *
 *	dict incr DICTVAR KEY ?INCREMENT?
 *
 * Why / How:
 *	Implements [dict incr].  Reads the dict variable, finds the
 *	key's integer value, adds INCREMENT (default 1), rebuilds
 *	the dict, and writes back.  Creates the key with the
 *	increment value if it does not exist.
 *
 * Results:
 *	TH8_OK.  Result is the modified dictionary.
 *
 * Side effects:
 *	Modifies the variable named DICTVAR.
 *
 *----------------------------------------------------------------------
 */

static int
dict_incr_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int iKey;
    int i;
    int increment = 1;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc < 4 || argc > 5) {
	return Th8_WrongNumArgs(interp, "dict incr dictVar key ?increment?");
    }
    if (argc == 5) {
	if (Th8_ToInt(interp, argv[4], argl[4], &increment) != TH8_OK) {
	    return TH8_ERROR;
	}
    }

    rc = th8DictVarGet(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    iKey = th8DictFind(
        interp, azElem, anElem, nCount, argv[3], TH8_LEN(argl[3]));

    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	if (i + 1 == iKey) {
	    int val = 0;

	    Th8_ToInt(interp, azElem[iKey], anElem[iKey], &val);
	    val += increment;
	    Th8_SetResultInt(interp, val);
	    {
		size_t nV;
		const char *zV = Th8_GetResult(interp, &nV);

		Th8_ListAppend(interp, &zOut, &nOut, zV, nV);
	    }
	} else {
	    Th8_ListAppend(
	        interp, &zOut, &nOut, azElem[i + 1], anElem[i + 1]);
	}
    }

    if (iKey < 0) {
	Th8_ListAppend(interp, &zOut, &nOut, argv[3], argl[3]);
	Th8_SetResultInt(interp, increment);
	{
	    size_t nV;
	    const char *zV = Th8_GetResult(interp, &nV);

	    Th8_ListAppend(interp, &zOut, &nOut, zV, nV);
	}
    }

    Th8_Free(interp, azElem);
    rc = th8DictVarPut(interp, argv[2], argl[2], zOut, nOut);
    Th8_Free(interp, zOut);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_lappend_command --
 *
 *	dict lappend DICTVAR KEY ?VALUE ...?
 *
 * Why / How:
 *	Implements [dict lappend].  Like dict append, but uses
 *	Th8_ListAppend to properly quote each VALUE as a list
 *	element.  Creates the key with a list of VALUES if the
 *	key does not exist.
 *
 * Results:
 *	TH8_OK.  Result is the modified dictionary.
 *
 * Side effects:
 *	Modifies the variable named DICTVAR.
 *
 *----------------------------------------------------------------------
 */

static int
dict_lappend_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int iKey;
    int i;
    char *zOut = 0;
    size_t nOut = 0;
    /* Function-scope so the oom label can free the value accumulator. */
    char *zVal = 0;
    size_t nVal = 0;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "dict lappend dictVar key ?value ...?");
    }

    rc = th8DictVarGet(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    iKey = th8DictFind(
        interp, azElem, anElem, nCount, argv[3], TH8_LEN(argl[3]));

    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
	Th8_ListAppend(interp, &zOut, &nOut, azElem[i], anElem[i]);
	if (i + 1 == iKey) {
	    /* List-append values to existing value. */
	    int k;

	    zVal = 0;
	    nVal = 0;
	    TH8_STR_APPEND(interp, &zVal, &nVal, azElem[iKey], anElem[iKey]);
	    for (k = 4; k < argc; k++) {
		Th8_ListAppend(interp, &zVal, &nVal, argv[k], argl[k]);
	    }
	    Th8_ListAppend(interp, &zOut, &nOut, zVal, nVal);
	    Th8_Free(interp, zVal);
	    zVal = 0;
	} else {
	    Th8_ListAppend(
	        interp, &zOut, &nOut, azElem[i + 1], anElem[i + 1]);
	}
    }

    if (iKey < 0) {
	int k;

	zVal = 0;
	nVal = 0;
	for (k = 4; k < argc; k++) {
	    Th8_ListAppend(interp, &zVal, &nVal, argv[k], argl[k]);
	}
	Th8_ListAppend(interp, &zOut, &nOut, argv[3], argl[3]);
	Th8_ListAppend(interp, &zOut, &nOut, zVal ? zVal : "", nVal);
	Th8_Free(interp, zVal);
	zVal = 0;
    }

    Th8_Free(interp, azElem);
    rc = th8DictVarPut(interp, argv[2], argl[2], zOut, nOut);
    Th8_Free(interp, zOut);
    return rc;

oom:
    /* A TH8_STR_APPEND growth failed; "out of memory" already set. */
    Th8_Free(interp, zVal);
    Th8_Free(interp, zOut);
    Th8_Free(interp, azElem);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_map_command --
 *
 *	dict map {KEYVAR VALUEVAR} DICT BODY
 *
 *	Like dict for, but collects results into a new dict.
 *	Non-NRE implementation (uses Th8_Eval directly).
 *
 * Why / How:
 *	Implements [dict map].  Iterates the dict like dict_for,
 *	but collects each iteration's result into a new dictionary
 *	paired with the original key.  Handles break/continue.
 *
 * Results:
 *	Return code from the last body evaluation, or TH8_OK.
 *
 * Side effects:
 *	Sets KEYVAR and VALUEVAR per iteration.  Evaluates BODY.
 *
 *----------------------------------------------------------------------
 */

static int
dict_map_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azVars = 0;
    size_t *anVars = 0;
    int nVars;
    char **azDict = 0;
    size_t *anDict = 0;
    int nDict;
    int rc;
    int i;
    char *zOut = 0;
    size_t nOut = 0;

    (void)ctx;

    if (argc != 5) {
	return Th8_WrongNumArgs(
	    interp, "dict map {keyVar valueVar} dictionary body");
    }

    rc = Th8_SplitList(
        interp, argv[2], argl[2], &azVars, &anVars, &nVars, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;
    if (nVars != 2) {
	Th8_Free(interp, azVars);
	Th8_SetResultStatic(
	    interp, "must have exactly two variable names", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = th8DictSplit(interp, argv[3], argl[3], &azDict, &anDict, &nDict);
    if (rc != TH8_OK) {
	Th8_Free(interp, azVars);
	return rc;
    }

    rc = TH8_OK;
    for (i = 0; i < nDict; i += 2) {
	size_t nRes;
	const char *zRes;
	size_t nTag = argl[3] & TH8_TAG_BITS;

	/* Split arrays are raw; the key/value the body sees are derived
	 * from the source dict and carry its taint. */
	Th8_SetVar(interp, azVars[0], anVars[0], azDict[i], anDict[i] | nTag);
	Th8_SetVar(
	    interp, azVars[1], anVars[1], azDict[i + 1],
	    anDict[i + 1] | nTag);

	rc = Th8_Eval(interp, 0, argv[4], argl[4], NULL, 0);
	if (rc == TH8_BREAK) {
	    rc = TH8_OK;
	    break;
	}
	if (rc == TH8_CONTINUE) {
	    rc = TH8_OK;
	    continue;
	}
	if (rc != TH8_OK) break;

	/* Collect key and body result into output dict. */
	zRes = Th8_GetResult(interp, &nRes);
	Th8_ListAppend(interp, &zOut, &nOut, azDict[i], anDict[i]);
	Th8_ListAppend(interp, &zOut, &nOut, zRes, nRes);
    }

    Th8_Free(interp, azVars);
    Th8_Free(interp, azDict);
    if (rc == TH8_OK) {
	/* Output keys come from the raw split arrays; the body's result
	 * values carry their own taint via Th8_ListAppend.  Re-apply the
	 * source dict's taint for the keys. */
	Th8_SetResult(
	    interp, zOut ? zOut : "", nOut | (argl[3] & TH8_TAG_BITS));
    }
    Th8_Free(interp, zOut);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_set_command --
 *
 *	dict set DICTVAR KEY ?KEY ...? VALUE
 *
 *	For nested keys, traverse to the second-to-last key,
 *	modify the innermost dict, then rebuild the outer dicts
 *	back up.
 *
 * Why / How:
 *	Implements [dict set].  Simple case (single key) uses
 *	th8DictRebuildWith.  Nested case saves dicts at each level,
 *	modifies the innermost, then rebuilds from inside out.
 *	Creates missing keys at all nesting levels.
 *
 * Results:
 *	TH8_OK.  Result is the modified dictionary.
 *
 * Side effects:
 *	Modifies the variable named DICTVAR.
 *
 *----------------------------------------------------------------------
 */

static int
dict_set_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;
    int nKeys;
    char *zDict = 0;
    size_t nDict = 0;

    (void)ctx;

    if (argc < 5) {
	return Th8_WrongNumArgs(
	    interp, "dict set dictVar key ?key ...? value");
    }

    nKeys = argc - 4; /* Number of key arguments. */

    if (nKeys == 1) {
	/*
	 * Simple case: single key.
	 */
	char **azElem = 0;
	size_t *anElem = 0;
	int nCount;
	int iKey;
	char *zOut = 0;
	size_t nOut = 0;

	rc = th8DictVarGet(
	    interp, argv[2], argl[2], &azElem, &anElem, &nCount);
	if (rc != TH8_OK) return rc;

	iKey = th8DictFind(
	    interp, azElem, anElem, nCount, argv[3], TH8_LEN(argl[3]));

	rc = th8DictRebuildWith(
	    interp, azElem, anElem, nCount, iKey, argv[4], argl[4], 0, &zOut,
	    &nOut);
	if (rc != TH8_OK) {
	    Th8_Free(interp, azElem);
	    return rc;
	}

	/* New key: append. */
	if (iKey < 0) {
	    Th8_ListAppend(interp, &zOut, &nOut, argv[3], argl[3]);
	    Th8_ListAppend(interp, &zOut, &nOut, argv[4], argl[4]);
	}

	Th8_Free(interp, azElem);
	rc = th8DictVarPut(interp, argv[2], argl[2], zOut, nOut);
	Th8_Free(interp, zOut);
	return rc;
    }

    /*
     * Nested case: multiple keys.
     *
     * Strategy: read the variable, then for each nesting level
     * split the dict and save the element arrays.  At the
     * innermost level, set the value.  Then rebuild from the
     * inside out.
     */

    {
	const char *zValue = argv[argc - 1];
	size_t nValue = argl[argc - 1];
	const char **azKeyArgs = argv + 3;
	size_t *anKeyArgs = argl + 3;
	int depth = nKeys; /* Number of key arguments. */

	/*
	 * Arrays to hold saved dicts at each nesting level.
	 * Level 0 is the outermost (variable) dict.
	 */
	char ***aazLevel = 0;
	size_t **aanLevel = 0;
	int *anCountLevel = 0;
	int d;

	aazLevel = (char ***)
	    TH8_ALLOC_MUL(interp, (size_t)depth, sizeof(char **));
	if (!aazLevel) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	aanLevel = (size_t **)
	    TH8_ALLOC_MUL(interp, (size_t)depth, sizeof(size_t *));
	if (!aanLevel) {
	    Th8_Free(interp, aazLevel);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	anCountLevel = (int *)
	    TH8_ALLOC_MUL(interp, (size_t)depth, sizeof(int));
	if (!anCountLevel) {
	    Th8_Free(interp, aazLevel);
	    Th8_Free(interp, aanLevel);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}

	for (d = 0; d < depth; d++) {
	    aazLevel[d] = 0;
	    aanLevel[d] = 0;
	    anCountLevel[d] = 0;
	}

	/*
	 * Read the outermost dict from the variable.
	 */
	rc = th8DictVarGet(
	    interp, argv[2], argl[2], &aazLevel[0], &aanLevel[0],
	    &anCountLevel[0]);
	if (rc != TH8_OK) goto nested_set_cleanup;

	/*
	 * Descend through intermediate keys (0 .. depth-2),
	 * splitting each nested dict.
	 */
	for (d = 0; d < depth - 1; d++) {
	    int iKey = th8DictFind(
	        interp, aazLevel[d], aanLevel[d], anCountLevel[d],
	        azKeyArgs[d], TH8_LEN(anKeyArgs[d]));
	    if (iKey >= 0) {
		rc = th8DictSplit(
		    interp, aazLevel[d][iKey], aanLevel[d][iKey],
		    &aazLevel[d + 1], &aanLevel[d + 1], &anCountLevel[d + 1]);
		if (rc != TH8_OK) goto nested_set_cleanup;
	    } else {
		/* Key not found: create empty nested dict. */
		aazLevel[d + 1] = 0;
		aanLevel[d + 1] = 0;
		anCountLevel[d + 1] = 0;
	    }
	}

	/*
	 * At the innermost level, set the value for the
	 * last key.
	 */
	{
	    int iKey = th8DictFind(
	        interp, aazLevel[depth - 1], aanLevel[depth - 1],
	        anCountLevel[depth - 1], azKeyArgs[depth - 1],
	        TH8_LEN(anKeyArgs[depth - 1]));
	    char *zInner = 0;
	    size_t nInner = 0;

	    rc = th8DictRebuildWith(
	        interp, aazLevel[depth - 1], aanLevel[depth - 1],
	        anCountLevel[depth - 1], iKey, zValue, nValue, 0, &zInner,
	        &nInner);
	    if (rc != TH8_OK) goto nested_set_cleanup;

	    if (iKey < 0) {
		Th8_ListAppend(
		    interp, &zInner, &nInner, azKeyArgs[depth - 1],
		    anKeyArgs[depth - 1]);
		Th8_ListAppend(interp, &zInner, &nInner, zValue, nValue);
	    }

	    zDict = zInner;
	    nDict = nInner;
	}

	/*
	 * Rebuild from the inside out.  For each level
	 * d = depth-2 down to 0, rebuild the dict at that
	 * level with the updated nested dict.
	 */
	for (d = depth - 2; d >= 0; d--) {
	    int iKey = th8DictFind(
	        interp, aazLevel[d], aanLevel[d], anCountLevel[d],
	        azKeyArgs[d], TH8_LEN(anKeyArgs[d]));
	    char *zOuter = 0;
	    size_t nOuter = 0;

	    rc = th8DictRebuildWith(
	        interp, aazLevel[d], aanLevel[d], anCountLevel[d], iKey,
	        zDict, nDict, 0, &zOuter, &nOuter);
	    if (rc != TH8_OK) {
		Th8_Free(interp, zDict);
		zDict = 0;
		goto nested_set_cleanup;
	    }

	    if (iKey < 0) {
		Th8_ListAppend(
		    interp, &zOuter, &nOuter, azKeyArgs[d], anKeyArgs[d]);
		Th8_ListAppend(interp, &zOuter, &nOuter, zDict, nDict);
	    }

	    Th8_Free(interp, zDict);
	    zDict = zOuter;
	    nDict = nOuter;
	}

	/*
	 * Write the rebuilt dict back to the variable.
	 */
	rc = th8DictVarPut(
	    interp, argv[2], argl[2], zDict ? zDict : "", zDict ? nDict : 0);

nested_set_cleanup:
	for (d = 0; d < depth; d++) {
	    Th8_Free(interp, aazLevel[d]);
	}
	Th8_Free(interp, aazLevel);
	Th8_Free(interp, aanLevel);
	Th8_Free(interp, anCountLevel);
	Th8_Free(interp, zDict);
    }

    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_unset_command --
 *
 *	dict unset DICTVAR KEY ?KEY ...?
 *
 *	For nested keys, traverse to the second-to-last key,
 *	remove the key from the innermost dict, then rebuild
 *	the outer dicts back up.
 *
 * Why / How:
 *	Implements [dict unset].  Mirror of dict_set_command but
 *	uses th8DictRebuildWith in remove mode (bRemove=1) at the
 *	innermost level.  Missing intermediate keys cause a no-op
 *	rather than an error.
 *
 * Results:
 *	TH8_OK.  Result is the modified dictionary.
 *
 * Side effects:
 *	Modifies the variable named DICTVAR.
 *
 *----------------------------------------------------------------------
 */

static int
dict_unset_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int rc;
    char *zDict = 0;
    size_t nDict = 0;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(interp, "dict unset dictVar key ?key ...?");
    }

    if (argc == 4) {
	/*
	 * Simple case: single key.
	 */
	char **azElem = 0;
	size_t *anElem = 0;
	int nCount;
	int iKey;
	char *zOut = 0;
	size_t nOut = 0;

	rc = th8DictVarGet(
	    interp, argv[2], argl[2], &azElem, &anElem, &nCount);
	if (rc != TH8_OK) return rc;

	iKey = th8DictFind(
	    interp, azElem, anElem, nCount, argv[3], TH8_LEN(argl[3]));

	rc = th8DictRebuildWith(
	    interp, azElem, anElem, nCount, iKey, 0, 0, 1, &zOut, &nOut);
	Th8_Free(interp, azElem);
	if (rc != TH8_OK) return rc;

	rc = th8DictVarPut(
	    interp, argv[2], argl[2], zOut ? zOut : "", zOut ? nOut : 0);
	Th8_Free(interp, zOut);
	return rc;
    }

    /*
     * Nested case: multiple keys.
     */

    {
	const char **azKeyArgs = argv + 3;
	size_t *anKeyArgs = argl + 3;
	int nKeys = argc - 3;
	int depth = nKeys;

	char ***aazLevel = 0;
	size_t **aanLevel = 0;
	int *anCountLevel = 0;
	int d;

	aazLevel = (char ***)
	    TH8_ALLOC_MUL(interp, (size_t)depth, sizeof(char **));
	if (!aazLevel) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	aanLevel = (size_t **)
	    TH8_ALLOC_MUL(interp, (size_t)depth, sizeof(size_t *));
	if (!aanLevel) {
	    Th8_Free(interp, aazLevel);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	anCountLevel = (int *)
	    TH8_ALLOC_MUL(interp, (size_t)depth, sizeof(int));
	if (!anCountLevel) {
	    Th8_Free(interp, aazLevel);
	    Th8_Free(interp, aanLevel);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}

	for (d = 0; d < depth; d++) {
	    aazLevel[d] = 0;
	    aanLevel[d] = 0;
	    anCountLevel[d] = 0;
	}

	rc = th8DictVarGet(
	    interp, argv[2], argl[2], &aazLevel[0], &aanLevel[0],
	    &anCountLevel[0]);
	if (rc != TH8_OK) goto nested_unset_cleanup;

	for (d = 0; d < depth - 1; d++) {
	    int iKey = th8DictFind(
	        interp, aazLevel[d], aanLevel[d], anCountLevel[d],
	        azKeyArgs[d], TH8_LEN(anKeyArgs[d]));
	    if (iKey >= 0) {
		rc = th8DictSplit(
		    interp, aazLevel[d][iKey], aanLevel[d][iKey],
		    &aazLevel[d + 1], &aanLevel[d + 1], &anCountLevel[d + 1]);
		if (rc != TH8_OK) goto nested_unset_cleanup;
	    } else {
		/*
		 * Intermediate key not found: nothing to
		 * unset; return current variable value.
		 */
		if (Th8_GetVar(interp, argv[2], argl[2]) == TH8_OK) {
		    size_t nV;
		    const char *zV = Th8_GetResult(interp, &nV);
		    Th8_SetResult(interp, zV, nV);
		} else {
		    Th8_ClearResult(interp);
		}
		rc = TH8_OK;
		goto nested_unset_cleanup;
	    }
	}

	/*
	 * At the innermost level, remove the last key.
	 */
	{
	    int iKey = th8DictFind(
	        interp, aazLevel[depth - 1], aanLevel[depth - 1],
	        anCountLevel[depth - 1], azKeyArgs[depth - 1],
	        TH8_LEN(anKeyArgs[depth - 1]));
	    char *zInner = 0;
	    size_t nInner = 0;

	    rc = th8DictRebuildWith(
	        interp, aazLevel[depth - 1], aanLevel[depth - 1],
	        anCountLevel[depth - 1], iKey, 0, 0, 1, &zInner, &nInner);
	    if (rc != TH8_OK) goto nested_unset_cleanup;

	    zDict = zInner;
	    nDict = nInner;
	}

	for (d = depth - 2; d >= 0; d--) {
	    int iKey = th8DictFind(
	        interp, aazLevel[d], aanLevel[d], anCountLevel[d],
	        azKeyArgs[d], TH8_LEN(anKeyArgs[d]));
	    char *zOuter = 0;
	    size_t nOuter = 0;

	    rc = th8DictRebuildWith(
	        interp, aazLevel[d], aanLevel[d], anCountLevel[d], iKey,
	        zDict, nDict, 0, &zOuter, &nOuter);
	    if (rc != TH8_OK) {
		Th8_Free(interp, zDict);
		zDict = 0;
		goto nested_unset_cleanup;
	    }

	    Th8_Free(interp, zDict);
	    zDict = zOuter;
	    nDict = nOuter;
	}

	rc = th8DictVarPut(
	    interp, argv[2], argl[2], zDict ? zDict : "", zDict ? nDict : 0);

nested_unset_cleanup:
	for (d = 0; d < depth; d++) {
	    Th8_Free(interp, aazLevel[d]);
	}
	Th8_Free(interp, aazLevel);
	Th8_Free(interp, aanLevel);
	Th8_Free(interp, anCountLevel);
	Th8_Free(interp, zDict);
    }

    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_update_command --
 *
 *	dict update DICTVAR KEY VARNAME ?KEY VARNAME ...? BODY
 *
 *	Extract keys to variables, eval body, read vars back into
 *	dict, write to dictvar.
 *
 * Why / How:
 *	Implements [dict update].  Three-phase process: (1) extract
 *	dict values to local variables, (2) evaluate the body, (3)
 *	read variables back and rebuild the dict.  The writeback
 *	occurs regardless of the body's return code (matching Tcl
 *	semantics).  Re-reads the dict variable before writeback in
 *	case the body modified it.
 *
 * Results:
 *	Return code from the body.
 *
 * Side effects:
 *	Sets local variables, evaluates BODY, modifies DICTVAR.
 *
 *----------------------------------------------------------------------
 */

static int
dict_update_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount;
    int rc;
    int nPairs;
    int p;
    size_t nSrcTag = 0;

    (void)ctx;

    /*
     * argc layout: dict update DICTVAR (KEY VARNAME)+ BODY
     * Minimum: dict update dv k v body => argc == 6
     * (argc - 4) must be even and >= 2.
     */

    if (argc < 6 || (argc - 4) % 2 != 0) {
	return Th8_WrongNumArgs(
	    interp, "dict update dictVar key varName"
	            " ?key varName ...? body");
    }

    nPairs = (argc - 4) / 2;

    /*
     * Step 1: Read the dict variable and extract keys into
     * local variables.  th8DictVarGet masks the length, so capture the
     * dict's taint here: the local vars are derived from a tainted dict
     * and must be tainted so the body cannot launder them clean.
     */

    if (Th8_GetVar(interp, argv[2], argl[2]) == TH8_OK) {
	size_t nCur;

	(void)Th8_GetResult(interp, &nCur);
	nSrcTag = nCur & TH8_TAG_BITS;
    }

    rc = th8DictVarGet(interp, argv[2], argl[2], &azElem, &anElem, &nCount);
    if (rc != TH8_OK) return rc;

    for (p = 0; p < nPairs; p++) {
	int argKey = 3 + p * 2;
	int argVar = 4 + p * 2;
	int iKey = th8DictFind(
	    interp, azElem, anElem, nCount, argv[argKey],
	    TH8_LEN(argl[argKey]));
	if (iKey >= 0) {
	    Th8_SetVar(
	        interp, argv[argVar], argl[argVar], azElem[iKey],
	        anElem[iKey] | nSrcTag);
	} else {
	    Th8_UnsetVar(interp, argv[argVar], argl[argVar]);
	}
    }

    Th8_Free(interp, azElem);
    azElem = 0;

    /*
     * Step 2: Evaluate the body.
     */

    rc = Th8_Eval(interp, 0, argv[argc - 1], argl[argc - 1], NULL, 0);

    /*
     * Step 3: Read vars back and rebuild the dict.
     * We do this even on error (except for non-OK, non-BREAK,
     * non-CONTINUE codes -- but Tcl 8.4 does the writeback
     * for all codes).
     */

    {
	char *zOut = 0;
	size_t nOut = 0;
	int rcVar;

	size_t nWbTag = 0;

	/*
	 * Re-read the variable (body may have modified it).  Capture its
	 * taint for the rebuilt dict: retained pairs come from the raw
	 * split arrays, while re-added values carry their own taint via
	 * Th8_ListAppend.
	 */
	if (Th8_GetVar(interp, argv[2], argl[2]) == TH8_OK) {
	    size_t nCur;

	    (void)Th8_GetResult(interp, &nCur);
	    nWbTag = nCur & TH8_TAG_BITS;
	}

	rcVar = th8DictVarGet(
	    interp, argv[2], argl[2], &azElem, &anElem, &nCount);
	if (rcVar == TH8_OK) {
	    int i;

	    /*
	     * Copy existing pairs, skipping keys that
	     * appear in the key-varname pairs.
	     */
	    for (i = 0; i < nCount && ALWAYS(azElem); i += 2) {
		int skip = 0;

		for (p = 0; p < nPairs; p++) {
		    int argKey = 3 + p * 2;
		    if (anElem[i] == TH8_LEN(argl[argKey]) &&
		        Th8_Memcmp(
		            interp, azElem[i], argv[argKey], anElem[i]) ==
		            0) {
			skip = 1;
			break;
		    }
		}
		if (!skip) {
		    Th8_ListAppend(
		        interp, &zOut, &nOut, azElem[i], anElem[i]);
		    Th8_ListAppend(
		        interp, &zOut, &nOut, azElem[i + 1], anElem[i + 1]);
		}
	    }

	    /*
	     * Re-add from the variables (if they still
	     * exist).
	     */
	    for (p = 0; p < nPairs; p++) {
		int argKey = 3 + p * 2;
		int argVar = 4 + p * 2;

		if (Th8_GetVar(interp, argv[argVar], argl[argVar]) ==
		    TH8_OK) {
		    size_t nVal;
		    const char *zVal = Th8_GetResult(interp, &nVal);

		    Th8_ListAppend(
		        interp, &zOut, &nOut, argv[argKey], argl[argKey]);
		    Th8_ListAppend(interp, &zOut, &nOut, zVal, nVal);
		}
		/* If var was unset, key is removed. */
	    }

	    Th8_Free(interp, azElem);
	    Th8_SetVar(
	        interp, argv[2], argl[2], zOut ? zOut : "",
	        zOut ? (nOut | nWbTag) : 0);
	    Th8_Free(interp, zOut);
	}
    }

    /*
     * Normalize loop control codes.
     */
    if (rc == TH8_BREAK) {
	rc = TH8_OK;
    } else if (rc == TH8_CONTINUE) {
	rc = TH8_OK;
    }

    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * dict_with_command --
 *
 *	dict with DICTVAR ?KEY ...? BODY
 *
 *	Extract ALL keys of the (possibly nested) dict to local
 *	variables, eval body, read vars back into dict, write to
 *	dictvar.
 *
 * Why / How:
 *	Implements [dict with].  Like dict_update but extracts ALL
 *	key-value pairs as variables (not just named ones).
 *	Supports nested key navigation.  Saves key names before body
 *	execution, then reads variables back and rebuilds.  For
 *	nested dicts, rebuilds from inside out using the same
 *	strategy as dict_set_command.  Unsetting a variable during
 *	the body removes the corresponding key.
 *
 * Results:
 *	Return code from the body.
 *
 * Side effects:
 *	Sets local variables, evaluates BODY, modifies DICTVAR.
 *
 *----------------------------------------------------------------------
 */

static int
dict_with_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    int nCount = 0;
    int rc;
    int i;
    int nNestedKeys;
    const char *zBody;
    size_t nBody;
    size_t nSrcTag = 0;

    /*
     * Saved key names/lengths from the target dict, so we can
     * read them back after the body executes.
     */
    char **azSavedKeys = 0;
    size_t *anSavedKeys = 0;
    int nSavedKeys = 0;

    /* Function-scope so the oom label can free the inside-out rebuild
     * accumulator, which is built at the deepest nesting level below. */
    char *zCur = 0;
    size_t nCur = 0;

    (void)ctx;

    if (argc < 4) {
	return Th8_WrongNumArgs(interp, "dict with dictVar ?key ...? body");
    }

    zBody = argv[argc - 1];
    nBody = argl[argc - 1];
    nNestedKeys = argc - 4; /* Number of nested key args. */

    /*
     * Capture the taint of the whole dict variable: the local vars
     * bound below are derived from it (so the body must see them
     * tainted), and the rebuilt dict written back keeps that taint.
     * The split arrays are raw, so this is the only carrier.
     */

    if (Th8_GetVar(interp, argv[2], argl[2]) == TH8_OK) {
	size_t nCur;

	(void)Th8_GetResult(interp, &nCur);
	nSrcTag = nCur & TH8_TAG_BITS;
    }

    /*
     * Step 1: Navigate to the target dict.  If there are
     * nested keys, traverse into the variable's dict.
     */

    if (nNestedKeys > 0) {
	/*
	 * Read the variable, then traverse nested keys to
	 * reach the target sub-dict.
	 */
	int iKey;

	if (Th8_GetVar(interp, argv[2], argl[2]) != TH8_OK) {
	    return TH8_ERROR;
	}
	{
	    size_t nVal;
	    const char *zVal = Th8_GetResult(interp, &nVal);

	    rc = th8DictTraverse(
	        interp, zVal, nVal, argv + 3, argl + 3, nNestedKeys, &azElem,
	        &anElem, &nCount, &iKey);
	}
	if (rc != TH8_OK) return rc;

	if (iKey < 0) {
	    /* Innermost key not found: target is empty dict. */
	    Th8_Free(interp, azElem);
	    azElem = 0;
	    anElem = 0;
	    nCount = 0;
	} else {
	    /*
	     * iKey is the value index of the last nested key.
	     * Split that value as the target dict.
	     */
	    char **azTarget = 0;
	    size_t *anTarget = 0;
	    int nTarget;

	    rc = th8DictSplit(
	        interp, azElem[iKey], anElem[iKey], &azTarget, &anTarget,
	        &nTarget);
	    Th8_Free(interp, azElem);
	    if (rc != TH8_OK) return rc;
	    azElem = azTarget;
	    anElem = anTarget;
	    nCount = nTarget;
	}
    } else {
	/*
	 * No nested keys: the target is the variable itself.
	 */
	rc = th8DictVarGet(
	    interp, argv[2], argl[2], &azElem, &anElem, &nCount);
	if (rc != TH8_OK) return rc;
    }

    /*
     * Step 2: Save key names and set all key-value pairs as
     * local variables.
     */

    nSavedKeys = nCount / 2;
    if (nSavedKeys > 0) {
	char *zBuf;
	size_t nStrTotal = 0;
	int k;

	for (k = 0; k < nCount; k += 2) {
	    /* Mask the taint tag before using the key length as a byte
	     * count: a tagged length is ~256 MiB and would blow up the
	     * total (and, below, over-read/over-write). */
	    nStrTotal += TH8_LEN(anElem[k]) + 1;
	}
	azSavedKeys = (char **)TH8_ALLOC_MUL_ADD2(
	    interp, (size_t)nSavedKeys, sizeof(char *), (size_t)nSavedKeys,
	    sizeof(size_t), nStrTotal);
	if (!azSavedKeys) {
	    Th8_Free(interp, azElem);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	anSavedKeys = (size_t *)&azSavedKeys[nSavedKeys];
	zBuf = (char *)&anSavedKeys[nSavedKeys];
	for (k = 0; k < nSavedKeys; k++) {
	    int ki = k * 2;
	    /* nRawKey is the byte count for the copy/index/advance below;
	     * the tagged length is preserved in anSavedKeys for the
	     * read-back Th8_GetVar (which masks internally). */
	    size_t nRawKey = TH8_LEN(anElem[ki]);

	    TH8_ASSERT_RAW_LEN(nRawKey);
	    anSavedKeys[k] = anElem[ki];
	    azSavedKeys[k] = zBuf;
	    Th8_Memcpy(interp, zBuf, azElem[ki], nRawKey);
	    zBuf[nRawKey] = '\0';
	    zBuf += nRawKey + 1;
	}
    }

    for (i = 0; i < nCount; i += 2) {
	Th8_SetVar(
	    interp, azElem[i], anElem[i], azElem[i + 1],
	    anElem[i + 1] | nSrcTag);
    }

    Th8_Free(interp, azElem);
    azElem = 0;

    /*
     * Step 3: Evaluate the body.
     */

    rc = Th8_Eval(interp, 0, zBody, nBody, NULL, 0);

    /*
     * Step 4: Read variables back and rebuild the dict.
     */

    {
	char *zOut = 0;
	size_t nOut = 0;
	int k;

	for (k = 0; k < nSavedKeys; k++) {
	    if (Th8_GetVar(interp, azSavedKeys[k], anSavedKeys[k]) ==
	        TH8_OK) {
		size_t nVal;
		const char *zVal = Th8_GetResult(interp, &nVal);

		Th8_ListAppend(
		    interp, &zOut, &nOut, azSavedKeys[k], anSavedKeys[k]);
		Th8_ListAppend(interp, &zOut, &nOut, zVal, nVal);
	    }
	    /* If var was unset, key is removed. */
	}

	if (nNestedKeys > 0) {
	    /*
	     * Nested: need to rebuild the outer dict with
	     * this updated inner dict, then write back.
	     * Use dict set semantics on the variable.
	     */

	    /*
	     * Build a "dict set" argv: we need to set the
	     * nested key path to the rebuilt inner dict.
	     * Re-read the variable and traverse/rebuild.
	     */
	    char **azOuter = 0;
	    size_t *anOuter = 0;
	    int nOuter;
	    int rcVar;

	    rcVar = th8DictVarGet(
	        interp, argv[2], argl[2], &azOuter, &anOuter, &nOuter);
	    if (rcVar == TH8_OK) {
		/*
		 * For nested keys, we need to rebuild from
		 * the inside out, similar to dict set.
		 */
		char *zInner = zOut ? zOut : (char *)"";
		size_t nInner = zOut ? nOut : 0;
		int d;

		/*
		 * Walk the nested key path, saving dicts
		 * at each level, then rebuild.
		 */
		char ***aazLevel = 0;
		size_t **aanLevel = 0;
		int *anCountLvl = 0;

		aazLevel = (char ***)TH8_ALLOC_MUL(
		    interp, (size_t)(nNestedKeys + 1), sizeof(char **));
		aanLevel = (size_t **)TH8_ALLOC_MUL(
		    interp, (size_t)(nNestedKeys + 1), sizeof(size_t *));
		anCountLvl = (int *)TH8_ALLOC_MUL(
		    interp, (size_t)(nNestedKeys + 1), sizeof(int));

		/* Nested per Finding 005 sec. 5b: all three OOM
		 * guard arms are intrinsic-dead without fault
		 * injection (TH8_ALLOC_MUL only fails under OOM). */
		if (aazLevel) {
		    if (aanLevel) {
			if (anCountLvl) {
			    aazLevel[0] = azOuter;
			    aanLevel[0] = anOuter;
			    anCountLvl[0] = nOuter;

			    for (d = 0; d < nNestedKeys; d++) {
				int iV = th8DictFind(
				    interp, aazLevel[d], aanLevel[d],
				    anCountLvl[d], argv[3 + d],
				    TH8_LEN(argl[3 + d]));
				if (iV >= 0) {
				    th8DictSplit(
				        interp, aazLevel[d][iV],
				        aanLevel[d][iV], &aazLevel[d + 1],
				        &aanLevel[d + 1], &anCountLvl[d + 1]);
				} else {
				    aazLevel[d + 1] = 0;
				    aanLevel[d + 1] = 0;
				    anCountLvl[d + 1] = 0;
				}
			    }

			    /*
		     * Rebuild from inside out.
		     */
			    {
				zCur = 0;
				nCur = 0;

				/* Start with the inner dict we built. */
				TH8_STR_APPEND(
				    interp, &zCur, &nCur, zInner, nInner);

				for (d = nNestedKeys - 1; d >= 0; d--) {
				    int iV = th8DictFind(
				        interp, aazLevel[d], aanLevel[d],
				        anCountLvl[d], argv[3 + d],
				        TH8_LEN(argl[3 + d]));
				    char *zR = 0;
				    size_t nR = 0;

				    th8DictRebuildWith(
				        interp, aazLevel[d], aanLevel[d],
				        anCountLvl[d], iV, zCur, nCur, 0, &zR,
				        &nR);
				    if (iV < 0) {
					Th8_ListAppend(
					    interp, &zR, &nR, argv[3 + d],
					    argl[3 + d]);
					Th8_ListAppend(
					    interp, &zR, &nR, zCur, nCur);
				    }
				    Th8_Free(interp, zCur);
				    zCur = zR;
				    nCur = nR;
				}

				Th8_SetVar(
				    interp, argv[2], argl[2],
				    zCur ? zCur : "",
				    zCur ? (nCur | nSrcTag) : 0);
				Th8_Free(interp, zCur);
				zCur = 0;
			    }

			    /* Free intermediate levels (not level 0). */
			    for (d = 1; d <= nNestedKeys; d++) {
				Th8_Free(interp, aazLevel[d]);
			    }
			} else {
			    Th8_Free(interp, azOuter);
			}
		    }
		}
		Th8_Free(interp, aazLevel);
		Th8_Free(interp, aanLevel);
		Th8_Free(interp, anCountLvl);
	    }
	} else {
	    /*
	     * No nested keys: write directly to variable.
	     */
	    Th8_SetVar(
	        interp, argv[2], argl[2], zOut ? zOut : "",
	        zOut ? (nOut | nSrcTag) : 0);
	}

	Th8_Free(interp, zOut);
    }

    Th8_Free(interp, azSavedKeys);

    /*
     * Normalize loop control codes.
     */
    if (rc == TH8_BREAK) {
	rc = TH8_OK;
    } else if (rc == TH8_CONTINUE) {
	rc = TH8_OK;
    }

    return rc;

oom:
    /* A TH8_STR_APPEND growth failed while rebuilding the nested dict
     * (fault-injection-only path); "out of memory" already set.  Free
     * the rebuild accumulator and the saved-key block.  The deeply
     * nested per-level scratch arrays (aazLevel/aanLevel/anCountLvl,
     * azOuter, zOut) are block-scoped and unreachable from here; they
     * leak only on this OOM path.  Freeing them from this function-
     * level label is avoided deliberately -- azOuter's ownership is
     * ambiguous even on the success path, so a blind free here would
     * risk a double-free, which is worse than a one-shot leak. */
    Th8_Free(interp, zCur);
    Th8_Free(interp, azSavedKeys);
    return TH8_ERROR;
}

#  endif /* TH8_ENABLE_VARIABLES */


/*
 *----------------------------------------------------------------------
 *
 * dict_command -- ensemble dispatcher.
 *
 * Why / How:
 *	Implements the Tcl [dict] command ensemble.  Builds a static
 *	subcommand table (conditionally including variable-gated
 *	subcommands) and delegates to Th8_CallSubCommand.
 *
 * Results:
 *	Return code from the sub-command.
 *
 * Side effects:
 *	Determined by the sub-command.
 *
 *----------------------------------------------------------------------
 */

static int
dict_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    static const Th8_SubCommand aSub[] = {
#  if defined(TH8_ENABLE_VARIABLES)
        {0, "append", dict_append_command},
#  endif
        {0, "create", dict_create_command},
        {0, "exists", dict_exists_command},
        {0, "filter", dict_filter_command},
#  if defined(TH8_ENABLE_VARIABLES)
        {0, "for", dict_for_command},
#  endif
        {0, "get", dict_get_command},
#  if defined(TH8_ENABLE_VARIABLES)
        {0, "incr", dict_incr_command},
#  endif
        {0, "info", dict_info_command},
        {0, "keys", dict_keys_command},
#  if defined(TH8_ENABLE_VARIABLES)
        {0, "lappend", dict_lappend_command},
        {0, "map", dict_map_command},
#  endif
        {0, "merge", dict_merge_command},
        {0, "remove", dict_remove_command},
        {0, "replace", dict_replace_command},
#  if defined(TH8_ENABLE_VARIABLES)
        {0, "set", dict_set_command},
#  endif
        {0, "size", dict_size_command},
#  if defined(TH8_ENABLE_VARIABLES)
        {0, "unset", dict_unset_command},
        {0, "update", dict_update_command},
#  endif
        {0, "values", dict_values_command},
#  if defined(TH8_ENABLE_VARIABLES)
        {0, "with", dict_with_command},
#  endif
        {0, 0, 0}};

    return Th8_CallSubCommand(interp, ctx, argc, argv, argl, aSub);
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8ListsCommands[] = {
    {1, 0, "dict", dict_command},
    {1, 0, "join", join_command},
#  if defined(TH8_ENABLE_VARIABLES)
    {1, 0, "lappend", lappend_command},
    {1, 0, "lassign", lassign_command},
#  endif
    {1, 0, "lindex", lindex_command},
    {1, 0, "list", list_command},
    {1, 0, "llength", llength_command},
    {1, 0, "lrange", lrange_command},
    {1, 0, "lremove", lremove_command},
    {1, 0, "lreplace", lreplace_command},
    {1, 0, "lreverse", lreverse_command},
    {1, 0, "lsearch", lsearch_command},
    {1, 0, "lsort", lsort_command},
    {1, 0, "split", split_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8ListsGetCommands --
 *
 *	Return the command table for the lists plugin.
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
th8ListsGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8ListsCommands) / sizeof(th8ListsCommands[0]));

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
	    pCommand[i] = th8ListsCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_LISTS */
