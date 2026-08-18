/*
 * th8_variables.c -- Variables plugin for TH8.
 *
 * Implements the variable manipulation commands: append, array, global,
 * incr, set, unset, uplevel, upvar, variable.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8VariablesGetCommands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_vars.h"
#include "th8_util.h"
#include "th8_plugin.h"

#if defined(TH8_PLUGIN_VARIABLES)

/*
 * Exported for use by info subcommands (e.g. info_commands_command
 * enumerating array sub-commands).
 */

const Th8_SubCommand *th8_array_aSub;

/*
 *----------------------------------------------------------------------
 *
 * Utility helpers --
 *
 *	Common patterns extracted to avoid repetition.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8GetOrCreateVar --
 *
 *	Retrieve a variable's value, creating it with a default if it
 *	does not yet exist.  This is the "get-or-init" convenience
 *	pattern used by append_command (default "") and incr_command
 *	(default "0") to avoid special-casing the first use of a
 *	variable.
 *
 *	On return the interpreter result always holds the variable's
 *	current value (either the pre-existing value or the newly
 *	assigned default).
 *
 * Why / How:
 *	Calls Th8_GetVar first; that call, on success, already leaves the
 *	existing value in the interpreter result.  Only when the variable
 *	does not exist does it set the variable to the supplied default and
 *	push that default into the result, so callers never have to
 *	special-case the first use of a variable.
 *
 * Results:
 *	TH8_OK unconditionally.  The interpreter result is set to
 *	the variable's value.
 *
 * Side effects:
 *	Variable may be created with the default value.
 *
 *----------------------------------------------------------------------
 */

static int
th8GetOrCreateVar(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar,  /* Variable name. */
    size_t nVar,  /* Variable name length. */
    const char *zDefault, /* Default value if not found. */
    size_t nDefault)  /* Default value length. */
{
    if (Th8_GetVar(interp, zVar, nVar) != TH8_OK) {
	Th8_SetVar(interp, zVar, nVar, zDefault, nDefault);
	Th8_SetResult(interp, zDefault, nDefault);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetVarFromResult --
 *
 *	Store the current interpreter result into a variable.
 *	This implements a common convenience pattern: after calling
 *	Th8_SetResultInt (or similar), the caller wants the same
 *	value persisted in a variable without re-formatting it.
 *
 *	Typical sequence:
 *	    Th8_SetResultInt(interp, newValue);
 *	    th8SetVarFromResult(interp, zVar, nVar);
 *
 *	Used by incr_command to update the variable after computing
 *	the new integer value.
 *
 * Why / How:
 *	Reads back the current interpreter result string (and its length)
 *	with Th8_GetResult and hands it straight to Th8_SetVar, avoiding a
 *	second integer-to-string conversion by reusing the formatting the
 *	previous Th8_SetResult* call already performed.
 *
 * Results:
 *	TH8_OK if the variable was stored; otherwise the TH8_ERROR status
 *	returned by Th8_SetVar.
 *
 * Side effects:
 *	Variable is set to the current interpreter result string.
 *
 *----------------------------------------------------------------------
 */

static int
th8SetVarFromResult(
    Th8_Interp *interp, /* Interpreter. */
    const char *zVar,  /* Variable name. */
    size_t nVar)  /* Variable name length. */
{
    size_t nResult;
    const char *zResult;

    zResult = Th8_GetResult(interp, &nResult);
    return Th8_SetVar(interp, zVar, nVar, zResult, nResult);
}

/*
 *----------------------------------------------------------------------
 *
 * Variable commands --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8CheckSystemVar --
 *
 *	Check if a variable is a system variable (read-only from
 *	scripts).  Returns TH8_ERROR with a diagnostic if it is,
 *	TH8_OK if the write is allowed.
 *
 * Why / How:
 *	System variables (e.g. env, errorInfo) are managed by the
 *	interpreter core and must not be modified by script code.
 *	This guard is called before set, append, incr, unset, and
 *	array set to enforce that invariant.
 *
 * Results:
 *	TH8_OK if the write is allowed; TH8_ERROR with a diagnostic
 *	message if the variable is read-only.
 *
 * Side effects:
 *	May set the interpreter result to an error message.
 *
 *----------------------------------------------------------------------
 */

static int
th8CheckSystemVar(Th8_Interp *interp, const char *zName, size_t nName)
{
    if (Th8_IsSystemVar(interp, zName, nName)) {
	Th8_ErrorMessage(
	    interp, "can't modify system variable \"", zName, nName);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * set_command --
 *
 *	Implements the Tcl [set] command.  Sets or reads a variable.
 *
 *	set VARNAME ?VALUE?
 *
 * Why / How:
 *	With two arguments, writes VALUE to VARNAME (after checking
 *	for system variable protection).  With one argument, reads
 *	the variable.  In both cases the final value is left in the
 *	interpreter result via Th8_GetVar.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if variable not found (read).
 *
 * Side effects:
 *	Variable may be created or updated.
 *
 *----------------------------------------------------------------------
 */

static int
set_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "set varname ?value?");
    }
    if (argc == 3) {
	if (th8CheckSystemVar(interp, argv[1], argl[1]) != TH8_OK) {
	    return TH8_ERROR;
	}
	/* A failed store (e.g. out of memory) must be reported, not ignored:
	 * otherwise [set] would report success and then read back a stale or
	 * empty value (TH8K-030). */
	if (Th8_SetVar(interp, argv[1], argl[1], argv[2], argl[2]) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
    }
    return Th8_GetVar(interp, argv[1], argl[1]);
}


/*
 *----------------------------------------------------------------------
 *
 * append_command --
 *
 *	Implements the Tcl [append] command.  Appends values to a
 *	variable.
 *
 *	append VARNAME ?VALUE ...?
 *
 * Why / How:
 *	Uses a buffered append strategy: computes the total size needed,
 *	obtains a single buffer from the pool, seeds it with the
 *	existing value, appends all new values, then stores the result
 *	in the variable.  Falls back to iterative StringAppend if the
 *	buffer pool allocation fails.  With no value arguments, acts
 *	as a read (same as [set]).
 *
 * Results:
 *	TH8_OK.  Result is the new value.
 *
 * Side effects:
 *	Variable is created if it does not exist.
 *
 *----------------------------------------------------------------------
 */

static int
append_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Not used. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    int i;
    char *zNew = 0;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "append varname ?value ...?");
    }
    if (argc >= 3 && th8CheckSystemVar(interp, argv[1], argl[1]) != TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * Per Tcl standard: with no value args, just return the
     * current value (error if nonexistent).  With value args,
     * create variable as empty if nonexistent, then append.
     */

    if (argc == 2) {
	return Th8_GetVar(interp, argv[1], argl[1]);
    }

    /*
     * Buffered append path.
     *
     * Two strategies, tried in order:
     *
     * (A) In-place extension: if the variable already borrows a
     *     pool buffer with enough spare capacity, append directly
     *     into the existing buffer (no allocation, no copy of the
     *     existing value).  This makes looped [append] O(n) total
     *     rather than O(n^2).
     *
     * (B) New buffer: allocate a fresh pool buffer sized for
     *     existing+new with power-of-2 headroom, copy existing
     *     value in, append new data, borrow into the variable.
     */

    /*
     * Path A: try in-place extension of a borrowed buffer.
     */
    if (th8AppendInPlace(
            interp, argv[1], argl[1], argc - 2, argv + 2, argl + 2) ==
        TH8_OK) {
	return TH8_OK;
    }

    /*
     * Path B: new-buffer path.
     */
    {
	const char *zCur = "";
	size_t nCur = 0;
	size_t nAppend = 0;
	size_t nTotal;
	size_t nTag = 0; /* OR of the existing value + all appended taints */
	char *pBuf;

	if (Th8_GetVar(interp, argv[1], argl[1]) == TH8_OK) {
	    zCur = Th8_GetResult(interp, &nCur);
	    nTag |= (nCur & TH8_TAG_BITS);
	    nCur = TH8_LEN(nCur);
	}

	for (i = 2; i < argc; i++) {
	    nTag |= (argl[i] & TH8_TAG_BITS);
	    nAppend += TH8_LEN(argl[i]);
	}
	nTotal = nCur + nAppend;

	/*
	 * Allocate at least 2x the needed size (rounded up to the
	 * next power of 2) to ensure amortized O(1) per append.
	 * The extra headroom lets path A extend in-place for many
	 * iterations before falling back here.
	 */
	{
	    size_t nAlloc = 0;
	    size_t nPow2 = 64;

	    if (TH8_SAFE_ADD_SIZE(nTotal, 1, &nAlloc)) goto fallback;

	    /* At least 2x the needed size for doubling amortization. */
	    if (nAlloc <= ((size_t)-1 >> 1)) {
		nAlloc *= 2;
	    }

	    while (nPow2 < nAlloc && ALWAYS(nPow2 <= ((size_t)-1 >> 1))) {
		nPow2 *= 2;
	    }
	    nAlloc = nPow2;
	    pBuf = (char *)th8BufferAlloc(interp, nAlloc);
	    if (!pBuf) goto fallback;

	    if (nCur > 0) {
		Th8_Memcpy(interp, pBuf, zCur, nCur);
	    }
	    for (i = 2; i < argc; i++) {
		Th8_Memcpy(interp, pBuf + nCur, argv[i], TH8_LEN(argl[i]));
		nCur += TH8_LEN(argl[i]);
	    }
	    pBuf[nCur] = '\0';

	    {
		Th8_Value val;
		val.u.buffer.pBuffer = (void *)pBuf;
		/* nUsed carries the accumulated taint; th8SetVarValue masks
		 * it to the raw length for buffer arithmetic and stores the
		 * tagged length in the variable. */
		val.u.buffer.nUsed = nCur | nTag;
		val.u.buffer.nCapacity = nAlloc;
		if (th8SetVarValue(interp, argv[1], argl[1], &val) !=
		    TH8_OK) {
		    th8BufferFree(interp, pBuf, nAlloc);
		    goto fallback;
		}
	    }
	    Th8_SetResult(interp, pBuf, nCur | nTag);
	    return TH8_OK;
	}
    }

fallback:
    /*
     * Fallback: original path if caching fails.
     */
    th8GetOrCreateVar(interp, argv[1], argl[1], "", 0);
    for (i = 2; i < argc; i++) {
	size_t nCur;
	const char *zCur;
	size_t nNew = 0;

	zNew = 0;
	zCur = Th8_GetResult(interp, &nCur);
	TH8_STR_APPEND(interp, &zNew, &nNew, zCur, nCur);
	TH8_STR_APPEND(interp, &zNew, &nNew, argv[i], argl[i]);
	Th8_SetVar(interp, argv[1], argl[1], zNew, nNew);
	Th8_Free(interp, zNew);
	zNew = 0;
	Th8_GetVar(interp, argv[1], argl[1]);
    }
    return TH8_OK;

oom:
    Th8_Free(interp, zNew);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * incr_command --
 *
 *	Implements the Tcl [incr] command.  Increments a variable
 *	by an integer amount.
 *
 *	incr VARNAME ?INCREMENT?
 *
 * Why / How:
 *	Gets the variable's current integer value (defaulting to 0 if
 *	nonexistent), adds the increment (defaulting to 1), performs
 *	overflow-checked addition when enabled, then stores the result
 *	via th8SetVarFromResult.
 *
 * Results:
 *	TH8_OK.  Result is the new value.
 *
 * Side effects:
 *	Variable is created if it does not exist.
 *
 *----------------------------------------------------------------------
 */

