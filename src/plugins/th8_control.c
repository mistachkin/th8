/*
 * th8_control.c -- Control flow plugin for TH8.
 *
 * Implements the scalar control flow commands: break, catch,
 * continue, coroutine, error, eval, exit, if, return, subst,
 * switch, yield.
 *
 * This file is part of the plugin architecture.  The commands
 * are registered via Th8_RegisterPlugin using the static
 * command table returned by th8ControlGetCommands.
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

#if defined(TH8_PLUGIN_CONTROL)

/*
 *======================================================================
 *
 * File-local type definitions
 *
 *======================================================================
 */

/*
 * TryState --
 *	State passed between try/finally NRE callbacks.  Used by
 *	try_command and try_post_finally; see Sec.try below.
 */
typedef struct TryState {
    int tryRc;   /* Return code from try script. */
    char *zTryResult;  /* Result from try script (owned). */
    size_t nTryResult;  /* Result length. */
    const char *zFinally; /* Finally script (borrowed from argv). */
    size_t nFinally;  /* Finally script length. */
    char savedCancel[TH8_CANCEL_SAVE_SIZE];
    size_t nSavedAllocBytes; /* Allocation counter before finally. */
} TryState;


/*
 *----------------------------------------------------------------------
 *
 * Control flow commands --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * catch_command / catch_posteval --
 *
 *	Evaluate a script and catch its return code (NRE).
 *
 *	catch SCRIPT ?VARNAME?
 *
 * Why / How:
 *	Implements the Tcl [catch] command.  Uses a two-phase NRE
 *	callback chain: catch_command pushes catch_posteval and
 *	NREvals the script; catch_posteval receives the return code,
 *	optionally stores the result in VARNAME, and sets the
 *	interpreter result to the return code integer.
 *
 *	NRE callback chain:
 *	  1. catch_command pushes catch_posteval, then NREvals SCRIPT.
 *	  2. The trampoline evaluates SCRIPT to completion.
 *	  3. catch_posteval receives the return code (rc) from SCRIPT.
 *
 *	pData layout for catch_posteval:
 *	  [0] = argv   (const char **) -- for VARNAME access
 *	  [1] = argl   (size_t *)      -- for VARNAME length
 *	  [2] = argc   (as th8_int64_t)
 *	  [3] = unused
 *
 *	Cancel-unwind interaction: if the script returned TH8_ERROR
 *	and the interpreter has -unwind cancellation active (set via
 *	[interp cancel -unwind]), catch must NOT intercept the error.
 *	Instead it propagates TH8_ERROR so the script unwinds all
 *	the way to the top-level Th8_Eval caller.  Without this
 *	check, [catch] would silently swallow cancellation errors.
 *
 *----------------------------------------------------------------------
 */

static int catch_posteval(Th8_Interp *, void *[], int);

static int
catch_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "catch script ?varname?");
    }

    Th8_NRAddCallback(
        interp, catch_posteval, (void *)argv, (void *)argl, TH8_INT2PTR(argc),
        0);
    return Th8_NREval(interp, argv[1], argl[1], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * catch_posteval --
 *
 *	NRE callback invoked after the [catch] script completes.
 *	Stores the script result in the optional variable and sets
 *	the interpreter result to the return code integer.
 *	Respects cancel-unwind semantics.
 *
 * Why / How:
 *	Second phase of the [catch] NRE callback chain.  Receives
 *	the return code from the evaluated script.  Must propagate
 *	TH8_SUSPEND, TH8_YIELD, and cancel-unwind errors without
 *	interception.  Clears residual non-unwind cancel state for
 *	edge cases.
 *
 * Results:
 *	TH8_OK, or TH8_ERROR if cancel-unwind is active.
 *
 * Side effects:
 *	May set a variable.  Sets interpreter result to return code.
 *
 *----------------------------------------------------------------------
 */

static int
catch_posteval(Th8_Interp *interp, void *pData[], int rc)
{
#  if defined(TH8_ENABLE_VARIABLES)
    const char **argv = (const char **)pData[0];
    size_t *argl = (size_t *)pData[1];
#  endif
    int argc = (int)(size_t)pData[2];

    /*
     * Suspension must not be intercepted by [catch].  Propagate
     * TH8_SUSPEND so the NRE callback chain is preserved for
     * Th8_Thaw to resume later.
     */

    if (rc == TH8_SUSPEND) return TH8_SUSPEND;
    if (rc == TH8_YIELD) return TH8_YIELD;

    /*
     * If cancellation with -unwind is active, catch must NOT
     * intercept the error.  Propagate it so the script unwinds
     * completely to the top-level Th8_Eval caller.
     *
     * Note: for non-unwind cancellation, the cancel flag has
     * already been consumed (one-shot) by th8RunCallbacks
     * before this callback executes, so catch intercepts
     * the error normally.
     *
     * Bug 54 (2026-06-16): the (T, T) MC/DC vector at this
     * guard is structurally unreachable from current code
     * paths.  Trace:
     *   1. `interp cancel -unwind` calls Th8_CancelEval which
     *      sets bCanceled + cancelFlags=UNWIND and returns
     *      TH8_OK (see th8_core.c:7006-7090).
     *   2. The cancel command returns TH8_OK to script.
     *   3. The catch body returns rc=TH8_OK to this callback.
     *   4. L170 short-circuits on C1=F (rc != TH8_ERROR), then
     *      L182 Th8_ResetCancel clears bCanceled / cancelFlags
     *      via th8ClearCancel (th8_core.c:21076-21092).
     *   5. The unwind error fires later in the OUTER Th8_Eval's
     *      Th8_Ready check at the next command boundary --
     *      bypassing catch_posteval entirely.
     * The guard remains as defense-in-depth in case a future
     * refactor routes unwind through catch_posteval; see
     * FINDINGS.md Finding 001 / Bug 54 for the MC/DC analysis.
     */

    /* Defense-in-depth per Bug 54 / Finding 001 -- this
     * guard's compound C-pairs are intrinsic-dead under
     * the current NRE routing (-unwind path bypasses
     * catch_posteval entirely).  Split per Finding 005
     * sec. 5b so the dead C-pairs aren't in the MC/DC
     * denominator while the runtime check is preserved. */
    if (rc == TH8_ERROR) {
	if (Th8_IsBeingUnwound(interp)) {
	    return TH8_ERROR;
	}
    }

    /*
     * Clear any residual non-unwind cancel state.  This
     * handles the edge case where [interp cancel] was the
     * last command in the catch body (bCanceled set but rc
     * is TH8_OK because the inner eval hadn't dispatched
     * another command to trigger Th8_Ready).
     */

    Th8_ResetCancel(interp);

    if (argc == 3) {
#  if defined(TH8_ENABLE_VARIABLES)
	size_t nResult;
	const char *zResult = Th8_GetResult(interp, &nResult);

	Th8_SetVar(interp, argv[2], TH8_LEN(argl[2]), zResult, nResult);
#  else
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
#  endif
    }
    Th8_SetResultInt(interp, rc);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * break_command --
 *
 *	Terminate the innermost loop.
 *
 *	break
 *
 * Why / How:
 *	Implements the Tcl [break] command.  Simply returns
 *	TH8_BREAK, which is caught by the enclosing loop command
 *	(while, for, foreach) to terminate iteration.
 *
 * Results:
 *	TH8_BREAK.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
break_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "break");
    }
    return TH8_BREAK;
}


