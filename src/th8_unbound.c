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
 *	  *  `harden-referral-path: yes` -- deliberately OMITTED.
 *	     It is an experimental, non-RFC option that fires
 *	     extra infrastructure queries to validate every
 *	     nameserver on the delegation chain.  libunbound's own
 *	     documentation warns it "could lead to performance
 *	     problems because of the extra query load" and that it
 *	     needs a larger `outgoing-range` / more threads to work
 *	     reliably.  In a one-shot embedded `ub_ctx` (default
 *	     small outgoing-range, single lookup) it makes ordinary
 *	     recursion SERVFAIL -- e.g. `time.w.sb` resolves
 *	     `secure` without it and returns rcode 2 with it (Bug
 *	     79).  It is NOT load-bearing for DNSSEC validation: the
 *	     validator still requires a `secure` answer, and the
 *	     answer/glue records are still hardened by
 *	     `harden-glue`, `harden-dnssec-stripped`, and
 *	     `harden-algo-downgrade`.
 *	  *  `use-caps-for-id: yes` (DNS 0x20 case randomisation) --
 *	     deliberately OMITTED.  It is an anti-spoofing measure
 *	     for the recursion path, but many authoritative servers
 *	     (Cloudflare and other CDNs prominent among them) do NOT
 *	     preserve query-name case, so unbound rejects their
 *	     case-folded replies and the lookup SERVFAILs.  Measured
 *	     ~40% intermittent failures resolving `time.w.sb` (a
 *	     Cloudflare-hosted name) with it on, 0% with it off (Bug
 *	     79).  It is not load-bearing here: DNSSEC validation is
 *	     the actual anti-spoof (a forged answer fails signature
 *	     validation and is rejected as bogus), and unbound still
 *	     randomises source port and transaction ID.  Trading a
 *	     redundant off-path-spoofing hardener for reliable
 *	     resolution against a major DNS provider is the correct
 *	     call for a validating stub.
 *
 * Why / How:
 *	First disables libunbound's own stderr logging via
 *	ub_ctx_debugout(ctx, NULL) -- an embedded library must not spew to
 *	the host's stderr, and validation status is reported through the
 *	resolve result instead.  It then applies a fixed table of hardening
 *	and behavior options (validator module, glue/NSEC/algo-downgrade
 *	hardening, qname minimisation, cache TTL bounds, EDNS buffer size,
 *	etc.) with ub_ctx_set_option.  A failure to set any single option is
 *	non-fatal: it is traced and the remaining options are still applied.
 *
 * Parameters:
 *	ubctx -- live libunbound context.
 *
 * Results:
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
        {"harden-algo-downgrade:", "yes"},
        {"harden-large-queries:", "yes"},
        {"harden-short-bufsize:", "yes"},
        {"qname-minimisation:", "yes"},
        {"aggressive-nsec:", "yes"},
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
 * Why / How:
 *	Resolves the managed-anchor path through pOps->xGetManagedAnchorPath.
 *	If that file is already readable, it registers it directly with
 *	ub_ctx_add_ta_autr and is done.  Otherwise it performs the first-run
 *	bootstrap: it requires a source static anchor (zSrcStatic), creates
 *	the parent directory (xEnsureParentDir), copies the static anchor into
 *	the managed location (xCopyFileContents), and registers the copy with
 *	ub_ctx_add_ta_autr.  Any missing step returns 0 so the caller can fall
 *	back to static-only mode.  Auto-roll (RFC 5011) state lives on disk,
 *	so it survives the per-resolution context lifetime.
 *
 * Parameters:
 *	ubctx       -- live libunbound context (already hardened).
 *	pOps        -- platform operations vtable (non-NULL).
 *	zSrcStatic  -- source static-anchor path used only on
 *		the first-run bootstrap (caller supplies the
 *		output of `pOps->xFindStaticAnchorPath`).  May
 *		be NULL when the managed copy already exists.
 *
 * Results:
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

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * th8UnboundVerifyTh8Anchor --
 *
 *	Return 1 iff the trust-anchor file `zPath` is covered by a valid TH8
 *	detached signature in the companion `zPath + ".b64sig"`.  Used to
 *	authenticate a TH8-DOMAIN anchor -- the bundled module-adjacent
 *	root.key, or a TH8_DNS_ROOT_KEY file -- before it is handed to
 *	libunbound.
 *
 * Why / How:
 *	Both files are read RAW via pOps->xReadFile (never through the
 *	signed-only script policy, so no recursion), into fixed stack
 *	buffers -- anchors and their signatures are small (a few KB), and a
 *	file that does not fit is refused rather than truncated.  The bytes
 *	are handed to th8VerifyAnchorSig, which checks the detached
 *	signature against the compiled-in trusted keys.  Returns 0 (untrusted)
 *	whenever no raw reader is available, either file is missing/oversize,
 *	or verification fails.
 *
 * Results:
 *	1 if the anchor file is covered by a valid TH8 detached signature;
 *	0 otherwise (no raw reader, a missing/empty/oversize anchor or
 *	signature file, or a failed signature check).
 *
 * Side effects:
 *	Reads two files.  Secure-zeroes its stack buffers before returning.
 *
 *----------------------------------------------------------------------
 */
