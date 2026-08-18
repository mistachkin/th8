/*
 * th8_plugin.c -- Plugin system for TH8.
 *
 * Implements plugin registration, unregistration, and the
 * token-based command removal that makes unregistration
 * robust against rename.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_plugin.h"


/*
 *----------------------------------------------------------------------
 *
 * Th8_PluginEntry --
 *
 *	Per-plugin metadata.  Stored in a singly-linked list
 *	anchored at interp->pPlugins (via th8GetPluginList).
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_PluginEntry {
    char *zName;  /* Plugin name (owned, NUL-term). */
    size_t nName;  /* Byte length of zName. */
    Th8_GetCommandsProc xGetCommands;
    th8_uint64_t *aToken; /* Array of tokens for registered cmds. */
    int nToken;   /* Number of entries in aToken. */
    struct Th8_PluginEntry *pNext;
} Th8_PluginEntry;


/*
 *----------------------------------------------------------------------
 *
 * th8PluginFind --
 *
 *	Find a plugin entry by name.  Returns NULL if not found.
 *	If ppPrev is non-NULL, sets *ppPrev to the pointer to
 *	the pNext field that points to the found entry (for
 *	unlinking).
 *
 * Why / How:
 *	Registration, duplicate-detection, and unregistration all
 *	need to locate a plugin by name; centralizing the lookup here
 *	keeps that one linear walk of the interpreter's singly-linked
 *	plugin list (comparing length then bytes).  The list is short
 *	(one node per registered plugin) so a linear scan is fine.
 *
 * Results:
 *	Pointer to the matching Th8_PluginEntry, or NULL if no plugin
 *	with that name is registered.
 *
 * Side effects:
 *	None.  Returns a borrowed pointer into the plugin list.
 *
 *----------------------------------------------------------------------
 */

