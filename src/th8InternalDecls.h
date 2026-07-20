/*
 * th8InternalDecls.h --
 *
 *	TH8 INTERNAL stubs table declarations.  Hand-maintained
 *	(not auto-generated).  Provides binary plugins like
 *	src/test/th8_testlib.c with access to TH8_INTERNAL
 *	functions that are not part of the public stubs table
 *	(libth8stub.a) because they are not exported from the
 *	shared library (visibility=hidden).
 *
 *	The table is retrieved via the public Th8_GetInternalStubs()
 *	API (declared in th8.h) and contains direct function pointers
 *	resolved at the main library's link time.  Plugins call
 *	Th8_GetInternalStubs() once at load and then invoke each
 *	internal entry through the returned table.
 *
 *	NOT for use by general extensions -- this table is part of
 *	the test/diagnostic surface and the function set is shaped
 *	by what testlib.c needs to drive MC/DC closures on otherwise-
 *	unreachable internal paths.
 *
 *	When USE_TH8_INTERNAL_STUBS is defined (e.g. by testlib.c),
 *	the lower-case th8_* names below redirect to the table.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_INTERNAL_DECLS_H
#define TH8_INTERNAL_DECLS_H

#include "th8.h"

/*
 * Forward declarations of types used only by internal-stub
 * signatures.  Th8_Parse is declared in th8.h already, so no
 * forward needed here.  Guarded so files that already include
 * th8_int.h (which defines these) don't trip C11 typedef-
 * redefinition warnings.
 */

#ifndef TH8_HAVE_CHANNEL_TYPEDEF
#  define TH8_HAVE_CHANNEL_TYPEDEF
typedef struct Th8_Channel Th8_Channel;
#endif
#ifndef TH8_HAVE_ASYNC_STATE_TYPEDEF
#  define TH8_HAVE_ASYNC_STATE_TYPEDEF
typedef struct Th8_AsyncState Th8_AsyncState;
#endif
#ifndef TH8_HAVE_NAMESPACE_TYPEDEF
#  define TH8_HAVE_NAMESPACE_TYPEDEF
typedef struct Th8_Namespace Th8_Namespace;
#endif

/*
 * The internal stubs table: a struct of function pointers for
 * every TH8_INTERNAL function that test/diagnostic plugins
 * need to call.
 */

typedef struct Th8InternalStubsTable {
    int magic;     /* Must be TH8_INTERNAL_STUBS_MAGIC. */
    int version;   /* Internal stubs table version. */

    /*
     * th8_core.c parser entry points.  th8ParseCommand has no
     * in-tree caller (declared TH8_INTERNAL with no in-tree
     * caller); this entry makes it reachable from testlib so
     * the parser's ~15 MC/DC decisions become coverable.
     * Companion th8FreeParse must be called on the Th8_Parse
     * struct to release internal allocations regardless of
     * th8ParseCommand's return code.
     */

    int (*th8_ParseCommand)(
        Th8_Interp *interp,
        const char *zScript,
        size_t nScript,
        int nLine,
        Th8_Parse *pParse);
    void (*th8_FreeParse)(Th8_Interp *interp, Th8_Parse *pParse);

    /*
     * th8_expr.c expression parser entry point.  Like
     * th8ParseCommand, declared TH8_INTERNAL with no in-tree
     * caller (the in-tree expr evaluator builds its own AST
     * directly).  Drives the expression-text -> word-list
     * tokenizer.  Th8_Parse is freed via th8FreeParse above.
     */

    int (*th8_ParseExpr)(
        Th8_Interp *interp,
        const char *zExpr,
        size_t nExpr,
        Th8_Parse *pParse);

    /*
     * th8_vars.c $name / $name(...) tokenizer entry point.
     * Declared TH8_INTERNAL with no in-tree caller.
     * Caller-allocated Th8_Value receives the parsed token;
     * lifetime is managed by the caller (no separate free).
     */

    int (*th8_ParseVarName)(
        Th8_Interp *interp,
        const char *zString,
        size_t nString,
        Th8_Value *pToken);

    /*
     * th8_plat.c platform-callback wrappers that are
     * never called from in-tree code (the codebase calls
     * the public Th8_API variants directly).  Each is a
     * thin defensive shim with multiple decision pairs --
     * unreachable without an external caller.  Exposing
     * them through the internal stubs lets testlib drive
     * the NULL-pointer + missing-callback edge cases.
     */

    void *(*th8_Memmove)(
        Th8_Interp *interp,
        void *dst,
        const void *src,
        size_t n);
    int (*th8_Strcmp)(Th8_Interp *interp, const char *s1, const char *s2);
    char *(*th8_Strchr)(Th8_Interp *interp, const char *s, int c);
    char *(*th8_Strrchr)(Th8_Interp *interp, const char *s, int c);
    int (*th8_Atoi)(Th8_Interp *interp, const char *s);
    void (*th8_Qsort)(
        Th8_Interp *interp,
        void *base,
        size_t nMemb,
        size_t size,
        int (*cmp)(const void *, const void *));
    int (*th8_Snprintf)(
        Th8_Interp *interp,
        char *buf,
        size_t size,
        const char *fmt,
        ...);
    void (*th8_MemBarrier)(Th8_Interp *interp);

    /*
     * th8_plat.c CRLF -> LF translator.  TH8_INTERNAL with no
     * in-tree callers; the in-tree CR/LF handling at the
     * Th8_GetData/Th8_TranslateEol layer uses a different
     * code path.  Drives the line-ending validation MC/DC
     * pairs.
     */

    int (*th8_TranslateLineEndings)(char *zBuf, size_t *pnBuf);

    /*
     * th8_vars.c dead-wrapper entry points.  Both declared
     * TH8_INTERNAL with no in-tree callers.
     */

    int (*th8_GetVarValue)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar,
        Th8_Value *pValue);
    int (*th8_SetVarLength)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar,
        const char *zNewData,
        size_t nNewLen);

    /*
     * th8_load.c / th8_core.c test-only token perturbers.
     * Each XORs the corresponding token with a non-zero
     * pattern so that nXxxOk != nXxxToken without zeroing
     * either, driving the (T, F) MC/DC vector at the
     * Th8_IsXxxEnabled gates.  Production callers MUST NOT
     * use these -- only scoped test interps may safely
     * tolerate the divergent state.
     */
    /*
     * Token-field XOR accessors -- the actual XOR mask lives in
     * src/test/th8_testlib.c so this slot stays generic.  Each
     * accessor is one-liner (interp->nXxxToken ^= mask) with no
     * MC/DC decisions of its own.
     */
    void (*th8_XorInterpLoadToken)(Th8_Interp *interp, th8_int64_t mask);
    void (*th8_XorInterpBigintToken)(Th8_Interp *interp, th8_int64_t mask);
    void (*th8_XorInterpSignedToken)(Th8_Interp *interp, th8_int64_t mask);
    void (*th8_XorInterpSecurePersistToken)(
        Th8_Interp *interp,
        th8_int64_t mask);
    void (
        *th8_XorInterpSecurePersistOk)(Th8_Interp *interp, th8_int64_t mask);
    void (*th8_XorInterpUnloadToken)(Th8_Interp *interp, th8_int64_t mask);
    void (*th8_ClearInterpUnloadFlags)(Th8_Interp *interp);

    /*
     * th8_load.c unload gates exposed for direct testlib
     * invocation so the (F, -) and (T, F) C1-Pair vectors at
     * th8IsUnloadDangerous L567 can be driven without
     * routing through Th8_Unload (which short-circuits when
     * unload is disabled).
     */
    int (*th8_IsUnloadEnabled)(Th8_Interp *interp);
    int (*th8_IsUnloadDangerous)(Th8_Interp *interp);

    /*
     * th8_load.c load-name match predicate.  Static C function
     * (file-scope) -- exposed so testlib can drive the C1=F
     * vector at L258 `if (nSymA > 0 && ...)` which is
     * unreachable through the normal `load LIB ?SYM?` path
     * because th8CanonLoadName always emits a "lib:sym"
     * canonical name (nSym > 0).  Direct invocation with a
     * colon-less name string drives nSymA == 0.
     */
    int (*th8_LoadNameMatch)(
        Th8_Interp *interp,
        const char *zA,
        size_t nA,
        const char *zB,
        size_t nB);

    /*
     * th8_channel.c entry point exposed so testlib can call
     * the temporary-channel constructor against a child
     * interp running a custom-platform stub xGetTemporaryData.
     * Drives the C2 / C3 vectors at L122 (rc=OK / zOsPath
     * NULL / pChannel NULL) which no in-tree caller produces.
     */
    int (*th8_ChannelCreate)(Th8_Interp *interp, size_t nSize);

    /*
     * th8_channel.c TH8_INTERNAL ops exposed for direct
     * testlib invocation (xChannelControl fallback drive).
     */
    Th8_Channel *(*th8_ChannelFind)(
        Th8_Interp *interp,
        const char *zName,
        size_t nName);
    int (*th8_ChannelWrite)(
        Th8_Interp *interp,
        Th8_Channel *pChan,
        const char *z,
        size_t n);
    int (*th8_ChannelRead)(
        Th8_Interp *interp,
        Th8_Channel *pChan,
        char **pzOut,
        size_t *pnOut);
    int (*th8_ChannelSeek)(
        Th8_Interp *interp,
        Th8_Channel *pChan,
        long offset,
        int whence);

    /*
     * th8_protect.c TH8_INTERNAL entry points.  Gated on
     * TH8_ENABLE_CRYPTOGRAPHY; entries are NULL when the
     * crypto feature is omitted.  Exposed so testlib can call
     * them against a synthesised Th8_ProtectedRegion whose
     * pPage is NULL -- the only path that drives the
     * C2-Pair (!pRegion->pPage == true) MC/DC vector at L365
     * (th8ProtectedCheckCanary) and L435 (th8ProtectedData).
     * No in-tree caller ever produces a region with NULL pPage
     * because the allocator always either succeeds (non-NULL
     * pPage) or returns NULL (the region itself never exists).
     */
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    int (*th8_ProtectedCheckCanary)(
        Th8_Interp *interp,
        const Th8_ProtectedRegion *pRegion);
    unsigned char *(*th8_ProtectedData)(const Th8_ProtectedRegion *pRegion);