static int
incr_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Not used. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    int iVal = 0;
    int iIncr = 1;
    int rc;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "incr varname ?increment?");
    }
    if (th8CheckSystemVar(interp, argv[1], argl[1]) != TH8_OK) {
	return TH8_ERROR;
    }
    if (argc == 3) {
	rc = Th8_ToInt(interp, argv[2], argl[2], &iIncr);
	if (rc != TH8_OK) {
	    return rc;
	}
    }

    /*
     * Get current value; create as 0 if nonexistent.
     */

    if (Th8_GetVar(interp, argv[1], argl[1]) == TH8_OK) {
	rc = Th8_ToInt(interp, Th8_GetResult(interp, 0), TH8_NOLEN, &iVal);
	if (rc != TH8_OK) {
	    return rc;
	}
    }
    /*
     * Overflow-checked addition (when enabled).
     */

    if (Th8_GetOverflowCheck(interp)) {
	if ((iIncr > 0 && iVal > 0x7fffffff - iIncr) ||
	    (iIncr < 0 && iVal < (int)0x80000000 - iIncr)) {
	    Th8_SetResultStatic(interp, "integer overflow", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }
    iVal += iIncr;
    if (Th8_SetResultInt(interp, iVal) != TH8_OK) {
	return TH8_ERROR;
    }
    /* A failed store (out of memory) must be reported, not ignored, or [incr]
     * reports success while the variable keeps its old value (TH8K-030). */
    if (th8SetVarFromResult(interp, argv[1], argl[1]) != TH8_OK) {
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * unset_command --
 *
 *	Implements the Tcl [unset] command.  Removes one or more
 *	variables.
 *
 *	unset ?-nocomplain? ?--? ?VARNAME ...?
 *
 *	Options:
 *	    -nocomplain  Suppress errors for nonexistent variables.
 *	    --           End of options (allows variable names
 *	                 beginning with a dash).
 *
 *	With no variable names, the command is a no-op.
 *
 * Why / How:
 *	Parses -nocomplain and -- options first, then iterates over
 *	remaining arguments.  Each variable is checked against the
 *	system variable guard before removal.  With -nocomplain,
 *	errors are silently ignored.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if a variable does not exist
 *	(unless -nocomplain is given).  Returns the empty string.
 *
 * Side effects:
 *	Named variables are removed from the current scope.
 *
 *----------------------------------------------------------------------
 */

static int
unset_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Not used. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    int i;
    int rc;
    int bNoComplain = 0; /* True if -nocomplain was given. */

    (void)ctx;

    /*
     * Parse options.
     */

    for (i = 1; i < argc; i++) {
	if (th8StrEq(interp, argv[i], argl[i], "-nocomplain")) {
	    bNoComplain = 1;
	} else if (th8StrEq(interp, argv[i], argl[i], "--")) {
	    i++;
	    break;
	} else {
	    break;
	}
    }

    /*
     * Per Tcl standard, unset accepts zero or more variable names.
     * With no args after options, it is a no-op.
     */

    for (; i < argc; i++) {
	if (th8CheckSystemVar(interp, argv[i], argl[i]) != TH8_OK) {
	    if (bNoComplain) continue;
	    return TH8_ERROR;
	}
	rc = Th8_UnsetVar(interp, argv[i], TH8_LEN(argl[i]));
	if (rc != TH8_OK) {
	    if (bNoComplain) continue;
	    return rc;
	}
    }
    Th8_SetResult(interp, 0, 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * global_command --
 *
 *	Implements the Tcl [global] command.  Links local variables
 *	to the global namespace.
 *
 *	global VARNAME ?VARNAME ...?
 *
 * Why / How:
 *	For each variable name, builds the qualified global name
 *	"::varname", ensures the global variable exists (creating it
 *	as empty if needed), then creates a local link via
 *	Th8_LinkVar.  Silently ignores "already linked" errors so
 *	that calling [global] twice is harmless.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Creates local variable links to globals.
 *
 *----------------------------------------------------------------------
 */

static int
global_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Not used. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    int i;
    char *zGlobal = 0;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "global varname ?varname ...?");
    }
    for (i = 1; i < argc; i++) {
	/*
	 * Build the global name "::varname" and create a link.
	 */

	size_t nGlobal = 0;
	int rc;

	zGlobal = 0;
	TH8_STR_APPEND(interp, &zGlobal, &nGlobal, "::", 2);
	TH8_STR_APPEND(interp, &zGlobal, &nGlobal, argv[i], argl[i]);

	/*
	 * Ensure the global variable exists (create if needed).
	 */

	if (!Th8_ExistsVar(interp, zGlobal, nGlobal)) {
	    Th8_SetVar(interp, zGlobal, nGlobal, "", 0);
	}
	rc = Th8_LinkVar(
	    interp, argv[i], TH8_LEN(argl[i]), 1, zGlobal, nGlobal);
	Th8_Free(interp, zGlobal);
	zGlobal = 0;

	/*
	 * Ignore "variable exists" error -- the variable may
	 * already be linked (e.g., calling global twice).
	 */

	if (rc != TH8_OK) {
	    /* Silently continue */
	}
    }
    Th8_ClearResult(interp);
    return TH8_OK;

oom:
    Th8_Free(interp, zGlobal);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * uplevel_command --
 *
 *	Implements the Tcl [uplevel] command.  Evaluates a script
 *	in a caller's frame.
 *
 *	uplevel ?LEVEL? SCRIPT
 *
 * Why / How:
 *	Parses the optional level specifier: "#N" for absolute level,
 *	plain integer for relative offset, default -1 (one level up).
 *	Converts the level to a negative frame offset and delegates to
 *	Th8_NREvalInFrame for frame-switched evaluation, or Th8_NREval
 *	for level 0 (current frame).
 *
 * Results:
 *	Return code from the script evaluation.
 *
 * Side effects:
 *	Temporarily switches the evaluation frame.
 *
 *----------------------------------------------------------------------
 */

static int
uplevel_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iFrame = -1; /* Default: one level up */

    if (argc < 2 || argc > 3) {
	return Th8_WrongNumArgs(interp, "uplevel ?level? script");
    }
    if (argc == 3) {
	/* Parse frame spec */
	if (argv[1][0] == '#') {
	    /*
	     * Absolute level: #0 = global, #1 = first proc.
	     * Convert to negative offset from current.
	     */

	    int absLevel;
	    int rc = Th8_ToInt(interp, &argv[1][1], argl[1] - 1, &absLevel);
	    int curLevel;

	    if (rc != TH8_OK) return rc;
	    curLevel = th8GetFrameLevel(interp);
	    iFrame = -(curLevel - absLevel);
	} else {
	    int rc = Th8_ToInt(interp, argv[1], argl[1], &iFrame);

	    if (rc != TH8_OK) return rc;
	    iFrame = -iFrame;
	}
    }

    if (iFrame == 0) {
	/*
	 * No frame switch needed: evaluate in current frame.
	 */

	return Th8_NREval(interp, argv[argc - 1], argl[argc - 1], NULL, 0);
    }
    return Th8_NREvalInFrame(
        interp, iFrame, argv[argc - 1], argl[argc - 1], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * upvar_command --
 *
 *	Implements the Tcl [upvar] command.  Links local variables
 *	to variables in another frame.
 *
 *	upvar ?LEVEL? OTHERVAR MYVAR ?OTHERVAR MYVAR ...?
 *
 * Why / How:
 *	Uses argc parity to detect the optional level argument: even
 *	argc means the first arg is a level specifier.  Parses #N
 *	(absolute) or plain integer (relative) level, then creates
 *	variable links for each othervar/myvar pair via Th8_LinkVar.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if linking fails.
 *
 * Side effects:
 *	Creates local variable links to another frame's variables.
 *
 *----------------------------------------------------------------------
 */

static int
upvar_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iFrame = -1;
    int iStart = 1;
    int i;
    char *zErr = 0;

    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "upvar ?level? othervar myvar"
	            " ?othervar myvar?");
    }

    /*
     * Check if first arg is a frame spec.
     */

    if ((argc % 2) == 0) {
	/* Even argc: first arg is level */
	int badLevel = 0;
	if (argv[1][0] == '#') {
	    if (Th8_ToInt(interp, &argv[1][1], argl[1] - 1, &iFrame) !=
	            TH8_OK ||
	        iFrame < 0) {
		badLevel = 1;
	    }
	} else {
	    if (Th8_ToInt(interp, argv[1], argl[1], &iFrame) != TH8_OK ||
	        iFrame < 0) {
		badLevel = 1;
	    } else {
		iFrame = -iFrame;
	    }
	}
	if (badLevel) {
	    size_t nErr = 0;
	    TH8_STR_APPEND(interp, &zErr, &nErr, "bad level \"", 11);
	    TH8_STR_APPEND(interp, &zErr, &nErr, argv[1], argl[1]);
	    TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	    Th8_SetResult(interp, zErr, nErr);
	    Th8_Free(interp, zErr);
	    return TH8_ERROR;
	}
	iStart = 2;
    }

    for (i = iStart; i + 1 < argc; i += 2) {
	int rc;

	rc = Th8_LinkVar(
	    interp, argv[i + 1], TH8_LEN(argl[i + 1]), iFrame, argv[i],
	    argl[i]);
	if (rc != TH8_OK) return rc;
    }
    Th8_ClearResult(interp);
    return TH8_OK;

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * variable_command --
 *
 *	Implements the Tcl [variable] command.  Declares a namespace
 *	variable.
 *
 *	variable VARNAME ?VALUE? ?VARNAME VALUE ...?
 *
 *	At namespace level, creates or sets the variable in the
 *	current namespace.  Inside a procedure, creates the
 *	namespace variable (if needed) AND creates a local link
 *	to it, so that unqualified reads/writes from the proc
 *	body access the namespace variable.
 *
 * Why / How:
 *	Detects whether a local link is needed by checking whether the
 *	current frame is inside a proc or a namespace eval.  For each
 *	variable, builds the fully-qualified namespace name, optionally
 *	sets the value, and (when in a proc) creates a local link from
 *	the unqualified tail to the namespace variable via Th8_LinkVar.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	May create a namespace variable and/or a local link.
 *
 *----------------------------------------------------------------------
 */

static int
variable_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Not used. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    int i;
    int inProc; /* True if we're inside a procedure. */
    char *zQual = 0;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "variable varname ?value? ...");
    }

    /*
     * Detect whether we need to create a frame-local link
     * to the namespace variable.  A link is needed whenever
     * the current frame is not the namespace's persistent
     * storage -- i.e., inside a proc OR inside a namespace
     * eval (which pushes an ephemeral frame).
     *
     * We check frame level > 0 (proc body) OR whether the
     * current namespace is non-global (namespace eval frame).
     * In the global namespace at level 0, no link is needed
     * because set already writes to the global frame which
     * IS the namespace's storage.
     */
    {
	const char *zCurNs = Th8_GetCurrentNamespace(interp);

	/* 5-condition compound decomposed into a sequential
	 * single-condition cascade per Finding 005 sec. 5b.
	 * C2 (zCurNs NULL) is intrinsic-dead -- Th8_GetCurrent-
	 * Namespace returns "::" or current ns, never NULL.
	 * C3 and C4 (the ':' char checks) are also dead in
	 * practice because any non-global namespace path starts
	 * with "::"; only C5 (the trailing '\0' check) is
	 * meaningfully driven from script. */
	inProc = 0;
	if (th8GetFrameLevel(interp) > 0) {
	    inProc = 1;
	} else if (zCurNs) {
	    if (zCurNs[0] == ':') {
		if (zCurNs[1] == ':') {
		    if (zCurNs[2] != '\0') inProc = 1;
		}
	    }
	}
    }

    for (i = 1; i < argc; i += 2) {
	const char *zName = argv[i];
	size_t nName = argl[i];
	size_t nQual = 0;
	const char *zNs;

	zQual = 0;

	/*
	 * Build the fully qualified namespace variable name.
	 * If already qualified (starts with ::), use as-is.
	 * Otherwise, prepend the current namespace.
	 *
	 * NOTE: The evaluator sets pCurrentNs to the command's
	 * defining namespace before calling xProc, so
	 * Th8_GetCurrentNamespace always returns the correct
	 * namespace here - even for imported commands.
	 */

	zNs = Th8_GetCurrentNamespace(interp);
	/* Refactored from a 6-condition compound into nested
	 * single-condition checks so clang's MC/DC instrumenter
	 * can encode the decision.  See FINDINGS.md Finding 005.
	 * Semantic: qualify zName with the current namespace iff
	 * (a) zName is NOT already an absolute name (lacks the
	 * leading "::") AND (b) the current namespace is a real
	 * non-root namespace (starts with "::" and has more). */
	{
	    int nameIsAbsolute = 0;
	    int nsIsRealNonRoot = 0;

	    if (nName > 2) {
		if (zName[0] == ':') {
		    if (zName[1] == ':') nameIsAbsolute = 1;
		}
	    }
	    if (zNs[0] == ':') {
		if (zNs[1] == ':') {
		    if (zNs[2] != 0) nsIsRealNonRoot = 1;
		}
	    }
	    if (!nameIsAbsolute && nsIsRealNonRoot) {
		TH8_STR_APPEND(interp, &zQual, &nQual, zNs, TH8_NOLEN);
		TH8_STR_APPEND(interp, &zQual, &nQual, "::", 2);
		TH8_STR_APPEND(interp, &zQual, &nQual, zName, nName);
		zName = zQual;
		nName = nQual;
	    }
	}

	/*
	 * Ensure the namespace variable exists.  If a value
	 * was provided, set it; otherwise create as empty
	 * if it doesn't already exist.
	 */

	if (i + 1 < argc) {
	    Th8_SetVar(interp, zName, nName, argv[i + 1], argl[i + 1]);
	}
	/*
	 * When no value is provided, do NOT create the variable.
	 * Tcl's [variable foo] without a value declares the name
	 * but leaves it unset - [info exists foo] returns 0 until
	 * the variable is explicitly assigned.
	 */

	/*
	 * If we are inside a procedure (or namespace eval),
	 * create a local link from the unqualified tail to
	 * the namespace variable.  This is the key step that
	 * makes "variable x" inside a namespace proc give
	 * access to ::ns::x via plain $x.
	 *
	 * When the name was already fully qualified
	 * (e.g., "variable ::foo::x"), zQual is NULL but
	 * we still need to create the link using the tail
	 * portion as the local name.  Tcl 8.x supports
	 * this form.
	 */

	if (inProc) {
	    const char *zLocal = argv[i];
	    size_t nLocal = TH8_LEN(argl[i]);

	    /* Decompose the 4-condition compound into a nested
	     * single-condition chain so clang's MC/DC instrumenter
	     * does not have to encode 4 branches.  Semantics
	     * unchanged.  See FINDINGS.md Finding 005. */
	    int isAlreadyQual = 0;

	    if (!zQual) {
		if (nName > 2) {
		    if (zName[0] == ':') {
			if (zName[1] == ':') isAlreadyQual = 1;
		    }
		}
	    }
	    if (isAlreadyQual) {
		/*
		 * Already qualified: extract the tail after
		 * the last "::" as the local variable name.
		 */
		const char *p = zName + nName;

		while (p > zName + 2) {
		    if (((size_t)(p - zName) & 0xFFF) == 0) {
			if (Th8_Ready(interp) != TH8_OK) {
			    Th8_Free(interp, zQual);
			    return TH8_ERROR;
			}
		    }
		    p--;
		    if (p[-1] == ':' && p[0] == ':') {
			zLocal = p + 1;
			nLocal = (size_t)((zName + nName) - zLocal);
			break;
		    }
		}
	    }

	    if (nLocal > 0) {
		Th8_LinkVar(interp, zLocal, nLocal, 0, zName, nName);
	    }
	}

	Th8_Free(interp, zQual);
	zQual = 0;
    }
    Th8_ClearResult(interp);
    return TH8_OK;

