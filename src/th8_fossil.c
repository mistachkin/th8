/*
** th8_fossil.c -- TH8 / Fossil integration bridge.
**
** This file provides the integration layer between the TH8 scripting
** language and the Fossil SCM.  It implements:
**
**   1. Interpreter lifecycle management (init/finalize)
**   2. Dual-language trampoline: TH1 commands accessible from TH8
**   3. Cross-language evaluation: th1Eval/th1Invoke and th8Eval/th8Invoke
**   4. TH8 command registration in the Fossil-specific command table
**
** Design notes:
**
**   The trampoline mechanism works by registering wrapper commands in
**   TH8 that, when invoked, marshal the TH8 arguments into TH1-
**   compatible form (const char** + int* argl) and call the
**   corresponding TH1 command implementation function directly.
**   This avoids the overhead of serializing to a string and reparsing.
**
**   For cross-language eval (th1Eval from TH8, th8Eval from TH1),
**   the bridge serializes the script as a string and calls the
**   other interpreter's Eval function.  Results are copied back.
**
** Build requirements:
**
**   This file is compiled as part of the Fossil build, NOT as part
**   of the standalone TH8 build.  It requires access to both:
**     - th8.h (TH8 public API)
**     - th.h  (TH1 public API, from Fossil)
**     - config.h (Fossil's generated config)
**
** Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
**
** See the file "license.terms" for information on usage and
** redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
*/

#include "config.h"
#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_fossil.h"
#include "th.h"
#include "th8_int.h"

/*
** The global TH1 and TH8 interpreters for use with Fossil.
*/

extern Th_Interp *Th_GetFossilInterp(void);
static Th8_Interp *th8FossilInterp = 0;


/*
** ====================================================================
** Section 1: Return code translation and argument marshalling
**
** TH1 and TH8 have different return code values:
**
**   TH_OK=0  TH_ERROR=1  TH_BREAK=2  TH_RETURN=3  TH_CONTINUE=4
**   TH8_OK=0 TH8_ERROR=1 TH8_RETURN=2 TH8_BREAK=3 TH8_CONTINUE=4
**
** BREAK and RETURN are swapped.  All bridge functions must translate.
**
** TH1 uses (int *argl) for argument lengths.
** TH8 uses (size_t *argl) for argument lengths.
** ====================================================================
*/

/*
** Translate a TH1 return code to TH8.
*/
static int
th8FromTh1Rc(int th1Rc)
{
    switch (th1Rc) {
    case TH_OK:
	return TH8_OK;
    case TH_ERROR:
	return TH8_ERROR;
    case TH_BREAK:
	return TH8_BREAK;
    case TH_RETURN:
	return TH8_RETURN;
    case TH_CONTINUE:
	return TH8_CONTINUE;
    default:
	return th1Rc;
    }
}

/*
** Translate a TH8 return code to TH1.
*/
static int
th1FromTh8Rc(int th8Rc)
{
    switch (th8Rc) {
    case TH8_OK:
	return TH_OK;
    case TH8_ERROR:
	return TH_ERROR;
    case TH8_BREAK:
	return TH_BREAK;
    case TH8_RETURN:
	return TH_RETURN;
    case TH8_CONTINUE:
	return TH_CONTINUE;
    default:
	return th8Rc;
    }
}

/*
** Convert TH1's int-length array to TH8's size_t-length array.
** Returns a Th8_Malloc'd array that the caller must Th8_Free.
*/
static size_t *
th8FossilIntToSizeT(Th8_Interp *interp, const int *anTh1, int argc)
{
    size_t *anTh8;
    int i;

    if (argc <= 0) return 0;
    anTh8 = (size_t *)TH8_ALLOC_MUL(interp, (size_t)argc, sizeof(size_t));
    if (!anTh8) return 0;
    for (i = 0; i < argc; i++) {
	/*
	 * Negative TH1 lengths mean "compute from NUL" (TH1's
	 * equivalent of TH8_NOLEN).  Clamp to 0 since TH8 uses
	 * TH8_NOLEN ((size_t)-1) for that purpose, and the
	 * caller should handle TH8_NOLEN separately if needed.
	 */

	anTh8[i] = (anTh1[i] >= 0) ? (size_t)anTh1[i] : 0;
    }
    return anTh8;
}