#else
    void *th8_ProtectedCheckCanary;
    void *th8_ProtectedData;
#endif

    /*
     * th8_vars.c th8SetVarValue.  TH8_INTERNAL with one
     * in-tree caller (plugins/th8_variables.c:357) that
     * always passes a fully-populated Th8_Value.  Exposed
     * so testlib can drive the NULL pValue and
     * NULL-pBuffer C2-Pair vectors at L959.
     */
    int (*th8_SetVarValue)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar,
        Th8_Value *pValue);

    /*
     * th8_vars.c th8GetArrayEpoch and th8GetArrayGeneration.
     * TH8_INTERNAL with in-tree callers that gate on
     * Th8_ExistsArrayVar first, so the !pEntry vector at
     * L1543 / L1604 is unreachable through the gated path.
     * Direct invocation with a nonexistent name drives the
     * {T,C} vector and exercises the early-return-on-NULL
     * defensive branch.
     */
    int (*th8_GetArrayEpoch)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar);
    int (*th8_GetArrayGeneration)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar);

    /*
     * th8_core.c th8SetCmdToken and th8GetCmdToken.
     * TH8_INTERNAL dead wrappers with no in-tree callers.
     * Exposed so testlib can drive the (T,C) and (F,-)
     * pEntry vectors at L853 / L906 via existing + missing
     * command name lookups.
     */
    void (*th8_SetCmdToken)(
        Th8_Interp *interp,
        const char *zName,
        th8_uint64_t token);
    th8_uint64_t (*th8_GetCmdToken)(Th8_Interp *interp, const char *zName);

    /*
     * th8_core.c th8SignalEvent / th8ResetEvent /
     * th8WaitEvent / th8PStateQueueLen.  TH8_INTERNAL
     * cross-thread async-event wrappers with no in-tree
     * callers (the only documented call site is the
     * cross-thread Th8_QueueEvent path which uses xEventSet
     * directly).  All three guard their pState arg with
     * `!pState || !xEvent_ || !pEventHandle`
     * and need testlib coverage to drive the (T,-,-) pair
     * (NULL pState) plus (F,-,-) (valid pState) so the C1
     * pair closes.  The pState arg is opaque (void *) at
     * the API; the stubs accept Th8_AsyncState * which the
     * caller can satisfy with the void * returned by
     * Th8_CreateAsyncState (already used by null_guard
     * queue_event).
     */
    void (*th8_SignalEvent)(Th8_AsyncState *pState);
    void (*th8_ResetEvent)(Th8_AsyncState *pState);
    int (*th8_WaitEvent)(Th8_AsyncState *pState, int nTimeoutMs);
    int (*th8_PStateQueueLen)(Th8_AsyncState *pState);

    /*
     * th8_core.c TH8_INTERNAL dead-wrapper accessors and
     * helpers with no in-tree callers.  Exposed for testlib
     * coverage closure.
     */
    void (*th8_SetLine)(Th8_Interp *interp, int nLine);
    void (*th8_SetFrameNsPtr)(Th8_Interp *interp, void *pNs);
    int (*th8_GetFinallyRc)(Th8_Interp *interp);
    const char *(*th8_GetFinallyResult)(Th8_Interp *interp, size_t *pn);
    int (*th8_EvalTrampoline)(Th8_Interp *interp);
    int (*th8_SetResultBorrowed)(Th8_Interp *interp, const char *z, size_t n);
    int (*th8_InFrame)(
        Th8_Interp *interp,
        int (*xCall)(Th8_Interp *, void *, void *),
        void *pContext1,
        void *pContext2);

    /*
     * th8_core.c spilornis libgeneric shims.  Defined but
     * unreachable: the se_memcmp / se_strlen / se_strncmp /
     * se_strncpy macros that expand to these are defined in
     * th8_spilornis.h but no in-tree code uses them, so the
     * function bodies show as unexecuted.  Calling each from
     * testlib (under an active th8SpilornisSetup window so
     * th8_spilornis_interp is non-NULL) drives the executed
     * branches.
     */
    int (*th8_spilornis_memcmp)(const void *a, const void *b, size_t n);
    size_t (*th8_spilornis_strlen)(const char *s);
    int (*th8_spilornis_strncmp)(const char *a, const char *b, size_t n);
    char *(*th8_spilornis_strncpy)(char *d, const char *s, size_t n);

    /*
     * th8_cache.c TH8_INTERNAL Value helpers.  No in-tree
     * callers in the test path; exposed for closure.
     */
    Th8_Value *(*th8_CopyValue)(Th8_Interp *interp, const Th8_Value *pSrc);
    void (*th8_FreeValue)(Th8_Interp *interp, Th8_Value *pVal);

    /*
     * th8_cache.c th8RemoveFromCache TH8_INTERNAL.  In-tree
     * callers all pass non-NULL string args; the NULL-arg
     * defensive branch at L754 needs a direct call.
     */
    void (*th8_RemoveFromCache)(
        Th8_Interp *interp,
        int cacheType,
        const char *z,
        size_t n);

    /*
     * th8_cache.c th8FindListInCache TH8_INTERNAL.  In-tree
     * callers (list/expr) always pass valid azElem/anElem
     * with nElem >= 0; the NULL-arg + negative-nElem
     * defensives at L902/L910 need direct calls.
     */
    Th8_Value *(*th8_FindListInCache)(
        Th8_Interp *interp,
        int cacheType,
        int nElem,
        const char **azElem,
        const size_t *anElem);

    /*
     * th8_cache.c th8SetCacheString TH8_INTERNAL.  In-tree
     * callers always pass valid args; expose so testlib can
     * drive the NULL-z defensive at L1085.
     */
    void (*th8_SetCacheString)(
        Th8_Interp *interp,
        Th8_Value *pVal,
        const char *z,
        size_t n);

    /*
     * Phase 2 (2026-05-29) -- bulk expansion to cover all
     * unconditional TH8_INTERNAL helpers so testlib can drive
     * their MC/DC decisions.  Gated helpers (secure / protected /
     * NTP / fault / variable-append) deferred to a follow-up
     * batch with `#if` wrappers.  Layout MUST match the
     * initialiser order in src/th8InternalStubInit.c.
     */

    int (*th8_AnyEventQueued)(Th8_Interp *interp);
    void *(*th8_BufferAlloc)(Th8_Interp *interp, size_t nBytes);
    void (*th8_BufferFree)(Th8_Interp *interp, void *p, size_t nBytes);
    void (*th8_ChannelCleanup)(Th8_Interp *interp);
    int (*th8_ChannelClose)(
        Th8_Interp *interp,
        const char *zName,
        size_t nName);
    int (*th8_ChannelFlush)(Th8_Interp *interp, Th8_Channel *pChan);
    int (*th8_ChannelList)(Th8_Interp *interp, char **pz, size_t *pn);
    long (*th8_ChannelTell)(Th8_Channel *pChan);
    int (*th8_CheckStack)(Th8_Interp *interp);
    void (*th8_ClearCache)(Th8_Interp *interp);
    int (*th8_DrainAll)(Th8_Interp *interp, int nLimit, int *pnProcessed);
    int (*th8_DrainOneStateEvent)(Th8_AsyncState *pState, int *pbDrained);
    int (
        *th8_EventQueueAvailable)(Th8_Interp *interp, Th8_AsyncState *pState);
    void (*th8_FreePStateEvents)(Th8_AsyncState *pState);
    void *(*th8_GetCurrentNsPtr)(Th8_Interp *interp);
    int (*th8_GetFrameLevel)(Th8_Interp *interp);
    const char
        *(*th8_GetNsParent)(Th8_Interp *interp, const char *zNs, size_t nNs);
    void *(*th8_GetPlatformLibs)(Th8_Interp *interp);
    void (*th8_GlobalMutexEnter)(Th8_Interp *interp);
    void (*th8_GlobalMutexLeave)(Th8_Interp *interp);
    int (*th8_IsAlnum)(int c);
    int (*th8_IsAlpha)(int c);
    int (*th8_IsBinDig)(int c);
    int (*th8_IsDigit)(int c);
    int (*th8_IsHexDig)(int c);
    int (*th8_IsOctDig)(int c);
    int (*th8_IsSpace)(int c);
    int (*th8_IsSpecial)(int c);
