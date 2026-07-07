/*
 * th8_extensibility.c -- Extensibility plugin for TH8.
 *
 * Implements the extensibility commands: load, package, unload.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8ExtensibilityGetCommands.
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

#if defined(TH8_PLUGIN_EXTENSIBILITY)

/*
 *======================================================================
 *
 * File-local type definitions
 *
 *======================================================================
 */

/*
 * Th8_PkgInfo is now defined in th8_int.h so testlib can
 * synthesize partial-state records for MC/DC coverage of
 * `pPkg && pPkg->zVersion` and `pPkg && pPkg->paIfNeeded`
 * compounds.  See the typedef there for field documentation.
 *
 * paIfNeeded entries' pData is a Th8_Strdup'd script.
 */


/*
 * Exported for info subcommands (declared extern in th8_int.h).
 */

const Th8_SubCommand *th8_package_aSub;

/*
 *----------------------------------------------------------------------
 *
 * load_command --
 *
 *	Implements the Tcl [load] command.  Loads a binary extension
 *	into the interpreter.
 *
 *	load NAME ?INITPROC?
 *
 *	NAME identifies the binary to load (platform-specific).
 *	INITPROC, if given, names the initialization entry point;
 *	otherwise the entry point is derived from NAME.
 *
 * Why / How:
 *	Delegates to Th8_Load which handles the platform callback
 *	(xLoad), init-proc lookup, and tracking the loaded library
 *	for later [unload].
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if loading is not enabled,
 *	the platform callback is missing, or the load fails.
 *
 * Side effects:
 *	Calls xLoad; records the library for later [unload].
 *
 *----------------------------------------------------------------------
 */

static int
load_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *zProc = 0;
    size_t nProc = 0;

    if (argc < 2 || argc > 3) {
	return Th8_WrongNumArgs(interp, "load name ?initProc?");
    }
    if (argc == 3) {
	zProc = argv[2];
	nProc = argl[2];
    }
    return Th8_Load(interp, argv[1], argl[1], zProc, nProc);
}


/*
 *----------------------------------------------------------------------
 *
 * unload_command --
 *
 *	Implements the Tcl [unload] command.  Unloads a previously
 *	loaded binary extension.
 *
 *	unload ?-nocomplain? ?-keeplibrary? ?-nokeeplibrary? ?--? NAME
 *
 *	Options:
 *	    -nocomplain    Suppress all errors silently.
 *	    -keeplibrary   (Default) Call _Unload entry point but
 *	                   do not close the shared library.
 *	    -nokeeplibrary Call _Unload AND close the library.
 *	                   Requires TH8_UNLOAD_DANGEROUS.
 *	    --             End of options.
 *
 * Why / How:
 *	Parses options, checks whether unloading is enabled, then
 *	delegates to Th8_Unload which handles the _Unload entry point
 *	call, the dangerous-close gate, and tracking list removal.
 *	With -nocomplain, all errors are silently converted to TH8_OK.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if unloading is not enabled,
 *	the library is not loaded, or -nokeeplibrary is used without
 *	TH8_UNLOAD_DANGEROUS.  With -nocomplain, always TH8_OK.
 *
 * Side effects:
 *	Calls xUnload; removes the library from tracking.
 *
 *----------------------------------------------------------------------
 */

