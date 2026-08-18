/*
 * th8_management.c -- Management plugin for TH8.
 *
 * Implements the management commands: interp, namespace, rename.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8ManagementGetCommands.
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

#if defined(TH8_PLUGIN_MANAGEMENT)

/*
 * Exported for info subcommands (declared extern in th8_int.h).
 */

const Th8_SubCommand *th8_namespace_aSub;

/*
 *----------------------------------------------------------------------
 *
 * rename_command --
 *
 *	Implements the Tcl [rename] command.  Renames or deletes a
 *	command.
 *
 *	rename OLDNAME NEWNAME
 *
 * Why / How:
 *	Delegates to Th8_RenameCommand which handles name lookup,
 *	hash table manipulation, and (when NEWNAME is empty) command
 *	deletion including the xDelete callback.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if OLDNAME does not exist.
 *
 * Side effects:
 *	Changes or removes a command entry in the interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
rename_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "rename oldcmd newcmd");
    }
    return Th8_RenameCommand(interp, argv[1], argl[1], argv[2], argl[2]);
}


/*
 *----------------------------------------------------------------------
 *
 * interp_cancel_command --
 *
 *	Implements the [interp cancel] sub-command.  Requests
 *	cancellation of the running script.
 *
 *	interp cancel ?-unwind? ?--? ?path? ?result?
 *
 *	Per TIP #285 the first non-switch argument is ALWAYS the
 *	interpreter path; the second (if present) is the result
 *	message.  TH8 has no child interpreters, so the path must be
 *	"" (empty string) or absent.  A result message for the current
 *	interpreter is therefore passed as `interp cancel "" MESSAGE`:
 *	a lone non-empty argument is an interpreter path (and rejected
 *	as "could not find interpreter"), NOT a message.
 *
 * Why / How:
 *	Parses the -unwind flag and the optional path/result arguments
 *	following TIP #285 semantics.  Since TH8 has no child
 *	interpreters, any non-empty path is rejected as "could not
 *	find interpreter".  Delegates to Th8_CancelEval which sets
 *	the cancel flag checked by the evaluation loop.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the interpreter path is
 *	invalid.
 *
 * Side effects:
 *	Sets the interpreter's cancel flag and optional result message.
 *
 *----------------------------------------------------------------------
 */

static int
interp_cancel_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int flags = 0;
    int iArg = 2;
    const char *zMsg = 0;
    size_t nMsg = 0;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "interp cancel ?-unwind? ?--? ?path? ?result?");
    }
    while (iArg < argc) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-unwind")) {
	    flags |= TH8_CANCEL_UNWIND;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "--")) {
	    iArg++;
	    break;
	} else {
	    break;
	}
    }

    /*
     * TIP #285: The first non-switch argument is ALWAYS the
     * interpreter path.  The second (if present) is the result
     * message.  TH8 has no child interpreters, so the path
     * must be "" (empty string).  Any non-empty path is an
     * error ("could not find interpreter").
     *
     * If no non-switch arguments remain, cancel the current
     * interpreter with the default message.
     */

    if (iArg < argc) {
	/* First remaining arg = interpreter path. */
	if (TH8_LEN(argl[iArg]) != 0) {
	    Th8_ErrorMessage(
	        interp, "could not find interpreter \"", argv[iArg],
	        argl[iArg]);
	    return TH8_ERROR;
	}
	iArg++;  /* Skip the empty path. */

	/* Second remaining arg (if any) = result message. */
	if (iArg < argc) {
	    zMsg = argv[iArg];
	    nMsg = argl[iArg];
	}
    }
    return Th8_CancelEval(interp, zMsg, nMsg, flags);
}


/*
 *----------------------------------------------------------------------
 *
 * th8InterpSub --
 *
 *	Catalogue of `interp` sub-commands, installed into the `interp`
 *	ensemble command's per-interpreter sub-command hash at registration
 *	(TH8K-025).
 *
 * Why / How:
 *	Published as th8_interp_aSub so [info subcommands] can enumerate the
 *	available interp sub-commands.  Currently only "cancel" is supported;
 *	additional sub-commands (e.g. "create", "eval") may be added in future
 *	phases.
 *
 * Results:
 *	None (data table).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/* Published for th8_lang.c ensemble population (TH8K-025). */
const Th8_SubCommand *th8_interp_aSub;

static const Th8_SubCommand th8InterpSub[] =
    {{0, "cancel", interp_cancel_command}, {0, 0, 0}};


