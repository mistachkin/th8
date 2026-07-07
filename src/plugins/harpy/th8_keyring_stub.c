/*
 * th8_keyring_stub.c -- Default empty implementation of
 *	Th8_GetEmbeddedKeyring().
 *
 * The canonical TH8 build has no embedded trust anchors; embedders
 * generate their own keyring source via:
 *
 *	tclsh tools/mkkey.tcl --keyring NAME k1.snk k2.snk ...
 *
 * and either replace this stub in the link or define
 * TH8_OMIT_KEYRING_STUB at compile time so this translation unit
 * elides its definition, letting the generated keyring's strong
 * definition win.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"

#if defined(TH8_ENABLE_CRYPTOGRAPHY) && !defined(TH8_OMIT_KEYRING_STUB)

/*
 * Th8_GetEmbeddedKeyring --
 *
 *	Return the embedder-supplied keyring of trust anchors used
 *	by the signed-only loader.  This is the weak, default
 *	implementation: the canonical TH8 build ships with no
 *	embedded keyring, so this stub reports zero entries.
 *
 *	An embedder that wants its own keyring generates a strong
 *	definition with `tclsh tools/mkkey.tcl --keyring ...` and
 *	either replaces this object in the link or builds with
 *	`-DTH8_OMIT_KEYRING_STUB` so the strong definition wins.
 *
 * Parameters:
 *	pnEntries -- output pointer for the number of keyring
 *		entries.  May be NULL; if non-NULL the stub writes 0.
 *
 * Returns:
 *	NULL (no keyring).  An overriding strong definition would
 *	return a pointer to an array of `*pnEntries` entries.
 *
 * Side effects:
 *	None.  Writes 0 to `*pnEntries` if non-NULL.
 */
const Th8_KeyringEntry *
Th8_GetEmbeddedKeyring(size_t *pnEntries)
{
    if (pnEntries) *pnEntries = 0;
    return (const Th8_KeyringEntry *)0;
}

#endif /* TH8_ENABLE_CRYPTOGRAPHY && !TH8_OMIT_KEYRING_STUB */
