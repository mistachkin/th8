/*
 * th8_lang.c -- Built-in language commands for TH8.
 *
 * This file implements approximately 90 built-in TH8 commands that
 * collectively form the Tcl-like scripting language.  Every command
 * is implemented using *only* the public Th8_ interface declared in
 * th8.h -- no internal interpreter structures are accessed directly.
 * As such, this file serves two roles:
 *
 *   1. The language implementation: control flow (for, while, foreach,
 *      if, switch, catch), variables (set, append, incr, unset, global,
 *      upvar), procedures (proc, nproc, apply, napply, tailcall),
 *      lists, strings, formatting (format, scan), I/O (puts, gets),
 *      introspection (info), packages, namespaces, and more.
 *
 *   2. An example of how to extend TH8: every pattern used here --
 *      argument validation, NRE callback chains, sub-command dispatch
 *      tables, heap-allocated state structs, the Th8_ProcDefn memory
 *      layout -- is available to embedders writing their own commands.
 *
 * NRE (Non-Recursive Evaluation) conventions:
 *   - Loop commands (for, while, foreach) push NR callbacks instead
 *     of using C recursion, keeping the C stack depth constant
 *     regardless of iteration count.
 *   - Callbacks use a pData[4] array to carry state between
 *     invocations.  The layout is documented per command.
 *   - An "empty-body step guard" pattern prevents infinite loops
 *     when the loop body produces zero interpreter steps: if the
 *     step count is unchanged after the body, Th8_Ready is called
 *     to force a step and allow cancellation/resource-limit checks.
 *   - break propagation: callbacks return TH8_OK on TH8_BREAK.
 *   - continue propagation: callbacks convert TH8_CONTINUE to
 *     TH8_OK, then re-enter the loop condition.
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


/*
 *----------------------------------------------------------------------
 *
 * Command registration --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_ResetSecurityArray --
 *
 *	Set all elements of the ::th8_security array to "none".
 *	This is the canonical way to reset the security state;
 *	all callers should use this instead of setting the
 *	individual elements directly.
 *
 * Why / How:
 *	The security array has seven elements (algorithmName,
 *	dataName, flags, notAfter, notBefore, policy,
 *	publicKeyToken) that must always be set together.  This
 *	function guarantees atomicity of the reset: no caller can
 *	accidentally leave a stale element.  Bug 41 fix (2026-06-07):
 *	`flags` was previously populated by the harpy annotation
 *	extractor but not reset here, leaving a stale value across
 *	subsequent security contexts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets seven elements of the ::th8_security array variable.
 *
 *----------------------------------------------------------------------
 */

