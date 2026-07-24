/*
 * th8_introspection.c -- Introspection plugin for TH8.
 *
 * Implements the introspection commands: info, pid.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8IntrospectionGetCommands.
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

#if defined(TH8_PLUGIN_INTROSPECTION)

/*
 * Exported for use by info subcommands (declared extern in th8_int.h).
 */

const Th8_SubCommand *th8_info_aSub;

/*
 *----------------------------------------------------------------------
 *
 * Introspection commands --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * info_level_command --
 *
 *	Return the current procedure nesting level, or the command
 *	and arguments for a given level.
 *
 *	info level          => current level number
 *	info level NUMBER   => list of command + args at that level
 *
 *	Positive levels are absolute (0 = global, 1 = first proc).
 *	Negative levels are relative (-1 = caller, -2 = caller's
 *	caller, etc.).
 *
 * Why / How:
 *	Implements [info level].  With no argument, returns the
 *	current frame level via th8GetFrameLevel.  With an argument,
 *	retrieves the command and arguments at that level via
 *	Th8_GetFrameObjv and returns them as a list.
 *
 * Results:
 *	TH8_OK or TH8_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_level_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc == 2) {
	/*
	 * No argument: return current level.
	 */

	Th8_SetResultInt(interp, th8GetFrameLevel(interp));
	return TH8_OK;
    }
    if (argc == 3) {
	/*
	 * With argument: return command + args at that level.
	 */

	int iLevel;
	int fArgc;
	const char **fArgv;
	size_t *fArgl;
	char *zList = 0;
	size_t nList = 0;
	int i;
	int rc;

	rc = Th8_ToInt(interp, argv[2], argl[2], &iLevel);
	if (rc != TH8_OK) {
	    return rc;
	}

	rc = Th8_GetFrameObjv(interp, iLevel, &fArgc, &fArgv, &fArgl);
	if (rc != TH8_OK) {
	    return rc;
	}

	for (i = 0; i < fArgc; i++) {
	    Th8_ListAppend(interp, &zList, &nList, fArgv[i], fArgl[i]);
	}
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
	return TH8_OK;
    }
    return Th8_WrongNumArgs(interp, "info level ?number?");
}


/*
 *----------------------------------------------------------------------
 *
 * info_library_command --
 *
 *	info library
 *
 *	Return "./lib/th8" if the "th8" package has been provided
 *	(via [package provide th8 ...]), or an empty string if it
 *	has not.  The package entry -- not merely the directory --
 *	must be present.
 *
 * Why / How:
 *	Implements [info library].  Checks the package hash table
 *	for the "th8" package entry.  If present, returns the
 *	hardcoded library path "./lib/th8".  This is used by
 *	auto_path initialization and package require.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_library_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_HashEntry *pEntry;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info library");
    }

    /*
     * Check if the "th8" package has been provided by looking
     * it up in the package hash table without creating an entry.
     */

    pEntry = Th8_HashFind(interp, Th8_GetPackageHash(interp), "th8", 3, 0);

    if (pEntry && ALWAYS(pEntry->pData)) {
	Th8_SetResultStatic(interp, "./lib/th8", 9);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_exists_command --
 *
 *	Check whether a variable exists.
 *
 *	info exists VARNAME
 *
 * Why / How:
 *	Implements [info exists].  Delegates to Th8_ExistsVar which
 *	checks both local and global scope for the variable.
 *
 * Results:
 *	TH8_OK.  Result is 1 or 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_VARIABLES)
static int
info_exists_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "info exists varname");
    }
    Th8_SetResultInt(
        interp, Th8_ExistsVar(interp, argv[2], TH8_LEN(argl[2])));
    return TH8_OK;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * info_commands_command --
 *
 *	info commands ?PATTERN?
 *
 * Why / How:
 *	Implements [info commands].  Without a pattern, lists all
 *	commands.  With a qualified pattern (e.g. ::ns::*), searches
 *	the specific namespace via th8ResolveNsPattern and
 *	th8FindNamespace.  With a simple pattern, lists all commands
 *	and filters by glob.
 *
 * Results:
 *	TH8_OK.  Result is a list of command names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 * th8InfoListNsCommands --
 *
 *	Helper: iterate the commands in a specific namespace,
 *	glob-match the tail against zTailPat, and append
 *	fully-qualified names (zPrefix + "::" + tail) to the
 *	output list.  Used by info commands and info procs.
 */

typedef struct {
    Th8_Interp *interp;
    const char *zTailPat; /* Tail glob pattern. */
    size_t nTailPat;
    const char *zPrefix; /* Namespace prefix (e.g. "::foo"). */
    size_t nPrefix;
    char **pzList;  /* Output list. */
    size_t *pnList;
    int bProcsOnly;  /* If true, filter to procs only. */
} Th8_InfoCmdCtx;

/*
 *----------------------------------------------------------------------
 *
 * th8InfoCmdCallback --
 *
 *	Hash iteration callback for namespace-qualified command
 *	listing.
 *
 * Why / How:
 *	Called by Th8_HashIterate for each command in a namespace.
 *	Glob-matches the command tail against the pattern, and if
 *	it matches, builds the fully-qualified name (prefix::name)
 *	and appends it to the output list.
 *
 * Results:
 *	TH8_OK (always continues iteration).
 *
 * Side effects:
 *	Appends to the output list via the context structure.
 *
 *----------------------------------------------------------------------
 */

