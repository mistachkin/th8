/*
 * th8_snk.c -- RSA SNK key loading for TH8.
 *
 * Parses Microsoft CAPI / .NET Strong Name Key (SNK) files and
 * extracts RSA key components (modulus, exponent, and optionally
 * private key factors).
 *
 * Supported input formats:
 *
 *   1. Raw CAPI PRIVATEKEYBLOB (from `sn -k key.snk`):
 *      Starts with PUBLICKEYSTRUC { bType=0x07, bVersion=0x02,
 *      aiKeyAlg=0x00002400 } followed by RSAPUBKEY { magic="RSA2",
 *      bitlen, pubexp } and the key material.
 *
 *   2. Raw CAPI PUBLICKEYBLOB (public key only):
 *      Same structure but bType=0x06, magic="RSA1", and only
 *      the modulus is present.
 *
 *   3. .NET Strong Name public key (from `sn -p`):
 *      A 12-byte header { SigAlgId, HashAlgId, cbPublicKey }
 *      followed by a CAPI PUBLICKEYBLOB.
 *
 * All multi-byte integers in CAPI blobs are little-endian.
 * The RSA components are stored in big-endian (mathematical)
 * byte order in the Th8_RsaKey structure.
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

#  include <openssl/sha.h>


/*
 *----------------------------------------------------------------------
 *
 * CAPI blob constants.
 *
 *----------------------------------------------------------------------
 */

#  define CAPI_PRIVATEKEYBLOB 0x07
#  define CAPI_PUBLICKEYBLOB  0x06
#  define CAPI_BVERSION       0x02
#  define CAPI_CALG_RSA_SIGN  0x00002400
#  define CAPI_CALG_RSA_KEYX  0x0000a400

#  define CAPI_RSA1_MAGIC 0x31415352  /* "RSA1" little-endian */
#  define CAPI_RSA2_MAGIC 0x32415352  /* "RSA2" little-endian */

/*
 * .NET Strong Name public key header.
 */
#  define DOTNET_SIG_ALG_RSA   0x00002400
#  define DOTNET_HASH_ALG_SHA1 0x00008004


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKey --
 *
 *	Parsed RSA key.  All byte arrays are in big-endian
 *	(mathematical) order, suitable for use with libtommath
 *	or libtomcrypt.
 *
 *	For a public-only key, the private fields (zPrivExp,
 *	zPrime1, etc.) are NULL.
 *
 *----------------------------------------------------------------------
 */

struct Th8_RsaKey {
    int nBits;   /* Key size in bits (1024, 2048, ...). */
    unsigned int ePub;  /* Public exponent (usually 65537). */
    int bHasPrivate;  /* Non-zero if private key is present. */
    int bProtected;  /* Non-zero if key material is in a
				 * Th8_ProtectedRegion (mlock'd page). */

    /*
     * Protected region: when bProtected is true, all component
     * pointers below point INTO this region's data page.  The
     * page is locked into physical memory and flanked by guard
     * pages.  When bProtected is false (key too large for a
     * single page), components are individually Th8_AttemptMalloc'd.
     */
    Th8_ProtectedRegion protectedMem;

    /*
     * Raw public key blob for token computation.  For .NET
     * wrapped keys this is the full wrapped blob (12-byte header
     * + CAPI blob).  For raw CAPI keys it is the CAPI blob only
     * (PUBLICKEYSTRUC + RSAPUBKEY + modulus, little-endian).
     */
    unsigned char *zPubBlob; /* Public key blob for token. */
    size_t nPubBlob;

    /* Public component: modulus (n). */
    unsigned char *zModulus; /* Big-endian, nBits/8 bytes. */
    size_t nModulus;

    /* Private components (NULL if public-only). */
    unsigned char *zPrivExp; /* Private exponent (d), nBits/8 bytes. */
    size_t nPrivExp;

    unsigned char *zPrime1; /* First prime (p), nBits/16 bytes. */
    size_t nPrime1;
    unsigned char *zPrime2; /* Second prime (q), nBits/16 bytes. */
    size_t nPrime2;

    unsigned char *zExp1; /* d mod (p-1), nBits/16 bytes. */
    size_t nExp1;
    unsigned char *zExp2; /* d mod (q-1), nBits/16 bytes. */
    size_t nExp2;

    unsigned char *zCoeff; /* q^-1 mod p, nBits/16 bytes. */
    size_t nCoeff;
};


/*
 *----------------------------------------------------------------------
 *
 * th8ReadLE32 --
 *
 *	Read a 32-bit little-endian unsigned integer from a byte
 *	pointer.
 *
 * Why / How:
 *	CAPI blobs store all multi-byte integers in little-endian
 *	byte order regardless of host architecture.  This function
 *	performs a portable byte-wise read so the parser works
 *	correctly on both little-endian and big-endian hosts.
 *
 * Results:
 *	The 32-bit unsigned integer value at the given byte offset.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
th8ReadLE32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/*
 *----------------------------------------------------------------------
 *
 * th8WriteLE32 --
 *
 *	Write a 32-bit unsigned integer to a byte buffer in
 *	little-endian byte order.
 *
 * Why / How:
 *	Used to construct CAPI blob headers (e.g. the .NET
 *	PublicKeyBlob wrapper) in portable, endian-safe manner.
 *	Each byte is extracted by masking and shifting so the
 *	output is always little-endian regardless of host byte
 *	order.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes 4 bytes starting at p[0].
 *
 *----------------------------------------------------------------------
 */

static void
th8WriteLE32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}


/*
 *----------------------------------------------------------------------
 *
 * th8ReverseCopy --
 *
 *	Copy n bytes from src to dst in reverse order (little-endian
 *	to big-endian conversion).
 *
 * Why / How:
 *	RSA key components in CAPI blobs are stored in little-endian
 *	byte order, but mathematical libraries (libtommath, OpenSSL
 *	BIGNUMs) and the Th8_RsaKey structure use big-endian
 *	(network / mathematical) order.  This function performs the
 *	byte-reversal in a single pass with no temporary buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes n bytes to dst.
 *
 *----------------------------------------------------------------------
 */

