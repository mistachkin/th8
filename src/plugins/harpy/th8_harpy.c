/*
 * th8_harpy.c -- Harpy script signature support for TH8.
 *
 * Parses Harpy ".b64sig" raw signature files and (in the future)
 * Harpy script certificate files.  Harpy is the script signing
 * system used by the Eagle scripting language.
 *
 * Compile-time gate: TH8_ENABLE_CRYPTOGRAPHY
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_util.h"

#if defined(TH8_ENABLE_CRYPTOGRAPHY)


/*
 *======================================================================
 *
 * HARPY SIGNATURE FILE LOADING
 *
 *	Parses a Harpy ".b64sig" raw signature file.  The format is:
 *
 *	  - A Tcl comment header (lines starting with '#').
 *	  - One or more lines of base64-encoded signature data,
 *	    possibly indented with whitespace.
 *	  - Optional trailing whitespace/newlines.
 *
 *	The comment header contains the public key token on line 3
 *	in the form "# filename -- PUBLICKEYTOKEN".  This token
 *	is extracted if present and returned to the caller.
 *
 *	The base64 body is decoded to produce the raw RSA signature
 *	bytes.
 *
 *======================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * Th8_HarpySigLoad --
 *
 *	Parse a Harpy .b64sig file (raw bytes, not a file path).
 *	Extracts the base64 body, decodes it, and returns the raw
 *	signature bytes.
 *
 *	The caller must free *ppSig with Th8_Free when done.
 *
 *	If ppId is non-NULL and the header contains a public key
 *	token (e.g. "26f17c3a1a544324"), a NUL-terminated copy is
 *	returned in *ppId.  The caller must free it with Th8_Free.
 *
 * Why / How:
 *	TH8's signed-script policy requires loading Harpy signature
 *	files to verify script authenticity.  The .b64sig format is a
 *	plain-text file with a Tcl comment header (lines starting
 *	with '#') followed by base64-encoded RSA signature data.  The
 *	parser makes two logical passes: first it skips comment lines
 *	while extracting the public key token from the third header
 *	line (pattern "# name -- TOKEN"), then it collects the
 *	remaining non-whitespace characters as base64 and decodes
 *	them via th8Base64Decode.  The public key token identifies
 *	which signing key produced the signature, enabling multi-key
 *	verification.
 *
 * Results:
 *	TH8_OK on success with *ppSig and *pnSig set to the decoded
 *	signature bytes, and *ppId set to the key token (if found).
 *	TH8_ERROR if the input is NULL, contains no base64 data, or
 *	the base64 decode fails.
 *
 * Side effects:
 *	Allocates memory for the signature bytes and (optionally)
 *	the key token string.  The caller owns both allocations.
 *
 *----------------------------------------------------------------------
 */