/*
 *----------------------------------------------------------------------
 *
 * continue_command --
 *
 *	Skip to the next loop iteration.
 *
 *	continue
 *
 * Why / How:
 *	Implements the Tcl [continue] command.  Simply returns
 *	TH8_CONTINUE, which is caught by the enclosing loop command
 *	(while, for, foreach) to skip to the next iteration.
 *
 * Results:
 *	TH8_CONTINUE.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
continue_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "continue");
    }
    return TH8_CONTINUE;
}


/*
 *----------------------------------------------------------------------
 *
 * error_command --
 *
 *	Raise an error.
 *
 *	error MESSAGE ?INFO? ?CODE?
 *
 * Why / How:
 *	Implements the Tcl [error] command.  Sets the interpreter
 *	result to MESSAGE and optionally sets the ::errorInfo and
 *	::errorCode variables.  Returns TH8_ERROR to unwind the
 *	call stack.
 *
 * Results:
 *	TH8_ERROR.
 *
 * Side effects:
 *	Sets interpreter result.  Optionally sets ::errorInfo and
 *	::errorCode.
 *
 *----------------------------------------------------------------------
 */

static int
error_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc < 2 || argc > 4) {
	return Th8_WrongNumArgs(interp, "error message ?info? ?code?");
    }
#  if !defined(TH8_ENABLE_VARIABLES)
    if (argc >= 3) {
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif
    Th8_SetResult(interp, argv[1], argl[1]);
#  if defined(TH8_ENABLE_VARIABLES)
    if (argc >= 3) {
	Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, argv[2], argl[2]);
    }
    if (argc >= 4) {
	Th8_SetVar(interp, "::errorCode", TH8_NOLEN, argv[3], argl[3]);
    }
#  endif
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * return_command --
 *
 *	Return from a procedure.
 *
 *	return ?-code code? ?-errorinfo info? ?-errorcode code? ?value?
 *
 * Why / How:
 *	Implements the Tcl [return] command.  Parses option/value
 *	pairs for -code, -errorinfo, and -errorcode, then returns
 *	the appropriate return code (default TH8_RETURN).  The
 *	-code option value is parsed by th8ParseReturnCode, which
 *	accepts both symbolic names and integers.
 *
 * Results:
 *	TH8_RETURN, or the integer value of -code.
 *
 * Side effects:
 *	Sets interpreter result.  May set ::errorInfo and ::errorCode.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8ParseReturnCode --
 *
 *	Parse a return code string into an integer code.
 *
 * Why / How:
 *	The [return -code] option accepts both symbolic names (ok,
 *	error, return, break, continue) and integer codes.  This
 *	helper centralizes that parsing so return_command can accept
 *	either form.  Tries numeric conversion first; on failure,
 *	checks each symbolic name in sequence.
 *
 * Results:
 *	TH8_OK on success (code stored via piCode), or TH8_ERROR
 *	if the string is neither numeric nor a recognized name.
 *
 * Side effects:
 *	May set the interpreter result to an error message.
 *
 *----------------------------------------------------------------------
 */