static Th8_PluginEntry *
th8PluginFind(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_PluginEntry *p = (Th8_PluginEntry *)th8GetPluginList(interp);

    while (p) {
	if (p->nName == nName &&
	    Th8_Memcmp(interp, p->zName, zName, nName) == 0) {
	    return p;
	}
	p = p->pNext;
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PluginRegistered --
 *
 *	Report whether a plugin with the given name is already
 *	registered in the interpreter.  Lets a re-registering caller
 *	(TH8K-006) distinguish the harmless "already present" case from
 *	a real registration failure so it can propagate the latter.
 *
 * Why / How:
 *	Thin predicate over th8PluginFind: it exposes only a yes/no
 *	answer so callers outside this file can test membership
 *	without seeing (or depending on) the Th8_PluginEntry layout.
 *
 * Results:
 *	Non-zero if the named plugin is registered; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8PluginRegistered(Th8_Interp *interp, const char *zName, size_t nName)
{
    return th8PluginFind(interp, zName, nName) != NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8RemoveCommandByToken --
 *
 *	Remove the command identified by the given creation token,
 *	if it still exists.
 *
 * Why / How:
 *	Plugin unregistration must delete exactly the commands the
 *	plugin created, identified by the tokens recorded at
 *	registration.  Rather than scanning every namespace, it
 *	delegates to Th8_DeleteCommand, which resolves the token in
 *	O(1) via the interpreter's secondary token index; a token that
 *	no longer maps to a live command is silently ignored.
 *
 * Results:
 *	None (void).  Any delete failure is intentionally ignored.
 *
 * Side effects:
 *	Deletes the matching command from the interpreter (if present).
 *
 *----------------------------------------------------------------------
 */

static void
th8RemoveCommandByToken(Th8_Interp *interp, th8_uint64_t token)
{
    /* O(1) via the secondary token index + stored name. */
    (void)Th8_DeleteCommand(interp, token);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RegisterPlugin --
 *
 *	Register a named command plugin with the interpreter, creating every
 *	command the plugin supplies.
 *
 * Why / How:
 *	A plugin is a named group of commands produced by a single
 *	Th8_GetCommandsProc.  This validates its arguments, rejects a name
 *	that is already registered, then calls xGetCommands(NULL, &nCommand)
 *	to size the command table, allocates and zero-fills it, calls
 *	xGetCommands again to fill it, and creates each command -- recording
 *	the created command tokens in a plugin entry linked onto the
 *	interpreter's plugin list so Th8_UnregisterPlugin can later remove
 *	them.  Any failure frees what was allocated and leaves no command or
 *	plugin entry behind.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR (with an interpreter result message) on
 *	invalid arguments, a duplicate name, an empty or failed command
 *	table, allocation failure, or a command-creation failure.
 *
 * Side effects:
 *	Creates commands in the interpreter and adds a plugin entry to its
 *	plugin list.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RegisterPlugin(
    Th8_Interp *interp,
    const char *zName,
    Th8_GetCommandsProc xGetCommands)
{
    Th8_PluginEntry *pPlugin;
    Th8_CommandEntry *aEntry = NULL;
    int nCommand = 0;
    int i;
    size_t nName;

    if (!interp || !zName || !xGetCommands) {
	if (interp) {
	    Th8_SetResult(interp, "plugin: invalid arguments", TH8_NOLEN);
	}
	return TH8_ERROR;
    }

    nName = Th8_Strlen(interp, zName);

    if (th8PluginFind(interp, zName, nName)) {
	Th8_ErrorMessage(
	    interp, "plugin already registered: \"", zName, nName);
	return TH8_ERROR;
    }

    /* Query command count. */
    if (xGetCommands(NULL, &nCommand) != TH8_OK || nCommand <= 0) {
	Th8_SetResult(
	    interp, "plugin: GetCommands returned no commands", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Allocate and fill command table. */
    aEntry = (Th8_CommandEntry *)
        TH8_ALLOC_MUL(interp, (size_t)nCommand, sizeof(Th8_CommandEntry));
    if (!aEntry) return TH8_ERROR;
    /*
     * AUDIT-OK[size-cast-multiply-after,size-name-multiply]: the
     * TH8_ALLOC_MUL call above already validated that
     * nCommand * sizeof(Th8_CommandEntry) does not overflow, so
     * the same multiplied byte count here is provably safe.
     * Recomputing it via TH8_SAFE_MUL_SIZE would only duplicate
     * the check the allocator just performed.
     */
    Th8_Memset(
        interp, aEntry, 0, (size_t)nCommand * sizeof(Th8_CommandEntry));

    if (xGetCommands(aEntry, &nCommand) != TH8_OK) {
	Th8_Free(interp, aEntry);
	Th8_SetResult(interp, "plugin: GetCommands failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Allocate plugin metadata. */
    pPlugin = (Th8_PluginEntry *)TH8_ALLOC(interp, sizeof(Th8_PluginEntry));
    if (!pPlugin) {
	Th8_Free(interp, aEntry);
	return TH8_ERROR;
    }
    Th8_Memset(interp, pPlugin, 0, sizeof(Th8_PluginEntry));

    pPlugin->zName = (char *)TH8_ALLOC_STR(interp, nName);
    if (!pPlugin->zName) {
	Th8_Free(interp, pPlugin);
	Th8_Free(interp, aEntry);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, pPlugin->zName, zName, nName);
    pPlugin->zName[nName] = 0;
    pPlugin->nName = nName;
    pPlugin->xGetCommands = xGetCommands;

    pPlugin->aToken = (th8_uint64_t *)
        TH8_ALLOC_MUL(interp, (size_t)nCommand, sizeof(th8_uint64_t));
    if (!pPlugin->aToken) {
	Th8_Free(interp, pPlugin->zName);
	Th8_Free(interp, pPlugin);
	Th8_Free(interp, aEntry);
	return TH8_ERROR;
    }
    pPlugin->nToken = nCommand;

    /* Register each command. */
    for (i = 0; i < nCommand; i++) {
	th8_uint64_t tok = th8NextCmdToken(interp);

	Th8_CreateCommand(
	    interp, aEntry[i].zName, aEntry[i].xProc, NULL, NULL, &tok);

	aEntry[i].token = tok;
	pPlugin->aToken[i] = tok;
    }

    /* Link into the interpreter's plugin list. */
    pPlugin->pNext = (Th8_PluginEntry *)th8GetPluginList(interp);
    th8SetPluginList(interp, pPlugin);

    Th8_Free(interp, aEntry);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_UnregisterPlugin --
 *
 *	Unregister a previously registered plugin by name, removing every
 *	command it created.
 *
 * Why / How:
 *	Looks up the plugin entry by name on the interpreter's plugin list;
 *	if found, deletes each command it created (by the tokens recorded at
 *	registration), unlinks the entry from the list, and frees the entry's
 *	token array, name, and the entry itself.  It is the inverse of
 *	Th8_RegisterPlugin.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR (with an interpreter result message) on
 *	a NULL interpreter/name or a name that is not registered.
 *
 * Side effects:
 *	Deletes the plugin's commands from the interpreter and frees the
 *	plugin entry.
 *
 *----------------------------------------------------------------------
 */

int
Th8_UnregisterPlugin(Th8_Interp *interp, const char *zName)
{
    Th8_PluginEntry *p, *pPrev;
    size_t nName;
    int i;

    /* Split per Finding 005.  Single-condition `if`s. */
    if (!interp) return TH8_ERROR;
    if (!zName) {
	Th8_SetResult(interp, "plugin: invalid arguments", TH8_NOLEN);
	return TH8_ERROR;
    }

    nName = Th8_Strlen(interp, zName);

    /* Find the plugin. */
    pPrev = NULL;
    p = (Th8_PluginEntry *)th8GetPluginList(interp);
    while (p) {
	if (p->nName == nName &&
	    Th8_Memcmp(interp, p->zName, zName, nName) == 0) {
	    break;
	}
	pPrev = p;
	p = p->pNext;
    }

    if (!p) {
	Th8_ErrorMessage(interp, "plugin not registered: \"", zName, nName);
	return TH8_ERROR;
    }

    /* Remove commands by token. */
    for (i = 0; i < p->nToken; i++) {
	th8RemoveCommandByToken(interp, p->aToken[i]);
    }

    /* Unlink from list. */
    if (pPrev) {
	pPrev->pNext = p->pNext;
    } else {
	th8SetPluginList(interp, p->pNext);
    }

    /* Free plugin metadata. */
    Th8_Free(interp, p->aToken);
    Th8_Free(interp, p->zName);
    Th8_Free(interp, p);

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendPlugins --
 *
 *	Append the name of every registered plugin to a Tcl-list string.
 *
 * Why / How:
 *	Walks the interpreter's plugin list and appends each plugin's name as
 *	one element to the caller's growing list buffer (*pz / *pn) via
 *	Th8_ListAppend, in registration order.  Used to report the set of
 *	loaded plugins (e.g. for introspection).
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Grows the caller's list buffer (*pz / *pn), reallocating as needed.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendPlugins(Th8_Interp *interp, char **pz, size_t *pn)
{
    Th8_PluginEntry *p;

    p = (Th8_PluginEntry *)th8GetPluginList(interp);
    while (p) {
	Th8_ListAppend(interp, pz, pn, p->zName, p->nName);
	p = p->pNext;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PluginCleanup --
 *
 *	Free all plugin metadata during interpreter deletion.
 *	Commands are already freed by namespace cleanup; this
 *	only frees the plugin entries themselves.
 *
 * Why / How:
 *	By the time an interpreter is torn down its commands have
 *	already been destroyed with the namespaces that held them, so
 *	the plugin entries would otherwise leak.  This walks the
 *	plugin list once, freeing each entry's recorded token array,
 *	its name, and the entry node, then clears the interpreter's
 *	list head so nothing dangles.
 *
 * Results:
 *	None (void).
 *
 * Side effects:
 *	Frees every plugin entry (token array, name, node) and sets
 *	the interpreter's plugin list to NULL.
 *
 *----------------------------------------------------------------------
 */

void
th8PluginCleanup(Th8_Interp *interp)
{
    Th8_PluginEntry *p = (Th8_PluginEntry *)th8GetPluginList(interp);

    while (p) {
	Th8_PluginEntry *pNext = p->pNext;

	Th8_Free(interp, p->aToken);
	Th8_Free(interp, p->zName);
	Th8_Free(interp, p);
	p = pNext;
    }
    th8SetPluginList(interp, NULL);
}
