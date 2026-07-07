/*
 * th8_shell.h --
 *
 * Public declarations for the reusable TH8 shell helpers.
 *
 * The accompanying `th8_shell.c` is intended to be consumed
 * AS SOURCE by embedders building their own custom TH8 shell:
 * drop both files into your build alongside your own `main()`,
 * call into the `Th8Shell_*` API to compose a shell, and
 * customise the parts you want to change (banner, line editor,
 * signal handling, etc.) by editing the source directly.
 *
 * These helpers are NOT part of the public TH8 stubs table --
 * loadable extensions never need shell-flavoured behaviour, so
 * the table is kept focused on the language API.  Embedders
 * link against `th8_shell.c` statically (or include it directly
 * in their build sources).
 *
 * The companion stock shell binary `bin/th8sh` (built from
 * `th8sh.c`) is the reference user of this API.  It is itself
 * a thin orchestrator that calls the helpers below in a
 * conventional order; embedders that want a different shape
 * (e.g. a different banner, an embedded REPL inside a GUI,
 * a daemon mode that never enters the REPL) can write their
 * own `main()` that composes the same helpers.
 *
 * Naming: every public helper uses the `Th8Shell_*` prefix
 * (continuous, no underscore between `Th8` and `Shell`),
 * mirroring the `Th8test_*` and `Th8_RegisterLanguage` tier.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_SHELL_H
#define TH8_SHELL_H

#include "th8.h"


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_MaybeBreakForDebugger --
 *
 *	If the TH8SH_BREAK environment variable is set, pause the
 *	process so a debugger can attach.  On a TTY this prints a
 *	"attach debugger to PID N" prompt and waits for input;
 *	otherwise it raises SIGTRAP (POSIX) or calls DebugBreak
 *	(Win32) so an already-attached debugger receives the
 *	break.
 *
 *	Intended to be the FIRST thing main() calls so the
 *	debugger sees the process before any TH8 init runs.
 *
 *	Returns TH8_OK normally; returns TH8_EXIT_DEBUGGER if the
 *	user typed "exit" at the attach prompt.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_MaybeBreakForDebugger(void);


#if !defined(_WIN32) && !defined(WIN32)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_PreScanChrootArgs --
 *
 *	POSIX-only argv pre-scanner that recognises the shell's
 *	chroot-related options:
 *	  --chroot              force the chroot/privilege-drop path
 *	  --chroot-user NAME    drop to user NAME (default "nobody")
 *
 *	The scan happens BEFORE the main argument processing so
 *	that these options can be stripped from the effective
 *	argv (advancing `*pStart` past them) and so the chroot
 *	itself can run BEFORE Th8_Initialize, which captures the
 *	process CWD.  Running as uid 0 also forces chroot, even
 *	when neither option is supplied.
 *
 *	`*pStart` should be initialised to 1 by the caller; on
 *	return it is the index of the first non-chroot argument
 *	in argv (i.e. where main() should begin its own argv
 *	dispatch).  `*pDoChroot` is set non-zero when the chroot
 *	path should run; `*pzUser` is set to the user name from
 *	`--chroot-user` or NULL when the default should be used.
 *
 *	Returns TH8_OK on success, TH8_ERROR if any out-pointer
 *	is NULL.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_PreScanChrootArgs(
    int argc,
    char **argv,
    int *pStart,
    int *pDoChroot,
    const char **pzUser);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_MaybeChroot --
 *
 *	POSIX-only privilege-drop and chroot helper.  When
 *	`forceChroot` is non-zero OR the process is running as
 *	root, performs the full hardening sequence:
 *	  1. chroot(".").
 *	  2. chdir("/") inside the new root.
 *	  3. Th8_SetBasePath(".").
 *	  4. drop supplementary groups, setgid, setuid to the
 *	     named user (default "nobody").
 *	  5. verify the drop is irreversible (setuid(0) must fail).
 *	  6. update $USER so tcl_platform(user) reflects the new
 *	     identity.
 *
 *	When neither condition applies, this is a no-op.
 *
 *	Returns TH8_OK on success, TH8_ERROR on any chroot or
 *	privilege-drop failure.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_MaybeChroot(int forceChroot, const char *zUser);
#endif /* !_WIN32 */