static int
unload_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    int i;
    int bNoComplain = 0;
    int bClose = 0;
    const char *zName;
    size_t nName;

    if (argc < 2) {
	return Th8_WrongNumArgs(
	    interp, "unload ?-nocomplain? ?-keeplibrary?"
	            " ?-nokeeplibrary? ?--? name");
    }

    /*
     * Parse options.
     */

    for (i = 1; i < argc; i++) {
	if (th8StrEq(interp, argv[i], argl[i], "-nocomplain")) {
	    bNoComplain = 1;
	} else if (th8StrEq(interp, argv[i], argl[i], "-keeplibrary")) {
	    bClose = 0;
	} else if (th8StrEq(interp, argv[i], argl[i], "-nokeeplibrary")) {
	    bClose = 1;
	} else if (th8StrEq(interp, argv[i], argl[i], "--")) {
	    i++;
	    break;
	} else {
	    break;
	}
    }
    if (i >= argc) {
	return Th8_WrongNumArgs(interp, "unload ?options? name");
    }
    zName = argv[i];
    nName = argl[i];

    /*
     * Check gate.
     */

    if (!th8IsUnloadEnabled(interp)) {
	if (bNoComplain) {
	    Th8_SetResult(interp, 0, 0);
	    return TH8_OK;
	}
	Th8_SetResultStatic(interp, "unloading is not enabled", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Attempt unload.  Th8_Unload handles the dangerous
     * check and the tracking list.
     */

    {
	int rc = Th8_Unload(interp, zName, nName, 0, 0, bClose);

	if (rc != TH8_OK && bNoComplain) {
	    Th8_SetResult(interp, 0, 0);
	    return TH8_OK;
	}
	return rc;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8LangFirstEntry --
 *
 *	Hash iteration callback that returns the first entry found.
 *	Used to pick an arbitrary version from the ifneeded sub-hash
 *	when no specific version is requested.
 *
 * Results:
 *	TH8_BREAK (stop iteration after the first entry).
 *
 * Side effects:
 *	Stores the entry pointer in the context.
 *
 *----------------------------------------------------------------------
 */

static int
th8LangFirstEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_HashEntry **ppResult = (Th8_HashEntry **)pCtx;

    *ppResult = pEntry;
    return TH8_BREAK;  /* stop iteration after first */
}


/*
 *----------------------------------------------------------------------
 *
 * th8LangAppendHashKeys --
 *
 *	Hash iteration callback that appends each entry's key to a
 *	list.  Used by [package names] and [package versions].
 *
 * Results:
 *	TH8_OK (continue iteration).
 *
 * Side effects:
 *	Appends the key string to the list in the context.
 *
 *----------------------------------------------------------------------
 */

static int
th8LangAppendHashKeys(Th8_HashEntry *pEntry, void *pCtx)
{
    void **ap = (void **)pCtx;
    Th8_Interp *interp = (Th8_Interp *)ap[0];
    char **pzList = (char **)ap[1];
    size_t *pnList = (size_t *)ap[2];

    Th8_ListAppend(interp, pzList, pnList, pEntry->zKey, pEntry->nKey);
    return TH8_OK;
}


/* Th8_PkgInfo -- declared at top of file. */

/*
 *----------------------------------------------------------------------
 *
 * th8FreeIfneededEntry --
 *
 *	Hash iteration callback: free an ifneeded script string
 *	stored as pData in a paIfNeeded sub-hash entry.
 *
 * Why / How:
 *	Each ifneeded entry's pData is a Th8_Strdup'd script string
 *	that must be freed when the entry is removed.  Used during
 *	package forget and interpreter cleanup.
 *
 * Results:
 *	TH8_OK (continue iteration).
 *
 * Side effects:
 *	Frees the script string.
 *
 *----------------------------------------------------------------------
 */

static int
th8FreeIfneededEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;

    Th8_Free(interp, pEntry->pData);
    pEntry->pData = 0;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8FreePkgEntry --
 *
 *	Hash iteration callback: free a Th8_PkgInfo struct stored
 *	as pData in the package registry hash.  Frees the version
 *	string and the ifneeded sub-hash (including its scripts).
 *
 * Why / How:
 *	Each package entry owns a Th8_PkgInfo with a version string
 *	and an ifneeded sub-hash.  Cleanup must free the sub-hash
 *	entries (via th8FreeIfneededEntry), destroy the sub-hash, free
 *	the version string, and free the PkgInfo itself.
 *
 * Results:
 *	TH8_OK (continue iteration).
 *
 * Side effects:
 *	Frees the Th8_PkgInfo and all associated data.
 *
 *----------------------------------------------------------------------
 */

static int
th8FreePkgEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;
    Th8_PkgInfo *pPkg = (Th8_PkgInfo *)pEntry->pData;

    if (pPkg) {
	Th8_Free(interp, pPkg->zVersion);
	if (pPkg->paIfNeeded) {
	    Th8_HashIterate(
	        interp, pPkg->paIfNeeded, th8FreeIfneededEntry,
	        (void *)interp);
	    Th8_HashDelete(interp, pPkg->paIfNeeded);
	}
	Th8_Free(interp, pPkg);
	pEntry->pData = 0;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8CleanupPackages --
 *
 *	Free all package registry data.  Called during interpreter
 *	deletion.
 *
 * Why / How:
 *	Iterates over the package hash using th8FreePkgEntry to free
 *	each package's PkgInfo, version string, and ifneeded sub-hash.
 *	The hash itself is freed by the caller (interpreter deletion).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all package registry data.
 *
 *----------------------------------------------------------------------
 */

void
th8CleanupPackages(Th8_Interp *interp)
{
    Th8_Hash *paPackage = Th8_GetPackageHash(interp);

    if (paPackage) {
	Th8_HashIterate(interp, paPackage, th8FreePkgEntry, (void *)interp);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * package_provide_command --
 *
 *	Implements the [package provide] sub-command.  Registers or
 *	queries a package's provided version.
 *
 *	package provide NAME ?VERSION?
 *
 * Why / How:
 *	Looks up (or creates) the package entry in the package hash.
 *	With VERSION, updates zVersion.  Without VERSION, returns the
 *	current version (or empty string if not yet provided).
 *
 * Results:
 *	TH8_OK.  Result is the package version.
 *
 * Side effects:
 *	May create a package entry or update its version.
 *
 *----------------------------------------------------------------------
 */

static int
package_provide_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_HashEntry *pEntry;
    Th8_PkgInfo *pPkg;

    if (argc != 3 && argc != 4) {
	return Th8_WrongNumArgs(interp, "package provide name ?version?");
    }
    pEntry = Th8_HashFind(
        interp, Th8_GetPackageHash(interp), argv[2], TH8_LEN(argl[2]), 1);
    pPkg = (Th8_PkgInfo *)pEntry->pData;
    if (!pPkg) {
	pPkg = (Th8_PkgInfo *)TH8_ALLOC(interp, sizeof(Th8_PkgInfo));
	if (!pPkg) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pEntry->pData = (void *)pPkg;
    }
    if (argc == 4) {
	Th8_Free(interp, pPkg->zVersion);
	pPkg->zVersion = Th8_Strdup(interp, argv[3], argl[3]);
	pPkg->nVersion = TH8_LEN(argl[3]);
    }
    if (pPkg->zVersion) {
	Th8_SetResult(interp, pPkg->zVersion, pPkg->nVersion);
    } else {
	Th8_ClearResult(interp);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * package_require_command --
 *
 *	Load a package, using the ifneeded/unknown handler chain.
 *
 *	package require ?-exact? NAME ?VERSION?
 *
 *	Lookup/load algorithm:
 *	  1. Look up NAME in the package hash.  If already provided
 *	     (zVersion is set), return the version immediately.
 *	  2. Search the ifneeded sub-hash for a matching version
 *	     (specific version if requested, or first registered
 *	     version if any version is acceptable).
 *	  3. If an ifneeded script is found, evaluate it via
 *	     Th8_Eval.  After evaluation, re-check the package hash
 *	     to verify that the script called [package provide].
 *	     If the version is now present, return it.  Otherwise
 *	     error: "package provided no version".
 *	  4. If no ifneeded script matched, try the package unknown
 *	     handler (set via [package unknown COMMAND]).  Build a
 *	     command "COMMAND name ?version?" and Th8_Eval it.
 *	  5. After the unknown handler, re-check the package hash.
 *	     If the package is now provided, return the version.
 *	  6. If all attempts fail, error: "can't find package".
 *
 *	NOTE: The -exact flag is parsed and accepted but not yet
 *	enforced for version matching.
 *
 * Why / How:
 *	Implements a three-tier lookup: (1) check if already provided,
 *	(2) search ifneeded scripts, (3) try the unknown handler.
 *	Each tier re-checks the package hash after evaluation because
 *	the script may call [package provide] as a side effect.
 *
 * Results:
 *	TH8_OK with the version as result; TH8_ERROR if the package
 *	cannot be found or loaded.
 *
 * Side effects:
 *	May evaluate ifneeded scripts or the unknown handler.
 *
 *----------------------------------------------------------------------
 */

static int
package_require_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_HashEntry *pEntry;
    Th8_PkgInfo *pPkg;

    if (argc < 3 || argc > 5) {
	return Th8_WrongNumArgs(
	    interp, "package require ?-exact? name ?version?");
    }

    /*
     * Skip -exact flag (accepted but not enforced yet).
     */

    {
	int iName = 2;

	if (th8StrEq(interp, argv[2], argl[2], "-exact")) {
	    iName = 3;
	}
	if (iName >= argc) {
	    return Th8_WrongNumArgs(
	        interp, "package require ?-exact? name ?version?");
	}

	pEntry = Th8_HashFind(
	    interp, Th8_GetPackageHash(interp), argv[iName],
	    TH8_LEN(argl[iName]), 0);
	pPkg = pEntry ? (Th8_PkgInfo *)pEntry->pData : 0;

	if (pPkg && pPkg->zVersion) {
	    Th8_SetResult(interp, pPkg->zVersion, pPkg->nVersion);
	    return TH8_OK;
	}

	/*
	 * Not loaded.  Search ifneeded scripts for a
	 * matching version and evaluate it.
	 */

	if (pPkg && pPkg->paIfNeeded) {
	    Th8_HashEntry *pVer = 0;

	    if (iName + 1 < argc) {
		/*
		 * Specific version requested.
		 */

		pVer = Th8_HashFind(
		    interp, pPkg->paIfNeeded, argv[iName + 1],
		    TH8_LEN(argl[iName + 1]), 0);
	    } else {
		/*
		 * Any version: pick the first registered.
		 */

		Th8_HashIterate(
		    interp, pPkg->paIfNeeded, th8LangFirstEntry,
		    (void *)&pVer);
	    }

	    if (pVer && ALWAYS(pVer->pData)) {
		int rc;
		const char *zScript;

		zScript = (const char *)pVer->pData;
		rc = Th8_Eval(interp, 0, zScript, TH8_NOLEN, NULL, 0);
		if (rc != TH8_OK) return rc;

		/*
		 * Verify the package was provided.
		 */

		pEntry = Th8_HashFind(
		    interp, Th8_GetPackageHash(interp), argv[iName],
		    TH8_LEN(argl[iName]), 0);
		pPkg = pEntry ? (Th8_PkgInfo *)pEntry->pData : 0;
		if (pPkg && pPkg->zVersion) {
		    Th8_SetResult(interp, pPkg->zVersion, pPkg->nVersion);
		    return TH8_OK;
		}
		Th8_SetResultStatic(
		    interp, "package provided no version", TH8_NOLEN);
		return TH8_ERROR;
	    }
	}

	/*
	 * Try the unknown handler.
	 */

	{
	    const char *zUnk;

	    zUnk = Th8_GetPackageUnknown(interp);
	    if (ALWAYS(zUnk) && zUnk[0]) {
		char *zCmd = 0;
		size_t nCmd = 0;
		int rc;

		Th8_StringAppend(interp, &zCmd, &nCmd, zUnk, TH8_NOLEN);
		Th8_StringAppend(interp, &zCmd, &nCmd, " ", 1);
		Th8_ListAppend(
		    interp, &zCmd, &nCmd, argv[iName], argl[iName]);
		if (iName + 1 < argc) {
		    Th8_StringAppend(interp, &zCmd, &nCmd, " ", 1);
		    Th8_ListAppend(
		        interp, &zCmd, &nCmd, argv[iName + 1],
		        argl[iName + 1]);
		}
		rc = Th8_Eval(interp, 0, zCmd, nCmd, NULL, 0);
		Th8_Free(interp, zCmd);
		if (rc != TH8_OK) return rc;

		/*
		 * Re-check after unknown handler.
		 */

		pEntry = Th8_HashFind(
		    interp, Th8_GetPackageHash(interp), argv[iName],
		    TH8_LEN(argl[iName]), 0);
		pPkg = pEntry ? (Th8_PkgInfo *)pEntry->pData : 0;
		if (pPkg && pPkg->zVersion) {
		    Th8_SetResult(interp, pPkg->zVersion, pPkg->nVersion);
		    return TH8_OK;
		}
	    }
	}

	Th8_ErrorMessage(
	    interp, "can't find package", argv[iName], argl[iName]);
	return TH8_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * package_names_command --
 *
 *	Implements the [package names] sub-command.  Returns a list
 *	of all known package names.
 *
 *	package names
 *
 * Why / How:
 *	Iterates over the package hash using th8LangAppendHashKeys
 *	to collect all package names into a list.
 *
 * Results:
 *	TH8_OK.  Result is a list of package names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
package_names_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    char *zList = 0;
    size_t nList = 0;
    void *aCtx[3];

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "package names");
    }
    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)&zList;
    aCtx[2] = (void *)&nList;
    Th8_HashIterate(
        interp, Th8_GetPackageHash(interp), th8LangAppendHashKeys,
        (void *)aCtx);
    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * package_forget_command --
 *
 *	Implements the [package forget] sub-command.  Removes all
 *	knowledge of a package from the interpreter.
 *
 *	package forget NAME
 *
 * Why / How:
 *	Looks up the package in the hash, frees its PkgInfo (version
 *	string + ifneeded sub-hash), then removes the hash entry via
 *	Th8_HashFind with create=-1 (delete mode).
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Removes the package entry, its version, and ifneeded scripts.
 *
 *----------------------------------------------------------------------
 */

static int
package_forget_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_HashEntry *pEntry;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "package forget name");
    }
    pEntry = Th8_HashFind(
        interp, Th8_GetPackageHash(interp), argv[2], TH8_LEN(argl[2]), 0);
    if (pEntry && ALWAYS(pEntry->pData)) {
	Th8_PkgInfo *pPkg = (Th8_PkgInfo *)pEntry->pData;

	Th8_Free(interp, pPkg->zVersion);
	if (pPkg->paIfNeeded) {
	    Th8_HashIterate(
	        interp, pPkg->paIfNeeded, th8FreeIfneededEntry,
	        (void *)interp);
	    Th8_HashDelete(interp, pPkg->paIfNeeded);
	}
	Th8_Free(interp, pPkg);
	Th8_HashFind(
	    interp, Th8_GetPackageHash(interp), argv[2], TH8_LEN(argl[2]),
	    -1);
    }
    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * package_ifneeded_command --
 *
 *	Implements the [package ifneeded] sub-command.  Registers or
 *	queries a package ifneeded script for a version.
 *
 *	package ifneeded NAME VERSION ?SCRIPT?
 *
 * Why / How:
 *	Looks up (or creates) the package entry and its ifneeded
 *	sub-hash.  With SCRIPT, stores the script as a Th8_Strdup'd
 *	string keyed by VERSION.  Without SCRIPT, looks up the
 *	version key and returns the registered script.
 *
 * Results:
 *	TH8_OK.  In query mode, result is the registered script.
 *
 * Side effects:
 *	May create a package entry and register an ifneeded script.
 *
 *----------------------------------------------------------------------
 */

static int
package_ifneeded_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_HashEntry *pEntry;
    Th8_PkgInfo *pPkg;

    if (argc != 4 && argc != 5) {
	return Th8_WrongNumArgs(
	    interp, "package ifneeded name version ?script?");
    }
    pEntry = Th8_HashFind(
        interp, Th8_GetPackageHash(interp), argv[2], TH8_LEN(argl[2]), 1);
    pPkg = (Th8_PkgInfo *)pEntry->pData;
    if (!pPkg) {
	pPkg = (Th8_PkgInfo *)TH8_ALLOC(interp, sizeof(Th8_PkgInfo));
	if (!pPkg) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pEntry->pData = (void *)pPkg;
    }
    if (!pPkg->paIfNeeded) {
	pPkg->paIfNeeded = Th8_HashNew(interp);
	if (!pPkg->paIfNeeded) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }
    if (argc == 5) {
	/*
	 * Register the script for this version.
	 */

	Th8_HashEntry *pVer;

	pVer = Th8_HashFind(
	    interp, pPkg->paIfNeeded, argv[3], TH8_LEN(argl[3]), 1);
	Th8_Free(interp, pVer->pData);
	pVer->pData = (void *)Th8_Strdup(interp, argv[4], argl[4]);
	Th8_ClearResult(interp);
    } else {
	/*
	 * Query: return the script for this version.
	 */

	Th8_HashEntry *pVer;

	pVer = Th8_HashFind(
	    interp, pPkg->paIfNeeded, argv[3], TH8_LEN(argl[3]), 0);
	if (pVer && ALWAYS(pVer->pData)) {
	    Th8_SetResult(interp, (const char *)pVer->pData, TH8_NOLEN);
	} else {
	    Th8_ClearResult(interp);
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * package_present_command --
 *
 *	Implements the [package present] sub-command.  Checks whether
 *	a package is already loaded (provided).
 *
 *	package present ?-exact? NAME ?VERSION?
 *
 * Why / How:
 *	Looks up the package in the hash and checks whether zVersion
 *	is set (indicating [package provide] was called).  Unlike
 *	[package require], does not attempt to load the package.
 *
 * Results:
 *	TH8_OK if present; TH8_ERROR if not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
package_present_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_HashEntry *pEntry;
    Th8_PkgInfo *pPkg;
    int iName = 2;

    if (argc < 3 || argc > 5) {
	return Th8_WrongNumArgs(
	    interp, "package present ?-exact? name ?version?");
    }
    if (th8StrEq(interp, argv[2], argl[2], "-exact")) {
	iName = 3;
    }
    if (iName >= argc) {
	return Th8_WrongNumArgs(
	    interp, "package present ?-exact? name ?version?");
    }
    pEntry = Th8_HashFind(
        interp, Th8_GetPackageHash(interp), argv[iName], TH8_LEN(argl[iName]),
        0);
    pPkg = pEntry ? (Th8_PkgInfo *)pEntry->pData : 0;
    if (pPkg && pPkg->zVersion) {
	Th8_SetResult(interp, pPkg->zVersion, pPkg->nVersion);
	return TH8_OK;
    }
    Th8_ErrorMessage(
        interp, "package not present:", argv[iName], argl[iName]);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * package_unknown_command --
 *
 *	Implements the [package unknown] sub-command.  Sets or queries
 *	the package unknown handler command.
 *
 *	package unknown ?COMMAND?
 *
 * Why / How:
 *	With COMMAND, stores the handler via Th8_SetPackageUnknown.
 *	In both cases, returns the current handler string.  The
 *	unknown handler is invoked by [package require] when no
 *	ifneeded script matches.
 *
 * Results:
 *	TH8_OK.  Result is the current unknown handler.
 *
 * Side effects:
 *	May set the package unknown handler.
 *
 *----------------------------------------------------------------------
 */

static int
package_unknown_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "package unknown ?command?");
    }
    if (argc == 3) {
	Th8_SetPackageUnknown(interp, argv[2], argl[2]);
    }
    Th8_SetResult(interp, Th8_GetPackageUnknown(interp), TH8_NOLEN);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * package_scan_command --
 *
 *	Implements the [package scan] sub-command.  Re-scan ::auto_path
 *	for pkgIndex.th8 files and source any that have not yet been
 *	processed.
 *
 *	package scan
 *
 * Why / How:
 *	Scripts may append new directories to ::auto_path after the
 *	initial Th8_AutoPathSearch that runs at interpreter startup.
 *	This command lets a script (or the [package require] fallback
 *	path) trigger a fresh scan so that newly-added directories are
 *	picked up.  Delegates to Th8_AutoPathSearch(interp, 0, 0).
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on failure.
 *
 * Side effects:
 *	May source pkgIndex.th8 files and register new packages.
 *
 *----------------------------------------------------------------------
 */

static int
package_scan_command(
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
	return Th8_WrongNumArgs(interp, "package scan");
    }
    if (Th8_AutoPathSearch(interp, 0, 0) != TH8_OK) {
	return TH8_ERROR;
    }
    Th8_ClearResult(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8VersionParse --
 *
 *	Parse a dotted version string into an array of integers.
 *	Returns the number of components.
 *
 * Why / How:
 *	Walks the string character by character, accumulating decimal
 *	digits into each component and advancing on '.'.  Stops when
 *	the string is exhausted or nMax components are reached.
 *	Used by package vcompare and vsatisfies.
 *
 * Results:
 *	Number of version components parsed.
 *
 * Side effects:
 *	Fills aVer[0..result-1] with the component values.
 *
 *----------------------------------------------------------------------
 */

static int
th8VersionParse(
    const char *z,
    size_t n,
    int *aVer,   /* OUT: version components. */
    int nMax)   /* Max components to parse. */
{
    int nVer = 0;
    size_t i = 0;

    if (n == TH8_NOLEN) {
	while (z[n])
	    n++;
    }
    n = TH8_LEN(n);

    while (i < n && nVer < nMax) {
	int val = 0;

	while (i < n && z[i] >= '0' && z[i] <= '9') {
	    val = val * 10 + (z[i] - '0');
	    i++;
	}
	aVer[nVer++] = val;
	if (i < n && z[i] == '.') i++;
    }
    return nVer;
}


/*
 *----------------------------------------------------------------------
 *
 * package_vcompare_command --
 *
 *	Implements the [package vcompare] sub-command.  Compares two
 *	dotted version strings.
 *
 *	package vcompare VERSION1 VERSION2
 *
 * Why / How:
 *	Parses both versions via th8VersionParse, then compares
 *	component by component.  Missing trailing components are
 *	treated as zero.
 *
 * Results:
 *	TH8_OK.  Result is -1, 0, or 1.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
package_vcompare_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int aV1[16], aV2[16];
    int n1, n2, i, nMax;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "package vcompare version1 version2");
    }
    n1 = th8VersionParse(argv[2], argl[2], aV1, 16);
    n2 = th8VersionParse(argv[3], argl[3], aV2, 16);
    nMax = n1 > n2 ? n1 : n2;
    for (i = 0; i < nMax; i++) {
	int v1 = (i < n1) ? aV1[i] : 0;
	int v2 = (i < n2) ? aV2[i] : 0;

	if (v1 < v2) {
	    Th8_SetResultInt(interp, -1);
	    return TH8_OK;
	}
	if (v1 > v2) {
	    Th8_SetResultInt(interp, 1);
	    return TH8_OK;
	}
    }
    Th8_SetResultInt(interp, 0);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * package_vsatisfies_command --
 *
 *	Implements the [package vsatisfies] sub-command.  Checks
 *	whether a version satisfies a requirement.
 *
 *	package vsatisfies VERSION REQUIREMENT
 *
 * Why / How:
 *	Parses both version strings, requires exact match on the major
 *	component, and >= on all remaining components.  This matches
 *	the Tcl 8.4 version satisfaction semantics where the major
 *	version is the API compatibility boundary.
 *
 * Results:
 *	TH8_OK.  Result is 1 if satisfied, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
package_vsatisfies_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    int aVer[16], aReq[16];
    int nVer, nReq;

    if (argc != 4) {
	return Th8_WrongNumArgs(
	    interp, "package vsatisfies version requirement");
    }
    nVer = th8VersionParse(argv[2], argl[2], aVer, 16);
    nReq = th8VersionParse(argv[3], argl[3], aReq, 16);

    /*
     * Major version must match exactly.
     */

    if (nVer < 1 || nReq < 1 || aVer[0] != aReq[0]) {
	Th8_SetResultInt(interp, 0);
	return TH8_OK;
    }

    /*
     * Remaining components: version must be >= requirement.
     */

    {
	int i;
	int nMax = nVer > nReq ? nVer : nReq;

	for (i = 1; i < nMax; i++) {
	    int v = (i < nVer) ? aVer[i] : 0;
	    int r = (i < nReq) ? aReq[i] : 0;

	    if (v < r) {
		Th8_SetResultInt(interp, 0);
		return TH8_OK;
	    }
	    if (v > r) {
		break;
	    }
	}
    }
    Th8_SetResultInt(interp, 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * package_versions_command --
 *
 *	Implements the [package versions] sub-command.  Returns a list
 *	of registered ifneeded versions for a package.
 *
 *	package versions NAME
 *
 * Why / How:
 *	Looks up the package, then iterates over its paIfNeeded
 *	sub-hash using th8LangAppendHashKeys to collect all version
 *	keys into a list.
 *
 * Results:
 *	TH8_OK.  Result is a list of version strings.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
package_versions_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_HashEntry *pEntry;
    Th8_PkgInfo *pPkg;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "package versions name");
    }
    pEntry = Th8_HashFind(
        interp, Th8_GetPackageHash(interp), argv[2], TH8_LEN(argl[2]), 0);
    pPkg = pEntry ? (Th8_PkgInfo *)pEntry->pData : 0;
    if (pPkg && pPkg->paIfNeeded) {
	char *zList = 0;
	size_t nList = 0;
	void *aCtx[3];

	aCtx[0] = (void *)interp;
	aCtx[1] = (void *)&zList;
	aCtx[2] = (void *)&nList;
	Th8_HashIterate(
	    interp, pPkg->paIfNeeded, th8LangAppendHashKeys, (void *)aCtx);
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
 * package_command --
 *
 *	Implements the Tcl [package] command.  Dispatcher for
 *	[package] sub-commands.
 *
 * Why / How:
 *	Uses Th8_CallSubCommand with a static sub-command table.
 *	Exports th8_package_aSub so that [info commands] can
 *	enumerate the available package sub-commands.
 *
 * Results:
 *	Return code from the sub-command.
 *
 * Side effects:
 *	Determined by the sub-command.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand th8PackageSub[] =
    {{0, "forget", package_forget_command},
     {0, "ifneeded", package_ifneeded_command},
     {0, "names", package_names_command},
     {0, "present", package_present_command},
     {0, "provide", package_provide_command},
     {0, "require", package_require_command},
     {0, "scan", package_scan_command},
     {0, "unknown", package_unknown_command},
     {0, "vcompare", package_vcompare_command},
     {0, "versions", package_versions_command},
     {0, "vsatisfies", package_vsatisfies_command},
     {0, 0, 0}};

/*
 *----------------------------------------------------------------------
 *
 * package_command --
 *
 *	Implements the script-visible `[package ...]` ensemble
 *	(`forget`, `ifneeded`, `names`, `present`, `provide`,
 *	`require`, `unknown`, `vcompare`, `versions`,
 *	`vsatisfies`).  Thin dispatcher into `th8PackageSub`
 *	via `Th8_CallSubCommand`; unknown / ambiguous
 *	subcommands fall through to its diagnostics.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	ctx    -- command context (forwarded).
 *	argc   -- argument count.
 *	argv   -- argument vector.
 *	argl   -- argument byte-length vector.
 *
 * Returns:
 *	The selected subcommand's return code, or `TH8_ERROR`
 *	with a diagnostic if the subcommand name is unknown.
 *
 * Side effects:
 *	Whatever the dispatched subcommand performs.
 *
 *----------------------------------------------------------------------
 */
static int
package_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    return Th8_CallSubCommand(interp, ctx, argc, argv, argl, th8PackageSub);
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8ExtensibilityCommands[] = {
    {1, 0, "load", load_command},
    {1, 0, "package", package_command},
    {1, 0, "unload", unload_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8ExtensibilityGetCommands --
 *
 *	Return the command table for the extensibility plugin.
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
th8ExtensibilityGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8ExtensibilityCommands) /
                  sizeof(th8ExtensibilityCommands[0]));

    th8_package_aSub = th8PackageSub;

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
	    pCommand[i] = th8ExtensibilityCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_EXTENSIBILITY */
