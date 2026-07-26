/*
 * th8_time.c -- NTP v4 / HTTPS time clients for TH8.
 *
 * Minimal client for querying authenticated wall-clock time.
 * Used by the signed-only policy subsystem to verify key expiration
 * dates and detect local clock manipulation (time-travel attacks).
 *
 * Features:
 *   - Multi-server consensus (median + disagreement threshold)
 *   - Per-interpreter cache for backward-clock detection
 *   - Cross-platform: POSIX sockets and Winsock
 *   - Configurable timeout and disagreement threshold
 *
 * Compile-time gate: TH8_ENABLE_CRYPTOGRAPHY
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"

#if defined(_WIN32) || defined(WIN32)
#  include "th8_meta_msvc.h"
#  include "th8_meta_win32.h"
#else
#  include "th8_meta_posix.h"
#endif

#include "th8_plat.h"
#include "th8.h"
#include "th8_int.h"

#if defined(TH8_ENABLE_CRYPTOGRAPHY)

#  include "th8_util.h"

/*
 * Platform socket headers (via meta-headers).
 */

#  if defined(_WIN32)
   /* <windows.h>, <winsock2.h>, <ws2tcpip.h> via th8_meta_win32.h */
typedef SOCKET ntp_socket_t;
#    define NTP_INVALID_SOCKET   INVALID_SOCKET
#    define ntp_close            closesocket
#    define ntp_poll(pfd, n, ms) WSAPoll((pfd), (n), (ms))
#  else
   /* POSIX socket headers via th8_meta_posix.h */
typedef int ntp_socket_t;
#    define NTP_INVALID_SOCKET   (-1)
#    define ntp_close            close
#    define ntp_poll(pfd, n, ms) poll((pfd), (n), (ms))
#  endif

#  if defined(TH8_ENABLE_UNBOUND)
#    include <unbound.h>
#  endif


/*
 *----------------------------------------------------------------------
 *
 * Constants.
 *
 *----------------------------------------------------------------------
 */

#  define NTP_PACKET_SIZE          48
#  define NTP_VERSION              4
#  define NTP_MODE_CLIENT          3
#  define NTP_MODE_SERVER          4
#  define NTP_EPOCH_DELTA          2208988800UL /* 1900-01-01 to 1970-01-01 */
#  define NTP_DEFAULT_TIMEOUT_MS   3000
#  define NTP_DEFAULT_MAX_DISAGREE 5 /* seconds */
#  define NTP_MAX_SERVERS          8
/*
 * Per-server send/receive attempts.  NTP runs over UDP, so a lost
 * request or reply is expected rather than exceptional; a single shot
 * would fail the whole query on any transient loss.  Only transient
 * failures (timeout / short read / send error) are retried -- a
 * response that arrives but fails validation is never retried.  A
 * caller may pass a value <= 0 to accept this default, or 1 to disable
 * retries.
 */
#  define NTP_DEFAULT_ATTEMPTS 3
#  define NTP_MAX_ATTEMPTS     10

static const char *th8NtpDefaultServers[] = {"urn.to", NULL};


/*
 *----------------------------------------------------------------------
 *
 * NTP v4 packet (48 bytes, network byte order).
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    unsigned char flags; /* LI(2) | VN(3) | Mode(3) */
    unsigned char stratum;
    unsigned char poll;
    signed char precision;
    unsigned char rootDelay[4];
    unsigned char rootDisp[4];
    unsigned char refId[4];
    unsigned char refTs[8]; /* Reference timestamp */
    unsigned char origTs[8]; /* Origin timestamp (T1 echoed) */
    unsigned char rxTs[8]; /* Receive timestamp (T2) */
    unsigned char txTs[8]; /* Transmit timestamp (T3) */
} Th8_NtpPacket;


/*
 *----------------------------------------------------------------------
 *
 * Timestamp helpers.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8NtpReadTs --
 *
 *	Read the seconds portion of an NTP v4 timestamp from a raw
 *	8-byte buffer (network / big-endian byte order).
 *
 * Why / How:
 *	NTP timestamps are 64-bit fixed-point values (32-bit seconds
 *	+ 32-bit fraction) in big-endian order.  Only the integer
 *	seconds are needed for certificate expiration checks, so this
 *	helper manually shifts the first four bytes into a host-order
 *	64-bit value, avoiding any dependency on ntohl() or endian
 *	headers.
 *
 * Results:
 *	The 32-bit seconds field as a host-order th8_uint64_t.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8NtpReadTs(const unsigned char ts[8])
{
    /* Big-endian 32-bit seconds. */
    return ((th8_uint64_t)ts[0] << 24) | ((th8_uint64_t)ts[1] << 16) |
           ((th8_uint64_t)ts[2] << 8) | ((th8_uint64_t)ts[3]);
}

