/*
 * th8_shell.c --
 *
 * Reusable TH8 shell helpers.  See `th8_shell.h` for the
 * public API contract and the embedder-customisation model.
 *
 * Every function in this file with the `Th8Shell_` prefix is
 * intended to be called from an embedder's `main()`.  The
 * accompanying stock shell binary (`bin/th8sh`, built from
 * `th8sh.c`) is the reference user.
 *
 * Internal state -- the file holds two small statics:
 *   `Th8ShellState_g`        records which features the helpers
 *                            set up so Th8Shell_Cleanup can tear
 *                            them down.
 *   `Th8Shell_pCurrentInterp` lets the async signal handlers
 *                            reach the interpreter pointer
 *                            without parameter passing.
 *
 * Embedders running multiple shells in one process need a
 * different model; the convention is one shell per process
 * (matching the stock binary) and the statics document that
 * constraint explicitly.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_version_gen.h"
#include "th8_meta_defs.h"
#include "th8_meta_libc.h"

#if defined(_WIN32) || defined(WIN32)
#  include "th8_meta_msvc.h"
#  include "th8_meta_win32.h"
#  define TH8SHELL_EXIT_MESSAGE "hit Ctrl-Z, then hit Enter"
#  define TH8SHELL_ISATTY(fd)   _isatty(fd)
#  define TH8SHELL_GETPID       (int)GetCurrentProcessId
#else
#  include "th8_meta_posix.h"
#  define TH8SHELL_EXIT_MESSAGE "hit Ctrl-D"
#  define TH8SHELL_ISATTY(fd)   isatty(fd)
#  define TH8SHELL_GETPID       getpid
#endif

#include "th8.h"
#include "th8_int.h"
#include "th8_mem.h"
#include "th8_shell.h"

#if defined(TH8_USE_BESTLINE)
#  include "bestline.h"
#  define TH8SHELL_READLINE(prompt)  bestline(prompt)
#  define TH8SHELL_ADD_HISTORY(line) bestlineHistoryAdd(line)
#  define TH8SHELL_FREE_LINE(line)   bestlineFree(line)
#else
   /*
    * Fallback line reader (declared just below) used when
    * bestline is not built in.  Returns a malloc'd line that
    * the caller frees with TH8SHELL_FREE_LINE.
    */
static char *Th8Shell_FallbackReadLine(const char *prompt);
#  define TH8SHELL_READLINE(prompt)  Th8Shell_FallbackReadLine(prompt)
#  define TH8SHELL_ADD_HISTORY(line) ((void)0)
#  define TH8SHELL_FREE_LINE(line)   free(line)
#endif


/*
 *----------------------------------------------------------------------
 *
 * Internal state (one shell per process).
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8ShellState {
    int bSignalHandlersInstalled;
    int bSignedPolicyInstalled;
    void *pPolicyCtx;
    int bKeyTokensCmdRegistered;
    int bSignedOnly;            /* set after policy install for banner */
} Th8ShellState;

static Th8ShellState Th8ShellState_g = {0};
static volatile Th8_Interp *Th8Shell_pCurrentInterp = 0;

#if defined(TH8_ENABLE_LIBCURL)
static int (*Th8Shell_origGetData)(
    Th8_Interp *,
    void *,
    const char *,
    size_t,
    char **,
    size_t *) = 0;
