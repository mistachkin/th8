/*
 * th8_looping.c -- Looping plugin for TH8.
 *
 * Implements the iterative loop commands: for, foreach, while.
 *
 * All three share the NRE three-callback loop pattern:
 *   command -> step (condition test) -> postbody (break/continue
 *   handling + empty-body step guard) -> back to step.
 *
 * This file is part of the plugin architecture.  The commands
 * are registered via Th8_RegisterPlugin using the static
 * command table returned by th8LoopingGetCommands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_plugin.h"

#if defined(TH8_PLUGIN_LOOPING)


/*
 *======================================================================
 *
 * File-local type definitions
 *
 *======================================================================
 */

#  if defined(TH8_ENABLE_VARIABLES)
/*
 * Th8_ForeachPair --
 *	One varlist-list pair for [foreach].  Built from a pair of
 *	[foreach { i j } $list ...] arguments; iIndex tracks the
 *	cursor into azValue across iterations.
 */
typedef struct Th8_ForeachPair Th8_ForeachPair;
struct Th8_ForeachPair {
    char **azVar; /* Variable names (from SplitList) */
    size_t *anVar; /* Variable name lengths */
    int nVar;  /* Number of variable names */
    char **azValue; /* List values (from SplitList) */
    size_t *anValue; /* Value lengths */
    int nValue;  /* Number of values */
    int iIndex;  /* Current value index for this pair */
};

/*
 * Th8_ForeachState --
 *	NRE state for [foreach]: the array of varlist-list pairs
 *	plus the body script.  Passed across foreach_step /
 *	foreach_postbody / foreach_cleanup callbacks.
 */
typedef struct Th8_ForeachState Th8_ForeachState;
struct Th8_ForeachState {
    int nPair;  /* Number of varlist-list pairs */
    Th8_ForeachPair *aPair; /* Array of pairs */
    const char *zBody; /* Body script (borrows from argv) */
    size_t nBody; /* Body script length */
};
#  endif


/*
 *----------------------------------------------------------------------
 *
 * for_command / for_step / for_postbody --
 *
 *	C-style for loop, NRE-converted.
 *
 *	for INIT TEST NEXT BODY
 *
 *	NRE callback chain (three-callback loop pattern):
 *
 *	  1. for_command pushes for_step, then NREvals INIT.
 *	  2. for_step evaluates TEST inline.  If true, pushes
 *	     for_postbody and NREvals BODY.
 *	  3. for_postbody handles break/continue, applies the
 *	     empty-body step guard, pushes for_step, and NREvals NEXT.
 *	  4. Control returns to step 2 via the trampoline.
 *
 *	Each continuation returns to the trampoline, keeping the
 *	C stack depth constant regardless of iteration count.
 *
 *	pData layout for for_step:
 *	  [0] = argv  (const char **)
 *	  [1] = argl  (size_t *)
 *	  [2] = unused
 *	  [3] = unused
 *
 *	pData layout for for_postbody:
 *	  [0] = argv  (const char **)
 *	  [1] = argl  (size_t *)
 *	  [2] = nBefore (th8_int64_t) -- step count before body,
 *	         used by the empty-body step guard to detect bodies
 *	         that produce zero steps and force one step via
 *	         Th8_Ready to prevent infinite loops
 *	  [3] = unused
 *
 *	Break/continue propagation:
 *	  - TH8_BREAK in for_step or for_postbody -> return TH8_OK
 *	    (terminates the loop cleanly).
 *	  - TH8_CONTINUE in for_postbody -> converted to TH8_OK,
 *	    then NEXT is evaluated and the loop continues.
 *
 * Why / How:
 *	The three-callback loop pattern (command/step/postbody) keeps
 *	the C stack depth constant: each iteration returns to the NRE
 *	trampoline instead of recursing.  The for_command entry point
 *	pushes for_step, then NREvals INIT; the trampoline drives the
 *	rest.
 *
 *----------------------------------------------------------------------
 */

static int for_step(Th8_Interp *, void *[], int);
static int for_postbody(Th8_Interp *, void *[], int);