/*
 *----------------------------------------------------------------------
 *
 * Two-phase script-path resolution.
 *
 * Resolves a script path argument to absolute (phase 1, before
 * Th8_Initialize) and then re-relativises it against the base
 * path (phase 2, after the interpreter exists).  Without this
 * dance, relative paths like `cd tests && ../bin/th8sh foo.tcl`
 * would fail because Th8_Initialize changes the CWD to the
 * base path during its setup.
 *
 * Phase 1 must run BEFORE Th8_Initialize.  It uses the platform
 * table's xGetRealPath callback directly because no interpreter
 * yet exists.  Returns 1 if the path was successfully resolved
 * into the supplied buffer; 0 otherwise (caller treats this as
 * "no special handling").
 *
 * Phase 2 must run AFTER Th8_CreateInterp.  It allocates and
 * returns a new path string on the interpreter's allocator,
 * adjusted to either a base-relative form (when the absolute
 * path is inside the base tree) or the absolute form itself.
 * The caller takes ownership of the returned pointer and is
 * responsible for freeing it via Th8_Free at shutdown.
 *
 * Callers that don't need this behaviour (embedders that always
 * pass absolute paths, or never source from disk at all) can
 * skip both phases entirely.
 *
 * The recommended buffer size for `zAbsBuf` is
 * TH8_SHELL_PATH_BUF (4096 bytes) -- this matches the path
 * limits enforced by the platform layer and is the size the
 * stock shell uses.
 *
 *----------------------------------------------------------------------
 */

#define TH8_SHELL_PATH_BUF 4096

int Th8Shell_ResolveScriptPathPreInit(
    const Th8_Platform *pPlatform,
    const char *zPath,
    char *zAbsBuf,
    size_t nAbsBuf);

char *
Th8Shell_ResolveScriptPathPostInit(Th8_Interp *interp, const char *zAbsBuf);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_InstallCurlGetData --
 *
 *	When TH8_ENABLE_LIBCURL is compiled in, install a wrapper
 *	xGetData on the platform table that dispatches HTTP and
 *	HTTPS URIs to libcurl and falls back to the original
 *	xGetData (filesystem) for everything else.  The original
 *	xGetData pointer is captured internally so the fallback
 *	path keeps working.
 *
 *	No-op on builds without TH8_ENABLE_LIBCURL.
 *
 *	Must be called BEFORE Th8_Initialize so the modified
 *	platform table is used everywhere.
 *
 *----------------------------------------------------------------------
 */

void Th8Shell_InstallCurlGetData(Th8_Platform *pPlatform);


#if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_RegisterKeyTokensCmd --
 *
 *	Register the `key_tokens` script-level command, which
 *	returns the list of public-key tokens currently trusted
 *	by the signed-only policy installed on the interpreter.
 *	The command is bound to the supplied policy context
 *	cookie (returned by Th8_EnableSignedPolicy).
 *
 *	Returns TH8_OK on success.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_RegisterKeyTokensCmd(Th8_Interp *interp, void *pPolicyCtx);
#endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_InstallSignalHandlers --
 *
 *	Install the shell's signal / exception handlers on this
 *	interpreter.
 *
 *	  POSIX:
 *	    SIGINT          -> cancel the running evaluation
 *	                       (gated by TH8SH_NO_CTRLC_HANDLER).
 *	    SIGSEGV/SIGBUS/
 *	    SIGFPE/SIGABRT/
 *	    SIGILL/SIGPIPE/
 *	    SIGTERM/SIGHUP/
 *	    SIGQUIT/SIGSYS  -> _exit(TH8_EXIT_EXCEPTION)
 *	                       (gated by TH8SH_NO_FATAL_HANDLER
 *	                        and the compile-time
 *	                        TH8_NO_FATAL_HANDLER).
 *	  Win32:
 *	    Console-control -> cancel the running evaluation
 *	                       (gated by TH8SH_NO_CTRLC_HANDLER).
 *	    Unhandled exc.  -> ExitProcess(TH8_EXIT_EXCEPTION)
 *	                       (gated by TH8SH_NO_FATAL_HANDLER).
 *
 *	The interpreter pointer is stashed in a static within
 *	the helpers so the async signal handlers can reach it
 *	without parameter passing.  The matching teardown is
 *	performed by Th8Shell_Cleanup.
 *
 *	Returns TH8_OK.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_InstallSignalHandlers(Th8_Interp *interp);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ApplyPledgeUnveil --
 *
 *	Apply the shell's standard `pledge(2)` and `unveil(2)`
 *	hardening profile to this interpreter.  On non-OpenBSD
 *	platforms the underlying Th8_Pledge / Th8_Unveil are
 *	no-ops, so this function is safe to call unconditionally.
 *
 *	Skipped if TH8SH_NO_PLATFORM_SECURITY is set.
 *
 *	Pledge promises (assembled dynamically based on enabled
 *	features):
 *	  stdio rpath tty             -- always
 *	  prot_exec                   -- when [load] is enabled
 *	  proc exec                   -- when TH8SH_YES_TESTLIB
 *	  inet dns                    -- when libcurl is built in
 *
 *	Unveil paths:
 *	  ".", "/dev/urandom", "/dev/tty"  -- always
 *	  "/usr/lib", "/usr/local/lib"     -- when [load] is enabled
 *	  NULL, NULL                       -- lock the unveil set
 *
 *	Returns TH8_OK or TH8_EXIT_PLATFORM on failure.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_ApplyPledgeUnveil(Th8_Interp *interp);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ApplyStandardEnvFeatures --
 *
 *	Apply every standard `TH8SH_*` env-var feature toggle in
 *	one call:
 *
 *	  TH8SH_NO_LOAD            -- disables Th8_EnableLoad
 *	  TH8SH_NO_UNLOAD          -- disables Th8_EnableUnload
 *	  TH8SH_NO_BIGINT          -- disables Th8_EnableBigint
 *	  TH8SH_NO_SCRIPT_SECURITY -- skips Th8_EnableSignedPolicy
 *	  TH8SH_NO_EXPR_FEATURES   -- forces strict expr(n)
 *	  TH8SH_EXPR_FEATURES=...  -- comma-list of feature names
 *	                              (top-comma, var-assign, ...)
 *	  TH8SH_YES_TESTLIB        -- amalgamation: registers
 *	                              Th8test_Init
 *
 *	Each branch is gated by its own compile-time flag so a
 *	build that disables a feature simply skips that branch.
 *	Returns TH8_OK on success or a TH8_EXIT_* code on failure.
 *
 *	Side effects: when TH8_ENABLE_CRYPTOGRAPHY is built in
 *	and signed-only is not disabled, this function ALSO
 *	installs the policy via Th8_EnableSignedPolicy and
 *	stashes the policy context internally so that
 *	Th8Shell_Cleanup can release it; embedders never see the
 *	cookie directly.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_ApplyStandardEnvFeatures(Th8_Interp *interp);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ParseExprFeatures --
 *
 *	Parse a comma-separated list of expression-feature token
 *	names (top-comma, var-assign, none, all, ...) into a
 *	bitwise-OR of TH8_EXPR_* flags.
 *
 *	Returns TH8_OK on success with *pFlags set; returns
 *	TH8_ERROR on an unknown token (with a diagnostic written
 *	to stderr and *pFlags untouched).
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_ParseExprFeatures(
    Th8_Interp *interp,
    const char *zList,
    size_t nList,
    int *pFlags);


#if defined(TH8_AMALGAMATION)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_AmalRegisterTestLib --
 *
 *	Amalgamation builds only.  When TH8SH_YES_TESTLIB is set,
 *	register the embedded testlib commands into the
 *	interpreter (no [load] needed).  Records the registration
 *	internally so Th8Shell_Cleanup will call Th8test_Unload.
 *
 *	Returns TH8_OK.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_AmalRegisterTestLib(Th8_Interp *interp);
#endif