/*
 *----------------------------------------------------------------------
 *
 * namespace_current_command --
 *
 *	Implements the [namespace current] sub-command.  Returns the
 *	fully qualified name of the current namespace.
 *
 *	namespace current
 *
 * Why / How:
 *	Delegates to Th8_GetCurrentNamespace which returns the
 *	pCurrentNs pointer's name string.
 *
 * Results:
 *	TH8_OK.  Result is the current namespace name.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_current_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "namespace current");
    }
    Th8_SetResult(interp, Th8_GetCurrentNamespace(interp), TH8_NOLEN);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_eval_command --
 *
 *	Evaluate a script in a named namespace.
 *
 *	namespace eval NAME SCRIPT ?SCRIPT ...?
 *
 *	Delegates to Th8_NsEval which implements the pCurrentNs
 *	switch pattern: save the current namespace, set pCurrentNs
 *	to the named namespace (creating it if needed), evaluate
 *	the script, then restore pCurrentNs to the saved value.
 *	This ensures that commands and variables created during the
 *	script are placed in the target namespace.
 *
 *	When multiple scripts are given, they are concatenated with
 *	spaces before evaluation (same pattern as eval_command).
 *
 * Why / How:
 *	Single-script case delegates directly to Th8_NsEval.
 *	Multi-script case concatenates all script arguments with spaces
 *	first, then passes the combined script to Th8_NsEval.
 *
 * Results:
 *	Return code from the script evaluation.
 *
 * Side effects:
 *	May create the target namespace.  Commands and variables
 *	created during the script are placed in the target namespace.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_eval_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc < 4) {
	return Th8_WrongNumArgs(
	    interp, "namespace eval name script ?script ...?");
    }

    if (argc == 4) {
	return Th8_NsEval(interp, argv[2], argl[2], argv[3], argl[3]);
    } else {
	/*
	 * Concatenate all script args.
	 */

	char *zScript = 0;
	size_t nScript = 0;
	int i;
	int rc;

	for (i = 3; i < argc; i++) {
	    if (i > 3) {
		TH8_STR_APPEND(interp, &zScript, &nScript, " ", 1);
	    }
	    TH8_STR_APPEND(interp, &zScript, &nScript, argv[i], argl[i]);
	}
	rc = Th8_NsEval(interp, argv[2], argl[2], zScript, nScript);
	Th8_Free(interp, zScript);
	return rc;

oom:
	Th8_Free(interp, zScript);
	return TH8_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_exists_command --
 *
 *	Implements the [namespace exists] sub-command.  Checks
 *	whether a namespace exists.
 *
 *	namespace exists NAME
 *
 * Why / How:
 *	Delegates to Th8_FindNamespace with the create flag set to 0
 *	(lookup only).
 *
 * Results:
 *	TH8_OK.  Result is 1 if the namespace exists, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_exists_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "namespace exists name");
    }
    Th8_SetResultInt(interp, Th8_FindNamespace(interp, argv[2], argl[2], 0));
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_children_command --
 *
 *	Implements the [namespace children] sub-command.  Returns a
 *	list of child namespaces.
 *
 *	namespace children ?NAMESPACE?
 *
 * Why / How:
 *	Delegates to Th8_ListAppendNsChildren which iterates over
 *	the namespace tree and appends child names to a list.  If
 *	no namespace argument is given, lists children of the current
 *	namespace.
 *
 * Results:
 *	TH8_OK.  Result is a list of child namespace names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_children_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;
    const char *zNs = 0;
    size_t nNs = 0;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "namespace children ?namespace?");
    }
    if (argc == 3) {
	zNs = argv[2];
	nNs = argl[2];
    }
    Th8_ListAppendNsChildren(interp, zNs, nNs, &zList, &nList);
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_parent_command --
 *
 *	Implements the [namespace parent] sub-command.  Returns the
 *	parent namespace of a given namespace.
 *
 *	namespace parent ?NAMESPACE?
 *
 * Why / How:
 *	Delegates to th8GetNsParent which strips the last "::" segment
 *	from the namespace name.  If no namespace argument is given,
 *	returns the parent of the current namespace.
 *
 * Results:
 *	TH8_OK.  Result is the parent namespace name.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_parent_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const char *zParent;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "namespace parent ?namespace?");
    }
    if (argc == 3) {
	zParent = th8GetNsParent(interp, argv[2], argl[2]);
    } else {
	zParent = th8GetNsParent(interp, 0, 0);
    }
    Th8_SetResult(interp, zParent, TH8_NOLEN);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_delete_command --
 *
 *	Implements the [namespace delete] sub-command.  Deletes one
 *	or more namespaces.
 *
 *	namespace delete ?NAMESPACE ...?
 *
 * Why / How:
 *	Iterates over namespace arguments and delegates each to
 *	Th8_DeleteNamespace which removes the namespace and all its
 *	commands and variables.  Stops on the first error.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if deletion fails.
 *
 * Side effects:
 *	Removes namespaces and their contents from the interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_delete_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int i;
    int rc;

    if (argc < 3) {
	return Th8_WrongNumArgs(interp, "namespace delete ?namespace ...?");
    }
    for (i = 2; i < argc; i++) {
	rc = Th8_DeleteNamespace(interp, argv[i], argl[i]);
	if (rc != TH8_OK) return rc;
    }
    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_code_command --
 *
 *	Implements the [namespace code] sub-command.  Wraps a script
 *	so it evaluates in the current namespace.
 *
 *	namespace code SCRIPT
 *
 * Why / How:
 *	Constructs a string "::namespace eval NSNAME {script}" that
 *	can be passed as a callback.  When later evaluated, the
 *	wrapper ensures the script runs in the namespace that was
 *	current when [namespace code] was called.
 *
 * Results:
 *	TH8_OK.  Result is the wrapped script string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_code_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zOut = 0;
    size_t nOut = 0;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "namespace code script");
    }

    /*
     * Wrap the script in a [namespace eval] call that
     * evaluates it in the current namespace.
     */

    TH8_STR_APPEND(interp, &zOut, &nOut, "::namespace eval ", TH8_NOLEN);
    TH8_STR_APPEND(
        interp, &zOut, &nOut, Th8_GetCurrentNamespace(interp), TH8_NOLEN);
    TH8_STR_APPEND(interp, &zOut, &nOut, " ", 1);
    Th8_ListAppend(interp, &zOut, &nOut, argv[2], argl[2]);
    Th8_SetResult(interp, zOut, nOut);
    Th8_Free(interp, zOut);
    return TH8_OK;

