/*
 * th8_xlib.c -- Extended library functions for TH8.
 *
 * Higher-level convenience functions built on the core public API.
 * These functions use only the public TH8 API (th8.h) and do not
 * access interpreter internals.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_util.h"


/*
 *----------------------------------------------------------------------
 *
 * Th8_EvalFile --
 *
 *	Evaluate the contents of a file identified by name.  This is
 *	the C API equivalent of the [source] command.
 *
 *	1. Retrieves the file contents via the platform's xGetData
 *	   callback (Th8_GetData).
 *	2. Pushes the file name onto the [info script] stack
 *	   (Th8_PushSourceName).
 *	3. Evaluates the script (Th8_Eval at frame 0).
 *	4. Pops the source name (Th8_PopSourceName).
 *	5. Frees the retrieved data (Th8_Free).
 *
 *	Returns TH8_OK on success or TH8_ERROR if the file cannot
 *	be retrieved or the script produces an error.  On error, the
 *	interpreter result contains the error message.
 *
 * Why / How:
 *	Scripts commonly need to source other scripts ([source]),
 *	and the C embedding API needs the same capability.  This
 *	function orchestrates the full file-evaluation lifecycle:
 *	platform-level data retrieval (which also performs signature
 *	verification when signed-only policy is active), [info
 *	script] stack management so nested sources report the correct
 *	file name, global-scope evaluation, and cleanup.  The
 *	::th8_security array is saved and restored around the eval
 *	so that security policy settings from inner scripts (e.g.
 *	prologue.tcl sourced from security.tcl) do not leak into
 *	the outer file's context.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *
 * Side effects:
 *	Executes arbitrary script code.  May modify interpreter
 *	state (variables, commands, result).  Temporarily modifies
 *	the [info script] stack and the ::th8_security array.
 *	Allocates and frees file data via the platform callbacks.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EvalFile(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName,  /* File name (platform-dependent). */
    size_t nName)  /* Byte length, or TH8_NOLEN. */
{
    char *zData = NULL;
    size_t nData = 0;
    int rc;
#if defined(TH8_ENABLE_VARIABLES)
    void *pSecSaved = NULL;
    char *zSavedNotBefore = NULL;
    char *zSavedNotAfter = NULL;
#endif

    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!zName) return TH8_ERROR;

    if (nName == TH8_NOLEN) {
	nName = Th8_Strlen(interp, zName);
    }

    /*
     * Save the ::th8_security array BEFORE GetData so that
     * the saved state captures the OUTER file's security
     * details.  When this EvalFile returns, the restore puts
     * the outer context back -- giving the security array
     * stack-like behavior tied to source depth.
     */

#if defined(TH8_ENABLE_VARIABLES)
    Th8_SaveSystemVar(interp, "::th8_security", TH8_NOLEN, &pSecSaved);
#endif

    /*
     * Step 1: Retrieve the file contents via the platform's
     * xGetData callback.  The TH8_TRANSLATE_EOL flag requests
     * CR/LF to LF translation for cross-platform scripts.
     *
     * This triggers signature verification and annotation
     * extraction (th8PolicyVerifyData) which populates
     * ::th8_security with the verified key details and any
     * notBefore/notAfter annotations from this file.
     */

    rc = Th8_GetData(interp, zName, nName, &zData, &nData, TH8_TRANSLATE_EOL);
    if (rc != TH8_OK) {
#if defined(TH8_ENABLE_VARIABLES)
	Th8_RestoreSystemVar(interp, "::th8_security", TH8_NOLEN, pSecSaved);
#endif
	return rc;
    }

    /*
     * Snapshot the notBefore/notAfter values set by the
     * verification phase.  These are annotation results from
     * THIS file's script text.  The restore below will revert
     * to the outer state; we re-apply these afterward so the
     * caller can read the sourced file's annotations.
     */

#if defined(TH8_ENABLE_VARIABLES)
    if (Th8_GetVar(interp, "::th8_security(notBefore)", TH8_NOLEN) ==
        TH8_OK) {
	const char *z;
	size_t n;

	z = Th8_GetResult(interp, &n);
	/* Nest the NULL guard and length check so each `if`
	 * has a single condition; the innermost "not the
	 * sentinel string 'none'" check is also kept as a
	 * 2-condition compound to stay below the clang
	 * MC/DC truth-table cap.  See FINDINGS.md Finding 005. */
	if (z) {
	    if (n > 0) {
		/* Decompose "not the sentinel 'none'" check.  Per
		 * Finding 005 sec. 5b: the inner Memcmp can only be
		 * 0 when n == 4 (substring lengths must match), so
		 * the (n != 4, Memcmp == 0) C-pair is intrinsic-
		 * dead.  Match flag explicitly. */
		int isNone = 0;
		if (n == 4)
		    if (Th8_Memcmp(interp, z, "none", 4) == 0) isNone = 1;
		if (!isNone) {
		    zSavedNotBefore = (char *)TH8_ALLOC_STR(interp, n);
		    if (zSavedNotBefore) {
			Th8_Memcpy(interp, zSavedNotBefore, z, n);
			zSavedNotBefore[n] = '\0';
		    }
		}
	    }
	}
    }
    if (Th8_GetVar(interp, "::th8_security(notAfter)", TH8_NOLEN) == TH8_OK) {
	const char *z;
	size_t n;

	z = Th8_GetResult(interp, &n);
	if (z) {
	    if (n > 0) {
		/* Decompose "not the sentinel 'none'" check.  Per
		 * Finding 005 sec. 5b: the inner Memcmp can only be
		 * 0 when n == 4 (substring lengths must match), so
		 * the (n != 4, Memcmp == 0) C-pair is intrinsic-
		 * dead.  Match flag explicitly. */
		int isNone = 0;
		if (n == 4)
		    if (Th8_Memcmp(interp, z, "none", 4) == 0) isNone = 1;
		if (!isNone) {
		    zSavedNotAfter = (char *)TH8_ALLOC_STR(interp, n);
		    if (zSavedNotAfter) {
			Th8_Memcpy(interp, zSavedNotAfter, z, n);
			zSavedNotAfter[n] = '\0';
		    }
		}
	    }
	}
    }
