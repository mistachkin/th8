/*
 * th8_tcl.c --
 *
 *	Bidirectional bridge between TH8 and native Tcl interpreters.
 *
 *	This shared library provides cross-interpreter evaluation in
 *	both directions:
 *
 *	  TH8_TCL_BRIDGE_TCL mode (loaded into tclsh):
 *	    Adds the [th8Eval] command, which creates a TH8 sub-
 *	    interpreter and evaluates scripts in it.
 *
 *	  TH8_TCL_BRIDGE_TH8 mode (loaded into th8sh):
 *	    Adds the [tclEval] command, which creates a Tcl sub-
 *	    interpreter and evaluates scripts in it.
 *
 *	Entry points:
 *	  TH8_TCL_BRIDGE_TCL:
 *	    Tclth8bridge_Init   (Tcl_Interp*)
 *	    Tclth8bridge_Unload (Tcl_Interp*, int)
 *	    Loaded via: load <path> Tclth8bridge
 *
 *	  TH8_TCL_BRIDGE_TH8:
 *	    Th8bridge_Init   (Th8_Interp*)
 *	    Th8bridge_Unload (Th8_Interp*, int)
 *	    Loaded via: load <path>:Th8bridge
 *
 *	The sub-interpreter is created during Init and destroyed
 *	during Unload.  It persists across calls, so variables and
 *	state accumulate.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

/*
 *======================================================================
 *
 * TH8-IN-TCL MODE (TH8_TCL_BRIDGE_TCL)
 *
 *	Loaded into a native Tcl interpreter.  Creates a TH8 sub-
 *	interpreter and provides [th8Eval] to evaluate scripts in it.
 *
 *	Links against: libth8.a (full TH8) + libtclstub.a (Tcl stubs).
 *
 *======================================================================
 */

#ifdef TH8_TCL_BRIDGE_TCL

#  ifndef USE_TCL_STUBS
#    define USE_TCL_STUBS
#  endif

#  include "th8_meta_defs.h"
#  include "th8_meta_libc.h"
#  include "th8.h"
#  include "tcl.h"
#  include "th8_tcl.h"

/*
 * Per-interpreter bridge state.  Stored as the ClientData of the
 * [th8Eval] command and freed by the command delete proc.
 */

typedef struct Th8TclBridgeState {
    Th8_Interp *th8Interp; /* The TH8 sub-interpreter. */
    Th8_Platform *pPlatform; /* Platform (owned, merged). */
} Th8TclBridgeState;


/*
 *----------------------------------------------------------------------
 *
 * th8Eval_objCmd --
 *
 *	Implementation of [th8Eval script].
 *	Evaluates the script in the TH8 sub-interpreter and returns
 *	the result.  The return code is translated directly (the
 *	standard codes 0-4 are identical in Tcl and TH8).
 *
 *----------------------------------------------------------------------
 */