#if defined(TH8_ENABLE_EXPRESSIONS)
    int (*th8_MathOp)(
        Th8_Interp *interp,
        int op,
        double *pResult,
        double a,
        double b);
#endif
#if defined(TH8_USE_MIMALLOC)
    void (*th8_MiHeapDone)(void);
    void (*th8_MiHeapInit)(void);
#endif
    int (*th8_NRInFrame)(
        Th8_Interp *interp,
        Th8_CallbackProc xCall,
        void *p0,
        void *p1,
        void *p2,
        void *p3);
    void (*th8_NotifyDeleteInterp)(Th8_Interp *interp, void *pCtx);
    void (*th8_NotifyPreDeleteInterp)(Th8_Interp *interp, void *pCtx);
    const char
        *(*th8_NsGetExport)(Th8_Interp *interp, const char *zNs, size_t nNs);
    void (*th8_OversizeString)(Th8_Interp *interp);
    int (*th8_PlatformHasEventQueue)(Th8_Interp *interp);
    void (*th8_RestoreCancel)(
        Th8_Interp *interp,
        const char savedCancel[TH8_CANCEL_SAVE_SIZE]);
    void (*th8_SaveCancel)(
        Th8_Interp *interp,
        char savedCancel[TH8_CANCEL_SAVE_SIZE]);
    void (*th8_SetAllocBytes)(Th8_Interp *interp, size_t n);
    void (*th8_SetCurrentNsPtr)(Th8_Interp *interp, void *pNs);
    void (*th8_SetFinallyState)(
        Th8_Interp *interp,
        const char *z,
        size_t n,
        int rc);
    void (*th8_SetFrameObjv)(
        Th8_Interp *interp,
        int argc,
        const char **argv,
        size_t *argl);
    void (*th8_SetPlatformLibs)(Th8_Interp *interp, void *pLibs);
    void (*th8_SignalAllStates)(Th8_Interp *interp);
    int (*th8_Step)(Th8_Interp *interp);

    /*
     * Test-only exposure of two static helpers so the testlib
     * NULL-arg / case-sweep exercisers (which now live in
     * th8_testlib.c so their own bookkeeping is immune from
     * MC/DC) can call them directly.
     */
    void (*th8_BigintCacheStore)(
        Th8_Interp *interp,
        const char *z,
        size_t n,
        const void *pSrc);
    int (*th8_IsDeviceName)(const char *z, size_t n);

    int (*th8_Vsnprintf)(
        Th8_Interp *interp,
        char *buf,
        size_t size,
        const char *fmt,
        va_list ap);

    /*
     * Phase 2 (2026-05-29) gated -- crypto-only (NTP/HTTPS time,
     * protected memory, sensitive-result finalisation).
     */

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    int (*th8_FinalizeSensitiveResult)(Th8_Interp *interp, size_t nLen);
    th8_int64_t (*th8_GetLastLocalMs)(Th8_Interp *interp);
    th8_int64_t (*th8_GetLastNtpSec)(Th8_Interp *interp);
    Th8_ProtectedRegion *(*th8_GetProtectedResultRegion)(Th8_Interp *interp);
    int (*th8_HttpsTimeQuery)(
        Th8_Interp *interp,
        const char *zUrl,
        size_t nUrl,
        th8_int64_t *pEpochSec);
    int (*th8_NtpQuery)(
        Th8_Interp *interp,
        const char **azServers,
        int nServers,
        int timeoutMs,
        int maxDisagreeSec,
        th8_int64_t *pEpochSec);
    int (*th8_ProtectedAlloc)(
        Th8_Interp *interp,
        Th8_ProtectedRegion *pRegion);
    size_t (*th8_ProtectedCanarySize)(void);
    void (
        *th8_ProtectedFree)(Th8_Interp *interp, Th8_ProtectedRegion *pRegion);
    size_t (*th8_ProtectedPageSize)(const Th8_ProtectedRegion *pRegion);
    void (*th8_SetLastLocalMs)(Th8_Interp *interp, th8_int64_t ms);
    void (*th8_SetLastNtpSec)(Th8_Interp *interp, th8_int64_t sec);