oom:
    Th8_Free(interp, zQual);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Array commands --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * array_exists_command --
 *
 *	Implements the [array exists] sub-command.  Tests whether
 *	a variable is an array.
 *
 *	array exists VARNAME
 *
 * Why / How:
 *	Delegates to Th8_ExistsArrayVar which checks for the presence
 *	of any parenthesized (element) entries under VARNAME.
 *
 * Results:
 *	TH8_OK.  Result is 1 if the variable is an array, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
array_exists_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "array exists varname");
    }
    Th8_SetResultInt(
        interp, Th8_ExistsArrayVar(interp, argv[2], TH8_LEN(argl[2])));
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * array_names_command --
 *
 *	Implements the [array names] sub-command.  Returns the list
 *	of element names in an array.
 *
 *	array names VARNAME
 *
 * Why / How:
 *	Delegates to Th8_ListAppendArray which iterates over all
 *	variables matching "VARNAME(*)" and extracts the element
 *	keys into a list.
 *
 * Results:
 *	TH8_OK.  Result is a list of element names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
array_names_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "array names varname");
    }
    Th8_ListAppendArray(interp, argv[2], TH8_LEN(argl[2]), &zList, &nList);
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * array_get_command --
 *
 *	Implements the [array get] sub-command.  Returns a list of
 *	name-value pairs for all elements of the array.
 *
 *	array get VARNAME ?PATTERN?
 *
 *	If PATTERN is given, only names matching the glob pattern
 *	are included.
 *
 * Why / How:
 *	Gets the element names via Th8_ListAppendArray, then iterates
 *	over them.  For each matching name, builds the full
 *	"arrayName(key)" variable name, fetches the value, and appends
 *	the name-value pair to the result list.
 *
 * Results:
 *	TH8_OK.  Result is a flat list of alternating names and values.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
