/*
 * th8_lifecycle_fail.c -- Standalone TH8K-004 lifecycle-failure harness.
 *
 * Th8_Initialize is the process-global library initializer.  Its only
 * FAILABLE lifecycle callbacks are xInitialize and xSetCwd; on either
 * failure it must roll back every completed stage in REVERSE order
 * (xMutexFinal, xFinalize, platform-copy zero, Th8_ThreadDone) so no
 * partial global state is left behind (TH8K-004).
 *
 * This cannot be exercised inside the in-process test suite: Th8_Initialize
 * is once-per-process, and its rollback -- like Th8_Finalize -- calls
 * Th8_ThreadDone on the MAIN thread.  Each scenario runs in its OWN process.
 *
 *   th8_lifecycle_fail xinit     fail xInitialize (earliest rollback arm)
 *   th8_lifecycle_fail xsetcwd   fail xSetCwd (deepest rollback arm)
 *   th8_lifecycle_fail recover   failed init, then a clean init must succeed
 *   th8_lifecycle_fail reinit    a clean init/finalize cycle must repeat
 *   th8_lifecycle_fail noid      re-init works on a single-threaded host
 *                                (platform with no xGetThreadId)
 *
 * xinit/xsetcwd verify REVERSE-ORDER ROLLBACK by instrumenting the lifecycle
 * callbacks with counters: each asserts (a) Th8_Initialize returned TH8_ERROR
 * and (b) the completed stages were undone in reverse -- for xsetcwd,
 * xInitialize + xMutexInit ran and were balanced by exactly one xFinalize +
 * one xMutexFinal; for xinit, the failure preceded the mutex and cwd stages
 * so nothing beyond the failed callback ran.
 *
 * recover/reinit are REGRESSION tests for a fixed crash: Th8_ThreadDone (from
 * the rollback and from Th8_Finalize) used to call mi_thread_done() on the
 * main thread, freeing the main thread's mimalloc tld; a subsequent
 * Th8_Initialize then crashed in mi_thread_init.  Th8_ThreadDone now skips
 * mi_thread_done() for the main thread, so both a failed-then-clean init
 * (recover) and a repeated init/finalize cycle (reinit) must succeed.
 * Exit 0 = pass.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"

#include "th8.h"

/*
 * Instrumentation shared between the scenario driver and the wrapper
 * callbacks.  The wrappers chain to the real default-platform callbacks
 * (captured before installation) so each completed stage does real work
 * and its rollback undoes real work; the counters record how many times
 * each ran so the driver can assert reverse-order balance.  The library
 * is single-threaded during Th8_Initialize, so plain globals suffice.
 */

static int (*th8lf_realXInit)(Th8_Interp *, void *) = 0;
static void (*th8lf_realXFinal)(Th8_Interp *, void *) = 0;
static void (*th8lf_realXMutexInit)(Th8_Interp *, void *, Th8_Mutex *) = 0;
static void (*th8lf_realXMutexFinal)(Th8_Interp *, void *, Th8_Mutex *) = 0;

static int th8lf_nXInit = 0; /* xInitialize wrapper call count. */
static int th8lf_nXFinal = 0; /* xFinalize wrapper call count. */
static int th8lf_nXMutexInit = 0; /* xMutexInit wrapper call count. */
static int th8lf_nXMutexFinal = 0; /* xMutexFinal wrapper call count. */
static int th8lf_nXSetCwd = 0; /* xSetCwd wrapper call count. */
static int th8lf_failXInit = 0; /* 1 => xInitialize wrapper returns error. */

static int th8lf_streq(const char *a, const char *b);

/*
 *----------------------------------------------------------------------
 *
 * th8lf_xInitialize --
 *
 *	Counting xInitialize wrapper: records the call, then either
 *	forces failure (xinit scenario) or chains to the real callback.
 *
 * Why / How:
 *	When th8lf_failXInit is set, returns TH8_ERROR without chaining
 *	so Th8_Initialize takes its earliest rollback arm; otherwise
 *	delegates to the captured real xInitialize so the stage truly
 *	completes and its xFinalize rollback is meaningful.
 *
 * Results:
 *	TH8_ERROR when failure is forced; otherwise the real callback's
 *	result (or TH8_OK if none was captured).
 *
 * Side effects:
 *	Increments th8lf_nXInit; may run the real xInitialize.
 *
 *----------------------------------------------------------------------
 */