#endif /* TH8_ENABLE_CRYPTOGRAPHY */

    /*
     * Phase 2 (2026-05-29) gated -- variables + crypto (secure
     * variable store / encrypted-at-rest).
     */

#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
    void *(*th8_GetSecureKeyStore)(Th8_Interp *interp);
    Th8_Hash *(*th8_GetSecureVarHash)(Th8_Interp *interp);
    int (*th8_IsSecureVar)(Th8_Interp *interp, const char *zVar, size_t nVar);
    void (*th8_SecureFinish)(Th8_Interp *interp);
    int (*th8_SecureInit)(Th8_Interp *interp);
    int (*th8_SecureLoad)(Th8_Interp *interp, const char *zVar, size_t nVar);
    int (*th8_SecureSave)(Th8_Interp *interp, const char *zVar, size_t nVar);
    int (*th8_SecureVarCreate)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar,
        const char *zVal,
        size_t nVal);
    int (*th8_SecureVarDelete)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar);
    void (*th8_SetSecureKeyStore)(Th8_Interp *interp, void *p);
    void (*th8_SetSecureVarHash)(Th8_Interp *interp, Th8_Hash *p);
#endif /* TH8_ENABLE_VARIABLES && TH8_ENABLE_CRYPTOGRAPHY */

    /*
     * Phase 2 (2026-05-29) gated -- individual flags.  The
     * declarations in th8_int.h are unconditional, but the
     * definitions live in plugin / feature files that are
     * compiled out under the respective flag.
     */

#if defined(TH8_PLUGIN_VARIABLES)
    int (*th8_AppendInPlace)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar,
        int nArgs,
        const char **azArg,
        const size_t *anArg);
#endif

#if defined(TH8_ENABLE_FAULT_INJECTION)
    void (*th8_FaultStashSite)(
        Th8_Interp *interp,
        const char *zFile,
        int nLine);
