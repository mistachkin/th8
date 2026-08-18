/*
 * th8_filesystems.c -- File systems plugin for TH8.
 *
 * Implements the file system commands: cd, file, pwd, source.
 *
 * This file is part of the plugin architecture.  The commands are
 * registered via Th8_RegisterPlugin using the static command table
 * returned by th8FilesystemsGetCommands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8.h"
#include "th8_int.h"
#include "th8_util.h"
#include "th8_plugin.h"

#if defined(TH8_PLUGIN_FILE_SYSTEMS)

/*
 * th8_file_aSub: non-static so th8_lang.c can access it
 * for info subcommands.
 */

const Th8_SubCommand *th8_file_aSub;

/*
 *----------------------------------------------------------------------
 *
 * th8IsPathSep --
 *
 *	Test whether a character is a path separator.
 *
 * Why / How:
 *	Accepts both '/' and '\\' as separators so that path
 *	manipulation works correctly on both POSIX and Win32
 *	regardless of which separator the input uses.
 *
 * Results:
 *	Non-zero if c is '/' or '\\', zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8IsPathSep(char c)   /* Character to test. */
{
    return (c == '/' || c == '\\');
}


/*
 *----------------------------------------------------------------------
 *
 * file_dirname_command --
 *
 *	Implements the [file dirname] sub-command.  Returns the
 *	directory portion of a path.
 *
 *	file dirname NAME
 *
 *	Pure string manipulation:
 *	  - "a/b/c"   -> "a/b"
 *	  - "/a/b"    -> "/a"
 *	  - "/"       -> "/"
 *	  - "a"       -> "."
 *	  - ""        -> "."
 *
 * Why / How:
 *	Strips trailing separators, finds the last separator, then
 *	returns everything before it.  No separator -> ".".  Separator
 *	only at position 0 -> "/".  Strips trailing separators from
 *	the dirname result as well.
 *
 * Results:
 *	TH8_OK.  Result is the directory portion.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_dirname_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *z;
    size_t n;
    size_t i;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file dirname name");
    }

    z = argv[2];
    n = argl[2];

    /*
     * Strip trailing separators (but not if the entire string
     * is separators, i.e. root).
     */

    while (n > 1 && th8IsPathSep(z[n - 1])) {
	if ((n & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	n--;
    }

    /*
     * Find the last separator.
     */

    i = n;
    while (i > 0 && !th8IsPathSep(z[i - 1])) {
	if ((i & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	i--;
    }

    if (i == 0) {
	/*
	 * No separator found -> "."
	 */

	Th8_SetResultStatic(interp, ".", 1);
    } else if (i == 1) {
	/*
	 * Separator at position 0 -> root "/"
	 */

	Th8_SetResult(interp, z, 1);
    } else {
	/*
	 * Return everything before the last separator,
	 * stripping trailing separators from the dirname.
	 */

	while (i > 1 && th8IsPathSep(z[i - 1])) {
	    if ((i & 0xFFF) == 0) {
		if (Th8_Ready(interp) != TH8_OK) {
		    return TH8_ERROR;
		}
	    }
	    i--;
	}
	Th8_SetResult(interp, z, i);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_join_command --
 *
 *	Join path components with the platform-neutral separator "/".
 *
 *	file join NAME ?NAME ...?
 *
 *	Rules (matching Tcl):
 *	  - An absolute component resets the result.
 *	  - Empty components are skipped.
 *	  - A single "/" is inserted between non-empty components.
 *	  - Trailing separators on intermediate components are trimmed.
 *
 * Why / How:
 *	Iterates over components, resetting the result on absolute
 *	components, trimming trailing separators from intermediates,
 *	and inserting "/" between non-empty components.
 *
 * Results:
 *	TH8_OK.  Result is the joined path.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_join_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zResult = 0;
    size_t nResult = 0;
    int i;
    int rc = TH8_OK;

    if (argc < 3) {
	return Th8_WrongNumArgs(interp, "file join name ?name ...?");
    }

    for (i = 2; i < argc; i++) {
	const char *z = argv[i];
	size_t n = argl[i];

	if (n == 0) continue;

	/*
	 * Absolute component resets the result.
	 */

	if (th8IsPathSep(z[0])) {
	    Th8_Free(interp, zResult);
	    zResult = 0;
	    nResult = 0;
	}

	/*
	 * Strip trailing separators from this component
	 * (unless it is the last one and entirely separators).
	 */

	if (i < argc - 1) {
	    while (n > 1 && th8IsPathSep(z[n - 1])) {
		if ((n & 0xFFF) == 0) {
		    if (Th8_Ready(interp) != TH8_OK) {
			Th8_Free(interp, zResult);
			return TH8_ERROR;
		    }
		}
		n--;
	    }
	}

	/*
	 * Insert separator between components.
	 */

	if (nResult > 0 && !th8IsPathSep(zResult[nResult - 1])) {
	    TH8_STR_APPEND(interp, &zResult, &nResult, "/", 1);
	}

	TH8_STR_APPEND(interp, &zResult, &nResult, z, n);
    }

    if (zResult) {
	Th8_SetResult(interp, zResult, nResult);
    } else {
	Th8_ClearResult(interp);
    }

done:
    Th8_Free(interp, zResult);
    return rc;

oom:
    /* TH8_STR_APPEND growth failed; "out of memory" already set. */
    rc = TH8_ERROR;
    goto done;
}


/*
 *----------------------------------------------------------------------
 *
 * file_split_command --
 *
 *	Split a path into its components as a Tcl list.
 *
 *	file split NAME
 *
 *	Rules (matching Tcl):
 *	  - Leading "/" becomes the first element "/".
 *	  - Consecutive separators are collapsed.
 *	  - Trailing separators are ignored.
 *
 * Why / How:
 *	Handles leading separators as a "/" list element, then walks
 *	the rest of the path splitting on separator boundaries and
 *	collapsing consecutive separators.
 *
 * Results:
 *	TH8_OK.  Result is a Tcl list of path components.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_split_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zResult = 0;
    size_t nResult = 0;
    const char *z;
    size_t n;
    size_t i;
    int rc = TH8_OK;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file split name");
    }

    z = argv[2];
    n = argl[2];
    i = 0;

    /*
     * Handle leading separator (absolute path -> "/" element).
     */

    if (n > 0 && th8IsPathSep(z[0])) {
	rc = Th8_ListAppend(interp, &zResult, &nResult, "/", 1);
	if (rc != TH8_OK) goto done;
	while (i < n && th8IsPathSep(z[i])) {
	    if ((i & 0xFFF) == 0) {
		if (Th8_Ready(interp) != TH8_OK) {
		    rc = TH8_ERROR;
		    goto done;
		}
	    }
	    i++;
	}
    }

    /*
     * Walk the rest of the path, splitting on separators.
     */

    while (i < n) {
	size_t start;

	if (Th8_Ready(interp) != TH8_OK) {
	    rc = TH8_ERROR;
	    goto done;
	}
	start = i;

	while (i < n && !th8IsPathSep(z[i])) {
	    if ((i & 0xFFF) == 0) {
		if (Th8_Ready(interp) != TH8_OK) {
		    rc = TH8_ERROR;
		    goto done;
		}
	    }
	    i++;
	}

	if (i > start) {
	    rc = Th8_ListAppend(
	        interp, &zResult, &nResult, &z[start], i - start);
	    if (rc != TH8_OK) goto done;
	}

	while (i < n && th8IsPathSep(z[i])) {
	    if ((i & 0xFFF) == 0) {
		if (Th8_Ready(interp) != TH8_OK) {
		    rc = TH8_ERROR;
		    goto done;
		}
	    }
	    i++;
	}
    }

    if (zResult) {
	Th8_SetResult(interp, zResult, nResult);
    } else {
	Th8_ClearResult(interp);
    }

done:
    Th8_Free(interp, zResult);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * file_exists_command --
 *
 *	Test whether named data exists via the platform's
 *	xDataExists callback.
 *
 *	file exists NAME
 *
 *	Returns 1 if the named data exists, 0 otherwise.
 *	This command is "safe" because the platform controls
 *	what "exists" means -- it may check a file system, a
 *	database, a virtual file system, or any other data store.
 *
 * Why / How:
 *	Delegates to Th8_DataExists which invokes the platform's
 *	xDataExists callback.  The platform defines what "named data"
 *	means, keeping this command portable across all embedding
 *	environments.
 *
 * Results:
 *	TH8_OK.  Result is 1 if the named data exists, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_exists_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file exists name");
    }
    return Th8_SetResultInt(
        interp, Th8_DataExists(interp, argv[2], argl[2], NULL));
}


/*
 *----------------------------------------------------------------------
 *
 * file_tail_command --
 *
 *	Implements the [file tail] sub-command.  Returns the last
 *	component of a path.
 *
 *	file tail NAME
 *
 *	Pure string manipulation:
 *	  - "a/b/c"   -> "c"
 *	  - "/a/b"    -> "b"
 *	  - "/"       -> ""
 *	  - "a"       -> "a"
 *	  - ""        -> ""
 *
 * Why / How:
 *	Strips trailing separators, finds the last separator, and
 *	returns everything after it.  No separator -> whole string
 *	is the tail.  All separators -> empty tail.
 *
 * Results:
 *	TH8_OK.  Result is the last path component.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_tail_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *z;
    size_t n;
    size_t i;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file tail name");
    }

    z = argv[2];
    n = argl[2];

    /*
     * Strip trailing separators (but not if the entire string
     * is separators, i.e. root).
     */

    while (n > 1 && th8IsPathSep(z[n - 1])) {
	if ((n & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	n--;
    }

    /*
     * Find the last separator.
     */

    i = n;
    while (i > 0 && !th8IsPathSep(z[i - 1])) {
	if ((i & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	i--;
    }

    if (i == 0) {
	/*
	 * No separator found -> the whole name is the tail.
	 */

	Th8_SetResult(interp, z, n);
    } else if (i == n) {
	/*
	 * All separators (e.g. "/") -> empty tail.
	 */

	Th8_ClearResult(interp);
    } else {
	/*
	 * Return everything after the last separator.
	 */

	Th8_SetResult(interp, &z[i], n - i);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_normalize_command --
 *
 *	Return the canonical absolute path for a file name.
 *
 *	file normalize NAME
 *
 *	Routes through the platform's xNormalizePath callback.
 *	If the callback is NULL, the path is returned unchanged.
 *
 * Why / How:
 *	Delegates to Th8_NormalizePath which invokes the platform's
 *	xNormalizePath callback.  Falls back to returning the path
 *	unchanged if no callback is registered.
 *
 * Results:
 *	TH8_OK.  Result is the normalized path.
 *
 * Side effects:
 *	Allocates and frees a temporary string for the normalized path.
 *
 *----------------------------------------------------------------------
 */

static int
file_normalize_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zNorm;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file normalize name");
    }

    zNorm = Th8_NormalizePath(interp, argv[2], argl[2]);
    if (zNorm) {
	Th8_SetResult(interp, zNorm, TH8_NOLEN);
	Th8_Free(interp, zNorm);
    } else {
	/*
	 * No platform callback: return the path unchanged.
	 */

	Th8_SetResult(interp, argv[2], argl[2]);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_tempname_command --
 *
 *	Implements the [file tempname] sub-command.  Creates a
 *	pre-allocated temporary file of SIZE bytes and returns its
 *	abstract channel name.
 *
 *	file tempname SIZE
 *
 * Why / How:
 *	Parses SIZE as a positive wide integer, then delegates to
 *	th8ChannelCreate which allocates an in-memory channel buffer
 *	and registers it in the channel table.
 *
 * Results:
 *	TH8_OK with the channel name as result; TH8_ERROR if SIZE
 *	is not a positive integer.
 *
 * Side effects:
 *	Allocates an in-memory channel.
 *
 *----------------------------------------------------------------------
 */

static int
file_tempname_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    th8_int64_t nSize;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file tempname size");
    }
    if (Th8_ToWideInt(interp, argv[2], argl[2], &nSize) != TH8_OK ||
        nSize <= 0) {
	Th8_SetResultStatic(
	    interp, "file tempname: size must be a positive integer",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    return th8ChannelCreate(interp, (size_t)nSize);
}


/*
 *----------------------------------------------------------------------
 *
 * file_channels_command --
 *
 *	Implements the [file channels] sub-command.  Lists all open
 *	channels, optionally filtered by glob pattern.
 *
 *	file channels ?PATTERN?
 *
 * Why / How:
 *	Builds a list starting with standard channels (stdin/stdout)
 *	if the platform provides them, adds temporary file channels
 *	via th8ChannelList, then optionally filters by glob pattern.
 *
 * Results:
 *	TH8_OK.  Result is a list of channel names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_channels_command(
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
	return Th8_WrongNumArgs(interp, "file channels ?pattern?");
    }

    /* Include standard channels if the platform provides them. */
    {
	const Th8_Platform *pPlat = Th8_GetPlatform(interp);

	if (ALWAYS(pPlat) && pPlat->xInput) {
	    Th8_ListAppend(interp, &zList, &nList, "stdin", 5);
	}
	if (ALWAYS(pPlat) && pPlat->xOutput) {
	    Th8_ListAppend(interp, &zList, &nList, "stdout", 6);
	}
    }

    /* Add temporary file channels. */
    th8ChannelList(interp, &zList, &nList);

    /* Filter by glob pattern if given. */
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
	            interp, argv[2], argl[2], azElem[i],
	            TH8_LEN(anElem[i]))) {
		Th8_ListAppend(
		    interp, &zFiltered, &nFiltered, azElem[i], anElem[i]);
	    }
	}
	Th8_Free(interp, azElem);
	/* anElem is interior to the azElem block. */
	Th8_Free(interp, zList);
	zList = zFiltered;
	nList = nFiltered;
    }

    Th8_SetResult(interp, zList, nList);
    Th8_Free(interp, zList);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_type_command --
 *
 *	Return the type of a named data item.
 *
 *	file type NAME
 *
 *	Returns: "file", "directory", "file symbolicLink",
 *	"directory symbolicLink", "unsupported", or "unknown".
 *
 *	"unknown" is returned when the native API fails (including
 *	file not found).  "unsupported" is returned for anything
 *	that is not a regular file, directory, or symbolic link.
 *
 * Why / How:
 *	Calls Th8_DataExists with an attrs output parameter.  Maps
 *	the attribute flags (TH8_FILE_ATTR_FILE, TH8_FILE_ATTR_DIRECTORY,
 *	TH8_FILE_ATTR_SYMLINK) to the appropriate result string.
 *
 * Results:
 *	TH8_OK.  Result is the type string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_type_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    int attrs = 0;
    int baseType;
    int isSymlink;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file type name");
    }

    if (!Th8_DataExists(interp, argv[2], argl[2], &attrs)) {
	Th8_SetResultStatic(interp, "unknown", 7);
	return TH8_OK;
    }

    baseType = attrs & ~TH8_FILE_ATTR_SYMLINK;
    isSymlink = (attrs & TH8_FILE_ATTR_SYMLINK) != 0;

    if (baseType == TH8_FILE_ATTR_FILE) {
	if (isSymlink) {
	    Th8_SetResultStatic(interp, "file symbolicLink", 17);
	} else {
	    Th8_SetResultStatic(interp, "file", 4);
	}
    } else if (baseType == TH8_FILE_ATTR_DIRECTORY) {
	if (isSymlink) {
	    Th8_SetResultStatic(interp, "directory symbolicLink", 22);
	} else {
	    Th8_SetResultStatic(interp, "directory", 9);
	}
    } else {
	Th8_SetResultStatic(interp, "unsupported", 11);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_under_command --
 *
 *	Test whether one path is under (a descendant of) another.
 *
 *	file under NAME1 NAME2
 *
 *	Both paths are normalized.  Returns "1" if NAME1 starts
 *	with NAME2 followed by a path separator, or is exactly
 *	NAME2.  Returns "0" otherwise.
 *
 *	Uses TH8_PATH_CMP for platform-appropriate comparison:
 *	case-sensitive on POSIX, case-insensitive on Win32.
 *
 * Why / How:
 *	Normalizes both paths, then checks whether NAME1 starts with
 *	NAME2 followed by a separator (or is exactly NAME2).  Uses
 *	TH8_PATH_CMP so the comparison is case-appropriate for the
 *	platform.
 *
 * Results:
 *	TH8_OK.  Result is "1" if under, "0" otherwise.
 *
 * Side effects:
 *	Allocates and frees normalized path strings.
 *
 *----------------------------------------------------------------------
 */

static int
file_under_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *z1;
    char *z2;
    size_t n1;
    size_t n2;
    int result = 0;

    (void)ctx;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "file under name1 name2");
    }

    z1 = Th8_NormalizePath(interp, argv[2], argl[2]);
    if (!z1) {
	Th8_SetResultStatic(
	    interp, "file under: cannot normalize first path", TH8_NOLEN);
	return TH8_ERROR;
    }

    z2 = Th8_NormalizePath(interp, argv[3], argl[3]);
    if (!z2) {
	Th8_Free(interp, z1);
	Th8_SetResultStatic(
	    interp, "file under: cannot normalize second path", TH8_NOLEN);
	return TH8_ERROR;
    }

    n1 = Th8_Strlen(interp, z1);
    n2 = Th8_Strlen(interp, z2);

    if (n1 == n2 && TH8_PATH_CMP(z1, z2, n1) == 0) {
	/*
	 * Exact match: name1 IS name2.
	 */

	result = 1;
    } else if (
        n1 > n2 && TH8_PATH_CMP(z1, z2, n2) == 0 && TH8_IS_SEP(z1[n2])) {
	/*
	 * name1 starts with name2 + separator.
	 */

	result = 1;
    }

    Th8_Free(interp, z1);
    Th8_Free(interp, z2);
    Th8_SetResult(interp, result ? "1" : "0", 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_validname_command --
 *
 *	Validate whether a path string is syntactically valid.
 *	This command does NOT access the filesystem; it validates
 *	the path syntax only.
 *
 *	file validname PATH ?PATHTYPE?
 *
 *	If PATHTYPE is given, it must be "None" (case-insensitive);
 *	any other value returns an error.
 *
 *	Returns "1" if the path is valid, "0" otherwise.
 *
 *	Validation rules:
 *	  - Must not be empty.
 *	  - Must not contain NUL bytes.
 *	  - On Win32: reject < > : " | ? * characters.
 *	  - On Win32: reject reserved device names (CON, PRN,
 *	    AUX, NUL, COM1-9, LPT1-9) as path components.
 *	  - Must be <= 4096 bytes.
 *
 * Why / How:
 *	Applies a series of validation checks: empty, NUL bytes,
 *	length limit, and platform-specific rules (Win32 invalid
 *	characters and reserved device names).  Uses TH8_IS_SEP('\\')
 *	as a compile-time proxy for Win32.
 *
 * Results:
 *	TH8_OK.  Result is "1" if valid, "0" otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8IsDeviceName --
 *
 *	Check if a path component is a reserved Win32 device name.
 *
 * Why / How:
 *	Strips trailing dots and spaces (Windows ignores these for
 *	device name matching), then checks for CON, PRN, AUX, NUL,
 *	COM1-9, and LPT1-9 using case-insensitive character
 *	comparison.  Called by file_validname_command during the
 *	Win32 validation pass.
 *
 * Results:
 *	1 if the component is a reserved device name, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TH8_INTERNAL int
th8IsDeviceName(
    const char *z,  /* Start of component. */
    size_t n)   /* Length of component. */
{
    /*
     * Strip trailing dots and spaces (Windows ignores them
     * for device name matching).
     */

    while (n > 0 && (z[n - 1] == '.' || z[n - 1] == ' ')) {
	n--;
    }

    /* Per-name nesting per Finding 005 sec. 5b: the original
     * combined compound was 24 conditions (n==3 case) and 14
     * conditions (n==4 case), each well past the size where
     * clang's MC/DC representation can attribute most C-pairs.
     * Each device name now gets its own small (6-cond) decision
     * that the testlib exercises individually. */
    if (n == 3) {
	/* CON, PRN, AUX, NUL */
	if (z[0] == 'C' || z[0] == 'c') {
	    if (z[1] == 'O' || z[1] == 'o') {
		if (z[2] == 'N' || z[2] == 'n') return 1;
	    }
	}
	if (z[0] == 'P' || z[0] == 'p') {
	    if (z[1] == 'R' || z[1] == 'r') {
		if (z[2] == 'N' || z[2] == 'n') return 1;
	    }
	}
	if (z[0] == 'A' || z[0] == 'a') {
	    if (z[1] == 'U' || z[1] == 'u') {
		if (z[2] == 'X' || z[2] == 'x') return 1;
	    }
	}
	if (z[0] == 'N' || z[0] == 'n') {
	    if (z[1] == 'U' || z[1] == 'u') {
		if (z[2] == 'L' || z[2] == 'l') return 1;
	    }
	}
    } else if (n == 4) {
	/* COM1-9, LPT1-9 */
	if (z[0] == 'C' || z[0] == 'c') {
	    if (z[1] == 'O' || z[1] == 'o') {
		if (z[2] == 'M' || z[2] == 'm') {
		    if (z[3] >= '1')
			if (z[3] <= '9') return 1;
		}
	    }
	}
	if (z[0] == 'L' || z[0] == 'l') {
	    if (z[1] == 'P' || z[1] == 'p') {
		if (z[2] == 'T' || z[2] == 't') {
		    if (z[3] >= '1')
			if (z[3] <= '9') return 1;
		}
	    }
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * file_validname_command --
 *
 *	Implements the [file validname] sub-command.  Validates whether
 *	a path string is syntactically valid without accessing the
 *	filesystem.
 *
 *	file validname PATH ?PATHTYPE?
 *
 * Why / How:
 *	Applies a series of platform-neutral checks (empty, NUL bytes,
 *	length) and platform-specific checks (Win32 invalid characters
 *	and reserved device names via th8IsDeviceName).  See the
 *	overview comment block above for the full validation rules.
 *
 * Results:
 *	TH8_OK.  Result is "1" if valid, "0" otherwise.
 *	TH8_ERROR if PATHTYPE is given but is not "None".
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_validname_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *z;
    size_t n;
    size_t i;

    (void)ctx;

    if (argc != 3 && argc != 4) {
	return Th8_WrongNumArgs(interp, "file validname path ?pathType?");
    }

    /*
     * If pathType is given, it must be "None" (case-insensitive).
     */

    if (argc == 4) {
	const char *zType = argv[3];
	size_t nType = argl[3];

	/* Case-insensitive "None" check; nested per-char so the
	 * decision stays below the clang MC/DC truth-table cap.
	 * See FINDINGS.md Finding 005. */
	{
	    int isNone = 0;

	    if (nType == 4) {
		if (zType[0] == 'N' || zType[0] == 'n') {
		    if (zType[1] == 'O' || zType[1] == 'o') {
			if (zType[2] == 'N' || zType[2] == 'n') {
			    if (zType[3] == 'E' || zType[3] == 'e') {
				isNone = 1;
			    }
			}
		    }
		}
	    }
	    if (!isNone) {
		Th8_SetResultStatic(
		    interp, "file validname: pathType must be None",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }
	}
    }

    z = argv[2];
    n = argl[2];

    /*
     * Must not be empty.
     */

    if (n == 0) {
	Th8_SetResultStatic(interp, "0", 1);
	return TH8_OK;
    }

    /*
     * Must be <= 4096 bytes.
     */

    if (n > 4096) {
	Th8_SetResultStatic(interp, "0", 1);
	return TH8_OK;
    }

    /*
     * Must not contain NUL bytes.
     */

    if (Th8_Strlen(interp, z) != n) {
	Th8_SetResultStatic(interp, "0", 1);
	return TH8_OK;
    }

    /*
     * Platform-specific checks (Win32).
     *
     * Use TH8_IS_SEP('\\') as a proxy for Win32:
     * on POSIX, backslash is not a separator and this
     * evaluates to false.
     */

    if (TH8_IS_SEP('\\')) {
	/*
	 * Reject invalid Win32 characters.
	 */

	for (i = 0; i < n; i++) {
	    char c = z[i];

	    if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' ||
	        c == '?' || c == '*') {
		Th8_SetResultStatic(interp, "0", 1);
		return TH8_OK;
	    }
	}

	/*
	 * Reject reserved device names as path components.
	 * Walk the path component by component.
	 */

	i = 0;
	while (i < n) {
	    size_t start = i;

	    while (i < n && !th8IsPathSep(z[i])) {
		i++;
	    }
	    if (i > start) {
		if (th8IsDeviceName(&z[start], i - start)) {
		    Th8_SetResultStatic(interp, "0", 1);
		    return TH8_OK;
		}
	    }
	    while (i < n && th8IsPathSep(z[i])) {
		i++;
	    }
	}
    }

    Th8_SetResultStatic(interp, "1", 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_extension_command --
 *
 *	Implements the [file extension] sub-command.  Returns the
 *	file extension (including the dot).
 *
 *	file extension NAME
 *
 *	Pure string manipulation:
 *	  - "foo.tcl"     -> ".tcl"
 *	  - "foo"         -> ""
 *	  - "foo.bar.baz" -> ".baz"
 *	  - ".hidden"     -> ".hidden"
 *	  - "a/b.c"       -> ".c"
 *	  - "a.b/c"       -> ""
 *
 * Why / How:
 *	Finds the last path separator, then finds the last '.' after
 *	it.  Returns everything from the last dot to the end, or
 *	empty string if no dot is found after the last separator.
 *
 * Results:
 *	TH8_OK.  Result is the extension string (including dot) or
 *	empty string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_extension_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *z;
    size_t n;
    size_t lastSep;
    size_t lastDot;
    size_t i;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file extension name");
    }

    z = argv[2];
    n = argl[2];

    /*
     * Find the last path separator.
     */

    lastSep = 0;
    for (i = 0; i < n; i++) {
	if ((i & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	if (th8IsPathSep(z[i])) {
	    lastSep = i + 1;
	}
    }

    /*
     * Find the last '.' after the last separator.
     */

    lastDot = n;  /* sentinel: no dot found */
    for (i = lastSep; i < n; i++) {
	if ((i & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	if (z[i] == '.') {
	    lastDot = i;
	}
    }

    if (lastDot < n) {
	Th8_SetResult(interp, &z[lastDot], n - lastDot);
    } else {
	Th8_ClearResult(interp);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_nativename_command --
 *
 *	Implements the [file nativename] sub-command.  Converts
 *	directory separators to the native format.
 *
 *	file nativename NAME
 *
 *	On Win32: convert '/' to '\\'.
 *	On POSIX: convert '\\' to '/'.
 *
 * Why / How:
 *	Copies the path string and converts separators in-place.
 *	Uses TH8_IS_SEP('\\') as a compile-time platform selector
 *	to determine which direction to convert.
 *
 * Results:
 *	TH8_OK.  Result is the path with native separators.
 *
 * Side effects:
 *	Allocates and frees a temporary copy of the path.
 *
 *----------------------------------------------------------------------
 */

static int
file_nativename_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char *zCopy;
    size_t n;
    size_t i;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file nativename name");
    }

    n = argl[2];
    zCopy = (char *)TH8_ALLOC_STR(interp, n);
    if (!zCopy) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_Memcpy(interp, zCopy, argv[2], n);
    zCopy[n] = '\0';

    if (TH8_IS_SEP('\\')) {
	/*
	 * Win32: convert '/' to '\\'.
	 */

	for (i = 0; i < n; i++) {
	    if ((i & 0xFFF) == 0) {
		if (Th8_Ready(interp) != TH8_OK) {
		    Th8_Free(interp, zCopy);
		    return TH8_ERROR;
		}
	    }
	    if (zCopy[i] == '/') {
		zCopy[i] = '\\';
	    }
	}
    } else {
	/*
	 * POSIX: convert '\\' to '/'.
	 */

	for (i = 0; i < n; i++) {
	    if ((i & 0xFFF) == 0) {
		if (Th8_Ready(interp) != TH8_OK) {
		    Th8_Free(interp, zCopy);
		    return TH8_ERROR;
		}
	    }
	    if (zCopy[i] == '\\') {
		zCopy[i] = '/';
	    }
	}
    }

    Th8_SetResult(interp, zCopy, n);
    Th8_Free(interp, zCopy);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_pathtype_command --
 *
 *	Return the path type: "absolute", "relative", or
 *	"volumerelative".
 *
 *	file pathtype NAME
 *
 *	Rules:
 *	  - Starts with "/" or "\\" -> absolute
 *	  - Starts with "//" or "\\\\" (UNC) -> absolute
 *	  - "X:/" or "X:\\" (drive + sep) -> absolute
 *	  - "X:" without separator -> volumerelative (Win32)
 *	  - Everything else -> relative (including empty)
 *
 * Why / How:
 *	Checks the first characters of the path: leading separator
 *	means absolute, "X:" plus separator means absolute, "X:"
 *	alone means volumerelative, everything else is relative.
 *
 * Results:
 *	TH8_OK.  Result is "absolute", "relative", or
 *	"volumerelative".
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_pathtype_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *z;
    size_t n;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file pathtype name");
    }

    z = argv[2];
    n = argl[2];

    if (n == 0) {
	Th8_SetResultStatic(interp, "relative", 8);
	return TH8_OK;
    }

    /*
     * Check for leading separator (absolute or UNC).
     */

    if (th8IsPathSep(z[0])) {
	Th8_SetResultStatic(interp, "absolute", 8);
	return TH8_OK;
    }

    /*
     * Check for drive letter: "X:" optionally followed by separator.
     */

    if (n >= 2 &&
        ((z[0] >= 'A' && z[0] <= 'Z') || (z[0] >= 'a' && z[0] <= 'z')) &&
        z[1] == ':') {
	if (n >= 3 && th8IsPathSep(z[2])) {
	    Th8_SetResultStatic(interp, "absolute", 8);
	} else {
	    Th8_SetResultStatic(interp, "volumerelative", 14);
	}
	return TH8_OK;
    }

    Th8_SetResultStatic(interp, "relative", 8);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_rootname_command --
 *
 *	Return the path without the file extension.
 *
 *	file rootname NAME
 *
 *	Find the last '.' after the last path separator and return
 *	everything before it.  If no extension, return the whole path.
 *
 *	Pure string manipulation:
 *	  - "foo.tcl"  -> "foo"
 *	  - "foo"      -> "foo"
 *	  - "a/b.c"    -> "a/b"
 *	  - "a.b/c"    -> "a.b/c"
 *
 * Why / How:
 *	Mirrors file_extension_command but returns everything before
 *	the last dot (after the last separator) instead of after it.
 *	If no extension is found, returns the whole path.
 *
 * Results:
 *	TH8_OK.  Result is the path without its extension.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_rootname_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *z;
    size_t n;
    size_t lastSep;
    size_t lastDot;
    size_t i;

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file rootname name");
    }

    z = argv[2];
    n = argl[2];

    /*
     * Find the last path separator.
     */

    lastSep = 0;
    for (i = 0; i < n; i++) {
	if ((i & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	if (th8IsPathSep(z[i])) {
	    lastSep = i + 1;
	}
    }

    /*
     * Find the last '.' after the last separator.
     */

    lastDot = n;  /* sentinel: no dot found */
    for (i = lastSep; i < n; i++) {
	if ((i & 0xFFF) == 0) {
	    if (Th8_Ready(interp) != TH8_OK) {
		return TH8_ERROR;
	    }
	}
	if (z[i] == '.') {
	    lastDot = i;
	}
    }

    if (lastDot < n) {
	Th8_SetResult(interp, z, lastDot);
    } else {
	Th8_SetResult(interp, z, n);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_rootpath_command --
 *
 *	Return the filesystem root (mount point) for a path.
 *
 *	file rootpath NAME
 *
 *	Routes through the platform's xGetRootPath callback.
 *
 * Why / How:
 *	Delegates to Th8_GetRootPath which invokes the platform's
 *	xGetRootPath callback.  The result is written into a
 *	stack-allocated buffer.
 *
 * Results:
 *	TH8_OK with the root path as result; TH8_ERROR if the
 *	platform cannot determine the root path.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_rootpath_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    char zBuf[4096];

    (void)ctx;

    if (argc != 3) {
	return Th8_WrongNumArgs(interp, "file rootpath name");
    }

    if (Th8_GetRootPath(interp, argv[2], argl[2], zBuf, sizeof(zBuf)) ==
        TH8_OK) {
	Th8_SetResult(interp, zBuf, TH8_NOLEN);
    } else {
	Th8_SetResultStatic(
	    interp, "file rootpath: cannot determine root path", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * file_same_command --
 *
 *	Test whether two paths refer to the same physical file.
 *
 *	file same NAME1 NAME2
 *
 *	Routes through the platform's xSameFile callback.
 *	Returns "1" if the same, "0" otherwise.
 *
 * Why / How:
 *	Checks for the xSameFile platform callback, then delegates to
 *	Th8_SameFile which compares the underlying file identities
 *	(e.g. inode/device on POSIX, file index on Win32).
 *
 * Results:
 *	TH8_OK with "1" or "0" as result; TH8_ERROR if the platform
 *	does not support same-file detection.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_same_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const Th8_Platform *pPlatform;

    (void)ctx;

    if (argc != 4) {
	return Th8_WrongNumArgs(interp, "file same name1 name2");
    }

    pPlatform = Th8_GetPlatform(interp);
    /* Bug 26: Th8_GetPlatform can return NULL during teardown.
     * Split per Finding 005 sec. 5b: the !pPlatform arm is
     * intrinsic-dead in the test corpus but kept defensive.
     * MUST be separate ifs (not nested) -- inside the second
     * if, dereferencing pPlatform->xSameFile would NULL-deref
     * if we'd nested under !pPlatform. */
    if (!pPlatform) {
	Th8_SetResultStatic(
	    interp,
	    "file same: platform does not support same-file detection",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!pPlatform->xSameFile) {
	Th8_SetResultStatic(
	    interp,
	    "file same: platform does not support same-file detection",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    return Th8_SetResultInt(
        interp, Th8_SameFile(interp, argv[2], argl[2], argv[3], argl[3]));
}


/*
 *----------------------------------------------------------------------
 *
 * file_separator_command --
 *
 *	Return the path separator.
 *
 *	file separator ?NAME?
 *
 *	Without name: return the native separator ("/" on POSIX,
 *	"\\" on Win32).
 *	With name: return the first separator found in the string,
 *	or the native separator if none.
 *
 * Why / How:
 *	Determines the native separator via TH8_IS_SEP('\\').  With
 *	a name argument, scans for the first '/' or '\\' in the
 *	string.  Without a name, returns the native separator directly.
 *
 * Results:
 *	TH8_OK.  Result is the separator character as a single-char
 *	string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
file_separator_command(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx,   /* Not used. */
    int argc,   /* Number of arguments. */
    const char **argv,  /* Argument values. */
    size_t *argl)  /* Argument lengths. */
{
    const char *zNative;

    (void)ctx;

    if (argc != 2 && argc != 3) {
	return Th8_WrongNumArgs(interp, "file separator ?name?");
    }

    zNative = TH8_IS_SEP('\\') ? "\\" : "/";

    if (argc == 3) {
	/*
	 * Scan the string for the first separator.
	 */

	const char *z = argv[2];
	size_t n = argl[2];
	size_t i;

	for (i = 0; i < n; i++) {
	    if ((i & 0xFFF) == 0) {
		if (Th8_Ready(interp) != TH8_OK) {
		    return TH8_ERROR;
		}
	    }
	    if (z[i] == '/') {
		Th8_SetResultStatic(interp, "/", 1);
		return TH8_OK;
	    }
	    if (z[i] == '\\') {
		Th8_SetResultStatic(interp, "\\", 1);
		return TH8_OK;
	    }
	}
    }

    Th8_SetResult(interp, zNative, 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FileSub --
 *
 *	Catalogue of `file` sub-commands, installed into the `file` ensemble
 *	command's per-interpreter sub-command hash at registration (TH8K-025).
 *
 * Why / How:
 *	Covers path manipulation (dirname, join, split, tail,
 *	extension, rootname, normalize, nativename, pathtype),
 *	existence checks (exists, type), identity (same, under),
 *	validation (validname), channels (channels, tempname),
 *	and filesystem info (rootpath, separator).  Exports
 *	th8_file_aSub for [info commands] enumeration.
 *
 * Results:
 *	Return code from the sub-command.
 *
 * Side effects:
 *	Determined by the sub-command.
 *
 *----------------------------------------------------------------------
 */

static const Th8_SubCommand th8FileSub[] =
    {{0, "channels", file_channels_command},
     {0, "dirname", file_dirname_command},
     {0, "exists", file_exists_command},
     {0, "extension", file_extension_command},
     {0, "join", file_join_command},
     {0, "nativename", file_nativename_command},
     {0, "normalize", file_normalize_command},
     {0, "pathtype", file_pathtype_command},
     {0, "rootname", file_rootname_command},
     {0, "rootpath", file_rootpath_command},
     {0, "same", file_same_command},
     {0, "separator", file_separator_command},
     {0, "split", file_split_command},
     {0, "tail", file_tail_command},
     {0, "tempname", file_tempname_command},
     {0, "type", file_type_command},
     {0, "under", file_under_command},
     {0, "validname", file_validname_command},
     {0, 0, 0}};


/*
 *----------------------------------------------------------------------
 *
 * pwd_command --
 *
 *	Return the current working directory via the platform's
 *	xGetCwd callback.
 *
 *	pwd
 *
 *	The security model restricts xGetCwd to return "." (the
 *	base directory) or an error.  If the platform callback is
 *	NULL, returns a script error.
 *
 * Why / How:
 *	Checks for the xGetCwd platform callback, then delegates to
 *	Th8_GetCwd.  The platform controls what directory information
 *	is exposed, enforcing the security model.
 *
 * Results:
 *	TH8_OK with the current directory as result; TH8_ERROR if the
 *	platform callback is missing or access is denied.
 *
 * Side effects:
 *	Allocates and frees a temporary string for the directory path.
 *
 *----------------------------------------------------------------------
 */

static int
pwd_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pPlatform;
    char *zCwd;

    (void)ctx;

    if (argc != 1) {
	return Th8_WrongNumArgs(interp, "pwd");
    }

    pPlatform = Th8_GetPlatform(interp);
    /* Bug 26: Th8_GetPlatform can return NULL during teardown.
     * Split per Finding 005 sec. 5b. */
    if (!pPlatform) {
	Th8_SetResultStatic(
	    interp, "permission denied: access to file system unavailable",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!pPlatform->xGetCwd) {
	Th8_SetResultStatic(
	    interp, "permission denied: access to file system unavailable",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    zCwd = Th8_GetCwd(interp);
    if (zCwd) {
	Th8_SetResult(interp, zCwd, TH8_NOLEN);
	Th8_Free(interp, zCwd);
	return TH8_OK;
    }

    Th8_SetResultStatic(
        interp, "permission denied: cannot access foreign directory",
        TH8_NOLEN);
    return TH8_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * cd_command --
 *
 *	Change the current working directory via the platform's
 *	xSetCwd callback.
 *
 *	cd ?dirName?
 *
 *	If dirName is omitted, it defaults to ".".  The security
 *	model restricts xSetCwd to accept only "." (the base
 *	directory).  If the platform callback is NULL, returns a
 *	script error.
 *
 * Why / How:
 *	Checks for the xSetCwd platform callback, defaults the path
 *	to "." if omitted, then delegates to Th8_SetCwd.  The platform
 *	controls which directories are accessible.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the platform callback is
 *	missing or access is denied.
 *
 * Side effects:
 *	May change the interpreter's current working directory.
 *
 *----------------------------------------------------------------------
 */

static int
cd_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    const Th8_Platform *pPlatform;
    const char *zPath;
    size_t nPath;

    (void)ctx;

    if (argc != 1 && argc != 2) {
	return Th8_WrongNumArgs(interp, "cd ?dirName?");
    }

    pPlatform = Th8_GetPlatform(interp);
    /* Bug 26: Th8_GetPlatform can return NULL during teardown.
     * Split per Finding 005 sec. 5b. */
    if (!pPlatform) {
	Th8_SetResultStatic(
	    interp, "permission denied: access to file system unavailable",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!pPlatform->xSetCwd) {
	Th8_SetResultStatic(
	    interp, "permission denied: access to file system unavailable",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    if (argc == 2) {
	zPath = argv[1];
	nPath = argl[1];
    } else {
	zPath = ".";
	nPath = 1;
    }

    if (Th8_SetCwd(interp, zPath, nPath) == TH8_OK) {
	Th8_ClearResult(interp);
	return TH8_OK;
    }

    Th8_SetResultStatic(
        interp, "permission denied: cannot access foreign directory",
        TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * source_command --
 *
 *	Retrieve and evaluate a script by opaque name.
 *
 *	source NAME
 *
 *	Delegates to the public Th8_EvalFile API, which handles
 *	data retrieval, [info script] push/pop, evaluation, and
 *	cleanup.
 *
 * Why / How:
 *	Thin wrapper around Th8_EvalFile which does all the real work:
 *	platform data retrieval, script name tracking for [info script],
 *	evaluation, and memory cleanup.
 *
 * Results:
 *	Return code from the script evaluation.
 *
 * Side effects:
 *	Retrieves and evaluates external script data.  Temporarily
 *	updates the [info script] name.
 *
 *----------------------------------------------------------------------
 */

static int
source_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    (void)ctx;

    if (argc != 2) {
	return Th8_WrongNumArgs(interp, "source name");
    }

    return Th8_EvalFile(interp, argv[1], TH8_LEN(argl[1]));
}


/*
 *----------------------------------------------------------------------
 *
 * Command table and plugin registration.
 *
 *----------------------------------------------------------------------
 */

static Th8_CommandEntry th8FilesystemsCommands[] = {
    {1, 0, "cd", cd_command},
    {1, 0, "file", 0}, /* pure ensemble (TH8K-025) */
    {1, 0, "pwd", pwd_command},
    {1, 0, "source", source_command},
};

/*
 *----------------------------------------------------------------------
 *
 * th8FilesystemsGetCommands --
 *
 *	Return the command table for the file systems plugin.
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
th8FilesystemsGetCommands(Th8_CommandEntry *pCommand, int *pnCommand)
{
    int n = (int)(sizeof(th8FilesystemsCommands) /
                  sizeof(th8FilesystemsCommands[0]));

    th8_file_aSub = th8FileSub;

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
	    pCommand[i] = th8FilesystemsCommands[i];
	}
    }
    return TH8_OK;
}
#endif /* TH8_PLUGIN_FILE_SYSTEMS */