array_get_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zNames = 0;
    size_t nNames = 0;
    char *zResult = 0;
    size_t nResult = 0;
    char **azElem = 0;
    size_t *anElem = 0;
    char *zFull = 0;
    int nCount = 0;
    int i;
    int rc;

    (void)ctx;
    if (argc != 3 && argc != 4) {
	return Th8_WrongNumArgs(interp, "array get arrayName ?pattern?");
    }

    Th8_ListAppendArray(interp, argv[2], TH8_LEN(argl[2]), &zNames, &nNames);
    if (!zNames) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    rc = Th8_SplitList(
        interp, zNames, nNames, &azElem, &anElem, &nCount, TH8_LIST_NONE);
    Th8_Free(interp, zNames);
    if (rc != TH8_OK) return rc;

    for (i = 0; i < nCount; i++) {
	size_t nVal;
	const char *zVal;
	size_t nFull;

	if (Th8_Ready(interp) != TH8_OK) goto oom;
	nFull = 0;
	zFull = 0;
	if (argc == 4 &&
	    !Th8_GlobMatch(
	        interp, argv[3], TH8_LEN(argl[3]), azElem[i], anElem[i])) {
	    continue;
	}

	TH8_STR_APPEND(interp, &zFull, &nFull, argv[2], argl[2]);
	TH8_STR_APPEND(interp, &zFull, &nFull, "(", 1);
	TH8_STR_APPEND(interp, &zFull, &nFull, azElem[i], anElem[i]);
	TH8_STR_APPEND(interp, &zFull, &nFull, ")", 1);

	Th8_GetVar(interp, zFull, nFull);
	zVal = Th8_GetResult(interp, &nVal);

	Th8_ListAppend(interp, &zResult, &nResult, azElem[i], anElem[i]);
	Th8_ListAppend(interp, &zResult, &nResult, zVal, nVal);
	Th8_Free(interp, zFull);
	zFull = 0;
    }
    Th8_Free(interp, azElem);

    if (zResult) {
	Th8_SetResult(interp, zResult, nResult);
	Th8_Free(interp, zResult);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;

oom:
    Th8_Free(interp, zFull);
    Th8_Free(interp, azElem);
    Th8_Free(interp, zResult);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * array_set_command --
 *
 *	Implements the [array set] sub-command.  Sets array elements
 *	from a name-value pair list.
 *
 *	array set VARNAME LIST
 *
 *	An odd-length list is an error.  An empty list creates an
 *	empty array.
 *
 * Why / How:
 *	Splits the LIST, validates even length, then iterates over
 *	pairs building "arrayName(key)" variable names and setting
 *	each.  An empty list is handled specially by creating and
 *	immediately unsetting a dummy element to establish the array.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on odd-length list or system
 *	variable violation.
 *
 * Side effects:
 *	Creates or updates array elements.
 *
 *----------------------------------------------------------------------
 */

static int
array_set_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char **azElem = 0;
    size_t *anElem = 0;
    char *zFull = 0;
    int nCount = 0;
    int rc;
    int i;

    (void)ctx;
    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "array set arrayName list");
    }
    if (th8CheckSystemVar(interp, argv[2], argl[2]) != TH8_OK) {
	return TH8_ERROR;
    }

    rc = Th8_SplitList(
        interp, argv[3], argl[3], &azElem, &anElem, &nCount, TH8_LIST_NONE);
    if (rc != TH8_OK) return rc;

    if (nCount == 0) {
	/*
	 * Empty list: ensure the variable exists as an array.
	 * Access it as "name()" then unset the dummy element.
	 */

	size_t nFull = 0;

	zFull = 0;
	TH8_STR_APPEND(interp, &zFull, &nFull, argv[2], argl[2]);
	TH8_STR_APPEND(interp, &zFull, &nFull, "(__th8_array_init__)", 20);
	Th8_SetVar(interp, zFull, nFull, "", 0);
	Th8_UnsetVar(interp, zFull, nFull);
	Th8_Free(interp, zFull);
	zFull = 0;
	Th8_Free(interp, azElem);
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    if (nCount % 2 != 0) {
	Th8_Free(interp, azElem);
	Th8_SetResultStatic(
	    interp, "list must have an even number of elements", TH8_NOLEN);
	return TH8_ERROR;
    }

    for (i = 0; i < nCount; i += 2) {
	size_t nFull;

	if (Th8_Ready(interp) != TH8_OK) goto oom;
	nFull = 0;
	zFull = 0;
	TH8_STR_APPEND(interp, &zFull, &nFull, argv[2], argl[2]);
	TH8_STR_APPEND(interp, &zFull, &nFull, "(", 1);
	TH8_STR_APPEND(interp, &zFull, &nFull, azElem[i], anElem[i]);
	TH8_STR_APPEND(interp, &zFull, &nFull, ")", 1);
	Th8_SetVar(interp, zFull, nFull, azElem[i + 1], anElem[i + 1]);
	Th8_Free(interp, zFull);
	zFull = 0;
    }
    Th8_Free(interp, azElem);
    Th8_ClearResult(interp);
    return TH8_OK;

oom:
    Th8_Free(interp, zFull);
    Th8_Free(interp, azElem);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * array_size_command --
 *
 *	Implements the [array size] sub-command.  Returns the number
 *	of elements in the array.
 *
 *	array size VARNAME
 *
 *	Returns 0 if the variable does not exist or is not an array.
 *
 * Why / How:
 *	Gets the element names via Th8_ListAppendArray, splits the
 *	resulting list to count elements, then returns the count.
 *
 * Results:
 *	TH8_OK.  Result is the element count as an integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
array_size_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zNames = 0;
    size_t nNames = 0;
    int nCount = 0;

    (void)ctx;
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "array size arrayName");
    }

    Th8_ListAppendArray(interp, argv[2], TH8_LEN(argl[2]), &zNames, &nNames);
    if (zNames) {
	Th8_SplitList(interp, zNames, nNames, 0, 0, &nCount, TH8_LIST_NONE);
	Th8_Free(interp, zNames);
    }
    Th8_SetResultInt(interp, nCount);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * array_unset_command --
 *
 *	Implements the [array unset] sub-command.  Removes array
 *	elements.
 *
 *	array unset arrayName ?pattern?
 *
 *	Without a pattern, removes the entire array (equivalent to
 *	[unset arrayName]).  With a pattern, removes only the
 *	elements whose names match the glob pattern.
 *
 * Why / How:
 *	Without a pattern, delegates to Th8_UnsetVar on the array
 *	name directly.  With a pattern, gets all element names,
 *	filters by glob match, builds "arrayName(element)" for each
 *	match, and unsets them individually.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the array does not exist
 *	(no-pattern case only).
 *
 * Side effects:
 *	Removes array elements or the entire array.
 *
 *----------------------------------------------------------------------
 */

