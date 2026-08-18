/*
 * th8sh.c -- Stock TH8 interactive shell.
 *
 * A thin orchestrator that composes the reusable helpers in
 * `th8_shell.c` into the conventional TH8 shell shape: chroot
 * pre-scan, platform init, interpreter creation, env-driven
 * feature toggles, signal handlers, pledge/unveil hardening,
 * dispatch on argv shape (-eval / script file / REPL), then
 * cleanup.
 *
 * Embedders writing their own shell can copy this file as a
 * starting point and customise the orchestration; the helpers
 * called from `main()` live in `th8_shell.[ch]` and are shipped
 * as source so they can be edited in place too.
 *
 * Modes:
 *   th8sh                  Interactive REPL.
 *   th8sh script.tcl       Execute a script file.
 *   th8sh -eval "code"     Evaluate a string.
 *
 * The shell honours a small family of `TH8SH_*` environment
 * variables that toggle features (loadable extensions, bigint,
 * fatal-signal handlers, expression-grammar features, ...).
 * See `th8_shell.h` and the man page `th8_shell.n` for the
 * full list.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"

#include "th8.h"
#include "th8_mem.h"
#include "th8_shell.h"


/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	Entry point.  See file header for the orchestration
 *	sequence.  Each step delegates to a Th8Shell_* helper or
 *	to a single Th8_* call.
 *
 * Why / How:
 *	Drives the stock shell as a fixed sequence of steps: optional
 *	debugger break, early --version short-circuit, chroot pre-scan
 *	and drop, default-platform construction (plus optional curl
 *	xGetData wrapper), two-phase script-path resolution around
 *	TH8 initialization, interpreter creation and language
 *	registration, env-var feature toggles, signal handlers, CWD
 *	anchoring and auto-path search, pledge/unveil hardening, and
 *	finally dispatch on argv shape to -eval, a script file, or the
 *	interactive REPL.  Cleanup runs through the shared `done` label.
 *
 * Results:
 *	A process exit code: TH8_EXIT_SUCCESS on a clean run (or after
 *	--version), TH8_EXIT_DEBUGGER when a debugger break is taken,
 *	TH8_EXIT_PLATFORM on platform/init failures, TH8_EXIT_FAILURE on
 *	other setup failures, or the exit code produced by the selected
 *	-eval / file / REPL dispatch.
 *
 * Side effects:
 *	Initializes the TH8 library, creates and destroys an
 *	interpreter, may chroot/drop privileges, installs signal
 *	handlers and pledge/unveil restrictions, changes the working
 *	directory, evaluates user scripts, writes diagnostics to stderr,
 *	and frees the resolved script-path allocation.
 *
 *----------------------------------------------------------------------
 */