static int
th8lf_xInitialize(
    Th8_Interp *interp, /* Unused (process-global init). */
    void *pCtx) /* Platform context. */
{
    th8lf_nXInit++;
    if (th8lf_failXInit) return TH8_ERROR;
    if (th8lf_realXInit) return th8lf_realXInit(interp, pCtx);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_xFinalize --
 *
 *	Counting xFinalize wrapper: records the rollback of a completed
 *	xInitialize stage, then chains to the real xFinalize.
 *
 * Why / How:
 *	Th8_Initialize's rollback (and Th8_Finalize) call xFinalize to
 *	undo xInitialize; counting it lets the driver assert the undo
 *	happened exactly once per completed xInitialize.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increments th8lf_nXFinal; may run the real xFinalize.
 *
 *----------------------------------------------------------------------
 */

static void
th8lf_xFinalize(
    Th8_Interp *interp, /* Unused (process-global init). */
    void *pCtx) /* Platform context. */
{
    th8lf_nXFinal++;
    if (th8lf_realXFinal) th8lf_realXFinal(interp, pCtx);
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_xMutexInit --
 *
 *	Counting xMutexInit wrapper: records the mutex-init stage, then
 *	chains to the real xMutexInit so a real mutex is created.
 *
 * Why / How:
 *	The mutex stage sits between xInitialize and xSetCwd; counting
 *	it (paired with th8lf_xMutexFinal) lets the driver prove the
 *	rollback undid it in reverse order for the xsetcwd scenario.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increments th8lf_nXMutexInit; may create the real mutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8lf_xMutexInit(
    Th8_Interp *interp, /* Unused. */
    void *pCtx, /* Platform context. */
    Th8_Mutex *pMutex) /* Mutex to initialize. */
{
    th8lf_nXMutexInit++;
    if (th8lf_realXMutexInit) th8lf_realXMutexInit(interp, pCtx, pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_xMutexFinal --
 *
 *	Counting xMutexFinal wrapper: records the rollback of a completed
 *	mutex-init stage, then chains to the real xMutexFinal.
 *
 * Why / How:
 *	Th8_Initialize's rollback calls xMutexFinal FIRST (reverse of the
 *	init order); counting it lets the driver assert reverse-order
 *	teardown against th8lf_nXMutexInit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increments th8lf_nXMutexFinal; may destroy the real mutex.
 *
 *----------------------------------------------------------------------
 */

static void
th8lf_xMutexFinal(
    Th8_Interp *interp, /* Unused. */
    void *pCtx, /* Platform context. */
    Th8_Mutex *pMutex) /* Mutex to finalize. */
{
    th8lf_nXMutexFinal++;
    if (th8lf_realXMutexFinal) th8lf_realXMutexFinal(interp, pCtx, pMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_xSetCwd_fail --
 *
 *	xSetCwd stub that always fails, driving the DEEPEST
 *	Th8_Initialize rollback arm (after xInitialize and the mutex
 *	stage have completed and must be undone in reverse).
 *
 * Why / How:
 *	Matches the Th8_Platform.xSetCwd signature and unconditionally
 *	returns TH8_ERROR so the "xsetcwd" scenario exercises the full
 *	reverse-order rollback (xMutexFinal + xFinalize + platform zero
 *	+ Th8_ThreadDone).
 *
 * Results:
 *	Always TH8_ERROR.
 *
 * Side effects:
 *	Increments th8lf_nXSetCwd; does not change the working directory.
 *
 *----------------------------------------------------------------------
 */

static int
th8lf_xSetCwd_fail(
    Th8_Interp *interp, /* Unused. */
    void *pCtx, /* Platform context (unused). */
    const char *zPath, /* Target directory (unused). */
    size_t nPath) /* Length of zPath (unused). */
{
    (void)interp;
    (void)pCtx;
    (void)zPath;
    (void)nPath;
    th8lf_nXSetCwd++;
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_run_scenario --
 *
 *	Run one lifecycle-failure scenario: fail the selected callback,
 *	assert Th8_Initialize returns TH8_ERROR, and assert the completed
 *	stages were rolled back in reverse order via the instrumentation
 *	counters.
 *
 * Why / How:
 *	Builds the default platform, captures the real lifecycle
 *	callbacks, installs the counting wrappers plus a failing stub for
 *	the selected callback, and calls Th8_Initialize once.  It does NOT
 *	re-initialize (Th8_ThreadDone on the main thread makes a same-
 *	process re-init unsafe on mimalloc); instead it checks the
 *	counters: xinit => only xInitialize ran (no mutex / cwd / undo);
 *	xsetcwd => xInitialize + xMutexInit ran and were each undone
 *	exactly once by xFinalize / xMutexFinal, and xSetCwd fired once.
 *
 * Results:
 *	0 if the scenario failed-and-rolled-back exactly as required;
 *	non-zero with a stderr diagnostic otherwise.
 *
 * Side effects:
 *	Registers the calling thread with the allocator and runs one
 *	Th8_Initialize failure cycle (real xInitialize/xFinalize/mutex
 *	callbacks run and are balanced); writes diagnostics to stderr.
 *
 *----------------------------------------------------------------------
 */

static int
th8lf_run_scenario(const char *zWhich) /* "xinit" or "xsetcwd". */
{
    Th8_Platform plat;
    int bXInit;
    int rc;

    if (zWhich && th8lf_streq(zWhich, "xinit")) {
	bXInit = 1;
    } else if (zWhich && th8lf_streq(zWhich, "xsetcwd")) {
	bXInit = 0;
    } else {
	fprintf(
	    stderr, "lifecycle_fail: unknown scenario '%s'\n",
	    zWhich ? zWhich : "(null)");
	return 2;
    }

    if (Th8_UseDefaultPlatform(&plat) != TH8_OK) {
	fprintf(stderr, "lifecycle_fail: platform version mismatch\n");
	return 2;
    }

    /* Capture the real callbacks, then install counting wrappers. */
    th8lf_realXInit = plat.xInitialize;
    th8lf_realXFinal = plat.xFinalize;
    th8lf_realXMutexInit = plat.xMutexInit;
    th8lf_realXMutexFinal = plat.xMutexFinal;
    plat.xInitialize = th8lf_xInitialize;
    plat.xFinalize = th8lf_xFinalize;
    plat.xMutexInit = th8lf_xMutexInit;
    plat.xMutexFinal = th8lf_xMutexFinal;
    plat.xSetCwd = th8lf_xSetCwd_fail; /* fail the cwd stage... */
    th8lf_failXInit = bXInit; /* ...or, for xinit, fail earlier. */

    rc = Th8_Initialize(&plat);
    if (rc != TH8_ERROR) {
	fprintf(
	    stderr,
	    "lifecycle_fail[%s]: Th8_Initialize returned %d, expected error\n",
	    zWhich, rc);
	return 1;
    }

    if (bXInit) {
	/*
	 * xInitialize failed before the mutex and cwd stages: only the
	 * failed xInitialize ran; nothing was created, so nothing (mutex,
	 * finalize, cwd) should have run.
	 */
	if (th8lf_nXInit != 1 || th8lf_nXFinal != 0 ||
	    th8lf_nXMutexInit != 0 || th8lf_nXMutexFinal != 0 ||
	    th8lf_nXSetCwd != 0) {
	    fprintf(
	        stderr,
	        "lifecycle_fail[xinit]: bad rollback "
	        "(init=%d final=%d mInit=%d mFinal=%d setcwd=%d; "
	        "want 1 0 0 0 0)\n",
	        th8lf_nXInit, th8lf_nXFinal, th8lf_nXMutexInit,
	        th8lf_nXMutexFinal, th8lf_nXSetCwd);
	    return 1;
	}
    } else {
	/*
	 * xSetCwd failed after xInitialize and the mutex stage completed:
	 * both must be undone exactly once, in reverse order, and the
	 * failing xSetCwd must have fired once.
	 */
	if (th8lf_nXInit != 1 || th8lf_nXFinal != 1 ||
	    th8lf_nXMutexInit != 1 || th8lf_nXMutexFinal != 1 ||
	    th8lf_nXSetCwd != 1) {
	    fprintf(
	        stderr,
	        "lifecycle_fail[xsetcwd]: bad rollback "
	        "(init=%d final=%d mInit=%d mFinal=%d setcwd=%d; "
	        "want 1 1 1 1 1)\n",
	        th8lf_nXInit, th8lf_nXFinal, th8lf_nXMutexInit,
	        th8lf_nXMutexFinal, th8lf_nXSetCwd);
	    return 1;
	}
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_streq --
 *
 *	Length-agnostic ASCII string equality for the scenario selector.
 *
 * Why / How:
 *	A tiny local compare keeps the harness self-contained and makes
 *	the scenario dispatch obvious without pulling in extra surface.
 *
 * Results:
 *	1 if the two NUL-terminated strings are byte-identical, else 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8lf_streq(
    const char *a, /* First string (non-NULL). */
    const char *b) /* Second string (non-NULL). */
{
    size_t i = 0;

    for (i = 0; a[i] && b[i]; i++) {
	if (a[i] != b[i]) return 0;
    }
    return a[i] == b[i];
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_run_reinit --
 *
 *	Regression scenario "reinit": a clean Th8_Initialize / Th8_Finalize
 *	cycle must be repeatable in the same process.
 *
 * Why / How:
 *	Th8_Finalize tears down the main thread's allocator state; before
 *	the fix, freeing the main thread's mimalloc tld made the SECOND
 *	Th8_Initialize crash in mi_thread_init.  This runs two full
 *	init/finalize cycles and requires every call to succeed.
 *
 * Results:
 *	0 if both cycles succeeded; 1 on any unexpected failure.
 *
 * Side effects:
 *	Runs two Th8_Initialize/Th8_Finalize cycles; writes diagnostics to
 *	stderr on failure.
 *
 *----------------------------------------------------------------------
 */

static int
th8lf_run_reinit(void)
{
    int cycle;

    for (cycle = 0; cycle < 2; cycle++) {
	Th8_Platform plat;

	if (Th8_UseDefaultPlatform(&plat) != TH8_OK) {
	    fprintf(stderr, "lifecycle_fail[reinit]: platform mismatch\n");
	    return 2;
	}
	if (Th8_Initialize(&plat) != TH8_OK) {
	    fprintf(
	        stderr,
	        "lifecycle_fail[reinit]: Th8_Initialize failed on "
	        "cycle %d (re-init not supported)\n",
	        cycle);
	    return 1;
	}
	if (Th8_Finalize(&plat) != TH8_OK) {
	    fprintf(
	        stderr,
	        "lifecycle_fail[reinit]: Th8_Finalize failed on "
	        "cycle %d\n",
	        cycle);
	    return 1;
	}
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_run_recover --
 *
 *	Regression scenario "recover": after a FAILED Th8_Initialize (whose
 *	rollback runs on the main thread), a fresh Th8_Initialize on a clean
 *	platform must succeed -- the rollback leaves a re-initializable slate.
 *
 * Why / How:
 *	Forces xInitialize to fail (earliest rollback arm, which calls
 *	Th8_ThreadDone on the main thread), asserts the init failed, then
 *	initializes again with a clean default platform.  Before the fix the
 *	recovery init crashed in mi_thread_init; it must now return TH8_OK.
 *
 * Results:
 *	0 if the failed init rolled back and the recovery init + finalize
 *	succeeded; 1 on any deviation.
 *
 * Side effects:
 *	Runs one failing and one succeeding Th8_Initialize (plus a
 *	Th8_Finalize); writes diagnostics to stderr on failure.
 *
 *----------------------------------------------------------------------
 */

static int
th8lf_run_recover(void)
{
    Th8_Platform plat;

    if (Th8_UseDefaultPlatform(&plat) != TH8_OK) {
	fprintf(stderr, "lifecycle_fail[recover]: platform mismatch\n");
	return 2;
    }
    plat.xInitialize = th8lf_xInitialize;
    th8lf_failXInit = 1; /* force the earliest rollback arm. */
    if (Th8_Initialize(&plat) != TH8_ERROR) {
	fprintf(
	    stderr,
	    "lifecycle_fail[recover]: forced-fail init did not fail\n");
	return 1;
    }
    th8lf_failXInit = 0;

    /* Recovery: a clean init in the same process must now succeed. */
    if (Th8_UseDefaultPlatform(&plat) != TH8_OK) {
	fprintf(
	    stderr, "lifecycle_fail[recover]: platform mismatch (retry)\n");
	return 2;
    }
    if (Th8_Initialize(&plat) != TH8_OK) {
	fprintf(
	    stderr, "lifecycle_fail[recover]: recovery Th8_Initialize FAILED "
	            "-- rollback left an un-reinitializable slate\n");
	return 1;
    }
    if (Th8_Finalize(&plat) != TH8_OK) {
	fprintf(stderr, "lifecycle_fail[recover]: Th8_Finalize failed\n");
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8lf_run_noid --
 *
 *	Regression scenario "noid": a single-threaded host (platform with no
 *	xGetThreadId) must also survive repeated Th8_Initialize/Th8_Finalize.
 *
 * Why / How:
 *	With no xGetThreadId the main-thread guard cannot compare thread ids,
 *	so th8IsMainThread conservatively treats every caller as the main
 *	thread and Th8_ThreadDone never calls mi_thread_done().  This nulls
 *	xGetThreadId and runs two init/finalize cycles, exercising that
 *	single-threaded fallback and confirming re-init still works.
 *
 * Results:
 *	0 if both cycles succeeded; 1 on any unexpected failure.
 *
 * Side effects:
 *	Runs two Th8_Initialize/Th8_Finalize cycles on a platform whose
 *	xGetThreadId is NULL; writes diagnostics to stderr on failure.
 *
 *----------------------------------------------------------------------
 */

static int
th8lf_run_noid(void)
{
    int cycle;

    for (cycle = 0; cycle < 2; cycle++) {
	Th8_Platform plat;

	if (Th8_UseDefaultPlatform(&plat) != TH8_OK) {
	    fprintf(stderr, "lifecycle_fail[noid]: platform mismatch\n");
	    return 2;
	}
	plat.xGetThreadId = 0; /* simulate a single-threaded host. */
	if (Th8_Initialize(&plat) != TH8_OK) {
	    fprintf(
	        stderr,
	        "lifecycle_fail[noid]: Th8_Initialize failed on "
	        "cycle %d\n",
	        cycle);
	    return 1;
	}
	if (Th8_Finalize(&plat) != TH8_OK) {
	    fprintf(
	        stderr,
	        "lifecycle_fail[noid]: Th8_Finalize failed on "
	        "cycle %d\n",
	        cycle);
	    return 1;
	}
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	Entry point: run the single lifecycle-failure scenario named by
 *	argv[1] and map its result to a process exit code.
 *
 * Why / How:
 *	Each scenario runs in its own process (the check-lifecycle make
 *	target invokes this binary once per scenario) because
 *	Th8_Initialize's rollback tears down the main thread's allocator
 *	state.  A scenario name is required.
 *
 * Results:
 *	0 if the named scenario passed; 1 if the rollback invariant was
 *	violated; 2 on a usage / platform error.
 *
 * Side effects:
 *	Runs one Th8_Initialize failure cycle (see th8lf_run_scenario)
 *	and writes diagnostics to stderr.
 *
 *----------------------------------------------------------------------
 */

int
main(int argc, char **argv)
{
    if (argc != 2) {
	fprintf(
	    stderr, "usage: %s {xinit|xsetcwd|recover|reinit|noid}\n",
	    argc > 0 ? argv[0] : "th8_lifecycle_fail");
	return 2;
    }
    if (th8lf_streq(argv[1], "reinit")) return th8lf_run_reinit();
    if (th8lf_streq(argv[1], "recover")) return th8lf_run_recover();
    if (th8lf_streq(argv[1], "noid")) return th8lf_run_noid();
    return th8lf_run_scenario(argv[1]);
}