oom:
    Th8_Free(interp, zOut);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_export_command --
 *
 *	Register export patterns on the current namespace.
 *	Exported commands can be imported by other namespaces.
 *
 *	namespace export ?PATTERN ...?
 *
 *	With no patterns, returns the current export list.
 *
 * Why / How:
 *	In query mode (no patterns), delegates to th8NsGetExport.
 *	In set mode, iterates over pattern arguments and registers
 *	each via Th8_NsExport.  Exported commands can subsequently
 *	be imported into other namespaces via [namespace import].
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if export registration fails.
 *
 * Side effects:
 *	Registers export patterns on the current namespace.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_export_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int i;

    if (argc == 2) {
	/*
	 * Query: return current export patterns.
	 */

	Th8_SetResult(interp, th8NsGetExport(interp, 0, 0), TH8_NOLEN);
	return TH8_OK;
    }
    for (i = 2; i < argc; i++) {
	int rc;

	rc = Th8_NsExport(interp, 0, 0, argv[i], argl[i]);
	if (rc != TH8_OK) return rc;
    }
    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_import_command --
 *
 *	Import exported commands from another namespace into the
 *	current namespace.
 *
 *	namespace import ?PATTERN ...?
 *
 *	Each PATTERN is a qualified glob like "::foo::*".
 *
 * Why / How:
 *	Checks for the optional -force flag, then iterates over
 *	pattern arguments delegating each to Th8_NsImport.  The
 *	-force flag enables overwriting existing commands in the
 *	target namespace.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if import fails.
 *
 * Side effects:
 *	Creates command entries in the current namespace that
 *	delegate to the source namespace's exported commands.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_import_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int i;
    int iStart = 2; /* Index of first pattern argument. */

    if (argc < 3) {
	return Th8_WrongNumArgs(
	    interp, "namespace import ?-force? pattern"
	            " ?pattern ...?");
    }

    /*
     * Check for the -force option.  When specified, existing
     * commands in the target namespace are overwritten.
     *
     * The L639 early-return guarantees argc >= 3 here, so the
     * argc > 2 sub-check is ALWAYS T at runtime.  Wrap to fold
     * it away from MC/DC analysis. */

    if (ALWAYS(argc > 2) && th8StrEq(interp, argv[2], argl[2], "-force")) {
	iStart = 3;
    }

    for (i = iStart; i < argc; i++) {
	int rc;

	rc = Th8_NsImport(interp, argv[i], argl[i], (iStart > 2));
	if (rc != TH8_OK) return rc;
    }
    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_origin_command --
 *
 *	Implements [namespace origin name].  Returns the fully
 *	qualified name of the original (non-imported) command.
 *	TH8 does not track import chains, so this is equivalent
 *	to [namespace which -command name] -- it resolves the
 *	command and returns its qualified name.
 *
 * Why / How:
 *	Builds a fully qualified name -- using the argument as-is
 *	when it already starts with "::", otherwise prefixing the
 *	current namespace -- then confirms the command exists by
 *	evaluating `info commands <qualified>` and checking for a
 *	non-empty result.  Because TH8 keeps no import bookkeeping,
 *	the "origin" is just the resolved qualified name rather than
 *	a walk back through an import chain.
 *
 * Results:
 *	TH8_OK with the fully qualified command name as the
 *	interpreter result; TH8_ERROR with an `invalid command
 *	name "..."` message if no such command exists, on a wrong
 *	argument count, or on an allocation failure.
 *
 * Side effects:
 *	Evaluates an `info commands` command (which sets the
 *	interpreter result); allocates and frees the qualified-name
 *	buffer; sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_origin_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zFull = 0;
    size_t nFull = 0;
    const char *zNs;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "namespace origin name");
    }

    /*
     * If the name is already qualified, check it directly.
     * Otherwise prepend the current namespace.
     */
    if (argl[2] > 2 && argv[2][0] == ':' && argv[2][1] == ':') {
	TH8_STR_APPEND(interp, &zFull, &nFull, argv[2], argl[2]);
    } else {
	zNs = Th8_GetCurrentNamespace(interp);
	if (ALWAYS(zNs[0] == ':') && ALWAYS(zNs[1] == ':') &&
	    zNs[2] == '\0') {
	    TH8_STR_APPEND(interp, &zFull, &nFull, "::", 2);
	} else {
	    TH8_STR_APPEND(interp, &zFull, &nFull, zNs, TH8_NOLEN);
	    TH8_STR_APPEND(interp, &zFull, &nFull, "::", 2);
	}
	TH8_STR_APPEND(interp, &zFull, &nFull, argv[2], argl[2]);
    }

    {
	char *zCmd = 0;
	size_t nCmd = 0;
	int found = 0;

	Th8_ListAppend(interp, &zCmd, &nCmd, "info", 4);
	Th8_ListAppend(interp, &zCmd, &nCmd, "commands", 8);
	Th8_ListAppend(interp, &zCmd, &nCmd, zFull, nFull);
	if (Th8_Eval(interp, 0, zCmd, nCmd, NULL, 0) == TH8_OK) {
	    size_t nRes;
	    Th8_GetResult(interp, &nRes);
	    found = (nRes > 0);
	}
	Th8_Free(interp, zCmd);

	if (found) {
	    Th8_SetResult(interp, zFull, nFull);
	} else {
	    Th8_ErrorMessage(
	        interp, "invalid command name \"", argv[2], argl[2]);
	    Th8_Free(interp, zFull);
	    return TH8_ERROR;
	}
    }
    Th8_Free(interp, zFull);
    return TH8_OK;