int
main(int argc, char **argv)
{
    Th8_Platform platform = {0};
    Th8_Interp *interp = NULL;
    int rc = TH8_EXIT_SUCCESS;
    int j = 1;
    char *zScriptPathAlloc = NULL;
    char zAbsBuf[TH8_SHELL_PATH_BUF];
    int bScriptPathResolved = 0;
    int bSignedOnly = 0;
#if !defined(_WIN32) && !defined(WIN32)
    int doChroot = 0;
    const char *zChrootUser = NULL;
#endif

    /*
     * Step 1: optional debugger break BEFORE any TH8 init.
     */

    rc = Th8Shell_MaybeBreakForDebugger();
    if (rc == TH8_EXIT_DEBUGGER) return rc;

    /*
     * Early --version / -V short-circuit.  Done before ANY further
     * init so the flag works even on a half-broken installation
     * (e.g. missing platform libraries that would otherwise abort
     * Th8_Initialize).  Exits with success after printing.
     */

    if (argc > 1 &&
        (strcmp(argv[1], "--version") == 0 ||
         strcmp(argv[1], "-version") == 0 || strcmp(argv[1], "-V") == 0)) {
	Th8Shell_PrintVersion();
	return TH8_EXIT_SUCCESS;
    }

    /*
     * Step 2: pre-scan argv for chroot options and strip them
     * so they do not confuse the main argument processing.
     */

#if !defined(_WIN32) && !defined(WIN32)
    if (Th8Shell_PreScanChrootArgs(argc, argv, &j, &doChroot, &zChrootUser) !=
        TH8_OK) {
	return TH8_EXIT_FAILURE;
    }
    if (doChroot) {
	if (Th8Shell_MaybeChroot(doChroot, zChrootUser) != TH8_OK) {
	    return TH8_EXIT_FAILURE;
	}
    }
#endif

    /*
     * Step 3: build the default platform table and (optionally)
     * install the curl xGetData wrapper.
     */

    if (Th8_UseDefaultPlatform(&platform) != TH8_OK) {
	fprintf(stderr, "th8sh: platform version mismatch\n");
	return TH8_EXIT_PLATFORM;
    }
    Th8Shell_InstallCurlGetData(&platform);

    /*
     * Step 4: phase-1 script path resolution (BEFORE init so
     * we capture the original CWD).
     */

    if (j < argc && argv[j][0] != '-') {
	bScriptPathResolved = Th8Shell_ResolveScriptPathPreInit(
	    &platform, argv[j], zAbsBuf, sizeof(zAbsBuf));
    }

    /*
     * Step 5: initialise TH8, create the interpreter, register
     * the language.
     */

    if (Th8_Initialize(&platform) != TH8_OK) {
	fprintf(stderr, "th8sh: Th8_Initialize failed\n");
	return TH8_EXIT_PLATFORM;
    }
    interp = Th8_CreateInterp(&platform);
    if (!interp) {
	fprintf(stderr, "th8sh: failed to create interpreter\n");
	rc = TH8_EXIT_FAILURE;
	goto done;
    }
    if (Th8_RegisterLanguage(interp) != TH8_OK) {
	fprintf(stderr, "th8sh: failed to register language\n");
	rc = TH8_EXIT_FAILURE;
	goto done;
    }

    /*
     * Step 6: phase-2 script path resolution.
     */

    if (bScriptPathResolved) {
	zScriptPathAlloc =
	    Th8Shell_ResolveScriptPathPostInit(interp, zAbsBuf);
	if (zScriptPathAlloc) {
	    argv[j] = zScriptPathAlloc;
	}
    }

    /*
     * Step 7: apply standard env-var feature toggles.  This
     * call also installs the signed-only policy and registers
     * the `key_tokens` command when crypto is enabled.
     */

    if ((rc = Th8Shell_ApplyStandardEnvFeatures(interp)) != TH8_OK) {
	goto done;
    }
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    bSignedOnly = Th8_IsSignedOnlyEnabled(interp);
#endif

    /*
     * Step 8: install signal handlers (gated by env vars
     * within the helper).
     */

    Th8Shell_InstallSignalHandlers(interp);

    /*
     * Step 9: anchor the CWD at the base path before package
     * search, then run the auto-path search.
     */

    {
	const char *zBase = Th8_GetBasePath();

	if (!zBase) {
	    fprintf(stderr, "th8sh: base path not available\n");
	    rc = TH8_EXIT_PLATFORM;
	    goto done;
	}
	if (Th8_SetCwd(interp, zBase, TH8_NOLEN) != TH8_OK) {
	    fprintf(
	        stderr,
	        "th8sh: cannot set working directory to"
	        " base path: %s\n",
	        zBase);
	    rc = TH8_EXIT_PLATFORM;
	    goto done;
	}
    }
    Th8_AutoPathSearch(interp, 0, 0);

    /*
     * Step 10: apply pledge/unveil hardening (no-op on
     * non-OpenBSD platforms).
     */

    if ((rc = Th8Shell_ApplyPledgeUnveil(interp)) != TH8_OK) {
	goto done;
    }

    /*
     * Step 11: dispatch on argv shape.
     */

    if (j + 1 < argc && strncmp(argv[j], "-eval", strlen(argv[j])) == 0) {
	int exitCode;

	Th8Shell_EvalString(
	    interp, argv[0], argc, (const char **)argv, j + 2, argv[j + 1],
	    TH8_NOLEN, &exitCode);
	rc = exitCode;
    } else if (j < argc && argv[j][0] != '-') {
	int exitCode;

	Th8Shell_EvalFile(
	    interp, argv[j], argc, (const char **)argv, j + 1, argv[j],
	    &exitCode);
	rc = exitCode;
    } else {
	int exitCode;

	Th8Shell_RunRepl(
	    interp, argv[0], argc, (const char **)argv, j, bSignedOnly,
	    &exitCode);
	rc = exitCode;
    }

done:
    Th8_Free(interp, zScriptPathAlloc);
    Th8Shell_Cleanup(interp, &platform);
    return rc;
}
