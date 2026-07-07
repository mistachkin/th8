/*
 * th8_procedures.c -- Procedures plugin for TH8.
 *
 * Implements the procedure commands: apply, downlevel, napply, nproc,
 * proc, tailcall.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8ProceduresGetCommands.
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

#if defined(TH8_PLUGIN_PROCEDURES)

/*
 * Forward declarations.
 */

static Th8_ProcDefn *th8CopyProcDefn(Th8_Interp *, const Th8_ProcDefn *);
static int proc_ns_restore(Th8_Interp *, void *[], int);
/* th8EvalCleanup: defined in th8_control.c, declared in th8_int.h */
static void proc_del(Th8_Interp *, void *);

/*
 *----------------------------------------------------------------------
 *
 * proc_ns_restore --
 *
 *	NRE callback that restores the namespace context after
 *	a proc body finishes executing.
 *
 * Why / How:
 *	When a proc is defined in a namespace other than the one it is
 *	called from, proc_call_nr switches pCurrentNs to the defining
 *	namespace before evaluating the body.  This callback restores
 *	the original namespace afterwards, ensuring the switch is
 *	transparent to the caller.  Pushed via Th8_NRAddCallback so it
 *	runs after the body regardless of the return code.
 *
 * Results:
 *	The incoming return code, passed through unchanged.
 *
 * Side effects:
 *	Restores the interpreter's current namespace pointer.
 *
 *----------------------------------------------------------------------
 */

