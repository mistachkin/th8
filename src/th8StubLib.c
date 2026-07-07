/*
 *----------------------------------------------------------------------
 *
 * th8StubLib.c --
 *
 *	TH8 stubs library.  This file is compiled into a small static
 *	library (libth8stub.a / th8stub.lib) that extensions link
 *	against instead of the full TH8 library.
 *
 *	It provides:
 *	  - Th8_InitStubs()  -- retrieves the stubs table from the
 *	    interpreter and validates magic/version.
 *	  - th8StubsPtr      -- the global pointer that the macros
 *	    in th8Decls.h redirect through.
 *
 *	Extensions compiled with USE_TH8_STUBS call Th8_InitStubs()
 *	in their _Init function, then all subsequent Th8_* calls go
 *	through th8StubsPtr automatically.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 *----------------------------------------------------------------------
 */

#include "th8.h"
#include "th8Decls.h"

/*
 * This is the first portion of the of the Th8_Interp struct; it
 * is needed by Th8_GetStubs().
 */

struct Th8_Interp {
    th8_int64_t nVersion;
    const Th8StubsTable *pStubs;
};

/*
 * The global stubs pointer.  Set by Th8_InitStubs(), used by the
 * macros in th8Decls.h when USE_TH8_STUBS is defined.
 */

const Th8StubsTable *th8StubsPtr = 0;


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetStubs --
 *
 *	Return a pointer to the interpreter's stubs table.
 *	This is an opaque void* that the stubs library casts
 *	to Th8StubsTable*.
 *
 *----------------------------------------------------------------------
 */

const void *
Th8_GetStubs(
    Th8_Interp *interp)		/* Interpreter. */
{
    return interp->pStubs;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_InitStubs --
 *
 *	Initialize the stubs table for an extension.  This must be
 *	called as the first TH8 API call in an extension's _Init
 *	function.  It retrieves the stubs table pointer from the
 *	interpreter and validates the magic number and version.
 *
 *	If successful, th8StubsPtr is set and all subsequent Th8_*
 *	calls (via the macros) will go through the stubs table.
 *
 *	The version and exact parameters are reserved for future
 *	use (version negotiation).  Currently they are ignored.
 *
 * Results:
 *	Pointer to the stubs table on success; NULL on failure
 *	(stubs not available, magic mismatch, or version mismatch).
 *
 * Side effects:
 *	Sets the process-global th8StubsPtr.
 *
 *----------------------------------------------------------------------
 */

const Th8StubsTable *
Th8_InitStubs(
    Th8_Interp *interp,	/* Interpreter to get stubs from. */
    const char *version,	/* Required version (reserved). */
    int exact)			/* Exact version match (reserved). */
{
    const Th8StubsTable *pStubs;

    (void)version;
    (void)exact;

    if (!interp) {
	return 0;
    }

    /*
     * Th8_GetStubs returns the opaque pStubs pointer stored
     * in the interpreter.  We cast it to our struct type and
     * validate the magic number.
     */

    pStubs = (const Th8StubsTable *)Th8_GetStubs(interp);
    if (!pStubs) {
	return 0;
    }
    if (pStubs->magic != TH8_STUBS_MAGIC) {
	return 0;
    }
    if (pStubs->version < TH8_STUBS_VERSION) {
	return 0;
    }

    th8StubsPtr = pStubs;
    return pStubs;
}