#endif

    /*
     * Step 2: Push the file name for [info script].
     */

    Th8_PushSourceName(interp, zName, nName);

    /*
     * Step 3: Evaluate the script at frame 0 (global scope).
     */

    rc = Th8_Eval(interp, 0, zData, nData, zName, nName);

    /*
     * Step 4: Pop the source name.
     */

    Th8_PopSourceName(interp);

    /*
     * Step 5: Free the file data.
     */

    Th8_Free(interp, zData);

    /*
     * Step 6: Restore the outer security state.
     * Th8_RestoreSystemVar uses Th8_SetVar internally, which
     * does NOT modify the interpreter result, so the eval
     * result (or error message) is preserved.
     */

#if defined(TH8_ENABLE_VARIABLES)
    Th8_RestoreSystemVar(interp, "::th8_security", TH8_NOLEN, pSecSaved);

    /*
     * Re-apply the annotation values from this file's
     * verification.  The restore above reverted to the outer
     * context (correct for dataName, algorithmName, etc.), but
     * notBefore/notAfter are one-shot results that the caller
     * expects to see after [source] returns.
     */

    if (zSavedNotBefore) {
	Th8_SetVar(
	    interp, "::th8_security(notBefore)", TH8_NOLEN, zSavedNotBefore,
	    TH8_NOLEN);
	Th8_Free(interp, zSavedNotBefore);
    }
    if (zSavedNotAfter) {
	Th8_SetVar(
	    interp, "::th8_security(notAfter)", TH8_NOLEN, zSavedNotAfter,
	    TH8_NOLEN);
	Th8_Free(interp, zSavedNotAfter);
    }