static int
array_unset_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zNames = 0;
    size_t nNames = 0;
    char **azName = 0;
    size_t *anName = 0;
    char *zFull = 0;
    int nCount = 0;
    int i;

    (void)ctx;

    if (argc != 3 && argc != 4) {
	return Th8_WrongNumArgs(interp, "array unset arrayName ?pattern?");
    }
    if (argc == 3) {
	/* No pattern: remove the entire array. */
	return Th8_UnsetVar(interp, argv[2], argl[2]);
    }

    /* With pattern: remove matching elements. */
    {
	Th8_ListAppendArray(
	    interp, argv[2], TH8_LEN(argl[2]), &zNames, &nNames);
	if (zNames) {
	    Th8_SplitList(
	        interp, zNames, nNames, &azName, &anName, &nCount,
	        TH8_LIST_NONE);
	    Th8_Free(interp, zNames);
	}
	/* Th8_SplitList sets nCount and azName as a pair: when
	 * nCount > 0, azName is allocated and non-NULL.  azName
	 * is NULL only when nCount == 0, which short-circuits via
	 * the C1 (i < nCount) sub-check before reaching C2.  Wrap
	 * azName ALWAYS at the C2 position. */
	for (i = 0; i < nCount && ALWAYS(azName); i++) {
	    if (Th8_Ready(interp) != TH8_OK) goto oom;
	    if (Th8_GlobMatch(
	            interp, argv[3], TH8_LEN(argl[3]), azName[i],
	            TH8_LEN(anName[i]))) {
		size_t nFull = 0;

		zFull = 0;
		TH8_STR_APPEND(interp, &zFull, &nFull, argv[2], argl[2]);
		TH8_STR_APPEND(interp, &zFull, &nFull, "(", 1);
		TH8_STR_APPEND(interp, &zFull, &nFull, azName[i], anName[i]);
		TH8_STR_APPEND(interp, &zFull, &nFull, ")", 1);
		Th8_UnsetVar(interp, zFull, nFull);
		Th8_Free(interp, zFull);
		zFull = 0;
	    }
	}
	Th8_Free(interp, azName);
    }
    Th8_ClearResult(interp);
    return TH8_OK;

oom:
    Th8_Free(interp, zFull);
    Th8_Free(interp, azName);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ArraySub --
 *
 *	Catalogue of `array` sub-commands, installed into the `array` ensemble
 *	command's per-interpreter sub-command hash at registration (TH8K-025).
 *
 * Why / How:
 *	Published as th8_array_aSub so [info subcommands] can enumerate the
 *	available array sub-commands.
 *
 * Results:
 *	Return code from the sub-command.
 *
 * Side effects:
 *	Determined by the sub-command.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * array_statistics_command --
 *
 *	Implements the [array statistics] sub-command.  Returns a
 *	multi-line summary of the array's element-name and
 *	element-value byte usage, plus simple count and length
 *	statistics.
 *
 *	array statistics arrayName
 *
 * Why / How:
 *	Tcl 8.6's [array statistics] reports hash-bucket distribution
 *	statistics that depend on the implementation's hash table
 *	internals.  TH8 does not expose hash-bucket data to scripts
 *	(arrays are stored as separate variables `arrName(key)`),
 *	so this implementation produces a useful subset that is
 *	well-defined regardless of the implementation: total counts
 *	and lengths of element names and values.
 *
 *	The output format is a five-line text block, one statistic
 *	per line, in a stable order so callers can `gets` or
 *	`split` the result.  Each line is formatted as
 *	`label: value` for human readability.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the array does not exist.
 *
 * Side effects:
 *	None observable beyond the result string.
 *
 *----------------------------------------------------------------------
 */

static int
array_statistics_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zNames = 0;
    size_t nNames = 0;
    char **azElem = 0;
    size_t *anElem = 0;
    char *zFull = 0;
    char *zErr = 0;
    int nCount = 0;
    int i;
    int rc;
    size_t nNameBytesTotal = 0;
    size_t nValueBytesTotal = 0;
    size_t nNameLenMax = 0;
    char zBuf[256];
    int nBuf;
    char *zResult = 0;
    size_t nResult = 0;

    (void)ctx;
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "array statistics arrayName");
    }

    if (!Th8_ExistsArrayVar(interp, argv[2], TH8_LEN(argl[2]))) {
	size_t nErr = 0;
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[2], argl[2]);
	TH8_STR_APPEND(interp, &zErr, &nErr, "\" isn't an array", 16);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }

    Th8_ListAppendArray(interp, argv[2], TH8_LEN(argl[2]), &zNames, &nNames);
    if (zNames) {
	rc = Th8_SplitList(
	    interp, zNames, nNames, &azElem, &anElem, &nCount, TH8_LIST_NONE);
	Th8_Free(interp, zNames);
	if (rc != TH8_OK) return rc;
    }

    /*
     * Walk every element to gather length statistics.
     */
    for (i = 0; i < nCount && ALWAYS(azElem); i++) {
	size_t nVal;
	size_t nFull;

	if (Th8_Ready(interp) != TH8_OK) goto oom;
	nFull = 0;
	zFull = 0;
	nNameBytesTotal += anElem[i];
	if (anElem[i] > nNameLenMax) nNameLenMax = anElem[i];

	TH8_STR_APPEND(interp, &zFull, &nFull, argv[2], argl[2]);
	TH8_STR_APPEND(interp, &zFull, &nFull, "(", 1);
	TH8_STR_APPEND(interp, &zFull, &nFull, azElem[i], anElem[i]);
	TH8_STR_APPEND(interp, &zFull, &nFull, ")", 1);
	Th8_GetVar(interp, zFull, nFull);
	(void)Th8_GetResult(interp, &nVal);
	nValueBytesTotal += nVal;
	Th8_Free(interp, zFull);
	zFull = 0;
    }
    Th8_Free(interp, azElem);
    azElem = 0;
    Th8_ClearResult(interp);

    /*
     * Build the multi-line result.
     */
    nBuf = th8Snprintf(interp, zBuf, sizeof(zBuf), "%d entries\n", nCount);
    TH8_STR_APPEND(interp, &zResult, &nResult, zBuf, (size_t)nBuf);

    nBuf = th8Snprintf(
        interp, zBuf, sizeof(zBuf), "total element name bytes: %llu\n",
        (unsigned long long)nNameBytesTotal);
    TH8_STR_APPEND(interp, &zResult, &nResult, zBuf, (size_t)nBuf);

    nBuf = th8Snprintf(
        interp, zBuf, sizeof(zBuf), "total element value bytes: %llu\n",
        (unsigned long long)nValueBytesTotal);
    TH8_STR_APPEND(interp, &zResult, &nResult, zBuf, (size_t)nBuf);

    if (nCount > 0) {
	nBuf = th8Snprintf(
	    interp, zBuf, sizeof(zBuf), "average element name length: %.2f\n",
	    (double)nNameBytesTotal / (double)nCount);
    } else {
	nBuf = th8Snprintf(
	    interp, zBuf, sizeof(zBuf),
	    "average element name length: 0.00\n");
    }
    TH8_STR_APPEND(interp, &zResult, &nResult, zBuf, (size_t)nBuf);

    nBuf = th8Snprintf(
        interp, zBuf, sizeof(zBuf), "maximum element name length: %llu",
        (unsigned long long)nNameLenMax);
    TH8_STR_APPEND(interp, &zResult, &nResult, zBuf, (size_t)nBuf);

    Th8_SetResult(interp, zResult, nResult);
    Th8_Free(interp, zResult);
    return TH8_OK;