static int
th8UnboundVerifyTh8Anchor(
    const Th8_UnboundOps *pOps,
    struct Th8_Interp *interp,
    const char *zPath)
{
    char anchorBuf[8192];
    char sigBuf[8192];
    char zSigPath[4096];
    size_t nAnchor = 0;
    size_t nSig = 0;
    size_t nPath;
    int rc = 0;

    if (!pOps->xReadFile) return 0;
    nPath = Th8_Strlen(interp, zPath);
    if (nPath + 8 > sizeof(zSigPath)) return 0; /* zPath + ".b64sig" + NUL */
    if (!pOps->xReadFile(zPath, anchorBuf, sizeof(anchorBuf), &nAnchor) ||
        nAnchor == 0) {
	return 0;
    }
    Th8_Memcpy(interp, zSigPath, zPath, nPath);
    Th8_Memcpy(interp, zSigPath + nPath, ".b64sig", 8); /* 7 chars + NUL */
    if (pOps->xReadFile(zSigPath, sigBuf, sizeof(sigBuf), &nSig) &&
        nSig > 0) {
	rc =
	    (th8VerifyAnchorSig(interp, anchorBuf, nAnchor, sigBuf, nSig) ==
	     TH8_OK);
    }
    Th8_Memset(interp, anchorBuf, 0, sizeof(anchorBuf));
    Th8_Memset(interp, sigBuf, 0, sizeof(sigBuf));
    return rc;
}
#  endif /* TH8_ENABLE_CRYPTOGRAPHY */


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
 * Why / How:
 *	This is the single shared implementation so the POSIX and Win32 DNS
 *	callbacks need not duplicate the libunbound protocol.  It runs the
 *	numbered pipeline above: validate/copy the name, create and harden a
 *	fresh per-resolution context, select a trust anchor by the ordered
 *	fall-through (env override -> managed auto-roll copy -> static -> no
 *	validation), resolve synchronously with ub_resolve, and repackage the
 *	libunbound result -- including converting its int length array to the
 *	size_t array the TH8 result type exposes -- into a heap
 *	Th8_UnboundResultImpl owned by the caller.
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
 * Results:
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
	int bHaveTa = 0;
	char *zEnv = Th8_GetEnv(interp, "TH8_DNS_ROOT_KEY");
	if (zEnv && *zEnv) {
	    /*
             * Operator override (TH8-domain).  An attacker who can set the
             * environment must not be able to point us at an unsigned or
             * hostile anchor, so the file must carry a valid TH8 signature
             * (zEnv + ".b64sig"), verified here BEFORE use.  A missing or bad
             * signature => do NOT use it: bHaveTa stays 0 and we drop to the
             * insecure mode (d) below, where the caller's require-secure check
             * fails closed.  (Without cryptography compiled in there is no
             * signing to verify against, so the file is used as-is.)
             */
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
	    if (th8UnboundVerifyTh8Anchor(pOps, interp, zEnv)) {
		bHaveTa = (ub_ctx_add_ta_file(ubctx, zEnv) == 0);
	    } else {
		TH8_TRACE_ERR(
		    NULL, "TH8_DNS_ROOT_KEY anchor has no valid TH8 "
		          "signature; ignoring it");
	    }
#  else
	    bHaveTa = (ub_ctx_add_ta_file(ubctx, zEnv) == 0);
#  endif
	    Th8_Free(interp, zEnv);
	} else {
	    char zSrc[4096];
	    int bHaveSrc = pOps->xFindStaticAnchorPath(zSrc, sizeof(zSrc));

	    if (zEnv) Th8_Free(interp, zEnv);
#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
	    /*
             * If the static search returned the TH8-BUNDLED, module-adjacent
             * anchor, it is TH8-domain and must be signature-verified before
             * we trust it: it ships next to the binary, a softer tamper target
             * than the privileged OS anchor paths (which are trusted as-is by
             * the OS permission model).  A bundled anchor with no valid
             * signature is REFUSED (fail closed), never silently downgraded.
             */
	    if (bHaveSrc && pOps->xGetModuleAnchorPath) {
		char zMod[4096];
		size_t nSrc = Th8_Strlen(interp, zSrc);

		if (pOps->xGetModuleAnchorPath(zMod, sizeof(zMod)) &&
		    Th8_Strlen(interp, zMod) == nSrc &&
		    Th8_Memcmp(interp, zSrc, zMod, nSrc) == 0 &&
		    !th8UnboundVerifyTh8Anchor(pOps, interp, zSrc)) {
		    TH8_TRACE_ERR(
		        NULL, "bundled root.key has no valid TH8 "
		              "signature; ignoring it");
		    bHaveSrc = 0;
		}
	    }
#  endif
	    /* Try the bootstrap-then-auto-roll managed copy.  Pass zSrc so a
             * first-run bootstrap has a source to copy from; later runs ignore
             * it (the managed copy already exists). */
	    bHaveTa = th8UnboundSetupManagedAnchor(
	        ubctx, pOps, bHaveSrc ? zSrc : NULL);
	    if (!bHaveTa && bHaveSrc) {
		/* Managed-mode setup failed but we have a static source --
                 * fall back to static-only mode. */
		bHaveTa = (ub_ctx_add_ta_file(ubctx, zSrc) == 0);
	    }
	}

	/* Mode (d): no trust anchor could be configured on this host (no
	 * TH8_DNS_ROOT_KEY, no managed copy, no readable system root.key).
	 * Disable the validator module so libunbound performs INSECURE
	 * resolution (bogus always reads 0) instead of initializing the
	 * validator against its compiled-in default trust-anchor path -- which
	 * fails when that path (e.g. /etc/unbound/root.key) does not exist,
	 * aborts the resolve, AND spews the failure to stderr despite
	 * ub_ctx_debugout(NULL) because it is a module-init (not query) error.
	 * The harden pass set module-config to "validator iterator"; override
	 * it to "iterator" here so the validator never initializes. */
	if (!bHaveTa) {
	    static volatile int bWarnedInsecure = 0;

	    if (ub_ctx_set_option(ubctx, "module-config:", "iterator") != 0) {
		TH8_TRACE_ERR(NULL, "module-config:");
	    }
	    /* Surface the security downgrade ONCE per process through the
	     * platform trace hook (xEmitTrace) -- not raw stderr, which an
	     * embedded library must not touch.  Th8_EmitTrace is a no-op when
	     * the embedder installed no trace callback, so this stays silent
	     * unless the host opted in.  The CAS 0->1 lets only the first
	     * resolver that hits mode (d) emit the line. */
	    if (Th8_IntCmpXchg(NULL, &bWarnedInsecure, 1, 0) == 0) {
		Th8_EmitTrace(
		    interp,
		    "th8_unbound: no DNSSEC trust anchor available; DNS "
		    "resolution is INSECURE (bogus always 0). Set "
		    "TH8_DNS_ROOT_KEY, or install the DNS root key (e.g. "
		    "unbound-anchor), to enable validation.\n");
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
    /* `secure` is 1 ONLY when libunbound cryptographically validated the
     * answer (signed + verified).  In insecure mode (no trust anchor -> the
     * validator is disabled, mode (d) above) or for an unsigned domain, both
     * secure and bogus read 0 -- callers that require end-to-end authenticity
     * (e.g. clock ntp against the DNSSEC-signed default server) must demand
     * secure, not merely !bogus. */
    pImpl->pub.secure = ubr->secure ? 1 : 0;
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
 * Why / How:
 *	The inverse of th8UnboundResolve, casting the public Th8_DnsResult
 *	back to its Th8_UnboundResultImpl wrapper and releasing each owned
 *	resource in order -- the parallel length array, the libunbound result,
 *	the libunbound context, and finally the wrapper itself.  A NULL interp
 *	or result is a no-op so callers need not guard the call.
 *
 * Parameters:
 *	interp  -- live interpreter (used for `Th8_Free`).
 *	pResult -- result returned by `th8UnboundResolve`, or
 *		NULL.
 *
 * Results:
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