/*
** Convert TH8's size_t-length array to TH1's int-length array.
** Returns a Th8_Malloc'd array that the caller must Th8_Free.
** Values that exceed INT_MAX are clamped to INT_MAX.
*/
static int *
th8FossilSizeTToInt(Th8_Interp *interp, const size_t *anTh8, int argc)
{
    int *anTh1;
    int i;

    if (argc <= 0) return 0;
    anTh1 = (int *)TH8_ALLOC_MUL(interp, (size_t)argc, sizeof(int));
    if (!anTh1) return 0;
    for (i = 0; i < argc; i++) {
	/*
	 * TH8_NOLEN is (size_t)-1.  Map it to -1 for TH1.
	 * Otherwise, clamp to INT_MAX to avoid truncation
	 * on LP64 platforms where size_t is 64-bit and int
	 * is 32-bit.
	 */

	if (anTh8[i] == (size_t)-1) {
	    anTh1[i] = -1;
	} else if (anTh8[i] > (size_t)INT_MAX) {
	    anTh1[i] = INT_MAX;
	} else {
	    anTh1[i] = (int)anTh8[i];
	}
    }
    return anTh1;
}


/*
** ====================================================================
** Section 2: TH1 command trampoline (TH1 commands callable from TH8)
**
** Each Fossil-specific TH1 command (puts, query, html, etc.) is
** wrapped in a TH8 command that marshals arguments and calls the
** TH1 implementation directly.
**
** The trampoline context stores the TH1 command proc + context
** so the wrapper can invoke it.
** ====================================================================
*/

typedef struct Th8TrampolineCtx {
    int (*xTh1Proc)(Th_Interp *, void *, int, const char **, int *);
    void *pTh1Ctx;
} Th8TrampolineCtx;

