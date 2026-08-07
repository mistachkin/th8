/*
 * th8_curl.c -- libcurl-based data retrieval platform for TH8.
 *
 * Provides an xGetData callback that fetches data from a URI via
 * libcurl.  This enables [source] to load scripts from HTTPS URLs
 * and [file exists] to probe remote resources.
 *
 * Only xGetData is provided; all other callbacks are NULL.
 * Merge with other platforms (nullio, libc, posix) for the complete
 * set.
 *
 * Compile-time gate: TH8_ENABLE_LIBCURL
 *
 * Usage:
 *   Th8_Platform plat = *Th8_GetNullIoPlatform();
 *   Th8_MergePlatform(&plat, Th8_GetCurlPlatform());
 *   Th8_MergePlatform(&plat, Th8_GetLibcPlatform());
 *   // Now [source https://example.com/script.tcl] works
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"    /* For TH8_PLATFORM_CURL auto-detection. */
#include "th8_int.h"

#if defined(TH8_PLATFORM_CURL)

#  include <curl/curl.h>

#  if defined(TH8_ENABLE_UNBOUND)
#    include <unbound.h>
#    include <string.h>
#    if defined(_WIN32) || defined(WIN32)
#      include <ws2tcpip.h>    /* inet_ntop on Windows */
#    else
#      include <arpa/inet.h>   /* inet_ntop on POSIX */
#    endif
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8CurlIsValidUri --
 *
 *	Sanity-check a URI before passing it to libcurl.
 *	Only HTTPS and HTTP schemes are allowed.  Reject
 *	file://, ftp://, and other schemes that could access
 *	local resources or unexpected protocols.
 *
 * Why / How:
 *	Performs a byte-by-byte prefix check for "https://" (8 chars)
 *	or "http://" (7 chars).  Rejects anything else to prevent
 *	SSRF attacks via file://, ftp://, dict://, or other libcurl-
 *	supported schemes that could access local resources.
 *
 * Results:
 *	1 if the URI is valid (HTTP or HTTPS), 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8CurlTimeoutMs --
 *
 *	Read the per-operation timeout from the `::th8_timeout`
 *	script variable.  Returns the value in milliseconds, with
 *	30 000 (30 s) as the fallback if the variable is unset, has
 *	an unparseable value, or holds a non-positive / overflow
 *	value.  Used by th8CurlGetData to set CURLOPT_TIMEOUT_MS and
 *	CURLOPT_CONNECTTIMEOUT_MS on each curl handle.
 *
 *----------------------------------------------------------------------
 */

