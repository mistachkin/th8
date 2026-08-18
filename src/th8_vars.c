/*
 * th8_vars.c -- Variable subsystem for TH8.
 *
 * Reference-counted scalars and arrays with upvar/global linking,
 * system (read-only) variable protection, and $ substitution.
 *
 * This file implements the variable storage layer: creation, lookup,
 * get/set/unset, array enumeration, save/restore of system variables,
 * and the $ variable substitution pass used by the tokenizer.
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
#include "th8_vars.h"

#if defined(TH8_ENABLE_VARIABLES)


/*
 *----------------------------------------------------------------------
 *
 * th8FreeVariable --
 *
 *	Decrement a variable's reference count and free it when the
 *	count reaches zero.  For arrays, all elements are freed
 *	recursively.
 *
 * Why / How:
 *	Reference-counted variables may be shared across frames via
 *	[upvar] and [global].  This function is the single release
 *	point: every variable destruction path calls it, and the
 *	refcount ensures the storage is freed only when the last
 *	reference is dropped.  Arrays are handled by recursively
 *	freeing each element through the hash iterator.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory may be freed.
 *
 *----------------------------------------------------------------------
 */

static void
th8FreeVariable(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Variable *pVar)  /* Variable to free. */
{
    if (pVar) {
	/*
	 * Defensive: nRef should always be > 0 when called.
	 * A zero or negative refcount indicates a double-free
	 * bug in the caller.
	 */

	if (pVar->nRef <= 0) {
	    return;  /* Already freed or corrupt -- bail. */
	}
	pVar->nRef--;
	if (pVar->nRef <= 0) {
	    /* Defense in depth: securely zero sensitive plaintext before
	     * releasing the scalar value's backing memory. */
	    if (pVar->zData && TH8_SENSITIVE(pVar->nData)) {
		Th8_SecureZero(
		    interp, pVar->zData,
		    pVar->nAlloc > 0 ? pVar->nAlloc
		                     : TH8_LEN(pVar->nData) + 1);
	    }
	    if (pVar->bBorrowed) {
		if (pVar->nAlloc > 0) {
		    th8BufferFree(interp, pVar->zData, pVar->nAlloc);
		}
	    } else {
		Th8_Free(interp, pVar->zData);
	    }
	    if (pVar->pHash) {
		Th8_HashIterate(
		    interp, pVar->pHash, th8FreeVarEntry, (void *)interp);
		Th8_HashDelete(interp, pVar->pHash);
	    }
	    Th8_Free(interp, pVar);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreeVarEntry --
 *
 *	Hash iteration callback: free a variable entry.
 *
 * Why / How:
 *	Used as a callback for Th8_HashIterate when tearing down a
 *	frame or array hash.  Each entry's pData is a Th8_Variable*
 *	whose refcount is decremented via th8FreeVariable.
 *
 * Results:
 *	Always returns TH8_OK (continue iterating).
 *
 * Side effects:
 *	The variable is freed (reference count decremented).
 *
 *----------------------------------------------------------------------
 */

int
th8FreeVarEntry(
    Th8_HashEntry *pEntry, /* Hash entry to process. */
    void *pCtx) /* Interpreter (as void*). */
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;

    if (pEntry->pData) {
	th8FreeVariable(interp, (Th8_Variable *)pEntry->pData);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DeclareSystemVar --
 *
 *	Register a variable name as a system variable so that scripts
 *	cannot modify it via [set] or [unset].
 *
 * Why / How:
 *	System variables (e.g. th8_security) are populated by the
 *	interpreter or host application and must be protected from
 *	script-level mutation.  This function records the name in the
 *	per-interpreter paSystemVar hash with a sentinel value.
 *	Subsequent Th8_IsSystemVar lookups consult that hash.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on allocation failure.
 *
 * Side effects:
 *	The paSystemVar hash is created lazily if it does not exist.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DeclareSystemVar(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_HashEntry *pEntry;

    if (!interp) return TH8_ERROR;

    if (nName == TH8_NOLEN) nName = Th8_Strlen(interp, zName);

    if (!interp->paSystemVar) {
	interp->paSystemVar = Th8_HashNew(interp);
	if (!interp->paSystemVar) return TH8_ERROR;
    }
    pEntry = Th8_HashFind(interp, interp->paSystemVar, zName, nName, 1);
    if (!pEntry) {
	/* create=1 hash insert can fail on OOM (Th8_HashFind returns NULL).
	 * The old code returned TH8_OK regardless, so a transient allocation
	 * failure left the variable NOT marked as a system var (writable from
	 * scripts) while reporting success -- e.g. leaving ::th8_security
	 * script-writable after Th8_RegisterLanguage returned TH8_OK
	 * (TH8K-006 / Bug 85 class). */
	return TH8_ERROR;
    }
    pEntry->pData = (void *)(size_t)1;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_IsSystemVar --
 *
 *	Test whether a variable name has been declared as a system
 *	variable via Th8_DeclareSystemVar.
 *
 * Why / How:
 *	Called by [set] and [unset] command implementations to enforce
 *	the read-only policy on system variables.  For array element
 *	names like "th8_security(algorithmName)", the function strips
 *	the subscript and checks only the base name against the
 *	paSystemVar hash.
 *
 * Results:
 *	1 if the variable is a system variable, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsSystemVar(Th8_Interp *interp, const char *zName, size_t nName)
{
    size_t nBase;

    if (!interp) return 0;

    if (!interp->paSystemVar) return 0;
    if (nName == TH8_NOLEN) nName = Th8_Strlen(interp, zName);

    /*
     * For array elements like "th8_security(algorithmName)",
     * check the base name "th8_security".
     */
    nBase = nName;
    {
	size_t k;
	for (k = 0; k < nName; k++) {
	    if (zName[k] == '(') {
		nBase = k;
		break;
	    }
	}
    }

    return (Th8_HashFind(interp, interp->paSystemVar, zName, nBase, 0) != 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8AnalyzeVarName --
 *
 *	Parse a variable name into its components: global prefix,
 *	outer name, and optional array index.
 *
 * Why / How:
 *	Every variable operation (get, set, unset, exists) begins by
 *	decomposing the name into its parts.  This centralised parser
 *	handles the "::" global prefix and the "name(index)" array
 *	subscript syntax in one pass, so callers can work with the
 *	components directly without duplicating the parsing logic.
 *
 * Results:
 *	None.  Output parameters are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
th8AnalyzeVarName(
    const char *zVar, /* Variable name. */
    size_t nVar, /* Length (TH8_NOLEN = NUL-term). */
    const char **pzOuter, /* OUT: outer name start. */
    size_t *pnOuter, /* OUT: outer name length. */
    const char **pzInner, /* OUT: array index (or NULL). */
    size_t *pnInner, /* OUT: array index length. */
    int *pbGlobal) /* OUT: true if :: prefix. */
{
    size_t i;
    const char *zOuter = zVar;
    size_t nOuter;

    if (nVar == TH8_NOLEN) {
	nVar = Th8_Strlen(NULL, zVar);
    }
    nVar = TH8_LEN(nVar);

    *pbGlobal = 0;
    *pzInner = 0;
    *pnInner = 0;

    /*
     * Check for global :: prefix.
     */

    if (nVar > 2 && zVar[0] == ':' && zVar[1] == ':') {
	*pbGlobal = 1;
	zOuter = &zVar[2];
	nOuter = nVar - 2;
    } else {
	nOuter = nVar;
    }

    /*
     * Check for array subscript: name(index).
     */

    if (nOuter > 0 && zOuter[nOuter - 1] == ')') {
	for (i = 0; i < nOuter; i++) {
	    if (zOuter[i] == '(') {
		*pzInner = &zOuter[i + 1];
		*pnInner = nOuter - i - 2;
		nOuter = i;
		break;
	    }
	}
    }

    *pzOuter = zOuter;
    *pnOuter = nOuter;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FindValue --
 *
 *	Locate the Th8_Variable for a given variable name.
 *
 * Why / How:
 *	This is the central variable resolution engine.  It parses the
 *	name via th8AnalyzeVarName, resolves namespace-qualified paths,
 *	walks the call-frame chain for global references, and drills
 *	into array hashes for subscripted names.  All public get/set/
 *	unset/exists operations are built on top of this function.
 *
 * Results:
 *	Pointer to the variable, or NULL if not found (and bCreate
 *	is false).
 *
 * Side effects:
 *	If bCreate is true, the variable is created if it does not
 *	exist.  Error messages may be set on the interpreter.
 *
 *----------------------------------------------------------------------
 */

static Th8_Variable *
th8FindValue(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar, /* Length (TH8_NOLEN = NUL-term). */
    int bCreate) /* Create if not found? */
{
    const char *zOuter, *zInner;
    size_t nOuter, nInner;
    int bGlobal;
    Th8_HashEntry *pEntry;
    Th8_Variable *pVar;
    Th8_Frame *pFrame;
    /* Function-scope so the oom label can free the namespace-path
     * accumulator built in the qualified-name branch below. */
    char *zFull = 0;
    size_t nFull = 0;

    th8AnalyzeVarName(
        zVar, nVar, &zOuter, &nOuter, &zInner, &nInner, &bGlobal);

    /*
     * Namespace-qualified variable: if the outer name (after
     * stripping :: prefix) contains "::", it refers to a
     * variable in a specific namespace's paVar.
     *
     * Examples:
     *   ::foo::x     -> namespace ::foo, var x
     *   ::foo::bar::y -> namespace ::foo::bar, var y
     *   ::x          -> global frame, var x (no :: in "x")
     */

    if (bGlobal) {
	size_t k;
	int hasNs = 0;

	for (k = 0; k + 1 < nOuter; k++) {
	    if (zOuter[k] == ':' && zOuter[k + 1] == ':') {
		hasNs = 1;
		break;
	    }
	}

	if (hasNs) {
	    /*
	     * Split into namespace path + variable tail.
	     * Reconstruct the full qualified path for lookup.
	     */

	    const char *zNsPath;
	    size_t nNsPath;
	    const char *zTail;
	    size_t nTail;
	    Th8_Namespace *pNs;

	    th8SplitQualName(
	        zOuter, nOuter, &zNsPath, &nNsPath, &zTail, &nTail);

	    /*
	     * Build the full ns path: "::" + zNsPath.
	     */

	    {
		zFull = 0;
		nFull = 0;

		TH8_STR_APPEND(interp, &zFull, &nFull, "::", 2);
		/* Reaching this branch requires the outer name to
		 * contain "::" (per the hasNs loop above), so
		 * th8SplitQualName finds the separator and sets
		 * zNsPath (non-NULL) with positive length.  Both
		 * sub-conditions are defensive belt-and-braces
		 * tests, ALWAYS T at runtime. */
		if (ALWAYS(zNsPath != NULL && nNsPath > 0)) {
		    TH8_STR_APPEND(interp, &zFull, &nFull, zNsPath, nNsPath);
		}
		pNs = th8FindNamespace(interp, zFull, nFull, bCreate);
		Th8_Free(interp, zFull);
	    }
	    if (!pNs) {
		return 0;
	    }

	    /*
	     * Look up in the namespace's paVar.  Handle
	     * array subscripts from zInner.
	     */

	    pEntry = Th8_HashFind(
	        interp, pNs->paVar, zTail, nTail, bCreate ? 1 : 0);
	    if (!pEntry) {
		return 0;
	    }
	    pVar = (Th8_Variable *)pEntry->pData;
	    if (!pVar) {
		if (!bCreate) return 0;
		pVar = (Th8_Variable *)
		    TH8_ALLOC(interp, sizeof(Th8_Variable));
		if (!pVar) return 0;
		pVar->nRef = 1;
		pEntry->pData = (void *)pVar;
	    }
	    goto check_array;
	}
    }

    /*
     * Simple variable: look in the frame.
     */

    if (bGlobal) {
	pFrame = interp->pFrame;
	while (pFrame->pCaller) {
	    pFrame = pFrame->pCaller;
	}
    } else {
	pFrame = interp->pFrame;
    }

    pEntry =
        Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, bCreate ? 1 : 0);
    if (!pEntry) {
	return 0;
    }

    pVar = (Th8_Variable *)pEntry->pData;
    if (!pVar) {
	if (!bCreate) {
	    return 0;
	}
	pVar = (Th8_Variable *)TH8_ALLOC(interp, sizeof(Th8_Variable));
	if (!pVar) return 0;
	pVar->nRef = 1;
	pEntry->pData = (void *)pVar;
    }

check_array:

    /*
     * If this is an array reference, drill into the hash.
     */

    if (zInner) {
	Th8_Variable *pArr = pVar; /* Parent array; epoch lives here. */

	if (!pArr->pHash) {
	    if (!bCreate) {
		return 0;
	    }
	    if (pArr->zData) {
		Th8_ErrorMessage(
		    interp, "variable is a scalar:", zOuter, nOuter);
		return 0;
	    }
	    pArr->pHash = Th8_HashNew(interp);
	    if (!pArr->pHash) return 0;
	    /*
	     * Stamp a fresh generation onto the variable so any
	     * pending array-search SID can detect that this hash
	     * was newly (re)allocated -- even if the allocator
	     * handed us back the same address that the previous
	     * incarnation occupied.
	     */
	    pArr->nGeneration = ++interp->iArrayHashGeneration;
	}
	pEntry = Th8_HashFind(
	    interp, pArr->pHash, zInner, nInner, bCreate ? 1 : 0);
	if (!pEntry) {
	    return 0;
	}
	pVar = (Th8_Variable *)pEntry->pData;
	if (!pVar) {
	    if (!bCreate) {
		return 0;
	    }
	    pVar = (Th8_Variable *)TH8_ALLOC(interp, sizeof(Th8_Variable));
	    if (!pVar) return 0;
	    pVar->nRef = 1;
	    pEntry->pData = (void *)pVar;
	}
	/*
	 * Any element-level write (creation, value update, append,
	 * lappend, incr ...) bumps the parent array's epoch so any
	 * pending [array startsearch] iterations detect the mutation
	 * on their next nextelement/anymore call.  Read paths
	 * (bCreate==0) leave the epoch alone.
	 */
	if (bCreate) {
	    pArr->nEpoch++;
	}
    } else if (pVar->pHash && ALWAYS(!pVar->zData) && !bCreate) {
	/*
	 * Bare array name without a subscript.  Return the
	 * variable so that callers like Th8_ExistsVar can
	 * inspect it.  Callers that need a scalar value
	 * (Th8_GetVar) must check for the array case
	 * themselves.
	 *
	 * pHash and zData are mutually exclusive (see Th8_GetVar
	 * L562); !zData is ALWAYS T when pHash is set.
	 */
	return pVar;
    }

    return pVar;

oom:
    /* A TH8_STR_APPEND growth failed while building the namespace
     * path; "out of memory" already set.  Free the partial path and
     * report lookup failure (NULL). */
    Th8_Free(interp, zFull);
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetVar --
 *
 *	Retrieve the value of a variable.
 *
 * Why / How:
 *	Implements the read path for [set varName] (no value argument)
 *	and the internal $-substitution engine.  Delegates to
 *	th8FindValue for resolution, then copies the value into the
 *	interpreter result.  Bare array names (no subscript) are
 *	rejected with an error.  Secure variables are routed through
 *	th8SecureGetVar for decryption.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if variable does not exist.
 *
 * Side effects:
 *	Sets the interpreter result to the variable's value.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetVar(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar) /* Length (TH8_NOLEN = NUL-term). */
{
    Th8_Variable *pVar;

    if (!interp) return TH8_ERROR;

    TH8_ASSERT_OWNER(interp);

    pVar = th8FindValue(interp, zVar, nVar, 0);
    if (!pVar) {
	Th8_ErrorMessage(interp, "no such variable: \"", zVar, nVar);
	return TH8_ERROR;
    }
    /* Array detection: pHash is set when the variable is an
     * array; zData is set when it is a scalar.  The two
     * states are mutually exclusive by construction (the
     * scalar set/append path frees pHash before assigning
     * zData; th8MakeArray frees zData before assigning
     * pHash), so whenever pHash is non-NULL here, zData is
     * ALWAYS NULL.  The !zData sub-check is a defensive
     * belt-and-braces test, never F at runtime. */
    if (pVar->pHash && ALWAYS(!pVar->zData)) {
	Th8_ErrorMessage(interp, "variable is an array: \"", zVar, nVar);
	return TH8_ERROR;
    }

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (th8IsSecureVar(interp, zVar, nVar)) {
	return th8SecureGetVar(interp, zVar, nVar);
    }
#  endif

    return Th8_SetResult(interp, pVar->zData, pVar->nData);
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetVarValue --
 *
 *	Read a variable's current value into a caller-provided
 *	Th8_Value.  The Value's zData and nData point directly
 *	into the variable's storage (borrowed - valid until the
 *	variable is modified or unset).
 *
 *	The Value is also populated as a buffer description so it
 *	can be inspected uniformly:
 *	  pValue->zData  = variable string data (borrowed)
 *	  pValue->nData  = variable string length
 *
 *	On error (variable does not exist or is a bare array),
 *	sets the interpreter result to the error message and
 *	returns TH8_ERROR.  The Value is not modified on error.
 *
 * Why / How:
 *	Used by [append] and other commands that need direct access
 *	to a variable's storage without copying through the interpreter
 *	result.  The returned Value borrows the variable's zData pointer,
 *	avoiding an allocation for the common read-then-modify pattern.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the variable does not exist
 *	or is a bare array.
 *
 * Side effects:
 *	May set the interpreter result to an error message on failure.
 *
 *----------------------------------------------------------------------
 */

int
th8GetVarValue(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    Th8_Value *pValue)
{
    Th8_Variable *pVar;

    if (!pValue) return TH8_ERROR;

    pVar = th8FindValue(interp, zVar, nVar, 0);
    if (!pVar) {
	Th8_ErrorMessage(interp, "no such variable: \"", zVar, nVar);
	return TH8_ERROR;
    }
    /* See Th8_GetVar L562: pHash and zData are mutually
     * exclusive, so !zData is ALWAYS T when pHash is set. */
    if (pVar->pHash && ALWAYS(!pVar->zData)) {
	Th8_ErrorMessage(interp, "variable is an array: \"", zVar, nVar);
	return TH8_ERROR;
    }
    pValue->zData = pVar->zData;
    pValue->nData = pVar->nData;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetVarLength --
 *
 *	Update a variable's recorded length without reallocating or
 *	copying.  Used after in-place buffer extension on a borrowed
 *	variable (bBorrowed=1) where zData still points into the
 *	cache buffer but nData needs updating.
 *
 *	Also updates zData if the buffer was reallocated (the pointer
 *	may have changed).
 *
 * Why / How:
 *	The [append] fast path grows a cache buffer in-place and then
 *	needs the variable's nData to reflect the new length without
 *	copying the data.  This function patches the variable's
 *	metadata (zData pointer and nData length) directly, which is
 *	safe because the variable's bBorrowed flag is set and the
 *	buffer is cache-owned.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the variable does not exist.
 *
 * Side effects:
 *	The variable's zData and nData are updated in place.
 *
 *----------------------------------------------------------------------
 */

int
th8SetVarLength(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    const char *zNewData,
    size_t nNewLen)
{
    Th8_Variable *pVar;

    pVar = th8FindValue(interp, zVar, nVar, 0);
    if (!pVar) return TH8_ERROR;

    /* This function only patches a borrowed (cache-owned) buffer.
     * Calling it on a non-borrowed variable would overwrite the
     * heap-owned zData pointer with the caller's buffer, leaking
     * the original allocation and crashing on subsequent unset.
     * Refuse so callers get an explicit error rather than silent
     * corruption.  Bug 21 hardening. */
    if (!pVar->bBorrowed) return TH8_ERROR;

    pVar->zData = (char *)zNewData;
    pVar->nData = TH8_LEN(nNewLen);

    /* Variable changed: bump nWait + signal for [vwait]. */
    pVar->nWait++;
    th8SignalAllStates(interp);

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8AppendInPlace --
 *
 *	Try to append data to a variable's borrowed buffer without
 *	any allocation or copying of the existing value.  Succeeds
 *	only when the variable already borrows a pool buffer whose
 *	capacity (nAlloc) can accommodate the additional data.
 *
 * Why / How:
 *	The looped [append] pattern (e.g. "for ... { append s $x }")
 *	is O(n^2) if every iteration copies the full string into a
 *	fresh buffer.  By extending in-place, the total work becomes
 *	O(n).  Power-of-2 headroom allocated by the new-buffer path
 *	ensures amortized O(1) per append call.
 *
 * Results:
 *	TH8_OK if the in-place extension succeeded.  TH8_ERROR if
 *	the variable does not exist, is not borrowed, or lacks
 *	capacity.  The caller should fall through to the new-buffer
 *	path on TH8_ERROR.
 *
 * Side effects:
 *	On success, the variable's nData and the buffer content are
 *	updated.  The interp result is set to the new value.
 *
 *----------------------------------------------------------------------
 */

int
th8AppendInPlace(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    int nArgs,
    const char **azArg,
    const size_t *anArg)
{
    Th8_Variable *pVar;
    size_t nCur;
    size_t nAppend = 0;
    size_t nNeeded = 0;
    size_t nTag = 0; /* OR of existing value + appended taints */
    int i;

    /* Bug 26 (2026-06-07): plain check rather than NEVER -- borrowed
     * vars are expected to have nAlloc>0, but the live check is
     * cheap and avoids an undefined slot under TH8_OMIT collapse.
     * Split per Finding 005 sec. 5b: C3 (nAlloc == 0) intrinsic-
     * dead in test corpus per the comment above. */
    pVar = th8FindValue(interp, zVar, nVar, 0);
    if (!pVar) return TH8_ERROR;
    if (!pVar->bBorrowed) return TH8_ERROR;
    if (pVar->nAlloc == 0) return TH8_ERROR;

    nCur = TH8_LEN(pVar->nData);
    nTag = pVar->nData & TH8_TAG_BITS;
    for (i = 0; i < nArgs; i++) {
	nTag |= (anArg[i] & TH8_TAG_BITS);
	nAppend += TH8_LEN(anArg[i]);
    }

    /* Split per Finding 005 sec. 5b: C1 (TH8_SAFE_ADD_SIZE
     * overflow) requires absurdly large append sizes --
     * intrinsic-dead in the test corpus. */
    if (TH8_SAFE_ADD_SIZE(nCur, nAppend, &nNeeded)) return TH8_ERROR;
    if (nNeeded >= pVar->nAlloc) return TH8_ERROR;

    for (i = 0; i < nArgs; i++) {
	Th8_Memcpy(interp, pVar->zData + nCur, azArg[i], TH8_LEN(anArg[i]));
	nCur += TH8_LEN(anArg[i]);
    }
    pVar->zData[nCur] = '\0';
    /* nCur is the raw total; the stored/returned length carries the
     * accumulated taint. */
    pVar->nData = nCur | nTag;
    Th8_SetResult(interp, pVar->zData, nCur | nTag);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetVar --
 *
 *	Set the value of a variable, creating it if necessary.
 *
 * Why / How:
 *	Implements [set varName value].  Resolves the variable via
 *	th8FindValue(bCreate=1), invalidates any stale append-buffer
 *	cache entry, allocates a fresh copy of the new value, and
 *	stores it.  For secure variables, the plaintext is encrypted
 *	via th8SecureSetVar and then zeroed from heap memory.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the name is invalid.
 *
 * Side effects:
 *	Variable value is updated.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetVar(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar, /* Name length (TH8_NOLEN = NUL). */
    const char *zVal, /* Value to set. */
    size_t nVal) /* Value length (TH8_NOLEN = NUL). */
{
    Th8_Variable *pVar;
    size_t nTag = 0; /* taint bit of the incoming value, if any */

    if (!interp) return TH8_ERROR;

    TH8_ASSERT_OWNER(interp);

    pVar = th8FindValue(interp, zVar, nVar, 1);
    if (!pVar) {
	return TH8_ERROR;
    }

    /*
     * A bare array name cannot be assigned a scalar value (Tcl:
     * "can't set X: variable is array").  For a bare array name
     * th8FindValue returns the array variable itself, which has
     * pHash set; subscripted names resolve to a (scalar) element
     * whose pHash is NULL.  Without this guard the scalar write
     * below would set zData on a variable that already owns an
     * element hash, violating the scalar-XOR-array invariant and
     * later tripping th8FindValue's bare-array-read path.  This is
     * the mirror of the element-on-scalar guard in th8FindValue.
     */
    if (pVar->pHash) {
	Th8_ErrorMessage(
	    interp, "can't set variable (is array):", zVar, nVar);
	return TH8_ERROR;
    }

    if (nVal == TH8_NOLEN) {
	nVal = Th8_Strlen(interp, zVal);
    } else {
	nTag = nVal & TH8_TAG_BITS;
	nVal = TH8_LEN(nVal);
    }

    /*
     * Always invalidate any append buffer cache entry when a
     * variable is written via [set].  This ensures a subsequent
     * [append] re-seeds from the new value rather than using a
     * stale buffer from a prior append sequence.
     */
    th8RemoveFromCache(interp, TH8_CACHE_BUFFER, zVar, nVar);

    /* Defense in depth: if the old value was sensitive plaintext, securely
     * zero it before releasing its (pageable) backing memory. */
    if (pVar->zData && TH8_SENSITIVE(pVar->nData)) {
	Th8_SecureZero(
	    interp, pVar->zData,
	    pVar->nAlloc > 0 ? pVar->nAlloc : TH8_LEN(pVar->nData) + 1);
    }
    if (pVar->bBorrowed) {
	/* Return the borrowed buffer to the pool. */
	if (pVar->nAlloc > 0) {
	    th8BufferFree(interp, pVar->zData, pVar->nAlloc);
	}
    } else {
	Th8_Free(interp, pVar->zData);
    }
    pVar->bBorrowed = 0;
    pVar->nAlloc = 0;
    /* nData carries the tag bits (taint/sensitive); allocation/copy/index
     * below use the raw length nVal so the buffer size stays correct. */
    pVar->nData = nVal | nTag;
    pVar->zData = (char *)TH8_ALLOC_STR(interp, nVal);
    if (!pVar->zData) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (zVal) {
	Th8_Memcpy(interp, pVar->zData, zVal, nVal);
    }
    pVar->zData[nVal] = 0;

    /* Variable was created or its value changed: bump nWait
     * and signal the per-interp event so any [vwait] on this
     * variable wakes up.  See th8_vars.h design notes. */
    pVar->nWait++;
    th8SignalAllStates(interp);

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /*
     * If this is a secure variable, encrypt the new value and
     * rotate the key.  The plaintext in pVar->zData is zeroed
     * after encryption to prevent it from lingering in heap.
     */

    if (th8IsSecureVar(interp, zVar, nVar)) {
	int rcSec = th8SecureSetVar(
	    interp, zVar, nVar, pVar->zData, TH8_LEN(pVar->nData));

	/* Zero plaintext before freeing (raw length; nData may be
	 * tainted). */
	Th8_SecureZero(interp, pVar->zData, TH8_LEN(pVar->nData) + 1);
	Th8_Free(interp, pVar->zData);
	pVar->zData = (char *)TH8_ALLOC(interp, 1);
	if (!pVar->zData) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pVar->zData[0] = 0;
	pVar->nData = 0;
	if (rcSec != TH8_OK) return rcSec;
    }
#  endif

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetVarValue --
 *
 *	Set a variable's value from a Th8_Value with a buffer member.
 *	The variable takes ownership of the buffer pointer (pBuffer)
 *	as its zData -- NO copy is made.  The buffer's nUsed becomes
 *	the variable's nData.
 *
 *	After this call, the Th8_Value's buffer fields are cleared
 *	(pBuffer=NULL, nUsed=0, nCapacity=0) since the variable now
 *	owns the memory.
 *
 *	This is the O(1) path for [append]: the append command
 *	accumulates data in a cache buffer, then transfers ownership
 *	to the variable without copying.
 *
 * Why / How:
 *	Avoids the copy that Th8_SetVar would require by transferring
 *	the cache buffer directly into the variable via the bBorrowed
 *	flag.  The variable's zData points into the cache buffer, so
 *	subsequent [append] calls find the buffer still populated and
 *	can extend it in-place.  This makes repeated [append] O(1)
 *	amortised instead of O(n) per append.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on allocation failure or if
 *	pValue is NULL.
 *
 * Side effects:
 *	The variable's zData and nData are updated.  The Th8_Value's
 *	buffer fields may be cleared for secure variables.
 *
 *----------------------------------------------------------------------
 */

int
th8SetVarValue(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    Th8_Value *pValue)
{
    Th8_Variable *pVar;
    char *pBuf;
    size_t nUsed;

    if (!pValue || !pValue->u.buffer.pBuffer) {
	return TH8_ERROR;
    }

    pVar = th8FindValue(interp, zVar, nVar, 1);
    if (!pVar) {
	return TH8_ERROR;
    }

    /*
     * Reject a scalar write to a bare array name (pHash set), the
     * same scalar-XOR-array guard as Th8_SetVar.  Without it this
     * path would set zData on an array variable.
     */
    if (pVar->pHash) {
	Th8_ErrorMessage(
	    interp, "can't set variable (is array):", zVar, nVar);
	return TH8_ERROR;
    }

    if (nVar == TH8_NOLEN) {
	nVar = Th8_Strlen(interp, zVar);
    }

    pBuf = (char *)pValue->u.buffer.pBuffer;
    nUsed = pValue->u.buffer.nUsed; /* may carry a taint bit */

    pBuf[TH8_LEN(nUsed)] = '\0';

    /* Defense in depth: securely zero sensitive plaintext before releasing
     * the old value's backing memory. */
    if (pVar->zData && TH8_SENSITIVE(pVar->nData)) {
	Th8_SecureZero(
	    interp, pVar->zData,
	    pVar->nAlloc > 0 ? pVar->nAlloc : TH8_LEN(pVar->nData) + 1);
    }
    if (pVar->bBorrowed) {
	/* Return old borrowed buffer to the pool. */
	if (pVar->nAlloc > 0) {
	    th8BufferFree(interp, pVar->zData, pVar->nAlloc);
	}
    } else {
	Th8_Free(interp, pVar->zData);
    }

    /*
     * Borrow: the variable's zData points directly into the
     * cache buffer.  The cache retains ownership.  This means
     * the next [append] call finds the buffer still populated
     * (no re-seed, no copy) and can extend it in-place.
     *
     * The borrowed flag prevents double-free: when the variable
     * is overwritten by [set] or freed by [unset], it skips
     * Th8_Free on zData.
     */
    pVar->zData = pBuf;
    /* Preserve the taint bit; raw length is used for all buffer
     * arithmetic above and below. */
    pVar->nData = TH8_LEN(nUsed) | (nUsed & TH8_TAG_BITS);
    pVar->nAlloc = pValue->u.buffer.nCapacity;
    pVar->bBorrowed = 1;

    /* Variable was created or its value changed: bump nWait + signal. */
    pVar->nWait++;
    th8SignalAllStates(interp);

    /* Buffer stays in cache - NOT cleared. */

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (th8IsSecureVar(interp, zVar, nVar)) {
	int rcSec = th8SecureSetVar(
	    interp, zVar, nVar, pVar->zData, TH8_LEN(pVar->nData));

	/*
	 * Secure variables need their own copy - can't borrow
	 * the cache buffer because it must be zeroed.  Raw length:
	 * nData may carry a taint bit.
	 */
	Th8_SecureZero(interp, pVar->zData, TH8_LEN(pVar->nData) + 1);
	pVar->bBorrowed = 0;
	pVar->nAlloc = 0;
	/* Force the cache to release the buffer too. */
	pValue->u.buffer.pBuffer = 0;
	pValue->u.buffer.nUsed = 0;
	pVar->zData = (char *)TH8_ALLOC(interp, 1);
	if (!pVar->zData) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pVar->zData[0] = 0;
	pVar->nData = 0;
	if (rcSec != TH8_OK) return rcSec;
    }
#  endif

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SaveSystemVar / Th8_RestoreSystemVar --
 *
 *	Save and restore all elements of a system array variable.
 *	Th8_SaveSystemVar snapshots the current values of every
 *	element of the named array into an opaque handle.
 *	Th8_RestoreSystemVar copies the saved values back and
 *	frees the handle.  Each save/restore pair uses its own
 *	handle, so calls stack naturally via C local variables.
 *
 *	Neither function modifies the interpreter result.
 *
 * Why / How:
 *	Th8_SaveSystemVar walks the array's element hash, builds
 *	"arrayName(elemName)" for each element, reads its value
 *	directly via th8FindValue (to avoid clobbering the interp
 *	result), and stores a malloc'd copy in an opaque handle.
 *	Th8_RestoreSystemVar iterates the saved entries and writes
 *	them back via Th8_SetVar.  This save/restore mechanism is
 *	used by script evaluation to snapshot system arrays before
 *	untrusted code runs and restore them afterward.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on allocation failure.
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    char *zName; /* Full qualified name. */
    size_t nName;
    char *zData; /* Saved value (malloc'd copy). */
    size_t nData;
} Th8_SysVarEntry;

typedef struct {
    int nCount;
    Th8_SysVarEntry *aEntry;
} Th8_SysVarState;


/*
 *----------------------------------------------------------------------
 *
 * Th8_SaveSystemVar --
 *
 *	Public API: snapshot every element of a system array
 *	(e.g. `::th8_security`, `::env`) into an opaque
 *	handle returned via `*ppSaved`.  Paired with
 *	`Th8_RestoreSystemVar` (which consumes the handle)
 *	and `Th8_DiscardSystemVar` (which frees it without
 *	restoring).  The intended use pattern is:
 *
 *	    void *pSaved = NULL;
 *	    Th8_SaveSystemVar(interp, "::env", TH8_NOLEN, &pSaved);
 *	    ... mutate the array transiently ...
 *	    Th8_RestoreSystemVar(interp, pSaved);
 *
 *	Steps:
 *	  1. Enumerate the array's current element names via
 *	     `Th8_ListAppendArray` and split them into per-
 *	     element name buffers (`Th8_SplitList`).
 *	  2. For each element, read its current value via
 *	     `Th8_GetArrayElement` and store an owned copy
 *	     into the `Th8_SysVarState`.
 *	  3. Pack the array name + element vector into the
 *	     opaque `Th8_SysVarState` and return it.
 *
 *	An empty array (one that does not yet exist) yields
 *	`*ppSaved = NULL` and `TH8_OK` -- the matching
 *	restore is a no-op in that case.  All allocations are
 *	rolled back on any intermediate failure.
 *
 * Why / How:
 *	Security-sensitive operations often need to mutate a system
 *	array transiently and then put it back exactly as it was;
 *	this captures a deep, owned copy so the restore is independent
 *	of any later change to the live array.  It enumerates the
 *	element names (Th8_ListAppendArray + Th8_SplitList), reads and
 *	copies each element's value, and packs them into an opaque
 *	Th8_SysVarState.  Every partial allocation is rolled back on
 *	failure so the caller never receives a half-built handle.
 *
 * Parameters:
 *	interp  -- live interpreter.
 *	zArr    -- array name (e.g. `"::th8_security"`).
 *	nArr    -- length (or `TH8_NOLEN` to auto-detect).
 *	ppSaved -- output: opaque handle (NULL when the
 *		array was empty), suitable for
 *		`Th8_RestoreSystemVar`.
 *
 * Results:
 *	`TH8_OK` on success; `TH8_ERROR` on allocation
 *	failure or element-read failure (interpreter result:
 *	diagnostic).
 *
 * Side effects:
 *	Allocates a `Th8_SysVarState` plus per-element name
 *	and value copies.  Ownership transfers to the caller
 *	via `*ppSaved`.
 *
 *----------------------------------------------------------------------
 */
int
Th8_SaveSystemVar(
    Th8_Interp *interp,
    const char *zArr, /* Array name (e.g. "::th8_security"). */
    size_t nArr, /* Length (TH8_NOLEN = NUL-term). */
    void **ppSaved) /* OUT: opaque handle. */
{
    Th8_SysVarState *pState;
    char *zNames = NULL;
    size_t nNames = 0;
    char **azElem = NULL;
    size_t *anElem = NULL;
    int nCount = 0;
    int rc, i;
    /* Function-scope so the oom label can free the partial element
     * name for the entry being built when a growth fails. */
    char *zFull = NULL;
    size_t nFull = 0;

    if (!interp) return TH8_ERROR;
    *ppSaved = NULL;
    if (nArr == TH8_NOLEN) nArr = Th8_Strlen(interp, zArr);

    Th8_ListAppendArray(interp, zArr, nArr, &zNames, &nNames);
    if (!zNames) return TH8_OK;

    rc = Th8_SplitList(
        interp, zNames, nNames, &azElem, &anElem, &nCount, TH8_LIST_NONE);
    Th8_Free(interp, zNames);
    /* Split per Finding 005 sec. 5b: SplitList with non-zero
     * input succeeds and produces non-zero nCount in the test
     * corpus; both C-pairs intrinsic-dead. */
    if (rc != TH8_OK) {
	Th8_Free(interp, azElem);
	return rc;
    }
    if (nCount == 0) {
	Th8_Free(interp, azElem);
	return rc;
    }

    pState = (Th8_SysVarState *)TH8_ALLOC(interp, sizeof(Th8_SysVarState));
    if (!pState) {
	Th8_Free(interp, azElem);
	return TH8_ERROR;
    }

    pState->aEntry = (Th8_SysVarEntry *)
        TH8_ALLOC_MUL(interp, (size_t)nCount, sizeof(Th8_SysVarEntry));
    if (!pState->aEntry) {
	Th8_Free(interp, pState);
	Th8_Free(interp, azElem);
	return TH8_ERROR;
    }
    pState->nCount = nCount;

    for (i = 0; i < nCount; i++) {
	Th8_Variable *pVar;

	zFull = NULL;
	nFull = 0;
	TH8_STR_APPEND(interp, &zFull, &nFull, zArr, nArr);
	TH8_STR_APPEND(interp, &zFull, &nFull, "(", 1);
	TH8_STR_APPEND(interp, &zFull, &nFull, azElem[i], anElem[i]);
	TH8_STR_APPEND(interp, &zFull, &nFull, ")", 1);

	pState->aEntry[i].zName = zFull;
	pState->aEntry[i].nName = nFull;
	pState->aEntry[i].zData = NULL;
	pState->aEntry[i].nData = 0;

	/* Direct access -- does not clobber interp result. */
	pVar = th8FindValue(interp, zFull, nFull, 0);
	/* Nested per Finding 005 sec. 5b: pVar==NULL and
	 * zData==NULL are intrinsic-dead in the test corpus
	 * (cache lookup of a known-existent name). */
	if (pVar)
	    if (pVar->zData)
		if (TH8_LEN(pVar->nData) > 0) {
		    size_t nRaw = TH8_LEN(pVar->nData);

		    pState->aEntry[i].zData = (char *)TH8_ALLOC(interp, nRaw);
		    if (pState->aEntry[i].zData) {
			Th8_Memcpy(
			    interp, pState->aEntry[i].zData, pVar->zData,
			    nRaw);
			/* Preserve the taint bit in the snapshot metadata. */
			pState->aEntry[i].nData = pVar->nData;
		    }
		}
    }

    Th8_Free(interp, azElem);
    *ppSaved = pState;
    return TH8_OK;

oom:
    /* A TH8_STR_APPEND growth failed while building element name i;
     * "out of memory" already set.  zFull is the partial (unstored)
     * name for entry i.  Free it, unwind the fully-captured entries
     * (0..i-1) exactly as Th8_RestoreSystemVar would, then release
     * the snapshot and the split list. */
    Th8_Free(interp, zFull);
    {
	int j;

	for (j = 0; j < i; j++) {
	    Th8_Free(interp, pState->aEntry[j].zData);
	    Th8_Free(interp, pState->aEntry[j].zName);
	}
    }
    Th8_Free(interp, pState->aEntry);
    Th8_Free(interp, pState);
    Th8_Free(interp, azElem);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RestoreSystemVar --
 *
 *	Restore all elements of a system array variable from a
 *	previously saved snapshot and free the snapshot handle.
 *
 * Why / How:
 *	Iterates the saved entries, writes each value back via
 *	Th8_SetVar (defaulting to "none" for entries that had no
 *	saved data), then frees all snapshot memory.  Paired with
 *	Th8_SaveSystemVar to bracket untrusted script evaluation.
 *
 * Results:
 *	TH8_OK on success.
 *
 * Side effects:
 *	System array elements are restored to their saved values.
 *	The snapshot handle is freed and must not be reused.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RestoreSystemVar(
    Th8_Interp *interp,
    const char *zArr, /* Array name (unused, for API symmetry). */
    size_t nArr, /* Length (unused). */
    void *pSaved) /* Handle from Th8_SaveSystemVar. */
{
    Th8_SysVarState *pState = (Th8_SysVarState *)pSaved;
    int i;

    if (!interp) return TH8_ERROR;
    (void)zArr;
    (void)nArr;

    if (!pState) return TH8_OK;

    for (i = 0; i < pState->nCount; i++) {
	if (pState->aEntry[i].zData) {
	    /* Th8_SetVar does NOT modify the interp result. */
	    Th8_SetVar(
	        interp, pState->aEntry[i].zName, pState->aEntry[i].nName,
	        pState->aEntry[i].zData, pState->aEntry[i].nData);
	    Th8_Free(interp, pState->aEntry[i].zData);
	} else {
	    Th8_SetVar(
	        interp, pState->aEntry[i].zName, pState->aEntry[i].nName,
	        "none", TH8_NOLEN);
	}
	Th8_Free(interp, pState->aEntry[i].zName);
    }
    Th8_Free(interp, pState->aEntry);
    Th8_Free(interp, pState);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_UnsetVar --
 *
 *	Remove a variable from the current scope.
 *
 * Why / How:
 *	Implements the [unset] command.  For array elements, only the
 *	element is removed from the array hash.  For scalars and whole
 *	arrays, the variable is freed and removed from the frame hash.
 *	Shared variables (nRef > 1 from [upvar]/[global]) are removed
 *	from every frame that references them, matching Tcl semantics.
 *	Any associated append-buffer cache entry is also invalidated.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if not found.
 *
 * Side effects:
 *	Variable is freed.
 *
 *----------------------------------------------------------------------
 */

int
Th8_UnsetVar(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar) /* Length (TH8_NOLEN = NUL-term). */
{
    const char *zOuter, *zInner;
    size_t nOuter, nInner;
    int bGlobal;
    Th8_HashEntry *pEntry;
    Th8_Frame *pFrame;

    if (!interp) return TH8_ERROR;

    TH8_ASSERT_OWNER(interp);

    th8AnalyzeVarName(
        zVar, nVar, &zOuter, &nOuter, &zInner, &nInner, &bGlobal);

    if (bGlobal) {
	pFrame = interp->pFrame;
	while (pFrame->pCaller) {
	    pFrame = pFrame->pCaller;
	}
    } else {
	pFrame = interp->pFrame;
    }

    if (zInner) {
	/*
	 * Unset an array element.
	 */

	Th8_Variable *pArr;

	pEntry = Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, 0);
	if (!pEntry || !pEntry->pData) {
	    Th8_ErrorMessage(interp, "no such variable:", zVar, nVar);
	    return TH8_ERROR;
	}
	pArr = (Th8_Variable *)pEntry->pData;
	if (!pArr->pHash) {
	    Th8_ErrorMessage(interp, "no such variable:", zVar, nVar);
	    return TH8_ERROR;
	}
	pEntry = Th8_HashFind(interp, pArr->pHash, zInner, nInner, 0);
	if (!pEntry) {
	    Th8_ErrorMessage(interp, "no such variable:", zVar, nVar);
	    return TH8_ERROR;
	}
	if (pEntry->pData) {
	    /* Bump the element's nWait + signal BEFORE freeing,
	     * so any [vwait arr(k)] sees the value-change before
	     * the storage goes away (the [vwait] loop re-resolves
	     * by name each iteration so this is safe). */
	    Th8_Variable *pElem = (Th8_Variable *)pEntry->pData;
	    pElem->nWait++;
	    th8SignalAllStates(interp);
	    th8FreeVariable(interp, pElem);
	}
	Th8_HashFind(interp, pArr->pHash, zInner, nInner, -1);
	/*
	 * Element removed: bump parent array's epoch so any
	 * pending [array startsearch] iterations detect the
	 * mutation on their next nextelement/anymore call.
	 */
	pArr->nEpoch++;
    } else {
	/*
	 * Unset a scalar or whole array.
	 */

	Th8_Variable *pVar;

	pEntry = Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, 0);
	if (!pEntry || !pEntry->pData) {
	    Th8_ErrorMessage(interp, "no such variable:", zVar, nVar);
	    return TH8_ERROR;
	}
	pVar = (Th8_Variable *)pEntry->pData;

	/*
	 * If the variable is shared across frames (e.g. via
	 * [global] or [upvar]), remove it from every other
	 * frame that references the same Th8_Variable*.  This
	 * matches standard Tcl behaviour: unsetting through a
	 * link removes the variable from all frames.
	 */

	if (pVar->nRef > 1) {
	    Th8_Frame *p;

	    for (p = interp->pFrame; p; p = p->pCaller) {
		int i;

		if (p == pFrame) continue;
		for (i = 0; i < TH8_HASH_SIZE; i++) {
		    Th8_HashEntry *pe = p->paVar->aBucket[i];

		    while (pe) {
			Th8_HashEntry *peNext = pe->pNext;

			if (pe->pData == (void *)pVar) {
			    th8FreeVariable(interp, pVar);
			    Th8_HashRemove(
			        interp, p->paVar, pe->zKey, pe->nKey);
			}
			pe = peNext;
		    }
		}
	    }
	}
	/*
	 * Remove the buffer cache entry entirely on unset.
	 * The buffer was owned by the cache; freeing it here
	 * prevents stale entries from being reused by a
	 * later [append] to a new variable with the same name.
	 */
	th8RemoveFromCache(interp, TH8_CACHE_BUFFER, zOuter, nOuter);
	/* Bump nWait + signal BEFORE the variable storage goes
	 * away.  Any [vwait] on this variable will re-resolve by
	 * name on its next loop iteration; the bump just ensures
	 * the saved-counter check before re-resolve fires. */
	pVar->nWait++;
	th8SignalAllStates(interp);
	th8FreeVariable(interp, pVar);
	Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, -1);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ExistsVar --
 *
 *	Check whether a variable exists.
 *
 * Why / How:
 *	Implements [info exists].  Uses th8FindValue in read-only mode
 *	(bCreate=0) and then checks for actual content (zData or pHash)
 *	to distinguish an assigned variable from an uninitialized link
 *	created by [upvar] without assignment.
 *
 * Results:
 *	1 if the variable exists, 0 otherwise.
 *
 * Side effects:
 *	None (clears any error from th8FindValue).
 *
 *----------------------------------------------------------------------
 */

int
Th8_ExistsVar(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar) /* Length (TH8_NOLEN = NUL-term). */
{
    Th8_Variable *pVar;

    if (!interp) return 0;

    pVar = th8FindValue(interp, zVar, nVar, 0);
    if (!pVar) {
	Th8_SetResult(interp, 0, 0);
	return 0;
    }

    /*
     * A variable "exists" only if it has been assigned a value
     * (zData != 0) or is an array (pHash != 0).  A linked but
     * unset variable (created by upvar without assignment) does
     * not exist yet.
     */

    return (pVar->zData != 0 || pVar->pHash != 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ExistsArrayVar --
 *
 *	Check whether a variable is an array.
 *
 * Why / How:
 *	Implements [array exists].  Bypasses th8FindValue and looks up
 *	the variable directly in the frame hash to avoid the "variable
 *	is an array" error that th8FindValue would raise for a bare
 *	array name.  Checks the pHash member to distinguish arrays
 *	from scalars.
 *
 * Results:
 *	1 if the variable exists and is an array, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ExistsArrayVar(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar) /* Length (TH8_NOLEN = NUL-term). */
{
    const char *zOuter, *zInner;
    size_t nOuter, nInner;
    int bGlobal;
    Th8_HashEntry *pEntry;
    Th8_Frame *pFrame;

    if (!interp) return 0;
    th8AnalyzeVarName(
        zVar, nVar, &zOuter, &nOuter, &zInner, &nInner, &bGlobal);

    if (bGlobal) {
	pFrame = interp->pFrame;
	while (pFrame->pCaller) {
	    pFrame = pFrame->pCaller;
	}
    } else {
	pFrame = interp->pFrame;
    }

    pEntry = Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, 0);
    if (!pEntry || !pEntry->pData) {
	return 0;
    }
    return ((Th8_Variable *)pEntry->pData)->pHash != 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetArrayEpoch --
 *
 *	Return the per-array mutation counter.
 *
 * Why / How:
 *	The array's epoch is bumped every time an element is
 *	added to or removed from the array's element hash.  The
 *	[array startsearch] commands record this value at search
 *	creation; subsequent nextelement/anymore calls compare
 *	against the current value to detect mid-iteration
 *	mutations.  This avoids snapshotting the element-name
 *	list (which would defeat the purpose of the search API
 *	for very large arrays).
 *
 * Results:
 *	The current epoch value, or -1 if the variable does not
 *	exist as an array (caller must ALSO check existence with
 *	Th8_ExistsArrayVar to distinguish "epoch 0 on a fresh
 *	array" from "no such array").
 *
 * Side effects:
 *	None.  Only reads the variable's stored epoch counter.
 *
 *----------------------------------------------------------------------
 */

int
th8GetArrayEpoch(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar) /* Length (TH8_NOLEN = NUL-term). */
{
    const char *zOuter, *zInner;
    size_t nOuter, nInner;
    int bGlobal;
    Th8_HashEntry *pEntry;
    Th8_Frame *pFrame;
    Th8_Variable *pVar;

    if (!interp) return -1;
    th8AnalyzeVarName(
        zVar, nVar, &zOuter, &nOuter, &zInner, &nInner, &bGlobal);
    if (zInner) return -1; /* Must be bare array name. */

    if (bGlobal) {
	pFrame = interp->pFrame;
	while (pFrame->pCaller) {
	    pFrame = pFrame->pCaller;
	}
    } else {
	pFrame = interp->pFrame;
    }

    pEntry = Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, 0);
    if (!pEntry || !pEntry->pData) return -1;
    pVar = (Th8_Variable *)pEntry->pData;
    if (!pVar->pHash) return -1;
    return pVar->nEpoch;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetArrayGeneration --
 *
 *	Return the per-array element-hash allocation generation
 *	(stamped onto the Th8_Variable when its pHash was last
 *	created).
 *
 * Why / How:
 *	Pairs with the array-search SID validation in
 *	`th8ArraySearchFind`.  Pointer equality on `pArrayHash`
 *	can falsely succeed when the allocator reuses the address
 *	of a freed element-hash; the generation counter is
 *	monotonic per interp, so it never collides after an
 *	`array unset` followed by `array set` of the same name.
 *
 * Results:
 *	The generation value (always positive after the first
 *	pHash allocation), or -1 if the variable does not exist
 *	or has no element hash.
 *
 * Side effects:
 *	None.  Only reads the variable's stored generation counter.
 *
 *----------------------------------------------------------------------
 */

int
th8GetArrayGeneration(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar) /* Length (TH8_NOLEN = NUL-term). */
{
    const char *zOuter, *zInner;
    size_t nOuter, nInner;
    int bGlobal;
    Th8_HashEntry *pEntry;
    Th8_Frame *pFrame;
    Th8_Variable *pVar;

    if (!interp) return -1;
    th8AnalyzeVarName(
        zVar, nVar, &zOuter, &nOuter, &zInner, &nInner, &bGlobal);
    if (zInner) return -1;

    if (bGlobal) {
	pFrame = interp->pFrame;
	while (pFrame->pCaller) {
	    pFrame = pFrame->pCaller;
	}
    } else {
	pFrame = interp->pFrame;
    }

    pEntry = Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, 0);
    if (!pEntry || !pEntry->pData) return -1;
    pVar = (Th8_Variable *)pEntry->pData;
    if (!pVar->pHash) return -1;
    return pVar->nGeneration;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetArrayElementHash --
 *
 *	Return the array's element hash.
 *
 * Why / How:
 *	The [array startsearch] iteration walks the underlying
 *	hash directly (rather than snapshotting the element-name
 *	list).  This accessor exposes the hash without leaking
 *	the Th8_Variable structure to the caller.  Used together
 *	with th8GetArrayEpoch to detect mid-iteration mutations.
 *
 * Results:
 *	The element-hash pointer, or NULL if the variable does
 *	not exist or is not an array.
 *
 * Side effects:
 *	None.  Returns a borrowed pointer to the array's existing
 *	element hash; the caller must not free it.
 *
 *----------------------------------------------------------------------
 */

Th8_Hash *
th8GetArrayElementHash(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar, /* Variable name. */
    size_t nVar) /* Length (TH8_NOLEN = NUL-term). */
{
    const char *zOuter, *zInner;
    size_t nOuter, nInner;
    int bGlobal;
    Th8_HashEntry *pEntry;
    Th8_Frame *pFrame;
    Th8_Variable *pVar;

    if (!interp) return 0;
    th8AnalyzeVarName(
        zVar, nVar, &zOuter, &nOuter, &zInner, &nInner, &bGlobal);
    if (zInner) return 0; /* Must be bare array name. */

    if (bGlobal) {
	pFrame = interp->pFrame;
	while (pFrame->pCaller) {
	    pFrame = pFrame->pCaller;
	}
    } else {
	pFrame = interp->pFrame;
    }

    pEntry = Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, 0);
    if (!pEntry || !pEntry->pData) return 0;
    pVar = (Th8_Variable *)pEntry->pData;
    return pVar->pHash;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_LinkVar --
 *
 *	Create a local variable linked to a variable in another frame.
 *
 * Why / How:
 *	Implements [upvar] and [global].  Locates the target frame by
 *	converting absolute frame numbers (#N) to relative offsets,
 *	resolves the remote variable in that frame (creating it if
 *	needed), then aliases the local name to the same Th8_Variable*
 *	by incrementing its reference count.  This gives both names
 *	the same storage, so writes through either name are visible
 *	from the other.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	Increments the reference count of the remote variable.
 *
 *----------------------------------------------------------------------
 */

int
Th8_LinkVar(
    Th8_Interp *interp, /* Interpreter. */
    const char *zLocal, /* Local variable name. */
    size_t nLocal, /* Local name length. */
    int iFrame, /* Target frame identifier. */
    const char *zRemote, /* Remote variable name. */
    size_t nRemote) /* Remote name length. */
{
    Th8_Variable *pRemote;
    Th8_HashEntry *pEntry;
    Th8_Frame *pTarget;
    int i;

    if (!interp) return TH8_ERROR;

    /*
     * Find the target frame.
     */

    pTarget = interp->pFrame;
    if (iFrame >= 0) {
	/*
	 * Absolute frame number: #0 = global, #1 = first proc, etc.
	 * Convert to a negative relative offset from the current frame.
	 */

	int nFrames = 0;
	Th8_Frame *p;

	for (p = interp->pFrame; p; p = p->pCaller) {
	    nFrames++;
	}
	iFrame = -(nFrames - 1 - iFrame);
	pTarget = interp->pFrame;
    }
    for (i = 0; pTarget && i < (-iFrame); i++) {
	pTarget = pTarget->pCaller;
    }
    if (!pTarget) {
	Th8_ErrorMessage(interp, "no such frame:", zLocal, nLocal);
	return TH8_ERROR;
    }

    /*
     * Find the remote variable.
     */

    {
	Th8_Frame *pSaved = interp->pFrame;
	interp->pFrame = pTarget;
	pRemote = th8FindValue(interp, zRemote, nRemote, 1);
	interp->pFrame = pSaved;
    }
    if (!pRemote) {
	Th8_ErrorMessage(interp, "no such variable:", zRemote, nRemote);
	return TH8_ERROR;
    }

    /*
     * Check that the local does not already exist.
     */

    if (nLocal == TH8_NOLEN) {
	nLocal = Th8_Strlen(interp, zLocal);
    }
    pEntry = Th8_HashFind(interp, interp->pFrame->paVar, zLocal, nLocal, 0);
    if (pEntry && ALWAYS(pEntry->pData)) {
	Th8_ErrorMessage(interp, "variable exists:", zLocal, nLocal);
	return TH8_ERROR;
    }

    /*
     * Create the link.
     */

    pEntry = Th8_HashFind(interp, interp->pFrame->paVar, zLocal, nLocal, 1);
    pRemote->nRef++;
    pEntry->pData = (void *)pRemote;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Forward declaration: th8SubstWord is used below for
 * substituting array index contents.
 */

int th8SubstWord(Th8_Interp *, const char *, size_t, const char *, size_t);

/*
 *----------------------------------------------------------------------
 *
 * th8SubstVarName --
 *
 *	Perform variable substitution on a $... reference.
 *
 * Why / How:
 *	Called by the tokenizer/evaluator when a $-prefixed token is
 *	encountered.  Handles three syntactic forms: ${name} (braced),
 *	$name (simple), and $name(index) (array with possible nested
 *	$-substitution in the index).  For array indices containing
 *	variables, th8SubstWord is called recursively to substitute
 *	the index before the final lookup via Th8_GetVar.
 *
 * Results:
 *	Return code from Th8_GetVar.
 *
 * Side effects:
 *	Sets the interpreter result to the variable's value.
 *
 *----------------------------------------------------------------------
 */

int
th8SubstVarName(
    Th8_Interp *interp, /* Interpreter. */
    const char *zWord, /* Word (starts with '$'). */
    size_t nWord) /* Byte length of variable ref. */
{
    /* Function-scope so the oom label can free the substituted array
     * name built in the array-reference branch below. */
    char *zFull = 0;
    size_t nFull = 0;

    /*
     * ${name} form -- strip braces.
     */

    if (ALWAYS(nWord > 1) && zWord[1] == '{') {
	return Th8_GetVar(interp, &zWord[2], nWord - 3);
    }

    /*
     * $name or $name(index) form.
     *
     * For array subscripts, the index may contain variable
     * references (e.g., $arr($key)) that must be substituted
     * before looking up the variable.  Build the full name
     * with the substituted index.
     */

    {
	size_t i;
	const char *zName = &zWord[1];
	size_t nName = nWord - 1;

	/* Find the opening '(' if present. */
	for (i = 0; i < nName; i++) {
	    if (zName[i] == '(') break;
	}

	/* Array-reference detection.  Nested per Finding 005
	 * sec. 5b: when `i < nName`, nName > 0 is implied
	 * (i is non-negative); dropping that redundant
	 * condition removes one intrinsic-dead C-pair.  The
	 * remaining checks decompose into nested singles. */
	if (i < nName)
	    if (zName[i] == '(')
		if (zName[nName - 1] == ')') {
		    /*
	     * Array reference: zName[0..i-1] is the array name,
	     * zName[i+1..nName-2] is the raw index.
	     * Substitute the index, then build "name(substIndex)".
	     */

		    const char *zIdx = &zName[i + 1];
		    size_t nIdx = nName - i - 2; /* exclude parens */
		    int rc;

		    rc = th8SubstWord(interp, zIdx, nIdx, NULL, 0);
		    if (rc != TH8_OK) return rc;

		    {
			size_t nRes;
			const char *zRes = Th8_GetResult(interp, &nRes);
			zFull = 0;
			nFull = 0;
			TH8_STR_APPEND(interp, &zFull, &nFull, zName, i);
			TH8_STR_APPEND(interp, &zFull, &nFull, "(", 1);
			TH8_STR_APPEND(interp, &zFull, &nFull, zRes, nRes);
			TH8_STR_APPEND(interp, &zFull, &nFull, ")", 1);
			rc = Th8_GetVar(interp, zFull, nFull);
			Th8_Free(interp, zFull);
			return rc;
		    }
		}

	/* Simple variable (no array subscript). */
	return Th8_GetVar(interp, zName, nName);
    }

oom:
    /* A TH8_STR_APPEND growth failed while building the substituted
     * array name; "out of memory" already set. */
    Th8_Free(interp, zFull);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendVariables --
 *
 *	Append all variable names in the current frame to a list.
 *
 * Why / How:
 *	Implements [info vars] for the local scope.  Iterates the
 *	current frame's paVar hash via th8AppendHashKeys to collect
 *	every variable name into the caller's list buffer.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	The list buffer (*pz, *pn) is extended with variable names.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendVariables(
    Th8_Interp *interp, /* Interpreter. */
    char **pz, /* IN/OUT: list buffer. */
    size_t *pn) /* IN/OUT: list length. */
{
    void *aCtx[3];

    if (!interp) return TH8_ERROR;

    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(
        interp, interp->pFrame->paVar, th8AppendHashKeys, (void *)aCtx);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendNsVariables --
 *
 *	Append all variable names in the specified namespace to a list.
 *	If zNs is NULL or empty, uses the current namespace.
 *
 * Why / How:
 *	Supports [namespace eval ... {info vars}] and [info vars] when
 *	a namespace context is active.  Resolves the namespace via
 *	th8FindNamespace, then iterates its paVar hash to collect all
 *	variable names.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	The list buffer (*pz, *pn) is extended with variable names.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendNsVariables(
    Th8_Interp *interp, /* Interpreter. */
    const char *zNs, /* Namespace name (or NULL for current). */
    size_t nNs, /* Length of zNs. */
    char **pz, /* IN/OUT: list buffer. */
    size_t *pn) /* IN/OUT: list length. */
{
    Th8_Namespace *pNs;
    void *aCtx[3];

    if (!interp) return TH8_ERROR;

    if (!zNs || nNs == 0) {
	pNs = interp->pCurrentNs;
    } else {
	pNs = th8FindNamespace(interp, zNs, nNs, 0);
    }
    if (!pNs) return TH8_OK;

    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(interp, pNs->paVar, th8AppendHashKeys, (void *)aCtx);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendGlobalVariables --
 *
 *	Append all variable names in the global frame to a list.
 *
 * Why / How:
 *	Implements [info globals].  Walks the frame chain to the
 *	root (global) frame, then iterates its paVar hash to collect
 *	every variable name.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	The list buffer (*pz, *pn) is extended with variable names.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendGlobalVariables(
    Th8_Interp *interp, /* Interpreter. */
    char **pz, /* IN/OUT: list buffer. */
    size_t *pn) /* IN/OUT: list length. */
{
    Th8_Frame *pGlobal;
    void *aCtx[3];

    if (!interp) return TH8_ERROR;
    pGlobal = interp->pFrame;

    while (pGlobal->pCaller) {
	pGlobal = pGlobal->pCaller;
    }
    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(interp, pGlobal->paVar, th8AppendHashKeys, (void *)aCtx);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8AppendLinkedHashKeys --
 *
 *	Hash iterator callback: append the key to the list only if
 *	the associated Th8_Variable has nRef > 1 (indicating that
 *	it is shared across frames via upvar/global).
 *
 * Why / How:
 *	Used by Th8_ListAppendVarLinks to filter the frame's variables
 *	to only those that are linked.  The nRef > 1 check is the
 *	defining characteristic of a linked variable: exactly one ref
 *	per frame that holds it, so nRef > 1 means at least two
 *	frames share the same Th8_Variable.
 *
 * Results:
 *	TH8_OK always (continue iterating).
 *
 * Side effects:
 *	The list buffer may be extended with the key name.
 *
 *----------------------------------------------------------------------
 */

static int
th8AppendLinkedHashKeys(Th8_HashEntry *pEntry, void *pVoid)
{
    void **aCtx = (void **)pVoid;
    Th8_Interp *interp = (Th8_Interp *)aCtx[0];
    char **pz = (char **)aCtx[1];
    size_t *pn = (size_t *)aCtx[2];
    Th8_Variable *pVar = (Th8_Variable *)pEntry->pData;

    if (pVar && pVar->nRef > 1) {
	Th8_ListAppend(interp, pz, pn, pEntry->zKey, pEntry->nKey);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendVarLinks --
 *
 *	Append the names of all linked variables (upvar/global)
 *	in the current frame to a list.  A variable is considered
 *	linked if its reference count is greater than 1, meaning
 *	it is shared with another frame.
 *
 * Why / How:
 *	Provides introspection for [info varlinks] to show which
 *	local names are aliases into other frames.  Delegates to
 *	th8AppendLinkedHashKeys which filters on nRef > 1.
 *
 * Results:
 *	TH8_OK always.
 *
 * Side effects:
 *	The list buffer (*pz, *pn) is extended with linked variable
 *	names.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendVarLinks(
    Th8_Interp *interp, /* Interpreter. */
    char **pz, /* IN/OUT: list buffer. */
    size_t *pn) /* IN/OUT: list length. */
{
    void *aCtx[3];

    if (!interp) return TH8_ERROR;

    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(
        interp, interp->pFrame->paVar, th8AppendLinkedHashKeys, (void *)aCtx);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendArray --
 *
 *	Append all element names of an array to a list.
 *
 * Why / How:
 *	Implements [array names].  Looks up the array variable
 *	directly in the frame hash (bypassing th8FindValue to avoid
 *	the "variable is an array" error for bare array names),
 *	then iterates the array's element hash to collect all keys.
 *
 * Results:
 *	TH8_OK always.  Returns early (no error) if the variable
 *	does not exist or is not an array.
 *
 * Side effects:
 *	The list buffer (*pz, *pn) is extended with element names.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendArray(
    Th8_Interp *interp, /* Interpreter. */
    const char *zArr, /* Array name. */
    size_t nArr, /* Name length. */
    char **pz, /* IN/OUT: list buffer. */
    size_t *pn) /* IN/OUT: list length. */
{
    Th8_Variable *pVar;
    void *aCtx[3];

    if (!interp) return TH8_ERROR;

    /*
     * Look up the variable directly in the frame hash,
     * bypassing th8FindValue's "variable is an array" error
     * for bare array names.
     */

    {
	const char *zOuter, *zInner;
	size_t nOuter, nInner;
	int bGlobal;
	Th8_HashEntry *pEntry;
	Th8_Frame *pFrame;

	th8AnalyzeVarName(
	    zArr, nArr, &zOuter, &nOuter, &zInner, &nInner, &bGlobal);
	if (bGlobal) {
	    pFrame = interp->pFrame;
	    while (pFrame->pCaller)
		pFrame = pFrame->pCaller;
	} else {
	    pFrame = interp->pFrame;
	}
	pEntry = Th8_HashFind(interp, pFrame->paVar, zOuter, nOuter, 0);
	if (!pEntry || !pEntry->pData) return TH8_OK;
	pVar = (Th8_Variable *)pEntry->pData;
    }
    if (!pVar->pHash) {
	return TH8_OK;
    }
    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(interp, pVar->pHash, th8AppendHashKeys, (void *)aCtx);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ParseVarName --
 *
 *	Parse a variable reference at the start of zString.
 *	The string should begin with '$' (which is consumed).
 *	On success, pToken is filled in as a TH8_TOKEN_VARIABLE:
 *	  pToken->zData points to the '$'
 *	  pToken->nData covers the entire $name or ${name} or
 *	  $name(index) reference.
 *
 * Why / How:
 *	Used by the tokenizer to extract a complete variable reference
 *	from the input stream.  Delegates to th8NextVarName for the
 *	actual scanning of the variable name boundaries, then packages
 *	the result as a Th8_Value token with type TH8_TOKEN_VARIABLE.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if no valid variable follows.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8ParseVarName(
    Th8_Interp *interp, /* Interpreter (for error messages). */
    const char *zString, /* Input (should start with '$'). */
    size_t nString, /* Byte length (TH8_NOLEN = NUL). */
    Th8_Value *pToken) /* OUT: filled as TH8_TOKEN_VARIABLE. */
{
    size_t nVar = 0;
    int rc;

    if (!pToken) return TH8_ERROR;
    if (nString == TH8_NOLEN) {
	size_t k = 0;

	while (zString[k])
	    k++;
	nString = k;
    }

    if (nString == 0 || zString[0] != '$') {
	if (interp) {
	    Th8_SetResult(interp, "expected variable reference", TH8_NOLEN);
	}
	return TH8_ERROR;
    }

    rc = th8NextVarName(interp, zString, nString, &nVar);
    if (rc != TH8_OK) return rc;
    if (nVar <= 1) {
	if (interp) {
	    Th8_SetResult(interp, "invalid variable name", TH8_NOLEN);
	}
	return TH8_ERROR;
    }

    pToken->eType = TH8_TOKEN_VARIABLE;
    pToken->zData = zString;
    pToken->nData = nVar;
    pToken->u.token.nLine = 0;
    pToken->u.token.nCol = 0;
    pToken->u.token.nChild = 0;
    pToken->u.token.aChild = 0;
    return TH8_OK;
}


#endif /* TH8_ENABLE_VARIABLES */