/*
 *----------------------------------------------------------------------
 *
 * th8NtpWriteTs --
 *
 *	Write a seconds value into an 8-byte NTP timestamp buffer
 *	in big-endian (network) byte order, with the fractional part
 *	set to zero.
 *
 * Why / How:
 *	The NTP client request embeds the local clock as the origin
 *	timestamp (T1).  The server echoes it back in the origTs
 *	field, enabling the anti-spoof check in th8NtpQueryOne.
 *	Writing manually avoids htonl() / endian-header dependencies.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes 8 bytes into ts[].
 *
 *----------------------------------------------------------------------
 */

static void
th8NtpWriteTs(unsigned char ts[8], th8_uint64_t sec)
{
    ts[0] = (unsigned char)(sec >> 24);
    ts[1] = (unsigned char)(sec >> 16);
    ts[2] = (unsigned char)(sec >> 8);
    ts[3] = (unsigned char)(sec);
    ts[4] = ts[5] = ts[6] = ts[7] = 0; /* fraction = 0 */
}


/*
 *----------------------------------------------------------------------
 *
 * Winsock init/cleanup (Win32 only).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8NtpWsaInit --
 *
 *	Initialize the Winsock 2.2 library (Win32 only).
 *
 * Why / How:
 *	Windows requires a WSAStartup call before any socket API can
 *	be used.  This wrapper requests Winsock 2.2 and translates
 *	the Windows error code into a TH8_OK / TH8_ERROR return so
 *	that callers can use the standard TH8 error convention.
 *
 * Results:
 *	TH8_OK if WSAStartup succeeds, TH8_ERROR otherwise.
 *
 * Side effects:
 *	Loads the Winsock DLL and increments its internal reference
 *	count.  The caller must call WSACleanup() when finished.
 *
 *----------------------------------------------------------------------
 */

