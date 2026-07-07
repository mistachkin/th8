/*
 * th8_base64.c -- Base64 encoding and decoding for TH8.
 *
 * Shared implementation used by the [base64] script command
 * (th8_lang.c) and the Harpy signature loader (th8_rsa.c).
 *
 * Encoding uses the standard RFC 4648 alphabet (A-Z, a-z, 0-9, +, /)
 * with '=' padding and CRLF line breaks every 76 characters per
 * RFC 2045 (MIME).
 *
 * Decoding silently ignores whitespace.  Invalid characters produce
 * an error.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_util.h"


/*
 * RFC 4648 encoding alphabet.
 */

static const char th8B64Enc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
                                "ghijklmnopqrstuvwxyz0123456789+/";

/*
 * Decode table: maps ASCII byte to 6-bit value (0-63).
 * 64 = padding ('='), 65 = whitespace (skip), 0xFF = invalid.
 */

static const unsigned char th8B64Dec[256] =
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /*  0- 7 */
     0xFF, 65,   65,   65,   65,   65,   0xFF, 0xFF,  /*  8-15 */
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* 16-23 */
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* 24-31 */
     65,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* 32-39 */
     0xFF, 0xFF, 0xFF, 62,   0xFF, 0xFF, 0xFF, 63,  /* 40-47 */
     52,   53,   54,   55,   56,   57,   58,   59,  /* 48-55 */
     60,   61,   0xFF, 0xFF, 0xFF, 64,   0xFF, 0xFF,  /* 56-63 */
     0xFF, 0,    1,    2,    3,    4,    5,    6,  /* 64-71 */
     7,    8,    9,    10,   11,   12,   13,   14,  /* 72-79 */
     15,   16,   17,   18,   19,   20,   21,   22,  /* 80-87 */
     23,   24,   25,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* 88-95 */
     0xFF, 26,   27,   28,   29,   30,   31,   32,  /* 96-103 */
     33,   34,   35,   36,   37,   38,   39,   40,  /* 104-111 */
     41,   42,   43,   44,   45,   46,   47,   48,  /* 112-119 */
     49,   50,   51,   0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* 120-127 */
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};


/*
 *----------------------------------------------------------------------
 *
 * th8Base64Encode --
 *
 *	Encode binary data to base64.  Inserts CRLF every 76
 *	characters per RFC 2045.  No trailing line ending.
 *	Sets the interpreter result to the encoded string.
 *
 * Why / How:
 *	Base64 encoding is needed by the [base64] script command for
 *	general-purpose encoding and by the Harpy signature loader
 *	(th8_rsa.c) for certificate handling.  The output size is
 *	pre-computed exactly (4 output bytes per 3 input bytes, plus
 *	2 bytes per CRLF line break every 76 characters) so only a
 *	single allocation is needed.  Input is processed in 3-byte
 *	chunks packed into a 24-bit triple, then split into four
 *	6-bit indices into the RFC 4648 alphabet.  Trailing chunks
 *	of 1 or 2 bytes are padded with '=' per the standard.
 *
 * Results:
 *	TH8_OK on success with the interpreter result set to the
 *	encoded string; TH8_ERROR on allocation failure.
 *
 * Side effects:
 *	Sets the interpreter result.  Allocates and frees a temporary
 *	output buffer.
 *
 *----------------------------------------------------------------------
 */