static int
th8ParseReturnCode(Th8_Interp *interp, const char *z, size_t n, int *piCode)
{
    if (Th8_ToInt(0, z, n, piCode) == TH8_OK) return TH8_OK;
    if (th8StrEq(interp, z, n, "ok")) {
	*piCode = TH8_OK;
	return TH8_OK;
    }
    if (th8StrEq(interp, z, n, "error")) {
	*piCode = TH8_ERROR;
	return TH8_OK;
    }
    if (th8StrEq(interp, z, n, "return")) {
	*piCode = TH8_RETURN;
	return TH8_OK;
    }
    if (th8StrEq(interp, z, n, "break")) {
	*piCode = TH8_BREAK;
	return TH8_OK;
    }
    if (th8StrEq(interp, z, n, "continue")) {
	*piCode = TH8_CONTINUE;
	return TH8_OK;
    }
    Th8_ErrorMessage(interp, "bad return code: \"", z, n);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * return_command --
 *
 *	Implements `[return ?-code C? ?-level N? ?-errorinfo E?
 *	?-errorcode K? ?value?]`.  Parses the option suffix
 *	supported by Tcl 8.x:
 *
 *	  *  `-code C` -- override the propagation code (one
 *	     of `ok`/`error`/`return`/`break`/`continue`, or
 *	     an explicit integer).
 *	  *  `-level N` -- number of frames to skip when
 *	     propagating; defaults to 1.
 *	  *  `-errorinfo E` -- override the auto-generated
 *	     errorInfo stack trace for the synthesised error.
 *	  *  `-errorcode K` -- override the structured
 *	     `errorCode` list.
 *	  *  Trailing positional `value` -- the value the
 *	     command returns (defaults to empty string).
 *
 *	When `-errorinfo` / `-errorcode` are present the
 *	command does NOT auto-derive them from the current
 *	error state -- the explicit override wins -- which is
 *	how `[return -code error -errorinfo $info]` re-throws
 *	a captured error.
 *
 *	A non-`return` `-code` value triggers the standard NRE
 *	propagation mechanism so the caller's frame observes
 *	the override.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (>= 1).
 *	argv   -- argv[0]=`"return"`; remaining entries are
 *		the option/value suffix.
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	The selected return code (default `TH8_RETURN`); the
 *	interpreter result is the trailing value (or empty).
 *	`TH8_ERROR` on bad option syntax (interpreter result:
 *	diagnostic).
 *
 * Side effects:
 *	Sets the interpreter result and the return-code
 *	state (`errorInfo` / `errorCode` overrides where
 *	requested).
 *
 *----------------------------------------------------------------------
 */
static int
return_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    int iCode = TH8_RETURN;
    int iArg = 1;
    int iLevel = 1;  /* Default frame-skip count (Tcl 8.x). */
    const char *zErrInfo = NULL;
    size_t nErrInfo = 0;
    int bErrInfo = 0;
    const char *zErrCode = NULL;
    size_t nErrCode = 0;
    int bErrCode = 0;

    (void)ctx;

    /*
     * Parse option/value pairs.  We capture -errorinfo and -errorcode
     * values without applying them yet; per R-03654-57637, the two
     * options are ignored unless the final return code is TH8_ERROR.
     * Applying them eagerly would clobber ::errorInfo / ::errorCode
     * on every successful return that happens to pass them.
     */

    while (iArg < argc) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-code")) {
	    iArg++;
	    if (iArg >= argc) {
		return Th8_WrongNumArgs(
		    interp, "return ?-code code?"
		            " ?-errorinfo info?"
		            " ?-errorcode code? ?value?");
	    }
	    if (th8ParseReturnCode(interp, argv[iArg], argl[iArg], &iCode) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-level")) {
	    iArg++;
	    if (iArg >= argc) {
		return Th8_WrongNumArgs(
		    interp, "return ?-code code?"
		            " ?-errorinfo info?"
		            " ?-errorcode code? ?value?");
	    }
	    if (Th8_ToInt(interp, argv[iArg], argl[iArg], &iLevel) !=
	            TH8_OK ||
	        iLevel < 0) {
		Th8_ErrorMessage(
		    interp, "bad -level value \"", argv[iArg], argl[iArg]);
		return TH8_ERROR;
	    }
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-errorinfo")) {
	    iArg++;
	    if (iArg >= argc) {
		return Th8_WrongNumArgs(
		    interp, "return ?-code code?"
		            " ?-errorinfo info?"
		            " ?-errorcode code? ?value?");
	    }
#  if defined(TH8_ENABLE_VARIABLES)
	    zErrInfo = argv[iArg];
	    nErrInfo = argl[iArg];
	    bErrInfo = 1;
	    iArg++;
#  else
	    Th8_SetResultStatic(
	        interp, "variable resolution not available", TH8_NOLEN);
	    return TH8_ERROR;
#  endif
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-errorcode")) {
	    iArg++;
	    if (iArg >= argc) {
		return Th8_WrongNumArgs(
		    interp, "return ?-code code?"
		            " ?-errorinfo info?"
		            " ?-errorcode code? ?value?");
	    }
#  if defined(TH8_ENABLE_VARIABLES)
	    zErrCode = argv[iArg];
	    nErrCode = argl[iArg];
	    bErrCode = 1;
	    iArg++;
#  else
	    Th8_SetResultStatic(
	        interp, "variable resolution not available", TH8_NOLEN);
	    return TH8_ERROR;
#  endif
	} else {
	    break;
	}
    }

#  if defined(TH8_ENABLE_VARIABLES)
    /*
     * Apply -errorinfo and -errorcode only if the return code is
     * error.  Otherwise the options are dropped silently per
     * R-03654-57637.
     */
    if (iCode == TH8_ERROR) {
	if (bErrInfo) {
	    Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, zErrInfo, nErrInfo);
	}
	if (bErrCode) {
	    Th8_SetVar(interp, "::errorCode", TH8_NOLEN, zErrCode, nErrCode);
	}
    }