static void
th8ReverseCopy(unsigned char *dst, const unsigned char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	dst[i] = src[n - 1 - i];
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8RsaAllocComponent --
 *
 *	Allocate and reverse-copy a key component from the CAPI
 *	blob (little-endian) to the Th8_RsaKey (big-endian).
 *
 * Why / How:
 *	Each RSA component (modulus, primes, exponents, coefficient)
 *	must be extracted from the CAPI blob and converted from
 *	little-endian to big-endian.  This helper combines the
 *	allocation and byte-reversal into one call so the caller
 *	does not need to repeat that pattern for every component.
 *
 * Results:
 *	Pointer to a newly allocated big-endian byte array, or
 *	NULL on allocation failure.  If pnOut is non-NULL, *pnOut
 *	is set to n.
 *
 * Side effects:
 *	Allocates n bytes via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

static unsigned char *
th8RsaAllocComponent(
    Th8_Interp *interp,
    const unsigned char *src,
    size_t n,
    size_t *pnOut)
{
    unsigned char *dst;

    dst = (unsigned char *)TH8_ALLOC(interp, n);
    if (!dst) return NULL;
    th8ReverseCopy(dst, src, n);
    if (pnOut) *pnOut = n;
    return dst;
}


/*
 *----------------------------------------------------------------------
 *
 * th8RsaParseCapi --
 *
 *	Parse a raw CAPI blob (PUBLICKEYBLOB or PRIVATEKEYBLOB)
 *	and populate a Th8_RsaKey structure.
 *
 *	Layout (all little-endian):
 *
 *	  Offset  Size  Field
 *	  ------  ----  -----
 *	  0       1     bType (0x06=public, 0x07=private)
 *	  1       1     bVersion (0x02)
 *	  2       2     reserved (0x0000)
 *	  4       4     aiKeyAlg (0x00002400 = CALG_RSA_SIGN)
 *	  8       4     magic ("RSA1" or "RSA2")
 *	  12      4     bitlen (key size in bits)
 *	  16      4     pubexp (public exponent)
 *	  20      N     modulus (bitlen/8 bytes, little-endian)
 *
 *	For PRIVATEKEYBLOB (bType=0x07), after the modulus:
 *	  20+N    N/2   prime1 (p)
 *	  20+3N/2 N/2   prime2 (q)
 *	  20+2N   N/2   exponent1 (dp = d mod p-1)
 *	  20+5N/2 N/2   exponent2 (dq = d mod q-1)
 *	  20+3N   N/2   coefficient (qInv = q^-1 mod p)
 *	  20+7N/2 N     privateExponent (d)
 *
 * Why / How:
 *	This is the core security-critical parser for Microsoft CAPI
 *	RSA blobs.  Every field is validated before use: blob type,
 *	version, reserved bytes, algorithm ID, RSA magic, key length
 *	(must be a power of two >= 1024), public exponent (must be
 *	odd and > 1), and total blob size (must be large enough for
 *	all declared components).  Only after all checks pass are
 *	the key components extracted and byte-reversed.  The function
 *	also constructs the .NET PublicKeyBlob needed for public key
 *	token computation, patching the bType and magic to public-key
 *	form regardless of the source blob type.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on malformed input.
 *
 * Side effects:
 *	Allocates memory for key components and the public key blob
 *	via Th8_AttemptMalloc.  On error, any partially allocated
 *	fields remain in pKey and must be freed by the caller.
 *
 *----------------------------------------------------------------------
 */

int
th8RsaParseCapi(
    Th8_Interp *interp,
    const unsigned char *z, /* CAPI blob data. */
    size_t n,   /* Blob size in bytes. */
    void *pKeyv)  /* OUT: populated key (Th8_RsaKey *). */
{
    Th8_RsaKey *pKey = (Th8_RsaKey *)pKeyv;
    unsigned int magic, bitlen;
    size_t nMod, nHalf;
    size_t off;

    /*
     * Minimum size: PUBLICKEYSTRUC (8) + RSAPUBKEY (12) = 20.
     */
    if (n < 20) {
	Th8_SetResultStatic(interp, "SNK: blob too small", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Validate PUBLICKEYSTRUC.
     */
    if (z[0] != CAPI_PUBLICKEYBLOB && z[0] != CAPI_PRIVATEKEYBLOB) {
	Th8_SetResultStatic(interp, "SNK: invalid blob type", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (z[1] != CAPI_BVERSION) {
	Th8_SetResultStatic(
	    interp, "SNK: unsupported blob version", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (z[2] != 0 || z[3] != 0) {
	Th8_SetResultStatic(
	    interp, "SNK: reserved field must be zero", TH8_NOLEN);
	return TH8_ERROR;
    }
    {
	unsigned int aiKeyAlg = th8ReadLE32(&z[4]);

	if (aiKeyAlg != CAPI_CALG_RSA_SIGN) {
	    Th8_SetResult(
	        interp,
	        aiKeyAlg == CAPI_CALG_RSA_KEYX
	            ? "SNK: key exchange keys (CALG_RSA_KEYX) "
	              "are not valid for signing"
	            : "SNK: not an RSA signing key",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /*
     * Parse RSAPUBKEY.
     */
    magic = th8ReadLE32(&z[8]);
    bitlen = th8ReadLE32(&z[12]);
    pKey->ePub = th8ReadLE32(&z[16]);
    pKey->nBits = (int)bitlen;

    if (magic != CAPI_RSA1_MAGIC && magic != CAPI_RSA2_MAGIC) {
	Th8_SetResultStatic(interp, "SNK: invalid RSA magic", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Validate that magic is consistent with bType.
     */
    if (z[0] == CAPI_PUBLICKEYBLOB && magic != CAPI_RSA1_MAGIC) {
	Th8_SetResultStatic(
	    interp, "SNK: public key blob must use RSA1 magic", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (z[0] == CAPI_PRIVATEKEYBLOB && magic != CAPI_RSA2_MAGIC) {
	Th8_SetResultStatic(
	    interp, "SNK: private key blob must use RSA2 magic", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Validate bitlen: must be a power of 2, at least 1024 bits.
     */
    if (bitlen < 1024 || (bitlen & (bitlen - 1)) != 0) {
	Th8_SetResultStatic(interp, "SNK: invalid key bit length", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Validate public exponent: must be odd and > 1.
     */
    if (pKey->ePub < 3 || (pKey->ePub & 1) == 0) {
	Th8_SetResultStatic(
	    interp, "SNK: invalid public exponent", TH8_NOLEN);
	return TH8_ERROR;
    }

    nMod = bitlen / 8;
    nHalf = bitlen / 16;

    /*
     * Validate blob size.
     */
    if (z[0] == CAPI_PUBLICKEYBLOB) {
	if (n < 20 + nMod) {
	    Th8_SetResultStatic(
	        interp, "SNK: public key blob truncated", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pKey->bHasPrivate = 0;
    } else {
	/*
	 * Private blob: modulus + p + q + dp + dq + qInv + d
	 * = N + 5*(N/2) + N = 9*N/2
	 */
	if (n < 20 + nMod + 5 * nHalf + nMod) {
	    Th8_SetResultStatic(
	        interp, "SNK: private key blob truncated", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pKey->bHasPrivate = 1;
    }

    /*
     * Build the .NET PublicKeyBlob for token computation.
     *
     * The .NET public key token (as computed by "sn -tp") is
     * the last 8 bytes (reversed) of the SHA-1 hash of the
     * full PublicKeyBlob structure:
     *
     *   Offset  Size  Field
     *   ------  ----  -----
     *   0       4     SigAlgID      (CALG_RSA_SIGN = 0x00002400)
     *   4       4     HashAlgID     (CALG_SHA      = 0x00008004)
     *   8       4     cbPublicKey   (size of inner CAPI blob)
     *   12      8     PUBLICKEYSTRUC (bType=0x06, bVersion=0x02,
     *                                 reserved=0, aiKeyAlg=0x2400)
     *   20      12    RSAPUBKEY     (magic="RSA1", bitlen, pubexp)
     *   32      N/8   Modulus       (little-endian)
     *
     * The inner CAPI blob is always in public-key form (bType=0x06,
     * magic="RSA1") regardless of whether the source was a private
     * key blob.
     */
    {
	size_t nCapi = 20 + nMod; /* inner CAPI public blob */
	size_t nFull = 12 + nCapi; /* .NET wrapper + inner */
	unsigned char *p;

	pKey->zPubBlob = (unsigned char *)TH8_ALLOC(interp, nFull);
	if (!pKey->zPubBlob) return TH8_ERROR;
	pKey->nPubBlob = nFull;

	p = pKey->zPubBlob;

	/* .NET PublicKeyBlob header (12 bytes). */
	th8WriteLE32(&p[0], DOTNET_SIG_ALG_RSA);    /* SigAlgID */
	th8WriteLE32(&p[4], DOTNET_HASH_ALG_SHA1);   /* HashAlgID */
	th8WriteLE32(&p[8], (unsigned int)nCapi);     /* cbPublicKey */

	/* Inner CAPI PUBLICKEYBLOB: copy from source, then patch. */
	Th8_Memcpy(interp, &p[12], z, nCapi);

	p[12] = CAPI_PUBLICKEYBLOB; /* bType = 0x06 */
	/* bVersion (p[13]) and reserved (p[14..15]) are already correct. */
	/* aiKeyAlg (p[16..19]) is already CALG_RSA_SIGN (validated above). */
	p[20] = 'R'; /* magic = "RSA1" */
	p[21] = 'S';
	p[22] = 'A';
	p[23] = '1';
    }

    /*
     * Extract modulus (big-endian copy).
     */
    off = 20;
    pKey->zModulus =
        th8RsaAllocComponent(interp, &z[off], nMod, &pKey->nModulus);
    if (!pKey->zModulus) return TH8_ERROR;
    off += nMod;

    /*
     * Extract private components if present.
     */
    if (pKey->bHasPrivate) {
	pKey->zPrime1 =
	    th8RsaAllocComponent(interp, &z[off], nHalf, &pKey->nPrime1);
	if (!pKey->zPrime1) return TH8_ERROR;
	off += nHalf;

	pKey->zPrime2 =
	    th8RsaAllocComponent(interp, &z[off], nHalf, &pKey->nPrime2);
	if (!pKey->zPrime2) return TH8_ERROR;
	off += nHalf;

	pKey->zExp1 =
	    th8RsaAllocComponent(interp, &z[off], nHalf, &pKey->nExp1);
	if (!pKey->zExp1) return TH8_ERROR;
	off += nHalf;

	pKey->zExp2 =
	    th8RsaAllocComponent(interp, &z[off], nHalf, &pKey->nExp2);
	if (!pKey->zExp2) return TH8_ERROR;
	off += nHalf;

	pKey->zCoeff =
	    th8RsaAllocComponent(interp, &z[off], nHalf, &pKey->nCoeff);
	if (!pKey->zCoeff) return TH8_ERROR;
	off += nHalf;

	pKey->zPrivExp =
	    th8RsaAllocComponent(interp, &z[off], nMod, &pKey->nPrivExp);
	if (!pKey->zPrivExp) return TH8_ERROR;
    }

    return TH8_OK;
}


/*
 *======================================================================
 *
 * PUBLIC API
 *
 *======================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyLoad --
 *
 *	Parse an SNK blob (raw bytes, NOT a file path) and return
 *	a newly allocated Th8_RsaKey.  Supports raw CAPI blobs
 *	and .NET Strong Name wrapped public keys.
 *
 *	The caller must free the result with Th8_RsaKeyFree.
 *
 * Why / How:
 *	This is the main entry point for loading RSA keys from
 *	Microsoft SNK data.  It auto-detects the format by
 *	inspecting the first 4 bytes: if they match the .NET
 *	SigAlgId constant (0x00002400), the blob is treated as a
 *	.NET Strong Name wrapped public key (12-byte header + CAPI
 *	blob); otherwise the first byte is checked for CAPI blob
 *	type (0x06 public or 0x07 private).  After parsing, the
 *	function attempts to migrate all key material into a
 *	single mlock'd protected page (with guard pages) to
 *	prevent the key from being swapped to disk or appearing
 *	in core dumps.  If the total key material exceeds one
 *	page, the individually-allocated buffers are retained.
 *
 * Results:
 *	TH8_OK on success with *ppKey set to the new key.
 *	TH8_ERROR on any failure (malformed blob, allocation
 *	failure, etc.) with the interpreter result set.
 *
 * Side effects:
 *	Allocates a Th8_RsaKey and its component buffers.  May
 *	allocate and mlock a protected memory region.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RsaKeyLoad(
    Th8_Interp *interp,
    const unsigned char *zData, /* SNK blob data. */
    size_t nData, /* Blob size in bytes. */
    Th8_RsaKey **ppKey) /* OUT: parsed key. */
{
    Th8_RsaKey *pKey;
    int rc;

    if (!interp) return TH8_ERROR;
    if (!zData || nData < 20 || !ppKey) {
	Th8_SetResultStatic(interp, "SNK: invalid arguments", TH8_NOLEN);
	return TH8_ERROR;
    }
    *ppKey = NULL;

    pKey = (Th8_RsaKey *)TH8_ALLOC(interp, sizeof(Th8_RsaKey));
    if (!pKey) return TH8_ERROR;
    Th8_Memset(interp, pKey, 0, sizeof(Th8_RsaKey));

    /*
     * Detect format: .NET wrapped public key has SigAlgId
     * (0x00002400) at offset 0 and a CAPI blob at offset 12.
     * A raw CAPI blob has bType (0x06 or 0x07) at offset 0.
     */

    if (nData >= 12 && th8ReadLE32(&zData[0]) == DOTNET_SIG_ALG_RSA) {
	/*
	 * .NET Strong Name wrapped format.
	 * Bytes 0-3: SigAlgId
	 * Bytes 4-7: HashAlgId
	 * Bytes 8-11: cbPublicKey (size of CAPI blob)
	 * Bytes 12+: CAPI PUBLICKEYBLOB
	 */
	unsigned int hashAlgId = th8ReadLE32(&zData[4]);
	unsigned int cbBlob = th8ReadLE32(&zData[8]);

	if (hashAlgId != DOTNET_HASH_ALG_SHA1) {
	    Th8_SetResultStatic(
	        interp,
	        "SNK: unsupported hash algorithm in "
	        ".NET wrapper (expected SHA-1)",
	        TH8_NOLEN);
	    Th8_Free(interp, pKey);
	    return TH8_ERROR;
	}
	if (cbBlob == 0 || 12 + cbBlob > nData) {
	    Th8_SetResultStatic(
	        interp, "SNK: .NET wrapper truncated", TH8_NOLEN);
	    Th8_Free(interp, pKey);
	    return TH8_ERROR;
	}
	rc = th8RsaParseCapi(interp, &zData[12], cbBlob, pKey);

	/*
	 * For .NET wrapped public keys, the token must be
	 * computed from the full wrapped blob (12-byte header
	 * + CAPI blob), not just the CAPI portion.  Replace
	 * the CAPI-only zPubBlob saved by th8RsaParseCapi.
	 */
	if (rc == TH8_OK) {
	    size_t nFull = 12 + cbBlob;

	    Th8_Free(interp, pKey->zPubBlob);
	    pKey->zPubBlob = (unsigned char *)TH8_ALLOC(interp, nFull);
	    if (!pKey->zPubBlob) {
		Th8_RsaKeyFree(interp, pKey);
		return TH8_ERROR;
	    }
	    Th8_Memcpy(interp, pKey->zPubBlob, zData, nFull);
	    pKey->nPubBlob = nFull;
	}
    } else if (
        zData[0] == CAPI_PUBLICKEYBLOB || zData[0] == CAPI_PRIVATEKEYBLOB) {
	/*
	 * Raw CAPI blob.
	 */
	rc = th8RsaParseCapi(interp, zData, nData, pKey);
    } else {
	Th8_SetResultStatic(
	    interp, "SNK: unrecognized key format", TH8_NOLEN);
	Th8_Free(interp, pKey);
	return TH8_ERROR;
    }

    if (rc != TH8_OK) {
	Th8_RsaKeyFree(interp, pKey);
	return rc;
    }

    /*
     * Migrate key material into a protected memory region
     * (mlock'd page with guard pages).  This prevents the key
     * from being swapped to disk or read from core dumps.
     *
     * If the total key material fits in a single page (minus
     * the canary), pack all components contiguously.  Otherwise
     * fall back to the unprotected Th8_AttemptMalloc'd buffers (which
     * are still zeroed on free).
     */

    {
	size_t nTotal = pKey->nPubBlob + pKey->nModulus + pKey->nPrivExp +
	                pKey->nPrime1 + pKey->nPrime2 + pKey->nExp1 +
	                pKey->nExp2 + pKey->nCoeff;

	if (th8ProtectedAlloc(interp, &pKey->protectedMem) == TH8_OK) {
	    size_t nAvail = th8ProtectedPageSize(&pKey->protectedMem) -
	                    th8ProtectedCanarySize();

	    if (nTotal <= nAvail) {
		/*
		 * Pack all components into the protected page.
		 * Copy each, update the pointer, free the
		 * original Th8_AttemptMalloc'd buffer.
		 */
		unsigned char *pDst = th8ProtectedData(&pKey->protectedMem);

#  define MIGRATE(field, nField)                                             \
      do {                                                                   \
	  if (pKey->field && pKey->nField > 0) {                             \
	      Th8_Memcpy(interp, pDst, pKey->field, pKey->nField);           \
	      Th8_Free(interp, pKey->field);                                 \
	      pKey->field = pDst;                                            \
	      pDst += pKey->nField;                                          \
	  }                                                                  \
      } while (0)

		MIGRATE(zPubBlob, nPubBlob);
		MIGRATE(zModulus, nModulus);
		MIGRATE(zPrivExp, nPrivExp);
		MIGRATE(zPrime1, nPrime1);
		MIGRATE(zPrime2, nPrime2);
		MIGRATE(zExp1, nExp1);
		MIGRATE(zExp2, nExp2);
		MIGRATE(zCoeff, nCoeff);
#  undef MIGRATE

		pKey->bProtected = 1;
	    } else {
		/*
		 * Key too large for one page.  Release the
		 * protected region and keep using Th8_AttemptMalloc.
		 */
		th8ProtectedFree(interp, &pKey->protectedMem);
	    }
	}
    }

    *ppKey = pKey;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyFree --
 *
 *	Free all memory associated with a parsed RSA key.  If the key
 *	material is in a protected region, th8ProtectedFree securely
 *	zeroes and releases the entire page in one operation.  Otherwise
 *	each component is individually freed and the struct is zeroed.
 *
 * Why / How:
 *	Private key material must never leak into swap, core dumps,
 *	or freed heap pages.  When the key resides in a protected
 *	region (mlock'd page), a single th8ProtectedFree call
 *	securely zeroes and releases the entire page.  When using
 *	ordinary heap buffers, each private component is explicitly
 *	zeroed before being freed, and public components are also
 *	zeroed to prevent forensic recovery.  Finally, the
 *	Th8_RsaKey struct itself is securely zeroed (clearing all
 *	pointers, sizes, and flags) before being freed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all memory associated with pKey.  The pointer is
 *	invalid after this call.
 *
 *----------------------------------------------------------------------
 */

void
Th8_RsaKeyFree(Th8_Interp *interp, Th8_RsaKey *pKey)
{
    if (!interp) return;
    if (!pKey) return;

    if (pKey->bProtected) {
	/*
	 * Key material is in a protected region (mlock'd page).
	 * th8ProtectedFree securely zeroes the entire page,
	 * unlocks it, and releases the guard pages -- all key
	 * components are destroyed in one operation.  No
	 * individual Th8_Free calls needed since the pointers
	 * point into the protected page.
	 */
	th8ProtectedFree(interp, &pKey->protectedMem);
    } else {
	/*
	 * Key material is in individual Th8_AttemptMalloc'd buffers.
	 * Securely zero private components before freeing.
	 */
	if (pKey->zPrivExp) {
	    Th8_Memset(interp, pKey->zPrivExp, 0, pKey->nPrivExp);
	    Th8_Free(interp, pKey->zPrivExp);
	}
	if (pKey->zPrime1) {
	    Th8_Memset(interp, pKey->zPrime1, 0, pKey->nPrime1);
	    Th8_Free(interp, pKey->zPrime1);
	}
	if (pKey->zPrime2) {
	    Th8_Memset(interp, pKey->zPrime2, 0, pKey->nPrime2);
	    Th8_Free(interp, pKey->zPrime2);
	}
	if (pKey->zExp1) {
	    Th8_Memset(interp, pKey->zExp1, 0, pKey->nExp1);
	    Th8_Free(interp, pKey->zExp1);
	}
	if (pKey->zExp2) {
	    Th8_Memset(interp, pKey->zExp2, 0, pKey->nExp2);
	    Th8_Free(interp, pKey->zExp2);
	}
	if (pKey->zCoeff) {
	    Th8_Memset(interp, pKey->zCoeff, 0, pKey->nCoeff);
	    Th8_Free(interp, pKey->zCoeff);
	}
	/* Public components: zero to prevent tampering forensics. */
	if (pKey->zModulus) {
	    Th8_Memset(interp, pKey->zModulus, 0, pKey->nModulus);
	    Th8_Free(interp, pKey->zModulus);
	}
	if (pKey->zPubBlob) {
	    Th8_Memset(interp, pKey->zPubBlob, 0, pKey->nPubBlob);
	    Th8_Free(interp, pKey->zPubBlob);
	}
    }

    /*
     * Zero the struct itself (clears pointers, sizes, flags).
     */
    Th8_SecureZero(interp, pKey, sizeof(Th8_RsaKey));
    Th8_Free(interp, pKey);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyBitLen --
 *
 *	Return the key size in bits (e.g. 1024, 2048, 4096, 16384).
 *
 * Why / How:
 *	Callers need the key size to validate minimum strength
 *	requirements (e.g. >= 2048 per NIST SP 800-131A) and to
 *	allocate correctly sized buffers for RSA operations.
 *	Returns 0 for a NULL key so callers can safely use the
 *	result without a separate NULL check.
 *
 * Results:
 *	The key's bit length, or 0 if pKey is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RsaKeyBitLen(const Th8_RsaKey *pKey)
{
    return pKey ? pKey->nBits : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyHasPrivate --
 *
 *	Test whether the parsed key includes private components
 *	(private exponent, primes, CRT parameters).
 *
 * Why / How:
 *	Signing operations require a private key; verification
 *	requires only a public key.  This accessor lets callers
 *	check key type before attempting an operation, producing
 *	clear error messages instead of cryptic OpenSSL failures.
 *	Returns 0 for a NULL key for safe default behavior.
 *
 * Results:
 *	Non-zero if the key has private components, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RsaKeyHasPrivate(const Th8_RsaKey *pKey)
{
    return pKey ? pKey->bHasPrivate : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyPubExp --
 *
 *	Return the RSA public exponent (e), typically 65537.
 *
 * Why / How:
 *	The public exponent is needed when constructing an OpenSSL
 *	EVP_PKEY from the parsed key components (in Th8_RsaVerify,
 *	Th8_RsaSign, and Th8_RsaExtractHash).  Returns 0 for a
 *	NULL key, which will be rejected by the RSA key builder.
 *
 * Results:
 *	The public exponent value, or 0 if pKey is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned int
Th8_RsaKeyPubExp(const Th8_RsaKey *pKey)
{
    return pKey ? pKey->ePub : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyModulus --
 *
 *	Return a pointer to the RSA modulus (n) in big-endian byte
 *	order, and optionally its length in bytes.
 *
 * Why / How:
 *	The modulus is the primary public-key component required for
 *	both verification and signing.  It is stored in big-endian
 *	order suitable for direct use with BN_bin2bn.  The length
 *	output parameter is optional so callers that already know
 *	the size (nBits/8) can pass NULL.
 *
 * Results:
 *	Pointer to the big-endian modulus, or NULL if pKey is NULL.
 *	If pn is non-NULL, *pn is set to the modulus size in bytes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const unsigned char *
Th8_RsaKeyModulus(const Th8_RsaKey *pKey, size_t *pn)
{
    if (!pKey) {
	if (pn) *pn = 0;
	return NULL;
    }
    if (pn) *pn = pKey->nModulus;
    return pKey->zModulus;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyPrivExp --
 *
 *	Return a pointer to the RSA private exponent (d) in
 *	big-endian byte order, and optionally its length.
 *
 * Why / How:
 *	The private exponent is needed for signing operations and
 *	for constructing the OpenSSL keypair in Th8_RsaSign.
 *	Returns NULL if the key is public-only, which callers must
 *	check before attempting to sign.  The data may reside in a
 *	protected (mlock'd) memory region.
 *
 * Results:
 *	Pointer to the big-endian private exponent, or NULL if the
 *	key is NULL or public-only.  If pn is non-NULL, *pn is set
 *	to the size in bytes (or 0 on NULL return).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const unsigned char *
Th8_RsaKeyPrivExp(const Th8_RsaKey *pKey, size_t *pn)
{
    if (!pKey || !pKey->bHasPrivate) {
	if (pn) *pn = 0;
	return NULL;
    }
    if (pn) *pn = pKey->nPrivExp;
    return pKey->zPrivExp;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyPrime1 --
 *
 *	Return a pointer to the first RSA prime factor (p) in
 *	big-endian byte order, and optionally its length.
 *
 * Why / How:
 *	The first prime is needed for CRT-optimized RSA signing.
 *	OpenSSL 3.0.x requires p and q (along with the derived CRT
 *	exponents) to be provided explicitly via OSSL_PARAM_BLD.
 *	Returns NULL if the key is public-only.
 *
 * Results:
 *	Pointer to the big-endian first prime, or NULL if the key
 *	is NULL or public-only.  If pn is non-NULL, *pn is set to
 *	the size in bytes (nBits/16).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const unsigned char *
Th8_RsaKeyPrime1(const Th8_RsaKey *pKey, size_t *pn)
{
    if (!pKey || !pKey->bHasPrivate) {
	if (pn) *pn = 0;
	return NULL;
    }
    if (pn) *pn = pKey->nPrime1;
    return pKey->zPrime1;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyPrime2 --
 *
 *	Return a pointer to the second RSA prime factor (q) in
 *	big-endian byte order, and optionally its length.
 *
 * Why / How:
 *	The second prime is needed for CRT-optimized RSA signing.
 *	Together with p, it enables OpenSSL to compute the CRT
 *	parameters (dmp1, dmq1, iqmp) required for efficient
 *	private-key operations.  Returns NULL if the key is
 *	public-only.
 *
 * Results:
 *	Pointer to the big-endian second prime, or NULL if the key
 *	is NULL or public-only.  If pn is non-NULL, *pn is set to
 *	the size in bytes (nBits/16).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const unsigned char *
Th8_RsaKeyPrime2(const Th8_RsaKey *pKey, size_t *pn)
{
    if (!pKey || !pKey->bHasPrivate) {
	if (pn) *pn = 0;
	return NULL;
    }
    if (pn) *pn = pKey->nPrime2;
    return pKey->zPrime2;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyToken --
 *
 *	Compute the .NET public key token.  This is the last 8 bytes
 *	of the SHA-1 hash of the CAPI public key blob, byte-reversed
 *	(per the official Microsoft algorithm).
 *
 * Why / How:
 *	The public key token is a compact 8-byte identifier used by
 *	.NET and Eagle to reference an assembly's signing key without
 *	embedding the full public key.  The algorithm (defined by
 *	Microsoft and implemented by "sn -tp") computes SHA-1 over
 *	the full PublicKeyBlob structure, then takes the last 8
 *	bytes of the hash in reverse order.  This function uses the
 *	precomputed zPubBlob stored during key loading to ensure the
 *	token matches what "sn -tp" would produce.
 *
 * Results:
 *	TH8_OK on success with 8 bytes written to zOut.
 *	TH8_ERROR if the key has no public blob.
 *
 * Side effects:
 *	Writes 8 bytes to zOut.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RsaKeyToken(
    Th8_Interp *interp,
    const Th8_RsaKey *pKey,
    unsigned char zOut[8])
{
    unsigned char hash[SHA_DIGEST_LENGTH]; /* 20 bytes */
    int i;

    if (!interp) return TH8_ERROR;
    if (!pKey || !pKey->zPubBlob) {
	if (interp) {
	    Th8_SetResultStatic(
	        interp, "RSA: no public key blob for token", TH8_NOLEN);
	}
	return TH8_ERROR;
    }

    SHA1(pKey->zPubBlob, pKey->nPubBlob, hash);

    /*
     * Token = last 8 bytes of the SHA-1 hash, reversed.
     */
    for (i = 0; i < 8; i++) {
	zOut[i] = hash[SHA_DIGEST_LENGTH - 1 - i];
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaKeyTokenHex --
 *
 *	Like Th8_RsaKeyToken but returns the token as a 16-character
 *	lowercase hex string.
 *
 * Why / How:
 *	Callers often need the token in hex form for display,
 *	comparison with configuration values, or embedding in XML
 *	certificate files.  This wrapper calls Th8_RsaKeyToken
 *	and formats the 8 raw bytes as 16 lowercase hex characters
 *	with a NUL terminator, matching the format produced by
 *	"sn -tp" and used in .NET assembly references.
 *
 * Results:
 *	TH8_OK on success with 16 hex chars + NUL in zOut.
 *	TH8_ERROR if the underlying token computation fails.
 *
 * Side effects:
 *	Writes 17 bytes to zOut (16 hex chars + NUL).
 *
 *----------------------------------------------------------------------
 */

int
Th8_RsaKeyTokenHex(Th8_Interp *interp, const Th8_RsaKey *pKey, char zOut[17])
{
    unsigned char token[8];
    int rc;
    int i;
    static const char hex[] = "0123456789abcdef";

    if (!interp) return TH8_ERROR;
    rc = Th8_RsaKeyToken(interp, pKey, token);
    if (rc != TH8_OK) return rc;

    for (i = 0; i < 8; i++) {
	zOut[i * 2] = hex[(token[i] >> 4) & 0x0F];
	zOut[i * 2 + 1] = hex[token[i] & 0x0F];
    }
    zOut[16] = '\0';

    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaVerify --
 *
 *	Verify an RSA signature over arbitrary data using a public key.
 *	Uses PKCS#1 v1.5 padding with SHA-512.  The signature is
 *	expected to have been produced by signing the SHA-512 digest
 *	of zData/nData with the private counterpart of pKey.
 *
 * Why / How:
 *	This function is the trust anchor for Harpy certificate
 *	validation.  It reconstructs an OpenSSL EVP_PKEY from the
 *	parsed Th8_RsaKey components (modulus + exponent) using the
 *	non-deprecated OpenSSL 3.x EVP_PKEY_fromdata API, then
 *	performs a single-pass DigestVerify (SHA-512 + PKCS#1 v1.5).
 *	A pre-verification sanity check rejects signatures larger
 *	than the modulus (plus a small margin), preventing allocation
 *	bombs from malicious input.  All OpenSSL objects are freed
 *	on every code path (success, error, and OOM).
 *
 * Results:
 *	TH8_OK if the signature is valid, TH8_ERROR otherwise.
 *	On error the interpreter result contains a diagnostic.
 *
 * Side effects:
 *	Allocates and frees OpenSSL objects internally.  No
 *	persistent state is modified.
 *
 *----------------------------------------------------------------------
 */

#  include <openssl/evp.h>
#  include <openssl/rsa.h>
#  include <openssl/param_build.h>
#  include <openssl/core_names.h>

/*
 * Maximum RSA signature size in bytes.  Accommodates RSA keys up to
 * 65536 bits (8192-byte modulus) plus a 64-byte margin for encoding.
 * Override at compile time with -DTH8_RSA_MAX_SIG_BYTES=N.
 */

#  ifndef TH8_RSA_MAX_SIG_BYTES
#    define TH8_RSA_MAX_SIG_BYTES 16448 /* 131072-bit key + 64 margin */
#  endif

int
Th8_RsaVerify(
    Th8_Interp *interp, /* Interpreter (for error msgs). */
    const Th8_RsaKey *pKey, /* Public key. */
    const unsigned char *zData, /* Data that was signed. */
    size_t nData, /* Data length in bytes. */
    const unsigned char *zSig, /* Signature bytes. */
    size_t nSig) /* Signature length in bytes. */
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *kctx = NULL;
    EVP_MD_CTX *mdctx = NULL;
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *params = NULL;
    BIGNUM *bn_n = NULL;
    BIGNUM *bn_e = NULL;
    size_t nModulus;
    const unsigned char *zModulus;
    int ok = 0;

    if (!interp) return TH8_ERROR;
    zModulus = Th8_RsaKeyModulus(pKey, &nModulus);
    if (!zModulus || nModulus == 0) {
	Th8_SetResultStatic(
	    interp, "RSA verify: no modulus in key", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Sanity check: the signature size must not exceed the
     * modulus size (plus a small margin for encoding overhead).
     * This prevents allocating excessive memory before OpenSSL
     * rejects the signature.
     */
    if (nSig > nModulus + 64 || nSig > TH8_RSA_MAX_SIG_BYTES) {
	Th8_SetResultStatic(
	    interp, "RSA verify: signature too large", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Build the RSA public key using EVP_PKEY_fromdata
     * (OpenSSL 3.x non-deprecated API).
     */

    bn_n = BN_bin2bn(zModulus, (int)nModulus, NULL);
    if (!bn_n) goto oom;

    bn_e = BN_new();
    if (!bn_e) goto oom;
    BN_set_word(bn_e, (unsigned long)Th8_RsaKeyPubExp(pKey));

    bld = OSSL_PARAM_BLD_new();
    if (!bld) goto oom;

    if (!OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e)) {
	goto oom;
    }

    params = OSSL_PARAM_BLD_to_param(bld);
    if (!params) goto oom;

    kctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    if (!kctx) goto oom;

    if (EVP_PKEY_fromdata_init(kctx) != 1 ||
        EVP_PKEY_fromdata(kctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) != 1) {
	Th8_SetResultStatic(
	    interp, "RSA verify: key construction failed", TH8_NOLEN);
	goto cleanup;
    }

    /*
     * Verify: SHA-512 digest + PKCS#1 v1.5 padding.
     */

    mdctx = EVP_MD_CTX_new();
    if (!mdctx) goto oom;

    if (EVP_DigestVerifyInit(mdctx, NULL, EVP_sha512(), NULL, pkey) != 1) {
	Th8_SetResultStatic(interp, "RSA verify: init failed", TH8_NOLEN);
	goto cleanup;
    }
    if (EVP_DigestVerifyUpdate(mdctx, zData, nData) != 1) {
	Th8_SetResultStatic(interp, "RSA verify: update failed", TH8_NOLEN);
	goto cleanup;
    }
    if (EVP_DigestVerifyFinal(mdctx, zSig, nSig) == 1) {
	ok = 1;
    } else {
	Th8_SetResultStatic(
	    interp, "RSA verify: signature mismatch", TH8_NOLEN);
    }
    goto cleanup;

oom:
    Th8_SetResultStatic(interp, "RSA verify: out of memory", TH8_NOLEN);

cleanup:
    if (mdctx) EVP_MD_CTX_free(mdctx);
    if (pkey) EVP_PKEY_free(pkey);
    if (kctx) EVP_PKEY_CTX_free(kctx);
    if (params) OSSL_PARAM_free(params);
    if (bld) OSSL_PARAM_BLD_free(bld);
    if (bn_n) BN_free(bn_n);
    if (bn_e) BN_free(bn_e);

    return ok ? TH8_OK : TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaSign --
 *
 *	Sign data with RSA PKCS#1 v1.5 using SHA-512.  The key
 *	MUST contain a private key (`Th8_RsaKeyHasPrivate` returns
 *	non-zero).
 *
 *	On success, *ppSig receives a buffer allocated via
 *	Th8_AttemptMalloc containing the raw signature, and *pnSig
 *	receives its byte length.  The caller must free *ppSig
 *	with Th8_Free.
 *
 *	Security hardening:
 *
 *	  - Rejects keys shorter than 2048 bits (NIST minimum).
 *	  - Validates that the private key components (d, p, q)
 *	    are present and non-empty.
 *	  - After signing, performs a verification round-trip with
 *	    the public key to detect signing faults (Bellcore
 *	    attack mitigation for CRT-based implementations).
 *	  - All sensitive OpenSSL objects are freed via the
 *	    appropriate _free functions on all code paths.
 *	  - The intermediate signature buffer is securely zeroed
 *	    on failure paths.
 *
 * Why / How:
 *	Constructs a full RSA keypair (n, e, d, p, q plus CRT
 *	parameters dmp1, dmq1, iqmp) via OpenSSL 3.x
 *	EVP_PKEY_fromdata, then performs a two-pass DigestSign
 *	(first call queries output size, second produces the
 *	signature).  CRT exponents are computed explicitly because
 *	OpenSSL 3.0.2 cannot derive them from (n, e, d, p, q)
 *	alone.  After signing, a full verification round-trip via
 *	Th8_RsaVerify detects Bellcore-style CRT fault attacks: if
 *	a hardware fault corrupts a single CRT half, the resulting
 *	signature leaks a prime factor, so the self-check prevents
 *	releasing a compromised signature.  All sensitive OpenSSL
 *	BIGNUMs are freed with BN_clear_free (not BN_free) to
 *	securely zero their memory.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on any failure.
 *
 * Side effects:
 *	Allocates the signature buffer via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RsaSign(
    Th8_Interp *interp, /* Interpreter (for error msgs / alloc). */
    const Th8_RsaKey *pKey, /* Private key. */
    const unsigned char *zData, /* Data to sign. */
    size_t nData, /* Data length in bytes. */
    unsigned char **ppSig, /* OUT: signature (caller frees). */
    size_t *pnSig) /* OUT: signature length. */
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *kctx = NULL;
    EVP_MD_CTX *mdctx = NULL;
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *params = NULL;
    BIGNUM *bn_n = NULL;
    BIGNUM *bn_e = NULL;
    BIGNUM *bn_d = NULL;
    BIGNUM *bn_p = NULL;
    BIGNUM *bn_q = NULL;
    BIGNUM *bn_dmp1 = NULL; /* d mod (p-1) */
    BIGNUM *bn_dmq1 = NULL; /* d mod (q-1) */
    BIGNUM *bn_iqmp = NULL; /* q^(-1) mod p */
    size_t nModulus, nPrivExp, nPrime1, nPrime2;
    const unsigned char *zModulus, *zPrivExp, *zPrime1, *zPrime2;
    unsigned char *zSig = NULL;
    size_t nSig = 0;
    int ok = 0;

    if (!interp) return TH8_ERROR;
    *ppSig = NULL;
    *pnSig = 0;

    /*
     * Validate: key must have private components.
     */

    if (!Th8_RsaKeyHasPrivate(pKey)) {
	Th8_SetResultStatic(
	    interp, "RSA sign: key does not contain a private key",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Validate: minimum key size (2048 bits per NIST SP 800-131A).
     */

    if (Th8_RsaKeyBitLen(pKey) < 2048) {
	Th8_SetResultStatic(
	    interp, "RSA sign: key is shorter than 2048 bits", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Extract all key components.
     */

    zModulus = Th8_RsaKeyModulus(pKey, &nModulus);
    zPrivExp = Th8_RsaKeyPrivExp(pKey, &nPrivExp);
    zPrime1 = Th8_RsaKeyPrime1(pKey, &nPrime1);
    zPrime2 = Th8_RsaKeyPrime2(pKey, &nPrime2);

    if (!zModulus || nModulus == 0 || !zPrivExp || nPrivExp == 0 ||
        !zPrime1 || nPrime1 == 0 || !zPrime2 || nPrime2 == 0) {
	Th8_SetResultStatic(
	    interp, "RSA sign: incomplete private key components", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Build the RSA private key using EVP_PKEY_fromdata
     * with KEYPAIR selection (OpenSSL 3.x non-deprecated API).
     * Include n, e, d, p, q for full CRT optimization.
     */

    bn_n = BN_bin2bn(zModulus, (int)nModulus, NULL);
    if (!bn_n) goto oom;

    bn_e = BN_new();
    if (!bn_e) goto oom;
    BN_set_word(bn_e, (unsigned long)Th8_RsaKeyPubExp(pKey));

    bn_d = BN_bin2bn(zPrivExp, (int)nPrivExp, NULL);
    if (!bn_d) goto oom;

    bn_p = BN_bin2bn(zPrime1, (int)nPrime1, NULL);
    if (!bn_p) goto oom;

    bn_q = BN_bin2bn(zPrime2, (int)nPrime2, NULL);
    if (!bn_q) goto oom;

    /*
     * Compute CRT parameters required by OpenSSL 3.0.x.
     * Later versions can derive them, but 3.0.2 cannot.
     *   dmp1 = d mod (p-1)
     *   dmq1 = d mod (q-1)
     *   iqmp = q^(-1) mod p
     */
    {
	BN_CTX *bnctx = BN_CTX_new();
	BIGNUM *pm1 = BN_new();
	BIGNUM *qm1 = BN_new();

	bn_dmp1 = BN_new();
	bn_dmq1 = BN_new();
	bn_iqmp = BN_new();

	if (!bnctx || !pm1 || !qm1 || !bn_dmp1 || !bn_dmq1 || !bn_iqmp) {
	    BN_free(pm1);
	    BN_free(qm1);
	    BN_CTX_free(bnctx);
	    goto oom;
	}

	/* pm1 = p - 1 */
	BN_copy(pm1, bn_p);
	BN_sub_word(pm1, 1);

	/* qm1 = q - 1 */
	BN_copy(qm1, bn_q);
	BN_sub_word(qm1, 1);

	/* dmp1 = d mod (p-1) */
	BN_mod(bn_dmp1, bn_d, pm1, bnctx);

	/* dmq1 = d mod (q-1) */
	BN_mod(bn_dmq1, bn_d, qm1, bnctx);

	/* iqmp = q^(-1) mod p */
	BN_mod_inverse(bn_iqmp, bn_q, bn_p, bnctx);

	BN_free(pm1);
	BN_free(qm1);
	BN_CTX_free(bnctx);
    }

    bld = OSSL_PARAM_BLD_new();
    if (!bld) goto oom;

    if (!OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_D, bn_d) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR1, bn_p) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_FACTOR2, bn_q) ||
        !OSSL_PARAM_BLD_push_BN(
            bld, OSSL_PKEY_PARAM_RSA_EXPONENT1, bn_dmp1) ||
        !OSSL_PARAM_BLD_push_BN(
            bld, OSSL_PKEY_PARAM_RSA_EXPONENT2, bn_dmq1) ||
        !OSSL_PARAM_BLD_push_BN(
            bld, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, bn_iqmp)) {
	goto oom;
    }

    params = OSSL_PARAM_BLD_to_param(bld);
    if (!params) goto oom;

    kctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    if (!kctx) goto oom;

    if (EVP_PKEY_fromdata_init(kctx) != 1 ||
        EVP_PKEY_fromdata(kctx, &pkey, EVP_PKEY_KEYPAIR, params) != 1) {
	Th8_SetResultStatic(
	    interp, "RSA sign: private key construction failed", TH8_NOLEN);
	goto cleanup;
    }

    /*
     * Sign: SHA-512 digest + PKCS#1 v1.5 padding.
     *
     * Two-pass: first call with NULL output to get the required
     * signature length, then allocate and sign.
     */

    mdctx = EVP_MD_CTX_new();
    if (!mdctx) goto oom;

    if (EVP_DigestSignInit(mdctx, NULL, EVP_sha512(), NULL, pkey) != 1) {
	Th8_SetResultStatic(
	    interp, "RSA sign: DigestSignInit failed", TH8_NOLEN);
	goto cleanup;
    }
    if (EVP_DigestSignUpdate(mdctx, zData, nData) != 1) {
	Th8_SetResultStatic(
	    interp, "RSA sign: DigestSignUpdate failed", TH8_NOLEN);
	goto cleanup;
    }

    /*
     * Query required signature length.
     */

    if (EVP_DigestSignFinal(mdctx, NULL, &nSig) != 1 || nSig == 0) {
	Th8_SetResultStatic(
	    interp, "RSA sign: cannot determine signature size", TH8_NOLEN);
	goto cleanup;
    }

    /*
     * Sanity: signature size should equal the modulus size.
     * Reject absurd values to prevent allocation bombs.
     */

    if (nSig > nModulus + 64 || nSig > TH8_RSA_MAX_SIG_BYTES) {
	Th8_SetResultStatic(
	    interp, "RSA sign: unreasonable signature size", TH8_NOLEN);
	goto cleanup;
    }

    zSig = (unsigned char *)TH8_ALLOC(interp, nSig);
    if (!zSig) goto oom;

    if (EVP_DigestSignFinal(mdctx, zSig, &nSig) != 1) {
	Th8_SetResultStatic(
	    interp, "RSA sign: DigestSignFinal failed", TH8_NOLEN);
	goto cleanup;
    }

    /*
     * Bellcore attack mitigation: verify the signature we just
     * produced using the public key.  If the CRT computation
     * was corrupted by a fault (voltage glitch, cosmic ray,
     * etc.), the signature will not verify.  This detects the
     * attack at the cost of one extra public-key operation.
     */

    if (Th8_RsaVerify(interp, pKey, zData, nData, zSig, nSig) != TH8_OK) {
	/*
	 * Signature failed self-verification.  This indicates
	 * a signing fault -- do NOT release the signature.
	 * Securely zero it to prevent leaking key material.
	 */
	Th8_Memset(interp, zSig, 0, nSig);
	Th8_Free(interp, zSig);
	zSig = NULL;
	Th8_SetResultStatic(
	    interp,
	    "RSA sign: self-verification failed "
	    "(possible signing fault)",
	    TH8_NOLEN);
	goto cleanup;
    }

    /*
     * Success: transfer signature to caller.
     */

    *ppSig = zSig;
    *pnSig = nSig;
    zSig = NULL; /* Prevent cleanup from freeing it. */
    ok = 1;
    goto cleanup;

oom:
    Th8_SetResultStatic(interp, "RSA sign: out of memory", TH8_NOLEN);

cleanup:
    if (zSig) {
	Th8_Memset(interp, zSig, 0, nSig);
	Th8_Free(interp, zSig);
    }
    if (mdctx) EVP_MD_CTX_free(mdctx);
    if (pkey) EVP_PKEY_free(pkey);
    if (kctx) EVP_PKEY_CTX_free(kctx);
    if (params) OSSL_PARAM_free(params);
    if (bld) OSSL_PARAM_BLD_free(bld);
    if (bn_iqmp) BN_clear_free(bn_iqmp);
    if (bn_dmq1) BN_clear_free(bn_dmq1);
    if (bn_dmp1) BN_clear_free(bn_dmp1);
    if (bn_q) BN_clear_free(bn_q);
    if (bn_p) BN_clear_free(bn_p);
    if (bn_d) BN_clear_free(bn_d);
    if (bn_n) BN_free(bn_n);
    if (bn_e) BN_free(bn_e);

    return ok ? TH8_OK : TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Sha512Hex --
 *
 *	Compute the SHA-512 hash of zData/nData and write the
 *	128-character lowercase hex digest to zOut (must be at
 *	least 129 bytes).
 *
 * Why / How:
 *	Harpy certificate verification requires comparing the
 *	SHA-512 hash of the signed content against the hash
 *	extracted from the RSA signature.  This utility computes
 *	that hash using the OpenSSL EVP digest API and formats it
 *	as a 128-character lowercase hex string for direct string
 *	comparison with the output of Th8_RsaExtractHash.
 *
 * Results:
 *	TH8_OK on success with 128 hex chars + NUL in zOut.
 *	TH8_ERROR if the EVP context allocation fails.
 *
 * Side effects:
 *	Writes 129 bytes to zOut (128 hex chars + NUL).
 *
 *----------------------------------------------------------------------
 */

int
Th8_Sha512Hex(
    Th8_Interp *interp,
    const unsigned char *zData,
    size_t nData,
    char zOut[129])
{
    unsigned char md[64];
    unsigned int mdLen = 0;
    EVP_MD_CTX *ctx;
    int i;
    static const char hex[] = "0123456789abcdef";

    if (!interp) return TH8_ERROR;
    (void)interp;

    ctx = EVP_MD_CTX_new();
    if (!ctx) {
	zOut[0] = '\0';
	return TH8_ERROR;
    }
    EVP_DigestInit_ex(ctx, EVP_sha512(), NULL);
    EVP_DigestUpdate(ctx, zData, nData);
    EVP_DigestFinal_ex(ctx, md, &mdLen);
    EVP_MD_CTX_free(ctx);

    for (i = 0; i < 64; i++) {
	zOut[i * 2] = hex[(md[i] >> 4) & 0x0F];
	zOut[i * 2 + 1] = hex[md[i] & 0x0F];
    }
    zOut[128] = '\0';
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RsaExtractHash --
 *
 *	Recover the SHA-512 hash embedded in an RSA PKCS#1 v1.5
 *	signature by performing the public-key operation and
 *	stripping the DigestInfo ASN.1 wrapper.  Writes the
 *	128-character lowercase hex hash to zOut (at least 129
 *	bytes).  Returns TH8_OK on success, TH8_ERROR if the
 *	signature cannot be decoded.
 *
 * Why / How:
 *	Harpy certificates carry an RSA PKCS#1 v1.5 signature over
 *	the SHA-512 hash of the signed content.  To display or
 *	compare the expected hash (without re-hashing the content),
 *	this function uses EVP_PKEY_verify_recover to perform the
 *	raw RSA public-key operation and strip the PKCS#1 v1.5
 *	padding, yielding the DER-encoded DigestInfo.  The DigestInfo
 *	prefix is then validated against the known SHA-512 ASN.1
 *	AlgorithmIdentifier, and the trailing 64-byte hash is
 *	extracted and formatted as 128 lowercase hex characters.
 *	This is a security-sensitive operation: the DigestInfo
 *	prefix check prevents algorithm confusion attacks where a
 *	different hash algorithm's prefix might be substituted.
 *
 * Results:
 *	TH8_OK on success with 128 hex chars + NUL in zOut.
 *	TH8_ERROR if the signature cannot be decoded or does not
 *	contain a SHA-512 DigestInfo.
 *
 * Side effects:
 *	Allocates temporary buffers for the recovered plaintext.
 *	All OpenSSL objects are freed on every code path.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RsaExtractHash(
    Th8_Interp *interp,
    const Th8_RsaKey *pKey,
    const unsigned char *zSig,
    size_t nSig,
    char zOut[129])
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *kctx = NULL;
    EVP_PKEY_CTX *vctx = NULL;
    OSSL_PARAM_BLD *bld = NULL;
    OSSL_PARAM *params = NULL;
    BIGNUM *bn_n = NULL;
    BIGNUM *bn_e = NULL;
    unsigned char *recovered = NULL;
    size_t recoveredLen = 0;
    size_t nModulus;
    const unsigned char *zModulus;
    int rc = TH8_ERROR;
    static const char hex[] = "0123456789abcdef";

    /*
     * SHA-512 DigestInfo prefix (DER-encoded AlgorithmIdentifier):
     *   SEQUENCE { SEQUENCE { OID sha-512, NULL }, OCTET STRING(64) }
     *
     * Total prefix: 19 bytes.  Followed by 64 bytes of hash = 83.
     */

    static const unsigned char sha512Prefix[] = {0x30, 0x51, 0x30, 0x0d, 0x06,
                                                 0x09, 0x60, 0x86, 0x48, 0x01,
                                                 0x65, 0x03, 0x04, 0x02, 0x03,
                                                 0x05, 0x00, 0x04, 0x40};

    static const size_t sha512PrefixLen = sizeof(sha512Prefix);

    if (!interp) return TH8_ERROR;

    zModulus = Th8_RsaKeyModulus(pKey, &nModulus);
    if (!zModulus || nModulus == 0) {
	Th8_SetResultStatic(interp, "extract hash: no modulus", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Build EVP_PKEY from modulus + exponent. */
    bn_n = BN_bin2bn(zModulus, (int)nModulus, NULL);
    if (!bn_n) goto cleanup;
    bn_e = BN_new();
    if (!bn_e) goto cleanup;
    BN_set_word(bn_e, (unsigned long)Th8_RsaKeyPubExp(pKey));

    bld = OSSL_PARAM_BLD_new();
    if (!bld) goto cleanup;
    if (!OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e)) {
	goto cleanup;
    }
    params = OSSL_PARAM_BLD_to_param(bld);
    if (!params) goto cleanup;

    kctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    if (!kctx) goto cleanup;
    if (EVP_PKEY_fromdata_init(kctx) != 1 ||
        EVP_PKEY_fromdata(kctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) != 1) {
	goto cleanup;
    }

    /*
     * EVP_PKEY_verify_recover: performs the RSA public-key
     * operation and strips PKCS#1 v1.5 padding, returning
     * the raw DigestInfo bytes.
     */

    vctx = EVP_PKEY_CTX_new(pkey, NULL);
    if (!vctx) goto cleanup;
    if (EVP_PKEY_verify_recover_init(vctx) != 1) goto cleanup;
    if (EVP_PKEY_CTX_set_rsa_padding(vctx, RSA_PKCS1_PADDING) != 1)
	goto cleanup;

    /* Query output size. */
    if (EVP_PKEY_verify_recover(vctx, NULL, &recoveredLen, zSig, nSig) != 1)
	goto cleanup;

    recovered = (unsigned char *)TH8_ALLOC(interp, recoveredLen);
    if (!recovered) goto cleanup;

    if (EVP_PKEY_verify_recover(vctx, recovered, &recoveredLen, zSig, nSig) !=
        1) {
	Th8_SetResultStatic(
	    interp, "extract hash: RSA recover failed", TH8_NOLEN);
	goto cleanup;
    }

    /*
     * Verify DigestInfo prefix matches SHA-512 and extract
     * the 64-byte hash.
     */

    if (recoveredLen != sha512PrefixLen + 64 ||
        Th8_Memcmp(interp, recovered, sha512Prefix, sha512PrefixLen) != 0) {
	Th8_SetResultStatic(
	    interp, "extract hash: unexpected DigestInfo format", TH8_NOLEN);
	goto cleanup;
    }

    {
	const unsigned char *hash = &recovered[sha512PrefixLen];
	int i;

	for (i = 0; i < 64; i++) {
	    zOut[i * 2] = hex[(hash[i] >> 4) & 0x0F];
	    zOut[i * 2 + 1] = hex[hash[i] & 0x0F];
	}
	zOut[128] = '\0';
    }
    rc = TH8_OK;

cleanup:
    Th8_Free(interp, recovered);
    if (vctx) EVP_PKEY_CTX_free(vctx);
    if (pkey) EVP_PKEY_free(pkey);
    if (kctx) EVP_PKEY_CTX_free(kctx);
    if (params) OSSL_PARAM_free(params);
    if (bld) OSSL_PARAM_BLD_free(bld);
    if (bn_n) BN_free(bn_n);
    if (bn_e) BN_free(bn_e);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8TestRsaKeyClearPubBlob / th8TestRsaKeyRestorePubBlob --
 *
 *	Test-only helpers exposed via the internal stubs table for
 *	driving the C2-Pair (F,T) vector at Th8_RsaKeyToken's L1117
 *	`if (!pKey || !pKey->zPubBlob)`.  th8_snk.c is the only
 *	translation unit that can see the Th8_RsaKey layout, so
 *	testlib cannot directly null/restore zPubBlob.
 *
 *	Pattern of use: save -> null -> call Th8_RsaKeyToken
 *	(drives F,T) -> restore.  The save/restore protects the
 *	caller's key from corruption.
 *
 *----------------------------------------------------------------------
 */

void
th8TestRsaKeyClearPubBlob(
    Th8_RsaKey *pKey,
    unsigned char **ppSavedBlob,
    size_t *pSavedN)
{
    if (!pKey) return;
    if (!ppSavedBlob) return;
    if (!pSavedN) return;
    *ppSavedBlob = pKey->zPubBlob;
    *pSavedN = pKey->nPubBlob;
    pKey->zPubBlob = NULL;
    pKey->nPubBlob = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8TestRsaKeyRestorePubBlob --
 *
 *	Test-support helper that restores the public-key blob
 *	pointer and length on a `Th8_RsaKey` without touching
 *	any other field.  Paired with the matching capture
 *	helper above: a driver captures the original
 *	`(zPubBlob, nPubBlob)` pair, perturbs the key into a
 *	failing-input state to exercise an error arm, then
 *	calls this restorer so the per-interp test invariants
 *	hold for the next vector.
 *
 *	Exposed via the internal-stubs table (not the public
 *	API) -- production callers have no business
 *	overwriting RSA key fields after construction.
 *
 *	Gated on `TH8_ENABLE_CRYPTOGRAPHY`.
 *
 * Parameters:
 *	pKey       -- key to mutate (NULL is a no-op).
 *	pSavedBlob -- pointer to restore into `pKey->zPubBlob`.
 *	nSaved     -- length to restore into `pKey->nPubBlob`.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Mutates `pKey->zPubBlob` and `pKey->nPubBlob`.
 *
 *----------------------------------------------------------------------
 */
void
th8TestRsaKeyRestorePubBlob(
    Th8_RsaKey *pKey,
    unsigned char *pSavedBlob,
    size_t nSaved)
{
    if (!pKey) return;
    pKey->zPubBlob = pSavedBlob;
    pKey->nPubBlob = nSaved;
}


#endif /* TH8_ENABLE_CRYPTOGRAPHY */