#endif

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EvalFileAsData --
 *
 *	Load binary data by evaluating a signed script file in an
 *	isolated child interpreter.  The script's result is treated
 *	as a base64-encoded block and decoded to raw bytes.
 *
 *	This function creates a temporary child interpreter that
 *	inherits the parent's platform callbacks (and signed-only
 *	policy if active).  The named file is evaluated in the child
 *	via Th8_EvalFile.  On success, the child's result is base64-
 *	decoded and returned via pzData/pnData.  The child is always
 *	deleted, whether the operation succeeds or fails.
 *
 *	Primary use case: loading additional RSA public keys (as
 *	base64-encoded SNK blobs) into the signed-script policy
 *	context, where the key-delivery script itself must pass
 *	signature verification.
 *
 *	Steps:
 *	  1. Create a child interpreter using the parent's platform.
 *	  2. Register language commands in the child.
 *	  3. If the parent has signed-only policy enabled, enable
 *	     it in the child as well.
 *	  4. Evaluate the named file in the child (Th8_EvalFile).
 *	  5. Base64-decode the child's result.
 *	  6. Delete the child interpreter.
 *	  7. Return the decoded data to the caller.
 *
 *	The caller must free *pzData with Th8_Free(interp, ...).
 *	If any step fails, *pzData is set to NULL, *pnData to 0,
 *	the child is deleted, and TH8_ERROR is returned.  The
 *	parent's interpreter result is set to an error message.
 *
 * Why / How:
 *	The signed-script policy creates a bootstrapping problem:
 *	additional RSA public keys must be loaded to verify scripts,
 *	but the key-delivery mechanism itself must be tamper-proof.
 *	This function solves it by evaluating the key-delivery script
 *	in a disposable child interpreter that enforces the same
 *	signed-only policy as the parent.  The child inherits a clone
 *	of the parent's platform callbacks (so getData performs
 *	signature verification) and the full TH8 language.  The
 *	child's result is treated as base64 and decoded to raw bytes,
 *	which the caller can then load as an RSA key.  Isolation in a
 *	child interpreter prevents the key-delivery script from
 *	modifying the parent's state.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *
 * Side effects:
 *	Allocates memory for the decoded data.  Creates and
 *	destroys a temporary child interpreter.  Clones and frees
 *	a platform structure.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EvalFileAsData(
    Th8_Interp *interp,  /* Parent interpreter. */
    const char *zName,   /* File name to evaluate. */
    size_t nName,   /* Byte length, or TH8_NOLEN. */
    const unsigned char **pzData, /* OUT: decoded data. */
    size_t *pnData,   /* OUT: data length in bytes. */
    void *pCtx)    /* Reserved (must be NULL). */
{
    Th8_Interp *pChild = NULL;
    const Th8_Platform *pParentPlat;
    Th8_Platform *pChildPlat = NULL;
    int bParentSigned;
    const char *zResult;
    size_t nResult;
    unsigned char *zDecoded = NULL;
    size_t nDecoded = 0;
    int rc;
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    void *pPolicyCtx = NULL;
#endif

    (void)pCtx;

    if (!interp || !zName || !pzData || !pnData) {
	if (interp) {
	    Th8_SetResult(
	        interp, "Th8_EvalFileAsData: invalid arguments", TH8_NOLEN);
	}
	return TH8_ERROR;
    }

    *pzData = NULL;
    *pnData = 0;

    if (nName == TH8_NOLEN) {
	nName = Th8_Strlen(interp, zName);
    }

    /*
     * Step 1: Create a child interpreter using a clone of the
     * parent's platform.  The clone is owned by the child and
     * freed when the child is deleted.
     */

    pParentPlat = Th8_GetPlatform(interp);
    pChildPlat = Th8_ClonePlatform(pParentPlat);
    if (!pChildPlat) {
	Th8_SetResult(
	    interp, "Th8_EvalFileAsData: cannot clone platform", TH8_NOLEN);
	return TH8_ERROR;
    }

    pChild = Th8_CreateInterp(pChildPlat);
    if (!pChild) {
	Th8_FreePlatform(pChildPlat);
	Th8_SetResult(
	    interp, "Th8_EvalFileAsData: cannot create child interp",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Step 2: Register language commands in the child so that
     * the evaluated script has access to the full TH8 language.
     */

    Th8_RegisterLanguage(pChild);

    /*
     * Step 3: If the parent has signed-only policy enabled,
     * enable it in the child as well.
     */

    bParentSigned = Th8_IsSignedOnlyEnabled(interp);

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (bParentSigned) {
	rc = Th8_EnableSignedPolicy(pChild, &pPolicyCtx, 1);
	if (rc != TH8_OK) {
	    Th8_SetResult(
	        interp,
	        "Th8_EvalFileAsData: cannot enable "
	        "signed-only policy in child",
	        TH8_NOLEN);
	    goto cleanup;
	}
    }
#else
    (void)bParentSigned;
#endif

    /*
     * Step 4: Evaluate the named file in the child interpreter.
     * This goes through the full Th8_EvalFile path, including
     * getData signature verification if signed-only is active.
     */

    rc = Th8_EvalFile(pChild, zName, nName);
    if (rc != TH8_OK) {
	/*
	 * Copy the child's error message to the parent.
	 */

	zResult = Th8_GetResult(pChild, &nResult);
	Th8_SetResult(interp, zResult, nResult);
	goto cleanup;
    }

    /*
     * Step 5: Base64-decode the child's result.
     */

    zResult = Th8_GetResult(pChild, &nResult);
    /* Bug 26: Th8_GetResult is documented to return the empty-
     * string sentinel rather than NULL, but a future change or
     * teardown race could in theory yield NULL.  Use plain `if`
     * so the guard survives TH8_OMIT.  Split per Finding 005
     * sec. 5b: C1 (!zResult) intrinsic-dead in test corpus. */
    {
	int emptyResult = 0;

	if (!zResult)
	    emptyResult = 1;
	else if (nResult == 0)
	    emptyResult = 1;
	if (emptyResult) {
	    Th8_SetResult(
	        interp, "Th8_EvalFileAsData: script produced empty result",
	        TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto cleanup;
	}
    }

    rc = th8Base64Decode(interp, zResult, nResult, &zDecoded, &nDecoded);
    if (rc != TH8_OK) {
	Th8_SetResult(
	    interp, "Th8_EvalFileAsData: base64 decode failed", TH8_NOLEN);
	goto cleanup;
    }

    *pzData = zDecoded;
    *pnData = nDecoded;
    zDecoded = NULL;  /* Ownership transferred to caller. */

cleanup:
    /*
     * Step 6: Delete the child interpreter (always).
     */

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /* Nested per Finding 005 sec. 5b: C2 (pPolicyCtx NULL with
     * bParentSigned true) intrinsic-dead -- bParentSigned is
     * only set when the signed-policy install set pPolicyCtx. */
    if (bParentSigned)
	if (pPolicyCtx) {
	    Th8_EnableSignedPolicy(pChild, &pPolicyCtx, 0);
	}
#endif

    Th8_DeleteInterp(pChild);
    Th8_FreePlatform(pChildPlat);
    Th8_Free(interp, zDecoded);

    return rc;
}
