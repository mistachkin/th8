/*
 * th8_events.c -- Event-loop plugin for TH8.
 *
 * Implements the script-level event-loop commands [update] and
 * [vwait], which drain and wait on the per-interp thread-safe
 * event queue.  Events are enqueued by embedders via the public
 * thread-safe API Th8_QueueEvent (in th8_core.c); scripts can
 * NOT enqueue events directly -- that's the security boundary.
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

#if defined(TH8_PLUGIN_EVENTS)

/*
 *======================================================================
 *
 * Helpers
 *
 *======================================================================
 */

#  if defined(TH8_ENABLE_VARIABLES)
/*
 * Capture current variable value into pzCap/pnCap.  pzCap is
 * malloc'd if pVar exists; otherwise *pzCap is NULL and *pbExisted
 * is 0.  Caller frees *pzCap.                                     */
static int
events_capture_value(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    char **pzCap,
    size_t *pnCap,
    int *pbExisted)
{
    *pzCap = 0;
    *pnCap = 0;
    *pbExisted = 0;
    if (!Th8_ExistsVar(interp, zName, nName)) {
	return TH8_OK;
    }
    *pbExisted = 1;
    if (Th8_GetVar(interp, zName, nName) != TH8_OK) {
	/* Var exists per ExistsVar but read failed (could be a
	 * bare array).  Treat as "exists with empty content"
	 * for comparison purposes.                          */
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    {
	const char *zRes;
	size_t nRes;
	zRes = Th8_GetResult(interp, &nRes);
	*pzCap = (char *)TH8_ALLOC_STR(interp, nRes);
	if (!*pzCap) {
	    Th8_ClearResult(interp);
	    return TH8_ERROR;
	}
	/* Th8_GetResult returns the empty-string sentinel rather
	 * than NULL when the interp result is empty, so zRes is
	 * always non-NULL here.  Wrap with ALWAYS. */
	if (ALWAYS(zRes != NULL) && nRes > 0) {
	    Th8_Memcpy(interp, *pzCap, zRes, nRes);
	}
	(*pzCap)[nRes] = 0;
	*pnCap = nRes;
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}

/* Returns 1 if the named variable's current state differs from
 * the captured (zCap, nCap, bExisted), 0 if unchanged, -1 on
 * unrecoverable error.                                       */
static int
events_value_changed(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    const char *zCap,
    size_t nCap,
    int bExisted)
{
    int bExistsNow = Th8_ExistsVar(interp, zName, nName);
    if (bExistsNow != bExisted) {
	return 1; /* create or unset */
    }
    if (!bExistsNow) {
	return 0; /* still doesn't exist */
    }
    if (Th8_GetVar(interp, zName, nName) != TH8_OK) {
	Th8_ClearResult(interp);
	return 0;
    }
    {
	const char *zNow;
	size_t nNow;
	int diff = 0;
	zNow = Th8_GetResult(interp, &nNow);
	if (nNow != nCap) {
	    diff = 1;
	} else if (nNow > 0 && Th8_Memcmp(interp, zNow, zCap, nNow) != 0) {
	    /* Bug 26 family: plain guard, not ALWAYS().  Reaching
	     * here means nNow == nCap; both can legitimately be 0
	     * (a watched variable holding the empty string), in
	     * which case the values are equal and diff stays 0.
	     * ALWAYS(nNow > 0) asserted (abort) under TH8_DEBUG on
	     * that case and mis-evaluated under TH8_OMIT. */
	    diff = 1;
	}
	Th8_ClearResult(interp);
	return diff;
    }
}
#  endif /* TH8_ENABLE_VARIABLES (events_capture/value_changed) */


/*
 *======================================================================
 *
 * [update] -- drain pending events (NRE-aware)
 *
 *	update ?-limit N?
 *
 * Drains queued events on the interp's owning thread.  With
 * -limit N, processes at most N callbacks, then returns.  The
 * default (no -limit) drains the queue completely as it stands at
 * the time of the call, including any events newly enqueued by
 * earlier callbacks during the same [update].  Th8_Ready is
 * polled between callbacks so a Th8_CancelEval surfaces promptly.
 *
 * The command itself does no work; it allocates an UpdateState,
 * pushes update_step onto the NRE callback chain, and returns.
 * The trampoline drives update_step one tick at a time:
 *
 *   update_step:
 *     (a) propagate any error from the previous callback
 *     (b) check Th8_Ready (cancel/freeze)
 *     (c) if limit reached OR queue empty: terminate
 *     (d) drain ONE event synchronously
 *     (e) re-arm (push self again) and return TH8_OK
 *
 * Why NRE-aware: when an event callback's Th8_Eval evaluates a
 * script that runs [yield] (from inside a coroutine that called
 * [update]), the trampoline saves the entire callback chain --
 * INCLUDING our re-armed update_step -- into the coroutine's saved
 * state.  When the coroutine resumes, the chain re-attaches and
 * update_step continues with its remaining work.  A synchronous
 * update would have been on the C stack at yield time, which is
 * incoherent with coroutine save/restore semantics -- same
 * argument that justifies vwait's NRE design (see vwait_step
 * comment).
 *
 *======================================================================
 */

typedef struct UpdateState {
    int nLimit;     /* -1 = no limit; otherwise stop after  */
                    /* nLimit callbacks have been processed. */
    int nProcessed; /* Callbacks processed so far.           */
} UpdateState;

/*
 *----------------------------------------------------------------------
 *
 * update_state_free --
 *
 *	Release a heap-allocated `UpdateState` accumulator
 *	created by `update_command` for the duration of an
 *	`[update]` drain.  NULL-safe so callers do not need
 *	the boilerplate guard at every exit edge of
 *	`update_step`.
 *
 * Parameters:
 *	interp -- live interpreter (for `Th8_Free`).
 *	p      -- state pointer, or NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Frees the state allocation.
 *
 *----------------------------------------------------------------------
 */
static void
update_state_free(Th8_Interp *interp, UpdateState *p)
{
    if (!p) return;
    Th8_Free(interp, p);
}

static int update_step(Th8_Interp *interp, void *pData[], int rc);

/*
 *----------------------------------------------------------------------
 *
 * update_step --
 *
 *	NRE-trampoline tick for the script-visible `[update]`
 *	command: drain at most one queued event per call,
 *	then either re-arm itself or terminate the drain.
 *	Driven by `Th8_NRAddCallback` from `update_command`;
 *	each invocation observes the previous callback's
 *	return code in `rc`.
 *
 *	Each tick does, in order:
 *
 *	  (a) If `rc != TH8_OK`, propagate that code (an
 *	      error from the previous event callback or from
 *	      our own earlier iteration ends the drain).
 *	  (b) `Th8_Ready` check -- surfaces any
 *	      `Th8_CancelEval` that arrived during the prior
 *	      callback.
 *	  (c) Termination test: limit reached (`nLimit > 0
 *	      && nProcessed >= nLimit`) or queue empty.
 *	      Either case clears the result (matching the
 *	      synchronous original `[update]`) and reports
 *	      `TH8_OK`.
 *	  (d) Drain exactly one event via `th8DrainAll(...,
 *	      1, ...)`.  Yielding to the trampoline after
 *	      each callback keeps the NRE chain shape
 *	      consistent so a `[yield]` inside the callback
 *	      can save the chain at a coherent boundary.
 *	  (e) Re-arm: push `update_step` back onto the NRE
 *	      chain so the next tick runs.
 *
 *	On every terminating edge the helper frees the
 *	`UpdateState` accumulator via `update_state_free`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	pData  -- NRE data array; `pData[0]` is the
 *		`UpdateState *`.
 *	rc     -- return code from the previous callback in
 *		the NRE chain.
 *
 * Returns:
 *	`TH8_OK` on continuation (re-armed), terminal `TH8_OK`
 *	on completion (drain finished or limit hit), or the
 *	propagated error from `rc` / `Th8_Ready` /
 *	`th8DrainAll`.
 *
 * Side effects:
 *	Frees the `UpdateState` on every terminal exit.
 *	Drains one event from the per-interp event queue.
 *	Re-pushes onto the NRE chain on continuation paths.
 *
 *----------------------------------------------------------------------
 */
static int
update_step(Th8_Interp *interp, void *pData[], int rc)
{
    UpdateState *p = (UpdateState *)pData[0];
    int drained = 0;
    int drainRc;

    /* (a) If the previous callback (an event drain or our own
     * earlier iteration) returned an error, propagate. */
    if (rc != TH8_OK) {
	update_state_free(interp, p);
	return rc;
    }

    /* (b) cancel/freeze check -- surfaces a Th8_CancelEval that
     * fired during the prior callback (or before we started). */
    if (Th8_Ready(interp) != TH8_OK) {
	update_state_free(interp, p);
	return TH8_ERROR;
    }

    /* (c) terminate when the limit is reached or the queue is
     * empty.  ClearResult here matches the prior synchronous
     * version's behavior (event callbacks could have left a
     * stale result; the script-side [update] reports success
     * with empty result). */
    if ((p->nLimit > 0 && p->nProcessed >= p->nLimit) ||
        !th8AnyEventQueued(interp)) {
	update_state_free(interp, p);
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    /* (d) drain ONE event.  Yielding back to the trampoline
     * after each callback keeps the NRE chain shape consistent
     * (matches the per-tick design used by vwait_step and means
     * a [yield] inside the callback can save the chain at a
     * coherent boundary). */
    drainRc = th8DrainAll(interp, 1, &drained);
    if (drainRc != TH8_OK) {
	update_state_free(interp, p);
	return drainRc;
    }
    p->nProcessed += drained;

    /* (e) re-arm.  If [yield] fired inside the event's callback,
     * NRE state has already been saved with our previous push on
     * top -- coroutine resume re-enters update_step cleanly. */
    Th8_NRAddCallback(interp, update_step, p, NULL, NULL, NULL);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * update_command --
 *
 *	Implements the script-visible `[update ?-limit N?]`
 *	command.  Parses the optional `-limit` argument
 *	(positive integer, defaulting to no limit), checks
 *	that the platform has an event queue, runs an early
 *	`Th8_Ready` cancellation check (so a cancel that
 *	arrived before `[update]` was called surfaces
 *	immediately), allocates the `UpdateState` accumulator,
 *	and hands the drain off to `update_step` via
 *	`Th8_NRAddCallback`.
 *
 *	The command returns immediately -- its C stack frame
 *	goes away.  The NRE trampoline then drives the
 *	asynchronous drain.  See the top-of-section comment
 *	for why the trampoline indirection matters
 *	(coroutine-resume safety and `[yield]` boundary
 *	preservation).
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (1 or 3).
 *	argv   -- argv[0]=`"update"`; argv[1]=`-limit`;
 *		argv[2]=N (decimal positive integer).
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` once the NRE drain is enqueued.  `TH8_ERROR`
 *	on bad arguments, missing event queue, cancellation
 *	already pending, or allocation failure (interpreter
 *	result: diagnostic).
 *
 * Side effects:
 *	Allocates one `UpdateState` (ownership passes to the
 *	NRE chain).  Pushes one callback onto the NRE chain.
 *	May set the interpreter result on the error path.
 *
 *----------------------------------------------------------------------
 */
static int
update_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int nLimit = -1;
    UpdateState *pState;

    (void)ctx;

    if (argc == 1) {
	/* No options. */
    } else if (
        argc == 3 && argl[1] == 6 &&
        Th8_Memcmp(interp, argv[1], "-limit", 6) == 0) {
	if (Th8_ToInt(interp, argv[2], TH8_LEN(argl[2]), &nLimit) != TH8_OK) {
	    return TH8_ERROR;
	}
	if (nLimit < 1) {
	    Th8_SetResultStatic(
	        interp, "update: -limit must be >= 1", TH8_NOLEN);
	    return TH8_ERROR;
	}
    } else {
	return Th8_WrongNumArgs(interp, "update ?-limit N?");
    }

    if (!th8PlatformHasEventQueue(interp)) {
	Th8_SetResultStatic(
	    interp,
	    "event queue not available: platform threading "
	    "primitives not configured",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Pre-drain Th8_Ready check so a cancel that arrived before
     * [update] was called surfaces immediately rather than
     * after the first callback runs.  Same rationale as the
     * pre-drain check in the prior synchronous implementation
     * and as the entry-time check in vwait_command's path. */
    if (Th8_Ready(interp) != TH8_OK) {
	return TH8_ERROR;
    }

    /* Allocate state.  The drain loop is now driven by the NRE
     * trampoline via update_step; this command returns
     * immediately so its C stack frame goes away.  See the
     * top-of-section comment for why this matters. */
    pState = (UpdateState *)TH8_ALLOC(interp, sizeof(UpdateState));
    if (!pState) return TH8_ERROR;
    Th8_Memset(interp, pState, 0, sizeof(*pState));
    pState->nLimit = nLimit;
    pState->nProcessed = 0;

    /* Push the iterator onto the NRE chain.  The trampoline
     * drives the drain from here on. */
    Th8_NRAddCallback(interp, update_step, pState, NULL, NULL, NULL);
    return TH8_OK;
}


/*
 *======================================================================
 *
 * [vwait] -- wait until a variable is signaled (NRE-aware)
 *
 *	vwait ?-timeout MS? varName
 *
 * The command itself does no work; it allocates a VWaitState,
 * pushes vwait_step onto the NRE callback chain, and returns.
 * The trampoline drives vwait_step one tick at a time:
 *
 *   vwait_step:
 *     (a) check Th8_Ready (cancel/freeze)
 *     (b) check whether the variable changed since entry
 *     (c) drain one queued event synchronously
 *     (d) if no event, sleep a bounded slice
 *     (e) re-arm (push self again) and return TH8_OK
 *
 * Why NRE-aware: when an event callback's Th8_Eval evaluates
 * a script that runs [yield] (from inside a coroutine that
 * called [vwait]), the trampoline saves the entire callback
 * chain -- INCLUDING our re-armed vwait_step -- into the
 * coroutine's saved state.  When the coroutine resumes, the
 * chain re-attaches and vwait_step continues.  A synchronous
 * vwait would have been on the C stack at yield time, which
 * is incoherent with coroutine save/restore semantics.
 *
 * Returns the empty string on a successful wait.  On timeout
 * raises a script error of the form `vwait: timeout`.
 *
 *======================================================================
 */

#  if defined(TH8_ENABLE_VARIABLES)
#    define TH8_VWAIT_SLICE_MS 50 /* Max single sleep, like [after]. */

typedef struct VWaitState {
    char *zVar;     /* owned copy of variable name */
    size_t nVar;
    char *zCap;     /* owned copy of initial value */
    size_t nCap;
    int bExisted;
    th8_int64_t deadlineMs; /* -1 = infinite */

    /*
     * Cached platform callbacks (and their pCtx) used by
     * vwait_step.  Captured ONCE at vwait_command entry and
     * checked for NULL there; vwait_step relies on them
     * being valid and never reads interp->pPlatform itself.
     *	 xTimeMs  -- required only when a timeout was given.
     *	 xSleep   -- bounded slice between predicate polls.
     */
    int (*xTimeMs)(Th8_Interp *, void *, th8_int64_t *);
    void (*xSleep)(Th8_Interp *, void *, int nMs);
    void *pPlatCtx;
} VWaitState;

/*
 *----------------------------------------------------------------------
 *
 * vwait_state_free --
 *
 *	Release a `VWaitState` accumulator and its owned
 *	heap copies (`zVar` and the initial captured value
 *	`zCap`).  NULL-safe so callers do not need a guard
 *	at every exit edge of `vwait_step`.
 *
 * Parameters:
 *	interp -- live interpreter (for `Th8_Free`).
 *	p      -- state pointer, or NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Frees `p->zVar`, `p->zCap`, and `p` itself.
 *
 *----------------------------------------------------------------------
 */
static void
vwait_state_free(Th8_Interp *interp, VWaitState *p)
{
    if (!p) return;
    if (p->zVar) Th8_Free(interp, p->zVar);
    if (p->zCap) Th8_Free(interp, p->zCap);
    Th8_Free(interp, p);
}

static int vwait_step(Th8_Interp *interp, void *pData[], int rc);

/*
 *----------------------------------------------------------------------
 *
 * vwait_step --
 *
 *	NRE-trampoline tick for `[vwait]`.  One tick performs:
 *
 *	  (a) Cancel/freeze check (`Th8_Ready`).
 *	  (b) Variable-change check via
 *	      `events_value_changed` against the
 *	      initial-capture stored in the `VWaitState`.
 *	  (c) Drain one queued event (if any) with a limit
 *	      of 1 so the NRE trampoline retains chain-shape
 *	      consistency.  A drain hit re-arms and returns;
 *	      the trampoline reschedules.
 *	  (d) No event waiting -- compute remaining time
 *	      against the deadline (if a `-timeout MS` was
 *	      given) and bound the sleep to
 *	      `TH8_VWAIT_SLICE_MS` (50ms) so cancellation
 *	      latency stays small.  Re-check the predicates
 *	      after the drain attempt in case they raced.
 *	  (e) Bounded `xSleep` and re-arm.
 *
 *	The platform `xTimeMs` / `xSleep` pointers are
 *	captured ONCE in `vwait_command` so `vwait_step`
 *	never reads `interp->pPlatform` directly -- if a
 *	caller swaps the platform mid-wait, the original
 *	callbacks remain.  Per the `[vwait]` design notes
 *	above the function, this also makes `vwait_step`
 *	coroutine-resume-safe: if a `[yield]` runs inside
 *	an event callback, the NRE chain (including the
 *	re-armed `vwait_step`) is saved into the coroutine's
 *	state and re-attaches at resume time.
 *
 *	Gated on `TH8_ENABLE_VARIABLES`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	pData  -- NRE data array; `pData[0]` is the
 *		`VWaitState *`.
 *	rc     -- return code from the previous callback in
 *		the NRE chain.
 *
 * Returns:
 *	`TH8_OK` on continuation (re-armed) or on
 *	successful variable-change (interpreter result
 *	cleared).
 *	`TH8_ERROR` on cancel, predicate evaluation failure,
 *	timeout (interpreter result: `"vwait: timeout"`), or
 *	a propagated event-callback error.
 *
 * Side effects:
 *	May drain one event from the per-interp event queue.
 *	May sleep up to `TH8_VWAIT_SLICE_MS` ms.  Frees the
 *	`VWaitState` on every terminal exit.  Re-pushes
 *	itself onto the NRE chain on continuation paths.
 *
 *----------------------------------------------------------------------
 */
static int
vwait_step(Th8_Interp *interp, void *pData[], int rc)
{
    VWaitState *p = (VWaitState *)pData[0];
    int chg;
    int rem;
    int slice;

    /* If the previous callback (an event drain or our own
     * earlier iteration) returned an error, propagate. */
    if (rc != TH8_OK) {
	vwait_state_free(interp, p);
	return rc;
    }

    /* (a) cancel/freeze check */
    if (Th8_Ready(interp) != TH8_OK) {
	vwait_state_free(interp, p);
	return TH8_ERROR;
    }

    /* (b) variable changed? */
    chg = events_value_changed(
        interp, p->zVar, p->nVar, p->zCap, p->nCap, p->bExisted);
    if (chg < 0) {
	vwait_state_free(interp, p);
	return TH8_ERROR;
    }
    if (chg) {
	vwait_state_free(interp, p);
	Th8_ClearResult(interp);
	return TH8_OK; /* fired -- vwait succeeds */
    }

    /* (c) drain one event if available.  Drain across all
     * registered pStates with a limit of 1 so we yield back
     * to the trampoline after each callback (keeping the
     * NRE chain shape consistent with the prior design).   */
    if (th8AnyEventQueued(interp)) {
	int drained = 0;
	int drainRc = th8DrainAll(interp, 1, &drained);
	if (drainRc != TH8_OK) {
	    vwait_state_free(interp, p);
	    return drainRc;
	}
	/* Re-arm: push ourselves so the trampoline picks us
	 * up next.  If a [yield] fired inside the event's
	 * callback, NRE state has already been saved with our
	 * push from the PREVIOUS iteration on top -- so the
	 * coroutine resume re-enters vwait_step cleanly.  */
	(void)drained;
	Th8_NRAddCallback(interp, vwait_step, p, NULL, NULL, NULL);
	return TH8_OK;
    }

    /* (d) compute remaining time + bounded slice.  Use the
     * xTimeMs cached on the VWaitState at vwait_command
     * entry; never read interp->pPlatform from here.         */
    if (p->deadlineMs >= 0) {
	th8_int64_t now = 0;
	p->xTimeMs(interp, p->pPlatCtx, &now);
	if (now >= p->deadlineMs) {
	    vwait_state_free(interp, p);
	    Th8_SetResultStatic(interp, "vwait: timeout", TH8_NOLEN);
	    return TH8_ERROR;
	}
	rem = (int)(p->deadlineMs - now);
    } else {
	rem = -1;
    }
    slice = (rem < 0 || rem > TH8_VWAIT_SLICE_MS) ? TH8_VWAIT_SLICE_MS : rem;

    /* Re-check predicates after the drain attempt found
     * nothing -- they may have changed during the walk. */
    chg = events_value_changed(
        interp, p->zVar, p->nVar, p->zCap, p->nCap, p->bExisted);
    if (chg < 0) {
	vwait_state_free(interp, p);
	return TH8_ERROR;
    }
    if (chg) {
	vwait_state_free(interp, p);
	Th8_ClearResult(interp);
	return TH8_OK;
    }
    if (Th8_Ready(interp) != TH8_OK) {
	vwait_state_free(interp, p);
	return TH8_ERROR;
    }

    /* Bounded polling sleep.  Per-pState event handles can't
     * be multiplexed cheaply by [vwait] (variable change is
     * its own signal source, distinct from any pState).
     * Polling with a small slice gives <=TH8_VWAIT_SLICE_MS
     * latency for both event arrival and Th8_CancelEval --
     * the same bound the prior design targeted.
     *
     * Invariants at this point:
     *   - p->xSleep was validated non-NULL in vwait_command
     *     (L535-540 hard-fail), so ALWAYS at runtime.
     *   - slice is computed from rem at L426; if the
     *     deadline path applies, the L416 check guarantees
     *     rem = deadlineMs - now > 0; otherwise slice is the
     *     positive constant TH8_VWAIT_SLICE_MS.  Either way,
     *     slice > 0.  ALWAYS. */
    if (ALWAYS(p->xSleep && slice > 0)) {
	p->xSleep(interp, p->pPlatCtx, slice);
    }

    /* Re-arm for the next tick. */
    Th8_NRAddCallback(interp, vwait_step, p, NULL, NULL, NULL);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * vwait_command --
 *
 *	Implements the script-visible `[vwait ?-timeout MS?
 *	varName]` command.  Parses arguments, validates the
 *	event-queue and platform-callback prerequisites,
 *	builds and pre-populates a `VWaitState` accumulator,
 *	and hands the wait loop off to `vwait_step` via the
 *	NRE trampoline.
 *
 *	Critical setup steps performed here (NOT inside
 *	`vwait_step`) because they cannot run safely under
 *	a coroutine resume:
 *
 *	  1. Cache the platform callbacks
 *	     (`xTimeMs` if `-timeout` was given, `xSleep`
 *	     unconditionally) plus their `pCtx` into the
 *	     `VWaitState`.  Missing required callbacks
 *	     fail-fast with a clear diagnostic per
 *	     `feedback_hard_fail_required` -- the embedder
 *	     hears about it at vwait dispatch time, not
 *	     later inside the wait loop.
 *	  2. Allocate the state, copy the variable name,
 *	     and snapshot the initial value via
 *	     `events_capture_value`.  Both "variable exists"
 *	     and "variable absent" are valid starting states
 *	     -- variable creation is one of the signals
 *	     `[vwait]` waits for.
 *	  3. Compute the absolute deadline (in monotonic ms)
 *	     using the captured `xTimeMs` so the wait is not
 *	     subject to wall-clock adjustments.
 *
 *	After enqueueing the NRE callback the function
 *	returns immediately -- its C stack frame goes away
 *	and the trampoline drives the wait one tick at a
 *	time.
 *
 *	Gated on `TH8_ENABLE_VARIABLES`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (2 for `varName`; 4 for
 *		`-timeout MS varName`).
 *	argv   -- argv[0]=`"vwait"`; argv[1]=`-timeout` or
 *		varName; argv[2]=MS (-timeout form);
 *		argv[3]=varName (-timeout form).
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` once the NRE wait is enqueued.  `TH8_ERROR`
 *	on bad arguments, missing event queue, missing
 *	platform callback, allocation failure, or
 *	variable-capture failure (interpreter result:
 *	diagnostic).
 *
 * Side effects:
 *	Allocates one `VWaitState` and its owned name/value
 *	copies (ownership passes to the NRE chain).  Pushes
 *	one callback onto the NRE chain.  May set the
 *	interpreter result on the error path.
 *
 *----------------------------------------------------------------------
 */
static int
vwait_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int nTimeoutMs = -1;
    const char *zVar;
    size_t nVar;
    VWaitState *pState;

    (void)ctx;

    if (argc == 2) {
	zVar = argv[1];
	nVar = TH8_LEN(argl[1]);
    } else if (
        argc == 4 && argl[1] == 8 &&
        Th8_Memcmp(interp, argv[1], "-timeout", 8) == 0) {
	if (Th8_ToInt(interp, argv[2], TH8_LEN(argl[2]), &nTimeoutMs) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
	if (nTimeoutMs < 0) {
	    Th8_SetResultStatic(
	        interp, "vwait: -timeout must be >= 0", TH8_NOLEN);
	    return TH8_ERROR;
	}
	zVar = argv[3];
	nVar = TH8_LEN(argl[3]);
    } else {
	return Th8_WrongNumArgs(interp, "vwait ?-timeout MS? varName");
    }

    if (!th8PlatformHasEventQueue(interp)) {
	Th8_SetResultStatic(
	    interp,
	    "event queue not available: platform threading "
	    "primitives not configured",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Cache the platform pointer + the specific callbacks
     * vwait needs, ONCE here at command entry.  After this
     * point neither vwait_command nor vwait_step reads
     * interp->pPlatform -- they go through the cached
     * pointers exclusively.  If any required callback is
     * missing, fail immediately with a clear message so the
     * embedder finds out at vwait dispatch time, not later
     * inside the wait loop.
     */
    {
	const Th8_Platform *plat = Th8_GetPlatform(interp);
	int (*xTimeMs)(Th8_Interp *, void *, th8_int64_t *) = NULL;
	void (*xSleep)(Th8_Interp *, void *, int) = NULL;
	void *pPlatCtx = NULL;
	if (!plat) {
	    Th8_SetResultStatic(interp, "vwait: no platform", TH8_NOLEN);
	    return TH8_ERROR;
	}
	xTimeMs = plat->xTimeMs;
	xSleep = plat->xSleep;
	pPlatCtx = plat->pCtx;
	if (nTimeoutMs >= 0 && !xTimeMs) {
	    Th8_SetResultStatic(
	        interp, "vwait: -timeout requires platform xTimeMs",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (!xSleep) {
	    Th8_SetResultStatic(
	        interp, "vwait: requires platform xSleep", TH8_NOLEN);
	    return TH8_ERROR;
	}

	/* Allocate state.  Variable must exist OR not -- both
	 * are valid starting states; "create" is a signal
	 * source.                                             */
	pState = (VWaitState *)TH8_ALLOC(interp, sizeof(VWaitState));
	if (!pState) return TH8_ERROR;
	Th8_Memset(interp, pState, 0, sizeof(*pState));
	pState->nVar = nVar;
	pState->zVar = (char *)TH8_ALLOC_STR(interp, nVar);
	if (!pState->zVar) {
	    Th8_Free(interp, pState);
	    return TH8_ERROR;
	}
	Th8_Memcpy(interp, pState->zVar, zVar, nVar);
	pState->zVar[nVar] = '\0';
	if (events_capture_value(
	        interp, pState->zVar, pState->nVar, &pState->zCap,
	        &pState->nCap, &pState->bExisted) != TH8_OK) {
	    vwait_state_free(interp, pState);
	    return TH8_ERROR;
	}

	/* Compute deadline once using the cached xTimeMs. */
	pState->deadlineMs = -1;
	pState->xTimeMs = xTimeMs;
	pState->xSleep = xSleep;
	pState->pPlatCtx = pPlatCtx;
	if (nTimeoutMs >= 0) {
	    th8_int64_t now = 0;
	    xTimeMs(interp, pPlatCtx, &now);
	    pState->deadlineMs = now + (th8_int64_t)nTimeoutMs;
	}
    }

    /* Push the iterator onto the NRE chain.  The trampoline
     * drives the wait from here on; this command returns
     * immediately so its C stack frame goes away.            */
    Th8_NRAddCallback(interp, vwait_step, pState, NULL, NULL, NULL);
    return TH8_OK;
}
#  endif /* TH8_ENABLE_VARIABLES (vwait* block) */


/*
 *======================================================================
 *
 * Plugin registration
 *
 *======================================================================
 */

static Th8_CommandEntry th8EventsCommands[] = {
    {1, 0, "update", update_command},
#  if defined(TH8_ENABLE_VARIABLES)
    {1, 0, "vwait", vwait_command},
#  endif
};

/*
 *----------------------------------------------------------------------
 *
 * th8EventsGetCommands --
 *
 *	Plugin-registration entry point for the `update` /
 *	`vwait` ensemble.  Standard `Th8_CommandEntry` reporter
 *	contract:
 *	  * NULL `pCommand` -- store the entry count in
 *	    `*pnCommand` and return `TH8_OK` (query mode).
 *	  * Non-NULL `pCommand` -- caller-supplied buffer of
 *	    at least `*pnCommand` entries; copy in the table
 *	    and return `TH8_OK`.  Insufficient buffer returns
 *	    `TH8_ERROR` without touching `pCommand`.
 *
 *	The `vwait` slot is gated on `TH8_ENABLE_VARIABLES`;
 *	when variables are disabled, only `update` is reported.
 *
 *	NULL `pnCommand` is always an error.
 *
 * Parameters:
 *	pCommand  -- caller-supplied output buffer or NULL to
 *		query the count only.
 *	pnCommand -- in/out count; receives the table size on
 *		query, must be >= table size on copy.
 *
 * Returns:
 *	`TH8_OK` on success; `TH8_ERROR` on missing
 *	`pnCommand` or insufficient `*pnCommand`.
 *
 * Side effects:
 *	May overwrite `*pnCommand` and `pCommand[0..n-1]`.
 *
 *----------------------------------------------------------------------
 */
int
th8EventsGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8EventsCommands) / sizeof(th8EventsCommands[0]));
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
	    pCommand[i] = th8EventsCommands[i];
	}
    }
    return TH8_OK;
}

#endif /* TH8_PLUGIN_EVENTS */
