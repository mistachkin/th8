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

#if defined(TH8_ENABLE_VARIABLES)
/*
 *----------------------------------------------------------------------
 *
 * th8ResetSecurityArray --
 *
 *	Internal checked reset of the ::th8_security array.  Sets all
 *	seven elements to "none".
 *
 * Why / How:
 *	Writes each of the seven ::th8_security elements (algorithmName,
 *	dataName, flags, notAfter, notBefore, policy, publicKeyToken) to
 *	"none" with an independent, allocation-capable Th8_SetVar, and
 *	fails fast on the FIRST write that fails.  Each write is its own
 *	single-condition if so no compound MC/DC decision is formed.  On
 *	failure the array is left partially set; the caller
 *	(Th8_RegisterLanguage) discards the interpreter rather than
 *	presenting a partial security state as a complete language
 *	(TH8K-006).
 *
 * Results:
 *	TH8_OK only when all seven writes succeed; TH8_ERROR on the first
 *	Th8_SetVar failure.
 *
 * Side effects:
 *	Sets (and may allocate storage for) the ::th8_security array
 *	elements in the interpreter; on failure the array is left in a
 *	partially reset state.
 *
 *----------------------------------------------------------------------
 */
static int
th8ResetSecurityArray(Th8_Interp *interp)
{
    if (Th8_SetVar(
            interp, "::th8_security(algorithmName)", TH8_NOLEN, "none",
            TH8_NOLEN) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_SetVar(
            interp, "::th8_security(dataName)", TH8_NOLEN, "none",
            TH8_NOLEN) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_SetVar(
            interp, "::th8_security(flags)", TH8_NOLEN, "none", TH8_NOLEN) !=
        TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_SetVar(
            interp, "::th8_security(notAfter)", TH8_NOLEN, "none",
            TH8_NOLEN) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_SetVar(
            interp, "::th8_security(notBefore)", TH8_NOLEN, "none",
            TH8_NOLEN) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_SetVar(
            interp, "::th8_security(policy)", TH8_NOLEN, "none", TH8_NOLEN) !=
        TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_SetVar(
            interp, "::th8_security(publicKeyToken)", TH8_NOLEN, "none",
            TH8_NOLEN) != TH8_OK) {
	return TH8_ERROR;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_ResetSecurityArray --
 *
 *	Public entry point: set all seven elements of the ::th8_security
 *	array to "none".  This is the canonical way to reset the security
 *	state; callers should use it instead of setting the individual
 *	elements directly.
 *
 * Why / How:
 *	Delegates to the internal checked th8ResetSecurityArray and
 *	propagates its status, so a caller can no longer be handed a
 *	silently partial security array (TH8K-006/-020).  On TH8_ERROR the
 *	array may be partially reset; the security state is indeterminate
 *	and the caller MUST treat that as fatal for the current evaluation
 *	(Th8_RegisterLanguage discards the interpreter; the harpy policy
 *	chain rejects the eval), so a partial array is never observed by a
 *	running script.  Bug 41 fix (2026-06-07): `flags` was previously
 *	populated by the harpy annotation extractor but not reset here,
 *	leaving a stale value across subsequent security contexts.
 *
 * Results:
 *	TH8_OK when all seven elements were reset; TH8_ERROR on the first
 *	Th8_SetVar allocation failure (array possibly partially reset).
 *
 * Side effects:
 *	Sets seven elements of the ::th8_security array variable.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ResetSecurityArray(Th8_Interp *interp)
{
    return th8ResetSecurityArray(interp);
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * th8PopulateEnsemble --
 *
 *	Install every sub-command in a static catalogue into an ensemble
 *	command's per-interpreter sub-command hash (TH8K-025).
 *
 * Why / How:
 *	Iterates the NUL-terminated Th8_SubCommand catalogue and calls the
 *	public Th8_CreateSubCommand for each; the parent command must already
 *	exist.  A NULL catalogue (an unbuilt plugin) is a no-op success.  This
 *	is the full-language path; sub-command subsetting will select a subset
 *	of the catalogue instead.
 *
 * Results:
 *	TH8_OK when every sub-command registered, or TH8_ERROR (with the
 *	failing call's message) on the first failure.
 *
 * Side effects:
 *	Populates the ensemble command's sub-command hash.
 *
 *----------------------------------------------------------------------
 */

static int
th8PopulateEnsemble(
    Th8_Interp *interp, /* Interpreter. */
    const char *zCmdName, /* Ensemble command name. */
    const Th8_SubCommand *aSub) /* Sub-command catalogue (NUL-terminated). */
{
    int i;

    if (!aSub) return TH8_OK; /* Plugin not built -- nothing to populate. */
    for (i = 0; aSub[i].zName; i++) {
	if (Th8_CreateSubCommand(
	        interp, zCmdName, aSub[i].zName, aSub[i].xProc, 0, 0, 0) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
    }
    return TH8_OK;
}

/*
 * The built-in (statically linked) plugin registry: plugin name -> its
 * GetCommands function, compile-time gated.  File-static so both
 * th8RegisterStaticPlugins (register all) and the named-subset engine
 * (register a selected plugin) resolve plugins from one source of truth.
 */

typedef struct th8StaticPluginRec {
    const char *zName;
    Th8_GetCommandsProc xGetCommands;
} th8StaticPluginRec;

static const th8StaticPluginRec th8StaticPlugins[] = {
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

#define TH8_NUM_STATIC_PLUGINS                                               \
    ((int)(sizeof(th8StaticPlugins) / sizeof(th8StaticPlugins[0])))

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
 *	TH8_OK if every static plugin is registered (or was already
 *	registered); TH8_ERROR if a plugin fails to register for a real
 *	reason (e.g. OOM), so language registration is not reported
 *	complete when it is not (TH8K-006).
 *
 * Side effects:
 *	Registers plugins in the interpreter's plugin hash table.
 *
 *----------------------------------------------------------------------
 */

static int
th8RegisterStaticPlugins(Th8_Interp *interp)
{
    int i;

    for (i = 0; i < TH8_NUM_STATIC_PLUGINS; i++) {
	size_t nName = Th8_Strlen(interp, th8StaticPlugins[i].zName);

	/*
	 * Tolerate re-registration (Th8_RestoreInterp re-runs this on an
	 * interp that still has its plugins): skip a plugin that is
	 * already present.  But a plugin that is NOT present and fails to
	 * register is a real failure (e.g. OOM) that must be propagated
	 * so the language is not reported complete when it is not
	 * (TH8K-006).
	 */
	if (th8PluginRegistered(interp, th8StaticPlugins[i].zName, nName)) {
	    continue;
	}
	if (Th8_RegisterPlugin(
	        interp, th8StaticPlugins[i].zName,
	        th8StaticPlugins[i].xGetCommands) != TH8_OK) {
	    return TH8_ERROR;
	}
    }
    return TH8_OK;
}


/*
 * Ensemble registry: which plugin owns each ensemble command, and the
 * published sub-command catalogue to populate it from.  One source of truth for
 * both full-language registration (Th8_RegisterLanguage) and plugin-subset
 * registration.  Names are bare (th8LookupCommand resolves them in the global
 * namespace).  Each ppaSub points at the plugin-owned catalogue pointer, which
 * the plugin's *GetCommands assigns; it is non-NULL once that plugin is built.
 */

typedef struct th8EnsembleRec {
    const char *zPlugin; /* Owning plugin (matches th8StaticPlugins). */
    const char *zEnsemble; /* Ensemble command name (bare). */
    const Th8_SubCommand **ppaSub; /* Published catalogue pointer. */
} th8EnsembleRec;

static const th8EnsembleRec th8Ensembles[] = {
#if defined(TH8_PLUGIN_STRINGS)
    {"strings", "string", &th8_string_aSub},
#endif
#if defined(TH8_PLUGIN_INTROSPECTION)
    {"introspection", "info", &th8_info_aSub},
#endif
#if defined(TH8_PLUGIN_FILE_SYSTEMS)
    {"file_systems", "file", &th8_file_aSub},
#endif
#if defined(TH8_PLUGIN_MANAGEMENT)
    {"management", "namespace", &th8_namespace_aSub},
    {"management", "interp", &th8_interp_aSub},
#endif
#if defined(TH8_PLUGIN_VARIABLES)
    {"variables", "array", &th8_array_aSub},
#endif
#if defined(TH8_PLUGIN_EXTENSIBILITY)
    {"extensibility", "package", &th8_package_aSub},
#endif
#if defined(TH8_PLUGIN_TIMEKEEPING)
    {"timekeeping", "clock", &th8_clock_aSub},
#endif
#if defined(TH8_PLUGIN_LISTS)
    {"lists", "dict", &th8_dict_aSub},
#endif
#if defined(TH8_PLUGIN_BINARY)
    {"binary", "binary", &th8_binary_aSub},
#endif
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    {"harpy", "flags", &th8_flags_aSub},
#endif
};

#define TH8_NUM_ENSEMBLES                                                    \
    ((int)(sizeof(th8Ensembles) / sizeof(th8Ensembles[0])))

/*
 *----------------------------------------------------------------------
 *
 * th8CStrEq --
 *
 *	Return 1 iff the two NUL-terminated strings are equal.
 *
 * Why / How:
 *	A small helper for the subset engine's name comparisons (catalogue names
 *	and caller-supplied subset names are all C strings), using the platform
 *	length and compare primitives.
 *
 * Results:
 *	1 if equal, else 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8CStrEq(Th8_Interp *interp, const char *zA, const char *zB)
{
    size_t nA = Th8_Strlen(interp, zA);

    return nA == Th8_Strlen(interp, zB) &&
           Th8_Memcmp(interp, zA, zB, nA) == 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PopulatePluginEnsembles --
 *
 *	Populate the per-interpreter sub-command hashes of every ensemble owned
 *	by plugin zPlugin, from that plugin's published catalogues.
 *
 * Why / How:
 *	Called after a plugin is registered (full language or a plugin subset)
 *	so its ensemble commands gain their sub-commands.  A catalogue pointer
 *	that is still NULL (plugin not built) is skipped.
 *
 * Results:
 *	TH8_OK, or TH8_ERROR on a sub-command registration failure (e.g. OOM).
 *
 * Side effects:
 *	Registers sub-commands into the interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
th8PopulatePluginEnsembles(Th8_Interp *interp, const char *zPlugin)
{
    int i;

    for (i = 0; i < TH8_NUM_ENSEMBLES; i++) {
	if (th8CStrEq(interp, zPlugin, th8Ensembles[i].zPlugin) &&
	    *th8Ensembles[i].ppaSub) {
	    if (th8PopulateEnsemble(
	            interp, th8Ensembles[i].zEnsemble,
	            *th8Ensembles[i].ppaSub) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
    }
    return TH8_OK;
}

/*
 * Curated fine-grained subset: a read-only slice of [file].  Demonstrates the
 * hybrid model -- individual COMMAND + SUBCOMMAND members alongside the
 * plugin-granular subsets.  The COMMAND member installs the (NULL-handler)
 * ensemble shell; the SUBCOMMAND members fill in only the safe, read-only
 * operations, so `file delete` etc. are simply absent.
 */

static const Th8_SubsetMemberDef th8SafeFileMembers[] = {
#if defined(TH8_PLUGIN_FILE_SYSTEMS)
    {TH8_SUBSET_COMMAND, 0, "file"},
    {TH8_SUBSET_SUBCOMMAND, "file", "exists"},
    {TH8_SUBSET_SUBCOMMAND, "file", "dirname"},
    {TH8_SUBSET_SUBCOMMAND, "file", "tail"},
    {TH8_SUBSET_SUBCOMMAND, "file", "extension"},
    {TH8_SUBSET_SUBCOMMAND, "file", "rootname"},
    {TH8_SUBSET_SUBCOMMAND, "file", "join"},
#endif
    {0, 0, 0} /* Guarantees a non-empty array when file_systems is off. */
};

/* "math": grant [expr] plus the math functions.  Demonstrates the FUNCTION
 * member type (a specific function by name AND the "*" wildcard for all), the
 * capability the hybrid subset model exposes for embedders that want arithmetic
 * without the rest of the language. */
static const Th8_SubsetMemberDef th8MathMembers[] = {
#if defined(TH8_ENABLE_EXPRESSIONS)
    {TH8_SUBSET_COMMAND, 0, "expr"},
    {TH8_SUBSET_FUNCTION, 0, "abs"},
    {TH8_SUBSET_FUNCTION, 0, "*"},
#endif
    {0, 0, 0} /* Guarantees a non-empty array when expressions are off. */
};

static const Th8_SubsetDef th8CuratedSubsets[] = {
    {1, "safe-file", th8SafeFileMembers,
     (int)(sizeof(th8SafeFileMembers) / sizeof(th8SafeFileMembers[0])) - 1},
    {1, "math", th8MathMembers,
     (int)(sizeof(th8MathMembers) / sizeof(th8MathMembers[0])) - 1},
};

#define TH8_NUM_CURATED                                                      \
    ((int)(sizeof(th8CuratedSubsets) / sizeof(th8CuratedSubsets[0])))

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetFindPlugin --
 *
 *	Resolve a subset NAME to a built-in plugin record (each plugin is a
 *	subset whose membership is its own table), or NULL.
 *
 * Why / How:
 *	Linear scan of the static plugin registry by name.
 *
 * Results:
 *	The matching plugin record, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static const th8StaticPluginRec *
th8SubsetFindPlugin(Th8_Interp *interp, const char *zName)
{
    int i;

    for (i = 0; i < TH8_NUM_STATIC_PLUGINS; i++) {
	if (th8CStrEq(interp, zName, th8StaticPlugins[i].zName)) {
	    return &th8StaticPlugins[i];
	}
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetFindCurated --
 *
 *	Resolve a subset NAME to a curated subset definition, or NULL.
 *
 * Why / How:
 *	Linear scan of the curated subset catalogue by name.
 *
 * Results:
 *	The matching curated subset, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubsetDef *
th8SubsetFindCurated(Th8_Interp *interp, const char *zName)
{
    int i;

    for (i = 0; i < TH8_NUM_CURATED; i++) {
	if (th8CStrEq(interp, zName, th8CuratedSubsets[i].zName)) {
	    return &th8CuratedSubsets[i];
	}
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetFindCommandProc --
 *
 *	Resolve a command NAME to its implementation by scanning the built-in
 *	plugin command tables (the authoritative source).
 *
 * Why / How:
 *	For a COMMAND subset member: ask each plugin's GetCommands for its table
 *	and match the name.  An ensemble command legitimately has a NULL xProc,
 *	so "found" is reported separately from the (possibly NULL) proc.
 *
 * Results:
 *	1 if found (writes *pxProc), 0 if no such built-in command, -1 on an
 *	allocation failure while scanning.
 *
 * Side effects:
 *	None (temporary allocations are freed).
 *
 *----------------------------------------------------------------------
 */

static int
th8SubsetFindCommandProc(
    Th8_Interp *interp,
    const char *zName,
    Th8_CommandProc *pxProc)
{
    size_t nName = Th8_Strlen(interp, zName);
    int pi;

    for (pi = 0; pi < TH8_NUM_STATIC_PLUGINS; pi++) {
	int nCmd = 0;
	Th8_CommandEntry *aEntry;
	int ci;

	if (th8StaticPlugins[pi].xGetCommands(NULL, &nCmd) != TH8_OK ||
	    nCmd <= 0) {
	    continue;
	}
	aEntry = (Th8_CommandEntry *)
	    TH8_ALLOC_MUL(interp, (size_t)nCmd, sizeof(Th8_CommandEntry));
	if (!aEntry) return -1;
	if (th8StaticPlugins[pi].xGetCommands(aEntry, &nCmd) != TH8_OK) {
	    Th8_Free(interp, aEntry);
	    continue;
	}
	for (ci = 0; ci < nCmd; ci++) {
	    if (Th8_Strlen(interp, aEntry[ci].zName) == nName &&
	        Th8_Memcmp(interp, aEntry[ci].zName, zName, nName) == 0) {
		*pxProc = aEntry[ci].xProc;
		Th8_Free(interp, aEntry);
		return 1;
	    }
	}
	Th8_Free(interp, aEntry);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetFindEnsembleSub --
 *
 *	Resolve an (ensemble, sub-command) pair to its catalogue entry.
 *
 * Why / How:
 *	Find the ensemble in th8Ensembles (by bare command name), then scan its
 *	published catalogue for the sub-command name.
 *
 * Results:
 *	The Th8_SubCommand entry, or NULL if the ensemble or sub-command is not
 *	a built-in.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand *
th8SubsetFindEnsembleSub(
    Th8_Interp *interp,
    const char *zEnsemble,
    const char *zSub)
{
    size_t nSub = Th8_Strlen(interp, zSub);
    int i;

    for (i = 0; i < TH8_NUM_ENSEMBLES; i++) {
	const Th8_SubCommand *aSub;
	int j;

	if (!th8CStrEq(interp, zEnsemble, th8Ensembles[i].zEnsemble)) {
	    continue;
	}
	aSub = *th8Ensembles[i].ppaSub;
	if (!aSub) return NULL;
	for (j = 0; aSub[j].zName; j++) {
	    if (Th8_Strlen(interp, aSub[j].zName) == nSub &&
	        Th8_Memcmp(interp, aSub[j].zName, zSub, nSub) == 0) {
		return &aSub[j];
	    }
	}
	return NULL;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetResolveMember --
 *
 *	Validate one curated subset member against the authoritative tables,
 *	WITHOUT mutating the interpreter (the up-front resolve pass).
 *
 * Why / How:
 *	So an unknown member (a manifest that drifted from the command surface)
 *	makes Th8_RegisterSubsets register nothing and report the bad member,
 *	instead of a half-applied allowlist.
 *
 * Results:
 *	TH8_OK if the member resolves; TH8_ERROR (with a message) otherwise.
 *
 * Side effects:
 *	On error, sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8SubsetResolveMember(Th8_Interp *interp, const Th8_SubsetMemberDef *pM)
{
    switch (pM->eType) {
    case TH8_SUBSET_COMMAND: {
	Th8_CommandProc xProc = 0;
	int rc = th8SubsetFindCommandProc(interp, pM->zName, &xProc);

	if (rc < 0)
	    return TH8_ERROR; /* OOM (result already "out of memory"?) */
	if (rc == 0) {
	    Th8_ErrorMessage(
	        interp, "subset member: no such command:", pM->zName,
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
	return TH8_OK;
    }
    case TH8_SUBSET_FUNCTION:
#if defined(TH8_ENABLE_EXPRESSIONS)
	if (th8CStrEq(interp, pM->zName, "*") ||
	    th8FindMathFunc(
	        interp, pM->zName, Th8_Strlen(interp, pM->zName))) {
	    return TH8_OK;
	}
#endif
	Th8_ErrorMessage(
	    interp, "subset member: no such function:", pM->zName, TH8_NOLEN);
	return TH8_ERROR;
    case TH8_SUBSET_SUBCOMMAND:
	if (pM->zEnsemble &&
	    th8SubsetFindEnsembleSub(interp, pM->zEnsemble, pM->zName)) {
	    return TH8_OK;
	}
	Th8_ErrorMessage(
	    interp, "subset member: no such subcommand:", pM->zName,
	    TH8_NOLEN);
	return TH8_ERROR;
    case TH8_SUBSET_PLUGIN:
	if (th8SubsetFindPlugin(interp, pM->zName)) return TH8_OK;
	Th8_ErrorMessage(
	    interp, "subset member: no such plugin:", pM->zName, TH8_NOLEN);
	return TH8_ERROR;
    default:
	Th8_SetResultStatic(
	    interp, "subset member: unknown member type", TH8_NOLEN);
	return TH8_ERROR;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetApplyPlugin --
 *
 *	Register a plugin subset: install the plugin (if absent) and populate
 *	its ensembles; the "expressions" plugin also brings the math functions.
 *
 * Why / How:
 *	A plugin subset's membership IS the plugin's command table -- reuse the
 *	tested Th8_RegisterPlugin path, then th8PopulatePluginEnsembles.
 *
 * Results:
 *	TH8_OK, or TH8_ERROR on a registration failure.
 *
 * Side effects:
 *	Registers commands, sub-commands, and (for "expressions") math functions.
 *
 *----------------------------------------------------------------------
 */

static int
th8SubsetApplyPlugin(Th8_Interp *interp, const th8StaticPluginRec *pP)
{
    size_t nName = Th8_Strlen(interp, pP->zName);

    if (!th8PluginRegistered(interp, pP->zName, nName)) {
	if (Th8_RegisterPlugin(interp, pP->zName, pP->xGetCommands) !=
	    TH8_OK) {
	    return TH8_ERROR;
	}
    }
    if (th8PopulatePluginEnsembles(interp, pP->zName) != TH8_OK) {
	return TH8_ERROR;
    }
#if defined(TH8_ENABLE_EXPRESSIONS)
    if (th8CStrEq(interp, pP->zName, "expressions")) {
	if (th8RegisterMathFuncs(interp) != TH8_OK) return TH8_ERROR;
    }
#endif
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetApplyMember --
 *
 *	Register one curated subset member.  Two-phase: phase 1 installs
 *	structure (PLUGIN / COMMAND / FUNCTION), phase 2 installs SUBCOMMAND
 *	members (whose parent ensemble command must already exist).
 *
 * Why / How:
 *	Splitting the phases lets a subset name `file` (COMMAND, phase 1) and
 *	`file exists` (SUBCOMMAND, phase 2) in any order, and lets a SUBCOMMAND
 *	member land on an ensemble a DIFFERENT selected subset provided.  A
 *	command a prior subset already installed is skipped (th8CommandExists),
 *	so a second registration cannot replace and wipe an ensemble.
 *
 * Results:
 *	TH8_OK, or TH8_ERROR on a registration failure.
 *
 * Side effects:
 *	Registers a command / function / sub-command / plugin.
 *
 *----------------------------------------------------------------------
 */

static int
th8SubsetApplyMember(
    Th8_Interp *interp,
    const Th8_SubsetMemberDef *pM,
    int phase)
{
    switch (pM->eType) {
    case TH8_SUBSET_PLUGIN:
	if (phase != 1) return TH8_OK;
	{
	    const th8StaticPluginRec
	        *pP = th8SubsetFindPlugin(interp, pM->zName);

	    return pP ? th8SubsetApplyPlugin(interp, pP) : TH8_OK;
	}
    case TH8_SUBSET_COMMAND:
	if (phase != 1) return TH8_OK;
	if (th8CommandExists(interp, pM->zName, TH8_NOLEN)) return TH8_OK;
	{
	    Th8_CommandProc xProc = 0;
	    int rc = th8SubsetFindCommandProc(interp, pM->zName, &xProc);

	    if (rc <= 0) return (rc < 0) ? TH8_ERROR : TH8_OK;
	    return Th8_CreateCommand(interp, pM->zName, xProc, 0, 0, 0);
	}
    case TH8_SUBSET_FUNCTION:
	if (phase != 1) return TH8_OK;
#if defined(TH8_ENABLE_EXPRESSIONS)
	if (th8CStrEq(interp, pM->zName, "*")) {
	    return th8RegisterMathFuncs(interp);
	}
	return th8RegisterOneMathFunc(
	    interp, pM->zName, Th8_Strlen(interp, pM->zName));
#else
	return TH8_OK;
#endif
    case TH8_SUBSET_SUBCOMMAND:
	if (phase != 2) return TH8_OK;
	{
	    const Th8_SubCommand *pSub =
	        th8SubsetFindEnsembleSub(interp, pM->zEnsemble, pM->zName);

	    if (!pSub) return TH8_OK; /* resolved earlier; defensive */
	    return Th8_CreateSubCommand(
	        interp, pM->zEnsemble, pM->zName, pSub->xProc, 0, 0, 0);
	}
    default:
	return TH8_OK;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_RegisterSubsets --
 *
 *	Register the named subsets (an allowlist) into interp.  See th8.h.
 *
 * Why / How:
 *	Resolve every subset name (and every curated member) up front so an
 *	unknown name registers nothing (transactional).  Then register the
 *	essential syntax, plugin subsets, and curated members -- curated members
 *	in two phases so sub-commands land after their ensemble commands exist.
 *	Union/dedup is free (idempotent plugin/command registration).  A later
 *	OOM leaves a partial language; the caller must discard the interpreter.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR (with a message) on an unknown name/member
 *	or a registration failure.
 *
 * Side effects:
 *	Registers language surface into the interpreter.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RegisterSubsets(
    Th8_Interp *interp,
    const char *const *azNames,
    int nNames)
{
    int i;

    if (!interp) return TH8_ERROR;
    TH8_ASSERT_OWNER(interp);
    if (nNames < 0 || (nNames > 0 && !azNames)) {
	Th8_SetResultStatic(
	    interp, "Th8_RegisterSubsets: invalid arguments", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Resolve pass: every name must be a plugin or curated subset, and every
     * curated member must resolve.  Register nothing on any failure. */
    for (i = 0; i < nNames; i++) {
	const Th8_SubsetDef *pCur;

	if (!azNames[i]) {
	    Th8_SetResultStatic(
	        interp, "Th8_RegisterSubsets: null subset name", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (th8SubsetFindPlugin(interp, azNames[i])) continue;
	pCur = th8SubsetFindCurated(interp, azNames[i]);
	if (!pCur) {
	    Th8_ErrorMessage(
	        interp, "no such subset:", azNames[i], TH8_NOLEN);
	    return TH8_ERROR;
	}
	{
	    int m;

	    for (m = 0; m < pCur->nMember; m++) {
		if (th8SubsetResolveMember(interp, &pCur->aMember[m]) !=
		    TH8_OK) {
		    return TH8_ERROR;
		}
	    }
	}
    }

    /* Essential syntax: the {*} expansion operator (as Th8_RegisterLanguage). */
    if (Th8_RegisterExpansion(interp, "*", 1, 0, 0) != TH8_OK) {
	return TH8_ERROR;
    }

    /* Apply pass 1: plugin subsets + curated structure members. */
    for (i = 0; i < nNames; i++) {
	const th8StaticPluginRec
	    *pP = th8SubsetFindPlugin(interp, azNames[i]);

	if (pP) {
	    if (th8SubsetApplyPlugin(interp, pP) != TH8_OK) return TH8_ERROR;
	} else {
	    const Th8_SubsetDef
	        *pCur = th8SubsetFindCurated(interp, azNames[i]);
	    int m;

	    for (m = 0; m < pCur->nMember; m++) {
		if (th8SubsetApplyMember(interp, &pCur->aMember[m], 1) !=
		    TH8_OK) {
		    return TH8_ERROR;
		}
	    }
	}
    }

    /* Apply pass 2: curated SUBCOMMAND members (ensembles now exist). */
    for (i = 0; i < nNames; i++) {
	const Th8_SubsetDef *pCur = th8SubsetFindCurated(interp, azNames[i]);
	int m;

	if (!pCur) continue;
	for (m = 0; m < pCur->nMember; m++) {
	    if (th8SubsetApplyMember(interp, &pCur->aMember[m], 2) !=
	        TH8_OK) {
		return TH8_ERROR;
	    }
	}
    }

    Th8_SetResult(interp, 0, 0);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_ListSubsets --
 *
 *	Set the interpreter result to the list of valid subset names (every
 *	built-in plugin plus every curated subset).  See th8.h.
 *
 * Why / How:
 *	Builds a well-formed list with Th8_ListAppend.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListSubsets(Th8_Interp *interp)
{
    char *zList = 0;
    size_t nList = 0;
    int i;

    if (!interp) return TH8_ERROR;
    for (i = 0; i < TH8_NUM_STATIC_PLUGINS; i++) {
	if (Th8_ListAppend(
	        interp, &zList, &nList, th8StaticPlugins[i].zName,
	        TH8_NOLEN) != TH8_OK) {
	    Th8_Free(interp, zList);
	    return TH8_ERROR;
	}
    }
    for (i = 0; i < TH8_NUM_CURATED; i++) {
	if (Th8_ListAppend(
	        interp, &zList, &nList, th8CuratedSubsets[i].zName,
	        TH8_NOLEN) != TH8_OK) {
	    Th8_Free(interp, zList);
	    return TH8_ERROR;
	}
    }
    Th8_SetResult(interp, zList ? zList : "", zList ? nList : 0);
    Th8_Free(interp, zList);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SubsetAppendMember --
 *
 *	Append one member's audit form ("<type> <name>", or
 *	"subcommand <ensemble> <name>") to a list.
 *
 * Why / How:
 *	Shared by Th8_GetSubsetMembers for both member kinds.
 *
 * Results:
 *	TH8_OK, or TH8_ERROR on an append failure.
 *
 * Side effects:
 *	Grows the caller's list.
 *
 *----------------------------------------------------------------------
 */

static int
th8SubsetAppendMember(
    Th8_Interp *interp,
    char **pzList,
    size_t *pnList,
    const char *zType,
    const char *zEnsemble,
    const char *zName)
{
    char *zElem = 0;
    size_t nElem = 0;
    int rc = TH8_OK;

    if (Th8_StringAppend(interp, &zElem, &nElem, zType, TH8_NOLEN) !=
            TH8_OK ||
        Th8_StringAppend(interp, &zElem, &nElem, " ", 1) != TH8_OK) {
	Th8_Free(interp, zElem);
	return TH8_ERROR;
    }
    if (zEnsemble) {
	if (Th8_StringAppend(interp, &zElem, &nElem, zEnsemble, TH8_NOLEN) !=
	        TH8_OK ||
	    Th8_StringAppend(interp, &zElem, &nElem, " ", 1) != TH8_OK) {
	    Th8_Free(interp, zElem);
	    return TH8_ERROR;
	}
    }
    if (Th8_StringAppend(interp, &zElem, &nElem, zName, TH8_NOLEN) !=
        TH8_OK) {
	Th8_Free(interp, zElem);
	return TH8_ERROR;
    }
    rc = Th8_ListAppend(interp, pzList, pnList, zElem, nElem);
    Th8_Free(interp, zElem);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_GetSubsetMembers --
 *
 *	Set the interpreter result to the resolved members of one subset.  See
 *	th8.h.
 *
 * Why / How:
 *	For a plugin subset, enumerate its command table and its ensembles'
 *	sub-commands (and, for "expressions", the math functions) -- the same
 *	membership registration installs.  For a curated subset, list each
 *	member, validating it resolves so a drifted manifest fails here.
 *
 * Results:
 *	TH8_OK; TH8_ERROR (with a message) for an unknown subset name.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetSubsetMembers(Th8_Interp *interp, const char *zName)
{
    const th8StaticPluginRec *pP;
    const Th8_SubsetDef *pCur;
    char *zList = 0;
    size_t nList = 0;

    if (!interp || !zName) return TH8_ERROR;

    pP = th8SubsetFindPlugin(interp, zName);
    if (pP) {
	int nCmd = 0;
	Th8_CommandEntry *aEntry;
	int ci, ei;

	if (pP->xGetCommands(NULL, &nCmd) == TH8_OK && nCmd > 0) {
	    aEntry = (Th8_CommandEntry *)
	        TH8_ALLOC_MUL(interp, (size_t)nCmd, sizeof(Th8_CommandEntry));
	    if (!aEntry) return TH8_ERROR;
	    if (pP->xGetCommands(aEntry, &nCmd) != TH8_OK) {
		Th8_Free(interp, aEntry);
		return TH8_ERROR;
	    }
	    for (ci = 0; ci < nCmd; ci++) {
		if (th8SubsetAppendMember(
		        interp, &zList, &nList, "command", 0,
		        aEntry[ci].zName) != TH8_OK) {
		    Th8_Free(interp, aEntry);
		    Th8_Free(interp, zList);
		    return TH8_ERROR;
		}
	    }
	    Th8_Free(interp, aEntry);
	}
	/* Ensemble sub-commands owned by this plugin. */
	for (ei = 0; ei < TH8_NUM_ENSEMBLES; ei++) {
	    const Th8_SubCommand *aSub;
	    int j;

	    if (!th8CStrEq(interp, pP->zName, th8Ensembles[ei].zPlugin)) {
		continue;
	    }
	    aSub = *th8Ensembles[ei].ppaSub;
	    for (j = 0; aSub && aSub[j].zName; j++) {
		if (th8SubsetAppendMember(
		        interp, &zList, &nList, "subcommand",
		        th8Ensembles[ei].zEnsemble,
		        aSub[j].zName) != TH8_OK) {
		    Th8_Free(interp, zList);
		    return TH8_ERROR;
		}
	    }
	}
#if defined(TH8_ENABLE_EXPRESSIONS)
	if (th8CStrEq(interp, pP->zName, "expressions")) {
	    if (th8SubsetAppendMember(
	            interp, &zList, &nList, "function", 0, "*") != TH8_OK) {
		Th8_Free(interp, zList);
		return TH8_ERROR;
	    }
	}
#endif
	Th8_SetResult(interp, zList ? zList : "", zList ? nList : 0);
	Th8_Free(interp, zList);
	return TH8_OK;
    }

    pCur = th8SubsetFindCurated(interp, zName);
    if (pCur) {
	int m;

	for (m = 0; m < pCur->nMember; m++) {
	    const Th8_SubsetMemberDef *pM = &pCur->aMember[m];
	    const char *zType = (pM->eType == TH8_SUBSET_COMMAND) ? "command"
	                      : (pM->eType == TH8_SUBSET_FUNCTION)
	                          ? "function"
	                      : (pM->eType == TH8_SUBSET_SUBCOMMAND)
	                          ? "subcommand"
	                          : "plugin";

	    if (th8SubsetResolveMember(interp, pM) != TH8_OK) {
		Th8_Free(interp, zList);
		return TH8_ERROR;
	    }
	    if (th8SubsetAppendMember(
	            interp, &zList, &nList, zType, pM->zEnsemble,
	            pM->zName) != TH8_OK) {
		Th8_Free(interp, zList);
		return TH8_ERROR;
	    }
	}
	Th8_SetResult(interp, zList ? zList : "", zList ? nList : 0);
	Th8_Free(interp, zList);
	return TH8_OK;
    }

    Th8_ErrorMessage(interp, "no such subset:", zName, TH8_NOLEN);
    return TH8_ERROR;
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
 *	th8RegisterStaticPlugins, which also initializes the ensemble
 *	sub-command table pointers directly (each plugin's *GetCommands
 *	assigns them).  This function additionally registers the {*}
 *	expansion operator, the built-in math functions, the built-in
 *	Tcl/TH8 packages, and the ::th8_security system variable array.
 *
 * Results:
 *	TH8_OK if the full built-in language registered successfully.
 *	TH8_ERROR if any required step failed (e.g. OOM registering a
 *	plugin, math function, expansion operator, built-in package, or
 *	the security system variable) -- the previous version always
 *	returned TH8_OK, hiding a partial/inconsistent language
 *	(TH8K-006).  On TH8_ERROR the interpreter's language is partially
 *	registered; the caller SHOULD discard the interpreter rather than
 *	use it.
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
     * The ensemble subcommand-table pointers (th8_info_aSub,
     * th8_file_aSub, th8_string_aSub, th8_namespace_aSub,
     * th8_package_aSub, th8_array_aSub) are initialized DIRECTLY by each
     * plugin's *GetCommands function during th8RegisterStaticPlugins
     * below -- there is deliberately no eval-based "force-init" here
     * (TH8K-006).  The former loop called each ensemble with no args to
     * trigger the assignment, but it ran BEFORE th8RegisterStaticPlugins
     * (so the commands did not yet exist -- every eval failed "no such
     * command") AND discarded every result (so it could not distinguish
     * the intended usage error from a real OOM).  It was both broken and
     * redundant; removing it also removes a swallowed-error path.
     */

    /*
     * Register the built-in {*} expansion operator.
     * This enables Tcl 8.5 argument expansion syntax.
     * The host can unregister it for strict 8.4 compatibility.
     */

    if (Th8_RegisterExpansion(interp, "*", 1, 0, 0) != TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * Register built-in math functions (abs, int, double, sin,
     * cos, etc.) from the static table in th8_math.c.
     */

#if defined(TH8_ENABLE_EXPRESSIONS)
    if (th8RegisterMathFuncs(interp) != TH8_OK) {
	return TH8_ERROR;
    }
#endif

    /*
     * Register built-in plugins.  This must happen before any
     * Th8_Eval calls that use plugin-provided commands (package,
     * file, lappend, etc.).  A real registration failure (OOM) is
     * propagated so callers are not told the language is complete when
     * it is not (TH8K-006).
     */

    if (th8RegisterStaticPlugins(interp) != TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * Populate the ensemble commands' per-interpreter sub-command hashes from
     * their static catalogues (TH8K-025), iterating the shared th8Ensembles
     * table (the same source the plugin-subset path uses).  Done AFTER the
     * plugins register the top-level ensemble command and BEFORE any eval that
     * could use it.  An unbuilt plugin leaves its catalogue pointer NULL
     * (skipped).
     */

    {
	int e;

	for (e = 0; e < TH8_NUM_ENSEMBLES; e++) {
	    if (*th8Ensembles[e].ppaSub &&
	        th8PopulateEnsemble(
	            interp, th8Ensembles[e].zEnsemble,
	            *th8Ensembles[e].ppaSub) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
    }

    /*
     * Add the directory containing the process executable to the
     * auto-path because it may contain (test?) packages, etc.
     */

    if (Th8_Eval(
            interp, 0,
            "lappend ::auto_path [file dirname [info nameofexecutable]]",
            TH8_NOLEN, NULL, 0) != TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * Provide the built-in static packages that are already to
     * be present in the interpreter.
     */

    if (Th8_Eval(
            interp, 0, "package provide Tcl 8.6.21", TH8_NOLEN, NULL, 0) !=
        TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_Eval(interp, 0, "package provide TH8 1.0", TH8_NOLEN, NULL, 0) !=
        TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * System variables: read-only from scripts.
     *
     * th8_security: array with security configuration.
     *   algorithmName  -- signing algorithm (e.g. "none" or "RSA-4096")
     *   policy         -- "none", "warn", or "enforce"
     *   publicKeyToken -- hex token of the trusted key (or "")
     */

#if defined(TH8_ENABLE_VARIABLES)
    /*
     * Use the CHECKED reset: a transient OOM while setting any of the
     * seven ::th8_security elements must fail registration rather than
     * leave a partially-populated security array behind a TH8_OK return
     * (TH8K-006).
     */
    if (th8ResetSecurityArray(interp) != TH8_OK) {
	return TH8_ERROR;
    }

    if (Th8_DeclareSystemVar(interp, "::th8_security", TH8_NOLEN) != TH8_OK) {
	return TH8_ERROR;
    }
#endif

    return TH8_OK;
}