#endif

    /*
     * Namespace helpers exposed for test-only exercisers that
     * manipulate hash-table state (e.g. tombstone-vector drives).
     */
    void (*th8_SplitQualName)(
        const char *zName,
        size_t nName,
        const char **pzNs,
        size_t *pnNs,
        const char **pzTail,
        size_t *pnTail);
    Th8_Namespace *(*th8_FindNamespace)(
        Th8_Interp *interp,
        const char *zName,
        size_t nName,
        int bCreate);

    /*
     * Th8_Interp field accessors (defined in th8_core.c).  Allow
     * test-only helpers to read and temporarily swap interp
     * fields without including th8_int_core.h.
     */
    Th8_Hash *(*th8_GetInterpCmdToken)(Th8_Interp *interp);
    void (*th8_SetInterpCmdToken)(Th8_Interp *interp, Th8_Hash *paCmdToken);
    Th8_Namespace *(*th8_GetInterpCurrentNs)(Th8_Interp *interp);
    Th8_Hash *(*th8_GetFramePaVar)(Th8_Interp *interp);
    Th8_Hash *(*th8_GetInterpPaChannels)(Th8_Interp *interp);
    Th8_Hash *(*th8_GetArraySearchHash)(Th8_Interp *interp, int bCreate);
    Th8_Hash *(*th8_GetArrayElementHash)(
        Th8_Interp *interp,
        const char *zVar,
        size_t nVar);

    /*
     * Expansion-prefix scan extracted from th8SplitCommand /
     * th8NRSubstAndBuild so testlib can drive the {}rest (T,F)
     * vector without going through the in-tree parse path,
     * which rejects "{}rest" before the substitution layer
     * sees it.  Pure I/O (no Th8_CmdBuild exposure); both
     * in-tree call sites already route through this helper,
     * so the duplicated logic is gone at the source.
     */
    int (*th8_CheckExpansionPrefix)(
        Th8_Interp *interp,
        const char *zInput,
        size_t nWord,
        int *pbExpand,
        size_t *pnTag,
        Th8_ExpansionProc *pxExpand,
        void **ppExpandCtx);

    /*
     * Th8_AsyncState bMutexReady XOR perturber (test-only).
     * The struct is opaque outside th8_int_core.h; this
     * accessor lets testlib flip bMutexReady from 1 -> 0
     * for the duration of a single defensive-guard probe,
     * then XOR it back.  Drives th8_core.c L2177 / L2250
     * (`!pState || !pState->bMutexReady`) (F,T) vector.
     */
    int (*th8_AsyncStateXorBMutexReady)(Th8_AsyncState *pState, int mask);

    /*
     * Th8_AsyncState finalize-field scrubber (test-only).
     * Zeros pEventHandle / xEventDestroy / xMutexFinal so
     * the next Th8_FinalizeAsyncState drives the (F,-) /
     * (T,F) MC/DC vectors at th8_core.c L2645 + L2651.
     * Caller does NOT restore (finalize frees pState).
     */
    void (*th8_AsyncStateScrubField)(Th8_AsyncState *pState, int field);

    /*
     * Th8_Interp pPlatform exchanger (test-only).  Returns
     * the old platform after installing the supplied (often
     * NULL) replacement.  Drives the L967 / L1585 (F,T)
     * platform-NULL defensive guards in th8MallocCommon /
     * th8ReallocCommon.  Caller MUST restore the original
     * platform immediately after the targeted call.
     */
    Th8_Platform
        *(*th8_XchgInterpPlatform)(Th8_Interp *interp, Th8_Platform *pNew);

    /*
     * th8_bigint_calloc -- libtommath calloc replacement
     * (th8_bigint.c).  Exposed so testlib can drive the
     * (F,-) (nmemb==0) and (T,T) (overflow) MC/DC vectors
     * at th8_bigint.c L107.  Calls need th8BigintSetup
     * to have installed th8_bigint_interp (otherwise the
     * function short-circuits at L103).
     */
    void *(*th8_bigint_calloc)(size_t nmemb, size_t size);

    /*
     * th8BigintSetup / th8BigintTeardown -- bracket a
     * libtommath operation by installing/clearing
     * th8_bigint_interp (a thread-local global).  Exposed
     * so testlib can wrap a direct th8_bigint_calloc call
     * to drive L107.
     */
    void (*th8_BigintSetup)(Th8_Interp *interp);
    void (*th8_BigintTeardown)(void);

    /*
     * Fault-filter helpers (th8_fault.c).  Exposed so testlib
     * can drive the NULL-input and no-separator-boundary arms
     * of th8FaultStrEqAscii L117 and th8FaultPathMatchesBaseName
     * L159 directly -- the in-tree fault-filter callers always
     * supply non-NULL pointers and separator-suffixed paths.
     */
    int (*th8_FaultStrEqAscii)(const char *a, const char *b);
    int (*th8_FaultPathMatchesBaseName)(const char *zPath, const char *zBase);

    /*
     * Posix defense-in-depth path checker (th8_posix.c).
     * Exposed so testlib can drive the L4101 5-condition
     * `.`/`..` segment-detection compound -- in-tree callers
     * pass realpath-normalized paths so the defensive arms
     * never fire in production.
     */
    int (*th8_PosixIsUnderBase)(
        const char *zAbs,
        const char *zBase,
        size_t nBase);

    /*
     * Posix sandbox path checker (th8_posix.c).  Exposed so testlib
     * can drive NULL-zPath, empty-segment, and depth-saturation
     * arms in this defensive path validator.  In-tree callers
     * never feed NULL or pathological inputs.
     */
    int (*th8_PosixIsPathUnderBase)(const char *zPath);

    /*
     * Posix unload-proc dispatcher (th8_posix.c).  Exposed so
     * testlib can drive the L1020 4-condition entry-guard
     * compound (`!interp || !hLib || !zName || nName == 0`)
     * by passing NULL/0 for each argument independently.
     * In-tree callers always pass non-NULL inputs.
     */
    void (*th8_PosixCallUnloadProc)(
        Th8_Interp *interp,
        void *hLib,
        const char *zName,
        size_t nName,
        int cbFlags);

    /*
     * Harpy policy URI prefix helper.  Exposed so testlib can
     * drive the L295/L296 2-condition compounds for `http://` /
     * `https://` prefix detection.
     */
    int (*th8_PolicyIsHttpUri)(Th8_Interp *interp, const char *z, size_t n);

    /*
     * Harpy policy date helper.  Exposed so testlib can drive
     * the L351 2-condition month-range guard (`month < 1 ||
     * month > 12`) by passing 0, 5, 13.
     */
    int (*th8_PolicyDaysInMonth)(int year, int month);

    /*
     * Secure key-store canary checker.  Exposed so testlib can
     * drive the L165 NULL-pKS arm directly.
     */
    int (*th8_SecureCheckCanary)(Th8_Interp *interp, const void *pKSv);

    /*
     * Harpy NTP insertion-sort helper.  Exposed so testlib can
     * drive the L575 2-condition compound `j >= 0 && a[j] > key`
     * by passing a small unsorted array.
     */
    void (*th8_NtpSortTimes)(th8_int64_t *a, int n);

    /*
     * Harpy attrflags hex-key parser.  Exposed so testlib can
     * drive the L443/L445/L447 hex-digit range compounds for
     * each ASCII class (0-9 / a-f / A-F) by passing short
     * targeted hex strings.
     */
    int (*th8_AfParseHexKey)(const char *z, size_t n, th8_int64_t *pKey);

    /*
     * Harpy SNK CAPI RSA key blob parser.  Exposed so testlib can
     * drive the various early-error compounds (invalid blob type,
     * unsupported version, reserved-non-zero, wrong alg, invalid
     * magic, bType/magic mismatch, invalid bit length, invalid
     * exponent) with crafted byte arrays.
     */
    int (*th8_RsaParseCapi)(
        Th8_Interp *interp,
        const unsigned char *z,
        size_t n,
        void *pKeyv);

    /*
     * Harpy HTTPS-time RSA-SHA512 signature verifier.  Exposed so
     * testlib can drive the L880 (rc != OK || !pSig || nSig == 0)
     * early-bailout compound with crafted base64 inputs.
     */
    int (*th8_HttpsTimeVerifySignature)(
        Th8_Interp *interp,
        const char *zSignedData,
        size_t nSignedData,
        const char *zSigB64,
        size_t nSigB64);

    /*
     * Harpy HTTPS-time response field locator.  Exposed so testlib
     * can drive the L822 (anElem[i] == nKey && Memcmp == 0) name-
     * match compound and the L824 (pnVal != NULL) capture path
     * against crafted name/length/value arrays.
     */
    const char *(*th8_HttpsTimeFindField)(
        Th8_Interp *interp,
        char **azElem,
        size_t *anElem,
        int nCount,
        const char *zKey,
        size_t nKey,
        size_t *pnVal);

    /*
     * th8_secure.c master-key sentinel.  Exposed so testlib can
     * drive the L1581 (!pKS || !pKS->pPage) C1-Pair (NULL keystore
     * before any secure init) and the L1587 falls-through return-0
     * (init done but master slot all zeros).
     */
    int (*th8_SecureHasMasterKey)(Th8_Interp *interp);

    /*
     * th8_policy.c lazy-init cache reset.  Exposed so testlib can
     * clear the file-scope th8_pKeyZero/Root/Test pointers
     * between fault-injection scenarios, re-triggering the
     * `if (!zData || nData == 0)` guards at L2091/L2190/L2295
     * so the (T,-) C1 and (F,T) C2 MC/DC vectors become
     * observable when nFailEmbeddedKey0/Root/Test is set on the
     * active Th8_FaultConfig.  Safe only inside coverage tests;
     * the previously cached Th8_RsaKey objects are intentionally
     * leaked per the function's contract.
     */
    void (*th8_PolicyResetCachedKeys)(void);

    /*
     * th8_policy.c signed-policy data-verification entry point.
     * Static C function (file-scope) -- exposed so testlib can
     * call it directly with NULL or zero-length zName arguments
     * to drive the L1100 `if (!zName || nName == 0)` C1=T (T,-)
     * and C2=T (F,T) MC/DC vectors that are unreachable through
     * the normal Th8_EvalFile path (which always supplies a
     * file-path zName).  Caller must supply a valid Th8_PolicyCtx
     * obtained from Th8_InstallSignedPolicy.
     */
    int (*th8_PolicyVerifyData)(
        Th8_Interp *interp,
        const char *zName,
        size_t nName,
        const char *zData,
        size_t nData,
        void *pCtx);

    /*
     * th8_attrflags.c flag-set primitive helpers.  Static C
     * functions exposed so testlib can drive the L243/L278
     * C1=F vectors `if ((unsigned char)c < 128 && ...)` --
     * unreachable through the script-level `flags change ...`
     * path because th8AfIsIdentChar filters every input char
     * to the ASCII alnum / underscore subset (always c < 128)
     * before reaching these helpers.  Direct invocation with
     * c >= 128 drives the C1=F vector.
     */
    void (*th8_AfFlagSetAdd)(Th8_FlagSet *p, char c);
    void (*th8_AfFlagSetRemove)(Th8_FlagSet *p, char c);

    /*
     * th8_attrflags.c map-get helper.  Static C function
     * exposed so testlib can drive the L393 C1=F vector
     * `if (bCreate && p->n < AF_MAX_KEYS)` in th8AfMapGet --
     * unreachable through the two in-tree callers (L621 in
     * th8AttrFlagsChange and L1110 in Th8_AttrFlagsHave),
     * both of which pass bCreate=1.  Direct invocation
     * with bCreate=0 drives C1=F.
     */
    Th8_FlagSet *(*th8_AfMapGet)(
        Th8_Interp *interp,
        Th8_AfMap *p,
        th8_int64_t key,
        int bCreate);

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /*
     * th8_snk.c test helpers.  The Th8_RsaKey struct is
     * opaque outside th8_snk.c, so testlib cannot
     * directly null/restore its zPubBlob field to drive
     * the C2-Pair (F,T) vector at L1117
     * `if (!pKey || !pKey->zPubBlob)` in
     * Th8_RsaKeyToken.  These two helpers expose the
     * required save/restore primitive: callers null
     * zPubBlob, invoke Th8_RsaKeyToken to drive the
     * (F,T) vector, then restore the saved blob so the
     * key remains usable.
     */
    void (*th8_TestRsaKeyClearPubBlob)(
        Th8_RsaKey *pKey,
        unsigned char **ppSavedBlob,
        size_t *pSavedN);
    void (*th8_TestRsaKeyRestorePubBlob)(
        Th8_RsaKey *pKey,
        unsigned char *pSavedBlob,
        size_t nSaved);
    int (*th8_NtpValidateResponse)(
        Th8_Interp *interp,
        const void *respv,
        const void *reqv,
        th8_int64_t *pEpochSec);
