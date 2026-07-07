/*
 * th8_util.c -- Shared utility functions for TH8 modules.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_util.h"

/*
 *----------------------------------------------------------------------
 *
 * th8StrEq --
 *
 *	Compare a sized string against a NUL-terminated literal.
 *
 * Results:
 *	1 if equal, 0 if not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8StrEq(
    Th8_Interp *interp, /* Interpreter for Memcmp. */
    const char *z,  /* Sized string. */
    size_t n,   /* Length (raw, with taint). */
    const char *zLit)  /* NUL-terminated literal. */
{
    size_t nLit = 0;

    while (zLit[nLit]) {
	nLit++;
    }
    return (TH8_LEN(n) == nLit && 0 == Th8_Memcmp(interp, z, zLit, nLit));
}


/*
 *----------------------------------------------------------------------
 *
 * th8ParseIndex --
 *
 *	Parse an index that may be "end", "end-N", or plain integer.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on failure.
 *	*piIndex set to the resolved integer index.
 *
 * Side effects:
 *	Sets interpreter error message on failure.
 *
 *----------------------------------------------------------------------
 */

int
th8ParseIndex(
    Th8_Interp *interp, /* Interpreter. */
    const char *z,  /* Index string. */
    size_t n,   /* Length (raw). */
    int nCount,   /* Total number of elements. */
    int *piIndex)  /* OUT: resolved index. */
{
    if (th8StrEq(interp, z, n, "end")) {
	*piIndex = nCount - 1;
	return TH8_OK;
    }
    if (TH8_LEN(n) > 4 && 0 == Th8_Memcmp(interp, z, "end-", 4)) {
	int offset;
	int rc;

	rc = Th8_ToInt(interp, &z[4], TH8_LEN(n) - 4, &offset);
	if (rc != TH8_OK) return rc;
	*piIndex = nCount - 1 - offset;
	return TH8_OK;
    }
    return Th8_ToInt(interp, z, n, piIndex);
}
