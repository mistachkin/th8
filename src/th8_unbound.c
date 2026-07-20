/*
 * th8_unbound.c -- Shared libunbound DNSSEC-validating resolver
 *	driver for TH8's `Th8_Platform.xDnsResolve` /
 *	`xDnsResolveFree` callbacks.
 *
 *	Owns the platform-agnostic half of the libunbound
 *	integration: context creation, security hardening, the
 *	bootstrap-then-auto-roll trust-anchor lifecycle, the
 *	synchronous resolve call, the result-wrapping arithmetic,
 *	and teardown.
 *
 *	Each platform (POSIX, Win32) provides its own
 *	`Th8_UnboundOps` instance populated with native helpers
 *	(path discovery, file existence test, mkdir-p, atomic
 *	copy) and registers tiny one-line `xDnsResolve` /
 *	`xDnsResolveFree` wrappers in its `Th8_Platform`
 *	descriptor that delegate here.  Without this driver, the
 *	two platforms would duplicate ~200 lines of
 *	hardening / wrapping / orchestration logic each, with the
 *	matching drift risk.
 *
 *	Gated on TH8_ENABLE_UNBOUND -- when the macro is undefined
 *	this entire translation unit is empty.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8.h"
#include "th8_int.h"
#include "th8_plat.h"
#include "th8_unbound.h"

#if defined(TH8_ENABLE_UNBOUND)

#  include <unbound.h>

/*
 *----------------------------------------------------------------------
 *
 * Th8_UnboundResultImpl --
 *
 *	Wrapper that owns both the public `Th8_DnsResult` (which
 *	is the first field, so a `Th8_DnsResult *` punned from
 *	the wrapper points at it) and the libunbound `ub_ctx` /
 *	`ub_result` lifetimes.  Allocated by
 *	`th8UnboundResolve`; freed by `th8UnboundResolveFree`.
 *
 *	Opaque to callers -- the platform `xDnsResolve` callback
 *	hands the embedded `Th8_DnsResult *` back to the script
 *	layer, which only ever sees the public type.
 *
 *----------------------------------------------------------------------
 */
typedef struct Th8_UnboundResultImpl {
    Th8_DnsResult pub;     /* MUST be first. */
    struct ub_ctx *ubctx;
    struct ub_result *ubr;
} Th8_UnboundResultImpl;