oom:
    Th8_Free(interp, zErr);
    Th8_Free(interp, zFull);
    Th8_Free(interp, azElem);
    Th8_Free(interp, zResult);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * array search-iteration API: startsearch / nextelement / anymore /
 * donesearch.
 *
 * Design rationale, struct layout, and internal accessor
 * declarations live in th8_vars.h.  This block holds only the
 * function bodies.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8ArraySearchSkipEmpty --
 *
 *	Advance a hash-table iteration cursor past empty buckets.
 *	While `*ppCursor` is NULL, step to the next bucket and
 *	load its head entry, stopping at the first non-empty
 *	bucket or when the table is exhausted.  Backs the
 *	`[array nextelement]` walk.
 *
 * Why / How:
 *	The element hash is an array of bucket chains, many of which
 *	are empty; a naive `pHash->aBucket[i]` at the cursor's saved
 *	index can land on a NULL head.  This loop keeps incrementing
 *	`*piBucket` and reloading the bucket head until it finds a
 *	non-NULL entry or runs off the end of the table
 *	(TH8_HASH_SIZE), so callers of `[array nextelement]` always
 *	resume at a real element without duplicating the skip logic.
 *
 * Parameters:
 *	pHash    -- the array's element hash being iterated.
 *	piBucket -- in/out current bucket index; the caller has
 *		set `*ppCursor` to this bucket's head entry.
 *	ppCursor -- in/out cursor; NULL on entry means the current
 *		bucket is empty.  Set to a valid entry, or to NULL
 *		when iteration is exhausted, on return.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Advances `*piBucket` and rewrites `*ppCursor`.
 *
 *----------------------------------------------------------------------
 */