int
th8Base64Encode(Th8_Interp *interp, const unsigned char *zIn, size_t nIn)
{
    size_t nRaw = 0, nLines = 0, nOut = 0, nTmp = 0;
    char *zOut;
    size_t i, j;
    int linePos = 0;

    if (TH8_SAFE_ADD_SIZE(nIn, 2, &nTmp)) return TH8_ERROR;
    if (TH8_SAFE_MUL_SIZE(nTmp / 3, 4, &nRaw)) return TH8_ERROR;
    nLines = (nRaw > 0) ? (nRaw - 1) / 76 : 0;
    if (TH8_SAFE_MUL_SIZE(nLines, 2, &nTmp)) return TH8_ERROR;
    if (TH8_SAFE_ADD_SIZE(nRaw, nTmp, &nOut)) return TH8_ERROR;

    zOut = (char *)TH8_ALLOC_STR(interp, nOut);
    if (!zOut) return TH8_ERROR;

    for (i = 0, j = 0; i < nIn;) {
	unsigned int a, b, c, triple;
	int nChunk;

	a = zIn[i++];
	nChunk = 1;
	b = (i < nIn) ? (nChunk = 2, zIn[i++]) : 0;
	c = (i < nIn) ? (nChunk = 3, zIn[i++]) : 0;
	triple = (a << 16) | (b << 8) | c;

	zOut[j++] = th8B64Enc[(triple >> 18) & 0x3F];
	zOut[j++] = th8B64Enc[(triple >> 12) & 0x3F];
	zOut[j++] = (nChunk >= 2) ? th8B64Enc[(triple >> 6) & 0x3F] : '=';
	zOut[j++] = (nChunk >= 3) ? th8B64Enc[triple & 0x3F] : '=';
	linePos += 4;

	if (linePos >= 76 && i < nIn) {
	    zOut[j++] = '\r';
	    zOut[j++] = '\n';
	    linePos = 0;
	}
    }
    zOut[j] = '\0';

    Th8_SetResult(interp, zOut, j);
    Th8_Free(interp, zOut);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Base64Decode --
 *
 *	Decode base64 to binary.  Whitespace is silently ignored.
 *	Returns a Th8_AttemptMalloc'd buffer in *ppOut.
 *
 * Why / How:
 *	Used by the [base64] script command and the Harpy certificate
 *	loader to convert base64 text back to raw bytes.  The decode
 *	table (th8B64Dec) maps each ASCII byte to its 6-bit value,
 *	with sentinel values for whitespace (65 = skip), padding
 *	(64 = '='), and invalid characters (0xFF = error).
 *
 *	Security: the maximum output size is computed with an integer
 *	overflow guard before allocation -- if nIn is large enough to
 *	overflow the (nIn/4)*3+3 calculation, the function returns
 *	TH8_ERROR immediately.  Invalid characters (anything not in
 *	the base64 alphabet, whitespace, or padding) produce an
 *	explicit error rather than silent corruption.  The output
 *	pointers ppOut and pnOut are NULL-safe; when ppOut is NULL
 *	the decoded buffer is freed (useful for validation-only calls).
 *
 * Results:
 *	TH8_OK on success with *ppOut set to the decoded buffer and
 *	*pnOut set to the decoded byte count; TH8_ERROR on invalid
 *	input or allocation failure.
 *
 * Side effects:
 *	Allocates a buffer via Th8_AttemptMalloc that the caller must
 *	free.  May set the interpreter result to an error message.
 *
 *----------------------------------------------------------------------
 */

int
th8Base64Decode(
    Th8_Interp *interp,
    const char *zIn,
    size_t nIn,
    unsigned char **ppOut,
    size_t *pnOut)
{
    size_t nOutMax;
    unsigned char *zOut;
    size_t i, j;
    unsigned char buf[4];
    int bufLen = 0;
    int padCount = 0;

    /*
     * Guard against integer overflow in output size calculation.
     * Worst case: every 4 input bytes produce 3 output bytes.
     */

    if (nIn > ((size_t)-1 - 3) / 3 * 4) {
	if (ppOut) *ppOut = NULL;
	if (pnOut) *pnOut = 0;
	return TH8_ERROR;
    }
    nOutMax = (nIn / 4) * 3 + 3;

    if (ppOut) *ppOut = NULL;
    if (pnOut) *pnOut = 0;

    zOut = (unsigned char *)TH8_ALLOC_STR(interp, nOutMax);
    if (!zOut) return TH8_ERROR;

    j = 0;
    for (i = 0; i < nIn; i++) {
	unsigned char v = th8B64Dec[(unsigned char)zIn[i]];

	if (v == 65) continue;  /* whitespace */
	if (v == 0xFF) {
	    Th8_Free(interp, zOut);
	    Th8_SetResult(interp, "invalid base64 character", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (v == 64) {
	    padCount++;
	    v = 0;
	}
	buf[bufLen++] = v;

	if (bufLen == 4) {
	    unsigned int triple = ((unsigned int)buf[0] << 18) |
	                          ((unsigned int)buf[1] << 12) |
	                          ((unsigned int)buf[2] << 6) |
	                          ((unsigned int)buf[3]);

	    zOut[j++] = (unsigned char)((triple >> 16) & 0xFF);
	    if (padCount < 2) {
		zOut[j++] = (unsigned char)((triple >> 8) & 0xFF);
	    }
	    if (padCount < 1) {
		zOut[j++] = (unsigned char)(triple & 0xFF);
	    }
	    bufLen = 0;
	    padCount = 0;
	}
    }
    zOut[j] = '\0';

    if (ppOut)
	*ppOut = zOut;
    else
	Th8_Free(interp, zOut);
    if (pnOut) *pnOut = j;

    return TH8_OK;
}