#endif


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_MaybeBreakForDebugger --
 *
 *	If the TH8SH_BREAK environment variable is set, pause the
 *	process so a debugger can attach.  On a TTY, prints an
 *	"attach debugger to PID N" prompt and waits for ENTER;
 *	otherwise raises SIGTRAP (POSIX) or calls DebugBreak()
 *	(Win32) so an already-attached debugger receives the
 *	break.  If the user types "exit" at the TTY prompt, the
 *	function returns TH8_EXIT_DEBUGGER so the caller can
 *	abort startup cleanly.
 *
 * Why / How:
 *	Intended to be the very first thing main() calls so the
 *	debugger sees the process before any TH8 init runs.
 *	Reading TH8SH_BREAK directly via getenv (rather than
 *	through the platform xKeyValue layer) is deliberate:
 *	the platform table is not yet built at this point, and
 *	the audit catalog explicitly exempts this single
 *	getenv call via the EXEMPT marker on the line.
 *
 * Results:
 *	TH8_OK if the user did not request an exit, or the env
 *	var was not set (no-op).  TH8_EXIT_DEBUGGER if the user
 *	typed "exit" at the prompt, indicating a clean abort.
 *
 * Side effects:
 *	May write to stderr, may read a line from stdin, may
 *	raise SIGTRAP, or may call DebugBreak().
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_MaybeBreakForDebugger(void)
{
    if (getenv("TH8SH_BREAK")) { /* EXEMPT */
	if (TH8SHELL_ISATTY(0) && TH8SHELL_ISATTY(2)) {
	    char zBuf[20] = {0};

	    fprintf(
	        stderr,
	        "attach debugger to process %d and press"
	        " ENTER to continue... ",
	        TH8SHELL_GETPID());
	    if (fgets(zBuf, 7, stdin) && strcmp(zBuf, "exit\n") == 0) {
		return TH8_EXIT_DEBUGGER;
	    }
	} else {
#if defined(_WIN32) || defined(WIN32)
	    DebugBreak();
#elif defined(SIGTRAP)
	    raise(SIGTRAP);
#endif
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ResolveScriptPathPreInit --
 *
 *	Phase 1 of two-phase script-path resolution.  Captures
 *	the absolute form of `zPath` into `zAbsBuf` BEFORE
 *	Th8_Initialize changes the working directory.
 *
 * Why / How:
 *	Without this dance, relative paths like
 *	`cd tests && ../bin/th8sh foo.tcl` would fail because
 *	Th8_Initialize switches the CWD to the base path during
 *	its setup.  This function uses the platform's
 *	xGetRealPath callback directly because no interpreter
 *	exists yet -- it is the one place where the shell
 *	bypasses the public API for a deliberate, narrow reason.
 *	A NULL xGetRealPath is treated as "no resolution
 *	available", and the caller falls back to passing the
 *	original path through unchanged.
 *
 * Results:
 *	1 on success with zAbsBuf filled with a NUL-terminated
 *	absolute path; 0 if the platform has no xGetRealPath
 *	callback or the resolution failed -- the caller treats
 *	either as "no special handling required".
 *
 * Side effects:
 *	Writes up to nAbsBuf bytes into zAbsBuf.  No allocation,
 *	no I/O outside the platform's resolver implementation.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_ResolveScriptPathPreInit(
    const Th8_Platform *pPlatform,
    const char *zPath,
    char *zAbsBuf,
    size_t nAbsBuf)
{
    if (!pPlatform->xGetRealPath) return 0;
    return pPlatform->xGetRealPath(
               NULL, pPlatform->pCtx, zPath, strlen(zPath), zAbsBuf,
               nAbsBuf) == TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ResolveScriptPathPostInit --
 *
 *	Phase 2 of two-phase script-path resolution.  Given the
 *	absolute path captured by phase 1, allocate and return
 *	a normalised path string on the interpreter's allocator.
 *
 * Why / How:
 *	After Th8_Initialize has anchored the CWD at the base
 *	path, scripts referenced by absolute paths INSIDE the
 *	base tree should be addressable as base-relative for
 *	tidier diagnostics and consistent [info script] output.
 *	This function performs that adjustment: when zAbsBuf
 *	starts with the base path followed by a separator, the
 *	returned string is the relative remainder; otherwise
 *	the returned string is the absolute path verbatim.
 *
 *	On non-Cosmopolitan Windows builds, backslash-to-slash
 *	conversion is applied so downstream script code (and
 *	tcl_platform comparisons) see a uniform separator.
 *
 *	The caller takes ownership of the returned pointer and
 *	is responsible for freeing it via Th8_Free at shutdown.
 *	A NULL return indicates allocation failure; the caller
 *	should treat it as "no path adjustment performed".
 *
 * Results:
 *	A heap-allocated NUL-terminated string owned by the
 *	caller; NULL on allocation failure.
 *
 * Side effects:
 *	Allocates one buffer on the interpreter's allocator.
 *	No I/O.
 *
 *----------------------------------------------------------------------
 */

char *
Th8Shell_ResolveScriptPathPostInit(Th8_Interp *interp, const char *zAbsBuf)
{
    char zBase[TH8_SHELL_PATH_BUF];

    if (Th8_GetRealPath(interp, ".", 1, zBase, sizeof(zBase)) == TH8_OK) {
	size_t nBase = strlen(zBase);
	size_t nAbs = strlen(zAbsBuf);

	if (nBase > 0 && nAbs > nBase + 1 &&
	    TH8_PATH_CMP(zAbsBuf, zBase, nBase) == 0 &&
	    TH8_IS_SEP(zAbsBuf[nBase])) {
	    const char *zRel = zAbsBuf + nBase + 1;
	    size_t nRel = nAbs - nBase - 1;
	    char *zPathAlloc = Th8_Strdup(interp, zRel, nRel);

#if (defined(_WIN32) || defined(WIN32)) && !defined(__COSMOPOLITAN__)
	    if (zPathAlloc) {
		char *p;

		for (p = zPathAlloc; *p; p++) {
		    if (*p == '\\') *p = '/';
		}
	    }
#endif
	    return zPathAlloc;
	}
	return Th8_Strdup(interp, zAbsBuf, nAbs);
    }
    return Th8_Strdup(interp, zAbsBuf, strlen(zAbsBuf));
}


#if !defined(_WIN32) && !defined(WIN32)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_PreScanChrootArgs --
 *
 *	POSIX-only argv pre-scanner for the shell's chroot
 *	options.  Recognises:
 *	  --chroot              force chroot/privilege-drop path
 *	  --chroot-user NAME    drop to user NAME (default
 *	                        "nobody"; takes one argument)
 *
 *	Walks argv left-to-right, increments the caller's start
 *	index past each consumed option, and finally treats a
 *	uid-0 process as an implicit `--chroot` so the hardened
 *	path runs whenever the shell is invoked as root.  Does
 *	NOT mutate argv beyond advancing the start index -- the
 *	options remain in argv but main() skips them via
 *	`*pStart`.
 *
 * Why / How:
 *	The chroot decision must be settled BEFORE Th8_Initialize
 *	runs because chroot(".") plus chdir("/") needs to capture
 *	the original CWD.  Splitting the pre-scan into its own
 *	helper keeps main() short and lets embedders that build
 *	a custom shell either reuse the standard option grammar
 *	or call Th8Shell_MaybeChroot directly with their own
 *	option parsing.
 *
 * Results:
 *	TH8_OK on success.  TH8_ERROR if any out-pointer is
 *	NULL (defensive check; the stock orchestrator passes
 *	non-NULL pointers).
 *
 * Side effects:
 *	None.  Pure parsing of argv plus a getuid() probe.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_PreScanChrootArgs(
    int argc,
    char **argv,
    int *pStart,
    int *pDoChroot,
    const char **pzUser)
{
    int doChroot = 0;
    const char *zChrootUser = NULL;
    int start;
    int i;

    /* Defensive out-pointer checks, split into single-
     * condition guards so clang's MC/DC instrumenter records
     * them as branches instead of a 3-condition compound (whose
     * C-pairs are intrinsic-dead -- main() always passes valid
     * &-of-local addresses).  See FINDINGS.md Finding 005. */
    if (pStart == NULL) return TH8_ERROR;
    if (pDoChroot == NULL) return TH8_ERROR;
    if (pzUser == NULL) return TH8_ERROR;

    start = 1;
    for (i = 1; i < argc; i++) {
	if (strcmp(argv[i], "--chroot") == 0) {
	    doChroot = 1;
	    start++;
	} else if (strcmp(argv[i], "--chroot-user") == 0) {
	    /* Inner length guard split out of the original
	     * 2-condition compound; same short-circuit
	     * semantics. */
	    if (i + 1 < argc) {
		zChrootUser = argv[++i];
		start += 2;
	    }
	}
    }

    if (getuid() == 0) doChroot = 1;

    *pStart = start;
    *pDoChroot = doChroot;
    *pzUser = zChrootUser;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_MaybeChroot --
 *
 *	POSIX-only privilege-drop and chroot helper.  When the
 *	process is running as root OR `forceChroot` is non-zero,
 *	performs the full hardening sequence:
 *	  1. chroot(".") -- confine the filesystem view.
 *	  2. chdir("/") -- ensure CWD is inside the new root.
 *	  3. Th8_SetBasePath(".") -- anchor TH8's base-path
 *	     resolution at the new root.
 *	  4. drop supplementary groups (setgroups), setgid,
 *	     setuid to the named user (default "nobody"; falls
 *	     back to a hard-coded id when /etc/passwd is not
 *	     readable inside the chroot).
 *	  5. verify the privilege drop is irreversible -- if
 *	     setuid(0) succeeds, the drop did not work and the
 *	     process is _exit()ed immediately to prevent further
 *	     execution.
 *	  6. update $USER so that tcl_platform(user) reflects
 *	     the dropped-to identity even when /etc/passwd is
 *	     missing inside the chroot.
 *
 * Why / How:
 *	Defense in depth for server deployments.  Even if a
 *	script exploits a vulnerability that allows arbitrary
 *	file reads, the chroot confines that access to the base
 *	directory tree, and the irreversible privilege drop
 *	prevents regaining root via setuid binaries inside the
 *	chroot.  Each step is checked individually; a partial
 *	failure aborts with TH8_ERROR rather than continuing in
 *	a half-secured state.
 *
 *	Excluded from non-POSIX builds via the surrounding
 *	#if !defined(_WIN32) -- chroot/setuid have no Win32
 *	equivalent at this layer.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on chroot, chdir, base-
 *	path, setgid, or setuid failure.  On verification failure
 *	(setuid to root succeeded after the drop) the function
 *	does NOT return -- it _exit(1)s immediately.
 *
 * Side effects:
 *	Changes the filesystem root, working directory, real
 *	and effective uid/gid, supplementary group set, and the
 *	USER environment variable.  Writes to stderr on errors.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_MaybeChroot(int forceChroot, const char *zUser)
{
    (void)forceChroot;

    /*
     * Step 1: chroot into the current directory.
     */

    if (chroot(".") != 0) {
	perror("th8sh: chroot");
	return TH8_ERROR;
    }

    /*
     * Step 2: chdir to "/" (which is now the old ".").
     */

    if (chdir("/") != 0) {
	perror("th8sh: chdir after chroot");
	return TH8_ERROR;
    }

    /*
     * Step 3: Set the base path AFTER chroot+chdir so that it
     * resolves to "/" (the chroot root), not the pre-chroot
     * absolute path.
     */

    if (Th8_SetBasePath(".", 1) != TH8_OK) {
	fprintf(stderr, "th8sh: Th8_SetBasePath failed\n");
	return TH8_ERROR;
    }

    /*
     * Step 4: Drop root privileges.
     */

    if (getuid() == 0) {
	struct passwd *pw;
	uid_t uid;
	gid_t gid;

	if (!zUser) zUser = "nobody";

	pw = getpwnam(zUser);
	if (pw) {
	    uid = pw->pw_uid;
	    gid = pw->pw_gid;
	} else {
#  if defined(__APPLE__)
	    /* Fallback: nobody is typically -2. */
	    uid = -2;
	    gid = -2;
#  else
	    /* Fallback: nobody is typically 65534. */
	    uid = 65534;
	    gid = 65534;
#  endif
	}

	/* Clear supplementary groups. */
	if (setgroups(0, NULL) != 0) {
	    perror("th8sh: setgroups");
	    /* Non-fatal on some systems. */
	}

	/* Set group ID first (must be done while still root). */
	if (setgid(gid) != 0) {
	    perror("th8sh: setgid");
	    return TH8_ERROR;
	}

	/* Set user ID (irreversible). */
	if (setuid(uid) != 0) {
	    perror("th8sh: setuid");
	    return TH8_ERROR;
	}

	/*
	 * Step 5: Verify we actually dropped privileges.
	 * If setuid to root succeeds, we did not really drop.
	 */

	if (setuid(0) == 0) {
	    fprintf(stderr, "th8sh: FATAL: failed to drop root privileges\n");
	    _exit(1);
	}

	/*
	 * Step 6: Update $USER so tcl_platform(user) reflects the
	 * dropped-to user, even when /etc/passwd is not present
	 * inside the chroot.
	 */

	setenv("USER", zUser, 1);
    }

    return TH8_OK;
}
#endif /* !_WIN32 */


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_curlGetData --
 *
 *	Combined xGetData callback that dispatches HTTP/HTTPS
 *	URIs to libcurl and forwards every other name to the
 *	original (filesystem) xGetData captured at install time.
 *
 * Why / How:
 *	The shell supports `[source http://...]` for the same
 *	convenience reasons that motivate any modern interpreter
 *	to allow URL-shaped script sources.  The dispatcher
 *	checks for the literal "http" prefix (case-sensitive)
 *	and routes accordingly, leaving the fallback path to
 *	handle local files, packaged paths, and anything else
 *	the original xGetData accepted.
 *
 *	A NULL captured callback (which should not happen in
 *	practice but is defensible against pathological
 *	platform tables) maps non-URI names to TH8_ERROR with
 *	an empty buffer rather than silently dereferencing.
 *
 * Results:
 *	TH8_OK on success with *pzData and *pnData filled, or
 *	TH8_ERROR on libcurl failure / missing fallback.
 *
 * Side effects:
 *	May perform network I/O via libcurl for HTTP/HTTPS
 *	URIs.  Allocates the returned data buffer through the
 *	interpreter or the libcurl bridge.
 *
 *----------------------------------------------------------------------
 */

#if defined(TH8_ENABLE_LIBCURL)
static int
Th8Shell_curlGetData(
    Th8_Interp *interp,
    void *pCtx,
    const char *zName,
    size_t nName,
    char **pzData,
    size_t *pnData)
{
    if (nName >= 7 && zName[0] == 'h' && zName[1] == 't' && zName[2] == 't' &&
        zName[3] == 'p') {
	return Th8_GetCurlPlatform()
	    ->xGetData(interp, pCtx, zName, nName, pzData, pnData);
    }
    if (Th8Shell_origGetData) {
	return Th8Shell_origGetData(
	    interp, pCtx, zName, nName, pzData, pnData);
    }
    *pzData = 0;
    *pnData = 0;
    return TH8_ERROR;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_InstallCurlGetData --
 *
 *	When TH8_ENABLE_LIBCURL is built in, replace the supplied
 *	platform table's xGetData with a wrapper that routes
 *	HTTP/HTTPS URIs to libcurl and forwards everything else
 *	to the original callback.  The original callback pointer
 *	is captured into a static within this file so the
 *	fallback path remains reachable from the wrapper.
 *
 * Why / How:
 *	Must be called BEFORE Th8_Initialize: by the time the
 *	library has copied the platform table internally, our
 *	wrapper must already be in place.  No-op on builds
 *	compiled without libcurl, so embedders can call this
 *	unconditionally.
 *
 * Results:
 *	None.  Writes a single function pointer in the supplied
 *	platform structure.
 *
 * Side effects:
 *	Mutates pPlatform->xGetData and the file-static
 *	`Th8Shell_origGetData` capture.
 *
 *----------------------------------------------------------------------
 */

void
Th8Shell_InstallCurlGetData(Th8_Platform *pPlatform)
{
#if defined(TH8_ENABLE_LIBCURL)
    Th8Shell_origGetData = pPlatform->xGetData;
    pPlatform->xGetData = Th8Shell_curlGetData;
#else
    (void)pPlatform;
#endif
}


#if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_keyTokensCmd --
 *
 *	Script-callable implementation of the `key_tokens`
 *	command.  Returns the list of public-key tokens that the
 *	signed-only policy currently trusts.  The bound `ctx`
 *	cookie is the policy context returned by
 *	Th8_EnableSignedPolicy.
 *
 * Why / How:
 *	Exposes the policy context's key inventory at the script
 *	level so administrators can verify which signing keys
 *	are accepted by the running shell instance.
 *	Th8_PolicyGetKeyTokens does the heavy lifting; this
 *	wrapper exists only to satisfy the Th8_CommandProc
 *	signature.
 *
 * Results:
 *	TH8_OK with the key-token list as the interpreter
 *	result; TH8_ERROR on internal failure.
 *
 * Side effects:
 *	Sets the interpreter result.  No allocations beyond
 *	what Th8_PolicyGetKeyTokens performs.
 *
 *----------------------------------------------------------------------
 */

static int
Th8Shell_keyTokensCmd(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)argc;
    (void)argv;
    (void)argl;
    return Th8_PolicyGetKeyTokens(interp, ctx);
}

/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_RegisterKeyTokensCmd --
 *
 *	Register the `key_tokens` script-level command and bind
 *	the supplied policy-context cookie to it.  Records the
 *	registration in Th8ShellState_g so a later call to
 *	Th8Shell_Cleanup can recognise that the command exists
 *	(though Th8_DeleteInterp tears down the command entry
 *	itself; the state flag is informational only).
 *
 * Why / How:
 *	`Th8_EnableSignedPolicy` returns an opaque cookie that
 *	`Th8_PolicyGetKeyTokens` needs to enumerate the trusted
 *	keys.  The shell forwards the cookie via the bound
 *	command's clientData slot so the script-callable
 *	wrapper above doesn't need to find the cookie at call
 *	time.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if Th8_CreateCommand
 *	failed (e.g. allocation failure).  The state flag is
 *	mutated only on success.
 *
 * Side effects:
 *	Adds a `key_tokens` command to the interpreter's global
 *	namespace.  Mutates Th8ShellState_g.bKeyTokensCmdRegistered
 *	on success.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_RegisterKeyTokensCmd(Th8_Interp *interp, void *pPolicyCtx)
{
    int rc = Th8_CreateCommand(
        interp, "key_tokens", Th8Shell_keyTokensCmd, pPolicyCtx, 0, 0);
    if (rc == TH8_OK) {
	Th8ShellState_g.bKeyTokensCmdRegistered = 1;
    }
    return rc;
}
#endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * Signal handlers (installed by Th8Shell_InstallSignalHandlers).
 *
 * Each handler reaches the active interpreter through the
 * file-static `Th8Shell_pCurrentInterp` pointer, which the
 * installer stashes before registering the handlers.  All async-
 * unsafe operations are forbidden inside these handlers; only
 * Th8_CancelEval (which sets a single atomic flag) and _exit
 * are used.
 *
 *----------------------------------------------------------------------
 */

#if defined(_WIN32) || defined(WIN32)

/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_Win32CtrlHandler --
 *
 *	Win32 console control handler for Ctrl-C ONLY.  Calls
 *	Th8_CancelEval with TH8_CANCEL_UNWIND so the currently
 *	running script aborts at the next eval-loop checkpoint.
 *
 * Why / How:
 *	Registered via SetConsoleCtrlHandler.  Returns TRUE for
 *	Ctrl-C to suppress the default handler (which would
 *	terminate the process); returns FALSE for every other
 *	control type, including Ctrl-Break, so the OS handles
 *	them normally.
 *
 *	Ctrl-Break is deliberately NOT handled here -- by design
 *	it is reserved for "exit right now" semantics, providing
 *	the user a guaranteed escape hatch when a wedged or
 *	cancellation-resistant script would otherwise ignore
 *	Ctrl-C.  Letting the default handler take Ctrl-Break
 *	matches the conventional Win32 expectation that
 *	Ctrl-Break is a harder kill than Ctrl-C.
 *
 *	Uses TH8_CANCEL_SIGNAL because the cancel-message
 *	literal has indefinite lifetime.
 *
 * Results:
 *	TRUE if the event was handled (Ctrl-C); FALSE for every
 *	other control type, including Ctrl-Break.
 *
 * Side effects:
 *	Sets the cancel-unwind flag on the active interpreter.
 *	The eval loop notices it at the next Th8_Ready check.
 *
 *----------------------------------------------------------------------
 */

static BOOL WINAPI
Th8Shell_Win32CtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT) {
	if (Th8Shell_pCurrentInterp) {
	    Th8_CancelEval(
	        (Th8_Interp *)Th8Shell_pCurrentInterp,
	        "eval canceled (signal)", TH8_NOLEN,
	        TH8_CANCEL_UNWIND | TH8_CANCEL_SIGNAL);
	}
	return TRUE;
    }
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_Win32ExceptionFilter --
 *
 *	Win32 unhandled exception filter.  Catches all
 *	structured exceptions (access violations, stack
 *	overflows, illegal instructions, etc.) and exits the
 *	process with TH8_EXIT_EXCEPTION.
 *
 * Why / How:
 *	Suppresses the Windows crash dialog so the shell exits
 *	with a distinguishable code instead of waiting for user
 *	interaction.  Test harnesses can detect crashes
 *	programmatically via the exit code.  No re-entrant
 *	logic: ExitProcess is the only call.
 *
 * Results:
 *	Never returns; the EXCEPTION_EXECUTE_HANDLER return
 *	value is dead code that satisfies the compiler.
 *
 * Side effects:
 *	Terminates the process immediately with
 *	TH8_EXIT_EXCEPTION.
 *
 *----------------------------------------------------------------------
 */

static LONG WINAPI
Th8Shell_Win32ExceptionFilter(EXCEPTION_POINTERS *pExInfo)
{
    (void)pExInfo;
    ExitProcess(TH8_EXIT_EXCEPTION);
    return EXCEPTION_EXECUTE_HANDLER;  /* Not reached. */
}

#else /* POSIX */

/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_SigIntHandler --
 *
 *	POSIX signal handler for SIGINT (Ctrl-C).  Calls
 *	Th8_CancelEval to set the cancel-unwind flag on the
 *	active interpreter.
 *
 * Why / How:
 *	The POSIX counterpart to Th8Shell_Win32CtrlHandler.
 *	Th8_CancelEval performs only an atomic store to the
 *	interpreter's cancel field, which is async-signal-safe.
 *	The eval loop polls the flag at every Th8_Ready check
 *	and unwinds cleanly.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the cancel-unwind flag on the active interpreter.
 *
 *----------------------------------------------------------------------
 */

static void
Th8Shell_SigIntHandler(int sig)
{
    (void)sig;
    if (Th8Shell_pCurrentInterp) {
	Th8_CancelEval(
	    (Th8_Interp *)Th8Shell_pCurrentInterp, "eval canceled (signal)",
	    TH8_NOLEN, TH8_CANCEL_UNWIND | TH8_CANCEL_SIGNAL);
    }
}

#  if !defined(TH8_NO_FATAL_HANDLER)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_FatalSignalHandler --
 *
 *	Signal handler for fatal POSIX signals (SIGSEGV, SIGBUS,
 *	SIGFPE, SIGABRT, SIGILL, SIGPIPE, SIGTERM, SIGHUP,
 *	SIGQUIT, optionally SIGSYS).  Immediately exits with
 *	TH8_EXIT_EXCEPTION via _exit.
 *
 * Why / How:
 *	Provides a distinguishable exit code for fatal crashes
 *	so test harnesses can detect them.  The body is
 *	deliberately minimal: re-entrant signal handling for
 *	fatal faults is undefined behaviour in POSIX, so
 *	complex logic risks a secondary crash.  _exit avoids
 *	atexit handlers and stdio buffer flushes that themselves
 *	might crash inside a faulting signal handler.  Disabled
 *	at compile time when TH8_NO_FATAL_HANDLER is defined,
 *	and at runtime by the TH8SH_NO_FATAL_HANDLER env var.
 *
 * Results:
 *	Never returns.
 *
 * Side effects:
 *	Terminates the process with TH8_EXIT_EXCEPTION.
 *
 *----------------------------------------------------------------------
 */

static void
Th8Shell_FatalSignalHandler(int sig)
{
    (void)sig;
    _exit(TH8_EXIT_EXCEPTION);
}
#  endif

#endif /* POSIX */


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_InstallSignalHandlers --
 *
 *	Install the shell's signal / exception handlers on the
 *	supplied interpreter.  The interpreter pointer is
 *	stashed in `Th8Shell_pCurrentInterp` so the async
 *	handlers can reach it without parameter passing.
 *
 *	POSIX:
 *	  SIGINT          -> Th8Shell_SigIntHandler (cancel)
 *	                     gated by TH8SH_NO_CTRLC_HANDLER.
 *	  SIGSEGV/BUS/FPE/
 *	  ABRT/ILL/PIPE/
 *	  TERM/HUP/QUIT   -> Th8Shell_FatalSignalHandler
 *	  (and SIGSYS where defined)
 *	                     gated by TH8SH_NO_FATAL_HANDLER and
 *	                     by the compile-time
 *	                     TH8_NO_FATAL_HANDLER.
 *	Win32:
 *	  Console-control -> Th8Shell_Win32CtrlHandler
 *	                     gated by TH8SH_NO_CTRLC_HANDLER.
 *	  Unhandled exc.  -> Th8Shell_Win32ExceptionFilter
 *	                     gated by TH8SH_NO_FATAL_HANDLER.
 *
 * Why / How:
 *	Each install attempt is checked individually.  POSIX
 *	signal() returns SIG_ERR on failure (e.g. for a signal
 *	number the platform refuses to accept); Win32
 *	SetConsoleCtrlHandler returns FALSE on failure;
 *	SetUnhandledExceptionFilter cannot fail per the API
 *	contract and is counted as installed unconditionally.
 *	A counter tracks how many of the attempted installations
 *	actually succeeded.  The state flag
 *	`bSignalHandlersInstalled` is set only when at least one
 *	install succeeded so Th8Shell_Cleanup will not act on a
 *	failed-or-skipped install.
 *
 *	Returning TH8_ERROR when zero handlers were installed
 *	gives the caller an unambiguous signal that the function
 *	left the process in the same state it was called in --
 *	no matter whether the cause was an OS rejection of every
 *	signal() call or an embedder configuration that disabled
 *	every gate via env vars.  A caller that wants the
 *	"intentionally disabled" case to be silently OK can
 *	ignore the return value (the stock shell does so today).
 *
 * Results:
 *	TH8_OK if at least one handler was installed
 *	successfully.  TH8_ERROR if no handler ended up
 *	installed for any reason.
 *
 * Side effects:
 *	May install several signal / exception handlers.
 *	Mutates `Th8Shell_pCurrentInterp` and (on success)
 *	`Th8ShellState_g.bSignalHandlersInstalled`.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_InstallSignalHandlers(Th8_Interp *interp)
{
    int nInstalled = 0;

    Th8Shell_pCurrentInterp = interp;

#if defined(_WIN32) || defined(WIN32)
    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_CTRLC_HANDLER")) {
	if (SetConsoleCtrlHandler(Th8Shell_Win32CtrlHandler, TRUE)) {
	    nInstalled++;
	}
    }

    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_FATAL_HANDLER")) {
	/*
	 * SetUnhandledExceptionFilter returns the previous
	 * filter pointer; per the Win32 API contract it does
	 * not signal failure, so the install is always counted.
	 */
	(void)SetUnhandledExceptionFilter(Th8Shell_Win32ExceptionFilter);
	nInstalled++;
    }
#else
    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_CTRLC_HANDLER")) {
	if (signal(SIGINT, Th8Shell_SigIntHandler) != SIG_ERR) {
	    nInstalled++;
	}
    }

#  if !defined(TH8_NO_FATAL_HANDLER)
    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_FATAL_HANDLER")) {
	if (signal(SIGSEGV, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGBUS, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGFPE, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGABRT, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGILL, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGPIPE, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGTERM, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGHUP, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
	if (signal(SIGQUIT, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
#    ifdef SIGSYS
	if (signal(SIGSYS, Th8Shell_FatalSignalHandler) != SIG_ERR)
	    nInstalled++;
#    endif
    }
#  endif
#endif /* _WIN32 */

    if (nInstalled == 0) {
	return TH8_ERROR;
    }
    Th8ShellState_g.bSignalHandlersInstalled = 1;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ApplyPledgeUnveil --
 *
 *	Apply the shell's standard pledge(2) and unveil(2)
 *	hardening profile to this interpreter.  Skipped when
 *	TH8SH_NO_PLATFORM_SECURITY is set in the environment.
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
 * Why / How:
 *	The Th8_Pledge / Th8_Unveil platform calls are no-ops on
 *	non-OpenBSD systems, so this function compiles and runs
 *	on every supported platform; it just has nothing to
 *	enforce off-OpenBSD.  The promise string is built up via
 *	plain sprintf into a fixed buffer; the longest plausible
 *	combination ("stdio rpath tty prot_exec proc exec inet
 *	dns") is well under the 512-byte buffer.
 *
 *	Each step's failure path is fatal: a hardening helper
 *	that silently degrades is worse than one that aborts
 *	loudly so the embedder can decide whether to proceed.
 *
 * Results:
 *	TH8_OK on success or when skipped via the env var;
 *	TH8_EXIT_PLATFORM if any unveil or pledge call fails.
 *
 * Side effects:
 *	Locks the process's filesystem visibility (unveil) and
 *	system-call surface (pledge) on OpenBSD.  Writes a
 *	diagnostic to stderr on failure.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_ApplyPledgeUnveil(Th8_Interp *interp)
{
    char zPromises[512] = {0};
    char *p = zPromises;

    if (Th8_DoesEnvExist(interp, "TH8SH_NO_PLATFORM_SECURITY")) {
	return TH8_OK;
    }

    if (Th8_Unveil(interp, ".", "rx") != TH8_OK ||
        Th8_Unveil(interp, "/dev/urandom", "r") != TH8_OK ||
        Th8_Unveil(interp, "/dev/tty", "r") != TH8_OK) {
	fprintf(stderr, "th8sh: initial unveil failed\n");
	return TH8_EXIT_PLATFORM;
    }

#if defined(TH8_ENABLE_LOAD)
    if (Th8_IsLoadEnabled(interp)) {
	if (Th8_Unveil(interp, "/usr/lib", "r") != TH8_OK ||
	    Th8_Unveil(interp, "/usr/local/lib", "r") != TH8_OK) {
	    fprintf(stderr, "th8sh: unveil for [load] failed\n");
	    return TH8_EXIT_PLATFORM;
	}
    }
#endif

    if (Th8_Unveil(interp, NULL, NULL) != TH8_OK) {
	fprintf(stderr, "th8sh: final unveil failed\n");
	return TH8_EXIT_PLATFORM;
    }

    /* Core: always needed. */
    p += sprintf(p, "stdio rpath tty");

    /* Binary loading: dlopen requires prot_exec. */
#if defined(TH8_ENABLE_LOAD)
    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_LOAD") &&
        Th8_IsLoadEnabled(interp)) {
	p += sprintf(p, " prot_exec");
    }
#endif

    /* Test library: __test_only_exec needs fork+exec. */
    if (Th8_DoesEnvExist(interp, "TH8SH_YES_TESTLIB")) {
	p += sprintf(p, " proc exec");
    }

    /* libcurl: network access. */
#if defined(TH8_ENABLE_LIBCURL)
    p += sprintf(p, " inet dns");
#endif

    if (Th8_Pledge(interp, zPromises, NULL) != TH8_OK) {
	fprintf(stderr, "th8sh: pledge failed\n");
	return TH8_EXIT_PLATFORM;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ParseExprFeatures --
 *
 *	Parse a comma-separated list of expression-feature
 *	token names ("top-comma", "var-assign", "none", "all")
 *	into a bitwise-OR of TH8_EXPR_* flags.  An empty input
 *	yields TH8_EXPR_NONE; whitespace is NOT trimmed (callers
 *	pass already-trimmed lists).
 *
 * Why / How:
 *	Used by the env-var path (TH8SH_EXPR_FEATURES).  Centralising
 *	the parser here means token-name additions have a single source
 *	of truth and stay in lock-step between the two callers.
 *
 *	Unknown tokens are surfaced as a diagnostic on stderr
 *	and produce TH8_ERROR rather than being silently
 *	ignored; that prevents typos in env vars from masking
 *	the user's intent.
 *
 * Results:
 *	TH8_OK with *pFlags set to the parsed bitmask, or
 *	TH8_ERROR on an unknown token (with a diagnostic
 *	written to stderr; *pFlags is left untouched).
 *
 * Side effects:
 *	None on success.  On unknown-token error, writes one
 *	line to stderr.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_ParseExprFeatures(
    Th8_Interp *interp,
    const char *zList,
    size_t nList,
    int *pFlags)
{
    int flags = TH8_EXPR_NONE;
    size_t iStart = 0;
    size_t i;

    while (iStart < nList) {
	size_t nTok;

	i = iStart;
	while (i < nList && zList[i] != ',')
	    i++;
	nTok = i - iStart;

	if (nTok == 4 && Th8_Memcmp(interp, &zList[iStart], "none", 4) == 0) {
	    /* explicit none: no-op */
	} else if (
	    nTok == 3 && Th8_Memcmp(interp, &zList[iStart], "all", 3) == 0) {
	    flags |= TH8_EXPR_ALL;
	} else if (
	    nTok == 9 &&
	    Th8_Memcmp(interp, &zList[iStart], "top-comma", 9) == 0) {
	    flags |= TH8_EXPR_TOP_COMMA;
	} else if (
	    nTok == 10 &&
	    Th8_Memcmp(interp, &zList[iStart], "var-assign", 10) == 0) {
	    flags |= TH8_EXPR_VAR_ASSIGN;
	} else if (nTok > 0) {
	    fprintf(
	        stderr,
	        "th8sh: unknown TH8SH_EXPR_FEATURES "
	        "token: %.*s\n",
	        (int)nTok, &zList[iStart]);
	    return TH8_ERROR;
	}

	iStart = i + 1;
    }

    *pFlags = flags;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_ApplyStandardEnvFeatures --
 *
 *	Apply every standard `TH8SH_*` env-var feature toggle
 *	that the stock shell honors, in one call:
 *
 *	  TH8SH_NO_LOAD            -- skip Th8_EnableLoad
 *	  TH8SH_NO_UNLOAD          -- skip Th8_EnableUnload
 *	  TH8SH_NO_BIGINT          -- skip Th8_EnableBigint
 *	  TH8SH_NO_SCRIPT_SECURITY -- skip Th8_EnableSignedPolicy
 *	  TH8SH_NO_EXPR_FEATURES   -- force strict expr(n)
 *	  TH8SH_EXPR_FEATURES=...  -- comma-list of feature names
 *	                              (top-comma, var-assign, ...)
 *
 *	When the signed-only policy is enabled, this function
 *	ALSO calls Th8Shell_RegisterKeyTokensCmd so the
 *	`key_tokens` script-level command is available.  The
 *	policy context cookie is stashed in Th8ShellState_g so
 *	Th8Shell_Cleanup can later release it.
 *
 * Why / How:
 *	Bundling every TH8SH_* gate behind one call keeps
 *	main() short and gives embedders a one-stop helper for
 *	the conventional shell shape.  Each branch is gated by
 *	its own compile-time flag (TH8_ENABLE_LOAD, etc.) so a
 *	build that disables a feature simply skips that branch.
 *
 *	Every TH8SH_NO_* gate is opt-OUT (default ON), matching
 *	the existing shell convention.  TH8SH_EXPR_FEATURES is
 *	opt-IN (default OFF / strict) per the design decision
 *	captured in the standard's section 9.3.2.
 *
 * Results:
 *	TH8_OK on success.  TH8_EXIT_SECURITY if signed-only
 *	policy install fails.  TH8_EXIT_PLATFORM if the expr-
 *	features parser rejects an unknown token.
 *
 * Side effects:
 *	May enable [load] / unload / bigint / signed-only /
 *	expression features on the interpreter.  May install
 *	the `key_tokens` command.  Mutates Th8ShellState_g.
 *	May write to stderr on policy-install failure or
 *	unknown-token diagnostic.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_ApplyStandardEnvFeatures(Th8_Interp *interp)
{
#if defined(TH8_ENABLE_LOAD)
    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_LOAD")) {
	Th8_EnableLoad(interp, 1);
    }

    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_UNLOAD")) {
	Th8_EnableUnload(interp, TH8_UNLOAD_OK | TH8_UNLOAD_DANGEROUS);
    }
#endif

#if defined(TH8_ENABLE_BIGINT)
    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_BIGINT")) {
	Th8_EnableBigint(interp, 1);
    }
#endif

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_SCRIPT_SECURITY")) {
	if (Th8_EnableSignedPolicy(interp, &Th8ShellState_g.pPolicyCtx, 1) ==
	    TH8_OK) {
	    Th8ShellState_g.bSignedPolicyInstalled = 1;
	    Th8ShellState_g.bSignedOnly = 1;
	    Th8Shell_RegisterKeyTokensCmd(interp, Th8ShellState_g.pPolicyCtx);
	} else {
	    fprintf(stderr, "th8sh: could not enable signed-only policy\n");
	    return TH8_EXIT_SECURITY;
	}
    }
#endif

    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_EXPR_FEATURES") &&
        Th8_DoesEnvExist(interp, "TH8SH_EXPR_FEATURES")) {
	char *zFeat = Th8_GetEnv(interp, "TH8SH_EXPR_FEATURES");

	if (zFeat) {
	    int flags = TH8_EXPR_NONE;
	    size_t nFeat = 0;

	    while (zFeat[nFeat])
		nFeat++;
	    if (Th8Shell_ParseExprFeatures(interp, zFeat, nFeat, &flags) !=
	        TH8_OK) {
		Th8_Free(interp, zFeat);
		return TH8_EXIT_PLATFORM;
	    }
	    Th8_Free(interp, zFeat);
	    Th8_SetExprFeatures(interp, flags);
	}
    }

    return TH8_OK;
}


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
 * Why / How:
 *	Every Tcl-style shell exposes these three variables so
 *	scripts can inspect their invocation.  The argv list is
 *	encoded with proper Tcl quoting via Th8_ListAppend so
 *	that arguments containing whitespace, braces, or other
 *	list-special characters survive a `[lindex $::argv N]`
 *	round trip.  argc is rendered via snprintf into a small
 *	stack buffer (sufficient for any plausible 32-bit count).
 *
 *	Compiled out entirely when TH8_ENABLE_VARIABLES is not
 *	defined, so micro-builds without variable support do
 *	not waste code space on the helper.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets ::argv0, ::argc, and ::argv in the interpreter.
 *	Allocates and frees a temporary list buffer.
 *
 *----------------------------------------------------------------------
 */

void
Th8Shell_SetArgv(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start)
{
    char *zList = 0;
    size_t nList = 0;
    int remaining = argc - start;
    char zBuf[20];
    int i;

    Th8_SetVar(interp, "::argv0", TH8_NOLEN, zArgv0, TH8_NOLEN);

    snprintf(zBuf, sizeof(zBuf), "%d", remaining);
    Th8_SetVar(interp, "::argc", TH8_NOLEN, zBuf, TH8_NOLEN);

    for (i = start; i < argc; i++) {
	Th8_ListAppend(interp, &zList, &nList, argv[i], TH8_NOLEN);
    }
    Th8_SetVar(
        interp, "::argv", TH8_NOLEN, zList ? zList : "", zList ? nList : 0);
    Th8_Free(interp, zList);
}
#endif /* TH8_ENABLE_VARIABLES */


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_EmitResult --
 *
 *	Print the interpreter result to stdout (rc == TH8_OK)
 *	or stderr (otherwise), prefixing the error case with
 *	"error, line N: " so the user sees the file/line
 *	context.  Adds a trailing newline iff anything was
 *	emitted.
 *
 * Why / How:
 *	The conventional Tcl-shell "print result, error
 *	prefix" output style.  Emitting nothing when both rc
 *	is TH8_OK and the result string is empty avoids
 *	cluttering the REPL with blank lines.
 *
 *	The shell deliberately uses libc stdio rather than
 *	the platform's xOutput layer because shells are the
 *	one legitimate consumer of process-level stdout/stderr;
 *	tools/audit_patterns.tcl exempts this file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to stdout (success) or stderr (error).
 *
 *----------------------------------------------------------------------
 */

void
Th8Shell_EmitResult(Th8_Interp *interp, int rc)
{
    int nOutput = 0;
    size_t nResult = 0;
    FILE *pFile = (rc == TH8_OK) ? stdout : stderr;
    const char *zResult = Th8_GetResult(interp, &nResult);

    if (rc != TH8_OK) {
	pFile = stderr;
	fprintf(pFile, "error, line %d: ", Th8_GetErrorLine(interp));
	nOutput++;
    }
    if (nResult > 0) {
	fwrite(zResult, 1, nResult, pFile);
	nOutput++;
    }
    if (nOutput > 0) {
	fputc('\n', pFile);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_PrintVersion --
 *
 *	Print version information to stdout and return.  The portable
 *	counterpart of the Win32 VERSIONINFO resource block: makes the
 *	same fields (TH8_PATCH_LEVEL, TH8_SOURCE_ID, TH8_SOURCE_TIMESTAMP,
 *	etc.) queryable from any platform via `th8sh --version`.
 *
 *----------------------------------------------------------------------
 */

void
Th8Shell_PrintVersion(void)
{
    fprintf(stdout, "TH8 v" TH8_PATCH_LEVEL);
    fprintf(
        stdout,
        " check-in " TH8_SOURCE_ID " at " TH8_SOURCE_TIMESTAMP
        " with tags \"" TH8_SOURCE_TAGS "\" via " TH8_SOURCE_VCS "\n");
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_PrintBanner --
 *
 *	Print the standard TH8 shell banner (TH8 / copyright /
 *	license-pointer lines) to stdout, followed by the loaded
 *	signing-key tokens when crypto is enabled and signed-only
 *	is active.  Skipped entirely when TH8SH_PLEASE_BE_QUIET
 *	is set in the environment.
 *
 * Why / How:
 *	The banner is intentionally short and source-edit-
 *	friendly: embedders that want to rebrand simply edit
 *	this function (and rebuild) rather than parameterising
 *	a string.  The "exit shell" hint in the first line is
 *	platform-dependent (Ctrl-Z+Enter on Windows, Ctrl-D on
 *	POSIX) via the TH8SHELL_EXIT_MESSAGE macro defined at
 *	the top of this file.
 *
 *	When the signed-only policy is active, the Root and
 *	Zero key tokens are printed so administrators can verify
 *	at-a-glance which signing identities the running shell
 *	will accept.  The Test key (if compiled in) gets a
 *	prominent WARNING prefix so a misconfigured production
 *	build is hard to miss.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes several lines to stdout.  No allocation.
 *
 *----------------------------------------------------------------------
 */

void
Th8Shell_PrintBanner(Th8_Interp *interp, int bSignedOnly)
{
    if (Th8_DoesEnvExist(interp, "TH8SH_PLEASE_BE_QUIET")) {
	return;
    }

    printf("TH8: To exit shell, " TH8SHELL_EXIT_MESSAGE ".\n");
    printf("Copyright (c) by Joe Mistachkin.  All rights reserved.\n");
    printf("See the file \"license.terms\" for more information.\n");

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    if (bSignedOnly) {
	char zToken[17] = {0};

	if (Th8_GetPublicKeyRootToken(interp, zToken) == TH8_OK) {
	    printf("Root public key token enabled: %s\n", zToken);
	}
	if (Th8_GetPublicKeyZeroToken(interp, zToken) == TH8_OK) {
	    printf("Script public key token enabled: %s\n", zToken);
	}
#  if defined(TH8_ENABLE_TEST_KEY)
	if (Th8_GetPublicKeyTestToken(interp, zToken) == TH8_OK) {
	    printf("WARNING: Test public key token enabled: %s\n", zToken);
	}
#  endif
    }
#else
    (void)bSignedOnly;
#endif
    printf("\n");
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_EvalString --
 *
 *	Evaluate a single string of TH8 script (the `-eval`
 *	mode logic of the stock shell): set ::argv0/::argc/
 *	::argv, call Th8_EvalTrusted on the script bytes, and
 *	emit the result to stdout/stderr.  The pExitCode
 *	argument receives TH8_EXIT_DEMAND if the script called
 *	[exit], TH8_EXIT_FAILURE on parse/eval error, or
 *	TH8_EXIT_SUCCESS on normal completion.
 *
 * Why / How:
 *	One-call helper for the most common embedded-shell
 *	pattern: "evaluate this command-line string and exit
 *	with a sensible status code".  Embedders that want
 *	different result-emission can call Th8_EvalTrusted /
 *	Th8_GetResult / Th8_IsExited directly; this function
 *	is convenience over flexibility.
 *
 *	Th8_EvalTrusted is used (not Th8_Eval) because -eval's
 *	argument is supplied by the embedder, not loaded from
 *	disk -- it is, by definition, trusted code.  When the
 *	signed-only policy is active, that distinction matters.
 *
 * Results:
 *	The TH8_OK / TH8_ERROR return from Th8_EvalTrusted.
 *	Caller normally propagates *pExitCode to its own exit.
 *
 * Side effects:
 *	Sets ::argv0/::argc/::argv (when variables are built
 *	in).  Runs the supplied script.  Writes the result or
 *	error to stdout/stderr.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_EvalString(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start,
    const char *zScript,
    size_t nScript,
    int *pExitCode)
{
    int rc;

#if defined(TH8_ENABLE_VARIABLES)
    Th8Shell_SetArgv(interp, zArgv0, argc, argv, start);
#else
    (void)zArgv0;
    (void)argc;
    (void)argv;
    (void)start;
#endif

    rc = Th8_EvalTrusted(interp, 0, zScript, nScript, NULL, 0);
    if (rc == TH8_OK) {
	Th8Shell_EmitResult(interp, rc);
	if (pExitCode) {
	    *pExitCode = Th8_IsExited(interp) ? TH8_EXIT_DEMAND
	                                      : TH8_EXIT_SUCCESS;
	}
    } else {
	Th8Shell_EmitResult(interp, rc);
	if (pExitCode) {
	    *pExitCode = TH8_EXIT_FAILURE;
	}
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_EvalFile --
 *
 *	Evaluate a TH8 script file by path (the script-file
 *	mode logic of the stock shell): set ::argv0/::argc/
 *	::argv, call Th8_EvalFile on the path, and emit the
 *	result on error.  Same exit-code convention as
 *	Th8Shell_EvalString: TH8_EXIT_DEMAND if the script
 *	called [exit], TH8_EXIT_FAILURE on error, or
 *	TH8_EXIT_SUCCESS on normal completion.
 *
 * Why / How:
 *	Counterpart to Th8Shell_EvalString for the "th8sh
 *	script.tcl" command-line shape.  The success path is
 *	intentionally silent (does NOT call Th8Shell_EmitResult)
 *	because Tcl convention treats a script's last
 *	expression result as scaffolding rather than user-
 *	visible output -- scripts that want to print something
 *	use [puts] explicitly.  The error path DOES emit the
 *	result so the user sees the diagnostic that caused the
 *	failure.
 *
 *	Th8_EvalFile honors the signed-only policy when active,
 *	so this helper works correctly in both trusted and
 *	signed-only configurations without further plumbing.
 *
 * Results:
 *	The TH8_OK / TH8_ERROR return from Th8_EvalFile.
 *
 * Side effects:
 *	Sets ::argv0/::argc/::argv (when variables are built
 *	in).  Reads and runs the file at zPath.  Writes the
 *	error message (if any) to stderr.
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_EvalFile(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start,
    const char *zPath,
    int *pExitCode)
{
    int rc;

#if defined(TH8_ENABLE_VARIABLES)
    Th8Shell_SetArgv(interp, zArgv0, argc, argv, start);
#else
    (void)zArgv0;
    (void)argc;
    (void)argv;
    (void)start;
#endif

    rc = Th8_EvalFile(interp, zPath, TH8_NOLEN);
    if (rc != TH8_OK) {
	Th8Shell_EmitResult(interp, rc);
	if (pExitCode) {
	    *pExitCode = TH8_EXIT_FAILURE;
	}
    } else {
	if (pExitCode) {
	    *pExitCode = Th8_IsExited(interp) ? TH8_EXIT_DEMAND
	                                      : TH8_EXIT_SUCCESS;
	}
    }
    return rc;
}


#if !defined(TH8_USE_BESTLINE)
/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_FallbackReadLine --
 *
 *	Plain fgets-based line reader used when bestline is not
 *	compiled in.  Prints the prompt, reads one line from
 *	stdin, strips the trailing newline, and returns a
 *	strdup'd copy.  Returns NULL on EOF.
 *
 * Why / How:
 *	On platforms without bestline (e.g. minimal builds or
 *	where bestline.c was excluded for size), the shell
 *	still needs basic line input.  This function provides
 *	the same caller-frees-the-result interface as bestline
 *	so the REPL loop is uniform regardless of backend.
 *
 *	The static 4 KiB buffer is reused on every call; the
 *	strdup result is what the caller owns.  Lines longer
 *	than 4 KiB are truncated, matching fgets semantics.
 *
 * Results:
 *	A malloc'd NUL-terminated line (without newline) or
 *	NULL on EOF.
 *
 * Side effects:
 *	Writes the prompt to stdout.  Reads from stdin.
 *
 *----------------------------------------------------------------------
 */

static char *
Th8Shell_FallbackReadLine(const char *prompt)
{
    static char zBuf[4096];

    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(zBuf, sizeof(zBuf), stdin)) {
	return 0;
    }
    {
	size_t n = strlen(zBuf);

	if (n > 0 && zBuf[n - 1] == '\n') {
	    zBuf[n - 1] = 0;
	}
    }
    return strdup(zBuf);
}
#endif /* !TH8_USE_BESTLINE */


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_RunRepl --
 *
 *	Run the interactive read-eval-print loop until EOF or
 *	[exit].  Multi-line input is accumulated using
 *	Th8_Complete to detect when the command is balanced
 *	and ready to evaluate (per Tcl-style brace/bracket/
 *	quote balancing).  Uses bestline for line editing when
 *	TH8_USE_BESTLINE is compiled in, otherwise falls back
 *	to Th8Shell_FallbackReadLine.
 *
 *	Sets ::argv0/::argc/::argv before the first line.
 *	Prints the banner.  Sources `lib/th8/init.th8` unless
 *	TH8SH_NO_SCRIPT_LIBRARY is set.  Adds completed
 *	commands to the line-editor's history (when bestline is
 *	compiled in) so up-arrow recall works.
 *
 *	Sets *pExitCode to TH8_EXIT_END_OF_TRANSMISSION on EOF,
 *	TH8_EXIT_DEMAND on [exit], or TH8_EXIT_FAILURE on a
 *	fatal error from sourcing the init file.
 *
 * Why / How:
 *	The accumulator design treats unbalanced lines as
 *	continuations: the prompt switches from "% " to "> "
 *	while the buffered command is incomplete, mirroring
 *	the conventional Tcl shell.  Th8_Complete is the
 *	single source of truth for "is this script balanced
 *	yet?", so the REPL never tries to parse a partial
 *	command itself.
 *
 *	Each completed command goes through Th8_EvalTrusted --
 *	the REPL's input is by definition a trusted stream
 *	(the user typed it).  When the signed-only policy is
 *	enabled, that distinction is what allows the REPL to
 *	keep working without re-signing every typed command.
 *
 *	When TH8_BENCHMARKING is compiled in, every command's
 *	cache statistics are dumped to stderr afterwards as a
 *	dev-only diagnostic.
 *
 * Results:
 *	TH8_OK in all normal cases.
 *
 * Side effects:
 *	Reads stdin, writes stdout/stderr, sources the init
 *	file, evaluates user commands (which can mutate
 *	arbitrary interpreter state).
 *
 *----------------------------------------------------------------------
 */

int
Th8Shell_RunRepl(
    Th8_Interp *interp,
    const char *zArgv0,
    int argc,
    const char **argv,
    int start,
    int bSignedOnly,
    int *pExitCode)
{
    char *zCmd = 0;
    size_t nCmd = 0;
    int evalRc;
    int exitCode = TH8_EXIT_SUCCESS;

#if defined(TH8_ENABLE_VARIABLES)
    Th8Shell_SetArgv(interp, zArgv0, argc, argv, start);
#else
    (void)zArgv0;
    (void)argc;
    (void)argv;
    (void)start;
#endif

    Th8Shell_PrintBanner(interp, bSignedOnly);

    if (!Th8_DoesEnvExist(interp, "TH8SH_NO_SCRIPT_LIBRARY")) {
	evalRc = Th8_EvalFile(interp, "lib/th8/init.th8", TH8_NOLEN);
	if (evalRc != TH8_OK) {
	    Th8Shell_EmitResult(interp, evalRc);
	    exitCode = TH8_EXIT_FAILURE;
	    goto done;
	} else if (Th8_IsExited(interp)) {
	    exitCode = TH8_EXIT_DEMAND;
	    goto done;
	}
    }

    for (;;) {
	char *zLine;
	const char *zPrompt;

	zPrompt = (nCmd == 0) ? "% " : "> ";
	zLine = TH8SHELL_READLINE(zPrompt);
	if (!zLine) {
	    exitCode = TH8_EXIT_END_OF_TRANSMISSION;
	    break;
	}

	if (zLine[0] == 0 && nCmd == 0) {
	    TH8SHELL_FREE_LINE(zLine);
	    continue;
	}

	/*
	 * Accumulate into the command buffer.
	 */

	if (nCmd > 0) {
	    TH8_STR_APPEND(interp, &zCmd, &nCmd, "\n", 1);
	}
	TH8_STR_APPEND(interp, &zCmd, &nCmd, zLine, TH8_NOLEN);
	TH8SHELL_FREE_LINE(zLine);

	/*
	 * If the command is not yet complete (unbalanced braces,
	 * brackets, or quotes), prompt for more.
	 */

	if (!Th8_Complete(zCmd, nCmd)) {
	    continue;
	}

	/*
	 * Evaluate the complete command.
	 */

	TH8SHELL_ADD_HISTORY(zCmd);

	evalRc = Th8_EvalTrusted(interp, 0, zCmd, nCmd, NULL, 0);
	Th8Shell_EmitResult(interp, evalRc);

#if defined(TH8_BENCHMARKING)
	{
	    th8_uint64_t nHit, nMiss, nEvict;

	    Th8_GetCacheStats(interp, &nHit, &nMiss, &nEvict);
	    if (nHit || nMiss || nEvict) {
		fprintf(
		    stderr,
		    "[cache: hit=%llu miss=%llu evict=%llu"
		    " rate=%.1f%%]: %s\n",
		    (unsigned long long)nHit, (unsigned long long)nMiss,
		    (unsigned long long)nEvict,
		    (nHit + nMiss) > 0 ? 100.0 * nHit / (nHit + nMiss) : 0.0,
		    zCmd);
	    }
	}
#endif

	if (Th8_IsExited(interp)) {
	    exitCode = TH8_EXIT_DEMAND;
	    break;
	}

	Th8_Free(interp, zCmd);
	zCmd = 0;
	nCmd = 0;
    }
    Th8_Free(interp, zCmd);
    printf("\n");

done:
    if (pExitCode) *pExitCode = exitCode;
    return TH8_OK;

oom:
    Th8_Free(interp, zCmd);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8Shell_Cleanup --
 *
 *	Centralised teardown.  Inspects Th8ShellState_g to
 *	determine which features were set up by previous
 *	Th8Shell_* calls (signed-only policy, signal handlers)
 *	and tears down only those.  Finishes by calling
 *	Th8_DeleteInterp and Th8_Finalize so the embedder's
 *	main() can end with a single teardown line.
 *
 * Why / How:
 *	The state-tracked design means an embedder that calls
 *	only some of the setup helpers (e.g. wants signal
 *	handlers but not the signed-only policy) gets a
 *	matching teardown automatically without bookkeeping
 *	in main().  Features that were never installed are
 *	skipped silently -- no spurious "shutdown what was
 *	never set up" calls that could mask real bugs.
 *
 *	Both arguments are individually NULL-safe so the
 *	function is callable from a half-initialized error
 *	path (e.g. Th8_CreateInterp returned NULL but we still
 *	want to Th8_Finalize the platform).
 *
 *	Order matters: first the signed-only policy is removed
 *	(also while interp is functional), then the interp
 *	itself is destroyed, and finally the platform is
 *	finalised.  The `bSignalHandlersInstalled` flag is
 *	cleared but the handlers themselves are left in place
 *	-- POSIX doesn't provide a clean per-interpreter
 *	"uninstall signal handler" idiom, and the handlers
 *	reference the now- cleared `Th8Shell_pCurrentInterp`
 *	pointer so they become no-ops.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Tears down all installed features, frees the
 *	interpreter, finalises the platform.  Resets
 *	Th8ShellState_g and Th8Shell_pCurrentInterp.
 *
 *----------------------------------------------------------------------
 */

void
Th8Shell_Cleanup(Th8_Interp *interp, Th8_Platform *pPlatform)
{
    if (interp) {
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
	if (Th8ShellState_g.bSignedPolicyInstalled) {
	    Th8_EnableSignedPolicy(interp, &Th8ShellState_g.pPolicyCtx, 0);
	    Th8ShellState_g.bSignedPolicyInstalled = 0;
	    Th8ShellState_g.bSignedOnly = 0;
	}
	Th8ShellState_g.bKeyTokensCmdRegistered = 0;
#endif

	Th8ShellState_g.bSignalHandlersInstalled = 0;
	Th8Shell_pCurrentInterp = 0;

	Th8_DeleteInterp(interp);
    }

    if (pPlatform) {
	Th8_Finalize(pPlatform);
    }
}