/*
** Generic trampoline: TH8 command -> TH1 command.
*/
static int
th8FossilTrampoline(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8TrampolineCtx *pTramp = (Th8TrampolineCtx *)ctx;
    int *anTh1;
    int rc;
    Th_Interp *th1Interp = Th_GetFossilInterp();

    if (!th1Interp) {
	Th8_SetResult(interp, "TH1 interpreter not available", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Marshal TH8 size_t lengths to TH1 int lengths.
     */

    anTh1 = th8FossilSizeTToInt(interp, argl, argc);
    if (!anTh1) return TH8_ERROR;

    /*
     * Call the TH1 command implementation.
     */

    rc = pTramp->xTh1Proc(th1Interp, pTramp->pTh1Ctx, argc, argv, anTh1);

    /*
     * Copy the TH1 result back to TH8.
     */

    {
	int nResult = 0;
	const char *zResult = Th_GetResult(th1Interp, &nResult);

	if (zResult && nResult > 0) {
	    Th8_SetResult(interp, zResult, (size_t)nResult);
	}
    }

    Th8_Free(interp, anTh1);

    return th8FromTh1Rc(rc);
}

/*
** Destructor for trampoline context.
*/
static void
th8FossilTrampolineDel(Th8_Interp *interp, void *ctx)
{
    Th8_Free(interp, ctx);
}


/*
** th8FossilTh1Dispatch --
**
**     Generic TH8 command that dispatches to TH1.  The TH1 command
**     name is stored as the context pointer (from the static
**     azTrampoline[] table, so it's valid for the process lifetime).
**
**     Builds a TH1 command invocation from the TH8 arguments and
**     evaluates it in the TH1 interpreter.  The TH1 result is
**     copied back to TH8 and the return code is translated.
*/
static int
th8FossilTh1Dispatch(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zTh1Name = (const char *)ctx;
    Th_Interp *th1Interp = Th_GetFossilInterp();
    char *zScript = 0;
    size_t nScript = 0;
    int i;
    int rc;

    if (!th1Interp) {
	Th8_SetResult(interp, "TH1 interpreter not available", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Build the TH1 invocation: the first element is the TH1
     * command name (which may differ from the TH8 argv[0] if
     * the command was renamed), followed by the remaining args.
     */

    Th8_ListAppend(interp, &zScript, &nScript, zTh1Name, TH8_NOLEN);
    for (i = 1; i < argc; i++) {
	Th8_ListAppend(interp, &zScript, &nScript, argv[i], argl[i]);
    }

    rc = Th_Eval(th1Interp, 0, zScript, (int)nScript);
    {
	int nResult = 0;
	const char *zResult = Th_GetResult(th1Interp, &nResult);

	if (zResult && nResult > 0) {
	    Th8_SetResult(interp, zResult, (size_t)nResult);
	}
    }

    Th8_Free(interp, zScript);
    return th8FromTh1Rc(rc);
}


/*
** ====================================================================
** Section 3: Cross-language evaluation commands
** ====================================================================
*/

/*
** TH8 command: th1Eval SCRIPT
**
** Evaluate a TH1 script and return the result.
*/
static int
th8Th1EvalCmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th_Interp *th1Interp;
    int rc;

    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "th1Eval script");
    }

    th1Interp = Th_GetFossilInterp();
    if (!th1Interp) {
	Th8_SetResult(interp, "TH1 interpreter not available", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = Th_Eval(th1Interp, 0, argv[1], (int)argl[1]);
    {
	int nResult = 0;
	const char *zResult = Th_GetResult(th1Interp, &nResult);

	if (zResult && nResult > 0) {
	    Th8_SetResult(interp, zResult, (size_t)nResult);
	}
    }
    return th8FromTh1Rc(rc);
}

/*
** TH8 command: th1Invoke CMD ?ARG ...?
**
** Invoke a TH1 command by name with the given arguments.
** This constructs a TH1 command invocation and evaluates it.
*/
static int
th8Th1InvokeCmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th_Interp *th1Interp;
    char *zScript = 0;
    size_t nScript = 0;
    int i;
    int rc;

    (void)ctx;

    if (argc < 2) {
	return Th8_WrongNumArgs(interp, "th1Invoke command ?arg ...?");
    }

    th1Interp = Th_GetFossilInterp();
    if (!th1Interp) {
	Th8_SetResult(interp, "TH1 interpreter not available", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Build a TH1 command string by list-quoting the arguments.
     */

    for (i = 1; i < argc; i++) {
	Th8_ListAppend(interp, &zScript, &nScript, argv[i], argl[i]);
    }

    rc = Th_Eval(th1Interp, 0, zScript, (int)nScript);
    {
	int nResult = 0;
	const char *zResult = Th_GetResult(th1Interp, &nResult);

	if (zResult && nResult > 0) {
	    Th8_SetResult(interp, zResult, (size_t)nResult);
	}
    }

    Th8_Free(interp, zScript);
    return th8FromTh1Rc(rc);
}

/*
** TH1 command: th8Eval SCRIPT
**
** Registered into the TH1 interpreter.  Evaluates a TH8 script
** and returns the result to TH1.
**
** Note: This function has the TH1 command signature, not TH8.
*/
static int
th1Th8EvalCmd(
    Th_Interp *th1Interp,
    void *ctx,
    int argc,
    const char **argv,
    int *argl)
{
    int rc;

    (void)ctx;
    (void)th1Interp;

    if (argc != 2) {
	return Th_WrongNumArgs(th1Interp, "th8Eval script");
    }

    if (!th8FossilInterp) {
	Th_SetResult(th1Interp, "TH8 interpreter not available", -1);
	return TH_ERROR;
    }

    rc = Th8_Eval(
        th8FossilInterp, 0, argv[1], (size_t)(argl[1] >= 0 ? argl[1] : 0),
        NULL, 0);
    {
	size_t nResult;
	const char *zResult = Th8_GetResult(th8FossilInterp, &nResult);

	if (zResult && nResult > 0) {
	    Th_SetResult(th1Interp, zResult, (int)nResult);
	}
    }
    return th1FromTh8Rc(rc);
}

/*
** TH1 command: th8Invoke CMD ?ARG ...?
**
** Registered into the TH1 interpreter.  Invokes a TH8 command
** and returns the result to TH1.
*/
static int
th1Th8InvokeCmd(
    Th_Interp *th1Interp,
    void *ctx,
    int argc,
    const char **argv,
    int *argl)
{
    char *zScript = 0;
    size_t nScript = 0;
    int i;
    int rc;

    (void)ctx;
    (void)th1Interp;

    if (argc < 2) {
	return Th_WrongNumArgs(th1Interp, "th8Invoke command ?arg ...?");
    }

    if (!th8FossilInterp) {
	Th_SetResult(th1Interp, "TH8 interpreter not available", -1);
	return TH_ERROR;
    }

    for (i = 1; i < argc; i++) {
	Th8_ListAppend(
	    th8FossilInterp, &zScript, &nScript, argv[i],
	    (size_t)(argl[i] >= 0 ? argl[i] : 0));
    }

    rc = Th8_Eval(th8FossilInterp, 0, zScript, nScript, NULL, 0);
    {
	size_t nResult;
	const char *zResult = Th8_GetResult(th8FossilInterp, &nResult);

	if (zResult && nResult > 0) {
	    Th_SetResult(th1Interp, zResult, (int)nResult);
	}
    }

    Th8_Free(th8FossilInterp, zScript);
    return th1FromTh8Rc(rc);
}


/*
** ====================================================================
** Section 4: Interpreter lifecycle
** ====================================================================
*/

/*
** Register a single TH1 command as a trampoline in TH8.
*/
static void
th8FossilRegisterTrampoline(
    Th8_Interp *interp,
    const char *zName,
    int (*xTh1Proc)(Th_Interp *, void *, int, const char **, int *),
    void *pTh1Ctx)
{
    Th8TrampolineCtx *pTramp;

    pTramp = (Th8TrampolineCtx *)TH8_ALLOC(interp, sizeof(Th8TrampolineCtx));
    if (!pTramp) return;
    pTramp->xTh1Proc = xTh1Proc;
    pTramp->pTh1Ctx = pTh1Ctx;

    Th8_CreateCommand(
        interp, zName, th8FossilTrampoline, pTramp, th8FossilTrampolineDel,
        NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FossilReady --
 *
 *	Public API used by Fossil to test whether the TH8
 *	interpreter has already been initialised.  Cheaper
 *	than calling `Th8_InitializeForFossil` from a
 *	hot-path and used by call sites that want to defer
 *	initialisation until first actual use.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	1 if `th8FossilInterp` is non-NULL; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
Th8_FossilReady(void)
{
    return th8FossilInterp != 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetFossilInterp --
 *
 *	Public API: borrow the shared Fossil-owned
 *	`Th8_Interp` pointer.  Callers must not delete the
 *	interpreter -- ownership stays with the
 *	`Th8_InitializeForFossil` / `Th8_FinalizeForFossil`
 *	lifecycle.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	The Fossil-shared `Th8_Interp` (may be NULL if
 *	initialisation has not yet happened or has been
 *	finalised).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
Th8_Interp *
Th8_GetFossilInterp(void)
{
    return th8FossilInterp;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FossilEval --
 *
 *	Public API: evaluate a TH8 script in the Fossil-shared
 *	interpreter, lazily initialising it with
 *	`TH8_FOSSIL_DEFAULT` flags if it does not exist yet.
 *	A double-NULL guard catches initialisation failure
 *	(out of memory, missing platform pieces) and surfaces
 *	`TH8_ERROR` rather than dereferencing.
 *
 * Parameters:
 *	zScript -- script body.
 *	nScript -- script length in bytes (`TH8_NOLEN` to
 *		auto-detect via `strlen`).
 *
 * Returns:
 *	Whatever `Th8_Eval` returns (`TH8_OK`, `TH8_ERROR`,
 *	`TH8_RETURN`, `TH8_BREAK`, `TH8_CONTINUE`).
 *	`TH8_ERROR` if the lazy initialisation failed.
 *
 * Side effects:
 *	May initialise the Fossil-shared interpreter on the
 *	first call.  Whatever `Th8_Eval` performs (which is
 *	arbitrary).
 *
 *----------------------------------------------------------------------
 */
int
Th8_FossilEval(const char *zScript, size_t nScript)
{
    if (!th8FossilInterp) {
	Th8_InitializeForFossil(TH8_FOSSIL_DEFAULT);
    }
    if (!th8FossilInterp) return TH8_ERROR;
    return Th8_Eval(th8FossilInterp, 0, zScript, nScript, NULL, 0);
}


/*
** Th8_InitializeForFossil --
**
**     Main entry point for TH8 integration.  Creates and configures
**     the TH8 interpreter for use within Fossil.
*/
void
Th8_InitializeForFossil(unsigned int flags)
{
    int created = 0;
    int forceReset = flags & TH8_FOSSIL_FORCE_RESET;
    int forceSetup = flags & TH8_FOSSIL_FORCE_SETUP;
    int noTrampoline = flags & TH8_FOSSIL_NO_TRAMPOLINE;

    /*
     * Step 1: Create the interpreter if needed.
     */

    if (forceReset || th8FossilInterp == 0) {
	Th8_Platform *pPlatform;

	if (th8FossilInterp) {
	    Th8_DeleteInterp(th8FossilInterp);
	    th8FossilInterp = 0;
	}

	/*
	 * Build the platform from POSIX/Win32 + libc layers.
	 */

#if defined(_WIN32) || defined(WIN32)
	pPlatform = *Th8_GetWin32Platform();
#elif defined(__APPLE__)
	pPlatform = *Th8_GetMacOSPlatform();
	Th8_MergePlatform(&pPlatform, Th8_GetPosixPlatform());
#else
	pPlatform = *Th8_GetPosixPlatform();
#endif
	Th8_MergePlatform(&pPlatform, Th8_GetLibcPlatform());
	th8FossilInterp = Th8_CreateInterp(&pPlatform);
	if (!th8FossilInterp) return;

	/*
	 * Register the standard TH8 language commands.
	 */

	Th8_RegisterLanguage(th8FossilInterp);

	/*
	 * Enable binary loading for extensions and BigInt support.
	 */

#if defined(TH8_ENABLE_LOAD)
	if (fossil_getenv("TH8SH_NO_LOAD") == NULL) {
	    Th8_EnableLoad(interp, 1);
	}

	if (fossil_getenv("TH8SH_NO_UNLOAD") == NULL) {
	    Th8_EnableUnload(interp, TH8_UNLOAD_OK | TH8_UNLOAD_DANGEROUS);
	}
#endif

#if defined(TH8_ENABLE_BIGINT)
	if (fossil_getenv("TH8SH_NO_BIGINT") == NULL) {
	    Th8_EnableBigint(interp, 1);
	}
#endif

	created = 1;
    }

    /*
     * Step 2: Register cross-language bridge commands in TH8.
     */

    if (created || forceReset) {
	Th8_CreateCommand(
	    th8FossilInterp, "th1Eval", th8Th1EvalCmd, 0, 0, NULL);
	Th8_CreateCommand(
	    th8FossilInterp, "th1Invoke", th8Th1InvokeCmd, 0, 0, NULL);
    }

    /*
     * Step 3: Register cross-language bridge commands in TH1.
     *
     * This requires the TH1 interpreter to exist.  The caller
     * is responsible for ensuring TH1 is initialized first
     * (which Fossil's Th_FossilInit handles).
     */

    if (created || forceReset) {
	Th_Interp *th1Interp = Th_GetFossilInterp();

	if (th1Interp) {
	    Th_CreateCommand(th1Interp, "th8Eval", th1Th8EvalCmd, 0, 0);
	    Th_CreateCommand(th1Interp, "th8Invoke", th1Th8InvokeCmd, 0, 0);
	}
    }

    /*
     * Step 4: Trampoline Fossil's TH1 commands into TH8.
     *
     * Each trampolined command delegates to TH1 via th1Invoke.
     * This avoids needing to expose TH1's static command procs.
     * The overhead is minimal: one list-build + Th_Eval per call.
     *
     * Commands that are already provided natively by TH8 (puts,
     * string, regexp, etc.) are NOT trampolined -- TH8's own
     * implementations are used.
     */

    if (!noTrampoline && (created || forceReset)) {
	/*
	 * Register Fossil-specific TH1 commands as trampolines
	 * in TH8.  Each TH8 command wraps the TH1 C function
	 * directly, marshalling the argument lengths.
	 *
	 * Th_GetFossilCommands() returns the static table from
	 * th_main.c.  Commands that TH8 already provides natively
	 * (e.g., "puts" if TH8's IO plugin is enabled) are
	 * skipped -- TH8's implementation takes precedence.
	 */

	extern Th_FossilCommand *Th_GetFossilCommands(void);
	Th_FossilCommand *aCmd = Th_GetFossilCommands();
	int i;

	for (i = 0; aCmd[i].zName; i++) {
	    /*
	     * Skip commands that TH8 already provides natively.
	     */

	    if (Th8_GetCommandInfo(
	            th8FossilInterp, aCmd[i].zName, TH8_NOLEN, NULL, NULL) ==
	        TH8_OK) {
		continue;
	    }

	    th8FossilRegisterTrampoline(
	        th8FossilInterp, aCmd[i].zName, aCmd[i].xProc,
	        aCmd[i].pContext);
	}
    }

    /*
     * Step 5: Evaluate the "th8-setup" script if present.
     */

    if (created || forceSetup) {
	const char *zSetup = db_get("th8-setup", 0);

	if (zSetup) {
	    int rc =
	        Th8_Eval(th8FossilInterp, 0, zSetup, (size_t)-1, NULL, 0);
	    if (rc == TH8_ERROR) {
		size_t nResult;
		const char
		    *zResult = Th8_GetResult(th8FossilInterp, &nResult);
		/* Log the error but don't abort initialization. */
		if (g.thTrace) {
		    Th_Trace(
		        "th8-setup error: %.*s<br>\n", (int)nResult, zResult);
		}
	    }
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FinalizeForFossil --
 *
 *	Public API: tear down the Fossil-shared TH8
 *	interpreter (if any), freeing every per-interp
 *	resource and clearing the module-static
 *	`th8FossilInterp` pointer so a subsequent
 *	`Th8_InitializeForFossil` rebuilds from scratch.
 *	Safe to call when no interpreter exists.
 *
 * Parameters:
 *	(none)
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Deletes the shared `Th8_Interp` and zeros
 *	`th8FossilInterp`.
 *
 *----------------------------------------------------------------------
 */
void
Th8_FinalizeForFossil(void)
{
    if (th8FossilInterp) {
	Th8_DeleteInterp(th8FossilInterp);
	th8FossilInterp = 0;
    }
}