static void
th8ArraySearchSkipEmpty(
    Th8_Hash *pHash,
    int *piBucket,
    Th8_HashEntry **ppCursor)
{
    while (!*ppCursor) {
	if (++*piBucket >= TH8_HASH_SIZE) {
	    *ppCursor = 0;
	    return;
	}
	*ppCursor = pHash->aBucket[*piBucket];
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8ArraySearchFind --
 *
 *	Look up an `[array startsearch]` cursor by search id and
 *	validate it against the named array: the owning
 *	interpreter, the registered array name, array liveness,
 *	and the generation + epoch counters must all match so a
 *	stale or guessed search id cannot drive a re-bound array.
 *	Does NOT raise an error directly -- callers craft their
 *	own message based on context.
 *
 *	The generation counter (rather than a bare pointer
 *	compare on the array hash) avoids the Bug 9 / arrsearch-4.3
 *	flake where the allocator reused a freed hash address
 *	after `array unset` + `array set` of the same name.
 *
 * Why / How:
 *	Finds the search record in the per-interp search hash by SID,
 *	then applies defense-in-depth checks in order -- owning
 *	interp, registered array name, array liveness, generation,
 *	and epoch -- returning NULL on the first mismatch.  The
 *	per-interp monotonic generation counter (rather than a bare
 *	pointer compare on the array hash) is what makes a stale or
 *	guessed SID unable to drive a re-bound array, fixing the
 *	Bug 9 / arrsearch-4.3 allocator-reuse flake.  On a liveness
 *	or generation/epoch failure it also tears the stale record
 *	down (the `invalidate` path) so the caller reports a clean
 *	"couldn't find search".
 *
 * Parameters:
 *	interp -- live interpreter (owns the search hash).
 *	zSid   -- search id (the hash key).
 *	nSid   -- search-id length.
 *	zArray -- array name the search must be bound to.
 *	nArray -- array-name length.
 *
 * Results:
 *	The matching th8ArraySearch on success; NULL on any
 *	mismatch or if the search was found but invalidated.
 *
 * Side effects:
 *	On invalidation, removes the entry from the search hash
 *	and frees the search record and its owned `zArray` copy,
 *	so the caller can report "couldn't find search".
 *
 *----------------------------------------------------------------------
 */
static th8ArraySearch *
th8ArraySearchFind(
    Th8_Interp *interp,
    const char *zSid,
    size_t nSid,
    const char *zArray,
    size_t nArray)
{
    Th8_Hash *pHash = th8GetArraySearchHash(interp, 0);
    Th8_HashEntry *pEntry;
    th8ArraySearch *p;
    Th8_Hash *pArrayHashNow;

    if (!pHash) return 0;
    pEntry = Th8_HashFind(interp, pHash, zSid, nSid, 0);
    /* Bug 26: tombstoned entries may have pData==NULL even after a
     * successful find -- use plain `if`. */
    if (!pEntry || !pEntry->pData) return 0;
    p = (th8ArraySearch *)pEntry->pData;

    /*
     * Defense in depth: even though the hash is per-interp, also
     * check that the search's owning interp matches and that the
     * array-name argument matches what was registered.  Without
     * these, a script could drive a search by guessing its SID
     * after the array name had been re-bound to something else.
     */
    if (p->pInterp != interp) return 0;
    if (p->nArray != nArray ||
        Th8_Memcmp(interp, p->zArray, zArray, nArray) != 0) {
	return 0;
    }

    /*
     * Liveness + identity + generation + epoch.  Generation
     * replaces what used to be a bare pointer-equality check on
     * `pArrayHashNow == p->pArrayHash` -- that check fired
     * intermittently (Bug 9 / arrsearch-4.3 flake) whenever the
     * allocator handed back the same pHash address after an
     * `array unset` + `array set` of the same name.  The
     * generation counter is per-interp and monotonic, so it
     * never collides across hash reallocations.  We still
     * fetch pArrayHashNow as a liveness check (NULL means the
     * array no longer exists at all).
     */
    pArrayHashNow = th8GetArrayElementHash(interp, p->zArray, p->nArray);
    if (!pArrayHashNow) {
	goto invalidate;
    }
    if (th8GetArrayGeneration(interp, p->zArray, p->nArray) !=
        p->nGeneration) {
	goto invalidate;
    }
    if (th8GetArrayEpoch(interp, p->zArray, p->nArray) != p->nEpoch) {
	goto invalidate;
    }
    return p;

invalidate:
    Th8_HashRemove(interp, pHash, zSid, nSid);
    if (p->zArray) Th8_Free(interp, p->zArray);
    Th8_Free(interp, p);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ArraySearchFreeEntry --
 *
 *	Free a single array-search record.  Used both as the
 *	Th8_HashIterate callback during interpreter teardown and
 *	from `[array donesearch]` via the release path.  NULL-safe
 *	in `pEntry` and `pEntry->pData` (Bug 26): a tombstoned or
 *	absent entry is a no-op.
 *
 * Why / How:
 *	Casts the iterate context to the owning interpreter, treats
 *	`pEntry->pData` as the th8ArraySearch, frees its owned
 *	`zArray` copy and the record itself, then clears `pData` so a
 *	later pass over the same entry sees a tombstone.  Sharing one
 *	freer between teardown iteration and `[array donesearch]`
 *	keeps the ownership rules in a single place; the two leading
 *	NULL guards are kept as sequential `if`s (Bug 26 / Finding
 *	005 sec. 5b) so they survive TH8_OMIT without forming a dead
 *	MC/DC C-pair.
 *
 * Parameters:
 *	pEntry -- hash entry whose pData is the th8ArraySearch,
 *		or NULL.
 *	pCtx   -- the owning Th8_Interp (hash-iterate context).
 *
 * Results:
 *	TH8_OK unconditionally.
 *
 * Side effects:
 *	Frees the search record and its owned `zArray` copy, then
 *	clears `pEntry->pData`.
 *
 *----------------------------------------------------------------------
 */
int
th8ArraySearchFreeEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;
    th8ArraySearch *p;

    /* Bug 26: hash iterator callbacks should not assume pEntry
     * is non-NULL even though the iterator contract says so --
     * use plain `if` so the guard survives TH8_OMIT.  Split per
     * Finding 005 sec. 5b: both arms are intrinsic-dead from
     * the iterator contract; sequential singles remove the dead
     * C-pairs from the MC/DC denominator. */
    if (!pEntry) return TH8_OK;
    if (!pEntry->pData) return TH8_OK;
    p = (th8ArraySearch *)pEntry->pData;
    if (p->zArray) Th8_Free(interp, p->zArray);
    Th8_Free(interp, p);
    pEntry->pData = 0;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ArraySearchRelease --
 *
 *	Release every resource owned by an `[array startsearch]`
 *	cursor: remove its entry from the per-interp search
 *	hash (so subsequent `[array nextelement]` calls
 *	report "couldn't find search"), free the cursor's
 *	owned `zArray` copy, and free the cursor struct
 *	itself.
 *
 *	NULL-safe in every operand: missing search hash,
 *	missing sid, and missing search struct each skip
 *	their independent block.  The split-into-separate-`if`
 *	pattern reflects FINDINGS.md Finding 005 sec. 5b --
 *	the test corpus only exercises the all-non-NULL
 *	combination, but the defensive arms stay in place
 *	for future callers and embedder paths.
 *
 * Why / How:
 *	Fetches the per-interp search hash and, when both it and the
 *	SID are present, removes the cursor's entry so subsequent
 *	`[array nextelement]` lookups fail cleanly; then, when the
 *	cursor struct is present, frees its owned `zArray` copy and
 *	the struct itself.  Each operand is guarded by its own `if`
 *	(Finding 005 sec. 5b) so a missing hash, SID, or cursor skips
 *	only its own block rather than aborting the whole release.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	zSid   -- search id (the hash key), or NULL to skip
 *		the hash-removal step.
 *	nSid   -- search-id length.
 *	p      -- cursor struct, or NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the cursor's entry from the per-interp array-
 *	search hash; frees `p->zArray` and `p`.
 *
 *----------------------------------------------------------------------
 */
static void
th8ArraySearchRelease(
    Th8_Interp *interp,
    const char *zSid,
    size_t nSid,
    th8ArraySearch *p)
{
    Th8_Hash *pHash = th8GetArraySearchHash(interp, 0);

    /* Split per Finding 005 sec. 5b: callers always supply
     * a valid hash and sid; the C-pairs were intrinsic-dead
     * in the test corpus. */
    if (pHash) {
	if (zSid) {
	    Th8_HashRemove(interp, pHash, zSid, nSid);
	}
    }
    if (p) {
	if (p->zArray) Th8_Free(interp, p->zArray);
	Th8_Free(interp, p);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * array_startsearch_command --
 *
 *	Implements `[array startsearch arrayName]`.  Allocates
 *	a fresh `th8ArraySearch` cursor positioned at the
 *	first non-empty bucket of the array's element hash,
 *	stamps it with the array's current epoch + generation
 *	(so iteration aborts cleanly if the array is mutated
 *	in a way that invalidates the cursor), and registers
 *	it in the per-interp search hash under a generated
 *	search-id of the form `"s-<counter>-<arrayName>"`.
 *	The search id is returned as the command result.
 *
 *	`th8Snprintf` cannot return a negative encoding error
 *	for the controlled `"s-%d-%s"` inputs (counter is a
 *	non-negative int; the array name is a NUL-terminated
 *	copy of argv[2]), but Bug 26 (2026-06-07) keeps the
 *	negative arm defensive in case a platform snprintf
 *	bug ever returns -1: a fallback name
 *	`"s-<counter>-array"` keeps the search registerable
 *	without smashing a `size_t` cast.
 *
 *	Refuses to start a search on a non-array variable
 *	with the canonical `"arrayName" isn't an array` Tcl
 *	error.
 *
 * Why / How:
 *	Validates argc and array existence, then allocates and
 *	initializes a th8ArraySearch positioned at the first
 *	non-empty bucket and stamped with the array's current epoch
 *	and generation so a later mutation invalidates the cursor.
 *	The `"s-<counter>-<arrayName>"` SID formatted by th8Snprintf
 *	is both unique (monotonic counter) and human-readable; the
 *	defensive negative-return fallback to `"s-<counter>-array"`
 *	(Bug 26) keeps the search registerable even if a platform
 *	snprintf misbehaves, avoiding a bad size_t cast.  The record
 *	is registered in the per-interp search hash and its SID is
 *	returned so later `[array nextelement]`/`anymore`/
 *	`donesearch` calls can retrieve it.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 3).
 *	argv   -- argv[0]=`"array"`; argv[1]=`"startsearch"`;
 *		argv[2]=arrayName.
 *	argl   -- argument byte-lengths.
 *
 * Results:
 *	`TH8_OK` with the generated search-id as the
 *	interpreter result.  `TH8_ERROR` on wrong argument
 *	count, non-array variable, missing search hash, or
 *	allocation failure (interpreter result: diagnostic).
 *
 * Side effects:
 *	Allocates one `th8ArraySearch` cursor and registers
 *	it in the per-interp array-search hash (ownership
 *	tracked by the hash entry).
 *
 *----------------------------------------------------------------------
 */
static int
array_startsearch_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8ArraySearch *pSearch = 0;
    Th8_Hash *pSearchHash;
    Th8_Hash *pArrayHash;
    Th8_HashEntry *pEntry = 0;
    char zSid[64];
    int nSid;
    int counter;
    int nEpoch;
    char *zErr = 0;

    (void)ctx;
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "array startsearch arrayName");
    }
    if (!Th8_ExistsArrayVar(interp, argv[2], TH8_LEN(argl[2]))) {
	size_t nErr = 0;
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[2], argl[2]);
	TH8_STR_APPEND(interp, &zErr, &nErr, "\" isn't an array", 16);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }
    pArrayHash = th8GetArrayElementHash(interp, argv[2], TH8_LEN(argl[2]));
    nEpoch = th8GetArrayEpoch(interp, argv[2], TH8_LEN(argl[2]));

    pSearchHash = th8GetArraySearchHash(interp, 1);
    if (!pSearchHash) return TH8_ERROR;

    pSearch = (th8ArraySearch *)TH8_ALLOC(interp, sizeof(th8ArraySearch));
    if (!pSearch) return TH8_ERROR;
    pSearch->pInterp = interp;
    pSearch->nArray = argl[2];
    pSearch->zArray = (char *)TH8_ALLOC_STR(interp, argl[2]);
    if (!pSearch->zArray) {
	Th8_Free(interp, pSearch);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, pSearch->zArray, argv[2], argl[2]);
    pSearch->zArray[argl[2]] = '\0';
    pSearch->pArrayHash = pArrayHash;
    pSearch->nEpoch = nEpoch;
    pSearch->nGeneration =
        th8GetArrayGeneration(interp, argv[2], TH8_LEN(argl[2]));
    pSearch->iBucket = 0;
    pSearch->pCursor = pArrayHash ? pArrayHash->aBucket[0] : 0;
    if (pArrayHash) {
	th8ArraySearchSkipEmpty(
	    pArrayHash, &pSearch->iBucket, &pSearch->pCursor);
    }

    counter = th8NextArraySearchId(interp);
    nSid = th8Snprintf(
        interp, zSid, sizeof(zSid), "s-%d-%s", counter, pSearch->zArray);
    /* th8Snprintf cannot return a negative encoding error for the
     * controlled "s-%d-%s" inputs above (counter is a non-negative
     * int from th8NextArraySearchId, and pSearch->zArray is a
     * NUL-terminated copy of argv[2]); the truncation branch is
     * driven solely by the C2 buffer-overflow check.
     *
     * Bug 26 (2026-06-07): plain check rather than NEVER -- a
     * platform snprintf bug returning negative would otherwise
     * propagate as a huge size_t under TH8_OMIT collapse. */
    /* Split per Finding 005 sec. 5b.  C1 (`nSid < 0`) is
     * intrinsic-dead: th8Snprintf never returns negative for
     * the controlled "s-%d-%s" inputs above.  C2 (buffer-
     * overflow) is the only drivable arm. */
    if (nSid < 0) {
	/* Defensive: platform snprintf bug fallback. */
	nSid = th8Snprintf(interp, zSid, sizeof(zSid), "s-%d-array", counter);
    } else if ((size_t)nSid >= sizeof(zSid)) {
	/* Array name too long for the buffer; truncate cleanly. */
	nSid = th8Snprintf(interp, zSid, sizeof(zSid), "s-%d-array", counter);
    }
    pEntry = Th8_HashFind(interp, pSearchHash, zSid, (size_t)nSid, 1);
    if (!pEntry) {
	th8ArraySearchRelease(interp, 0, 0, pSearch);
	return TH8_ERROR;
    }
    pEntry->pData = pSearch;

    Th8_SetResult(interp, zSid, (size_t)nSid);
    return TH8_OK;

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * array_nextelement_command --
 *
 *	Implements `[array nextelement arrayName searchId]`.
 *	Looks up the search cursor by id (consulting the
 *	stamped epoch/generation to detect array mutation
 *	that invalidates the cursor), returns the key at the
 *	current cursor position as the command result, then
 *	advances the cursor to the next non-empty bucket
 *	entry via `th8ArraySearchSkipEmpty`.
 *
 *	When the cursor is past the end of the hash, the
 *	command sets the result to the empty string (the
 *	canonical Tcl signal that iteration is done).
 *	`[array anymore]` is the orthogonal "is there
 *	more?" predicate.
 *
 *	A cursor whose lookup fails (missing search id, or
 *	stale epoch) raises the canonical
 *	`couldn't find search "..."` error.
 *
 * Why / How:
 *	Resolves the cursor with th8ArraySearchFind (which also
 *	validates epoch/generation), sets the result to the current
 *	entry's key, then advances `p->pCursor` to `pCursor->pNext`.
 *	Because the epoch was just verified, the bucket chain is
 *	structurally unchanged so `pNext` is still valid; when it
 *	reaches the end of a chain, th8ArraySearchSkipEmpty walks on
 *	to the next non-empty bucket, leaving the cursor ready for
 *	the following call.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 4).
 *	argv   -- argv[0]=`"array"`; argv[1]=`"nextelement"`;
 *		argv[2]=arrayName; argv[3]=searchId.
 *	argl   -- argument byte-lengths.
 *
 * Results:
 *	`TH8_OK` with the next key (or empty result at end).
 *	`TH8_ERROR` on wrong argument count, missing cursor,
 *	or stale-epoch cursor (interpreter result:
 *	diagnostic).
 *
 * Side effects:
 *	Advances the cursor's `iBucket` / `pCursor` fields.
 *
 *----------------------------------------------------------------------
 */