#  if defined(_WIN32)
static int
th8NtpWsaInit(void)
{
    WSADATA wsa;

    return (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? TH8_OK : TH8_ERROR;
}
#  endif


/*
 *----------------------------------------------------------------------
 *
 * th8NtpValidateResponse --
 *
 *	Validate a received NTP server packet against the request we
 *	sent and, on success, derive the Unix epoch seconds from the
 *	server's transmit timestamp.  Factored out of th8NtpQueryOne
 *	so the protocol-validation logic (version/mode/stratum, the
 *	origin-timestamp anti-spoof check, and the zero-timestamp
 *	guard) can be exercised for MC/DC directly with crafted
 *	packets via the internal stubs -- no live NTP exchange and no
 *	network flakiness.
 *
 * Results:
 *	TH8_OK with *pEpochSec set when the response is well-formed
 *	and authentic; TH8_ERROR (with a specific interp result)
 *	otherwise.
 *
 *----------------------------------------------------------------------
 */

int
th8NtpValidateResponse(
    Th8_Interp *interp,
    const void *respv,
    const void *reqv,
    th8_int64_t *pEpochSec)
{
    const Th8_NtpPacket *resp = (const Th8_NtpPacket *)respv;
    const Th8_NtpPacket *req = (const Th8_NtpPacket *)reqv;
    int version = (resp->flags >> 3) & 0x07;
    int mode = resp->flags & 0x07;
    th8_uint64_t t3Sec;

    if ((version != 3 && version != 4) || mode != NTP_MODE_SERVER) {
	Th8_SetResultStatic(
	    interp,
	    "clock ntp: invalid response "
	    "(bad version or mode)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    if (resp->stratum == 0 || resp->stratum > 15) {
	Th8_SetResultStatic(
	    interp,
	    "clock ntp: invalid stratum "
	    "(kiss-of-death or out of range)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Verify origTs matches our T1 (anti-spoof). */
    if (Th8_Memcmp(interp, resp->origTs, req->txTs, 8) != 0) {
	Th8_SetResultStatic(
	    interp,
	    "clock ntp: response origTs mismatch "
	    "(possible spoof)",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /* txTs must be non-zero. */
    t3Sec = th8NtpReadTs(resp->txTs);
    if (t3Sec == 0) {
	Th8_SetResultStatic(
	    interp, "clock ntp: zero transmit timestamp", TH8_NOLEN);
	return TH8_ERROR;
    }

    *pEpochSec = (th8_int64_t)(t3Sec - NTP_EPOCH_DELTA);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NtpQueryOne --
 *
 *	Query a single NTP server.  Opens a UDP socket, sends a
 *	client request, receives the response with timeout, and
 *	extracts the server's transmit timestamp as Unix epoch
 *	seconds.
 *
 * Why / How:
 *	Certificate expiration checks need authenticated wall-clock
 *	time that cannot be faked by rolling back the local clock.
 *	This function implements the single-server leg of that query:
 *	DNS resolution (with optional DNSSEC validation via libunbound
 *	or the glibc AD-bit fallback), a standard NTP v4 client-mode
 *	exchange over UDP port 123, and strict response validation
 *	including version/mode/stratum checks and an origin-timestamp
 *	anti-spoof comparison that rejects reflected or replayed
 *	packets.
 *
 * Results:
 *	TH8_OK on success with *pEpochSec set.  TH8_ERROR on
 *	network or protocol error.
 *
 *----------------------------------------------------------------------
 */

static int
th8NtpQueryOne(
    Th8_Interp *interp,
    const char *zServer,
    int timeoutMs,
    int attempts,
    th8_int64_t *pEpochSec)
{
    struct addrinfo hints, *res = NULL;
    ntp_socket_t sock = NTP_INVALID_SOCKET;
    Th8_NtpPacket req, resp;
    th8_int64_t localMs = 0;
    th8_uint64_t t1Sec;
    int rc = TH8_ERROR;
    int attempt;

    *pEpochSec = 0;

    /*
     * DNS resolution with DNSSEC validation.
     *
     * When libunbound is available (TH8_ENABLE_UNBOUND),
     * perform local cryptographic DNSSEC validation using
     * the IANA root trust anchor.  ub_resolve() returns
     * result->secure=1 if the domain's DNS records were
     * validated through the full DNSSEC chain of trust.
     *
     * If DNSSEC validation fails (result->bogus), the
     * resolution is REJECTED -- this is a hard failure,
     * not a warning.  A bogus DNSSEC result indicates
     * active tampering or misconfiguration.
     *
     * If the domain is unsigned (neither secure nor bogus),
     * resolution proceeds normally -- DNSSEC is opt-in per
     * domain.
     *
     * When libunbound is not available, fall back to plain
     * getaddrinfo().  The NTP origin timestamp anti-spoof
     * mechanism provides a secondary defense.
     */

#  if defined(TH8_ENABLE_UNBOUND)
    {
	struct ub_ctx *ubctx;
	struct ub_result *ubresult = NULL;
	int ubrc;

	ubctx = ub_ctx_create();
	if (ubctx) {
	    /*
	     * Load the root trust anchor.  Try common locations.
	     * If none found, DNSSEC validation is disabled but
	     * resolution still works.
	     */
	    if (ub_ctx_add_ta_file(ubctx, "/etc/unbound/root.key") != 0 &&
	        ub_ctx_add_ta_file(ubctx, "/usr/share/dns/root.key") != 0 &&
	        ub_ctx_add_ta_file(ubctx, "/var/lib/unbound/root.key") != 0 &&
	        ub_ctx_add_ta_file(
	            ubctx, "/opt/homebrew/etc/unbound/root.key") != 0) {
		/*
		 * No trust anchor found.  DNSSEC validation
		 * will not be performed, but resolution works.
		 */
		TH8_TRACE_ERR(
		    interp, "NTP: no DNSSEC root trust anchor found");
	    }

	    ubrc = ub_resolve(
	        ubctx, zServer, 1 /* A record */, 1 /* IN class */,
	        &ubresult);

	    if (ubrc == 0 && ubresult) {
		if (ubresult->bogus) {
		    /*
		     * DNSSEC validation FAILED.  This indicates
		     * active DNS tampering or misconfiguration.
		     * Reject the resolution entirely.
		     */
		    Th8_SetResultStatic(
		        interp,
		        "clock ntp: DNSSEC validation failed "
		        "(bogus DNS response)",
		        TH8_NOLEN);
		    ub_resolve_free(ubresult);
		    ub_ctx_delete(ubctx);
		    return TH8_ERROR;
		}
		if (ubresult->secure) {
		    TH8_TRACE_ERR(interp, "NTP: DNSSEC validated (secure)");
		}
		/* If neither secure nor bogus: unsigned domain,
		 * proceed normally. */
	    }
	    if (ubresult) ub_resolve_free(ubresult);
	    ub_ctx_delete(ubctx);
	}
    }
#  elif !defined(_WIN32) && !defined(WIN32) && defined(__GLIBC__)
    /*
     * Fallback: no libunbound.  Use res_nquery() with
     * RES_USE_DNSSEC to check the AD bit from the recursive
     * resolver.  This is "trust the resolver" validation,
     * not local cryptographic validation.
     */
    {
	struct __res_state rs;
	unsigned char abuf[512];
	int alen;

	Th8_Memset(interp, &rs, 0, sizeof(rs));
	if (res_ninit(&rs) == 0) {
	    rs.options |= RES_USE_EDNS0;
#    if defined(RES_USE_DNSSEC)
	    rs.options |= RES_USE_DNSSEC;
#    endif
	    alen = res_nquery(
	        &rs, zServer, 1 /*C_IN*/, 1 /*T_A*/, abuf, sizeof(abuf));
	    if (alen >= 4) {
		int adBit = (abuf[3] >> 5) & 1;

		if (!adBit) {
		    TH8_TRACE_ERR(
		        interp, "NTP DNS: AD bit not set "
		                "(DNSSEC not validated by resolver)");
		}
	    }
	    res_nclose(&rs);
	}
    }
#  endif /* DNSSEC fallback */

    Th8_Memset(interp, &hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(zServer, "123", &hints, &res) != 0 || !res) {
	TH8_TRACE_ERR(interp, "getaddrinfo failed for NTP server");
	Th8_ErrorMessage(
	    interp, "clock ntp: cannot resolve \"", zServer,
	    Th8_Strlen(interp, zServer));
	return TH8_ERROR;
    }

    /*
     * Create UDP socket.
     */

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == NTP_INVALID_SOCKET) {
	TH8_TRACE_ERR(interp, "socket creation failed");
	Th8_SetResultStatic(
	    interp, "clock ntp: cannot create UDP socket", TH8_NOLEN);
	goto done;
    }

    /*
     * Set receive timeout.
     */

    {
#  if defined(_WIN32)
	DWORD tv = (DWORD)timeoutMs;

	setsockopt(
	    sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#  else
	struct timeval tv;

	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#  endif
    }

    /*
     * Query loop.  Each attempt builds a FRESH request (a new origin
     * timestamp) and does exactly one send / receive.  Transient
     * failures -- a sendto error, a receive timeout (a lost request or
     * reply), or a short response -- are retried up to `attempts`
     * times, because NTP runs over UDP and packet loss is expected.  A
     * response that arrives but fails validation is NOT retried: a bad
     * stratum or an origin-timestamp mismatch is a misbehaving- or
     * hostile-server signal, and retrying would only paper over it.
     * Rebuilding the request each attempt keeps the anti-spoof origin-
     * timestamp check in th8NtpValidateResponse sound per attempt.
     */

    if (attempts < 1) attempts = 1;

    for (attempt = 0; attempt < attempts; attempt++) {
	Th8_Memset(interp, &req, 0, sizeof(req));
	req.flags = (unsigned char)((NTP_VERSION << 3) | NTP_MODE_CLIENT);

	/* Set T1 (origin timestamp) from local clock. */
	Th8_GetTimeMs(interp, &localMs);
	t1Sec = (th8_uint64_t)(localMs / 1000) + NTP_EPOCH_DELTA;
	th8NtpWriteTs(req.txTs, t1Sec);

	if (sendto(
	        sock, (const char *)&req, NTP_PACKET_SIZE, 0, res->ai_addr,
	        (int)res->ai_addrlen) != NTP_PACKET_SIZE) {
	    TH8_TRACE_ERR(interp, "NTP sendto failed");
	    Th8_ErrorMessage(
	        interp, "clock ntp: sendto failed for \"", zServer,
	        Th8_Strlen(interp, zServer));
	    continue; /* transient -- retry */
	}

#  if !defined(_WIN32)
	{
	    struct pollfd pfd;

	    pfd.fd = sock;
	    pfd.events = POLLIN;
	    if (ntp_poll(&pfd, 1, timeoutMs) <= 0) {
		Th8_ErrorMessage(
		    interp, "clock ntp: timeout from \"", zServer,
		    Th8_Strlen(interp, zServer));
		continue; /* lost packet -- retry */
	    }
	}
#  endif

	Th8_Memset(interp, &resp, 0, sizeof(resp));
	{
	    int nRecv = (int)
	        recvfrom(sock, (char *)&resp, NTP_PACKET_SIZE, 0, NULL, NULL);

	    if (nRecv < NTP_PACKET_SIZE) {
		TH8_TRACE_ERR(interp, "NTP recvfrom failed");
		Th8_ErrorMessage(
		    interp, "clock ntp: incomplete response from \"", zServer,
		    Th8_Strlen(interp, zServer));
		continue; /* transient -- retry */
	    }
	}

	/*
	 * A full response arrived.  Validate and derive epoch seconds.
	 * Validation is TERMINAL -- whether it succeeds or rejects, the
	 * result is never retried (see the loop-header comment).  The
	 * protocol-validation logic is factored into
	 * th8NtpValidateResponse so it can be MC/DC-driven directly with
	 * crafted packets (via the internal stubs) without a live NTP
	 * exchange.
	 */

	rc = th8NtpValidateResponse(interp, &resp, &req, pEpochSec);
	goto done;
    }

    /*
     * Every attempt failed transiently; rc is still TH8_ERROR and the
     * interp result holds the last attempt's diagnostic.
     */

done:
    if (sock != NTP_INVALID_SOCKET) ntp_close(sock);
    if (res) freeaddrinfo(res);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NtpSortTimes --
 *
 *	Sort an array of epoch-second timestamps in ascending order
 *	using insertion sort.
 *
 * Why / How:
 *	The NTP consensus algorithm needs the median of the collected
 *	server timestamps.  The array is tiny (at most NTP_MAX_SERVERS
 *	= 8 elements), so insertion sort is optimal: no recursion, no
 *	extra allocation, and trivially correct.  A general-purpose
 *	qsort is avoided to eliminate the CRT dependency.
 *
 * Results:
 *	None.  The array a[] is sorted in place.
 *
 * Side effects:
 *	Reorders the elements of a[].
 *
 *----------------------------------------------------------------------
 */

void
th8NtpSortTimes(th8_int64_t *a, int n)
{
    int i, j;

    for (i = 1; i < n; i++) {
	th8_int64_t key = a[i];

	j = i - 1;
	while (j >= 0 && a[j] > key) {
	    a[j + 1] = a[j];
	    j--;
	}
	a[j + 1] = key;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8NtpQuery --
 *
 *	Query multiple NTP servers and return a consensus wall-clock
 *	time as Unix epoch seconds.
 *
 * Why / How:
 *	A single NTP server can be spoofed or compromised.  This
 *	function queries up to NTP_MAX_SERVERS servers, requires a
 *	quorum of (nServers+1)/2 successful responses, computes the
 *	median timestamp, and rejects the result if any response
 *	disagrees with the median by more than maxDisagreeSec.  It
 *	also performs backward-clock detection by comparing the new
 *	median against the per-interpreter cached last-known-good NTP
 *	time and elapsed local-clock time, catching local clock
 *	rollback ("time-travel") attacks.
 *
 * Results:
 *	TH8_OK with *pEpochSec set to the consensus time.
 *	TH8_ERROR if quorum is not met, servers disagree, or
 *	backward-clock tampering is detected.
 *
 * Side effects:
 *	Opens and closes network sockets.  Updates the per-interpreter
 *	NTP time cache (lastNtpSec, lastLocalMs).  On Win32,
 *	initializes and cleans up Winsock.
 *
 *----------------------------------------------------------------------
 */

int
th8NtpQuery(
    Th8_Interp *interp,
    const char **azServers,
    int nServers,
    int timeoutMs,
    int maxDisagreeSec,
    int attempts,
    th8_int64_t *pEpochSec)
{
    th8_int64_t aTimes[NTP_MAX_SERVERS];
    int nGood = 0;
    int i;
    th8_int64_t median;
    th8_int64_t localMs = 0;

    *pEpochSec = 0;

    if (!azServers || nServers <= 0) {
	azServers = th8NtpDefaultServers;
	nServers = 0;
	while (azServers[nServers])
	    nServers++;
    }
    if (nServers > NTP_MAX_SERVERS) nServers = NTP_MAX_SERVERS;
    if (timeoutMs <= 0) timeoutMs = NTP_DEFAULT_TIMEOUT_MS;
    if (maxDisagreeSec <= 0) maxDisagreeSec = NTP_DEFAULT_MAX_DISAGREE;
    if (attempts <= 0) attempts = NTP_DEFAULT_ATTEMPTS;
    if (attempts > NTP_MAX_ATTEMPTS) attempts = NTP_MAX_ATTEMPTS;

#  if defined(_WIN32)
    if (th8NtpWsaInit() != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "clock ntp: WSAStartup failed", TH8_NOLEN);
	return TH8_ERROR;
    }
#  endif

    /*
     * Query each server.  Collect successful results.
     */

    for (i = 0; i < nServers; i++) {
	th8_int64_t t = 0;

	if (th8NtpQueryOne(interp, azServers[i], timeoutMs, attempts, &t) ==
	    TH8_OK) {
	    aTimes[nGood++] = t;
	}
    }

#  if defined(_WIN32)
    WSACleanup();
#  endif

    /*
     * Quorum check: at least (nServers+1)/2 must respond.
     * For a single server, 1 response is required.
     */

    if (nGood < (nServers + 1) / 2 || nGood == 0) {
	Th8_SetResultStatic(
	    interp, "clock ntp: insufficient server responses", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Compute the median.
     */

    th8NtpSortTimes(aTimes, nGood);
    median = aTimes[nGood / 2];

    /*
     * Check consensus: all results within threshold of median.
     */

    for (i = 0; i < nGood; i++) {
	th8_int64_t diff = aTimes[i] - median;

	if (diff < 0) diff = -diff;
	if (diff > (th8_int64_t)maxDisagreeSec) {
	    Th8_SetResultStatic(
	        interp,
	        "clock ntp: servers disagree beyond "
	        "threshold",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    /*
     * Backward-clock detection: check against the cached
     * last-known-good NTP time.
     */

    {
	th8_int64_t lastNtp = th8GetLastNtpSec(interp);
	th8_int64_t lastLocal = th8GetLastLocalMs(interp);

	Th8_GetTimeMs(interp, &localMs);

	if (lastNtp > 0 && lastLocal > 0) {
	    th8_int64_t elapsedMs = localMs - lastLocal;

	    if (elapsedMs < -1000) {
		/* Local clock went backward by > 1 second. */
		Th8_SetResultStatic(
		    interp,
		    "clock ntp: local clock went "
		    "backward (possible time-travel)",
		    TH8_NOLEN);
		return TH8_ERROR;
	    }

	    {
		th8_int64_t expectedNtp = lastNtp + elapsedMs / 1000;
		th8_int64_t drift = median - expectedNtp;

		if (drift < 0) drift = -drift;
		if (drift > (th8_int64_t)maxDisagreeSec * 2) {
		    Th8_SetResultStatic(
		        interp,
		        "clock ntp: time-travel detected "
		        "(NTP time inconsistent with "
		        "prior reading)",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
	    }
	}
    }

    /*
     * Cache the successful result.
     */

    th8SetLastNtpSec(interp, median);
    th8SetLastLocalMs(interp, localMs);

    *pEpochSec = median;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HTTPS time query.
 *
 *	Fetches authenticated time from an HTTPS endpoint that returns
 *	a Tcl-formatted response: nonce {} ticks N timeStamp N
 *	hashValue HEX signature {BASE64...}
 *
 *	The TLS connection provides transport authentication.  The
 *	RSA signature (verified with the embedded keyTime public key)
 *	provides origin authentication: even if the TLS endpoint is
 *	compromised, a valid signature cannot be forged without the
 *	private key.
 *
 *----------------------------------------------------------------------
 */

#  include <openssl/evp.h>

#  define TH8_TIME_BASE_URL    "https://urn.to/r/get_time_01"
#  define TH8_TIME_NONCE_BYTES 16


/*
 *----------------------------------------------------------------------
 *
 * th8HttpsTimeFindField --
 *
 *	Find a named field in a split Tcl key-value list.
 *	The list must already be split into azElem/anElem/nCount.
 *	Returns the value pointer and length, or NULL if not found.
 *
 * Why / How:
 *	The HTTPS time server returns a Tcl key-value list such as
 *	"nonce N ticks T timeStamp S hashValue H".  After splitting
 *	with Th8_SplitList, the caller needs to extract individual
 *	fields by name.  This helper walks the even-indexed (key)
 *	elements comparing lengths and bytes against the requested
 *	key, and returns the corresponding odd-indexed (value) element
 *	pointer and length.
 *
 * Results:
 *	Pointer to the value string on success with *pnVal set to
 *	its byte length.  NULL if the key is not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
th8HttpsTimeFindField(
    Th8_Interp *interp,
    char **azElem,
    size_t *anElem,
    int nCount,
    const char *zKey,
    size_t nKey,
    size_t *pnVal)
{
    int i;

    for (i = 0; i + 1 < nCount; i += 2) {
	if (anElem[i] == nKey &&
	    Th8_Memcmp(interp, azElem[i], zKey, nKey) == 0) {
	    if (pnVal) *pnVal = anElem[i + 1];
	    return azElem[i + 1];
	}
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * th8HttpsTimeVerifySignature --
 *
 *	Verify the RSA-SHA512 signature over the response data.
 *	The signed data is the response string WITHOUT the
 *	"signature {...}" field -- i.e., the Tcl list:
 *	"nonce N ticks T timeStamp S hashValue H"
 *
 * Why / How:
 *	The TLS transport authenticates the server, but if the HTTPS
 *	endpoint were compromised an attacker could serve arbitrary
 *	timestamps.  The RSA-SHA512 signature provides origin
 *	authentication: only the holder of the keyTime private key
 *	can produce a valid signature.  This function base64-decodes
 *	the signature, loads the embedded keyTime public key, and
 *	calls Th8_RsaVerify to perform the cryptographic check.
 *
 * Results:
 *	TH8_OK if the signature is valid.  TH8_ERROR if the base64
 *	decode fails, the key cannot be loaded, or the RSA
 *	verification fails.
 *
 * Side effects:
 *	Allocates and frees temporary memory for the decoded
 *	signature and the RSA key structure.
 *
 *----------------------------------------------------------------------
 */

int
th8HttpsTimeVerifySignature(
    Th8_Interp *interp,
    const char *zSignedData,
    size_t nSignedData,
    const char *zSigB64,
    size_t nSigB64)
{
    unsigned char *pSig = NULL;
    size_t nSig = 0;
    Th8_RsaKey *pKey = NULL;
    const unsigned char *zKeyData;
    size_t nKeyData;
    int rc;

    /* Decode the base64 signature. */
    rc = th8Base64Decode(interp, zSigB64, nSigB64, &pSig, &nSig);
    if (rc != TH8_OK || !pSig || nSig == 0) {
	Th8_Free(interp, pSig);
	Th8_SetResultStatic(
	    interp, "clock https: invalid base64 signature", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* Load the keyTime public key. */
    zKeyData = Th8_GetEmbeddedKeyTime(&nKeyData);
    rc = Th8_RsaKeyLoad(interp, zKeyData, nKeyData, &pKey);
    if (rc != TH8_OK) {
	Th8_Free(interp, pSig);
	Th8_SetResultStatic(
	    interp, "clock https: cannot load keyTime", TH8_NOLEN);
	return TH8_ERROR;
    }

    /* RSA-SHA512 verify. */
    rc = Th8_RsaVerify(
        interp, pKey, (const unsigned char *)zSignedData, nSignedData, pSig,
        nSig);

    Th8_RsaKeyFree(interp, pKey);
    Th8_Free(interp, pSig);

    if (rc != TH8_OK) {
	Th8_SetResultStatic(
	    interp,
	    "clock https: RSA signature "
	    "verification failed",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8HttpsTimeQuery --
 *
 *	Fetch authenticated wall-clock time from an HTTPS time
 *	service.  The response is a signed Tcl key-value list
 *	containing a nonce, timestamp, and RSA-SHA512 signature.
 *
 * Why / How:
 *	This is the HTTPS counterpart to the NTP time query.  It
 *	provides a second, independent time source for certificate
 *	expiration checks.  Security is layered: (1) a random nonce
 *	prevents replay attacks, (2) TLS provides transport
 *	authentication, (3) the RSA-SHA512 signature over the
 *	response data (verified against the embedded keyTime public
 *	key) provides origin authentication, and (4) the backward-
 *	clock cache detects local clock rollback between queries.
 *	The signed-only policy is temporarily disabled for the HTTPS
 *	fetch since the URL is not a signed script.
 *
 * Results:
 *	TH8_OK with *pEpochSec set on success.  TH8_ERROR if the
 *	fetch fails, the nonce mismatches, the signature is invalid,
 *	the timestamp is out of plausible range, or backward-clock
 *	tampering is detected.
 *
 * Side effects:
 *	Performs an HTTPS request.  Allocates temporary memory.
 *	Updates the per-interpreter NTP time cache.  Temporarily
 *	modifies the signed-only policy (saved and restored).
 *
 *----------------------------------------------------------------------
 */

int
th8HttpsTimeQuery(
    Th8_Interp *interp,
    const char *zUrl,
    size_t nUrl,
    th8_int64_t *pEpochSec)
{
    char *zData = NULL;
    size_t nData = 0;
    int rc;
    char savedSigned[TH8_SIGNED_SAVE_SIZE];
    char *zFullUrl = NULL;
    size_t nFullUrl = 0;

    /* Generate a random nonce for replay prevention. */
    unsigned char aNonce[TH8_TIME_NONCE_BYTES];
    char zNonceHex[TH8_TIME_NONCE_BYTES * 2 + 1];

    *pEpochSec = 0;

    /*
     * Sensitivity boundary: the URL is transmitted to a remote time
     * server, so a sensitive value used as (or within) the URL must
     * never leave the process.  The tag rides in nUrl (the caller
     * passes argl[] straight through).  Reject before any network work.
     */
    if (TH8_SENSITIVE(nUrl)) {
	Th8_SetResultStatic(
	    interp, "sensitive value cannot be written", TH8_NOLEN);
	return TH8_ERROR;
    }

    rc = Th8_RandomBytes(interp, aNonce, TH8_TIME_NONCE_BYTES);
    if (rc != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "clock https: cannot generate nonce", TH8_NOLEN);
	return TH8_ERROR;
    }

    {
	int j;
	static const char hex[] = "0123456789abcdef";

	for (j = 0; j < TH8_TIME_NONCE_BYTES; j++) {
	    zNonceHex[j * 2] = hex[(aNonce[j] >> 4) & 0x0f];
	    zNonceHex[j * 2 + 1] = hex[aNonce[j] & 0x0f];
	}
	zNonceHex[TH8_TIME_NONCE_BYTES * 2] = 0;
    }

    /*
     * Build the full URL: baseUrl + "&nonce=" + hex_nonce
     */

    if (!zUrl || nUrl == 0) {
	zUrl = TH8_TIME_BASE_URL;
	nUrl = Th8_Strlen(interp, zUrl);
    }

    TH8_STR_APPEND(interp, &zFullUrl, &nFullUrl, zUrl, nUrl);

    /*
     * Append the nonce as a query parameter.  Use '?' if the
     * URL has no existing query string, '&' otherwise.
     */

    {
	int hasQuery = 0;
	size_t k;

	for (k = 0; k < nUrl; k++) {
	    if (zUrl[k] == '?') {
		hasQuery = 1;
		break;
	    }
	}
	TH8_STR_APPEND(
	    interp, &zFullUrl, &nFullUrl,
	    hasQuery ? "&nonce=" : "?nonce=", 7);
    }
    TH8_STR_APPEND(
        interp, &zFullUrl, &nFullUrl, zNonceHex, TH8_TIME_NONCE_BYTES * 2);

    /*
     * Temporarily disable signed-only policy for the HTTPS
     * fetch (the URL is not a signed script).
     */

    Th8_SaveSignedOnly(interp, savedSigned);
    if (Th8_EnableSignedOnly(interp, 0) != TH8_OK) {
	Th8_RestoreSignedOnly(interp, savedSigned);
	Th8_SetResultStatic(
	    interp, "clock https: cannot disable signed-only script policy",
	    TH8_NOLEN);
	return TH8_ERROR;
    }
    rc = Th8_GetData(interp, zFullUrl, nFullUrl, &zData, &nData, 0);
    Th8_RestoreSignedOnly(interp, savedSigned);

    Th8_Free(interp, zFullUrl);

    if (rc != TH8_OK) {
	Th8_SetResultStatic(
	    interp, "clock https: cannot fetch time server", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Parse the response.  The redirect endpoint returns a
     * two-element braced Tcl list:
     *   {nonce N ticks T timeStamp S hashValue H} {base64sig}
     *
     * Element [0] is the signed data, element [1] is the
     * base64-encoded RSA signature.
     */

    {
	char **azOuter = NULL;
	size_t *anOuter = NULL;
	int nOuter = 0;
	char **azData = NULL;
	size_t *anData = NULL;
	int nData2 = 0;
	const char *zSignedData, *zSigB64;
	size_t nSignedData, nSigB64;
	const char *zNonce, *zTs;
	size_t nNonceR, nTs;
	th8_int64_t epochSec = 0;

	/* Split the outer response into 2 elements. */
	if (Th8_SplitList(
	        interp, zData, nData, &azOuter, &anOuter, &nOuter,
	        TH8_LIST_NONE) != TH8_OK ||
	    nOuter < 2) {
	    Th8_Free(interp, azOuter);
	    Th8_Free(interp, zData);
	    Th8_SetResultStatic(
	        interp, "clock https: invalid response format", TH8_NOLEN);
	    return TH8_ERROR;
	}

	zSignedData = azOuter[0];
	nSignedData = anOuter[0];
	zSigB64 = azOuter[1];
	nSigB64 = anOuter[1];

	/* Split the data element into key-value pairs. */
	if (Th8_SplitList(
	        interp, zSignedData, nSignedData, &azData, &anData, &nData2,
	        TH8_LIST_NONE) != TH8_OK) {
	    Th8_Free(interp, azOuter);
	    Th8_Free(interp, zData);
	    Th8_SetResultStatic(
	        interp, "clock https: invalid data list", TH8_NOLEN);
	    return TH8_ERROR;
	}

	/* Extract required fields from the data list. */
	zNonce = th8HttpsTimeFindField(
	    interp, azData, anData, nData2, "nonce", 5, &nNonceR);
	zTs = th8HttpsTimeFindField(
	    interp, azData, anData, nData2, "timeStamp", 9, &nTs);

	if (!zNonce || !zTs) {
	    Th8_Free(interp, azData);
	    Th8_Free(interp, azOuter);
	    Th8_Free(interp, zData);
	    Th8_SetResultStatic(
	        interp, "clock https: missing nonce or timeStamp", TH8_NOLEN);
	    return TH8_ERROR;
	}

	/*
	 * Step 1: Verify nonce matches what we sent.
	 */

	if (nNonceR != TH8_TIME_NONCE_BYTES * 2 ||
	    Th8_Memcmp(interp, zNonce, zNonceHex, nNonceR) != 0) {
	    Th8_Free(interp, azData);
	    Th8_Free(interp, azOuter);
	    Th8_Free(interp, zData);
	    Th8_SetResultStatic(
	        interp,
	        "clock https: nonce mismatch "
	        "(possible replay)",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}

	/*
	 * Step 2: Verify RSA-SHA512 signature.
	 * The signed data is element [0] of the outer list
	 * (the UTF-8 bytes of the data list string).
	 */

	rc = th8HttpsTimeVerifySignature(
	    interp, zSignedData, nSignedData, zSigB64, nSigB64);

	/*
	 * Step 3: Extract and validate the timestamp.
	 */

	if (rc == TH8_OK) {
	    rc = Th8_ToWideInt(interp, zTs, nTs, &epochSec);
	}

	Th8_Free(interp, azData);
	Th8_Free(interp, azOuter);
	Th8_Free(interp, zData);

	if (rc != TH8_OK) {
	    return TH8_ERROR;
	}

	if (epochSec < 1577836800LL || epochSec > 4102444800LL) {
	    Th8_SetResultStatic(
	        interp,
	        "clock https: timestamp out of "
	        "plausible range",
	        TH8_NOLEN);
	    return TH8_ERROR;
	}

	/*
	 * Step 4: Update the backward-clock cache.
	 */

	{
	    th8_int64_t localMs = 0;
	    th8_int64_t lastNtp = th8GetLastNtpSec(interp);
	    th8_int64_t lastLocal = th8GetLastLocalMs(interp);

	    Th8_GetTimeMs(interp, &localMs);

	    if (lastNtp > 0 && lastLocal > 0) {
		th8_int64_t elapsedMs = localMs - lastLocal;

		if (elapsedMs < -1000) {
		    Th8_SetResultStatic(
		        interp,
		        "clock https: local clock went "
		        "backward",
		        TH8_NOLEN);
		    return TH8_ERROR;
		}
	    }

	    th8SetLastNtpSec(interp, epochSec);
	    th8SetLastLocalMs(interp, localMs);
	}

	*pEpochSec = epochSec;
    }

    return TH8_OK;

oom:
    Th8_Free(interp, zFullUrl);
    return TH8_ERROR;
}


#endif /* TH8_ENABLE_CRYPTOGRAPHY */