#if defined(TH8_ENABLE_VARIABLES)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_SetArgv --
 *
 *	Set the Tcl-convention `::argv0`, `::argc`, and `::argv`
 *	global variables in the interpreter.  argv0 is the
 *	program or script name; argc/argv reflect the remaining
 *	arguments starting at argv[start].
 *
 *	The argv list is encoded with proper Tcl quoting via
 *	Th8_ListAppend so that arguments containing whitespace
 *	or special characters survive a `[lindex $::argv N]`.
 *
 *----------------------------------------------------------------------
 */

void Th8Shell_SetArgv(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start);
#endif


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_EmitResult --
 *
 *	Print the interpreter result to stdout (rc==TH8_OK) or
 *	stderr (rc==TH8_ERROR), prefixing error output with
 *	"error, line N: " so the user sees the file/line context.
 *	Adds a trailing newline iff anything was emitted.
 *
 *----------------------------------------------------------------------
 */

void Th8Shell_EmitResult(Th8_Interp *interp, int rc);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_PrintBanner --
 *
 *	Print the standard TH8 shell banner (TH8 / copyright /
 *	license-pointer lines) to stdout, followed by the loaded
 *	signing-key tokens when crypto is enabled and signed-only
 *	is active.  Skipped if TH8SH_PLEASE_BE_QUIET is set.
 *
 *	Embedders that want a different banner edit this function
 *	in `th8_shell.c` directly (the function is short and the
 *	full source is shipped).
 *
 *----------------------------------------------------------------------
 */

void Th8Shell_PrintBanner(Th8_Interp *interp, int bSignedOnly);

void Th8Shell_PrintVersion(void);

/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_EvalString --
 *
 *	Evaluate a single string of TH8 script (the `-eval` mode
 *	logic of the stock shell).  Sets ::argv0/::argc/::argv,
 *	calls Th8_EvalTrusted, and emits the result.  The exit-
 *	code argument is set to TH8_EXIT_DEMAND if the script
 *	called [exit], TH8_EXIT_FAILURE on error, or
 *	TH8_EXIT_SUCCESS on normal completion (the caller can
 *	choose to propagate to its own exit).
 *
 *	Returns TH8_OK or TH8_ERROR.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_EvalString(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start,
    const char *zScript,
    size_t nScript,
    int *pExitCode);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_EvalFile --
 *
 *	Evaluate a TH8 script file by path (the script-file mode
 *	logic of the stock shell).  Same semantics and pExitCode
 *	convention as Th8Shell_EvalString.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_EvalFile(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start,
    const char *zPath,
    int *pExitCode);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_RunRepl --
 *
 *	Run the interactive read-eval-print loop until EOF or
 *	[exit].  Multi-line input is accumulated using
 *	Th8_Complete to detect when the command is balanced and
 *	ready to evaluate.  Uses bestline for line editing when
 *	TH8_USE_BESTLINE is compiled in, otherwise falls back to
 *	a fgets-based line reader.
 *
 *	Sets ::argv0/::argc/::argv before the first line.  Prints
 *	the banner.  Sources `lib/th8/init.th8` unless
 *	TH8SH_NO_SCRIPT_LIBRARY is set.
 *
 *	Sets *pExitCode to TH8_EXIT_END_OF_TRANSMISSION on EOF,
 *	TH8_EXIT_DEMAND on [exit], or TH8_EXIT_FAILURE on a fatal
 *	error.  Returns TH8_OK normally.
 *
 *----------------------------------------------------------------------
 */

int Th8Shell_RunRepl(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start,
    int bSignedOnly,
    int *pExitCode);


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_Cleanup --
 *
 *	Centralised teardown.  Reads the internal Th8ShellState
 *	to determine which features were set up by previous
 *	Th8Shell_* calls (signal handlers, signed-only policy,
 *	amalgamation testlib, key_tokens command) and tears down
 *	only those.  Finishes by calling Th8_DeleteInterp and
 *	Th8_Finalize so the embedder's main() can end with a
 *	single teardown line.
 *
 *	Safe to call after partial setup -- features that were
 *	never installed are skipped silently.
 *
 *----------------------------------------------------------------------
 */

void Th8Shell_Cleanup(Th8_Interp *interp, Th8_Platform *pPlatform);


#endif /* TH8_SHELL_H */