/*
 *----------------------------------------------------------------------
 *
 * th8UnboundHardenCtx --
 *
 *	Apply the highest-reasonable security configuration to a
 *	freshly-built libunbound context.  Called from
 *	`th8UnboundResolve` right after `ub_ctx_create`, before
 *	any trust-anchor load or resolution call.
 *
 *	Each `ub_ctx_set_option` failure is logged via
 *	`TH8_TRACE_ERR` but not fatal: libunbound falls back to
 *	its built-in default for the failing option, which is
 *	always at least as safe as the explicit value we tried
 *	to set.  Hard-failing the whole resolution on an option
 *	rejection would mean that any libunbound build that
 *	drops a knob (e.g. an older library that doesn't know
 *	`aggressive-nsec`) would break embedders even though
 *	the security posture is still solid.
 *
 *	First disables libunbound's own stderr logging via
 *	`ub_ctx_debugout(ctx, NULL)` -- an embedded library must not
 *	write to the host's stderr, and this silences the benign
 *	"can't bind socket" noise when a sandbox denies the
 *	outgoing-query sockets (the resolve then fails via its return
 *	code).  Validation status still reaches the caller through the
 *	resolve RESULT, so nothing security-relevant is hidden.
 *
 *	Settings applied (every key has the libunbound trailing
 *	colon convention):
 *
 *	  *  `module-config: "validator iterator"` -- enable the
 *	     DNSSEC validator module by name (explicit even
 *	     though it is the default).  Without this, libunbound
 *	     runs as a plain stub resolver and `bogus` is
 *	     meaningless.
 *	  *  `val-permissive-mode: no` -- DNSSEC failures are
 *	     hard failures (the validated record is dropped),
 *	     not just an advisory `bogus` flag.
 *	  *  `harden-glue: yes` -- discard glue records that
 *	     have not been signed by the parent zone.
 *	  *  `harden-dnssec-stripped: yes` -- when DNSSEC
 *	     signatures are stripped en route, treat the response
 *	     as a hostile attempt to downgrade rather than as a
 *	     legitimate unsigned answer.
 *	  *  `harden-below-nxdomain: yes` -- if a name is
 *	     NXDOMAIN, every subdomain is too (RFC 8020).
 *	  *  `harden-referral-path: yes` -- validate every
 *	     nameserver in the delegation chain.
 *	  *  `harden-algo-downgrade: yes` -- refuse to fall back
 *	     to a weaker DNSSEC algorithm when stronger options
 *	     are advertised.
 *	  *  `harden-large-queries: yes` /
 *	     `harden-short-bufsize: yes` -- mitigate DNS
 *	     amplification and buffer-size confusion attacks.
 *	  *  `qname-minimisation: yes` -- only ask each
 *	     authoritative server for the labels it needs to
 *	     answer (RFC 7816).  `qname-minimisation-strict` is
 *	     intentionally NOT set: strict mode breaks against
 *	     some buggy authoritative implementations.
 *	  *  `aggressive-nsec: yes` -- use cached NSEC records
 *	     to synthesize NXDOMAIN responses (RFC 8198).
 *	  *  `use-caps-for-id: yes` -- 0x20-bit case
 *	     randomization on outbound queries (anti-spoofing).
 *	  *  `do-not-query-localhost: yes` -- explicit default;
 *	     defends against a hostile loopback DNS proxy.
 *	  *  `prefetch: no` -- one-shot embedded use does not
 *	     benefit from speculative cache refresh.
 *	  *  `unwanted-reply-threshold: 10000000` -- drop the
 *	     cache when too many unsolicited replies arrive
 *	     (Kaminsky-class defense).
 *	  *  `cache-min-ttl: 0` -- never extend an authoritative
 *	     short TTL.
 *	  *  `cache-max-ttl: 86400` -- cap caching at one day
 *	     so signature rollovers and key rotations propagate.
 *	  *  `cache-max-negative-ttl: 3600` -- cap negative cache
 *	     at one hour.
 *	  *  `minimal-responses: yes` -- strip extraneous answer-
 *	     section data.
 *	  *  `rrset-roundrobin: yes` -- spread load across
 *	     multiple A / AAAA answers.
 *	  *  `edns-buffer-size: 1232` -- DNS Flag Day 2020
 *	     recommendation; below the typical Ethernet MTU
 *	     minus IPv6 + UDP overhead.
 *
 *	What is intentionally NOT done:
 *
 *	  *  `ub_ctx_resolvconf(NULL)` -- libunbound runs as a
 *	     fully recursive resolver using its built-in root
 *	     hints.  Reading system DNS would route every TH8
 *	     query through the user's chosen upstream resolver,
 *	     which we cannot trust.
 *	  *  `ub_ctx_set_fwd` -- no forwarders.  Same reason.
 *
 * Parameters:
 *	ubctx -- live libunbound context.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Mutates `ubctx`'s option state.  May emit one
 *	`TH8_TRACE_ERR` per rejected option.
 *
 *----------------------------------------------------------------------
 */