static int
array_nextelement_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8ArraySearch *p;
    Th8_HashEntry *pCur;
    char *zErr = 0;

    (void)ctx;
    if (argc != 4) {
	return Th8_WrongNumArgs(
	    interp, "array nextelement arrayName searchId");
    }
    p = th8ArraySearchFind(
        interp, argv[3], TH8_LEN(argl[3]), argv[2], TH8_LEN(argl[2]));
    if (!p) {
	size_t nErr = 0;
	TH8_STR_APPEND(interp, &zErr, &nErr, "couldn't find search \"", 22);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[3], argl[3]);
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }
    pCur = p->pCursor;
    if (!pCur) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    Th8_SetResult(interp, pCur->zKey, pCur->nKey);

    /*
     * Advance the cursor.  Because the epoch was just verified
     * by th8ArraySearchFind, the bucket chain is structurally
     * unchanged from when this entry was linked, so pCur->pNext
     * is still a valid pointer (or NULL).
     */
    p->pCursor = pCur->pNext;
    /* Split per Finding 005 sec. 5b: C2 (!p->pArrayHash with
     * !p->pCursor) is intrinsic-dead because pArrayHash is set
     * at search creation and never cleared during the search
     * lifetime. */
    if (!p->pCursor) {
	if (p->pArrayHash) {
	    th8ArraySearchSkipEmpty(p->pArrayHash, &p->iBucket, &p->pCursor);
	}
    }
    return TH8_OK;

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * array_anymore_command --
 *
 *	Implements `[array anymore arrayName searchId]`.
 *	Looks up the search cursor by id and reports whether
 *	more elements remain (1) or the cursor is past the
 *	end (0).  Does not advance the cursor -- callers
 *	pair this query with `[array nextelement]` to
 *	iterate.
 *
 *	A missing cursor raises the canonical
 *	`couldn't find search "..."` error, matching the
 *	other `array *search` subcommands.
 *
 * Why / How:
 *	Resolves the cursor with th8ArraySearchFind and reports the
 *	boolean `p->pCursor != 0` as the result -- a non-NULL cursor
 *	means an element is still pending.  It deliberately does not
 *	advance the cursor, so a script can test `anymore` and then
 *	fetch with `nextelement` without skipping an element.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 4).
 *	argv   -- argv[0]=`"array"`; argv[1]=`"anymore"`;
 *		argv[2]=arrayName; argv[3]=searchId.
 *	argl   -- argument byte-lengths.
 *
 * Results:
 *	`TH8_OK` with `1` / `0` as the interpreter result.
 *	`TH8_ERROR` on wrong argument count or missing
 *	cursor (interpreter result: diagnostic).
 *
 * Side effects:
 *	None beyond the interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
array_anymore_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8ArraySearch *p;
    char *zErr = 0;

    (void)ctx;
    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "array anymore arrayName searchId");
    }
    p = th8ArraySearchFind(
        interp, argv[3], TH8_LEN(argl[3]), argv[2], TH8_LEN(argl[2]));
    if (!p) {
	size_t nErr = 0;
	TH8_STR_APPEND(interp, &zErr, &nErr, "couldn't find search \"", 22);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[3], argl[3]);
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }
    Th8_SetResultInt(interp, p->pCursor != 0);
    return TH8_OK;

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * array_donesearch_command --
 *
 *	Implements `[array donesearch arrayName searchId]`.
 *	Looks up the search cursor by id and releases it via
 *	`th8ArraySearchRelease`, which removes the entry from
 *	the per-interp search hash and frees the cursor.
 *	Subsequent `[array nextelement]` calls with the same
 *	id raise the canonical "couldn't find search" error
 *	-- the contract callers rely on for explicit cleanup.
 *
 *	A missing cursor itself raises that same diagnostic;
 *	the helper is idempotent only at the "cursor was
 *	already released" granularity, not at the script
 *	level.
 *
 * Why / How:
 *	Resolves the cursor with th8ArraySearchFind first, so a bad
 *	or stale search id produces the canonical error before any
 *	freeing happens; on success it hands the id and cursor to
 *	th8ArraySearchRelease (unlinking the hash entry and freeing
 *	the record) and clears the result, giving scripts explicit,
 *	deterministic cleanup rather than waiting for interpreter
 *	teardown.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 4).
 *	argv   -- argv[0]=`"array"`; argv[1]=`"donesearch"`;
 *		argv[2]=arrayName; argv[3]=searchId.
 *	argl   -- argument byte-lengths.
 *
 * Results:
 *	`TH8_OK` with the interpreter result cleared.
 *	`TH8_ERROR` on wrong argument count or missing
 *	cursor (interpreter result: diagnostic).
 *
 * Side effects:
 *	Removes the cursor from the per-interp search hash
 *	and frees its allocations.
 *
 *----------------------------------------------------------------------
 */
static int
array_donesearch_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8ArraySearch *p;
    char *zErr = 0;

    (void)ctx;
    if (argc != 4) {
	return Th8_WrongNumArgs(
	    interp, "array donesearch arrayName searchId");
    }
    p = th8ArraySearchFind(
        interp, argv[3], TH8_LEN(argl[3]), argv[2], TH8_LEN(argl[2]));
    if (!p) {
	size_t nErr = 0;
	TH8_STR_APPEND(interp, &zErr, &nErr, "couldn't find search \"", 22);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[3], argl[3]);
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }
    th8ArraySearchRelease(interp, argv[3], TH8_LEN(argl[3]), p);
    Th8_ClearResult(interp);
    return TH8_OK;

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}


static const Th8_SubCommand th8ArraySub[] =
    {{0, "anymore", array_anymore_command},
     {0, "donesearch", array_donesearch_command},
     {0, "exists", array_exists_command},
     {0, "get", array_get_command},
     {0, "names", array_names_command},
     {0, "nextelement", array_nextelement_command},
     {0, "set", array_set_command},
     {0, "size", array_size_command},
     {0, "startsearch", array_startsearch_command},
     {0, "statistics", array_statistics_command},
     {0, "unset", array_unset_command},
     {0, 0, 0}};


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8VariablesCommands[] = {
    {1, 0, "append", append_command},
    {1, 0, "array", 0}, /* pure ensemble (TH8K-025) */
    {1, 0, "global", global_command},
    {1, 0, "incr", incr_command},
    {1, 0, "set", set_command},
    {1, 0, "unset", unset_command},
    {1, 0, "uplevel", uplevel_command},
    {1, 0, "upvar", upvar_command},
    {1, 0, "variable", variable_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8VariablesGetCommands --
 *
 *	Return the command table for the variables plugin.
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
th8VariablesGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8VariablesCommands) /
                  sizeof(th8VariablesCommands[0]));

    th8_array_aSub = th8ArraySub;

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
	    pCommand[i] = th8VariablesCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_VARIABLES */