#endif

} Th8InternalStubsTable;

/*
 * Magic / version constants.  "TH8I" in big-endian ASCII.
 * Bumped whenever the struct shape changes.
 */

#define TH8_INTERNAL_STUBS_MAGIC   (0x54483849)  /* "TH8I" */
#define TH8_INTERNAL_STUBS_VERSION (52)

/*
 * Optional macro-redirection for plugins.  When
 * USE_TH8_INTERNAL_STUBS is defined, the lower-case names
 * below redirect to the stubs table pointer that the plugin
 * caches at init time (in a variable called th8InternalStubsPtr).
 */

#ifdef USE_TH8_INTERNAL_STUBS

extern const Th8InternalStubsTable *th8InternalStubsPtr;

#  define th8ParseCommand (th8InternalStubsPtr->th8_ParseCommand)
#  define th8FreeParse    (th8InternalStubsPtr->th8_FreeParse)
#  define th8ParseExpr    (th8InternalStubsPtr->th8_ParseExpr)
#  define th8ParseVarName (th8InternalStubsPtr->th8_ParseVarName)
#  define th8Memmove      (th8InternalStubsPtr->th8_Memmove)
#  define th8Strcmp       (th8InternalStubsPtr->th8_Strcmp)
#  define th8Strchr       (th8InternalStubsPtr->th8_Strchr)
#  define th8Strrchr      (th8InternalStubsPtr->th8_Strrchr)
#  define th8Atoi         (th8InternalStubsPtr->th8_Atoi)
#  define th8Qsort        (th8InternalStubsPtr->th8_Qsort)
#  define th8Snprintf     (th8InternalStubsPtr->th8_Snprintf)
#  define th8MemBarrier   (th8InternalStubsPtr->th8_MemBarrier)
#  define th8TranslateLineEndings                                            \
      (th8InternalStubsPtr->th8_TranslateLineEndings)
#  define th8GetVarValue        (th8InternalStubsPtr->th8_GetVarValue)
#  define th8SetVarLength       (th8InternalStubsPtr->th8_SetVarLength)
#  define th8XorInterpLoadToken (th8InternalStubsPtr->th8_XorInterpLoadToken)
#  define th8XorInterpBigintToken                                            \
      (th8InternalStubsPtr->th8_XorInterpBigintToken)
#  define th8XorInterpSignedToken                                            \
      (th8InternalStubsPtr->th8_XorInterpSignedToken)
#  define th8XorInterpSecurePersistToken                                     \
      (th8InternalStubsPtr->th8_XorInterpSecurePersistToken)
#  define th8XorInterpSecurePersistOk                                        \
      (th8InternalStubsPtr->th8_XorInterpSecurePersistOk)
#  define th8XorInterpUnloadToken                                            \
      (th8InternalStubsPtr->th8_XorInterpUnloadToken)
#  define th8ClearInterpUnloadFlags                                          \
      (th8InternalStubsPtr->th8_ClearInterpUnloadFlags)
#  define th8IsUnloadEnabled   (th8InternalStubsPtr->th8_IsUnloadEnabled)
#  define th8IsUnloadDangerous (th8InternalStubsPtr->th8_IsUnloadDangerous)
#  define th8LoadNameMatch     (th8InternalStubsPtr->th8_LoadNameMatch)
#  define th8ChannelCreate     (th8InternalStubsPtr->th8_ChannelCreate)
#  define th8ChannelFind       (th8InternalStubsPtr->th8_ChannelFind)
#  define th8ChannelWrite      (th8InternalStubsPtr->th8_ChannelWrite)
#  define th8ChannelRead       (th8InternalStubsPtr->th8_ChannelRead)
#  define th8ChannelSeek       (th8InternalStubsPtr->th8_ChannelSeek)
#  define th8ProtectedCheckCanary                                            \
      (th8InternalStubsPtr->th8_ProtectedCheckCanary)