static long
th8CurlTimeoutMs(Th8_Interp *interp)
{
    long nMs = 30000L;
    size_t nVal = 0;
    const char *zVal;
    th8_int64_t parsed = 0;
    int rc;

    rc = Th8_GetVar(interp, "::th8_timeout", TH8_NOLEN);
    if (rc != TH8_OK) return nMs;
    zVal = Th8_GetResult(interp, &nVal);
    if (Th8_ToWideInt(0, zVal, nVal, &parsed) != TH8_OK) goto done;
    if (parsed < 1 || parsed > (th8_int64_t)0x7FFFFFFF) goto done;
    nMs = (long)parsed;
done:
    Th8_ClearResult(interp);
    return nMs;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CurlIsValidUri --
 *
 *	URI scheme-prefix validator used by the curl plugin
 *	before handing a URL to libcurl.  Accepts only
 *	`"http://"` (7 bytes) and `"https://"` (8 bytes)
 *	prefixes -- every other scheme (`file:`, `ftp:`,
 *	`gopher:`, `data:`, etc.) is rejected up front so a
 *	hostile or buggy script cannot route through libcurl
 *	into local-filesystem reads or other unintended
 *	protocols.
 *
 *	The minimum-length test `nUri < 8` short-circuits all
 *	subsequent character checks; per FINDINGS.md
 *	Finding 005 the inner `ALWAYS(nUri >= ...)` operands
 *	are intrinsic-true at runtime so the MC/DC analysis
 *	stays focused on the per-character scheme prefix.
 *
 * Parameters:
 *	zUri -- URI bytes (not necessarily NUL-terminated).
 *	nUri -- URI length.
 *
 * Returns:
 *	1 if the URI starts with `"http://"` or `"https://"`;
 *	0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
th8CurlIsValidUri(
    const char *zUri,  /* URI to validate. */
    size_t nUri)  /* Length. */
{
    /*
     * Minimum valid: "http://x" (8 chars).
     */

    if (nUri < 8) return 0;

    /*
     * Must start with "https://" or "http://".
     *
     * The L78 early return guarantees nUri >= 8 below, so
     * the nUri-comparison sub-conditions in the two scheme
     * checks are ALWAYS T at runtime.  Wrap to fold them
     * away from MC/DC analysis, leaving only the per-
     * character scheme-prefix sub-conditions as real tests. */

    if (ALWAYS(nUri >= 8) && zUri[0] == 'h' && zUri[1] == 't' &&
        zUri[2] == 't' && zUri[3] == 'p' && zUri[4] == 's' &&
        zUri[5] == ':' && zUri[6] == '/' && zUri[7] == '/') {
	return 1;
    }
    if (ALWAYS(nUri >= 7) && zUri[0] == 'h' && zUri[1] == 't' &&
        zUri[2] == 't' && zUri[3] == 'p' && zUri[4] == ':' &&
        zUri[5] == '/' && zUri[6] == '/') {
	return 1;
    }
    return 0;
}


/*
 * CurlWriteCtx -- per-request accumulator passed to libcurl as the
 * `userdata` pointer for `CURLOPT_WRITEFUNCTION`.  Owns a growable
 * heap buffer allocated through the interpreter's platform
 * allocator so the entire request body lives in TH8-tracked memory.
 */
typedef struct {
    char *zBuf;   /* Accumulated data. */
    size_t nBuf;  /* Current length. */
    size_t nAlloc;  /* Allocated capacity. */
    Th8_Interp *interp; /* For Th8_Malloc/Realloc. */
} CurlWriteCtx;

/*
 *----------------------------------------------------------------------
 *
 * th8CurlWriteData --
 *
 *	libcurl `CURLOPT_WRITEFUNCTION` callback that
 *	appends `(size * nmemb)` received bytes to the
 *	`CurlWriteCtx` accumulator pointed to by `userdata`.
 *	Doubles the allocation when needed (minimum 4096) via
 *	`Th8_AttemptRealloc`, and copies through
 *	`Th8_Memcpy` so every byte goes through the
 *	interpreter's platform allocator.
 *
 *	Returning a byte count less than `(size * nmemb)`
 *	signals an error to libcurl, which then aborts the
 *	transfer.  This helper returns 0 on any allocation
 *	or arithmetic-overflow failure so the request fails
 *	cleanly rather than truncating silently.
 *
 *	Overflow checks (`TH8_SAFE_MUL_SIZE`,
 *	`TH8_SAFE_ADD_SIZE`) cover the `size * nmemb` and
 *	`ctx->nBuf + nBytes + 1` arithmetic up front,
 *	defending against pathological libcurl invocations
 *	that would otherwise wrap to a small allocation.
 *
 * Parameters:
 *	ptr      -- libcurl-supplied data block.
 *	size     -- bytes per element (libcurl always sets 1).
 *	nmemb    -- number of elements.
 *	userdata -- `CurlWriteCtx *` accumulator.
 *
 * Returns:
 *	Number of bytes consumed (== `size * nmemb`) on
 *	success; 0 on overflow / OOM.
 *
 * Side effects:
 *	Grows the accumulator's heap buffer; copies bytes
 *	into it.
 *
 *----------------------------------------------------------------------
 */
static size_t
th8CurlWriteData(
    void *ptr,   /* Data from curl. */
    size_t size,  /* Element size (always 1). */
    size_t nmemb,  /* Number of elements. */
    void *userdata)  /* CurlWriteCtx pointer. */
{
    CurlWriteCtx *ctx = (CurlWriteCtx *)userdata;
    size_t nBytes = 0;
    size_t nNeeded = 0;

    if (TH8_SAFE_MUL_SIZE(size, nmemb, &nBytes)) return 0;
    {
	size_t nTmp = 0;
	if (TH8_SAFE_ADD_SIZE(ctx->nBuf, nBytes, &nTmp)) return 0;
	if (TH8_SAFE_ADD_SIZE(nTmp, 1, &nNeeded)) return 0;
    }

    if (nNeeded > ctx->nAlloc) {
	size_t nNew = ctx->nAlloc * 2;
	char *zNew;

	if (nNew < nNeeded) nNew = nNeeded;
	if (nNew < 4096) nNew = 4096;
	zNew = (char *)Th8_AttemptRealloc(ctx->interp, ctx->zBuf, nNew);
	if (!zNew) return 0;  /* Signal error to curl */
	ctx->zBuf = zNew;
	ctx->nAlloc = nNew;
    }
    Th8_Memcpy(ctx->interp, &ctx->zBuf[ctx->nBuf], ptr, nBytes);
    ctx->nBuf += nBytes;
    ctx->zBuf[ctx->nBuf] = '\0';
    return nBytes;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CurlGetData --
 *
 *	xGetData callback: fetch data from an HTTP/HTTPS URI
 *	using libcurl.  The name is treated as a URI verbatim.
 *
 *	Returns TH8_OK on success (HTTP 200), TH8_ERROR otherwise.
 *
 * Why / How:
 *	Implements the Th8_Platform.xGetData callback for the curl
 *	platform.  Validates the URI scheme, initializes a curl easy
 *	handle, configures TLS verification, optional DNSSEC via
 *	libunbound, DNS-over-HTTPS, a 1MB download limit, and
 *	redirect following (max 5 hops).  The response body is
 *	accumulated by th8CurlWriteData.  Ownership of the buffer
 *	transfers to the caller on success.
 *
 * Results:
 *	TH8_OK with data in *pzData and *pnData on HTTP 200.
 *	TH8_ERROR with a descriptive message on any failure.
 *
 * Side effects:
 *	Performs a network HTTP/HTTPS request.  Allocates memory
 *	for the response body.
 *
 *----------------------------------------------------------------------
 */

static int
th8CurlGetData(
    Th8_Interp *interp, /* Interpreter (for allocation). */
    void *pCtx,   /* Host context (unused). */
    const char *zName,  /* URI to fetch. */
    size_t nName,  /* URI length. */
    char **pzData,  /* OUT: fetched data. */
    size_t *pnData)  /* OUT: data length. */
{
    CURL *curl;
    CURLcode res;
    CurlWriteCtx wctx;
    long httpCode = 0;
    char *zUri;
#  if defined(TH8_ENABLE_UNBOUND)
    struct curl_slist *pDnsResolve = NULL;
#  endif

    (void)pCtx;

    /*
     * Validate the URI scheme.
     */

    if (!th8CurlIsValidUri(zName, nName)) {
	*pzData = 0;
	*pnData = 0;
	Th8_SetResult(interp, "invalid or disallowed URI scheme", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Make a NUL-terminated copy (curl needs it).
     */

    zUri = (char *)TH8_ALLOC_STR(interp, nName);
    if (!zUri) {
	*pzData = 0;
	*pnData = 0;
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, zUri, zName, nName);
    zUri[nName] = '\0';

    /*
     * Initialize curl and perform the request.
     */

    curl = curl_easy_init();
    if (!curl) {
	Th8_Free(interp, zUri);
	*pzData = 0;
	*pnData = 0;
	Th8_SetResult(interp, "curl initialization failed", TH8_NOLEN);
	return TH8_ERROR;
    }

    wctx.zBuf = 0;
    wctx.nBuf = 0;
    wctx.nAlloc = 0;
    wctx.interp = interp;

    curl_easy_setopt(curl, CURLOPT_URL, zUri);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, th8CurlWriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    /*
     * Per-operation timeout (2026-06-08, Bug 47 fix): read
     * ::th8_timeout (MILLISECONDS) if set; fall back to 2000ms.
     * Apply to both the connect phase (CURLOPT_CONNECTTIMEOUT_MS)
     * and the whole operation (CURLOPT_TIMEOUT_MS).  Bounding the
     * connect phase prevents the long socket-timeout hangs
     * observed in the DNS-mock test sweep when the target host is
     * being held in connect-pending by the kernel rather than
     * refused immediately.  Scripts can raise the timeout via
     * `set ::th8_timeout N_MS` for longer-running requests.
     */
    curl_easy_setopt(
        curl, CURLOPT_CONNECTTIMEOUT_MS, (long)th8CurlTimeoutMs(interp));
    curl_easy_setopt(
        curl, CURLOPT_TIMEOUT_MS, (long)th8CurlTimeoutMs(interp));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    /*
     * Security: verify TLS certificates by default.
     */

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    /*
     * Security: pin a minimum TLS version of 1.2.  Older protocol
     * versions have known weaknesses; set the floor explicitly rather
     * than relying on the (backend-dependent) library default.
     */

#  if defined(CURL_SSLVERSION_TLSv1_2)
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
#  endif

    /*
     * Security: trust-root source.  Ask the TLS backend to consult the
     * operating system's native certificate store where it can (e.g.
     * the Windows CryptoAPI store under the OpenSSL/Schannel backends);
     * this is a no-op for macOS SecureTransport, which already uses the
     * keychain.  It keeps trust behaviour aligned with the host's
     * browsers instead of relying on a stale or absent compiled-in CA
     * bundle.  Deployments that ship their own bundle can override the
     * CA file at runtime via the conventional CURL_CA_BUNDLE
     * environment variable (libcurl does not read it on its own).
     */

#  if defined(CURLSSLOPT_NATIVE_CA)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#  endif
    {
	char *zCaBundle = Th8_GetEnv(interp, "CURL_CA_BUNDLE");

	if (zCaBundle) {
	    /* CURLOPT_CAINFO copies the string, so free our copy now. */
	    curl_easy_setopt(curl, CURLOPT_CAINFO, zCaBundle);
	    Th8_Free(interp, zCaBundle);
	}
    }

    /*
     * Security: restrict redirects to HTTPS only.  FOLLOWLOCATION is
     * enabled above; without this an attacker-controlled or
     * misconfigured redirect could downgrade a security-sensitive
     * fetch (signed scripts, public-key tokens, trusted time) from
     * https to cleartext http.  Forbid every scheme but https on
     * redirects (the initial request scheme is unaffected).
     */

#  if LIBCURL_VERSION_NUM >= 0x075500 /* 7.85.0: string form */
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#  else
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#  endif

    /*
     * Security: DNSSEC-validated DNS pre-resolution via libunbound.
     *
     * Extract the hostname from the URI, resolve it with full
     * DNSSEC chain-of-trust validation, and pin the result in
     * libcurl via CURLOPT_RESOLVE.  If DNSSEC validation returns
     * bogus (active tampering), reject the fetch entirely.
     *
     * This is a stopgap until libcurl itself gains native DNSSEC
     * validation support.  The DoH fallback (below) provides
     * transport-layer DNS authentication when unbound is not
     * available.
     */

#  if defined(TH8_ENABLE_UNBOUND)
    {
	/*
	 * Extract hostname from "https://host/path" or
	 * "http://host:port/path".
	 */
	{
	    const char *p = zUri;
	    const char *zHost = NULL;
	    size_t nHost = 0;
	    int port = 443;
	    char zHostBuf[256];

	    /* Skip scheme.  The caller (th8CurlIsValidUri) has
	     * already verified zUri starts with "http://" or
	     * "https://", so the four-char "http" prefix and the
	     * "://" trio that follow the optional 's' are ALWAYS
	     * present at this point. */
	    if (ALWAYS(
	            p[0] == 'h' && p[1] == 't' && p[2] == 't' &&
	            p[3] == 'p')) {
		p += 4;
		if (*p == 's') {
		    p++;
		    port = 443;
		} else {
		    port = 80;
		}
		if (ALWAYS(p[0] == ':' && p[1] == '/' && p[2] == '/')) p += 3;
	    }
	    zHost = p;
	    while (*p && *p != '/' && *p != ':' && *p != '?')
		p++;
	    nHost = (size_t)(p - zHost);
	    if (*p == ':') {
		port = 0;
		p++;
		while (*p >= '0' && *p <= '9') {
		    port = port * 10 + (*p - '0');
		    p++;
		}
	    }

	    if (nHost > 0 && nHost < sizeof(zHostBuf)) {
		Th8_DnsResult *pDns = NULL;

		memcpy(zHostBuf, zHost, nHost);
		zHostBuf[nHost] = '\0';

		if (Th8_DnsResolve(
		        interp, zHostBuf, nHost, TH8_DNS_TYPE_A, &pDns) ==
		        TH8_OK &&
		    pDns) {
		    if (pDns->bogus) {
			/* DNSSEC validation failed -- reject. */
			Th8_DnsResolveFree(interp, pDns);
			curl_easy_cleanup(curl);
			Th8_Free(interp, zUri);
			*pzData = 0;
			*pnData = 0;
			Th8_SetResult(
			    interp,
			    "DNSSEC validation failed for "
			    "fetch target (bogus DNS)",
			    TH8_NOLEN);
			return TH8_ERROR;
		    }
		    if (pDns->pData && pDns->pData[0] && pDns->pLen &&
		        pDns->pLen[0] == 4) {
			/* Pin the DNSSEC-validated A record. */
			char zEntry[300];
			char zIp[INET_ADDRSTRLEN];

			inet_ntop(AF_INET, pDns->pData[0], zIp, sizeof(zIp));
			/* Build "host:port:ip" without snprintf. */
			{
			    char zPort[8];
			    int pi = 0;
			    int pv = port;
			    char *p = zEntry;

			    if (pv == 0) {
				zPort[pi++] = '0';
			    } else {
				char tmp[8];
				int ti = 0;
				while (pv > 0) {
				    tmp[ti++] = '0' + (pv % 10);
				    pv /= 10;
				}
				while (ti > 0)
				    zPort[pi++] = tmp[--ti];
			    }
			    zPort[pi] = '\0';

			    memcpy(p, zHostBuf, nHost);
			    p += nHost;
			    *p++ = ':';
			    memcpy(p, zPort, pi);
			    p += pi;
			    *p++ = ':';
			    {
				size_t nIp = 0;
				while (zIp[nIp])
				    nIp++;
				memcpy(p, zIp, nIp);
				p += nIp;
			    }
			    *p = '\0';
			}
			pDnsResolve = curl_slist_append(pDnsResolve, zEntry);
			curl_easy_setopt(curl, CURLOPT_RESOLVE, pDnsResolve);
		    }
		    Th8_DnsResolveFree(interp, pDns);
		}
	    }
	}

	/* pDnsResolve freed after curl_easy_perform below. */
	(void)pDnsResolve;
    }
#  endif /* TH8_ENABLE_UNBOUND */

    /*
     * Security: enable DNS-over-HTTPS (DoH) for DNSSEC-grade
     * authenticated DNS resolution.  This prevents DNS spoofing
     * and cache poisoning attacks that could redirect fetches to
     * attacker-controlled servers.
     *
     * The DoH resolver validates DNSSEC on the server side, and
     * the HTTPS transport prevents on-path tampering.  Both the
     * DoH connection itself and the target connection use strict
     * TLS certificate verification.
     *
     * Cloudflare's 1.1.1.1 is used as the default resolver.
     * Override at compile time with -DTH8_CURL_DOH_URL="...".
     */

#  ifndef TH8_CURL_DOH_URL
#    define TH8_CURL_DOH_URL "https://1.1.1.1/dns-query"
#  endif

#  if LIBCURL_VERSION_NUM >= 0x073e00 /* 7.62.0 */
    curl_easy_setopt(curl, CURLOPT_DOH_URL, TH8_CURL_DOH_URL);
#  endif

#  if LIBCURL_VERSION_NUM >= 0x075500 /* 7.85.0 */
    /*
     * Verify the DoH server's TLS certificate as strictly
     * as the target connection's certificate.  VERIFYSTATUS
     * additionally requires a valid stapled OCSP response
     * (the DoH resolver, Cloudflare's 1.1.1.1 by default,
     * staples one); a backend without OCSP-stapling support
     * treats it as a no-op rather than an error.  The
     * trust-root options set for the main transfer above
     * (CURLSSLOPT_NATIVE_CA / CURL_CA_BUNDLE -> CAINFO) are
     * inherited by the DoH sub-transfer automatically, so no
     * separate DoH CA configuration is needed.
     */
    curl_easy_setopt(curl, CURLOPT_DOH_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_DOH_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_DOH_SSL_VERIFYSTATUS, 1L);
#  endif

    /*
     * Security: set a reasonable max download size (1MB).
     */

    curl_easy_setopt(
        curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)(1 * 1024 * 1024));

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
#  if defined(TH8_ENABLE_UNBOUND)
    if (pDnsResolve) curl_slist_free_all(pDnsResolve);
#  endif
    Th8_Free(interp, zUri);

    if (res != CURLE_OK) {
	Th8_Free(interp, wctx.zBuf);
	*pzData = 0;
	*pnData = 0;
	Th8_SetResult(interp, curl_easy_strerror(res), TH8_NOLEN);
	return TH8_ERROR;
    }

    if (httpCode != 200) {
	Th8_Free(interp, wctx.zBuf);
	*pzData = 0;
	*pnData = 0;
	Th8_SetResult(
	    interp, "HTTP request failed (non-200 status)", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Transfer ownership of the buffer to the caller.
     */

    if (wctx.zBuf) {
	*pzData = wctx.zBuf;
	*pnData = wctx.nBuf;
    } else {
	*pzData = (char *)TH8_ALLOC(interp, 1);
	if (*pzData) (*pzData)[0] = '\0';
	*pnData = 0;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_CurlPlatform --
 *
 *	Platform table with only xGetData (curl-based).
 *	All other callbacks are NULL.
 *
 * Why / How:
 *	Only the xGetData slot is filled with th8CurlGetData.
 *	Everything else is NULL and must be provided by merging
 *	with nullio, libc, and/or POSIX platforms.  The struct is
 *	static (not const) to allow embedder patching if needed.
 *
 *----------------------------------------------------------------------
 */

static Th8_Platform th8CurlPlatformData = {
    1, /* nVersion */
    0, 0, 0, 0, /* xInitialize, xFinalize, xPreDeleteInterp, xDeleteInterp */

    /* Memory */
    0, 0, 0, 0, /* xMalloc, xRealloc, xFree, xMemorySize */
    0, /* xNeedMemory */

    /* Byte operations */
    0, 0, 0, 0, /* xMemcpy, xMemmove, xMemset, xMemcmp */

    /* String / utility */
    0, 0, 0, 0, 0, 0, /* xStrlen .. xVsnprintf */

    /* Threading */
    0, 0, 0, 0, /* xMutexInit .. xMutexLeave */
    0, 0, /* xIntCmpXchg, xMemBarrier */

    /* Manual-reset event handle */
    0, 0, 0, 0,
    0, /* xEventCreate, xEventDestroy, xEventSet, xEventReset, xEventWait */

    /* I/O */
    0, 0, 0, /* xInput, xOutput, xOutputError */
    0, 0, 0, 0, 0, 0, /* xGet/SetInput, xGet/SetOutput, xGet/SetErrorOutput */

    /* Channel / temporary I/O */
    0, 0, 0, 0,
    0, /* xChannelControl, xGetTemporaryData, xDeleteTemporaryData, xSetTemporaryData, xCloseTemporaryData */

    /* Filesystem */
    0, 0, 0, 0, 0, /* xNormalizePath .. xGetRealPath */
    0, 0, /* xGetRootPath, xSameFile */

    /* Data retrieval / binary loading */
    th8CurlGetData, /* xGetData */
    0, /* xDataExists */
    0, 0, /* xLoad, xUnload */

    /* Time */
    0, 0, 0, /* xTimeMs, xTimeUs, xSleep */

    /* Process / host */
    0, 0, 0, 0, 0,
    0, /* xGetPid, xGetUserName, xGetHostName, xGetEnv, xKeyValue, xGetStackBounds */
    0, 0, /* xGetParentPid, xGetThreadId */

    /* Error / diagnostics */
    0, 0, 0, 0, /* xGetLastError, xSetLastError, xEmitTrace, xPanic */

    /* Math / entropy */
    0, 0, /* xMathFunc, xRandomBytes */

    /* DNS */
    0, 0, /* xDnsResolve, xDnsResolveFree */

    /* Diagnostics -- the th8_unwind (compiler-runtime) layer supplies xStackBackTrace. */
    0, /* xStackBackTrace */

    /* 64-bit atomics */
    0, /* xIntCmpXchg64 */

    /* Host context */
    0 /* pCtx */
};


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetCurlPlatform --
 *
 *	Return a pointer to the curl platform implementation.
 *
 * Why / How:
 *	Returns the address of the module-level static platform
 *	struct.  The caller merges this with other platforms to
 *	build a complete Th8_Platform before Th8_CreateInterp.
 *
 * Results:
 *	Pointer to a static Th8_Platform struct.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetCurlPlatform(void)
{
    return &th8CurlPlatformData;
}

#endif /* TH8_PLATFORM_CURL */