#if defined(TH8_ENABLE_VARIABLES)
void
Th8_ResetSecurityArray(Th8_Interp *interp)
{
    Th8_SetVar(
        interp, "::th8_security(algorithmName)", TH8_NOLEN, "none",
        TH8_NOLEN);
    Th8_SetVar(
        interp, "::th8_security(dataName)", TH8_NOLEN, "none", TH8_NOLEN);
    Th8_SetVar(interp, "::th8_security(flags)", TH8_NOLEN, "none", TH8_NOLEN);
    Th8_SetVar(
        interp, "::th8_security(notAfter)", TH8_NOLEN, "none", TH8_NOLEN);
    Th8_SetVar(
        interp, "::th8_security(notBefore)", TH8_NOLEN, "none", TH8_NOLEN);
    Th8_SetVar(
        interp, "::th8_security(policy)", TH8_NOLEN, "none", TH8_NOLEN);
    Th8_SetVar(
        interp, "::th8_security(publicKeyToken)", TH8_NOLEN, "none",
        TH8_NOLEN);
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * th8RegisterStaticPlugins --
 *
 *	Register all built-in (statically linked) plugins.  Each
 *	entry in the table maps a plugin name to its GetCommands
 *	function.  Compile-time gates control which plugins are
 *	included.  Duplicate registration (e.g., from
 *	Th8_RestoreInterp) is silently tolerated.
 *
 * Why / How:
 *	Iterates a compile-time-gated array of plugin name/callback
 *	pairs, calling Th8_RegisterPlugin for each.  The table is
 *	static so the set of built-in plugins is determined entirely
 *	at compile time.  Re-registration is harmless because the
 *	plugin system deduplicates by name.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Registers plugins in the interpreter's plugin hash table.
 *
 *----------------------------------------------------------------------
 */

static void
th8RegisterStaticPlugins(Th8_Interp *interp)
{
    static struct {
	const char *zName;
	Th8_GetCommandsProc xGetCommands;
    } aPlugin[] = {
#if defined(TH8_PLUGIN_BINARY)
        {"binary", th8BinaryGetCommands},
#endif
#if defined(TH8_PLUGIN_CONTROL)
        {"control", th8ControlGetCommands},
#endif
#if defined(TH8_PLUGIN_EVENTS)
        {"events", th8EventsGetCommands},
#endif
#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
        {"cryptography", th8CryptoGetCommands},
#endif
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
        {"harpy", th8HarpyGetCommands},
#endif
#if defined(TH8_PLUGIN_EXPRESSIONS)
        {"expressions", th8ExpressionsGetCommands},
#endif
#if defined(TH8_PLUGIN_EXTENSIBILITY)
        {"extensibility", th8ExtensibilityGetCommands},
#endif
#if defined(TH8_PLUGIN_FILE_SYSTEMS)
        {"file_systems", th8FilesystemsGetCommands},
#endif
#if defined(TH8_PLUGIN_FORMATTING)
        {"formatting", th8FormattingGetCommands},
#endif
#if defined(TH8_PLUGIN_INTROSPECTION)
        {"introspection", th8IntrospectionGetCommands},
#endif
#if defined(TH8_PLUGIN_IO)
        {"io", th8IoGetCommands},
#endif
#if defined(TH8_PLUGIN_LISTS)
        {"lists", th8ListsGetCommands},
#endif
#if defined(TH8_PLUGIN_LOOPING)
        {"looping", th8LoopingGetCommands},
#endif
#if defined(TH8_PLUGIN_MANAGEMENT)
        {"management", th8ManagementGetCommands},
#endif
#if defined(TH8_PLUGIN_PROCEDURES)
        {"procedures", th8ProceduresGetCommands},
#endif
#if defined(TH8_ENABLE_REGEXP)
        {"regular_expressions", th8RegexpGetCommands},
#endif
#if defined(TH8_PLUGIN_STRINGS)
        {"strings", th8StringsGetCommands},
#endif
#if defined(TH8_PLUGIN_TIMEKEEPING)
        {"timekeeping", th8TimekeepingGetCommands},
#endif
#if defined(TH8_PLUGIN_VARIABLES)
        {"variables", th8VariablesGetCommands},
#endif
    };
    int i;
    int n = (int)(sizeof(aPlugin) / sizeof(aPlugin[0]));

    for (i = 0; i < n; i++) {
	/* Silently tolerate re-registration. */
	(void)Th8_RegisterPlugin(
	    interp, aPlugin[i].zName, aPlugin[i].xGetCommands);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RegisterLanguage --
 *
 *	Master registration table: maps command names to their C
 *	implementation functions and registers them all in the
 *	interpreter via Th8_CreateCommand.
 *
 *	The table is organized by category:
 *	  - Time and process (clock, pid)
 *	  - Control flow (break, catch, continue, error, eval, for,
 *	    foreach, if, return, while, switch)
 *	  - Variables (append, array, global, incr, set, unset,
 *	    uplevel, upvar, variable)
 *	  - Expressions (expr)
 *	  - Formatting (format, scan)
 *	  - Procedures and lambdas (apply, downlevel, napply, nproc,
 *	    proc, rename, tailcall)
 *	  - Lists (lappend, lindex, list, llength, lrange, lreplace,
 *	    lsearch, lsort, join, split)
 *	  - Strings (string -- dispatches to sub-commands)
 *	  - File system (cd, pwd)
 *	  - I/O (gets, puts, source)
 *	  - Introspection (info, interp)
 *	  - Packages (package)
 *	  - Namespaces (namespace)
 *
 *	NOTE: regexp/regsub are registered separately by
 *	Th8_RegisterRegex() in th8_regex.c.
 *
 * Why / How:
 *	All commands are registered through the plugin system via
 *	th8RegisterStaticPlugins.  This function also initializes
 *	ensemble sub-command table pointers by triggering each
 *	ensemble once, registers the {*} expansion operator, the
 *	built-in math functions, the built-in Tcl/TH8 packages,
 *	and the ::th8_security system variable array.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Commands are created in the interpreter.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RegisterLanguage(Th8_Interp *interp) /* Interpreter. */
{
    TH8_ASSERT_OWNER(interp);

    /*
     * All commands are now registered through the plugin system.
     * The aCmd[] table has been fully replaced by th8RegisterStaticPlugins.
     */

    /*
     * Force-initialize the ensemble subcommand table pointers
     * by calling each ensemble once (no-arg call triggers
     * pointer assignment before returning an error, which we
     * discard).
     */

    {
	const char *azEns[] = {"info",    "file",  "string", "namespace",
	                       "package", "array", 0};
	size_t anEns[] = {4, 4, 6, 9, 7, 5};
	int j;

	for (j = 0; azEns[j]; j++) {
	    Th8_Eval(interp, 0, azEns[j], anEns[j], NULL, 0);
	}
	Th8_ClearResult(interp);
    }

    /*
     * Register the built-in {*} expansion operator.
     * This enables Tcl 8.5 argument expansion syntax.
     * The host can unregister it for strict 8.4 compatibility.
     */

    Th8_RegisterExpansion(interp, "*", 1, 0, 0);

    /*
     * Register built-in math functions (abs, int, double, sin,
     * cos, etc.) from the static table in th8_math.c.
     */

#if defined(TH8_ENABLE_EXPRESSIONS)
    th8RegisterMathFuncs(interp);
#endif

    /*
     * Register built-in plugins.  This must happen before any
     * Th8_Eval calls that use plugin-provided commands (package,
     * file, lappend, etc.).
     */

    th8RegisterStaticPlugins(interp);

    /*
     * Add the directory containing the process executable to the
     * auto-path because it may contain (test?) packages, etc.
     */

    Th8_Eval(
        interp, 0,
        "lappend ::auto_path [file dirname [info nameofexecutable]]",
        TH8_NOLEN, NULL, 0);

    /*
     * Provide the built-in static packages that are already to
     * be present in the interpreter.
     */

    Th8_Eval(interp, 0, "package provide Tcl 8.6.21", TH8_NOLEN, NULL, 0);
    Th8_Eval(interp, 0, "package provide TH8 1.0", TH8_NOLEN, NULL, 0);

    /*
     * System variables: read-only from scripts.
     *
     * th8_security: array with security configuration.
     *   algorithmName  -- signing algorithm (e.g. "none" or "RSA-4096")
     *   policy         -- "none", "warn", or "enforce"
     *   publicKeyToken -- hex token of the trusted key (or "")
     */

#if defined(TH8_ENABLE_VARIABLES)
    Th8_ResetSecurityArray(interp);

    Th8_DeclareSystemVar(interp, "::th8_security", TH8_NOLEN);
#endif

    return TH8_OK;
}