oom:
    Th8_Free(interp, zFull);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * namespace_which_command --
 *
 *	Implements [namespace which ?-command? ?-variable? name].
 *	Returns the fully qualified name of a command or variable.
 *	With -command (default): resolves the command name.
 *	With -variable: resolves the variable name.
 *	Returns empty string if not found.
 *
 * Why / How:
 *	Parses the leading `-command` / `-variable` flags (last one
 *	wins) to pick the resolution mode, then qualifies the name
 *	(used verbatim when already "::"-prefixed, otherwise prefixed
 *	with the current namespace).  In variable mode it checks
 *	Th8_ExistsVar (or, when built without TH8_ENABLE_VARIABLES,
 *	always reports not-found per Bug 35); in command mode it
 *	tests existence via `info commands`.  A hit sets the result
 *	to the qualified name; a miss clears it, matching Tcl's
 *	empty-string-on-not-found contract.
 *
 * Results:
 *	TH8_OK with the fully qualified name on a hit, or an empty
 *	interpreter result on a miss; TH8_ERROR on a wrong argument
 *	count/missing name or on an allocation failure.
 *
 * Side effects:
 *	In command mode, evaluates an `info commands` command (which
 *	sets the interpreter result); allocates and frees the
 *	qualified-name buffer; sets or clears the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