#  define th8ProtectedData      (th8InternalStubsPtr->th8_ProtectedData)
#  define th8SetVarValue        (th8InternalStubsPtr->th8_SetVarValue)
#  define th8GetArrayEpoch      (th8InternalStubsPtr->th8_GetArrayEpoch)
#  define th8GetArrayGeneration (th8InternalStubsPtr->th8_GetArrayGeneration)
#  define th8SetCmdToken        (th8InternalStubsPtr->th8_SetCmdToken)
#  define th8GetCmdToken        (th8InternalStubsPtr->th8_GetCmdToken)
#  define th8SignalEvent        (th8InternalStubsPtr->th8_SignalEvent)
#  define th8ResetEvent         (th8InternalStubsPtr->th8_ResetEvent)
#  define th8WaitEvent          (th8InternalStubsPtr->th8_WaitEvent)
#  define th8PStateQueueLen     (th8InternalStubsPtr->th8_PStateQueueLen)
#  define th8SetLine            (th8InternalStubsPtr->th8_SetLine)
#  define th8SetFrameNsPtr      (th8InternalStubsPtr->th8_SetFrameNsPtr)
#  define th8GetFinallyRc       (th8InternalStubsPtr->th8_GetFinallyRc)
#  define th8GetFinallyResult   (th8InternalStubsPtr->th8_GetFinallyResult)
#  define th8EvalTrampoline     (th8InternalStubsPtr->th8_EvalTrampoline)
#  define th8SetResultBorrowed  (th8InternalStubsPtr->th8_SetResultBorrowed)
#  define th8InFrame            (th8InternalStubsPtr->th8_InFrame)
#  define th8_spilornis_memcmp_stub                                          \
      (th8InternalStubsPtr->th8_spilornis_memcmp)
#  define th8_spilornis_strlen_stub                                          \
      (th8InternalStubsPtr->th8_spilornis_strlen)
#  define th8_spilornis_strncmp_stub                                         \
      (th8InternalStubsPtr->th8_spilornis_strncmp)
#  define th8_spilornis_strncpy_stub                                         \
      (th8InternalStubsPtr->th8_spilornis_strncpy)
#  define th8CopyValue       (th8InternalStubsPtr->th8_CopyValue)
#  define th8FreeValue       (th8InternalStubsPtr->th8_FreeValue)
#  define th8RemoveFromCache (th8InternalStubsPtr->th8_RemoveFromCache)
#  define th8FindListInCache (th8InternalStubsPtr->th8_FindListInCache)
#  define th8SetCacheString  (th8InternalStubsPtr->th8_SetCacheString)

/* Phase 2 (2026-05-29) -- unconditional bulk expansion. */
#  define th8AnyEventQueued     (th8InternalStubsPtr->th8_AnyEventQueued)
#  define th8BufferAlloc        (th8InternalStubsPtr->th8_BufferAlloc)
#  define th8BufferFree         (th8InternalStubsPtr->th8_BufferFree)
#  define th8ChannelCleanup     (th8InternalStubsPtr->th8_ChannelCleanup)
#  define th8ChannelClose       (th8InternalStubsPtr->th8_ChannelClose)
#  define th8ChannelFlush       (th8InternalStubsPtr->th8_ChannelFlush)
#  define th8ChannelList        (th8InternalStubsPtr->th8_ChannelList)
#  define th8ChannelTell        (th8InternalStubsPtr->th8_ChannelTell)
#  define th8CheckStack         (th8InternalStubsPtr->th8_CheckStack)
#  define th8ClearCache         (th8InternalStubsPtr->th8_ClearCache)
#  define th8DrainAll           (th8InternalStubsPtr->th8_DrainAll)
#  define th8DrainOneStateEvent (th8InternalStubsPtr->th8_DrainOneStateEvent)
#  define th8EventQueueAvailable                                             \
      (th8InternalStubsPtr->th8_EventQueueAvailable)
#  define th8FreePStateEvents (th8InternalStubsPtr->th8_FreePStateEvents)
#  define th8GetCurrentNsPtr  (th8InternalStubsPtr->th8_GetCurrentNsPtr)
#  define th8GetFrameLevel    (th8InternalStubsPtr->th8_GetFrameLevel)
#  define th8GetNsParent      (th8InternalStubsPtr->th8_GetNsParent)
#  define th8GetPlatformLibs  (th8InternalStubsPtr->th8_GetPlatformLibs)
#  define th8GlobalMutexEnter (th8InternalStubsPtr->th8_GlobalMutexEnter)
#  define th8GlobalMutexLeave (th8InternalStubsPtr->th8_GlobalMutexLeave)
#  define th8IsAlnum          (th8InternalStubsPtr->th8_IsAlnum)
#  define th8IsAlpha          (th8InternalStubsPtr->th8_IsAlpha)
#  define th8IsBinDig         (th8InternalStubsPtr->th8_IsBinDig)
#  define th8IsDigit          (th8InternalStubsPtr->th8_IsDigit)
#  define th8IsHexDig         (th8InternalStubsPtr->th8_IsHexDig)
#  define th8IsOctDig         (th8InternalStubsPtr->th8_IsOctDig)
#  define th8IsSpace          (th8InternalStubsPtr->th8_IsSpace)
#  define th8IsSpecial        (th8InternalStubsPtr->th8_IsSpecial)
#  if defined(TH8_ENABLE_EXPRESSIONS)
#    define th8MathOp (th8InternalStubsPtr->th8_MathOp)
#  endif
#  if defined(TH8_USE_MIMALLOC)
#    define th8MiHeapDone (th8InternalStubsPtr->th8_MiHeapDone)
#    define th8MiHeapInit (th8InternalStubsPtr->th8_MiHeapInit)
#  endif
#  define th8NRInFrame          (th8InternalStubsPtr->th8_NRInFrame)
#  define th8NotifyDeleteInterp (th8InternalStubsPtr->th8_NotifyDeleteInterp)
#  define th8NotifyPreDeleteInterp                                           \
      (th8InternalStubsPtr->th8_NotifyPreDeleteInterp)
#  define th8NsGetExport    (th8InternalStubsPtr->th8_NsGetExport)
#  define th8OversizeString (th8InternalStubsPtr->th8_OversizeString)
#  define th8PlatformHasEventQueue                                           \
      (th8InternalStubsPtr->th8_PlatformHasEventQueue)
#  define th8RestoreCancel      (th8InternalStubsPtr->th8_RestoreCancel)
#  define th8SaveCancel         (th8InternalStubsPtr->th8_SaveCancel)
#  define th8SetAllocBytes      (th8InternalStubsPtr->th8_SetAllocBytes)
#  define th8SetCurrentNsPtr    (th8InternalStubsPtr->th8_SetCurrentNsPtr)
#  define th8SetFinallyState    (th8InternalStubsPtr->th8_SetFinallyState)
#  define th8SetFrameObjv       (th8InternalStubsPtr->th8_SetFrameObjv)
#  define th8SetPlatformLibs    (th8InternalStubsPtr->th8_SetPlatformLibs)
#  define th8SignalAllStates    (th8InternalStubsPtr->th8_SignalAllStates)
#  define th8Step               (th8InternalStubsPtr->th8_Step)
#  define th8BigintCacheStore   (th8InternalStubsPtr->th8_BigintCacheStore)
#  define th8IsDeviceName       (th8InternalStubsPtr->th8_IsDeviceName)
#  define th8SplitQualName      (th8InternalStubsPtr->th8_SplitQualName)
#  define th8FindNamespace      (th8InternalStubsPtr->th8_FindNamespace)
#  define th8GetInterpCmdToken  (th8InternalStubsPtr->th8_GetInterpCmdToken)
#  define th8SetInterpCmdToken  (th8InternalStubsPtr->th8_SetInterpCmdToken)
#  define th8GetInterpCurrentNs (th8InternalStubsPtr->th8_GetInterpCurrentNs)
#  define th8GetFramePaVar      (th8InternalStubsPtr->th8_GetFramePaVar)
#  define th8GetInterpPaChannels                                             \
      (th8InternalStubsPtr->th8_GetInterpPaChannels)
