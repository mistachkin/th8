/*
 * th8_unbound.h -- Internal shared interface for the libunbound
 *	DNSSEC-validating resolver integration.
 *
 *	Owned by `src/th8_unbound.c`; included only by the platform
 *	implementations (`src/th8_posix.c`, `src/th8_win32.c`) that
 *	provide a `Th8_UnboundOps` instance.  Not part of the public
 *	API and intentionally NOT pulled into `th8.h`.
 *
 *	The header avoids `#include <unbound.h>` -- the only
 *	libunbound type it needs (`struct ub_ctx`) is forward-declared
 *	so the header can be included by files that have no
 *	libunbound build dependency themselves.
 *
 * Gated on TH8_ENABLE_UNBOUND -- a build that does not enable
 * libunbound never compiles `src/th8_unbound.c` and never includes
 * this header from anywhere.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_UNBOUND_H
#define TH8_UNBOUND_H

#if defined(TH8_ENABLE_UNBOUND)

#  include <stddef.h>

struct Th8_Interp;
struct Th8_DnsResult;
struct ub_ctx;

/*
 *----------------------------------------------------------------------
 *
 * Th8_UnboundOps --
 *
 *	Platform-operations vtable consumed by the shared
 *	`th8_unbound.c` driver.  Each platform implementation
 *	(POSIX, Win32) provides a single static instance of this
 *	struct populated with its native helpers; the shared
 *	driver dispatches through the vtable to keep its logic
 *	platform-agnostic.
 *
 *	Field contracts -- every function returns 1 on success
 *	and 0 on failure; failures are silent so the shared
 *	driver can walk a search chain without spurious noise
 *	when an expected fallback simply does not exist.
 *
 *	xFindStaticAnchorPath
 *	    Compose and validate a candidate static
 *	    trust-anchor path.  Walks the platform's
 *	    conventional install-location search list (TH8
 *	    module directory first, then OS-conventional
 *	    system paths) and returns the first readable
 *	    candidate in `zBuf`.  Used as the SOURCE for the
 *	    bootstrap-then-auto-roll flow in
 *	    `th8UnboundSetupManagedAnchor`.
 *
 *	xGetManagedAnchorPath
 *	    Compose the path to the per-user WRITABLE managed
 *	    copy of the trust anchor.  This is the file
 *	    libunbound's `ub_ctx_add_ta_autr` reads on every
 *	    resolve and writes state-machine updates to as the
 *	    RFC 5011 hold-down timer advances.  Returns 0 when
 *	    no per-user location is available (no `HOME`, no
 *	    `%APPDATA%`).
 *
 *	xPathReadable
 *	    Predicate: is `zPath` an existing file the calling
 *	    process can open for reading?  Cheap test so the
 *	    shared driver can skip non-existent candidates
 *	    without invoking libunbound.
 *
 *	xEnsureParentDir
 *	    Ensure every parent directory of `zPath` exists.
 *	    The basename of `zPath` is ignored.  Must create
 *	    directories with per-user-only permissions (POSIX
 *	    mode `0700`; Win32 inherited user-only ACL) so the
 *	    managed trust anchor is not exposed to other local
 *	    users on shared systems.
 *
 *	xCopyFileContents
 *	    Atomically copy `zSrc` to `zDst`, creating `zDst`
 *	    with per-user-only permissions (POSIX mode `0600`;
 *	    Win32 inherited user-only ACL).  Partial copies
 *	    must be removed on failure so the shared driver
 *	    never feeds libunbound a truncated trust-anchor
 *	    file.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_UnboundOps {
    int (*xFindStaticAnchorPath)(char *zBuf, size_t nBuf);
    int (*xGetManagedAnchorPath)(char *zBuf, size_t nBuf);
    int (*xPathReadable)(const char *zPath);
    int (*xEnsureParentDir)(const char *zPath);
    int (*xCopyFileContents)(const char *zSrc, const char *zDst);
} Th8_UnboundOps;

/*
 *----------------------------------------------------------------------
 *
 * Function declarations -- full per-function documentation lives
 * with the definitions in `src/th8_unbound.c`.
 *
 *----------------------------------------------------------------------
 */

void th8UnboundHardenCtx(struct ub_ctx *ubctx);

int th8UnboundSetupManagedAnchor(
    struct ub_ctx *ubctx,
    const Th8_UnboundOps *pOps,
    const char *zSrcStatic);

int th8UnboundResolve(
    struct Th8_Interp *interp,
    const Th8_UnboundOps *pOps,
    const char *zName,
    size_t nName,
    int eType,
    struct Th8_DnsResult **ppResult);

void th8UnboundResolveFree(
    struct Th8_Interp *interp,
    struct Th8_DnsResult *pResult);

#endif /* TH8_ENABLE_UNBOUND */

#endif /* TH8_UNBOUND_H */