void
th8UnboundHardenCtx(struct ub_ctx *ubctx)
{
    static const struct {
	const char *zKey;
	const char *zVal;
    } aOpt[] = {
        {"module-config:", "validator iterator"},
        {"val-permissive-mode:", "no"},
        {"harden-glue:", "yes"},
        {"harden-dnssec-stripped:", "yes"},
        {"harden-below-nxdomain:", "yes"},
        {"harden-referral-path:", "yes"},
        {"harden-algo-downgrade:", "yes"},
        {"harden-large-queries:", "yes"},
        {"harden-short-bufsize:", "yes"},
        {"qname-minimisation:", "yes"},
        {"aggressive-nsec:", "yes"},
        {"use-caps-for-id:", "yes"},
        {"do-not-query-localhost:", "yes"},
        {"prefetch:", "no"},
        {"unwanted-reply-threshold:", "10000000"},
        {"cache-min-ttl:", "0"},
        {"cache-max-ttl:", "86400"},
        {"cache-max-negative-ttl:", "3600"},
        {"minimal-responses:", "yes"},
        {"rrset-roundrobin:", "yes"},
        {"edns-buffer-size:", "1232"},
    };
    size_t i;

    /*
     * Silence libunbound's own logging (default destination: stderr).
     * TH8 is an embedded library and must not spew to the host
     * application's stderr.  DNSSEC validation status is reported to the
     * caller through the resolve RESULT (secure / bogus / insecure), not
     * via stderr, so nothing security-relevant is lost.  This also
     * suppresses the benign "can't bind socket: Operation not permitted"
     * noise emitted when the process runs in a sandbox that denies the
     * outgoing-query sockets libunbound opens for a recursive lookup --
     * the resolve then fails cleanly through its return code, which the
     * caller already handles.  ub_ctx_debugout(ctx, NULL) disables both
     * debug and error output.
     */
    if (ub_ctx_debugout(ubctx, NULL) != 0) {
	TH8_TRACE_ERR(NULL, "ub_ctx_debugout");
    }

    for (i = 0; i < sizeof(aOpt) / sizeof(aOpt[0]); i++) {
	if (ub_ctx_set_option(
	        ubctx, (char *)aOpt[i].zKey, (char *)aOpt[i].zVal) != 0) {
	    TH8_TRACE_ERR(NULL, aOpt[i].zKey);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8UnboundSetupManagedAnchor --
 *
 *	Set the libunbound context up for RFC 5011 auto-roll
 *	trust-anchor tracking.  Two-phase lifecycle:
 *
 *	  1. **Already-bootstrapped path.**  If the managed copy
 *	     reported by `pOps->xGetManagedAnchorPath` already
 *	     exists from a previous run, register it directly
 *	     via `ub_ctx_add_ta_autr`.  libunbound polls the
 *	     root DNSKEY on each resolve, runs the RFC 5011
 *	     state machine, and writes state transitions back
 *	     to the managed file.  No file copy this time.
 *
 *	  2. **First-run bootstrap.**  If the managed copy does
 *	     not yet exist, the helper expects `zSrcStatic` to
 *	     name a readable static anchor returned by
 *	     `pOps->xFindStaticAnchorPath`.  It calls
 *	     `pOps->xEnsureParentDir`, then
 *	     `pOps->xCopyFileContents` (which create the
 *	     managed dir and file with per-user-only
 *	     permissions), and finally registers the managed
 *	     copy via `ub_ctx_add_ta_autr`.
 *
 *	Returns 0 on any failure (no per-user dir, mkdir
 *	failed, copy failed, libunbound refused the file).  On
 *	failure the caller falls back to plain static-only mode
 *	against `zSrcStatic`.
 *
 *	Per-call-context note: this is called from
 *	`th8UnboundResolve`, which builds a fresh libunbound
 *	context per resolution.  AUTR state persists on disk,
 *	not in the context, so the state machine still advances
 *	across context teardowns; the RFC 5011 hold-down is
 *	wall-clock 30 days regardless of TH8 resolution cadence.
 *
 * Parameters:
 *	ubctx       -- live libunbound context (already hardened).
 *	pOps        -- platform operations vtable (non-NULL).
 *	zSrcStatic  -- source static-anchor path used only on
 *		the first-run bootstrap (caller supplies the
 *		output of `pOps->xFindStaticAnchorPath`).  May
 *		be NULL when the managed copy already exists.
 *
 * Returns:
 *	1 if auto-roll tracking was successfully configured.
 *	0 if the managed copy could not be located / created
 *	or libunbound refused the file.
 *
 * Side effects:
 *	May create the per-user managed directory and `root.key`
 *	on the first-run bootstrap.  Mutates `ubctx`'s
 *	trust-anchor state on success.
 *
 *----------------------------------------------------------------------
 */
int
th8UnboundSetupManagedAnchor(
    struct ub_ctx *ubctx,
    const Th8_UnboundOps *pOps,
    const char *zSrcStatic)
{
    char zManaged[4096];

    if (!pOps->xGetManagedAnchorPath(zManaged, sizeof(zManaged))) {
	return 0;
    }
    if (pOps->xPathReadable(zManaged)) {
	return ub_ctx_add_ta_autr(ubctx, zManaged) == 0;
    }
    /* First-run bootstrap requires a source. */
    if (!zSrcStatic) return 0;
    if (!pOps->xEnsureParentDir(zManaged)) return 0;
    if (!pOps->xCopyFileContents(zSrcStatic, zManaged)) return 0;
    return ub_ctx_add_ta_autr(ubctx, zManaged) == 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8UnboundResolve --
 *
 *	Synchronous DNSSEC-validating DNS resolution backed by
 *	libunbound.  Implements the platform-agnostic core of
 *	both `th8PosixDnsResolve` and `th8Win32DnsResolve` --
 *	those callbacks reduce to a single line that delegates
 *	here with the platform's `Th8_UnboundOps`.
 *
 *	Pipeline:
 *	  1. Validate arguments and NUL-terminate `zName` into
 *	     a stack buffer bounded by the 255-byte DNS-label
 *	     limit.
 *	  2. Create the libunbound context.
 *	  3. Apply the security-hardening pass via
 *	     `th8UnboundHardenCtx`.
 *	  4. Select the trust anchor.  Three modes, tried in
 *	     order; first that succeeds wins:
 *	       a. `TH8_DNS_ROOT_KEY` env var (operator override;
 *	          registered as static via `ub_ctx_add_ta_file`
 *	          because the operator owns the file lifecycle).
 *	       b. Bootstrap-then-auto-roll managed copy via
 *	          `th8UnboundSetupManagedAnchor` (the normal
 *	          path; full RFC 5011 auto-rollover).
 *	       c. Static-only fallback against the source
 *	          anchor `pOps->xFindStaticAnchorPath` returned,
 *	          used when the managed-copy setup failed.
 *	       d. None of the above -- validation disabled and
 *	          `bogus` always reads 0.
 *	  5. Call `ub_resolve` synchronously.  On libunbound
 *	     error or NULL result, tear down and report
 *	     `TH8_ERROR`.
 *	  6. Wrap the libunbound result in a
 *	     `Th8_UnboundResultImpl` and build a parallel
 *	     `size_t` length array (libunbound's
 *	     `ub_result.len` is `int *`; `Th8_DnsResult.pLen`
 *	     is `const size_t *`).
 *
 * Parameters:
 *	interp   -- live interpreter (used for `Th8_AttemptMalloc`).
 *	pOps     -- platform operations vtable (non-NULL).
 *	zName    -- hostname (not necessarily NUL-terminated).
 *	nName    -- hostname length (must be `> 0` and `< 256`).
 *	eType    -- DNS record type (libunbound `LDNS_RR_TYPE_*`).
 *	ppResult -- output: filled with the result pointer on
 *		success; set to NULL on failure.
 *
 * Returns:
 *	`TH8_OK` with `*ppResult` non-NULL on success.
 *	`TH8_ERROR` on bad arguments, libunbound failure, or
 *	allocation failure.
 *
 * Side effects:
 *	Allocates one `Th8_UnboundResultImpl` and (typically) a
 *	parallel `size_t` length array.  Opens a libunbound
 *	context; the context's lifetime extends until
 *	`th8UnboundResolveFree`.  Performs a network DNS query.
 *	On the first run after install, also creates the
 *	per-user managed trust-anchor copy.
 *
 *----------------------------------------------------------------------
 */
int
th8UnboundResolve(
    struct Th8_Interp *interp,
    const Th8_UnboundOps *pOps,
    const char *zName,
    size_t nName,
    int eType,
    struct Th8_DnsResult **ppResult)
{
    struct ub_ctx *ubctx;
    struct ub_result *ubr = NULL;
    Th8_UnboundResultImpl *pImpl;
    char zHostBuf[256];

    if (!interp || !pOps || !zName || !ppResult) return TH8_ERROR;
    *ppResult = NULL;
    if (nName == 0 || nName >= sizeof(zHostBuf)) return TH8_ERROR;
    Th8_Memcpy(interp, zHostBuf, zName, nName);
    zHostBuf[nName] = '\0';

    ubctx = ub_ctx_create();
    if (!ubctx) return TH8_ERROR;

    th8UnboundHardenCtx(ubctx);

    /* Trust-anchor selection.  See the function-level comment
     * above for the search-order rationale. */
    {
	char *zEnv = Th8_GetEnv(interp, "TH8_DNS_ROOT_KEY");
	if (zEnv && *zEnv) {
	    /* Operator override: static-only, embedder owns lifecycle. */
	    (void)(ub_ctx_add_ta_file(ubctx, zEnv) == 0);
	    Th8_Free(interp, zEnv);
	} else {
	    char zSrc[4096];
	    int bHaveSrc = pOps->xFindStaticAnchorPath(zSrc, sizeof(zSrc));
	    if (zEnv) Th8_Free(interp, zEnv);
	    /* Try the bootstrap-then-auto-roll managed copy.  Pass
	     * `zSrc` so a first-run bootstrap has a source to copy
	     * from; later runs ignore it (the managed copy already
	     * exists). */
	    if (!th8UnboundSetupManagedAnchor(
	            ubctx, pOps, bHaveSrc ? zSrc : NULL) &&
	        bHaveSrc) {
		/* Managed-mode setup failed but we have a static
		 * source -- fall back to static-only mode. */
		(void)(ub_ctx_add_ta_file(ubctx, zSrc) == 0);
	    }
	}
    }

    if (ub_resolve(ubctx, zHostBuf, eType, 1 /* IN */, &ubr) != 0 || !ubr) {
	ub_ctx_delete(ubctx);
	return TH8_ERROR;
    }

    pImpl = (Th8_UnboundResultImpl *)TH8_ALLOC(interp, sizeof(*pImpl));
    if (!pImpl) {
	ub_resolve_free(ubr);
	ub_ctx_delete(ubctx);
	return TH8_ERROR;
    }
    /* Count records (data is NULL-terminated array of pointers).
     * ubr->data CAN be NULL for empty result sets (DNS responses
     * with no matching records); the NULL check is REAL and must
     * stay un-wrapped or we segfault under TH8_OMIT. */
    {
	int n = 0;
	while (ubr->data && ubr->data[n])
	    n++;
	pImpl->pub.nRecord = n;
    }
    pImpl->pub.bogus = ubr->bogus ? 1 : 0;
    pImpl->pub.pData = (const unsigned char **)ubr->data;
    pImpl->pub.pLen = NULL; /* See note below. */
    pImpl->ubctx = ubctx;
    pImpl->ubr = ubr;

    /* libunbound's ub_result.len is `int *`; Th8_DnsResult's pLen
     * is `const size_t *`.  Build a parallel size_t array so the
     * caller can iterate uniformly.  Allocate alongside the
     * struct to keep lifetime simple.  libunbound sets ubr->len
     * as a parallel array whenever it returns records, so the
     * second check is intrinsic-true once nRecord > 0. */
    if (pImpl->pub.nRecord > 0 && ALWAYS(ubr->len)) {
	size_t *pSizes;
	pSizes = (size_t *)TH8_ALLOC_MUL_ADD(
	    interp, (size_t)pImpl->pub.nRecord, sizeof(size_t), 0);
	if (pSizes) {
	    int i;
	    for (i = 0; i < pImpl->pub.nRecord; i++) {
		pSizes[i] = (size_t)ubr->len[i];
	    }
	    pImpl->pub.pLen = pSizes;
	}
    }
    *ppResult = &pImpl->pub;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8UnboundResolveFree --
 *
 *	Tear down a `Th8_DnsResult` previously returned by
 *	`th8UnboundResolve`.  Implements the platform-agnostic
 *	teardown for both `th8PosixDnsResolveFree` and
 *	`th8Win32DnsResolveFree`.
 *
 *	Frees, in order:
 *	  1. The parallel `size_t` length array (if any).
 *	  2. The libunbound result via `ub_resolve_free`.
 *	  3. The libunbound context via `ub_ctx_delete`.
 *	  4. The wrapper struct itself.
 *
 *	The NULL guards on `interp` and `pResult` are split
 *	into separate `if` statements per FINDINGS.md Finding
 *	005 sec. 5b so the MC/DC C-pairs of each guard are
 *	individually reachable.
 *
 * Parameters:
 *	interp  -- live interpreter (used for `Th8_Free`).
 *	pResult -- result returned by `th8UnboundResolve`, or
 *		NULL.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Frees every resource owned by the result, including
 *	the libunbound context.
 *
 *----------------------------------------------------------------------
 */
void
th8UnboundResolveFree(
    struct Th8_Interp *interp,
    struct Th8_DnsResult *pResult)
{
    Th8_UnboundResultImpl *pImpl;

    if (!interp) return;
    if (!pResult) return;
    pImpl = (Th8_UnboundResultImpl *)pResult;
    if (pImpl->pub.pLen) Th8_Free(interp, (void *)pImpl->pub.pLen);
    if (pImpl->ubr) ub_resolve_free(pImpl->ubr);
    if (pImpl->ubctx) ub_ctx_delete(pImpl->ubctx);
    Th8_Free(interp, pImpl);
}

#endif /* TH8_ENABLE_UNBOUND */
