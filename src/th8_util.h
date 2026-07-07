/*
 * th8_util.h -- Shared utility functions for TH8 modules.
 *
 * Functions in this header are used by th8_lang.c, th8_regex.c,
 * and any future TH8 extension modules.  They depend only on
 * the public TH8 API (th8.h).
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_UTIL_H
#define TH8_UTIL_H

#include "th8.h"

/*
 *----------------------------------------------------------------------
 *
 * th8StrEq --
 *
 *	Compare a sized string against a NUL-terminated literal.
 *	Used for keyword matching (switches, subcommands, etc.).
 *
 * Results:
 *	1 if equal, 0 if not.
 *
 *----------------------------------------------------------------------
 */

int th8StrEq(Th8_Interp *interp, const char *z, size_t n, const char *zLit);

/*
 *----------------------------------------------------------------------
 *
 * th8ParseIndex --
 *
 *	Parse an index argument that may be "end", "end-N", or a
 *	plain integer.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on parse failure.
 *
 *----------------------------------------------------------------------
 */

int th8ParseIndex(
    Th8_Interp *interp,
    const char *z,
    size_t n,
    int nCount,
    int *piIndex);

/*
 *----------------------------------------------------------------------
 *
 * Base64 encoding/decoding (th8_base64.c)
 *
 *----------------------------------------------------------------------
 */

/*
 * th8Base64Encode --
 *
 *	Encode binary data to base64 with CRLF line breaks every
 *	76 characters (RFC 2045).  Sets the interpreter result to
 *	the encoded string.
 */
int th8Base64Encode(Th8_Interp *interp, const unsigned char *zIn, size_t nIn);

/*
 * th8Base64Decode --
 *
 *	Decode base64 to binary.  Whitespace is silently ignored.
 *	On success, *ppOut is set to a Th8_Malloc'd buffer and
 *	*pnOut to the byte count.  The caller must Th8_Free *ppOut.
 */
int th8Base64Decode(
    Th8_Interp *interp,
    const char *zIn,
    size_t nIn,
    unsigned char **ppOut,
    size_t *pnOut);

/*
 * th8GlobMatch -- alias for the public Th8_GlobMatch API.
 * Kept for backward compatibility with callers that use the
 * internal name.
 */
#define th8GlobMatch Th8_GlobMatch

#endif /* TH8_UTIL_H */