#  define th8GetArraySearchHash (th8InternalStubsPtr->th8_GetArraySearchHash)
#  define th8GetArrayElementHash                                             \
      (th8InternalStubsPtr->th8_GetArrayElementHash)
#  define th8CheckExpansionPrefix                                            \
      (th8InternalStubsPtr->th8_CheckExpansionPrefix)
#  define th8AsyncStateXorBMutexReady                                        \
      (th8InternalStubsPtr->th8_AsyncStateXorBMutexReady)
#  define th8AsyncStateScrubField                                            \
      (th8InternalStubsPtr->th8_AsyncStateScrubField)
#  define th8XchgInterpPlatform (th8InternalStubsPtr->th8_XchgInterpPlatform)
#  define th8_bigint_calloc     (th8InternalStubsPtr->th8_bigint_calloc)
#  define th8BigintSetup        (th8InternalStubsPtr->th8_BigintSetup)
#  define th8BigintTeardown     (th8InternalStubsPtr->th8_BigintTeardown)
#  define th8FaultStrEqAscii    (th8InternalStubsPtr->th8_FaultStrEqAscii)
#  define th8FaultPathMatchesBaseName                                        \
      (th8InternalStubsPtr->th8_FaultPathMatchesBaseName)
#  define th8PosixIsUnderBase (th8InternalStubsPtr->th8_PosixIsUnderBase)
#  define th8PosixIsPathUnderBase                                            \
      (th8InternalStubsPtr->th8_PosixIsPathUnderBase)
#  define th8PosixCallUnloadProc                                             \
      (th8InternalStubsPtr->th8_PosixCallUnloadProc)
#  define th8PolicyIsHttpUri   (th8InternalStubsPtr->th8_PolicyIsHttpUri)
#  define th8PolicyDaysInMonth (th8InternalStubsPtr->th8_PolicyDaysInMonth)
#  define th8SecureCheckCanary (th8InternalStubsPtr->th8_SecureCheckCanary)
#  define th8NtpSortTimes      (th8InternalStubsPtr->th8_NtpSortTimes)
#  define th8AfParseHexKey     (th8InternalStubsPtr->th8_AfParseHexKey)
#  define th8RsaParseCapi      (th8InternalStubsPtr->th8_RsaParseCapi)
#  define th8HttpsTimeVerifySignature                                        \
      (th8InternalStubsPtr->th8_HttpsTimeVerifySignature)
#  define th8HttpsTimeFindField (th8InternalStubsPtr->th8_HttpsTimeFindField)
#  define th8SecureHasMasterKey (th8InternalStubsPtr->th8_SecureHasMasterKey)
#  define th8PolicyResetCachedKeys                                           \
      (th8InternalStubsPtr->th8_PolicyResetCachedKeys)
#  define th8PolicyVerifyData (th8InternalStubsPtr->th8_PolicyVerifyData)
#  define th8AfFlagSetAdd     (th8InternalStubsPtr->th8_AfFlagSetAdd)
#  define th8AfFlagSetRemove  (th8InternalStubsPtr->th8_AfFlagSetRemove)
#  define th8AfMapGet         (th8InternalStubsPtr->th8_AfMapGet)
#  define th8Vsnprintf        (th8InternalStubsPtr->th8_Vsnprintf)

#  if defined(TH8_ENABLE_CRYPTOGRAPHY)
#    define th8TestRsaKeyClearPubBlob                                        \
	(th8InternalStubsPtr->th8_TestRsaKeyClearPubBlob)
#    define th8TestRsaKeyRestorePubBlob                                      \
	(th8InternalStubsPtr->th8_TestRsaKeyRestorePubBlob)
#    define th8NtpValidateResponse                                           \
	(th8InternalStubsPtr->th8_NtpValidateResponse)
#    define th8FinalizeSensitiveResult                                       \
	(th8InternalStubsPtr->th8_FinalizeSensitiveResult)
#    define th8GetLastLocalMs (th8InternalStubsPtr->th8_GetLastLocalMs)
#    define th8GetLastNtpSec  (th8InternalStubsPtr->th8_GetLastNtpSec)
#    define th8GetProtectedResultRegion                                      \
	(th8InternalStubsPtr->th8_GetProtectedResultRegion)
#    define th8HttpsTimeQuery (th8InternalStubsPtr->th8_HttpsTimeQuery)
#    define th8NtpQuery       (th8InternalStubsPtr->th8_NtpQuery)
#    define th8ProtectedAlloc (th8InternalStubsPtr->th8_ProtectedAlloc)
#    define th8ProtectedCanarySize                                           \
	(th8InternalStubsPtr->th8_ProtectedCanarySize)
#    define th8ProtectedFree     (th8InternalStubsPtr->th8_ProtectedFree)
#    define th8ProtectedPageSize (th8InternalStubsPtr->th8_ProtectedPageSize)
#    define th8SetLastLocalMs    (th8InternalStubsPtr->th8_SetLastLocalMs)
#    define th8SetLastNtpSec     (th8InternalStubsPtr->th8_SetLastNtpSec)
#  endif

#  if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
#    define th8GetSecureKeyStore (th8InternalStubsPtr->th8_GetSecureKeyStore)
#    define th8GetSecureVarHash  (th8InternalStubsPtr->th8_GetSecureVarHash)
#    define th8IsSecureVar       (th8InternalStubsPtr->th8_IsSecureVar)
#    define th8SecureFinish      (th8InternalStubsPtr->th8_SecureFinish)
#    define th8SecureInit        (th8InternalStubsPtr->th8_SecureInit)
#    define th8SecureLoad        (th8InternalStubsPtr->th8_SecureLoad)
#    define th8SecureSave        (th8InternalStubsPtr->th8_SecureSave)
#    define th8SecureVarCreate   (th8InternalStubsPtr->th8_SecureVarCreate)
#    define th8SecureVarDelete   (th8InternalStubsPtr->th8_SecureVarDelete)
#    define th8SetSecureKeyStore (th8InternalStubsPtr->th8_SetSecureKeyStore)
#    define th8SetSecureVarHash  (th8InternalStubsPtr->th8_SetSecureVarHash)
#  endif

#  if defined(TH8_PLUGIN_VARIABLES)
#    define th8AppendInPlace (th8InternalStubsPtr->th8_AppendInPlace)
#  endif

#  if defined(TH8_ENABLE_FAULT_INJECTION)
#    define th8FaultStashSite (th8InternalStubsPtr->th8_FaultStashSite)
#  endif

#endif /* USE_TH8_INTERNAL_STUBS */

#endif /* TH8_INTERNAL_DECLS_H */