static int
th8Eval_objCmd(
    ClientData clientData, /* Th8TclBridgeState*. */
    Tcl_Interp *tclInterp, /* Host Tcl interpreter. */
    int objc,   /* Number of arguments. */
    Tcl_Obj *const objv[]) /* Argument objects. */
{
    Th8TclBridgeState *pState;
    const char *zScript;
    int nScript;
    int rc;
    size_t nResult;
    const char *zResult;

    pState = (Th8TclBridgeState *)clientData;

    if (objc != 2) {
	Tcl_WrongNumArgs(tclInterp, 1, objv, "script");
	return TCL_ERROR;
    }

    zScript = Tcl_GetStringFromObj(objv[1], &nScript);
    rc = Th8_Eval(pState->th8Interp, 0, zScript, (size_t)nScript, NULL, 0);
    zResult = Th8_GetResult(pState->th8Interp, &nResult);
    Tcl_SetObjResult(tclInterp, Tcl_NewStringObj(zResult, (int)nResult));

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Eval_deleteProc --
 *
 *	Command delete handler.  Destroys the TH8 sub-interpreter
 *	when the [th8Eval] command is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
th8Eval_deleteProc(ClientData clientData) /* Th8TclBridgeState*. */
{
    Th8TclBridgeState *pState;

    pState = (Th8TclBridgeState *)clientData;
    if (pState) {
	if (pState->th8Interp) {
	    Th8_DeleteInterp(pState->th8Interp);
	}
	/*
	 * pPlatform is allocated by Tcl_Alloc (ckalloc) since
	 * we can't use Th8_Malloc before the interp exists.
	 */

	if (pState->pPlatform) {
	    ckfree((char *)pState->pPlatform);
	}
	ckfree((char *)pState);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Tclth8bridge_Init --
 *
 *	Tcl package initialization.  Creates the TH8 sub-interpreter
 *	and registers the [th8Eval] command.
 *
 *	Loaded via: load <path> Tclth8bridge
 *
 *----------------------------------------------------------------------
 */

BRIDGE_EXPORT int
Tclth8bridge_Init(Tcl_Interp *tclInterp) /* Host Tcl interpreter. */
{
    Th8TclBridgeState *pState;
    Th8_Platform *pPlat;

    if (Tcl_InitStubs(tclInterp, "8.4", 0) == NULL) {
	return TCL_ERROR;
    }

    /*
     * Build a TH8 platform by merging libc + posix/win32.
     */

    pPlat = (Th8_Platform *)ckalloc(sizeof(Th8_Platform));
    memset(pPlat, 0, sizeof(Th8_Platform));
    {
	const Th8_Platform *pLibc = Th8_GetLibcPlatform();

	if (pLibc) {
	    Th8_MergePlatform(pPlat, pLibc);
	}
    }
#  if !defined(_WIN32) && !defined(WIN32)
    {
	const Th8_Platform *pPosix = Th8_GetPosixPlatform();

	if (pPosix) {
	    Th8_MergePlatform(pPlat, pPosix);
	}
    }
#  else
    {
	const Th8_Platform *pWin32 = Th8_GetWin32Platform();

	if (pWin32) {
	    Th8_MergePlatform(pPlat, pWin32);
	}
    }
#  endif

    /*
     * Initialize TH8 library and create the sub-interpreter.
     */

    Th8_Initialize(pPlat);

    pState = (Th8TclBridgeState *)ckalloc(sizeof(Th8TclBridgeState));
    memset(pState, 0, sizeof(Th8TclBridgeState));
    pState->pPlatform = pPlat;
    pState->th8Interp = Th8_CreateInterp(pPlat);

    if (!pState->th8Interp) {
	ckfree((char *)pState->pPlatform);
	ckfree((char *)pState);
	Tcl_SetResult(
	    tclInterp, "failed to create TH8 interpreter", TCL_STATIC);
	return TCL_ERROR;
    }

    /*
     * Register the TH8 built-in commands in the sub-interpreter.
     */

    Th8_RegisterLanguage(pState->th8Interp);

    /*
     * Register [th8Eval] in the host Tcl interpreter.
     */

    Tcl_CreateObjCommand(
        tclInterp, "th8Eval", th8Eval_objCmd, (ClientData)pState,
        th8Eval_deleteProc);

    Tcl_PkgProvide(tclInterp, "th8bridge", "1.0");
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tclth8bridge_Unload --
 *
 *	Tcl package unload.  The sub-interpreter is destroyed by
 *	the command delete proc when [th8Eval] is removed.
 *
 *----------------------------------------------------------------------
 */

BRIDGE_EXPORT int
Tclth8bridge_Unload(
    Tcl_Interp *tclInterp, /* Host Tcl interpreter. */
    int flags)   /* Unload flags. */
{
    Tcl_Eval(tclInterp, "catch {rename th8Eval {}}");

    if (flags & TCL_UNLOAD_DETACH_FROM_PROCESS) {
	Th8_Finalize(0);
    }

    Tcl_Eval(tclInterp, "catch {package forget th8bridge}");
    return TCL_OK;
}

#endif /* TH8_TCL_BRIDGE_TCL */


/*
 *======================================================================
 *
 * TCL-IN-TH8 MODE (TH8_TCL_BRIDGE_TH8)
 *
 *	Loaded into a TH8 interpreter.  Creates a Tcl sub-interpreter
 *	and provides [tclEval] to evaluate scripts in it.
 *
 *	Uses the "reverse stubs" technique (th8_tcl_stubs.c) to
 *	dynamically load the Tcl shared library at runtime, so no
 *	Tcl import library is needed at build time.
 *
 *	Links against: libth8stub.a (TH8 stubs) + libtclstub.a
 *
 *======================================================================
 */

#ifdef TH8_TCL_BRIDGE_TH8

#  include "th8_meta_libc.h"
#  include "th8.h"

#  ifdef USE_TH8_STUBS
#    include "th8Decls.h"
#  endif

#  include "th8_tcl.h"

/*
 * Forward declarations for the reverse-stubs API
 * (defined in th8_tcl_stubs.c, compiled from externals/mmm/tcl.c).
 */

extern int createTclInterp(
    int argc,
    char **argv,
    const char *setup,
    void **pContext,
    char **pzErrMsg);
extern int evaluateTcl(
    void *pContext,
    const char *zScript,
    size_t nScript,
    char **pzResult,
    size_t *pnResult);
extern int unloadTcl(void *pContext, char **pzErrMsg);

/*
 * The Tcl context (opaque pointer from createTclInterp).
 */

static void *th8TclBridgeCtx = 0;


/*
 *----------------------------------------------------------------------
 *
 * tclEval_command --
 *
 *	Implementation of [tclEval script].
 *	Evaluates the script in the Tcl sub-interpreter and returns
 *	the result.  Return codes are translated directly (the
 *	standard codes 0-4 are identical in TH8 and Tcl).
 *
 *----------------------------------------------------------------------
 */

static int
tclEval_command(
    Th8_Interp *interp, /* Host TH8 interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zResult = 0;
    size_t nResult = 0;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "tclEval script");
    }

    if (!th8TclBridgeCtx) {
	Th8_SetResult(interp, "Tcl interpreter not available", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = evaluateTcl(th8TclBridgeCtx, argv[1], argl[1], &zResult, &nResult);

    if (zResult) {
	Th8_SetResult(interp, zResult, nResult);
	free(zResult);
    }

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8bridge_Init --
 *
 *	TH8 package initialization.  Creates the Tcl sub-interpreter
 *	via the reverse-stubs loader and registers the [tclEval]
 *	command.
 *
 *	Loaded via: load <path>:Th8bridge
 *
 *----------------------------------------------------------------------
 */

BRIDGE_EXPORT int
Th8bridge_Init(Th8_Interp *interp) /* Host TH8 interpreter. */
{
    char *zErrMsg = 0;

#  ifdef USE_TH8_STUBS
    if (Th8_InitStubs(interp, "1.0", 0) == 0) {
	return TH8_ERROR;
    }
#  endif

    /*
     * Create the Tcl interpreter via the reverse-stubs loader.
     * This dynamically loads the Tcl shared library and
     * bootstraps the Tcl stubs table.
     */

    if (createTclInterp(0, 0, 0, &th8TclBridgeCtx, &zErrMsg) !=
        0 /* TCL_OK */) {
	Th8_SetResult(
	    interp, zErrMsg ? zErrMsg : "failed to create Tcl interpreter",
	    TH8_NOLEN);
	free(zErrMsg);
	return TH8_ERROR;
    }

    /*
     * Register [tclEval] in the host TH8 interpreter.
     */

    Th8_CreateCommand(interp, "tclEval", tclEval_command, 0, 0, 0);

    Th8_Eval(interp, 0, "package provide th8bridge 1.0", TH8_NOLEN, NULL, 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8bridge_Unload --
 *
 *	TH8 package unload.  Destroys the Tcl sub-interpreter
 *	and removes the [tclEval] command.
 *
 *----------------------------------------------------------------------
 */

BRIDGE_EXPORT int
Th8bridge_Unload(
    Th8_Interp *interp, /* Host TH8 interpreter. */
    int flags)   /* Unload flags (TH8_UNLOAD_*). */
{
    Th8_Eval(interp, 0, "catch {rename tclEval {}}", TH8_NOLEN, NULL, 0);

    if (th8TclBridgeCtx) {
	unloadTcl(th8TclBridgeCtx, 0);
	th8TclBridgeCtx = 0;
    }

    Th8_Eval(
        interp, 0, "catch {package forget th8bridge}", TH8_NOLEN, NULL, 0);

    (void)flags;
    return TH8_OK;
}

#endif /* TH8_TCL_BRIDGE_TH8 */