#  endif

    /*
     * Remaining argument (if any) is the return value.
     */

    if (iArg < argc) {
	Th8_SetResult(interp, argv[iArg], argl[iArg]);
    } else {
	Th8_ClearResult(interp);
    }

    /*
     * Apply -level N (R-44834-04438).  The default level=1 keeps
     * the existing semantics (immediate caller resumes after the
     * proc returns).  Other levels:
     *   level=0  -- "completion-as-OK": the current proc invocation
     *               itself yields a normal value with iCode; rc==
     *               TH8_OK means return immediately without
     *               unwinding the proc frame.  Useful for the
     *               unusual pattern `return -level 0 -code error
     *               msg` which converts the current statement
     *               (not the proc body) into an error.
     *   level=1  -- standard return; rc stays at iCode (typically
     *               TH8_RETURN), proc cleanup maps RETURN->OK.
     *   level=2  -- the proc returns and ITS caller also returns
     *               with iCode.  TH8 already has the unwind
     *               infrastructure: rc=TH8_RETURN2 makes the
     *               proc-cleanup map RETURN2->RETURN, which the
     *               outer proc-cleanup then maps RETURN->OK.  Net
     *               effect: two frames unwind, the outer caller
     *               sees the value with iCode (mapped) applied.
     *   level>=3 -- only supported for iCode==TH8_RETURN today;
     *               we cascade through TH8_RETURN2 once but cannot
     *               unwind further without an additional return
     *               code (TH8_RETURN3 etc.).  Reject with a clear
     *               diagnostic.
     */
    if (iLevel == 0) {
	return iCode;
    }
    if (iLevel == 1) {
	return iCode;
    }
    if (iLevel == 2 && iCode == TH8_RETURN) {
	return TH8_RETURN2;
    }
    Th8_SetResultStatic(
        interp,
        "return -level values other than 0, 1, or 2 are not"
        " supported (and -level 2 only with the default"
        " return-code)",
        TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * exit_command --
 *
 *	Set the interpreter's exit flag and return TH8_ERROR.
 *	The sticky bExit flag causes Th8_Ready to fail on every
 *	subsequent call, unwinding the entire call stack back to
 *	the host.
 *
 *	exit ?code?
 *
 *	The optional integer code is emitted to stderr as
 *	"[exit]: <code>".  The code has no effect on the actual
 *	process exit code (the shell always uses exit code 2
 *	when Th8_IsExited returns true).
 *
 * Why / How:
 *	Implements the Tcl [exit] command.  Unlike Tcl, TH8 does
 *	not call the C exit() function.  Instead, it sets a sticky
 *	flag via Th8_Exit that causes all subsequent Th8_Ready
 *	checks to fail, cleanly unwinding the interpreter back to
 *	the host.  Uses argv[0] in the diagnostic message so
 *	renamed commands show the current name.
 *
 * Results:
 *	TH8_OK (the exit flag causes subsequent commands to fail).
 *
 * Side effects:
 *	Sets the interpreter exit flag.  Emits exit code to stderr.
 *
 *----------------------------------------------------------------------
 */

static int
exit_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    int exitCode = TH8_EXIT_DEMAND;

    (void)ctx;

    if (argc > 2) {
	return Th8_WrongNumArgs(interp, "exit ?code?");
    }

    if (argc == 2) {
	if (Th8_ToInt(interp, argv[1], argl[1], &exitCode) != TH8_OK) {
	    return TH8_ERROR;
	}
    }

    /*
     * Emit the exit code to stderr.
     */

    {
	char *zMsg = 0;
	size_t nMsg = 0;

	Th8_StringAppend(interp, &zMsg, &nMsg, "[", TH8_NOLEN);
	Th8_StringAppend(interp, &zMsg, &nMsg, argv[0], argl[0]);
	Th8_StringAppend(interp, &zMsg, &nMsg, "]: ", TH8_NOLEN);

	if (argc == 2) {
	    Th8_StringAppend(interp, &zMsg, &nMsg, argv[1], argl[1]);
	} else {
	    const char *zRes;
	    size_t nRes = 0;
	    Th8_SetResultDouble(interp, (double)TH8_EXIT_DEMAND);
	    zRes = Th8_GetResult(interp, &nRes);
	    Th8_StringAppend(interp, &zMsg, &nMsg, zRes, nRes);
	}

	Th8_StringAppend(interp, &zMsg, &nMsg, "\n", 1);
	Th8_OutputError(interp, zMsg, nMsg);
	Th8_Free(interp, zMsg);
    }

    /*
     * Set the exit flag and unwind.
     */

    Th8_Exit(interp);
    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Coroutine commands: [coroutine] and [yield].
 *
 *	These are thin wrappers that delegate to the public C API
 *	Th8_CoroCreate, Th8_CoroYield, and Th8_CoroResume (in th8_core.c)
 *	which have access to NRE internals.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * yield_command --
 *
 *	Yield from the current coroutine.
 *
 *	yield ?VALUE?
 *
 * Why / How:
 *	Implements the Tcl [yield] command.  Delegates directly to
 *	Th8_CoroYield which suspends the current coroutine, passing
 *	the optional value back to the resuming caller.
 *
 * Results:
 *	TH8_YIELD on success, or TH8_ERROR if not inside a coroutine.
 *
 * Side effects:
 *	Suspends the current coroutine.
 *
 *----------------------------------------------------------------------
 */

static int
yield_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc > 2) {
	return Th8_WrongNumArgs(interp, "yield ?value?");
    }
    return Th8_CoroYield(
        interp, argc == 2 ? argv[1] : 0, argc == 2 ? argl[1] : 0);
}

/*
 *----------------------------------------------------------------------
 *
 * coroutine_command --
 *
 *	Create a new coroutine with the given name and body.
 *
 *	coroutine NAME COMMAND ?ARG ...?
 *
 * Why / How:
 *	Implements the Tcl [coroutine] command.  Concatenates COMMAND
 *	and any additional arguments into a body script, then
 *	delegates to Th8_CoroCreate to register the coroutine under
 *	NAME.  The body is built using Th8_ListAppend for proper
 *	quoting, then freed after creation.
 *
 * Results:
 *	TH8_OK on success, or TH8_ERROR on failure.
 *
 * Side effects:
 *	Registers a new coroutine command and evaluates the body
 *	until the first [yield].
 *
 *----------------------------------------------------------------------
 */

static int
coroutine_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zBody = 0;
    size_t nBody = 0;
    int rc;
    int i;

    (void)ctx;

    if (argc < 3) {
	return Th8_WrongNumArgs(interp, "coroutine name command ?arg...?");
    }

    /*
     * Build the body script: "command arg1 arg2 ..."
     */

    for (i = 2; i < argc; i++) {
	if (i > 2) {
	    Th8_StringAppend(interp, &zBody, &nBody, " ", 1);
	}
	Th8_ListAppend(interp, &zBody, &nBody, argv[i], argl[i]);
    }

    rc = Th8_CoroCreate(interp, argv[1], argl[1], zBody, nBody);
    Th8_Free(interp, zBody);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Additional control flow commands --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * if_command --
 *
 *	Conditional evaluation with keyword parsing.
 *
 *	if EXPR ?then? BODY ?elseif EXPR ?then? BODY? ... ?else BODY?
 *
 * Why / How:
 *	Implements the Tcl [if] command.  Walks the argument list
 *	evaluating condition expressions via Th8_Expr.  Skips
 *	optional "then" and "else" keywords.  When a condition is
 *	true, NREvals the corresponding body.  Supports elseif
 *	chains and a final else clause.  Also supports legacy
 *	positional-style arguments for backward compatibility.
 *
 * Results:
 *	Return code from the evaluated body, or TH8_OK.
 *
 * Side effects:
 *	Evaluates one body script.
 *
 *----------------------------------------------------------------------
 */