namespace_which_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int iArg = 2;
    int isVar = 0;
    char *zFull = 0;
    size_t nFull = 0;
    const char *zNs;

    (void)ctx;

    if (argc < 3 || argc > 4) {
	return Th8_WrongNumArgs(
	    interp, "namespace which ?-command? ?-variable? name");
    }

    while (iArg < argc - 1) {
	if (th8StrEq(interp, argv[iArg], argl[iArg], "-command")) {
	    isVar = 0;
	    iArg++;
	} else if (th8StrEq(interp, argv[iArg], argl[iArg], "-variable")) {
	    isVar = 1;
	    iArg++;
	} else {
	    break;
	}
    }

    if (iArg >= argc) {
	return Th8_WrongNumArgs(
	    interp, "namespace which ?-command? ?-variable? name");
    }

    /*
     * Build the fully qualified name.
     */
    if (argl[iArg] > 2 && argv[iArg][0] == ':' && argv[iArg][1] == ':') {
	TH8_STR_APPEND(interp, &zFull, &nFull, argv[iArg], argl[iArg]);
    } else {
	zNs = Th8_GetCurrentNamespace(interp);
	if (ALWAYS(zNs[0] == ':') && ALWAYS(zNs[1] == ':') &&
	    zNs[2] == '\0') {
	    TH8_STR_APPEND(interp, &zFull, &nFull, "::", 2);
	} else {
	    TH8_STR_APPEND(interp, &zFull, &nFull, zNs, TH8_NOLEN);
	    TH8_STR_APPEND(interp, &zFull, &nFull, "::", 2);
	}
	TH8_STR_APPEND(interp, &zFull, &nFull, argv[iArg], argl[iArg]);
    }

    if (isVar) {
#  if defined(TH8_ENABLE_VARIABLES)
	if (Th8_ExistsVar(interp, zFull, nFull)) {
	    Th8_SetResult(interp, zFull, nFull);
	} else {
	    Th8_ClearResult(interp);
	}
#  else
	/* Bug 35: without variables, namespace which -variable
	 * can only report "not found". */
	Th8_ClearResult(interp);
#  endif
    } else {
	char *zCmd = 0;
	size_t nCmd = 0;
	int found = 0;

	Th8_ListAppend(interp, &zCmd, &nCmd, "info", 4);
	Th8_ListAppend(interp, &zCmd, &nCmd, "commands", 8);
	Th8_ListAppend(interp, &zCmd, &nCmd, zFull, nFull);
	if (Th8_Eval(interp, 0, zCmd, nCmd, NULL, 0) == TH8_OK) {
	    size_t nRes;
	    Th8_GetResult(interp, &nRes);
	    found = (nRes > 0);
	}
	Th8_Free(interp, zCmd);

	if (found) {
	    Th8_SetResult(interp, zFull, nFull);
	} else {
	    Th8_ClearResult(interp);
	}
    }
    Th8_Free(interp, zFull);
    return TH8_OK;

oom:
    Th8_Free(interp, zFull);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NamespaceSub --
 *
 *	Catalogue of `namespace` sub-commands, installed into the `namespace`
 *	ensemble command's per-interpreter sub-command hash at registration
 *	(TH8K-025).
 *
 * Why / How:
 *	Published as th8_namespace_aSub so [info subcommands] can enumerate the
 *	available namespace sub-commands.
 *
 * Results:
 *	Return code from the sub-command.
 *
 * Side effects:
 *	Determined by the sub-command.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand th8NamespaceSub[] =
    {{0, "children", namespace_children_command},
     {0, "code", namespace_code_command},
     {0, "current", namespace_current_command},
     {0, "delete", namespace_delete_command},
     {0, "eval", namespace_eval_command},
     {0, "exists", namespace_exists_command},
     {0, "export", namespace_export_command},
     {0, "import", namespace_import_command},
     {0, "origin", namespace_origin_command},
     {0, "parent", namespace_parent_command},
     {0, "which", namespace_which_command},
     {0, 0, 0}};


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8ManagementCommands[] = {
    {1, 0, "interp", 0}, /* pure ensemble (TH8K-025) */
    {1, 0, "namespace", 0}, /* pure ensemble (TH8K-025) */
    {1, 0, "rename", rename_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8ManagementGetCommands --
 *
 *	Return the command table for the management plugin.
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
th8ManagementGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8ManagementCommands) /
                  sizeof(th8ManagementCommands[0]));

    th8_namespace_aSub = th8NamespaceSub;
    th8_interp_aSub = th8InterpSub; /* TH8K-025 */

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
	    pCommand[i] = th8ManagementCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_MANAGEMENT */