/*
 *----------------------------------------------------------------------
 *
 * for_command --
 *
 *	Implements the Tcl [for] command.  Entry point for the
 *	C-style for loop.
 *
 *	for INIT TEST NEXT BODY
 *
 * Why / How:
 *	Pushes the for_step continuation onto the NRE stack, then
 *	NREvals the INIT script.  The trampoline runs INIT first
 *	(LIFO order), then for_step takes over the iteration.
 *
 * Results:
 *	Return code from INIT evaluation (for_step handles the rest).
 *
 * Side effects:
 *	Pushes for_step onto the NRE callback stack.  Evaluates INIT.
 *
 *----------------------------------------------------------------------
 */

static int
for_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc != 5) {
	return Th8_WrongNumArgs(interp, "for init condition incr script");
    }

    /*
     * Push the step continuation, then NREval the init script.
     * The trampoline runs init first (LIFO), then for_step.
     */

    Th8_NRAddCallback(interp, for_step, (void *)argv, (void *)argl, 0, 0);
    return Th8_NREval(interp, argv[1], argl[1], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * for_step --
 *
 *	NRE callback for the [for] loop condition test.  Evaluates the
 *	condition expression.  If true, pushes for_postbody and NREvals
 *	the loop body.  Handles TH8_BREAK by returning TH8_OK.
 *
 * Why / How:
 *	Called by the trampoline after INIT (first iteration) or after
 *	NEXT (subsequent iterations).  Evaluates TEST as a boolean
 *	expression.  On true, saves the step count and pushes
 *	for_postbody before NREval-ing the body so the postbody can
 *	detect empty bodies via the step guard.
 *
 * Results:
 *	TH8_OK when the loop terminates or on break; propagates errors.
 *
 * Side effects:
 *	May push for_postbody onto the NRE callback stack and
 *	initiate body evaluation.
 *
 *----------------------------------------------------------------------
 */

static int
for_step(Th8_Interp *interp, void *pData[], int rc)
{
    const char **argv = (const char **)pData[0];
    size_t *argl = (size_t *)pData[1];
    int iCond;

    if (rc == TH8_BREAK) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (rc != TH8_OK) return rc;

    /*
     * Evaluate the condition expression.
     */

    rc = Th8_Expr(interp, argv[2], argl[2], NULL, 0);
    if (rc != TH8_OK) return rc;

    rc = Th8_ToBoolean(interp, Th8_GetResult(interp, 0), TH8_NOLEN, &iCond);
    if (rc != TH8_OK) return rc;

    if (!iCond) {
	Th8_SetResult(interp, 0, 0);
	return TH8_OK;
    }

    /*
     * Condition is true: push post-body handler, NREval body.
     * Save the step count before the body in pData[2] so the
     * post-body handler can detect empty bodies.
     */

    Th8_NRAddCallback(
        interp, for_postbody, (void *)argv, (void *)argl,
        TH8_INT2PTR(Th8_GetStepCount(interp)), 0);
    return Th8_NREval(interp, argv[4], argl[4], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * for_postbody --
 *
 *	NRE callback invoked after the [for] loop body completes.
 *	Handles break/continue, applies the empty-body step guard,
 *	pushes for_step for the next iteration, and NREvals the
 *	increment script.
 *
 * Why / How:
 *	After the body runs, this callback checks the return code for
 *	break/continue, applies the empty-body step guard to prevent
 *	infinite loops from bodies that consume zero steps, then chains
 *	back to for_step via the NEXT script.  The step guard compares
 *	the current step count against the saved nBefore value.
 *
 * Results:
 *	TH8_OK on break; propagates errors; otherwise continues loop.
 *
 * Side effects:
 *	May force a step via Th8_Ready.  Pushes for_step and
 *	NREvals the increment script.
 *
 *----------------------------------------------------------------------
 */

static int
for_postbody(Th8_Interp *interp, void *pData[], int rc)
{
    const char **argv = (const char **)pData[0];
    size_t *argl = (size_t *)pData[1];
    th8_int64_t nBefore = (th8_int64_t)(size_t)pData[2];

    if (rc == TH8_BREAK) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (rc == TH8_CONTINUE) rc = TH8_OK;
    if (rc != TH8_OK) return rc;

    /*
     * Empty-body step guard: if the body produced zero
     * steps, force-increment to prevent infinite loops.
     */

    if (Th8_GetStepCount(interp) == nBefore) {
	rc = Th8_Ready(interp);
	if (rc != TH8_OK) return rc;
    }

    /*
     * Push for_step (runs after incr), then NREval incr.
     */

    Th8_NRAddCallback(interp, for_step, (void *)argv, (void *)argl, 0, 0);
    return Th8_NREval(interp, argv[3], argl[3], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * while_command / while_step / while_postbody --
 *
 *	While loop, NRE-converted.  Follows the same three-callback
 *	loop pattern as for_command, but without the INIT and NEXT
 *	scripts.
 *
 *	while TEST BODY
 *
 *	NRE callback chain:
 *	  1. while_command evaluates TEST inline.  If true, pushes
 *	     while_postbody and NREvals BODY.
 *	  2. while_postbody handles break/continue, applies the
 *	     empty-body step guard, then pushes while_step.
 *	  3. while_step evaluates TEST.  If true, pushes
 *	     while_postbody and NREvals BODY.
 *	  4. Control returns to step 2 via the trampoline.
 *
 *	Note that while_command evaluates the first TEST inline
 *	(not via while_step) so that [while 0 {...}] never pushes
 *	any NR callbacks.
 *
 *	pData layout for while_step and while_postbody:
 *	  [0] = argv    (const char **)
 *	  [1] = argl    (size_t *)
 *	  [2] = nBefore (th8_int64_t) -- step count before body,
 *	         for empty-body step guard
 *	  [3] = unused
 *
 *	Break/continue propagation: identical to for_command.
 *
 * Why / How:
 *	Uses the same three-callback pattern as [for] but omits INIT
 *	and NEXT scripts.  The entry point evaluates TEST inline for
 *	the first iteration (avoiding unnecessary NRE callbacks when
 *	the condition is immediately false), then delegates to the
 *	while_step / while_postbody callbacks for subsequent iterations.
 *
 *----------------------------------------------------------------------
 */

static int while_step(Th8_Interp *, void *[], int);
static int while_postbody(Th8_Interp *, void *[], int);

/*
 *----------------------------------------------------------------------
 *
 * while_command --
 *
 *	Implements the Tcl [while] command.  Entry point for the
 *	while loop.
 *
 *	while TEST BODY
 *
 * Why / How:
 *	Evaluates TEST inline for the first iteration.  If true,
 *	pushes while_postbody and NREvals BODY.  Subsequent iterations
 *	are driven by the while_step / while_postbody callback chain.
 *
 * Results:
 *	TH8_OK when the loop terminates; propagates errors.
 *
 * Side effects:
 *	May push while_postbody and evaluate BODY.
 *
 *----------------------------------------------------------------------
 */

static int
while_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    int rc;
    int iCond;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "while test script");
    }

    /*
     * Evaluate the condition once.  If true, push
     * the NRE continuation and NREval the body.
     */

    rc = Th8_Expr(interp, argv[1], argl[1], NULL, 0);
    if (rc != TH8_OK) return rc;

    rc = Th8_ToBoolean(interp, Th8_GetResult(interp, 0), TH8_NOLEN, &iCond);
    if (rc != TH8_OK) return rc;

    if (!iCond) {
	Th8_SetResult(interp, 0, 0);
	return TH8_OK;
    }

    Th8_NRAddCallback(
        interp, while_postbody, (void *)argv, (void *)argl,
        TH8_INT2PTR(Th8_GetStepCount(interp)), 0);
    return Th8_NREval(interp, argv[2], argl[2], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * while_step --
 *
 *	NRE callback for the [while] loop condition test.  Evaluates
 *	the condition expression.  If true, pushes while_postbody and
 *	NREvals the loop body.  Handles TH8_BREAK by returning TH8_OK.
 *
 * Why / How:
 *	Structurally identical to for_step but without the NEXT script.
 *	Called by the trampoline after while_postbody pushes it for the
 *	next iteration.  Evaluates TEST as a boolean and conditionally
 *	pushes while_postbody before the body evaluation.
 *
 * Results:
 *	TH8_OK when the loop terminates or on break; propagates errors.
 *
 * Side effects:
 *	May push while_postbody and initiate body evaluation.
 *
 *----------------------------------------------------------------------
 */

static int
while_step(Th8_Interp *interp, void *pData[], int rc)
{
    const char **argv = (const char **)pData[0];
    size_t *argl = (size_t *)pData[1];
    int iCond;

    if (rc == TH8_BREAK) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (rc != TH8_OK) return rc;

    rc = Th8_Expr(interp, argv[1], argl[1], NULL, 0);
    if (rc != TH8_OK) return rc;

    rc = Th8_ToBoolean(interp, Th8_GetResult(interp, 0), TH8_NOLEN, &iCond);
    if (rc != TH8_OK) return rc;

    if (!iCond) {
	Th8_SetResult(interp, 0, 0);
	return TH8_OK;
    }

    Th8_NRAddCallback(
        interp, while_postbody, (void *)argv, (void *)argl,
        TH8_INT2PTR(Th8_GetStepCount(interp)), 0);
    return Th8_NREval(interp, argv[2], argl[2], NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * while_postbody --
 *
 *	NRE callback invoked after the [while] loop body completes.
 *	Handles break/continue, applies the empty-body step guard,
 *	and pushes while_step to re-enter the condition test.
 *
 * Why / How:
 *	Mirrors for_postbody but without the NEXT NREval.  After
 *	handling break/continue and the step guard, simply pushes
 *	while_step and returns TH8_OK so the trampoline re-enters
 *	the condition test.
 *
 * Results:
 *	TH8_OK on break; propagates errors; otherwise continues loop.
 *
 * Side effects:
 *	May force a step via Th8_Ready.  Pushes while_step.
 *
 *----------------------------------------------------------------------
 */

static int
while_postbody(Th8_Interp *interp, void *pData[], int rc)
{
    const char **argv = (const char **)pData[0];
    size_t *argl = (size_t *)pData[1];
    th8_int64_t nBefore = (th8_int64_t)(size_t)pData[2];

    if (rc == TH8_BREAK) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (rc == TH8_CONTINUE) rc = TH8_OK;
    if (rc != TH8_OK) return rc;

    if (Th8_GetStepCount(interp) == nBefore) {
	rc = Th8_Ready(interp);
	if (rc != TH8_OK) return rc;
    }

    /*
     * Re-enter condition test.
     */

    Th8_NRAddCallback(interp, while_step, (void *)argv, (void *)argl, 0, 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * foreach_command / foreach_step / foreach_postbody / foreach_cleanup --
 *
 *	Iterate over one or more lists, NRE-converted.
 *
 *	foreach VARLIST LIST ?VARLIST LIST ...? BODY
 *
 *	Supports the full Tcl 8.4 semantics with multiple
 *	varlist-list pairs.  Each pair's variables are bound
 *	from its list in lockstep; the loop continues until
 *	ALL lists are exhausted.  Variables whose list runs
 *	out are set to the empty string.
 *
 * Why / How:
 *	Allocates a Th8_ForeachState holding all varlist/list pairs,
 *	splits each list up front, then uses the three-callback
 *	pattern (foreach_step / foreach_postbody / foreach_cleanup)
 *	to iterate NRE-safely.  The cleanup callback is pushed first
 *	(runs last via LIFO) to guarantee resource release regardless
 *	of how the loop terminates.
 *
 *----------------------------------------------------------------------
 */

/* Th8_ForeachPair, Th8_ForeachState -- declared at top of file. */

#  if defined(TH8_ENABLE_VARIABLES)
static int foreach_step(Th8_Interp *, void *[], int);
static int foreach_postbody(Th8_Interp *, void *[], int);
static int foreach_cleanup(Th8_Interp *, void *[], int);

/*
 *----------------------------------------------------------------------
 *
 * foreach_command --
 *
 *	Implements the Tcl [foreach] command.  Entry point for
 *	multi-variable list iteration.
 *
 *	foreach VARLIST LIST ?VARLIST LIST ...? BODY
 *
 * Why / How:
 *	Parses and splits all varlist/list pairs up front into a
 *	Th8_ForeachState.  Pushes foreach_cleanup (guaranteed
 *	resource release) first, then foreach_step (iteration
 *	driver) second.  The trampoline runs foreach_step first
 *	due to LIFO ordering.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on parse or allocation failure.
 *
 * Side effects:
 *	Allocates Th8_ForeachState and split-list arrays.  Pushes
 *	NRE callbacks.
 *
 *----------------------------------------------------------------------
 */

static int
foreach_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    int rc;
    Th8_ForeachState *pState;
    int nPair;
    int p;

    /*
     * argc must be >= 4 and even: command + (varlist list)+ body.
     * The body is the last arg, and before it are pairs.
     */

    if (argc < 4 || (argc % 2) != 0) {
	return Th8_WrongNumArgs(
	    interp, "foreach varlist list ?varlist list ...?"
	            " body");
    }
    nPair = (argc - 2) / 2;

    pState = (Th8_ForeachState *)TH8_ALLOC(interp, sizeof(Th8_ForeachState));
    if (!pState) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    pState->nPair = nPair;
    pState->aPair = (Th8_ForeachPair *)
        TH8_ALLOC_MUL(interp, (size_t)nPair, sizeof(Th8_ForeachPair));
    if (!pState->aPair) {
	Th8_Free(interp, pState);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    for (p = 0; p < nPair; p++) {
	int iArg = 1 + p * 2;

	rc = Th8_SplitList(
	    interp, argv[iArg], argl[iArg], &pState->aPair[p].azVar,
	    &pState->aPair[p].anVar, &pState->aPair[p].nVar, TH8_LIST_NONE);
	if (rc != TH8_OK) goto fail;
	rc = Th8_SplitList(
	    interp, argv[iArg + 1], argl[iArg + 1], &pState->aPair[p].azValue,
	    &pState->aPair[p].anValue, &pState->aPair[p].nValue,
	    TH8_LIST_NONE);
	if (rc != TH8_OK) goto fail;
	pState->aPair[p].iIndex = 0;
    }

    pState->zBody = argv[argc - 1];
    pState->nBody = argl[argc - 1];

    Th8_NRAddCallback(interp, foreach_cleanup, (void *)pState, 0, 0, 0);
    Th8_NRAddCallback(interp, foreach_step, (void *)pState, 0, 0, 0);
    return TH8_OK;

fail:
    /*
     * Clean up partially-allocated pairs on error.
     */

    {
	int q;
	for (q = 0; q < p; q++) {
	    Th8_Free(interp, pState->aPair[q].azVar);
	    Th8_Free(interp, pState->aPair[q].azValue);
	}
	if (pState->aPair[p].azVar) {
	    Th8_Free(interp, pState->aPair[p].azVar);
	}
    }
    Th8_Free(interp, pState->aPair);
    Th8_Free(interp, pState);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * foreach_step --
 *
 *	NRE callback for the [foreach] iteration step.  Binds variables
 *	for the current iteration, advances the index, pushes
 *	foreach_postbody, and NREvals the loop body.
 *
 * Why / How:
 *	Checks all pairs for remaining values.  If any pair still has
 *	values, binds variables across all pairs for this iteration
 *	(using empty string for exhausted lists), advances each pair's
 *	index, then pushes foreach_postbody and NREvals the body.
 *
 * Results:
 *	TH8_OK when iteration is complete or on break; propagates errors.
 *
 * Side effects:
 *	Sets loop variables.  Pushes foreach_postbody and NREvals body.
 *
 *----------------------------------------------------------------------
 */

static int
foreach_step(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_ForeachState *pState;
    int p;
    int anyRemaining = 0;

    pState = (Th8_ForeachState *)pData[0];

    if (rc == TH8_BREAK) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (rc != TH8_OK) return rc;

    /*
     * Check if any pair has remaining values.
     */

    for (p = 0; p < pState->nPair; p++) {
	Th8_ForeachPair *pp = &pState->aPair[p];

	if (pp->nVar > 0 && pp->iIndex < pp->nValue) {
	    anyRemaining = 1;
	    break;
	}
    }
    if (!anyRemaining) {
	Th8_SetResult(interp, 0, 0);
	return TH8_OK;
    }

    /*
     * Bind variables for this iteration across all pairs.
     */

    for (p = 0; p < pState->nPair; p++) {
	Th8_ForeachPair *pp = &pState->aPair[p];
	int jj;

	for (jj = 0; jj < pp->nVar; jj++) {
	    if (ALWAYS(pp->azVar) && ALWAYS(pp->azValue) &&
	        pp->iIndex + jj < pp->nValue) {
		Th8_SetVar(
		    interp, pp->azVar[jj], pp->anVar[jj],
		    pp->azValue[pp->iIndex + jj],
		    pp->anValue[pp->iIndex + jj]);
	    } else if (pp->azVar) {
		Th8_SetVar(interp, pp->azVar[jj], pp->anVar[jj], "", 0);
	    }
	}
	pp->iIndex += pp->nVar;
    }

    Th8_NRAddCallback(
        interp, foreach_postbody, (void *)pState,
        TH8_INT2PTR(Th8_GetStepCount(interp)), 0, 0);
    return Th8_NREval(interp, pState->zBody, pState->nBody, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * foreach_postbody --
 *
 *	NRE callback invoked after the [foreach] loop body completes.
 *	Handles break/continue, applies the empty-body step guard,
 *	and pushes foreach_step for the next iteration.
 *
 * Why / How:
 *	Identical in structure to for_postbody and while_postbody.
 *	After handling break/continue and the empty-body step guard,
 *	pushes foreach_step and returns TH8_OK to continue iteration.
 *
 * Results:
 *	TH8_OK on break; propagates errors; otherwise continues loop.
 *
 * Side effects:
 *	May force a step via Th8_Ready.  Pushes foreach_step.
 *
 *----------------------------------------------------------------------
 */

static int
foreach_postbody(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_ForeachState *pState;
    th8_int64_t nBefore;

    pState = (Th8_ForeachState *)pData[0];
    nBefore = (th8_int64_t)(size_t)pData[1];

    if (rc == TH8_BREAK) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (rc == TH8_CONTINUE) rc = TH8_OK;
    if (rc != TH8_OK) return rc;

    if (Th8_GetStepCount(interp) == nBefore) {
	rc = Th8_Ready(interp);
	if (rc != TH8_OK) return rc;
    }

    /*
     * Push next step iteration.
     */

    Th8_NRAddCallback(interp, foreach_step, (void *)pState, 0, 0, 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * foreach_cleanup --
 *
 *	Guaranteed cleanup callback for [foreach].  Pushed first (runs
 *	last due to LIFO) by foreach_command.  Frees the
 *	Th8_ForeachState and its sub-allocations regardless of how the
 *	loop terminated.  Converts TH8_BREAK to TH8_OK as a final
 *	safety net.
 *
 * Why / How:
 *	Because foreach_command pushes this callback before
 *	foreach_step, the LIFO ordering guarantees it runs after all
 *	iteration callbacks complete -- even on error or break.  This
 *	ensures no memory leak regardless of the loop exit path.
 *
 * Results:
 *	The incoming rc, with TH8_BREAK converted to TH8_OK.
 *
 * Side effects:
 *	Frees the Th8_ForeachState and split list arrays.
 *
 *----------------------------------------------------------------------
 */

static int
foreach_cleanup(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_ForeachState *pState;

    pState = (Th8_ForeachState *)pData[0];
    {
	int p;
	for (p = 0; p < pState->nPair; p++) {
	    Th8_Free(interp, pState->aPair[p].azVar);
	    Th8_Free(interp, pState->aPair[p].azValue);
	}
    }
    Th8_Free(interp, pState->aPair);
    Th8_Free(interp, pState);

    if (rc == TH8_BREAK) {
	rc = TH8_OK;
    }
    return rc;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8LoopingCommands[] = {
    {1, 0, "for", for_command},
#  if defined(TH8_ENABLE_VARIABLES)
    {1, 0, "foreach", foreach_command},
#  endif
    {1, 0, "while", while_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8LoopingGetCommands --
 *
 *	Return the command table for the looping plugin.
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
th8LoopingGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8LoopingCommands) / sizeof(th8LoopingCommands[0]));

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
	    pCommand[i] = th8LoopingCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_LOOPING */