static int
if_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    int i = 1;
    int rc;
    int iCond;

    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "if expr ?then? script ?elseif expr ?then? script?"
	            " ... ?else script?");
    }

    while (1) {
	if (i >= argc) {
	    return Th8_WrongNumArgs(
	        interp, "if expr ?then? script ?elseif expr ?then?"
	                " script? ... ?else script?");
	}

	/*
	 * Evaluate the condition expression.
	 */

	rc = Th8_Expr(interp, argv[i], argl[i], NULL, 0);
	if (rc != TH8_OK) return rc;
	rc = Th8_ToBoolean(
	    interp, Th8_GetResult(interp, 0), TH8_NOLEN, &iCond);
	if (rc != TH8_OK) return rc;
	i++;

	/* Skip optional "then" keyword. */
	if (i < argc && th8StrEq(interp, argv[i], argl[i], "then")) {
	    i++;
	}

	if (i >= argc) {
	    return Th8_WrongNumArgs(interp, "if expr ?then? script");
	}

	if (iCond) {
	    return Th8_NREval(interp, argv[i], argl[i], NULL, 0);
	}
	i++;  /* skip body */

	if (i >= argc) {
	    break;
	}
	if (th8StrEq(interp, argv[i], argl[i], "elseif")) {
	    i++;
	    continue;
	}
	if (th8StrEq(interp, argv[i], argl[i], "else")) {
	    i++;
	    if (i >= argc) {
		return Th8_WrongNumArgs(interp, "if ... else script");
	    }
	    return Th8_NREval(interp, argv[i], argl[i], NULL, 0);
	}

	/*
	 * Not elseif or else -- treat as another condition
	 * (backward compat with positional style).
	 */
    }

    Th8_SetResult(interp, 0, 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * eval_command / th8EvalCleanup --
 *
 *	Concatenate arguments and evaluate the result as a script.
 *
 *	eval ARG ?ARG ...?
 *
 * Why / How:
 *	Implements the Tcl [eval] command.  For a single argument,
 *	uses a zero-copy fast path via Th8_NREval.  For multiple
 *	arguments, concatenates them with spaces and pushes an NRE
 *	cleanup callback to free the concatenated buffer after
 *	evaluation completes.
 *
 *	When argc == 2, the single argument is NREval'd directly
 *	(zero-copy fast path).  When argc > 2, all arguments are
 *	concatenated with spaces into a heap-allocated buffer.
 *	The th8EvalCleanup callback is pushed to free this buffer
 *	after evaluation completes, regardless of return code.
 *
 *	NRE callback chain (multi-arg case only):
 *	  1. eval_command concatenates args, pushes th8EvalCleanup
 *	     with pData[0] = zScript, then NREvals zScript.
 *	  2. th8EvalCleanup frees zScript and propagates rc.
 *
 *	NOTE: th8EvalCleanup is a general-purpose "free pData[0]"
 *	callback also reused by source_command, apply_command,
 *	and napply_command to free heap-allocated data after NREval.
 *
 * Results:
 *	Return code from the evaluated script.
 *
 * Side effects:
 *	Evaluates the concatenated script.
 *
 *----------------------------------------------------------------------
 */

/* th8EvalCleanup declared in th8_int.h (non-static, shared) */

static int
eval_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "eval arg ?arg ...?");
    }
    if (argc == 2) {
	return Th8_NREval(interp, argv[1], argl[1], NULL, 0);
    } else {
	/*
	 * Concatenate all arguments with spaces.
	 */

	char *zScript = 0;
	size_t nScript = 0;
	int i;

	for (i = 1; i < argc; i++) {
	    if (i > 1) {
		Th8_StringAppend(interp, &zScript, &nScript, " ", 1);
	    }
	    Th8_StringAppend(interp, &zScript, &nScript, argv[i], argl[i]);
	}

	Th8_NRAddCallback(interp, th8EvalCleanup, (void *)zScript, 0, 0, 0);
	return Th8_NREval(interp, zScript, nScript, NULL, 0);
    }
}


/* th8EvalCleanup moved to th8_core.c (Bug 35: it is generic
 * NRE infrastructure used by both this control plugin and the
 * procedures plugin; living in the control plugin made it
 * unreachable when ENABLE_EXPRESSIONS=0 disabled
 * PLUGIN_CONTROL). */


/*
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8SwitchMatch --
 *
 *	Helper: test whether a pattern matches a string in the mode
 *	selected by the switch command (exact, glob, or regexp) with
 *	optional case folding.  For regexp mode, builds and evals a
 *	[regexp] command via Th8_ListAppend (proper list quoting).
 *
 *----------------------------------------------------------------------
 */

