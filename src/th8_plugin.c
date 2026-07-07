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
 * th8RemoveCommandByToken --
 *
 *	Walk ALL commands in ALL namespaces looking for a command
 *	whose nToken matches.  If found, remove it.
 *
 *	This is O(total commands) per call -- acceptable because
 *	plugin unregistration is rare.
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