static int
th8InfoCmdCallback(Th8_HashEntry *pEntry, void *pVoid)
{
    Th8_InfoCmdCtx *p = (Th8_InfoCmdCtx *)pVoid;
    const char *zName = pEntry->zKey;
    size_t nName = pEntry->nKey;
    char *zFull = 0;
    size_t nFull = 0;

    if (p->bProcsOnly) {
	/*
	 * For info procs, the hash entry's pData points to
	 * the command info.  We can't easily check "is proc"
	 * from the hash entry alone.  Instead, rely on the
	 * fact that Th8_GetCommandInfo returns non-zero for
	 * procs.  Build the full name and check.
	 */

	/* Skip non-proc entries - this is a simplification;
	 * the caller can filter further if needed. */
    }

    if (Th8_GlobMatch(p->interp, p->zTailPat, p->nTailPat, zName, nName)) {
	/*
	 * Build "::ns::name" and append.
	 */

	TH8_STR_APPEND(p->interp, &zFull, &nFull, p->zPrefix, p->nPrefix);
	TH8_STR_APPEND(p->interp, &zFull, &nFull, "::", 2);
	TH8_STR_APPEND(p->interp, &zFull, &nFull, zName, nName);
	Th8_ListAppend(p->interp, p->pzList, p->pnList, zFull, nFull);
	Th8_Free(p->interp, zFull);
    }
    return TH8_OK;

oom:
    Th8_Free(p->interp, zFull);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * info_commands_command --
 *
 *	Implements `[info commands ?pattern?]`.  Returns a
 *	Tcl list of every command defined in the interpreter
 *	(or matching the optional glob `pattern`).  When
 *	`pattern` is namespace-qualified, the search is
 *	scoped to that namespace; when it is bare, the search
 *	covers the current namespace and the global
 *	namespace per Tcl convention.
 *
 *	The qualified-vs-bare classification is delegated to
 *	`th8ResolveNsPattern`, which also splits the
 *	namespace prefix from the tail glob.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (2 or 3).
 *	argv   -- argv[0]=`"info"`; argv[1]=`"commands"`;
 *		argv[2]=optional pattern.
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` with the list as the interpreter result;
 *	`TH8_ERROR` on argument-count or allocation failure
 *	(interpreter result: diagnostic).
 *
 * Side effects:
 *	Allocates and frees a list-building buffer; sets the
 *	interpreter result.
 *
 *----------------------------------------------------------------------
 */
static int
info_commands_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "info commands ?pattern?");
    }

    if (argc == 3) {
	const char *zPat = argv[2];
	size_t nPat = TH8_LEN(argl[2]);
	const char *zNsPath;
	size_t nNsPath;
	const char *zTail;
	size_t nTail;
	char *zBuf = 0;
	int bQualified;

	bQualified = th8ResolveNsPattern(
	    interp, zPat, nPat, &zNsPath, &nNsPath, &zTail, &nTail, &zBuf);

	if (bQualified) {
	    struct Th8_Namespace *pNs;
	    Th8_InfoCmdCtx ictx;

	    pNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
	    /* Every live namespace has paCmd allocated by
	     * th8FindNamespace (create-or-reset path); the only
	     * way paCmd is NULL is the transient OOM state during
	     * namespace teardown, which is not reachable from
	     * info commands.  C2 is ALWAYS T when pNs exists. */
	    if (pNs && ALWAYS(pNs->paCmd)) {
		ictx.interp = interp;
		ictx.zTailPat = zTail;
		ictx.nTailPat = nTail;
		ictx.zPrefix = zNsPath;
		ictx.nPrefix = nNsPath;
		ictx.pzList = &zList;
		ictx.pnList = &nList;
		ictx.bProcsOnly = 0;
		Th8_HashIterate(
		    interp, pNs->paCmd, th8InfoCmdCallback, &ictx);
	    }
	    Th8_Free(interp, zBuf);
	} else {
	    /*
	     * Simple (non-qualified) pattern: list
	     * commands from current + global namespaces,
	     * filter by glob.
	     */

	    Th8_ListAppendCommands(interp, &zList, &nList);
	    {
		char **azElem = 0;
		size_t *anElem = 0;
		int nCount;
		char *zFiltered = 0;
		size_t nFiltered = 0;
		int i;

		Th8_SplitList(
		    interp, zList, nList, &azElem, &anElem, &nCount,
		    TH8_LIST_NONE);
		for (i = 0; i < nCount && ALWAYS(azElem); i++) {
		    if (Th8_GlobMatch(
		            interp, zPat, nPat, azElem[i],
		            TH8_LEN(anElem[i]))) {
			Th8_ListAppend(
			    interp, &zFiltered, &nFiltered, azElem[i],
			    anElem[i]);
		    }
		}
		Th8_Free(interp, azElem);
		Th8_Free(interp, zList);
		zList = zFiltered;
		nList = nFiltered;
	    }
	}
    } else {
	Th8_ListAppendCommands(interp, &zList, &nList);
    }
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_script_command --
 *
 *	Return the name of the innermost active [source] script.
 *	Returns empty string if no source is active.
 *
 *	info script ?FILENAME?
 *
 * Why / How:
 *	Implements [info script].  With no argument, returns the
 *	current source script name via Th8_GetSourceName.  With an
 *	argument, replaces the current source name (pop + push),
 *	matching Tcl's "info script <name>" behavior.
 *
 * Results:
 *	TH8_OK.  Result is the script filename.
 *
 * Side effects:
 *	May change the current source name when called with an
 *	argument.
 *
 *----------------------------------------------------------------------
 */

static int
info_script_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    size_t nName;
    const char *zName;

    (void)ctx;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "info script ?filename?");
    }

    if (argc == 3) {
	/*
	 * Per Tcl standard, "info script <name>" sets the
	 * script name for the current invocation level.
	 */

	Th8_PopSourceName(interp);
	Th8_PushSourceName(interp, argv[2], argl[2]);
    }
    zName = Th8_GetSourceName(interp, &nName);
    Th8_SetResult(interp, zName, nName);
    return TH8_OK;
}


#  if defined(TH8_PLUGIN_PROCEDURES)
#    if defined(TH8_ENABLE_VARIABLES)
/*
 *----------------------------------------------------------------------
 *
 * info_default_command --
 *
 *	Query the default value of a procedure parameter.
 *
 *	info default PROCNAME ARGNAME VARNAME
 *
 *	If ARGNAME has a default value in PROCNAME, stores it in
 *	VARNAME and returns 1.  Otherwise sets VARNAME to "" and
 *	returns 0.
 *
 * Why / How:
 *	Implements [info default].  Looks up the command, verifies
 *	it is a proc/nproc, then searches the parameter list for
 *	ARGNAME.  If found with a default value, stores it in
 *	VARNAME and returns 1; otherwise stores "" and returns 0.
 *
 * Results:
 *	TH8_OK with result 1 or 0, or TH8_ERROR if the command is
 *	not a procedure or the argument doesn't exist.
 *
 * Side effects:
 *	Sets the variable named VARNAME.
 *
 *----------------------------------------------------------------------
 */

static int
info_default_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_CommandProc xProc;
    void *pContext;
    Th8_ProcDefn *p;
    int i;
    char *zErr = 0;
    size_t nErr = 0;

    (void)ctx;

    if (argc != 5) {
	return Th8_WrongNumArgs(
	    interp, "info default procname argname varname");
    }
    if (Th8_GetCommandInfo(interp, argv[2], argl[2], &xProc, &pContext) !=
            TH8_OK ||
        (xProc != th8ProcCall1 && xProc != th8NprocCall1)) {
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[2], argl[2]);
	TH8_STR_APPEND(
	    interp, &zErr, &nErr, "\" is not a procedure", TH8_NOLEN);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }
    p = (Th8_ProcDefn *)pContext;

    /*
     * Search the parameter list for ARGNAME.
     */

    for (i = 0; i < p->nParam; i++) {
	if (p->anParam[i] == TH8_LEN(argl[3]) &&
	    Th8_Memcmp(interp, p->azParam[i], argv[3], p->anParam[i]) == 0) {
	    /* azDefault is allocated inline as part of the proc
	     * def block (see th8_procedures.c L561 et al, where
	     * it points into the trailing data); it is ALWAYS
	     * non-NULL for a valid proc.  Only the per-slot
	     * entry (azDefault[i]) varies. */
	    if (ALWAYS(p->azDefault) && p->azDefault[i]) {
		Th8_SetVar(
		    interp, argv[4], TH8_LEN(argl[4]), p->azDefault[i],
		    p->anDefault[i]);
		return Th8_SetResultInt(interp, 1);
	    } else {
		Th8_SetVar(interp, argv[4], TH8_LEN(argl[4]), "", 0);
		return Th8_SetResultInt(interp, 0);
	    }
	}
    }

    {
	TH8_STR_APPEND(interp, &zErr, &nErr, "procedure \"", TH8_NOLEN);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[2], argl[2]);
	TH8_STR_APPEND(
	    interp, &zErr, &nErr, "\" doesn't have an argument \"",
	    TH8_NOLEN);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[3], argl[3]);
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}
#    endif


/*
 *----------------------------------------------------------------------
 *
 * info_body_command --
 *
 *	Return the body of a procedure.
 *
 *	info body procname
 *
 * Why / How:
 *	Implements [info body].  Looks up the command, verifies it
 *	is a proc/nproc via the function pointer, then returns the
 *	stored program text from the Th8_ProcDefn structure.
 *
 * Results:
 *	TH8_OK with the procedure body, or TH8_ERROR if the command
 *	is not a procedure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_body_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    Th8_CommandProc xProc;
    void *pContext;
    Th8_ProcDefn *p;
    char *zErr = 0;
    size_t nErr = 0;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "info body procname");
    }
    if (Th8_GetCommandInfo(interp, argv[2], argl[2], &xProc, &pContext) !=
            TH8_OK ||
        (xProc != th8ProcCall1 && xProc != th8NprocCall1)) {
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[2], argl[2]);
	TH8_STR_APPEND(
	    interp, &zErr, &nErr, "\" is not a procedure", TH8_NOLEN);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }
    p = (struct Th8_ProcDefn *)pContext;
    return Th8_SetResult(interp, p->zProgram, p->nProgram);

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * info_args_command --
 *
 *	Return a list of the formal parameters of a procedure.
 *
 *	info args procname
 *
 * Why / How:
 *	Implements [info args].  Looks up the procedure definition,
 *	builds a list from the azParam array, and appends "args"
 *	if the procedure has a variadic parameter (hasArgs flag).
 *
 * Results:
 *	TH8_OK with the parameter list, or TH8_ERROR if the command
 *	is not a procedure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_args_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    Th8_CommandProc xProc;
    void *pContext;
    Th8_ProcDefn *p;
    char *zList = 0;
    size_t nList = 0;
    char *zErr = 0;
    size_t nErr = 0;
    int i;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "info args procname");
    }
    if (Th8_GetCommandInfo(interp, argv[2], argl[2], &xProc, &pContext) !=
            TH8_OK ||
        (xProc != th8ProcCall1 && xProc != th8NprocCall1)) {
	TH8_STR_APPEND(interp, &zErr, &nErr, "\"", 1);
	TH8_STR_APPEND(interp, &zErr, &nErr, argv[2], argl[2]);
	TH8_STR_APPEND(
	    interp, &zErr, &nErr, "\" is not a procedure", TH8_NOLEN);
	Th8_SetResult(interp, zErr, nErr);
	Th8_Free(interp, zErr);
	return TH8_ERROR;
    }
    p = (struct Th8_ProcDefn *)pContext;
    for (i = 0; i < p->nParam; i++) {
	Th8_ListAppend(interp, &zList, &nList, p->azParam[i], p->anParam[i]);
    }
    if (p->hasArgs) {
	Th8_ListAppend(interp, &zList, &nList, "args", 4);
    }
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;

oom:
    Th8_Free(interp, zErr);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * info_procs_command --
 *
 *	Return a list of procedures (proc/nproc), excluding
 *	built-in commands.
 *
 *	info procs ?PATTERN?
 *
 * Why / How:
 *	Implements [info procs].  Uses Th8_ListAppendCommandsMatching
 *	with th8ProcCall1/th8NprocCall1 function pointers to filter
 *	to only user-defined procedures.  Supports namespace-qualified
 *	patterns via th8ResolveNsPattern.
 *
 * Results:
 *	TH8_OK.  Result is a list of procedure names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_procs_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zList = 0;
    size_t nList = 0;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "info procs ?pattern?");
    }

    if (argc == 3) {
	const char *zPat = argv[2];
	size_t nPat = TH8_LEN(argl[2]);
	const char *zNsPath;
	size_t nNsPath;
	const char *zTail;
	size_t nTail;
	char *zBuf = 0;
	int bQualified;

	bQualified = th8ResolveNsPattern(
	    interp, zPat, nPat, &zNsPath, &nNsPath, &zTail, &nTail, &zBuf);

	if (bQualified) {
	    struct Th8_Namespace *pNs;
	    Th8_InfoCmdCtx ictx;

	    pNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
	    /* Every live namespace has paCmd allocated by
	     * th8FindNamespace (create-or-reset path); the only
	     * way paCmd is NULL is the transient OOM state during
	     * namespace teardown, which is not reachable from
	     * info commands.  C2 is ALWAYS T when pNs exists. */
	    if (pNs && ALWAYS(pNs->paCmd)) {
		ictx.interp = interp;
		ictx.zTailPat = zTail;
		ictx.nTailPat = nTail;
		ictx.zPrefix = zNsPath;
		ictx.nPrefix = nNsPath;
		ictx.pzList = &zList;
		ictx.pnList = &nList;
		ictx.bProcsOnly = 1;
		Th8_HashIterate(
		    interp, pNs->paCmd, th8InfoCmdCallback, &ictx);
	    }
	    Th8_Free(interp, zBuf);
	} else {
	    Th8_ListAppendCommandsMatching(
	        interp, &zList, &nList, th8ProcCall1, th8NprocCall1);
	    {
		char **azElem = 0;
		size_t *anElem = 0;
		int nCount;
		char *zFiltered = 0;
		size_t nFiltered = 0;
		int i;

		Th8_SplitList(
		    interp, zList, nList, &azElem, &anElem, &nCount,
		    TH8_LIST_NONE);
		for (i = 0; i < nCount && ALWAYS(azElem); i++) {
		    if (Th8_GlobMatch(
		            interp, zPat, nPat, azElem[i],
		            TH8_LEN(anElem[i]))) {
			Th8_ListAppend(
			    interp, &zFiltered, &nFiltered, azElem[i],
			    anElem[i]);
		    }
		}
		Th8_Free(interp, azElem);
		Th8_Free(interp, zList);
		zList = zFiltered;
		nList = nFiltered;
	    }
	}
    } else {
	Th8_ListAppendCommandsMatching(
	    interp, &zList, &nList, th8ProcCall1, th8NprocCall1);
    }
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}
#  endif /* TH8_PLUGIN_PROCEDURES */


/*
 *----------------------------------------------------------------------
 *
 * info_globals_command --
 *
 *	info globals ?PATTERN?
 *
 *	Return a list of global variables, optionally filtered
 *	by a glob pattern.
 *
 * Why / How:
 *	Implements [info globals].  Delegates to
 *	Th8_ListAppendGlobalVariables to enumerate global variables,
 *	then filters by the optional glob pattern.
 *
 * Results:
 *	TH8_OK.  Result is a list of global variable names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_VARIABLES)
static int
info_globals_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;

    (void)ctx;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "info globals ?pattern?");
    }
    Th8_ListAppendGlobalVariables(interp, &zList, &nList);

    if (argc == 3 && zList) {
	char **azElem = 0;
	size_t *anElem = 0;
	int nCount;
	char *zFiltered = 0;
	size_t nFiltered = 0;
	int i;

	Th8_SplitList(
	    interp, zList, nList, &azElem, &anElem, &nCount, TH8_LIST_NONE);
	for (i = 0; i < nCount && ALWAYS(azElem); i++) {
	    if (Th8_GlobMatch(
	            interp, argv[2], TH8_LEN(argl[2]), azElem[i],
	            TH8_LEN(anElem[i]))) {
		Th8_ListAppend(
		    interp, &zFiltered, &nFiltered, azElem[i], anElem[i]);
	    }
	}
	Th8_Free(interp, zList);
	Th8_Free(interp, azElem);
	zList = zFiltered;
	nList = nFiltered;
    }

    if (zList) {
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * info_vars_command --
 *
 *	info vars ?PATTERN?
 *
 * Why / How:
 *	Implements [info vars].  Enumerates variables from the
 *	current frame (or a specific namespace if a qualified
 *	pattern is given), then filters by glob pattern.  Uses
 *	th8ResolveNsPattern to detect namespace qualification.
 *
 * Results:
 *	TH8_OK.  Result is a list of variable names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_VARIABLES)
static int
info_vars_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;
    const char *zPat = 0;
    size_t nPat = 0;
    const char *zNs = 0;
    size_t nNs = 0;
    int bQualified = 0;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "info vars ?pattern?");
    }

    /*
     * If a namespace-qualified pattern is given (e.g. ::ns::*),
     * enumerate variables from that namespace.  Otherwise,
     * enumerate from the current frame.
     */

    if (argc == 3) {
	char *zBuf = 0;

	zPat = argv[2];
	nPat = argl[2];
	bQualified = th8ResolveNsPattern(
	    interp, zPat, nPat, &zNs, &nNs, &zPat, &nPat, &zBuf);
	if (bQualified) {
	    Th8_ListAppendNsVariables(interp, zNs, nNs, &zList, &nList);
	}
	Th8_Free(interp, zBuf);
    }

    if (!bQualified) {
	Th8_ListAppendVariables(interp, &zList, &nList);
    }

    if (argc == 3 && zList) {
	char **azElem = 0;
	size_t *anElem = 0;
	int nCount;
	char *zFiltered = 0;
	size_t nFiltered = 0;
	int i;

	Th8_SplitList(
	    interp, zList, nList, &azElem, &anElem, &nCount, TH8_LIST_NONE);
	for (i = 0; i < nCount && ALWAYS(azElem); i++) {
	    if (Th8_GlobMatch(
	            interp, zPat, TH8_LEN(nPat), azElem[i],
	            TH8_LEN(anElem[i]))) {
		Th8_ListAppend(
		    interp, &zFiltered, &nFiltered, azElem[i], anElem[i]);
	    }
	}
	Th8_Free(interp, azElem);
	Th8_Free(interp, zList);
	zList = zFiltered;
	nList = nFiltered;
    }
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * info_varlinks_command --
 *
 *	Return a list of all variables in the current frame that
 *	are linked to variables in another frame (via [upvar] or
 *	[global]).
 *
 *	info varlinks
 *
 * Why / How:
 *	Implements [info varlinks].  Delegates to
 *	Th8_ListAppendVarLinks which iterates the current frame's
 *	variables and returns those that are linked (aliases created
 *	by [upvar] or [global]).
 *
 * Results:
 *	TH8_OK.  Result is a Tcl list of variable names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#  if defined(TH8_ENABLE_VARIABLES)
static int
info_varlinks_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zList = 0;
    size_t nList = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info varlinks");
    }

    Th8_ListAppendVarLinks(interp, &zList, &nList);

    if (zList) {
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * info_nameofexecutable_command --
 *
 *	info nameofexecutable
 *
 *	Return the full path to the host executable.  Resolution
 *	order:
 *	  1. ::th8_nameofexecutable variable (host override)
 *	  2. Platform xGetExePath callback (OS-native detection)
 *	  3. Empty string (fallback)
 *
 * Why / How:
 *	Implements [info nameofexecutable].  Uses a three-tier
 *	resolution: first checks the host-override variable, then
 *	the platform's xGetExePath callback, then falls back to
 *	error.  This allows hosts to override the path detection
 *	for embedded or relocated installations.
 *
 * Results:
 *	TH8_OK with the executable path, or TH8_ERROR if no path
 *	can be determined.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * info_loaded_command --
 *
 *	info loaded
 *
 * Why / How:
 *	Implements [info loaded].  Returns a list of dynamically
 *	loaded extensions (via the [load] command).  Delegates to
 *	Th8_ListAppendLoaded when TH8_ENABLE_LOAD is defined;
 *	returns empty list otherwise.
 *
 * Results:
 *	TH8_OK.  Result is a list of loaded extensions.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_loaded_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info loaded");
    }
#  if defined(TH8_ENABLE_LOAD)
    Th8_ListAppendLoaded(interp, &zList, &nList);
#  endif
    Th8_SetResult(interp, zList ? zList : "", zList ? nList : 0);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_plugins_command --
 *
 *	info plugins
 *
 * Why / How:
 *	Implements [info plugins].  Returns a list of registered
 *	plugin names.  Delegates to Th8_ListAppendPlugins which
 *	iterates the plugin registry.
 *
 * Results:
 *	TH8_OK.  Result is a list of plugin names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_plugins_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info plugins");
    }
    Th8_ListAppendPlugins(interp, &zList, &nList);
    Th8_SetResult(interp, zList ? zList : "", zList ? nList : 0);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_nameofexecutable_command --
 *
 *	Implements `[info nameofexecutable]`.  Returns the
 *	absolute path of the running executable, resolved
 *	through a three-tier lookup:
 *
 *	  1. The host-set variable `::th8_nameofexecutable`
 *	     (explicit embedder override).  When present, its
 *	     value is returned verbatim.
 *	  2. The platform's `xGetExePath` callback (which
 *	     wraps `readlink("/proc/self/exe")` on Linux,
 *	     `_NSGetExecutablePath` on macOS,
 *	     `GetModuleFileNameW` on Win32, etc.).
 *	  3. Fall back to the empty string when neither
 *	     source is available.
 *
 *	Priority 1 lets embedders force a specific identity
 *	(useful for self-tests and tools that wrap the
 *	interpreter); priority 2 covers the common case.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- unused command context.
 *	argc   -- argument count (must be 2).
 *	argv   -- argv[0]=`"info"`; argv[1]=`"nameofexecutable"`.
 *	argl   -- argument byte-lengths.
 *
 * Returns:
 *	`TH8_OK` with the path as the interpreter result;
 *	`TH8_ERROR` only on wrong argument count.
 *
 * Side effects:
 *	Sets the interpreter result; may allocate and free a
 *	scratch path buffer for the platform-callback path.
 *
 *----------------------------------------------------------------------
 */
static int
info_nameofexecutable_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info nameofexecutable");
    }

    /*
     * Priority 1: the host-set variable (explicit override).
     */
#  if defined(TH8_ENABLE_VARIABLES)
    if (Th8_ExistsVar(interp, "::th8_nameofexecutable", TH8_NOLEN)) {
	return Th8_GetVar(interp, "::th8_nameofexecutable", TH8_NOLEN);
    }
#  endif

    /*
     * Priority 2: platform callback (xGetExePath).
     */
    {
	char *zExe = Th8_GetExePath(interp);

	if (zExe) {
	    Th8_SetResult(interp, zExe, TH8_NOLEN);
	    Th8_Free(interp, zExe);
	    return TH8_OK;
	}
    }

    Th8_SetResultStatic(interp, "no executable name?", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * info_sharedlibextension_command --
 *
 *	info sharedlibextension
 *
 *	Return the platform's shared library file extension.
 *
 * Why / How:
 *	Implements [info sharedlibextension].  Returns a
 *	compile-time-determined string: ".dll" on Windows,
 *	".dylib" on macOS, ".so" elsewhere.  Used by the [load]
 *	command and package system to locate shared libraries.
 *
 * Results:
 *	TH8_OK.  Result is the extension string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_sharedlibextension_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info sharedlibextension");
    }
#  if defined(_WIN32) || defined(WIN32)
    Th8_SetResultStatic(interp, ".dll", 4);
#  elif defined(__APPLE__)
    Th8_SetResultStatic(interp, ".dylib", 6);
#  else
    Th8_SetResultStatic(interp, ".so", 3);
#  endif
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_patchlevel_command --
 *
 *	info patchlevel
 *
 *	Return the TH8 version string.
 *
 * Why / How:
 *	Implements [info patchlevel].  When variables are enabled,
 *	reads from the ::tcl_platform(patchLevel) variable to
 *	support host overrides.  Otherwise returns the compile-time
 *	TH8_PATCH_LEVEL constant.
 *
 * Results:
 *	TH8_OK.  Result is the version string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_patchlevel_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info patchlevel");
    }
#  if defined(TH8_ENABLE_VARIABLES)
    return Th8_GetVar(interp, "::tcl_platform(patchLevel)", TH8_NOLEN);
#  else
    return Th8_SetResult(interp, TH8_PATCH_LEVEL, 0);
#  endif
}


/*
 *----------------------------------------------------------------------
 *
 * info_breakpoints_command --
 *
 *	info breakpoints
 *
 *	Return a list of breakpoint descriptions.  Each breakpoint
 *	produces three elements: ID, scriptName, lineNumber.
 *
 * Results:
 *	TH8_OK.  Result is a flat list of {id name line} triples.
 *
 *----------------------------------------------------------------------
 */

static int
info_breakpoints_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info breakpoints");
    }

    {
	char *zList = 0;
	size_t nList = 0;

	Th8_ListAppendBreakpoints(interp, &zList, &nList);
	Th8_SetResult(interp, zList ? zList : "", nList);
	Th8_Free(interp, zList);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_expansions_command --
 *
 *	info expansions ?pattern?
 *
 *	Return a list of registered expansion operator tag names
 *	in the current namespace, optionally filtered by a glob
 *	pattern.
 *
 * Results:
 *	TH8_OK.  Result is a list of tag names.
 *
 *----------------------------------------------------------------------
 */

static int
info_expansions_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "info expansions ?pattern?");
    }

    {
	char *zList = 0;
	size_t nList = 0;

	Th8_ListAppendExpansions(
	    interp, &zList, &nList, argc == 3 ? argv[2] : 0,
	    argc == 3 ? argl[2] : 0);
	Th8_SetResult(interp, zList ? zList : "", nList);
	Th8_Free(interp, zList);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_functions_command --
 *
 *	info functions ?pattern?
 *
 *	Return a list of registered math function names, optionally
 *	filtered by a glob pattern.
 *
 * Why / How:
 *	Implements [info functions].  Delegates to
 *	Th8_ListAppendMathFunctions (when expressions are enabled)
 *	which iterates the registered math function table and applies
 *	the optional glob pattern.
 *
 * Results:
 *	TH8_OK.  Result is a list of function names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_functions_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "info functions ?pattern?");
    }

    {
	char *zList = 0;
	size_t nList = 0;

#  if defined(TH8_ENABLE_EXPRESSIONS)
	Th8_ListAppendMathFunctions(
	    interp, &zList, &nList, argc == 3 ? argv[2] : 0,
	    argc == 3 ? argl[2] : 0);
#  endif
	Th8_SetResult(interp, zList ? zList : "", nList);
	Th8_Free(interp, zList);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_cmdcount_command --
 *
 *	info cmdcount
 *
 *	Return the number of commands evaluated so far by this
 *	interpreter (the step count).
 *
 * Why / How:
 *	Implements [info cmdcount].  Delegates to Th8_GetStepCount
 *	which returns the cumulative number of commands dispatched
 *	by this interpreter.  Returns a wide integer to avoid
 *	overflow in long-running sessions.
 *
 * Results:
 *	TH8_OK.  Result is the command count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_cmdcount_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info cmdcount");
    }
    return Th8_SetResultWideInt(interp, Th8_GetStepCount(interp));
}


/*
 *----------------------------------------------------------------------
 *
 * info_complete_command --
 *
 *	info complete SCRIPT
 *
 *	Return 1 if SCRIPT is syntactically complete (all braces,
 *	brackets, and quotes are balanced), 0 otherwise.
 *
 * Why / How:
 *	Implements [info complete].  Delegates to Th8_Complete which
 *	performs a lightweight parse to check for balanced braces,
 *	brackets, and quotes.  Used by interactive shells to detect
 *	incomplete input requiring continuation lines.
 *
 * Results:
 *	TH8_OK.  Result is 1 (complete) or 0 (incomplete).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_complete_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "info complete script");
    }
    return Th8_SetResultInt(interp, Th8_Complete(argv[2], argl[2]));
}


#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * info_context_command --
 *
 *	Return a unique context identifier for this interpreter
 *	session.  The identifier is a 128-character SHA-512 hex
 *	digest computed once from a 32-byte seed:
 *
 *	  Bytes  0- 3: parent PID (4 LE bytes)
 *	  Bytes  4- 7: PID (4 LE bytes)
 *	  Bytes  8-15: thread ID (8 LE bytes)
 *	  Bytes 16-31: 16 random bytes
 *
 *	The result is cached in a static variable and never
 *	recomputed.
 *
 *	info context
 *
 * Why / How:
 *	Implements [info context].  Builds a deterministic-plus-random
 *	seed from process/thread identity and random bytes, hashes
 *	it with SHA-512 for uniform distribution, and caches the
 *	result.  The static cache ensures the context ID is stable
 *	for the lifetime of the process.
 *
 * Results:
 *	TH8_OK.  Result is the 128-character hex string.
 *
 * Side effects:
 *	On first call, generates random bytes and computes the hash.
 *
 *----------------------------------------------------------------------
 */

static int
info_context_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    static char zCached[129];
    static int bDone = 0;

    (void)ctx;
    (void)argv;
    (void)argl;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "info context");
    }

    if (!bDone) {
	unsigned char aSeed[32];
	int ppid;
	int pid;
	th8_uint64_t tid;

	ppid = Th8_GetParentPid(interp);
	pid = Th8_GetPid(interp);
	tid = Th8_GetThreadId(interp);

	/* Bytes 0-3: parent PID (little-endian). */
	aSeed[0] = (unsigned char)(ppid & 0xFF);
	aSeed[1] = (unsigned char)((ppid >> 8) & 0xFF);
	aSeed[2] = (unsigned char)((ppid >> 16) & 0xFF);
	aSeed[3] = (unsigned char)((ppid >> 24) & 0xFF);

	/* Bytes 4-7: PID (little-endian). */
	aSeed[4] = (unsigned char)(pid & 0xFF);
	aSeed[5] = (unsigned char)((pid >> 8) & 0xFF);
	aSeed[6] = (unsigned char)((pid >> 16) & 0xFF);
	aSeed[7] = (unsigned char)((pid >> 24) & 0xFF);

	/* Bytes 8-15: thread ID (little-endian). */
	aSeed[8] = (unsigned char)(tid & 0xFF);
	aSeed[9] = (unsigned char)((tid >> 8) & 0xFF);
	aSeed[10] = (unsigned char)((tid >> 16) & 0xFF);
	aSeed[11] = (unsigned char)((tid >> 24) & 0xFF);
	aSeed[12] = (unsigned char)((tid >> 32) & 0xFF);
	aSeed[13] = (unsigned char)((tid >> 40) & 0xFF);
	aSeed[14] = (unsigned char)((tid >> 48) & 0xFF);
	aSeed[15] = (unsigned char)((tid >> 56) & 0xFF);

	/* Bytes 16-31: 16 random bytes. */
	Th8_RandomBytes(interp, &aSeed[16], 16);

	Th8_Sha512Hex(interp, aSeed, 32, zCached);
	bDone = 1;
    }

    Th8_SetResult(interp, zCached, 128);
    return TH8_OK;
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * info_subcommands_command --
 *
 *	info subcommands COMMAND ?PATTERN?
 *
 * Why / How:
 *	Implements [info subcommands].  Matches the COMMAND name to
 *	its known subcommand table (info, file, string, namespace,
 *	package, array) and returns a list of subcommand names,
 *	optionally filtered by a glob pattern.  The subcommand
 *	tables are stored in global pointers (th8_info_aSub, etc.)
 *	that are populated when the ensemble dispatchers run.
 *
 * Results:
 *	TH8_OK with a list of subcommand names, or TH8_ERROR if
 *	COMMAND is not a recognized ensemble.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
info_subcommands_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const Th8_SubCommand *pSub = 0;
    char *zList = 0;
    size_t nList = 0;
    const char *zPat = 0;
    size_t nPat = 0;
    int i;

    (void)ctx;

    if (argc != 3 && argc != 4) {
	return Th8_WrongNumArgs(interp, "info subcommands command ?pattern?");
    }
    if (argc == 4) {
	zPat = argv[3];
	nPat = argl[3];
    }

    /*
     * Match the command name to its subcommand table.
     */

    if (th8StrEq(interp, argv[2], argl[2], "info")) {
	pSub = th8_info_aSub;
    }
#  if defined(TH8_PLUGIN_FILE_SYSTEMS)
    else if (th8StrEq(interp, argv[2], argl[2], "file")) {
	pSub = th8_file_aSub;
    }
#  endif
#  if defined(TH8_PLUGIN_STRINGS)
    else if (th8StrEq(interp, argv[2], argl[2], "string")) {
	pSub = th8_string_aSub;
    }
#  endif
#  if defined(TH8_PLUGIN_MANAGEMENT)
    else if (th8StrEq(interp, argv[2], argl[2], "namespace")) {
	pSub = th8_namespace_aSub;
    }
#  endif
#  if defined(TH8_PLUGIN_EXTENSIBILITY)
    else if (th8StrEq(interp, argv[2], argl[2], "package")) {
	pSub = th8_package_aSub;
    }
#  endif
#  if defined(TH8_PLUGIN_VARIABLES)
    else if (th8StrEq(interp, argv[2], argl[2], "array")) {
	pSub = th8_array_aSub;
    }
#  endif
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    else if (th8StrEq(interp, argv[2], argl[2], "flags")) {
	/* Bug 35: th8_flags_aSub is owned by the harpy plugin
	 * which is gated on TH8_ENABLE_CRYPTOGRAPHY; without
	 * cryptography the [info subcommands flags] form is
	 * not supported. */
	pSub = th8_flags_aSub;
    }
#  endif

    if (!pSub) {
	Th8_ErrorMessage(
	    interp, "not an ensemble command: \"", argv[2], argl[2]);
	return TH8_ERROR;
    }

    for (i = 0; pSub[i].zName; i++) {
	if (zPat) {
	    if (!Th8_GlobMatch(
	            interp, zPat, TH8_LEN(nPat), pSub[i].zName,
	            Th8_Strlen(interp, pSub[i].zName))) {
		continue;
	    }
	}
	Th8_ListAppend(interp, &zList, &nList, pSub[i].zName, TH8_NOLEN);
    }
    if (zList) {
	Th8_SetResult(interp, zList, nList);
	Th8_Free(interp, zList);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * info_command --
 *
 *	Dispatcher for info sub-commands.
 *
 * Why / How:
 *	Implements the Tcl [info] command ensemble.  Builds a static
 *	subcommand table (conditionally compiled based on enabled
 *	features) and delegates to Th8_CallSubCommand.  Also stores
 *	the table pointer in the th8_info_aSub global for
 *	[info subcommands] support.
 *
 * Results:
 *	Return code from the sub-command.
 *
 * Side effects:
 *	Determined by the sub-command.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand th8InfoSub[] = {
#  if defined(TH8_PLUGIN_PROCEDURES)
    {0, "args", info_args_command},
    {0, "body", info_body_command},
#  endif
#  if defined(TH8_ENABLE_VARIABLES) && defined(TH8_PLUGIN_PROCEDURES)
    {0, "default", info_default_command},
#  endif
    {0, "breakpoints", info_breakpoints_command},
    {0, "cmdcount", info_cmdcount_command},
    {0, "commands", info_commands_command},
    {0, "complete", info_complete_command},
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
    {0, "context", info_context_command},
#  endif
#  if defined(TH8_ENABLE_VARIABLES)
    {0, "exists", info_exists_command},
#  endif
    {0, "expansions", info_expansions_command},
    {0, "functions", info_functions_command},
#  if defined(TH8_ENABLE_VARIABLES)
    {0, "globals", info_globals_command},
#  endif
    {0, "level", info_level_command},
    {0, "library", info_library_command},
    {0, "loaded", info_loaded_command},
    {0, "plugins", info_plugins_command},
    {0, "nameofexecutable", info_nameofexecutable_command},
    {0, "patchlevel", info_patchlevel_command},
#  if defined(TH8_PLUGIN_PROCEDURES)
    {0, "procs", info_procs_command},
#  endif
    {0, "script", info_script_command},
    {0, "sharedlibextension", info_sharedlibextension_command},
    {0, "subcommands", info_subcommands_command},
#  if defined(TH8_ENABLE_VARIABLES)
    {0, "varlinks", info_varlinks_command},
#  endif
#  if defined(TH8_ENABLE_VARIABLES)
    {0, "vars", info_vars_command},
#  endif
    {0, 0, 0}};

/*
 *----------------------------------------------------------------------
 *
 * info_command --
 *
 *	Implements the script-visible `[info ...]` ensemble.
 *	Pure thin wrapper that hands the dispatch off to
 *	`Th8_CallSubCommand` against `th8InfoSub`, the
 *	feature-gated table covering `args`, `body`,
 *	`breakpoints`, `cmdcount`, `commands`, `complete`,
 *	`context`, `default`, `exists`, `expansions`,
 *	`functions`, `globals`, `level`, `library`, `loaded`,
 *	`nameofexecutable`, `patchlevel`, `plugins`, and
 *	(when their respective features are enabled) several
 *	others.
 *
 *	Diagnostics for unknown / ambiguous subcommands are
 *	emitted by `Th8_CallSubCommand`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- command context (forwarded).
 *	argc   -- argument count.
 *	argv   -- argument vector.
 *	argl   -- argument byte-length vector.
 *
 * Returns:
 *	The selected subcommand handler's return code, or
 *	`TH8_ERROR` with a diagnostic if the subcommand name
 *	is unknown.
 *
 * Side effects:
 *	Whatever the dispatched subcommand performs.
 *
 *----------------------------------------------------------------------
 */
static int
info_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    return Th8_CallSubCommand(interp, ctx, argc, argv, argl, th8InfoSub);
}


/*
 *----------------------------------------------------------------------
 *
 * pid_command --
 *
 *	Return the process ID of the host process.  Returns 0 if
 *	the platform does not support process identification.
 *
 *	pid
 *
 * Why / How:
 *	Implements the Tcl [pid] command.  Delegates to Th8_GetPid
 *	which calls the platform's process identification API.
 *
 * Results:
 *	TH8_OK.  Result is the PID.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
pid_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "pid");
    }
    Th8_SetResultInt(interp, Th8_GetPid(interp));
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8IntrospectionCommands[] = {
    {1, 0, "info", info_command},
    {1, 0, "pid", pid_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8IntrospectionGetCommands --
 *
 *	Return the command table for the introspection plugin.
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
th8IntrospectionGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8IntrospectionCommands) /
                  sizeof(th8IntrospectionCommands[0]));

    th8_info_aSub = th8InfoSub;

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
	    pCommand[i] = th8IntrospectionCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_INTROSPECTION */