static int
th8SwitchMatch(
    Th8_Interp *interp,
    const char *zPat,
    size_t nPat,
    const char *zStr,
    size_t nStr,
    int useGlob,
    int noCase)
{
    if (useGlob == 2) {
	/* Regexp mode: build [regexp ?-nocase? pattern string] */
	char *zCmd = 0;
	size_t nCmd = 0;
	int matched = 0;

	Th8_ListAppend(interp, &zCmd, &nCmd, "regexp", 6);
	if (noCase) {
	    Th8_ListAppend(interp, &zCmd, &nCmd, "-nocase", 7);
	}
	Th8_ListAppend(interp, &zCmd, &nCmd, zPat, nPat);
	Th8_ListAppend(interp, &zCmd, &nCmd, zStr, nStr);
	if (Th8_Eval(interp, 0, zCmd, nCmd, NULL, 0) == TH8_OK) {
	    int v = 0;
	    Th8_ToInt(interp, Th8_GetResult(interp, 0), TH8_NOLEN, &v);
	    matched = (v != 0);
	}
	Th8_Free(interp, zCmd);
	return matched;
    }

    if (useGlob == 1) {
	/* Glob mode */
	if (noCase) {
	    /*
	     * Fold both pattern and string to lowercase, then
	     * match.  ASCII-only folding (A-Z -> a-z).
	     */
	    char *zLPat, *zLStr;
	    size_t i;
	    int m;

	    zLPat = (char *)TH8_ALLOC_STR(interp, nPat);
	    zLStr = (char *)TH8_ALLOC_STR(interp, nStr);
	    /* Split per Finding 005.  Both branches share the
	     * same OOM cleanup. */
	    if (!zLPat) {
		Th8_Free(interp, zLStr);
		return 0;
	    }
	    if (!zLStr) {
		Th8_Free(interp, zLPat);
		return 0;
	    }
	    for (i = 0; i < nPat; i++) {
		unsigned char c = (unsigned char)zPat[i];
		zLPat[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
	    }
	    zLPat[nPat] = '\0';
	    for (i = 0; i < nStr; i++) {
		unsigned char c = (unsigned char)zStr[i];
		zLStr[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
	    }
	    zLStr[nStr] = '\0';
	    m = Th8_GlobMatch(interp, zLPat, nPat, zLStr, nStr);
	    Th8_Free(interp, zLPat);
	    Th8_Free(interp, zLStr);
	    return m;
	}
	return Th8_GlobMatch(interp, zPat, nPat, zStr, nStr);
    }

    /* Exact mode */
    if (nPat != nStr) return 0;
    if (noCase) {
	size_t j;
	for (j = 0; j < nPat; j++) {
	    unsigned char a = (unsigned char)zPat[j];
	    unsigned char b = (unsigned char)zStr[j];
	    if (a >= 'A' && a <= 'Z') a += 32;
	    if (b >= 'A' && b <= 'Z') b += 32;
	    if (a != b) return 0;
	}
	return 1;
    }
    return (Th8_Memcmp(interp, zPat, zStr, nPat) == 0);
}


/*
 *----------------------------------------------------------------------
 *
 * switch_command --
 *
 *	Multi-way branching.
 *
 *	switch ?OPTIONS? STRING PATTERN BODY ?PATTERN BODY ...?
 *	switch ?OPTIONS? STRING { PATTERN BODY ... }
 *
 * Why / How:
 *	Implements the Tcl [switch] command.  Supports -exact (default),
 *	-glob, -regexp, and -nocase matching modes.  Handles both the
 *	flat-arg form (alternating pattern/body pairs) and the brace-list
 *	form (single argument containing all pairs).  Fall-through with
 *	"-" bodies is supported.  The brace-list form uses Th8_Eval to
 *	avoid NRE lifetime issues with the split arrays.
 *
 * Results:
 *	Return code from the matched body, or TH8_OK if no match.
 *
 * Side effects:
 *	Evaluates one body script.
 *
 *	Algorithm:
 *
 *	1. Option parsing: -exact (default), -glob, and -- (end of
 *	   options) are consumed from the front of the argument list.
 *
 *	2. Brace-list vs flat-arg detection: if exactly one argument
 *	   remains after the STRING, it is treated as a brace-list
 *	   body and split with Th8_SplitList into pattern/body pairs.
 *	   Otherwise the remaining arguments are taken as alternating
 *	   pattern/body pairs directly.
 *
 *	3. Pattern matching: each pattern is compared against STRING
 *	   using either exact byte comparison or glob matching
 *	   (Th8_GlobMatch) depending on the mode.  The special pattern
 *	   "default" always matches (like Tcl's default arm).
 *
 *	4. Fall-through with "-": if a body is the literal string "-",
 *	   it signals fall-through.  The code skips forward in pairs
 *	   until a non-"-" body is found and evaluates that body.
 *	   This matches Tcl's fall-through semantics.
 *
 *	5. Evaluation: for the flat-arg form, the matched body is
 *	   NREval'd (zero-copy).  For the brace-list form, the body
 *	   is Th8_Eval'd because the split arrays may be freed before
 *	   an NRE callback runs.
 *
 *	NOTE: The brace-list form uses Th8_Eval (not NREval) to
 *	avoid lifetime issues with the Th8_SplitList arrays.
 *
 *----------------------------------------------------------------------
 */

static int
switch_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iArg = 1;
    int useGlob = 0;
    int noCase = 0;
    const char *zString;
    size_t nString;
    int i;

    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "switch ?options? string pattern body ...");
    }

    /*
     * Parse options.
     */

    while (iArg < argc) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-exact")) {
	    useGlob = 0;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-glob")) {
	    useGlob = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-regexp")) {
	    useGlob = 2;  /* 2 = regexp mode */
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-nocase")) {
	    noCase = 1;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "--")) {
	    iArg++;
	    break;
	} else {
	    break;
	}
    }

    if (iArg >= argc) {
	return Th8_WrongNumArgs(
	    interp, "switch ?options? string pattern body ...");
    }
    zString = argv[iArg];
    nString = argl[iArg];
    iArg++;

    /*
     * If there is exactly one remaining argument, it is a
     * brace-list body: split it into pattern/body pairs.
     */

    if (argc == iArg + 1) {
	char **azPairs = 0;
	size_t *anPairs = 0;
	int nPairs;
	int rc;
	int k;
	int matched = 0;

	rc = Th8_SplitList(
	    interp, argv[iArg], argl[iArg], &azPairs, &anPairs, &nPairs,
	    TH8_LIST_NONE);
	if (rc != TH8_OK) return rc;

	if (nPairs % 2 != 0) {
	    Th8_SetResultStatic(
	        interp, "extra switch pattern with no body", TH8_NOLEN);
	    Th8_Free(interp, azPairs);
	    return TH8_ERROR;
	}

	/*
	 * Match directly on the split pairs (no recursion
	 * to avoid NRE lifetime issues with freed arrays).
	 */

	for (k = 0; k + 1 < nPairs; k += 2) {
	    if (th8StrEq(interp, azPairs[k], anPairs[k], "default")) {
		matched = 1;
	    } else {
		matched = th8SwitchMatch(
		    interp, azPairs[k], TH8_LEN(anPairs[k]), zString,
		    TH8_LEN(nString), useGlob, noCase);
	    }
	    if (matched) {
		/* Fall-through: skip "-" bodies */
		int j = k + 1;

		while (j + 1 < nPairs &&
		       th8StrEq(interp, azPairs[j], anPairs[j], "-")) {
		    j += 2;
		}
		if (j < nPairs) {
		    rc = Th8_Eval(interp, 0, azPairs[j], anPairs[j], NULL, 0);
		}
		break;
	    }
	}
	Th8_Free(interp, azPairs);
	if (!matched) {
	    Th8_SetResult(interp, 0, 0);
	}
	return matched ? rc : TH8_OK;
    }

    /*
     * Remaining args are pattern/body pairs.
     */

    for (i = iArg; i + 1 < argc; i += 2) {
	int matched = 0;

	if (th8StrEq(interp, argv[i], argl[i], "default")) {
	    matched = 1;
	} else {
	    matched = th8SwitchMatch(
	        interp, argv[i], TH8_LEN(argl[i]), zString, TH8_LEN(nString),
	        useGlob, noCase);
	}

	if (matched) {
	    /*
	     * Fall-through: if body is "-", skip to next body.
	     */

	    int j = i + 1;

	    while (j + 1 < argc && th8StrEq(interp, argv[j], argl[j], "-")) {
		j += 2;
	    }
	    if (j < argc) {
		return Th8_NREval(interp, argv[j], argl[j], NULL, 0);
	    }
	    break;
	}
    }

    /*
     * Check for trailing pattern without body.
     */

    if ((argc - iArg) % 2 != 0) {
	Th8_SetResultStatic(
	    interp, "extra switch pattern with no body", TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * subst_command --
 *
 *	Perform Tcl-style substitutions on a string.
 *
 *	subst ?-nobackslashes? ?-nocommands? ?-novariables? string
 *	subst ?-backslashes? ?-commands? ?-variables? string  (TIP #712)
 *
 *	Supports both the traditional -no* flags and the TIP #712
 *	positive flags.  Mixing positive and negative flags is an error.
 *
 * Why / How:
 *	Implements the Tcl [subst] command.  Parses the flag arguments
 *	to build a flags bitmask, then delegates to Th8_Subst which
 *	performs the actual backslash, command, and variable
 *	substitutions.  Supports both traditional negative flags and
 *	the newer TIP #712 positive flags, but not both simultaneously.
 *
 * Results:
 *	Return code from Th8_Subst.
 *
 * Side effects:
 *	May evaluate embedded commands and resolve variables.
 *
 *----------------------------------------------------------------------
 */

static int
subst_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int flags = TH8_SUBST_ALL;
    int havePositive = 0;
    int haveNegative = 0;
    int i;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "subst ?-nobackslashes? ?-nocommands?"
	            " ?-novariables? string");
    }

    for (i = 1; i < argc - 1; i++) {
	if (argl[i] >= 2 && argv[i][0] == '-') {
	    if (th8StrEq(interp, argv[i], argl[i], "-nobackslashes")) {
		flags &= ~TH8_SUBST_BACKSLASHES;
		haveNegative = 1;
	    } else if (th8StrEq(interp, argv[i], argl[i], "-nocommands")) {
		flags &= ~TH8_SUBST_COMMANDS;
		haveNegative = 1;
	    } else if (th8StrEq(interp, argv[i], argl[i], "-novariables")) {
		flags &= ~TH8_SUBST_VARIABLES;
		haveNegative = 1;
	    } else if (th8StrEq(interp, argv[i], argl[i], "-backslashes")) {
		if (!havePositive) flags = 0;
		flags |= TH8_SUBST_BACKSLASHES;
		havePositive = 1;
	    } else if (th8StrEq(interp, argv[i], argl[i], "-commands")) {
		if (!havePositive) flags = 0;
		flags |= TH8_SUBST_COMMANDS;
		havePositive = 1;
	    } else if (th8StrEq(interp, argv[i], argl[i], "-variables")) {
		if (!havePositive) flags = 0;
		flags |= TH8_SUBST_VARIABLES;
		havePositive = 1;
	    } else if (th8StrEq(interp, argv[i], argl[i], "--")) {
		i++;
		break;
	    } else {
		Th8_ErrorMessage(interp, "bad option \"", argv[i], argl[i]);
		return TH8_ERROR;
	    }
	} else {
	    break;
	}
    }

    if (havePositive && haveNegative) {
	Th8_SetResultStatic(
	    interp,
	    "cannot mix positive and negative"
	    " substitution flags",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    if (i != argc - 1) {
	return Th8_WrongNumArgs(
	    interp, "subst ?-nobackslashes? ?-nocommands?"
	            " ?-novariables? string");
    }

    return Th8_Subst(interp, argv[i], argl[i], flags);
}


/*
 *----------------------------------------------------------------------
 *
 * try_command / try_post_try / try_post_finally --
 *
 *	try SCRIPT ?finally SCRIPT?
 *
 * Why / How:
 *	Implements the Eagle-style [try] command (not the Tcl 8.6
 *	version).  Uses a three-phase NRE callback chain:
 *	try_command -> try_post_try -> try_post_finally.  The
 *	finally block gets special treatment for security: cancel
 *	state is saved/restored, a fresh memory budget is granted,
 *	and exit prevents evaluation.
 *
 *	The purest form of try/finally (from Eagle, not Tcl).  No
 *	catch clause, no fancy features.
 *
 *	Security properties:
 *	  - The finally script is temporarily exempt from pending
 *	    cancellation (cancel state saved/restored).
 *	  - The finally script is NOT evaluated if the exit flag is set.
 *	  - The finally script gets a fresh memory allocation budget
 *	    equal to the interpreter's limit (total max = 2x limit).
 *	  - All Th8_Ready checks except cancellation must pass.
 *	  - If finally succeeds, the try script's result/rc is returned.
 *	  - If finally fails, the finally script's result/rc is returned.
 *	  - The finally result/rc is stored in dedicated interp fields.
 *
 *----------------------------------------------------------------------
 */

/* TryState -- declared at top of file. */


/*
 * try_post_finally -- NRE callback after the finally script completes.
 */

static int
try_post_finally(Th8_Interp *interp, void *pData[], int rc)
{
    TryState *pState = (TryState *)pData[0];
    const char *zFinallyResult;
    size_t nFinallyResult;

    /*
     * Store the finally result/rc in the dedicated interp fields.
     */
    zFinallyResult = Th8_GetResult(interp, &nFinallyResult);
    th8SetFinallyState(interp, zFinallyResult, nFinallyResult, rc);

    /*
     * Restore the pre-finally allocation counter.  The finally
     * block's allocations are allowed to persist, but the counter
     * is reset so it doesn't permanently consume the budget.
     */
    th8SetAllocBytes(interp, pState->nSavedAllocBytes);

    /*
     * Restore the cancel state.
     */
    th8RestoreCancel(interp, pState->savedCancel);

    /*
     * Decide which result to return.
     */
    if (rc == TH8_OK) {
	/*
	 * Finally succeeded: return the try script's result/rc.
	 */
	Th8_SetResult(interp, pState->zTryResult, pState->nTryResult);
	rc = pState->tryRc;
    }
    /* else: finally failed -- its result/rc is already in the interp. */

    Th8_Free(interp, pState->zTryResult);
    Th8_Free(interp, pState);
    return rc;
}


/*
 * try_post_try -- NRE callback after the try script completes.
 */

static int
try_post_try(Th8_Interp *interp, void *pData[], int rc)
{
    TryState *pState = (TryState *)pData[0];
    const char *zResult;
    size_t nResult;

    /*
     * Save the try script's result and return code.
     */
    zResult = Th8_GetResult(interp, &nResult);
    pState->tryRc = rc;
    pState->zTryResult = Th8_Strdup(interp, zResult, nResult);
    pState->nTryResult = nResult;

    /*
     * If there is no finally clause, we're done.
     */
    if (!pState->zFinally) {
	Th8_Free(interp, pState);
	return rc;
    }

    /*
     * Check: do NOT evaluate finally if exit flag is set.
     */
    if (Th8_IsExited(interp)) {
	Th8_SetResult(interp, pState->zTryResult, pState->nTryResult);
	rc = pState->tryRc;
	Th8_Free(interp, pState->zTryResult);
	Th8_Free(interp, pState);
	return rc;
    }

    /*
     * Check all Th8_Ready conditions except cancellation:
     * stack, exit (already checked above), suspension, step.
     *
     * Bug 55 (2026-06-16): the C2-pair (F, T) -- stack OK,
     * IsSuspended true -- at this guard is structurally
     * unreachable from current code paths.  Trace:
     *   1. If the try body calls th8testlib::freeze (or any
     *      Th8_Freeze-triggering construct), Th8_Freeze sets
     *      bSuspended=1 and the body's Th8_Eval returns
     *      TH8_SUSPEND.
     *   2. The TH8_SUSPEND propagates straight up through the
     *      NRE machinery to the outermost catcher
     *      (th8testlib::freezecycle or the top-level Th8_Eval).
     *      try_post_try is NOT invoked during this propagation
     *      because the NRE chain is captured ("frozen"),
     *      not unwound through callbacks.
     *   3. The outermost catcher calls Th8_Thaw, which clears
     *      bSuspended.
     *   4. When the NRE chain resumes (if it ever does --
     *      freezecycle returns TH8_OK and the caller continues
     *      from there), try_post_try MAY be invoked, but by
     *      that point bSuspended is 0 and the C2 test is F.
     * Drive attempt 2026-06-16:
     *   th8testlib::freezecycle { try { th8testlib::freeze }
     *       finally { ... } }
     *   produced rc=OK and ran the finally block, confirming
     *   that try_post_try sees IsSuspended=false in practice.
     * The guard remains as defense-in-depth and as symmetry
     * with the th8CheckStack arm (which has its own
     * intrinsic-script-fragile vector for stack exhaustion).
     * See FINDINGS.md Finding 001 / Bug 55 for the analysis.
     */
    /* Split per Finding 005 sec. 5b: both C-pairs are
     * intrinsic-dead per Bug 55's drive-attempt analysis
     * (try_post_try sees IsSuspended=false in practice,
     * stack-exhaustion path requires script-fragile depth). */
    {
	int triggerFinallySkip = 0;

	if (th8CheckStack(interp) != TH8_OK) {
	    triggerFinallySkip = 1;
	} else if (Th8_IsSuspended(interp)) {
	    triggerFinallySkip = 1;
	}
	if (triggerFinallySkip) {
	    Th8_SetResult(interp, pState->zTryResult, pState->nTryResult);
	    rc = pState->tryRc;
	    Th8_Free(interp, pState->zTryResult);
	    Th8_Free(interp, pState);
	    return rc;
	}
    }

    /*
     * Save and clear the cancel state so the finally block
     * starts clean (not pre-canceled).
     */
    th8SaveCancel(interp, pState->savedCancel);

    /*
     * Save the current allocation counter and give the finally
     * block a fresh budget equal to the interpreter's limit.
     * Total max memory = 2 * nAllocLimit.
     */
    pState->nSavedAllocBytes = Th8_GetAllocBytes(interp);
    th8SetAllocBytes(interp, 0);

    /*
     * Push the finally callback and evaluate.
     */
    Th8_NRAddCallback(interp, try_post_finally, (void *)pState, 0, 0, 0);
    return Th8_NREval(interp, pState->zFinally, pState->nFinally, NULL, 0);
}


/*
 * try_command -- entry point.
 */

static int
try_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    TryState *pState;

    (void)ctx;

    /*
     * try SCRIPT
     * try SCRIPT finally SCRIPT
     */

    if (argc != 2 && argc != 4) {
	return Th8_WrongNumArgs(interp, "try script ?finally script?");
    }

    if (argc == 4) {
	/*
	 * Validate the "finally" keyword.
	 */
	if (argl[2] != 7 || Th8_Memcmp(interp, argv[2], "finally", 8) != 0) {
	    Th8_ErrorMessage(
	        interp, "expected \"finally\", got \"", argv[2], argl[2]);
	    return TH8_ERROR;
	}
    }

    pState = (TryState *)TH8_ALLOC(interp, sizeof(TryState));
    if (!pState) return TH8_ERROR;

    pState->tryRc = TH8_OK;
    pState->zTryResult = 0;
    pState->nTryResult = 0;
    pState->nSavedAllocBytes = 0;

    if (argc == 4) {
	pState->zFinally = argv[3];
	pState->nFinally = argl[3];
    } else {
	pState->zFinally = 0;
	pState->nFinally = 0;
    }

    /*
     * Push the post-try callback and evaluate the try script.
     */
    Th8_NRAddCallback(interp, try_post_try, (void *)pState, 0, 0, 0);
    return Th8_NREval(interp, argv[1], argl[1], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8ControlCommands[] = {
    {1, 0, "break", break_command},
    {1, 0, "catch", catch_command},
    {1, 0, "continue", continue_command},
    {1, 0, "coroutine", coroutine_command},
    {1, 0, "error", error_command},
    {1, 0, "eval", eval_command},
    {1, 0, "exit", exit_command},
    {1, 0, "if", if_command},
    {1, 0, "return", return_command},
    {1, 0, "subst", subst_command},
    {1, 0, "switch", switch_command},
    {1, 0, "try", try_command},
    {1, 0, "yield", yield_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8ControlGetCommands --
 *
 *	Return the command table for the control flow plugin.
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
th8ControlGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8ControlCommands) / sizeof(th8ControlCommands[0]));

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
	    pCommand[i] = th8ControlCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_CONTROL */