static int
proc_ns_restore(
    Th8_Interp *interp, /* Interpreter. */
    void *pData[],  /* [0]=saved namespace pointer. */
    int rc)   /* Return code (passed through). */
{
    th8SetCurrentNsPtr(interp, pData[0]);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * proc_call_nr --
 *
 *	NRE callback that binds positional parameters in the new
 *	call frame, then NREvals the proc body.
 *
 *	This callback runs inside a frame pushed by th8NRInFrame
 *	(called from th8ProcCall1).  The frame lifecycle -- including
 *	popping the frame and mapping TH8_RETURN to TH8_OK -- is
 *	handled automatically by the frame-cleanup callback that
 *	th8NRInFrame installs.
 *
 *	Parameter binding algorithm (positional):
 *	  1. Validate argument count against nParam and hasArgs.
 *	  2. For each formal parameter i (0..nParam-1):
 *	     - If argv[i+1] exists, bind it.
 *	     - Else use azDefault[i] (must be non-NULL or error).
 *	  3. If hasArgs, collect all remaining arguments into a
 *	     list and bind it as "args".
 *	  4. NREval the proc body.
 *
 * Why / How:
 *	Proc parameter binding runs as an NRE callback so the C stack
 *	stays flat across arbitrarily deep call chains.  This callback
 *	is pushed by th8NRInFrame inside a freshly-created call frame;
 *	frame lifecycle (pop + TH8_RETURN mapping) is handled by
 *	th8NRInFrame's own cleanup callback.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on argument mismatch or missing
 *	default.
 *
 * Side effects:
 *	Binds local variables in the current frame.  Sets the frame's
 *	objv for [info level].  May switch the current namespace.
 *	Initiates NRE evaluation of the proc body.
 *
 *	pData layout:
 *	  [0] = Th8_ProcDefn * -- the proc definition
 *	  [1] = argv (const char **) -- call arguments
 *	  [2] = argl (size_t *) -- argument lengths
 *	  [3] = argc (as th8_int64_t)
 *
 *----------------------------------------------------------------------
 */

static int
proc_call_nr(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_ProcDefn *p = (Th8_ProcDefn *)pData[0];
    const char **argv = (const char **)pData[1];
    size_t *argl = (size_t *)pData[2];
    int argc = (int)(size_t)pData[3];
#  if defined(TH8_ENABLE_VARIABLES)
    int i;
#  endif

    (void)rc;

#  if !defined(TH8_ENABLE_VARIABLES)
    if (p->nParam > 0) {
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    /*
     * Store the invocation on this frame for [info level].
     */

    th8SetFrameObjv(interp, argc, argv, argl);

    /*
     * Check argument count.
     */

    if ((argc > (p->nParam + 1) && !p->hasArgs) ||
        (argc <= p->nParam && !p->azDefault[argc - 1])) {
	char *zUsage = 0;
	size_t nUsage = 0;

	Th8_StringAppend(interp, &zUsage, &nUsage, argv[0], argl[0]);
	Th8_StringAppend(interp, &zUsage, &nUsage, p->zUsage, p->nUsage);
	Th8_StringAppend(interp, &zUsage, &nUsage, "", 1);
	Th8_WrongNumArgs(interp, zUsage);
	Th8_Free(interp, zUsage);
	return TH8_ERROR;
    }

    /*
     * Bind formal parameters.
     */

#  if defined(TH8_ENABLE_VARIABLES)
    for (i = 0; i < p->nParam; i++) {
	const char *zVal;
	size_t nVal;

	if (argc > (i + 1)) {
	    zVal = argv[i + 1];
	    nVal = argl[i + 1];
	} else {
	    zVal = p->azDefault[i];
	    nVal = p->anDefault[i];
	}
	Th8_SetVar(interp, p->azParam[i], p->anParam[i], zVal, nVal);
    }

    /*
     * Bind "args" if present.
     */

    if (p->hasArgs) {
	char *zArgs = 0;
	size_t nArgs = 0;

	for (i = p->nParam + 1; i < argc; i++) {
	    Th8_ListAppend(interp, &zArgs, &nArgs, argv[i], argl[i]);
	}
	Th8_SetVar(interp, "args", TH8_NOLEN, zArgs, nArgs);
	Th8_Free(interp, zArgs);
    }
#  endif

    /*
     * Switch to the proc's defining namespace so that
     * [variable] and [namespace current] see the correct
     * namespace -- even for imported procs.  Push an NRE
     * cleanup callback to restore the namespace after
     * the body evaluation.
     */

    if (p->pDefNs) {
	void *pSaved = th8GetCurrentNsPtr(interp);

	if (p->pDefNs != pSaved) {
	    Th8_NRAddCallback(interp, proc_ns_restore, pSaved, 0, 0, 0);
	    th8SetCurrentNsPtr(interp, p->pDefNs);
	}
    }
    Th8_SetResult(interp, 0, 0);
    return Th8_NREval(interp, p->zProgram, p->nProgram, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProcCall1 --
 *
 *	Command dispatch function for procs.  This is the xProc
 *	registered by proc_command via Th8_CreateCommand.
 *
 *	It delegates to th8NRInFrame which:
 *	  1. Pushes a new call frame.
 *	  2. Pushes a frame-cleanup callback (handles frame pop
 *	     and TH8_RETURN -> TH8_OK mapping).
 *	  3. Pushes proc_call_nr (the parameter-binding callback).
 *	  4. Returns to the trampoline, which runs proc_call_nr.
 *
 * Why / How:
 *	This is the xProc callback stored in the command table entry for
 *	every [proc]-defined command.  It delegates entirely to the NRE
 *	infrastructure via th8NRInFrame, passing the ProcDefn as context
 *	and the call arguments as pData slots.
 *
 * Results:
 *	Return code from th8NRInFrame / proc_call_nr.
 *
 * Side effects:
 *	Pushes a new call frame and initiates parameter binding.
 *
 *----------------------------------------------------------------------
 */

int
th8ProcCall1(
    Th8_Interp *interp,
    void *pContext,
    int argc,
    const char **argv,
    size_t *argl)
{
    return th8NRInFrame(
        interp, proc_call_nr, pContext, (void *)argv, (void *)argl,
        TH8_INT2PTR(argc));
}


/*
 *----------------------------------------------------------------------
 *
 * proc_del --
 *
 *	Deletion callback for proc and nproc commands.  Frees the
 *	separately-allocated usage string, then frees the ProcDefn
 *	struct (which includes all embedded arrays in a single
 *	allocation).  Registered as the xDelete callback via
 *	Th8_CreateCommand.
 *
 * Why / How:
 *	Each ProcDefn is a two-part allocation: the main struct (a
 *	single block with embedded arrays) and the usage string
 *	(separate allocation).  Both must be freed when the command
 *	is deleted or overwritten.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the ProcDefn and its usage string.
 *
 *----------------------------------------------------------------------
 */

static void
proc_del(Th8_Interp *interp, void *pContext)
{
    Th8_ProcDefn *p = (Th8_ProcDefn *)pContext;

    Th8_Free(interp, p->zUsage);
    Th8_Free(interp, p);
}


/*
 *----------------------------------------------------------------------
 *
 * th8ProcCopy --
 *
 *	xCopy callback for safe namespace import.  Returns a deep
 *	copy of the ProcDefn via th8CopyProcDefn.
 *
 * Why / How:
 *	When a proc is imported into another namespace, the command
 *	entry needs its own independent copy of the ProcDefn so that
 *	deleting the original does not leave a dangling pointer.
 *	Registered via Th8_SetCommandCopy.
 *
 * Results:
 *	Pointer to the newly allocated ProcDefn copy.
 *
 * Side effects:
 *	Allocates a new ProcDefn and usage string.
 *
 *----------------------------------------------------------------------
 */

void *
th8ProcCopy(Th8_Interp *interp, void *pContext)
{
    return (void *)th8CopyProcDefn(interp, (const Th8_ProcDefn *)pContext);
}


/*
 *----------------------------------------------------------------------
 *
 * th8CopyProcDefn --
 *
 *	Deep-copy a ProcDefn for safe namespace import.
 *
 *	The ProcDefn is a single allocation with embedded pointer
 *	arrays and string data.  This function copies the entire
 *	block, then adjusts all internal pointers to reference the
 *	new copy.  The zUsage string is copied separately.
 *
 *	The returned ProcDefn is fully independent of the original
 *	and can be freed via proc_del without affecting the source.
 *
 * Why / How:
 *	The ProcDefn uses a flat allocation layout: all pointer arrays
 *	and string data are packed into a single malloc block after the
 *	struct header.  Copying requires a memcpy of the entire block
 *	followed by a pointer fixup pass that adds the delta between
 *	old and new base addresses to every internal pointer.
 *
 * Results:
 *	Pointer to the new ProcDefn, or NULL on allocation failure.
 *
 * Side effects:
 *	Allocates a new block for the ProcDefn and a separate
 *	allocation for the usage string.
 *
 *----------------------------------------------------------------------
 */

static Th8_ProcDefn *
th8CopyProcDefn(Th8_Interp *interp, const Th8_ProcDefn *pSrc)
{
    Th8_ProcDefn *pDst;
    ptrdiff_t delta;

    /* Bug 26: framework should pass valid pSrc per its contract,
     * but a framework bug or future code change could break that.
     * Use plain `if` so the guard survives TH8_OMIT.  Split per
     * Finding 005 sec. 5b: both C-pairs intrinsic-dead because
     * the framework contract holds in the test corpus. */
    if (!pSrc) return 0;
    if (pSrc->nAllocSize == 0) return 0;

    pDst = (Th8_ProcDefn *)TH8_ALLOC(interp, pSrc->nAllocSize);
    if (!pDst) return 0;

    /*
     * Copy the entire block, then fixup pointers.
     */

    Th8_Memcpy(interp, pDst, pSrc, pSrc->nAllocSize);
    delta = (char *)pDst - (char *)pSrc;

    /*
     * Adjust internal pointers.  Each pointer (azParam, anParam,
     * azDefault, anDefault, zProgram) points into the trailing
     * data of the same allocation, so adding delta relocates them.
     * azParam[i] and azDefault[i] also point into the block.
     */

    if (pDst->azParam) {
	int i;

	pDst->azParam = (char **)((char *)pDst->azParam + delta);
	pDst->anParam = (size_t *)((char *)pDst->anParam + delta);
	pDst->azDefault = (char **)((char *)pDst->azDefault + delta);
	pDst->anDefault = (size_t *)((char *)pDst->anDefault + delta);
	for (i = 0; i < pDst->nParam; i++) {
	    if (pDst->azParam[i]) {
		pDst->azParam[i] += delta;
	    }
	    if (pDst->azDefault[i]) {
		pDst->azDefault[i] += delta;
	    }
	}
    }
    if (pDst->zProgram) {
	pDst->zProgram += delta;
    }

    /*
     * Copy the usage string (separate allocation).
     */

    if (pSrc->zUsage && ALWAYS(pSrc->nUsage > 0)) {
	pDst->zUsage = (char *)TH8_ALLOC_STR(interp, pSrc->nUsage);
	if (pDst->zUsage) {
	    Th8_Memcpy(interp, pDst->zUsage, pSrc->zUsage, pSrc->nUsage);
	    pDst->zUsage[pSrc->nUsage] = 0;
	}
    } else {
	pDst->zUsage = 0;
	pDst->nUsage = 0;
    }

    return pDst;
}


/*
 *----------------------------------------------------------------------
 *
 * proc_command --
 *
 *	Implements the Tcl [proc] command.  Defines a new procedure
 *	with positional parameter binding.
 *
 *	proc NAME ARGLIST BODY
 *
 * Why / How:
 *	Parses ARGLIST into a single-allocation Th8_ProcDefn struct
 *	containing embedded parameter arrays, default values, and the
 *	body script.  Supports the special "args" last parameter for
 *	variadic procedures.  Registers the command via
 *	Th8_CreateCommand with th8ProcCall1 as xProc and proc_del as
 *	xDelete.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on argument or parse errors.
 *
 * Side effects:
 *	Creates a new command in the interpreter.  Allocates a
 *	ProcDefn and a usage string.
 *
 *----------------------------------------------------------------------
 */

static int
proc_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_ProcDefn *p;
    size_t nByte;
    int i;
    char *zSpace;
    char **azParam;
    size_t *anParam;
    int nParam;
    char *zUsage = 0;
    size_t nUsage = 0;
    int rc;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "proc name arglist code");
    }
    rc = Th8_SplitList(
        interp, argv[2], argl[2], &azParam, &anParam, &nParam, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    /*
     * Allocate ProcDefn with room for param arrays and body.
     */

    {
	/*
	 * Compute the total allocation size with overflow-safe
	 * arithmetic.  The procedure definition needs space for:
	 *   - the Th8_ProcDefn header
	 *   - two parallel arrays of (char*, size_t) pairs of length
	 *     nParam each (current params + their original spellings)
	 *   - the body and arglist source strings
	 * Any of the multiplies or adds could overflow on a script
	 * that synthesizes a huge nParam; one failure path covers all.
	 */
	size_t nPairBytes;
	size_t nParamArrays;
	/* Sequential overflow checks via an else-if ladder.
	 * Each leg is a single-condition decision so clang's
	 * MC/DC instrumenter does not have to encode the entire
	 * 5-way `||` compound (which exceeds the truth-table
	 * cap).  See FINDINGS.md Finding 005. */
	int tooLarge = 0;

	if (TH8_SAFE_MUL_SIZE(
	        sizeof(char *) + sizeof(size_t), (size_t)nParam,
	        &nPairBytes)) {
	    tooLarge = 1;
	} else if (TH8_SAFE_ADD_SIZE(nPairBytes, nPairBytes, &nParamArrays)) {
	    tooLarge = 1;
	} else if (
	    TH8_SAFE_ADD_SIZE(nParamArrays, sizeof(Th8_ProcDefn), &nByte)) {
	    tooLarge = 1;
	} else if (TH8_SAFE_ADD_SIZE(nByte, TH8_LEN(argl[3]), &nByte)) {
	    tooLarge = 1;
	} else if (TH8_SAFE_ADD_SIZE(nByte, TH8_LEN(argl[2]), &nByte)) {
	    tooLarge = 1;
	}
	if (tooLarge) {
	    Th8_Free(interp, azParam);
	    Th8_SetResultStatic(interp, "procedure too large", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }
    p = (Th8_ProcDefn *)TH8_ALLOC(interp, nByte);
    if (!p) {
	Th8_Free(interp, azParam);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    p->nAllocSize = nByte;

    /*
     * Check for "args" as last parameter.
     */

    if (nParam > 0 && ALWAYS(azParam)) {
	if (TH8_LEN(anParam[nParam - 1]) == 4 &&
	    0 == Th8_Memcmp(interp, azParam[nParam - 1], "args", 4)) {
	    p->hasArgs = 1;
	    nParam--;
	}
    }

    p->nParam = nParam;
    p->azParam = (char **)&p[1];
    p->anParam = (size_t *)&p->azParam[nParam];
    p->azDefault = (char **)&p->anParam[nParam];
    p->anDefault = (size_t *)&p->azDefault[nParam];
    p->zProgram = (char *)&p->anDefault[nParam];
    if (argv[3]) {
	Th8_Memcpy(interp, p->zProgram, argv[3], TH8_LEN(argl[3]));
    }
    p->nProgram = TH8_LEN(argl[3]);
    zSpace = &p->zProgram[p->nProgram];

    /*
     * Parse parameter list: each is either "name" or
     * {name default}.
     */

    for (i = 0; i < nParam; i++) {
	char **az = 0;
	size_t *an = 0;
	int n = 0;

	if (azParam) {
	    rc = Th8_SplitList(
	        interp, azParam[i], anParam[i], &az, &an, &n, TH8_LIST_NONE);
	}
	if (rc != TH8_OK || n < 1 || n > 2) {
	    if (rc == TH8_OK) {
		Th8_ErrorMessage(
		    interp, "expected parameter, got \"",
		    azParam ? azParam[i] : "", azParam ? anParam[i] : 0);
	    }
	    Th8_Free(interp, az);
	    goto error_out;
	}

	if (az) {
	    p->anParam[i] = TH8_LEN(an[0]);
	    p->azParam[i] = zSpace;
	    Th8_Memcpy(interp, zSpace, az[0], TH8_LEN(an[0]));
	    zSpace += TH8_LEN(an[0]);
	}
	if (n == 2 && ALWAYS(az)) {
	    p->anDefault[i] = TH8_LEN(an[1]);
	    p->azDefault[i] = zSpace;
	    Th8_Memcpy(interp, zSpace, az[1], TH8_LEN(an[1]));
	    zSpace += TH8_LEN(an[1]);
	}

	/*
	 * Build usage message.
	 */

	Th8_StringAppend(interp, &zUsage, &nUsage, " ", 1);
	if (n == 2) {
	    Th8_StringAppend(interp, &zUsage, &nUsage, "?", 1);
	    if (az) {
		Th8_StringAppend(interp, &zUsage, &nUsage, az[0], an[0]);
	    }
	    Th8_StringAppend(interp, &zUsage, &nUsage, "?", 1);
	} else {
	    if (az) {
		Th8_StringAppend(interp, &zUsage, &nUsage, az[0], an[0]);
	    }
	}

	Th8_Free(interp, az);
    }
    if (p->hasArgs) {
	Th8_StringAppend(interp, &zUsage, &nUsage, " ?args...?", TH8_NOLEN);
    }
    p->zUsage = zUsage;
    p->nUsage = nUsage;
    p->pDefNs = th8GetCurrentNsPtr(interp);

    Th8_CreateCommand(
        interp, argv[1], th8ProcCall1, (void *)p, proc_del, NULL);
    Th8_SetCommandCopy(interp, argv[1], th8ProcCopy);
    Th8_Free(interp, azParam);
    Th8_SetResult(interp, 0, 0);
    return TH8_OK;

error_out:
    Th8_Free(interp, azParam);
    Th8_Free(interp, zUsage);
    Th8_Free(interp, p);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * apply_command --
 *
 *	Implements the Tcl [apply] command.  Applies an anonymous
 *	function (lambda) to arguments.
 *
 *	apply {arglist body ?namespace?} ?arg ...?
 *
 *	The lambda is a two- or three-element list:
 *	{arglist body} or {arglist body namespace}.
 *	The arglist follows the same rules as [proc].
 *	When a namespace is specified, the body is evaluated
 *	in that namespace context (resolved relative to the
 *	global namespace, matching Tcl 8.5+ semantics).
 *
 *	Implementation: builds a temporary Th8_ProcDefn from the
 *	lambda's arglist (same memory layout as proc_command),
 *	pushes an th8EvalCleanup callback to free it after
 *	evaluation, then uses th8NRInFrame + proc_call_nr
 *	to bind parameters and NREval the body in a new frame.
 *
 *	The argv/argl pointers passed to proc_call_nr are shifted
 *	by 1 (&argv[1], &argl[1]) so that argv[0] inside the
 *	proc frame is the lambda itself (for error messages).
 *
 * Why / How:
 *	Reuses the proc infrastructure (ProcDefn + proc_call_nr) for
 *	anonymous functions.  The ProcDefn is ephemeral: th8EvalCleanup
 *	frees it after the body completes, so no command is registered
 *	in the interpreter.  This avoids code duplication while keeping
 *	lambda evaluation NRE-safe.
 *
 * Results:
 *	Return code from the lambda body evaluation.
 *
 * Side effects:
 *	Allocates and frees a temporary ProcDefn.  Pushes a call frame
 *	and binds parameters.
 *
 *----------------------------------------------------------------------
 */

static int
apply_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azLambda = 0;
    size_t *anLambda = 0;
    int nLambda;
    int rc;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "apply {arglist body ?namespace?} ?arg ...?");
    }

    /*
     * Split the lambda into {arglist body ?namespace?}.
     */

    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azLambda, &anLambda, &nLambda,
        TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;
    /* Bug 26: Th8_SplitList could return OK with NULL azLambda
     * on an edge case (empty list) -- use plain `if`.  Split
     * per Finding 005 sec. 5b: !azLambda C-pair is intrinsic-
     * dead in the test corpus; the C1/C2 nLambda checks are
     * the live arms. */
    {
	int badShape = 0;

	if (nLambda != 2 && nLambda != 3) {
	    badShape = 1;
	} else if (!azLambda) {
	    badShape = 1;
	}
	if (badShape) {
	    Th8_Free(interp, azLambda);
	    Th8_SetResultStatic(
	        interp,
	        "can't interpret lambda as"
	        " {arglist body ?namespace?}",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /*
     * Build a script that defines a temporary proc,
     * calls it with the arguments, and cleans up.
     *
     * Simpler and safer approach: use th8InFrame to push
     * a new scope, bind the lambda's arglist, and evaluate
     * the body -- exactly what proc_call_nr does.
     */

    {
	Th8_ProcDefn *p;
	size_t nByte;
	int nParam;
	char **azParam = 0;
	size_t *anParam = 0;
	int i;

	/*
	 * Parse the arglist from the lambda.
	 */

	rc = Th8_SplitList(
	    interp, azLambda[0], anLambda[0], &azParam, &anParam, &nParam,
	    TH8_LIST_NONE);
	if (rc != TH8_OK) {
	    Th8_Free(interp, azLambda);
	    return rc;
	}

	/*
	 * Build a minimal ProcDefn.
	 */

	nByte = sizeof(Th8_ProcDefn) +
	        (sizeof(char *) + sizeof(size_t)) * 2 * (size_t)nParam +
	        TH8_LEN(anLambda[1]) + TH8_LEN(anLambda[0]);
	p = (Th8_ProcDefn *)TH8_ALLOC(interp, nByte);
	if (!p) {
	    Th8_Free(interp, azParam);
	    Th8_Free(interp, azLambda);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	p->nAllocSize = nByte;
	p->nParam = nParam;
	p->azParam = (char **)&p[1];
	p->anParam = (size_t *)&p->azParam[nParam];
	p->azDefault = (char **)&p->anParam[nParam];
	p->anDefault = (size_t *)&p->azDefault[nParam];
	p->zProgram = (char *)&p->anDefault[nParam];
	Th8_Memcpy(interp, p->zProgram, azLambda[1], TH8_LEN(anLambda[1]));
	p->nProgram = TH8_LEN(anLambda[1]);

	/*
	 * Check for "args" as last parameter.
	 */

	if (nParam > 0 && ALWAYS(azParam) &&
	    TH8_LEN(anParam[nParam - 1]) == 4 &&
	    0 == Th8_Memcmp(interp, azParam[nParam - 1], "args", 4)) {
	    p->hasArgs = 1;
	    p->nParam = --nParam;
	}

	/*
	 * Copy parameter names (simplified -- no defaults).
	 */

	{
	    char *zSpace = &p->zProgram[p->nProgram];

	    for (i = 0; i < nParam && ALWAYS(azParam); i++) {
		size_t len = TH8_LEN(anParam[i]);

		p->azParam[i] = zSpace;
		p->anParam[i] = len;
		Th8_Memcpy(interp, zSpace, azParam[i], len);
		zSpace += len;
	    }
	}
	p->zUsage = 0;
	p->nUsage = 0;

	/*
	 * If a namespace was specified in the lambda, resolve
	 * it and store as the defining namespace.  Per Tcl 8.5+,
	 * the namespace is interpreted relative to the global
	 * namespace even if it doesn't start with "::".
	 */
	if (nLambda == 3 && anLambda[2] > 0) {
	    char *zNs = 0;
	    size_t nNs = 0;

	    if (azLambda[2][0] != ':' || azLambda[2][1] != ':') {
		Th8_StringAppend(interp, &zNs, &nNs, "::", 2);
	    }
	    Th8_StringAppend(interp, &zNs, &nNs, azLambda[2], anLambda[2]);
	    /*
	     * Per Tcl, the lambda's namespace must already exist;
	     * resolve find-only (bCreate=0).  Auto-creating it
	     * (the old bCreate=1) left behind undeletable spurious
	     * namespaces such as ":::name" for single-colon inputs
	     * -- Bug 6 / Bug 15.  Match Tcl's error on absence:
	     *   namespace "<fqn>" not found
	     */
	    p->pDefNs = (void *)th8FindNamespace(interp, zNs, nNs, 0);
	    if (!p->pDefNs) {
		char *zErr = 0;
		size_t nErr = 0;

		Th8_StringAppend(
		    interp, &zErr, &nErr, "namespace \"", TH8_NOLEN);
		Th8_StringAppend(interp, &zErr, &nErr, zNs, nNs);
		Th8_StringAppend(
		    interp, &zErr, &nErr, "\" not found", TH8_NOLEN);
		Th8_SetResult(interp, zErr, nErr);
		Th8_Free(interp, zErr);
		Th8_Free(interp, zNs);
		Th8_Free(interp, p);
		Th8_Free(interp, azParam);
		Th8_Free(interp, azLambda);
		return TH8_ERROR;
	    }
	    Th8_Free(interp, zNs);
	} else {
	    p->pDefNs = 0;
	}
	Th8_Free(interp, azParam);
	Th8_Free(interp, azLambda);

	/*
	 * Use NRE: push ProcDefn cleanup, then NRInFrame
	 * with proc_call_nr.  argv[1] is the lambda; the
	 * call arguments start at argv[1] (shifted by 1).
	 */

	Th8_NRAddCallback(interp, th8EvalCleanup, (void *)p, 0, 0, 0);
	return th8NRInFrame(
	    interp, proc_call_nr, (void *)p, (void *)&argv[1],
	    (void *)&argl[1], TH8_INT2PTR(argc - 1));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * nproc_call_nr --
 *
 *	NRE callback: bind named and positional parameters in the
 *	new frame, then NREval the nproc body.
 *
 *	This implements a two-pass named/positional parameter binding
 *	algorithm:
 *
 *	Pass 1 -- Named argument scan:
 *	  Iterate over call arguments.  For each argument starting
 *	  with '-', search the formal parameter list for a parameter
 *	  whose name matches (including the leading '-').  If found,
 *	  consume the next argument as the value and mark the
 *	  parameter as bound in the aBound[] tracking array.
 *	  Named parameters are bound with the leading '-' stripped
 *	  from the variable name (e.g., parameter "-flag" becomes
 *	  variable "flag" in the proc frame).  If no named parameter
 *	  matches, the argument falls through to pass 2 as a
 *	  positional argument.
 *
 *	Pass 2 -- Positional fill:
 *	  Iterate over unbound parameters.  For each one that is
 *	  not a named parameter (no leading '-'):
 *	    - Find the next non-named call argument (skip '-name
 *	      value' pairs already consumed in pass 1).
 *	    - Bind it, or use the default value, or error.
 *	  For unbound named parameters, use their default values.
 *
 *	pData layout:
 *	  [0] = Th8_ProcDefn * -- the nproc definition
 *	  [1] = argv (const char **) -- call arguments
 *	  [2] = argl (size_t *) -- argument lengths
 *	  [3] = argc (as th8_int64_t)
 *
 *	NOTE: The aBound array is heap-allocated and freed before
 *	NREval of the body.  It does not persist across callbacks.
 *
 * Why / How:
 *	Eagle-style named parameters require a two-pass algorithm:
 *	first scan for explicit -name/value pairs, then fill remaining
 *	positional slots.  The aBound tracking array ensures each
 *	parameter is bound exactly once.  This callback mirrors
 *	proc_call_nr but replaces the positional-only binding with
 *	the named/positional hybrid.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on unknown argument name or
 *	missing required parameter.
 *
 * Side effects:
 *	Binds local variables in the current frame.  Sets the frame's
 *	objv for [info level].  Allocates and frees the aBound array.
 *	Initiates NRE evaluation of the nproc body.
 *
 *----------------------------------------------------------------------
 */

static int
nproc_call_nr(Th8_Interp *interp, void *pData[], int rc)
{
#  if defined(TH8_ENABLE_VARIABLES)
    int i, j;
#  endif
    Th8_ProcDefn *p = (Th8_ProcDefn *)pData[0];
    const char **argv = (const char **)pData[1];
    size_t *argl = (size_t *)pData[2];
    int argc = (int)(size_t)pData[3];
#  if defined(TH8_ENABLE_VARIABLES)
    int *aBound;  /* Track which params are bound */
#  endif

    (void)rc;

#  if !defined(TH8_ENABLE_VARIABLES)
    if (p->nParam > 0) {
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    /*
     * Store the invocation on this frame for [info level].
     */

    th8SetFrameObjv(interp, argc, argv, argl);

#  if defined(TH8_ENABLE_VARIABLES)
    {
	size_t nAlloc = (size_t)(p->nParam + 1);
	if (nAlloc > (size_t)-1 / sizeof(int)) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	aBound = (int *)TH8_ALLOC_MUL(interp, sizeof(int), nAlloc);
    }
    if (!aBound) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    /*
     * Eagle-style named binding: ALL call arguments are
     * name-value pairs.  Arguments starting after argv[0]
     * are consumed as pairs: argname argvalue.  The argname
     * is matched against the formal parameter list.
     *
     * Variable names in the proc frame keep their original
     * parameter name (including any leading '-' for named
     * parameters).
     */

#  if defined(TH8_ENABLE_VARIABLES)
    for (i = 1; i + 1 < argc; i += 2) {
	const char *zArgName = argv[i];
	size_t nArgName = TH8_LEN(argl[i]);
	int found = 0;

	for (j = 0; j < p->nParam; j++) {
	    if (aBound[j]) continue;
	    if (TH8_LEN(p->anParam[j]) == nArgName &&
	        0 == Th8_Memcmp(interp, p->azParam[j], zArgName, nArgName)) {
		Th8_SetVar(
		    interp, p->azParam[j], p->anParam[j], argv[i + 1],
		    argl[i + 1]);
		aBound[j] = 1;
		found = 1;
		break;
	    }
	}
	if (!found) {
	    char *zMsg = 0;
	    size_t nMsg = 0;
	    const char *zCmd;
	    size_t nCmd;

	    /*
	     * Extract the simple command name from argv[0]
	     * (strip any namespace qualifiers and whitespace).
	     */

	    /*
	     * Use the command name as invoked, stripping any leading
	     * whitespace and namespace prefix.  The parser-fed argv[0]
	     * never carries leading whitespace at runtime.
	     *
	     * Bug 26 (2026-06-07): plain while rather than NEVER --
	     * collapsing the loop body under TH8_OMIT would silently
	     * skip the strip if any code path ever produced a name
	     * with leading whitespace. */
	    zCmd = argv[0];
	    nCmd = TH8_LEN(argl[0]);
	    /* Loop body never iterates in the test corpus -- per
	     * the Bug 26 comment above, argv[0] never carries
	     * leading whitespace at runtime.  Nested inner check
	     * per Finding 005 sec. 5b. */
	    while (nCmd > 0) {
		if (*zCmd != ' ') break;
		zCmd++;
		nCmd--;
	    }
	    /* Nested per Finding 005 sec. 5b: C3 (zCmd[1] check)
	     * is intrinsic-dead in the test corpus because any
	     * normalized name with ':' at position 0 starts with
	     * the full "::" namespace prefix. */
	    if (nCmd >= 2) {
		if (zCmd[0] == ':') {
		    if (zCmd[1] == ':') {
			zCmd += 2;
			nCmd -= 2;
		    }
		}
	    }
	    /* Use C string length to exclude embedded NULs. */
	    nCmd = Th8_Strlen(interp, zCmd);
	    Th8_StringAppend(interp, &zMsg, &nMsg, "procedure \"", 11);
	    Th8_StringAppend(interp, &zMsg, &nMsg, zCmd, nCmd);
	    Th8_StringAppend(
	        interp, &zMsg, &nMsg, "\" unsupported argument named \"", 30);
	    Th8_StringAppend(interp, &zMsg, &nMsg, zArgName, nArgName);
	    Th8_StringAppend(interp, &zMsg, &nMsg, "\"", 1);
	    Th8_SetResult(interp, zMsg, nMsg);
	    Th8_Free(interp, zMsg);
	    Th8_Free(interp, aBound);
	    return TH8_ERROR;
	}
    }

    /*
     * Fill unbound parameters with their default values.
     */

    for (j = 0; j < p->nParam; j++) {
	if (aBound[j]) continue;
	if (p->azDefault[j]) {
	    Th8_SetVar(
	        interp, p->azParam[j], p->anParam[j], p->azDefault[j],
	        p->anDefault[j]);
	} else {
	    Th8_ErrorMessage(
	        interp, "missing required argument \"", p->azParam[j],
	        p->anParam[j]);
	    Th8_Free(interp, aBound);
	    return TH8_ERROR;
	}
    }

    Th8_Free(interp, aBound);
#  endif

    Th8_SetResult(interp, 0, 0);
    return Th8_NREval(interp, p->zProgram, p->nProgram, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8NprocCall1 --
 *
 *	Command dispatch function for nprocs.  Identical to th8ProcCall1
 *	but uses nproc_call_nr for the named parameter binding
 *	algorithm.  Registered as the xProc via Th8_CreateCommand
 *	by nproc_command.
 *
 * Why / How:
 *	Provides the xProc entry point for [nproc]-defined commands.
 *	The only difference from th8ProcCall1 is that it passes
 *	nproc_call_nr to th8NRInFrame, routing parameter binding
 *	through the two-pass named/positional algorithm.
 *
 * Results:
 *	Return code from th8NRInFrame / nproc_call_nr.
 *
 * Side effects:
 *	Pushes a new call frame and initiates parameter binding.
 *
 *----------------------------------------------------------------------
 */

int
th8NprocCall1(
    Th8_Interp *interp,
    void *pContext,
    int argc,
    const char **argv,
    size_t *argl)
{
    return th8NRInFrame(
        interp, nproc_call_nr, pContext, (void *)argv, (void *)argl,
        TH8_INT2PTR(argc));
}


/*
 *----------------------------------------------------------------------
 *
 * nproc_command --
 *
 *	Implements the Eagle-style [nproc] command.  Defines a
 *	procedure with named/keyword arguments.
 *
 *	nproc NAME ARGLIST BODY
 *
 *	ARGLIST supports:
 *	  name         - positional required
 *	  {name def}   - positional optional
 *	  {-name def}  - named/keyword parameter
 *	  args         - variadic (same as proc)
 *
 * Why / How:
 *	Uses the same ProcDefn memory layout as proc_command but
 *	registers th8NprocCall1 (which invokes nproc_call_nr) instead
 *	of th8ProcCall1.  The named parameter binding logic lives
 *	entirely in nproc_call_nr; the definition-time code here is
 *	identical to proc_command except for the dispatch function.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on argument or parse errors.
 *
 * Side effects:
 *	Creates a new command in the interpreter.  Allocates a
 *	ProcDefn.
 *
 *----------------------------------------------------------------------
 */

static int
nproc_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    /*
     * nproc uses the same ProcDefn as proc, but registers
     * th8NprocCall1 instead of th8ProcCall1.  The argument
     * parsing in nproc_call_nr handles named params.
     */

    Th8_ProcDefn *p;
    size_t nByte;
    int i;
    char *zSpace;
    char **azParam = 0;
    size_t *anParam = 0;
    int nParam;
    int rc;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "nproc name arglist body");
    }
    rc = Th8_SplitList(
        interp, argv[2], argl[2], &azParam, &anParam, &nParam, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    nByte = sizeof(Th8_ProcDefn) +
            (sizeof(char *) + sizeof(size_t)) * 2 * (size_t)nParam +
            TH8_LEN(argl[3]) + TH8_LEN(argl[2]);
    p = (Th8_ProcDefn *)TH8_ALLOC(interp, nByte);
    if (!p) {
	Th8_Free(interp, azParam);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    p->nAllocSize = nByte;

    /*
     * Check for "args".
     */

    if (nParam > 0 && ALWAYS(azParam) && TH8_LEN(anParam[nParam - 1]) == 4 &&
        0 == Th8_Memcmp(interp, azParam[nParam - 1], "args", 4)) {
	p->hasArgs = 1;
	nParam--;
    }

    p->nParam = nParam;
    p->azParam = (char **)&p[1];
    p->anParam = (size_t *)&p->azParam[nParam];
    p->azDefault = (char **)&p->anParam[nParam];
    p->anDefault = (size_t *)&p->azDefault[nParam];
    p->zProgram = (char *)&p->anDefault[nParam];
    Th8_Memcpy(interp, p->zProgram, argv[3], TH8_LEN(argl[3]));
    p->nProgram = TH8_LEN(argl[3]);
    zSpace = &p->zProgram[p->nProgram];

    for (i = 0; i < nParam && ALWAYS(azParam); i++) {
	char **az = 0;
	size_t *an = 0;
	int n = 0;

	rc = Th8_SplitList(
	    interp, azParam[i], anParam[i], &az, &an, &n, TH8_LIST_NONE);
	if (rc != TH8_OK || n < 1 || n > 2) {
	    if (rc == TH8_OK) {
		Th8_ErrorMessage(
		    interp, "expected parameter, got \"", azParam[i],
		    anParam[i]);
	    }
	    Th8_Free(interp, az);
	    Th8_Free(interp, azParam);
	    Th8_Free(interp, p);
	    return TH8_ERROR;
	}

	p->anParam[i] = TH8_LEN(an[0]);
	p->azParam[i] = zSpace;
	Th8_Memcpy(interp, zSpace, az[0], TH8_LEN(an[0]));
	zSpace += TH8_LEN(an[0]);

	if (n == 2) {
	    p->anDefault[i] = TH8_LEN(an[1]);
	    p->azDefault[i] = zSpace;
	    Th8_Memcpy(interp, zSpace, az[1], TH8_LEN(an[1]));
	    zSpace += TH8_LEN(an[1]);
	}

	Th8_Free(interp, az);
    }

    p->zUsage = 0;
    p->nUsage = 0;
    p->pDefNs = th8GetCurrentNsPtr(interp);

    Th8_CreateCommand(
        interp, argv[1], th8NprocCall1, (void *)p, proc_del, NULL);
    Th8_SetCommandCopy(interp, argv[1], th8ProcCopy);
    Th8_Free(interp, azParam);
    Th8_SetResult(interp, 0, 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * napply_command --
 *
 *	Implements the Eagle-style [napply] command.  Applies a
 *	lambda with named argument binding (nproc style).
 *
 *	napply {arglist body} ?arg ...?
 *
 *	Same as apply_command but uses nproc_call_nr instead of
 *	proc_call_nr for the two-pass named/positional parameter
 *	binding algorithm.  See nproc_call_nr for details.
 *
 * Why / How:
 *	Combines the anonymous-function pattern of apply_command with
 *	the named-parameter binding of nproc_call_nr.  The ProcDefn
 *	is ephemeral, freed by th8EvalCleanup after the body runs.
 *
 * Results:
 *	Return code from the lambda body evaluation.
 *
 * Side effects:
 *	Allocates and frees a temporary ProcDefn.  Pushes a call frame
 *	and binds parameters using Eagle-style named binding.
 *
 *----------------------------------------------------------------------
 */

static int
napply_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azLambda = 0;
    size_t *anLambda = 0;
    int nLambda;
    int rc;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "napply {arglist body} ?arg ...?");
    }
    rc = Th8_SplitList(
        interp, argv[1], argl[1], &azLambda, &anLambda, &nLambda,
        TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;
    /* Bug 26: Th8_SplitList could return OK with NULL azLambda
     * on an edge case (empty list) -- use plain `if`.  Split
     * per Finding 005 sec. 5b: !azLambda intrinsic-dead in
     * the test corpus. */
    {
	int badShape = 0;

	if (nLambda != 2) {
	    badShape = 1;
	} else if (!azLambda) {
	    badShape = 1;
	}
	if (badShape) {
	    Th8_Free(interp, azLambda);
	    Th8_SetResultStatic(
	        interp,
	        "can't interpret lambda as"
	        " {arglist body}",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    {
	Th8_ProcDefn *p;
	size_t nByte;
	int nParam;
	char **azParam = 0;
	size_t *anParam = 0;
	int i;

	rc = Th8_SplitList(
	    interp, azLambda[0], anLambda[0], &azParam, &anParam, &nParam,
	    TH8_LIST_NONE);
	if (rc != TH8_OK) {
	    Th8_Free(interp, azLambda);
	    return rc;
	}

	nByte = sizeof(Th8_ProcDefn) +
	        (sizeof(char *) + sizeof(size_t)) * 2 * (size_t)nParam +
	        TH8_LEN(anLambda[1]) + TH8_LEN(anLambda[0]);
	p = (Th8_ProcDefn *)TH8_ALLOC(interp, nByte);
	if (!p) {
	    Th8_Free(interp, azParam);
	    Th8_Free(interp, azLambda);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	p->nAllocSize = nByte;
	p->nParam = nParam;
	p->azParam = (char **)&p[1];
	p->anParam = (size_t *)&p->azParam[nParam];
	p->azDefault = (char **)&p->anParam[nParam];
	p->anDefault = (size_t *)&p->azDefault[nParam];
	p->zProgram = (char *)&p->anDefault[nParam];
	Th8_Memcpy(interp, p->zProgram, azLambda[1], TH8_LEN(anLambda[1]));
	p->nProgram = TH8_LEN(anLambda[1]);

	if (nParam > 0 && ALWAYS(azParam) &&
	    TH8_LEN(anParam[nParam - 1]) == 4 &&
	    0 == Th8_Memcmp(interp, azParam[nParam - 1], "args", 4)) {
	    p->hasArgs = 1;
	    p->nParam = --nParam;
	}

	{
	    char *zSpace = &p->zProgram[p->nProgram];

	    for (i = 0; i < nParam && ALWAYS(azParam); i++) {
		size_t len = TH8_LEN(anParam[i]);

		p->azParam[i] = zSpace;
		p->anParam[i] = len;
		Th8_Memcpy(interp, zSpace, azParam[i], len);
		zSpace += len;
	    }
	}
	p->zUsage = 0;
	p->nUsage = 0;
	Th8_Free(interp, azParam);
	Th8_Free(interp, azLambda);

	Th8_NRAddCallback(interp, th8EvalCleanup, (void *)p, 0, 0, 0);
	return th8NRInFrame(
	    interp, nproc_call_nr, (void *)p, (void *)&argv[1],
	    (void *)&argl[1], TH8_INT2PTR(argc - 1));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * downlevel_command --
 *
 *	Implements the Eagle-style [downlevel] command.  Evaluates a
 *	script in the call frame that was active just prior to the
 *	most recent [uplevel].  Per Eagle semantics, [downlevel]
 *	undoes the scope change of [uplevel].
 *
 *	downlevel SCRIPT
 *
 *	It is an error to call [downlevel] when no [uplevel] is
 *	currently active.
 *
 * Why / How:
 *	Delegates to Th8_EvalDownlevel which maintains a saved-frame
 *	stack.  Each [uplevel] pushes the pre-switch frame; [downlevel]
 *	pops it and evaluates the script there.  This provides
 *	symmetric scope navigation that Eagle scripts rely on.
 *
 * Results:
 *	Return code from the script evaluation.
 *
 * Side effects:
 *	Temporarily changes the evaluation frame.
 *
 *----------------------------------------------------------------------
 */

static int
downlevel_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "downlevel script");
    }
    return Th8_EvalDownlevel(interp, argv[1], argl[1], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * tailcall_command --
 *
 *	Implements the Tcl [tailcall] command.  Replaces the current
 *	procedure invocation with a call to another command.  The
 *	current frame is released before the new command executes.
 *
 *	tailcall COMMAND ?ARG ...?
 *
 *	In the current non-NRE implementation, tailcall evaluates
 *	the command in the caller's frame (via uplevel).  Full NRE
 *	tail-call elimination comes in Phase 7.
 *
 * Why / How:
 *	Builds the command as a properly-quoted list, evaluates it in
 *	the caller's frame via Th8_Eval with frame offset -1, then
 *	converts TH8_OK to TH8_RETURN so the proc wrapper knows the
 *	current procedure has completed.  This approximates true
 *	tail-call semantics without requiring NRE frame manipulation.
 *
 * Results:
 *	TH8_RETURN on success (signals proc completion); propagates
 *	errors.
 *
 * Side effects:
 *	Evaluates a command in the caller's frame.
 *
 *----------------------------------------------------------------------
 */

static int
tailcall_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zCmd = 0;
    size_t nCmd = 0;
    int i;
    int rc;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "tailcall command ?arg ...?");
    }

    /*
     * Build the command as a list (properly quoted).
     */

    for (i = 1; i < argc; i++) {
	Th8_ListAppend(interp, &zCmd, &nCmd, argv[i], argl[i]);
    }

    /*
     * Evaluate in the caller's frame (-1).
     * This provides the correct semantics: the current
     * proc's frame is abandoned and the command runs
     * in the caller's context.
     */

    rc = Th8_Eval(interp, -1, zCmd, nCmd, NULL, 0);
    Th8_Free(interp, zCmd);

    /*
     * Convert RETURN to OK (the proc is finished) and
     * propagate the return code as TH8_RETURN2 so the
     * th8ProcCall1 wrapper converts it properly.
     */

    if (rc == TH8_OK) {
	rc = TH8_RETURN;
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8ProceduresCommands[] = {
    {1, 0, "apply", apply_command},   {1, 0, "downlevel", downlevel_command},
    {1, 0, "napply", napply_command}, {1, 0, "nproc", nproc_command},
    {1, 0, "proc", proc_command},     {1, 0, "tailcall", tailcall_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8ProceduresGetCommands --
 *
 *	Return the command table for the procedures plugin.
 *
 * Why / How:
 *	Called by Th8_RegisterPlugin during interpreter initialization.
 *	Uses the standard two-call protocol: first call with pCommand
 *	NULL to query the count, second call to copy the entries.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if pnCommand is NULL or the
 *	output array is too small.
 *
 * Side effects:
 *	Copies command entries into the caller-provided array.
 *
 *----------------------------------------------------------------------
 */

int
th8ProceduresGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8ProceduresCommands) /
                  sizeof(th8ProceduresCommands[0]));

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
	    pCommand[i] = th8ProceduresCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_PROCEDURES */