int
Th8_HarpySigLoad(
    Th8_Interp *interp,
    const char *zData,  /* File contents (text). */
    size_t nData,  /* Byte length, or TH8_NOLEN. */
    unsigned char **ppSig, /* OUT: decoded signature bytes. */
    size_t *pnSig,  /* OUT: signature byte count. */
    char **ppId)  /* OUT: public key token (or NULL). */
{
    size_t i;
    size_t nB64 = 0;
    char *zB64 = NULL;
    unsigned char *zSig = NULL;
    size_t nSig = 0;
    char *zId = NULL;

    if (!zData || !ppSig || !pnSig) {
	Th8_SetResultStatic(interp, "Harpy: invalid arguments", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (nData == TH8_NOLEN) nData = Th8_Strlen(interp, zData);

    *ppSig = NULL;
    *pnSig = 0;
    if (ppId) *ppId = NULL;

    /*
     * Pass 1: Skip the comment header, collect base64 body.
     *
     * Comment lines start with '#' (possibly after whitespace).
     * The first non-comment, non-empty line starts the base64 body.
     * Extract the public key token from line 3 if it matches the
     * pattern "# name -- TOKEN".
     */

    {
	int lineNo = 0;
	int inHeader = 1;
	size_t bodyStart = 0;

	for (i = 0; i < nData;) {
	    size_t lineStart = i;
	    size_t lineEnd;

	    /* Find end of line. */
	    while (i < nData && zData[i] != '\n')
		i++;
	    lineEnd = i;
	    if (i < nData) i++; /* skip \n */

	    lineNo++;

	    if (inHeader) {
		/* Skip leading whitespace on the line. */
		size_t j = lineStart;
		while (j < lineEnd && (zData[j] == ' ' || zData[j] == '\t')) {
		    j++;
		}

		if (j < lineEnd && zData[j] == '#') {
		    /*
		     * Comment line.  Check line 3 for the token.
		     * Pattern: "# name -- PUBLICKEYTOKEN"
		     */
		    if (lineNo == 3 && ppId) {
			const char *zDash;
			size_t nLine = lineEnd - lineStart;

			zDash = NULL;
			{
			    size_t k;
			    for (k = lineStart; k + 1 < lineEnd; k++) {
				if (zData[k] == '-' && zData[k + 1] == '-') {
				    zDash = &zData[k];
				    break;
				}
			    }
			}
			if (zDash) {
			    const char *p = zDash + 2;
			    size_t end = lineEnd;

			    /* Skip space after "--" */
			    while (p < &zData[end] && *p == ' ')
				p++;
			    /* Trim trailing whitespace. */
			    while (end > (size_t)(p - zData) &&
			           (zData[end - 1] == ' ' ||
			            zData[end - 1] == '\r')) {
				end--;
			    }
			    if (p < &zData[end]) {
				size_t nId = end - (size_t)(p - zData);
				zId = (char *)TH8_ALLOC_STR(interp, nId);
				if (zId) {
				    Th8_Memcpy(interp, zId, p, nId);
				    zId[nId] = '\0';
				}
			    }
			}
			(void)nLine;
		    }
		    continue;
		}

		/* Empty line in header: skip. */
		if (j >= lineEnd) continue;

		/* First non-comment, non-empty line. */
		inHeader = 0;
		bodyStart = lineStart;
	    }
	}

	/*
	 * Collect all base64 body text (from bodyStart to end,
	 * stripping whitespace).
	 */
	/* Nested per Finding 005 sec. 5b: bodyStart < nData is
	 * structurally guaranteed by the parser loop above
	 * (lineStart < nData when bodyStart is assigned);
	 * C2-Pair is intrinsic-dead. */
	if (bodyStart > 0)
	    if (bodyStart < nData) {
		size_t nRaw = nData - bodyStart;
		size_t k, m;

		zB64 = (char *)TH8_ALLOC_STR(interp, nRaw);
		if (!zB64) {
		    Th8_Free(interp, zId);
		    return TH8_ERROR;
		}

		m = 0;
		for (k = bodyStart; k < nData; k++) {
		    char c = zData[k];

		    /* Keep only base64 alphabet + padding. */
		    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		        (c >= '0' && c <= '9') || c == '+' || c == '/' ||
		        c == '=') {
			zB64[m++] = c;
		    }
		    /* Skip whitespace, newlines, etc. */
		}
		zB64[m] = '\0';
		nB64 = m;
	    }
    }

    if (!zB64 || nB64 == 0) {
	Th8_Free(interp, zId);
	Th8_Free(interp, zB64);
	Th8_SetResultStatic(
	    interp, "Harpy: no base64 signature data found", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Decode the base64 body using the shared decoder.
     */
    {
	int rc = th8Base64Decode(interp, zB64, nB64, &zSig, &nSig);

	Th8_Free(interp, zB64);

	if (rc != TH8_OK) {
	    Th8_Free(interp, zId);
	    return rc;
	}
    }

    *ppSig = zSig;
    *pnSig = nSig;
    if (ppId)
	*ppId = zId;
    else
	Th8_Free(interp, zId);

    return TH8_OK;
}


#endif /* TH8_ENABLE_CRYPTOGRAPHY */
