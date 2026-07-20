/*
 * th8_int.h --
 *
 *	TH8 internal header.  Contains definitions used by the TH8
 *	core implementation files (th8_core.c, th8_lang.c, th8_regex.c,
 *	th8_util.c) but NOT needed by extensions or embedders.
 *
 *	Extensions should include only th8.h (and optionally
 *	th8Decls.h for stubs).  This file is NOT part of the
 *	public API.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_INT_H
#define TH8_INT_H

/*
 * NOTE: This header does not include other project headers.
 * Each .c file must include th8.h before th8_int.h.
 */

/*
 *----------------------------------------------------------------------
 *
 * TH8_INTERNAL --
 *
 *	Decoration for functions that are shared across compilation
 *	units within the TH8 library but are NOT part of the public
 *	API.  These are not exported from the shared library and are
 *	not available through the stubs table.
 *
 *	On GCC/Clang with -fvisibility=hidden, this explicitly marks
 *	the symbol as hidden (redundant with the flag but documents
 *	intent).  On Windows, it is a plain extern (no dllexport).
 *
 *----------------------------------------------------------------------
 */

#ifndef TH8_INTERNAL
#  if defined(__GNUC__) && __GNUC__ >= 4
#    define TH8_INTERNAL __attribute__((visibility("hidden")))
#  else
#    define TH8_INTERNAL extern
#  endif
#endif

/*
 *----------------------------------------------------------------------
 *
 * TH8_TLS --
 *
 *	Thread-local storage qualifier.  Compiles to __declspec(thread)
 *	on MSVC and __thread on GCC/Clang.  Used for module-private
 *	per-thread state (e.g. the per-thread mimalloc heap pointer).
 *
 *----------------------------------------------------------------------
 */

#ifndef TH8_TLS
#  if defined(_MSC_VER)
#    define TH8_TLS __declspec(thread)
#  elif defined(__GNUC__) || defined(__clang__)
#    define TH8_TLS __thread
#  else
#    error                                                                    \
	"TH8_TLS: no thread-local-storage qualifier known for this compiler"
#  endif
#endif

/*
 *----------------------------------------------------------------------
 *
 * Mimalloc per-thread heap helpers (th8_mimalloc.c) --
 *
 *	Defined only when TH8_USE_MIMALLOC is set.  Called by
 *	Th8_ThreadInit and Th8_ThreadDone to manage the calling
 *	thread's dedicated mimalloc heap.  Direct callers outside
 *	those two routines should not use these -- go through the
 *	public Th8_ThreadInit / Th8_ThreadDone API instead.
 *
 *----------------------------------------------------------------------
 */

#if defined(TH8_USE_MIMALLOC)
TH8_INTERNAL void th8MiHeapInit(void);
TH8_INTERNAL void th8MiHeapDone(void);
#endif

/*
 * NOTE: This is the flag set by Th8_EvalTrusted to signal to the
 *       policy callback(s) that the script is implicitly trusted
 *       by the embedder.  It should be noted that this only works
 *       if there is no script origin set.
 */

#define TH8_EVAL_TRUSTED ((int)0x01)

/*
 * Forward declarations for internal struct types.  Full definitions
 * live in th8_int_core.h, included after this header in every .c
 * file that needs to dereference them.
 */
#ifndef TH8_HAVE_ASYNC_STATE_TYPEDEF
#  define TH8_HAVE_ASYNC_STATE_TYPEDEF
typedef struct Th8_AsyncState Th8_AsyncState;
#endif

/* ====================================================================
 * Internal platform wrappers (th8_plat.c)
 * ==================================================================== */

/* Platform memmove -- internal */
TH8_INTERNAL void *
th8Memmove(Th8_Interp *interp, void *dst, const void *src, size_t n);
/* Platform strcmp -- internal */
TH8_INTERNAL int
th8Strcmp(Th8_Interp *interp, const char *s1, const char *s2);
/* Platform strchr -- internal */
TH8_INTERNAL char *th8Strchr(Th8_Interp *interp, const char *s, int c);
/* Platform strchr -- internal */
TH8_INTERNAL char *th8Strrchr(Th8_Interp *interp, const char *s, int c);
/* Platform atoi -- internal */
TH8_INTERNAL int th8Atoi(Th8_Interp *interp, const char *s);
/* Platform qsort -- internal */
TH8_INTERNAL void th8Qsort(
    Th8_Interp *interp,
    void *base,
    size_t nmemb,
    size_t size,
    int (*cmp)(const void *, const void *));
/* Platform vsnprintf -- internal */
TH8_INTERNAL int th8Vsnprintf(
    Th8_Interp *interp,
    char *buf,
    size_t size,
    const char *fmt,
    va_list ap);
/* Platform snprintf -- internal */
TH8_INTERNAL int
th8Snprintf(Th8_Interp *interp, char *buf, size_t size, const char *fmt, ...);
/* Full memory barrier via platform callback -- internal */
TH8_INTERNAL void th8MemBarrier(Th8_Interp *interp);

/* ====================================================================
 * Global mutex (th8_plat.c)
 * ==================================================================== */

/* Enter process-global mutex -- internal */
TH8_INTERNAL void th8GlobalMutexEnter(Th8_Interp *interp);
/* Leave process-global mutex -- internal */
TH8_INTERNAL void th8GlobalMutexLeave(Th8_Interp *interp);

/* ====================================================================
 * Event queue + per-pState manual-reset event handle (th8_core.c)
 *
 * The event queue is per-Th8_AsyncState (see th8_int_core.h): each
 * pState owns its own queue, mutex, and signal handle.  Cross-thread
 * helpers therefore take a pState pointer, not an interp -- by the
 * time these are called the cached function pointers and signal
 * handle on the pState are everything they need.
 *
 * th8EventQueueAvailable validates the platform's required event
 * callbacks AND populates the pState's cached pointer set in one
 * step.  Call it from Th8_CreateAsyncState only.
 *
 * th8SignalEvent / th8ResetEvent / th8WaitEvent wrap the
 * pState->pEventHandle through pState's cached xEvent* pointers.
 *
 * th8DrainOneStateEvent pops one callback from a pState's queue
 * and invokes it (mutex released across the call).  th8DrainAll
 * walks every live (non-finalized) pState on an interp and drains
 * each in FIFO order; this is what Th8_DrainQueueEvents wraps.
 * th8AnyEventQueued is the predicate used by [vwait]'s wait loop.
 *
 * th8FreePStateEvents drains any remaining events on a pState
 * WITHOUT invoking callbacks (used by Th8_FinalizeAsyncState and
 * Th8_DeleteInterp's teardown).
 *
 * All these are internal-only.  The public surface is just
 * Th8_QueueEvent + Th8_DrainQueueEvents.
 * ==================================================================== */

TH8_INTERNAL int
th8EventQueueAvailable(Th8_Interp *interp, Th8_AsyncState *pState);
TH8_INTERNAL int th8PlatformHasEventQueue(Th8_Interp *interp);
TH8_INTERNAL void th8SignalEvent(Th8_AsyncState *pState);
TH8_INTERNAL void th8ResetEvent(Th8_AsyncState *pState);
TH8_INTERNAL int th8WaitEvent(Th8_AsyncState *pState, int nTimeoutMs);
TH8_INTERNAL int
th8DrainOneStateEvent(Th8_AsyncState *pState, int *pbDrained);
TH8_INTERNAL int
th8DrainAll(Th8_Interp *interp, int nLimit, int *pnProcessed);
TH8_INTERNAL int th8AnyEventQueued(Th8_Interp *interp);
TH8_INTERNAL int th8PStateQueueLen(Th8_AsyncState *pState);
TH8_INTERNAL void th8FreePStateEvents(Th8_AsyncState *pState);
TH8_INTERNAL void th8SignalAllStates(Th8_Interp *interp);

/*
 * TH8_CHECK_EVENT_CALLBACKS(p) --
 *
 *	Predicate macro: non-zero if struct `p` has every platform
 *	function pointer required for cross-thread event-queue use.
 *	Works on either Th8_Platform OR Th8_AsyncState because the
 *	field names match.  Used by th8EventQueueAvailable to fail
 *	the create call when the platform is incomplete, and inside
 *	Th8_QueueEvent's pState validation.
 */
#define TH8_CHECK_EVENT_CALLBACKS(p)                                         \
    ((p)->xMalloc != NULL && (p)->xFree != NULL &&                           \
     (p)->xMutexInit != NULL && (p)->xMutexFinal != NULL &&                  \
     (p)->xMutexEnter != NULL && (p)->xMutexLeave != NULL &&                 \
     (p)->xEventCreate != NULL && (p)->xEventDestroy != NULL &&              \
     (p)->xEventSet != NULL && (p)->xEventReset != NULL &&                   \
     (p)->xEventWait != NULL && (p)->xIntCmpXchg != NULL)

/* ====================================================================
 * Character classification (th8_core.c)
 * ==================================================================== */

/* Decimal digit [0-9] -- internal */
TH8_INTERNAL int th8IsDigit(int c);
/* Whitespace -- internal */
TH8_INTERNAL int th8IsSpace(int c);
/* Alphanumeric [a-zA-Z0-9] -- internal */
TH8_INTERNAL int th8IsAlnum(int c);
/* Alphabetic [a-zA-Z] -- internal */
TH8_INTERNAL int th8IsAlpha(int c);
/* Tcl special char -- internal */
TH8_INTERNAL int th8IsSpecial(int c);
/* Hexadecimal digit [0-9a-fA-F] -- internal */
TH8_INTERNAL int th8IsHexDig(int c);
/* Octal digit [0-7] -- internal */
TH8_INTERNAL int th8IsOctDig(int c);
/* Binary digit [0-1] -- internal */
TH8_INTERNAL int th8IsBinDig(int c);

/*
 * Stolen from Tcl: STRINGIFY takes an argument and wraps it in ""
 * (double-quotation marks), JOIN joins two arguments, in theory.
 */

#ifndef STRINGIFY
#  define STRINGIFY(x)  STRINGIFY1(x)
#  define STRINGIFY1(x) #x
#endif

#ifndef JOIN
#  define JOIN(a, b)  JOIN1(a, b)
#  define JOIN1(a, b) a##b
#endif

/*
 * Generated version information.  If the header doesn't exist
 * (e.g., building without Make), provide defaults.
 */

#if __has_include("th8_version_gen.h")
#  include "th8_version_gen.h"
#else
#  ifndef TH8_SOURCE_ID
#    define TH8_SOURCE_ID "unknown"
#  endif
#  ifndef TH8_SOURCE_TIMESTAMP
#    define TH8_SOURCE_TIMESTAMP "unknown"
#  endif
#  ifndef TH8_SOURCE_TAGS
#    define TH8_SOURCE_TAGS "unknown"
#  endif
#  ifndef TH8_PATCH_LEVEL
#    define TH8_PATCH_LEVEL "0.0.0"
#  endif
#endif


/*
 *----------------------------------------------------------------------
 *
 * Safe arithmetic macros --
 *
 *	Overflow-safe size_t multiplication and addition.  Used by
 *	allocation size calculations to prevent wraparound.  These
 *	are internal to the core and not part of the public API.
 *
 *----------------------------------------------------------------------
 */

/*
 * Returns 1 if a*b would overflow size_t; stores result in *pOut.
 * On the overflow path, *pOut is unconditionally set to 0 so callers
 * (and MSVC's flow analyzer) see *pOut as definitely written on every
 * path.  Callers must still check the return value -- the 0 sentinel
 * is just a defensive default, not a meaningful size.
 */
#define TH8_SAFE_MUL_SIZE(a, b, pOut)                                        \
    ((b) != 0 && (a) > (size_t)-1 / (b) ? (*(pOut) = 0, 1)                   \
                                        : (*(pOut) = (a) * (b), 0))

/*
 * Returns 1 if a+b would overflow size_t; stores result in *pOut.
 * On overflow, *pOut is set to 0 (see TH8_SAFE_MUL_SIZE for the
 * always-write rationale).
 */
#define TH8_SAFE_ADD_SIZE(a, b, pOut)                                        \
    ((a) > (size_t)-1 - (b) ? (*(pOut) = 0, 1) : (*(pOut) = (a) + (b), 0))

/*
 * TH8_ALLOC / TH8_ALLOC_STR --
 *
 *	Overflow-checked, traceable allocation macros.  These validate
 *	the requested size against TH8_MX_ALLOC, invoke the platform
 *	allocator, and try the xNeedMemory second-chance callback on
 *	failure.  In debug builds, allocation failures are traced with
 *	__FILE__ and __LINE__ of the call site.
 *
 *	TH8_ALLOC(interp, nByte)   -- allocate nByte bytes.
 *	TH8_ALLOC_STR(interp, nLen) -- allocate nLen+1 bytes (overflow-safe).
 */

/*
 * Safe allocation functions are declared in th8.h as public API
 * (Th8_SafeAlloc, Th8_SafeAllocStr, etc.).  The TH8_ALLOC*
 * macros below call through to the public names.
 */

/*
 * TH8_ALLOC(interp, nByte) --
 *     Overflow-checked allocation (pre-computed size).
 *
 * TH8_ALLOC_STR(interp, nLen) --
 *     Allocate nLen+1 bytes (overflow-safe for NUL terminator).
 *
 * TH8_ALLOC_MUL(interp, a, b) --
 *     Allocate a*b bytes (rejects if a*b overflows).
 *
 * TH8_ALLOC_ADD(interp, a, b) --
 *     Allocate a+b bytes (rejects if a+b overflows).
 *
 * TH8_ALLOC_MUL_ADD(interp, a, b, c) --
 *     Allocate a*b+c bytes (rejects if a*b or a*b+c overflows).
 *     This is the most common pattern: nElem * sizeof(T) + extra.
 */

/* TH8_ALLOC* macros are defined in th8.h (public API). */

/*
 * th8CleanupPackages --
 *	Free all package registry data during interpreter deletion.
 *	Defined in th8_extensibility.c.
 */
#if defined(TH8_PLUGIN_EXTENSIBILITY)
void th8CleanupPackages(Th8_Interp *interp);
#endif
#if defined(TH8_ENABLE_EXPRESSIONS)
void th8RegisterMathFuncs(Th8_Interp *interp);
#endif

/*
 * th8SubstWord -- perform variable/command/backslash substitution
 * on a single word.  Defined in th8_core.c, called by th8_expr.c
 * for expression operand evaluation.
 */
int th8SubstWord(
    Th8_Interp *interp,
    const char *zWord,
    size_t nWord,
    const char *zName,
    size_t nName);

/*
 * Variable subsystem (th8_vars.c).
 * th8FreeVarEntry: hash callback for frame/namespace variable cleanup.
 * th8SubstVarName: perform $ variable substitution in a word.
 * Both gated on TH8_ENABLE_VARIABLES.
 */
/*
 * Hash iteration callback: append entry keys to a Tcl list.
 * Defined in th8_core.c, used by th8_vars.c for variable listing.
 */
int th8AppendHashKeys(Th8_HashEntry *pEntry, void *pVoid);

/*
 * th8EvalCleanup -- NRE callback: free pData[0] and propagate rc.
 * Defined in th8_control.c, used by th8_procedures.c.
 */
int th8EvalCleanup(Th8_Interp *interp, void *pData[], int rc);

#if defined(TH8_ENABLE_VARIABLES)
int th8FreeVarEntry(Th8_HashEntry *pEntry, void *pVoid);
int th8SubstVarName(Th8_Interp *interp, const char *z, size_t n);
#endif

/*
 * Tokenizer helpers used by both the core parser and the
 * expression tokenizer (th8_expr.c).
 */
int th8NextVarName(Th8_Interp *interp, const char *z, size_t n, size_t *pLen);
int th8NextCommand(Th8_Interp *interp, const char *z, size_t n, size_t *pLen);

/*
 *----------------------------------------------------------------------
 *
 * TH8_THREAD_LOCAL --
 *
 *	Thread-local storage qualifier.  Used for per-thread global
 *	state in the regex, bigint, and Spilornis bridges.
 *
 *----------------------------------------------------------------------
 */

#ifndef TH8_THREAD_LOCAL
#  if defined(_MSC_VER)
#    define TH8_THREAD_LOCAL __declspec(thread)
#  elif defined(__GNUC__) || defined(__clang__)
#    define TH8_THREAD_LOCAL __thread
#  else
#    define TH8_THREAD_LOCAL  /* fallback: no TLS */
#  endif
#endif

/*
 *----------------------------------------------------------------------
 *
 * th8MaybeGlobalMutexEnter / th8MaybeGlobalMutexLeave --
 *
 *	On platforms without real TLS (where TH8_THREAD_LOCAL is
 *	empty), the global mutex must be held around access to
 *	thread-local-like globals (regex bridge, bigint bridge,
 *	Spilornis bridge).  On platforms with real TLS, these are
 *	no-ops since each thread has its own storage.
 *
 *----------------------------------------------------------------------
 */

#ifndef th8MaybeGlobalMutexEnter
#  if defined(_MSC_VER)
#    define th8MaybeGlobalMutexEnter(interp) th8GlobalMutexEnter(interp)
#    define th8MaybeGlobalMutexLeave(interp) th8GlobalMutexLeave(interp)
#  elif defined(__GNUC__) || defined(__clang__)
#    define th8MaybeGlobalMutexEnter(interp)
#    define th8MaybeGlobalMutexLeave(interp)
#  else
#    define th8MaybeGlobalMutexEnter(interp) th8GlobalMutexEnter(interp)
#    define th8MaybeGlobalMutexLeave(interp) th8GlobalMutexLeave(interp)
#  endif
#endif

/*
 *----------------------------------------------------------------------
 *
 * Bigint memory bridge (TH8_ENABLE_BIGINT) --
 *
 *	These functions route libtommath allocations through the
 *	TH8 platform allocator.  Declared here so the tommath
 *	amalgamation can reference them via MP_MALLOC etc.
 *
 *----------------------------------------------------------------------
 */

#if defined(TH8_ENABLE_BIGINT)
void *th8_bigint_malloc(size_t n);
void *th8_bigint_calloc(size_t nmemb, size_t size);
void *th8_bigint_realloc(void *mem, size_t oldsize, size_t newsize);
void th8_bigint_free(void *mem, size_t size);
void th8BigintDestroy(Th8_Interp *interp, Th8_Bigint *pBigint);

/*
 * th8BigintSetup / th8BigintTeardown bracket every libtommath
 * operation: setup stores the interp for the allocator bridge (and
 * acquires the global mutex), teardown clears it (and releases the
 * mutex).  Exposed so the Th8_ToDouble halfway-case exact-comparison
 * fallback in th8_core.c can bracket its own mp_* usage when bigint
 * is enabled at runtime for the interpreter.
 */
void th8BigintSetup(Th8_Interp *interp);
void th8BigintTeardown(void);
#endif

/*
 *----------------------------------------------------------------------
 *
 * Internal-representation cache internals --
 *
 *	Th8_CacheEntry is the per-entry structure stored in the
 *	per-interpreter cache hash table (Th8_Interp.paCache).
 *	The cache owns all memory reachable from the entry.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_CacheEntry Th8_CacheEntry;
struct Th8_CacheEntry {
    int cacheType;  /* TH8_CACHE_* that was requested. */
    char *zOriginal;  /* Copy of original input (owned). */
    size_t nOriginal;  /* Byte length of zOriginal. */
    Th8_Value value;  /* The cached value (inline). */
    /*
     * For TH8_CACHE_LIST: the split-list result in Th8_SplitList
     * format so callers can borrow it directly.
     */
    /*
     * For TH8_CACHE_LIST: cached split-list element arrays.
     * These mirror the Th8_Value.u.splitlist fields but live
     * outside the union so that they survive when a different
     * union member is active.  Owned by the cache entry.
     */
    char **azListElem;  /* Element string pointers (owned). */
    size_t *anListElem;  /* Element byte lengths. */
    int nListElem;  /* Number of elements. */
};

/*
 * Cache lifecycle (called from Th8_CreateInterp / Th8_DeleteInterp).
 */
void th8CacheInit(Th8_Interp *interp);
void th8CacheFinish(Th8_Interp *interp);

/*
 * Cache accessor helpers (defined in th8_core.c where the full
 * Th8_Interp layout is visible; called from th8_cache.c).
 */
Th8_Hash **th8CacheHashPtr(Th8_Interp *interp);
int th8CacheMutexReady(Th8_Interp *interp);
void th8CacheMutexLock(Th8_Interp *interp);
void th8CacheMutexUnlock(Th8_Interp *interp);
void th8CacheMutexSetup(Th8_Interp *interp);
void th8CacheMutexTeardown(Th8_Interp *interp);

/*
 * Namespace helpers (defined in th8_core.c, used by th8_lang.c).
 */

void th8SplitQualName(
    const char *zName,
    size_t nName,
    const char **pzNs,
    size_t *pnNs,
    const char **pzTail,
    size_t *pnTail);

/*
 * th8ResolveNsPattern --
 *
 *	Resolve a possibly namespace-qualified pattern for use by
 *	info commands/vars/procs.  Handles both "::ns::tail" (fully
 *	qualified) and "ns::tail" (relative, prepended with "::").
 *
 *	On output:
 *	  *pzNs / *pnNs   = namespace path (e.g., "::foo")
 *	  *pzTail / *pnTail = tail pattern (e.g., "*")
 *	  *pzBuf           = allocated buffer (caller frees), or
 *	                     NULL if no allocation was needed
 *
 *	Returns non-zero if the pattern was namespace-qualified,
 *	zero if it was a simple unqualified pattern.
 */
int th8ResolveNsPattern(
    Th8_Interp *interp,
    const char *zPat,
    size_t nPat,
    const char **pzNs,
    size_t *pnNs,
    const char **pzTail,
    size_t *pnTail,
    char **pzBuf);

/*
 * th8FindNamespace returns the internal Th8_Namespace pointer.
 * The struct is defined in th8_core.c; th8_lang.c uses it opaquely
 * via the paCmd hash field for info commands/procs.
 */

/*
 * Th8_Namespace: must match the definition in th8_core.c.
 * Exposed here so th8_lang.c can access paCmd for info commands.
 */

#ifndef TH8_HAVE_NAMESPACE_TYPEDEF
#  define TH8_HAVE_NAMESPACE_TYPEDEF
typedef struct Th8_Namespace Th8_Namespace;
#endif
struct Th8_Namespace {
    char *zName;  /* Fully qualified name including the
				 * leading "::" (e.g. "::foo::bar").
				 * For the global namespace, zName is
				 * "::".  Owned; freed by
				 * th8FreeNamespace. */
    size_t nName;  /* Byte length of zName (not including
				 * the NUL terminator). */
    Th8_Namespace *pParent; /* Parent namespace in the tree.
				 * NULL only for the global namespace. */
    Th8_Hash *paChild;  /* Child namespaces (simple tail name
				 * -> Th8_Namespace*).  Created eagerly
				 * when the namespace is created. */
    Th8_Hash *paCmd;  /* Commands in this namespace (simple
				 * name -> Th8_Command*). */
#if defined(TH8_ENABLE_VARIABLES)
    Th8_Hash *paVar;  /* Namespace-scoped variables (simple
				 * name -> Th8_Variable*).
				 * FUTURE: integrate with frame vars. */
#endif
    char *zExport;  /* Space-separated list of export
				 * glob patterns, or NULL if none.
				 * Set by [namespace export]. */
    size_t nExport;  /* Byte length of zExport. */
    Th8_Hash *paExpansion; /* Expansion operators: tag name ->
				 * Th8_ExpansionEntry*.  NULL until
				 * the first operator is registered
				 * in this namespace. */
};

Th8_Namespace *th8FindNamespace(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    int bCreate);

/*
 * Secure variable hooks (defined in crypto/th8_secure.c).
 * Called from Th8_GetVar, Th8_SetVar, and th8FreeVariable in
 * th8_core.c when the variable has secure metadata.
 */

/*
 * Th8_ProcDefn and procedure dispatch.
 * Shared between th8_procedures.c and th8_introspection.c.
 */
#if defined(TH8_PLUGIN_PROCEDURES)
typedef struct Th8_ProcDefn Th8_ProcDefn;
struct Th8_ProcDefn {
    int nParam;   /* Number of formal params (not args) */
    char **azParam;  /* Parameter names */
    size_t *anParam;  /* Parameter name lengths */
    char **azDefault;  /* Default values (NULL = required) */
    size_t *anDefault;  /* Default value lengths */
    int hasArgs;  /* True if last param is "args" */
    char *zProgram;  /* Proc body */
    size_t nProgram;  /* Body length */
    char *zUsage;  /* Usage message (separate allocation) */
    size_t nUsage;  /* Usage message length */
    size_t nAllocSize;  /* Total size of this allocation block. */
    void *pDefNs;  /* Defining namespace (Th8_Namespace*). */
};
int th8ProcCall1(Th8_Interp *, void *, int, const char **, size_t *);
int th8NprocCall1(Th8_Interp *, void *, int, const char **, size_t *);
void *th8ProcCopy(Th8_Interp *, void *);
#endif

/*
 * Ensemble subcommand table pointers.
 * Set by each ensemble's dispatch function; read by
 * info_subcommands_command for introspection.
 * Each is gated on its owning plugin.
 */
#if defined(TH8_PLUGIN_VARIABLES)
extern const Th8_SubCommand *th8_array_aSub;
#endif
extern const Th8_SubCommand *th8_flags_aSub;
#if defined(TH8_PLUGIN_INTROSPECTION)
extern const Th8_SubCommand *th8_info_aSub;
#endif
#if defined(TH8_PLUGIN_FILE_SYSTEMS)
extern const Th8_SubCommand *th8_file_aSub;
#endif
#if defined(TH8_PLUGIN_MANAGEMENT)
extern const Th8_SubCommand *th8_namespace_aSub;
#endif
#if defined(TH8_PLUGIN_EXTENSIBILITY)
extern const Th8_SubCommand *th8_package_aSub;

/*
 *----------------------------------------------------------------------
 *
 * Th8_PkgInfo --
 *
 *	Per-package state used by the [package] ensemble.  Each
 *	package name in the package hash maps to one of these.
 *	zVersion is set by [package provide] (NULL until first
 *	provide); paIfNeeded is a sub-hash mapping version strings
 *	to ifneeded scripts (set by [package ifneeded]).
 *
 *	Defined here (vs. embedded in th8_extensibility.c) so that
 *	testlib can synthesize partial-state Th8_PkgInfo records
 *	for MC/DC coverage of `pPkg && pPkg->zVersion` /
 *	`pPkg && pPkg->paIfNeeded` compounds.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_PkgInfo Th8_PkgInfo;
struct Th8_PkgInfo {
    char *zVersion;  /* Provided version (or NULL) */
    size_t nVersion;
    Th8_Hash *paIfNeeded; /* Version -> ifneeded script hash */
};
#endif
#if defined(TH8_PLUGIN_STRINGS)
extern const Th8_SubCommand *th8_string_aSub;
#endif

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
int th8SecureGetVar(Th8_Interp *interp, const char *zVar, size_t nVar);
int th8SecureSetVar(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    const char *zNewVal,
    size_t nNewVal);
void th8SecureVarCleanup(Th8_Interp *interp, const char *zVar, size_t nVar);
#endif

#if defined(TH8_ENABLE_LOAD)
void th8FreeLoadedLibs(Th8_Interp *interp);
#endif

/*
 *----------------------------------------------------------------------
 *
 * Th8_Channel --
 *
 *	Per-channel state for a temporary file.
 *
 *----------------------------------------------------------------------
 */

#ifndef TH8_HAVE_CHANNEL_TYPEDEF
#  define TH8_HAVE_CHANNEL_TYPEDEF
typedef struct Th8_Channel Th8_Channel;
#endif
struct Th8_Channel {
    char *zName; /* Channel name (e.g., "./tmp/foo.tmp"). */
    size_t nName;
    char *zOsPath; /* OS file path (from xGetTemporaryData). */
    size_t nOsPath;
    size_t nMaxSize; /* Pre-allocated size limit. */
    size_t nPos; /* Current read/write position. */
    void *pChannel; /* Opaque handle from xGetTemporaryData.
			 * Passed to xInput/xOutput as pChannel. */
};

/*
 * Channel subsystem (th8_channel.c).
 */

TH8_INTERNAL void th8ChannelCleanup(Th8_Interp *interp);
TH8_INTERNAL int
th8ChannelClose(Th8_Interp *interp, const char *zName, size_t nName);
TH8_INTERNAL int th8ChannelCreate(Th8_Interp *interp, size_t nSize);
TH8_INTERNAL Th8_Channel *
th8ChannelFind(Th8_Interp *interp, const char *zName, size_t nName);
TH8_INTERNAL int th8ChannelWrite(
    Th8_Interp *interp,
    Th8_Channel *pChan,
    const char *z,
    size_t n);
TH8_INTERNAL int th8ChannelRead(
    Th8_Interp *interp,
    Th8_Channel *pChan,
    char **pzOut,
    size_t *pnOut);
TH8_INTERNAL int th8ChannelSeek(
    Th8_Interp *interp,
    Th8_Channel *pChan,
    long offset,
    int whence);
TH8_INTERNAL long th8ChannelTell(Th8_Channel *pChan);
TH8_INTERNAL int th8ChannelFlush(Th8_Interp *interp, Th8_Channel *pChan);
TH8_INTERNAL int th8ChannelList(Th8_Interp *interp, char **pz, size_t *pn);

/*
 *----------------------------------------------------------------------
 *
 * Internalized API functions --
 *
 *	These were formerly public (TH8_API) but are only used
 *	within the core library.  They are shared across compilation
 *	units but are NOT exported from the shared library.
 *
 *----------------------------------------------------------------------
 */

/*
 * Variable/value helpers (th8_vars.c).
 */
TH8_INTERNAL int th8GetVarValue(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    Th8_Value *pValue);
TH8_INTERNAL int th8SetVarValue(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    Th8_Value *pValue);
TH8_INTERNAL int th8SetVarLength(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    const char *zNewData,
    size_t nNewLen);
TH8_INTERNAL int th8AppendInPlace(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    int nArgs,
    const char **azArg,
    const size_t *anArg);

/*
 * Result/alloc/cancel (th8_core.c).
 */
TH8_INTERNAL int
th8SetResultBorrowed(Th8_Interp *interp, const char *z, size_t n);
TH8_INTERNAL void th8SetAllocBytes(Th8_Interp *interp, size_t n);
TH8_INTERNAL void
th8SaveCancel(Th8_Interp *interp, char savedCancel[TH8_CANCEL_SAVE_SIZE]);
TH8_INTERNAL void th8RestoreCancel(
    Th8_Interp *interp,
    const char savedCancel[TH8_CANCEL_SAVE_SIZE]);

/*
 * Finally state (th8_core.c).
 */
TH8_INTERNAL const char *th8GetFinallyResult(Th8_Interp *interp, size_t *pn);
TH8_INTERNAL int th8GetFinallyRc(Th8_Interp *interp);
TH8_INTERNAL void
th8SetFinallyState(Th8_Interp *interp, const char *z, size_t n, int rc);

/*
 * Cache internals (th8_cache.c).
 */
TH8_INTERNAL Th8_Value *
th8CopyValue(Th8_Interp *interp, const Th8_Value *pSrc);
TH8_INTERNAL void th8FreeValue(Th8_Interp *interp, Th8_Value *pVal);
TH8_INTERNAL void th8SetCacheString(
    Th8_Interp *interp,
    Th8_Value *pVal,
    const char *z,
    size_t n);
TH8_INTERNAL void *th8BufferAlloc(Th8_Interp *interp, size_t nBytes);
TH8_INTERNAL void th8BufferFree(Th8_Interp *interp, void *p, size_t nBytes);

/*
 * Platform misc (th8_plat.c).
 */
TH8_INTERNAL int th8TranslateLineEndings(char *zBuf, size_t *pnBuf);
TH8_INTERNAL void th8NotifyDeleteInterp(Th8_Interp *interp, void *pCtx);
TH8_INTERNAL void th8NotifyPreDeleteInterp(Th8_Interp *interp, void *pCtx);

/*
 * Panic (th8_core.c).
 */
TH8_INTERNAL void th8OversizeString(Th8_Interp *interp);

/*
 * Step / stack checking (th8_core.c).
 */
TH8_INTERNAL int th8Step(Th8_Interp *interp);
TH8_INTERNAL int th8CheckStack(Th8_Interp *interp);

/*
 * NRE frame helpers (th8_core.c).
 */
TH8_INTERNAL int th8NRInFrame(
    Th8_Interp *interp,
    Th8_CallbackProc xCall,
    void *p0,
    void *p1,
    void *p2,
    void *p3);
TH8_INTERNAL int th8EvalTrampoline(Th8_Interp *interp);
TH8_INTERNAL int th8InFrame(
    Th8_Interp *interp,
    int (*xCall)(Th8_Interp *, void *, void *),
    void *pContext1,
    void *pContext2);

/*
 * Frame accessors (th8_core.c).
 */
TH8_INTERNAL void th8SetFrameObjv(
    Th8_Interp *interp,
    int argc,
    const char **argv,
    size_t *argl);
TH8_INTERNAL int th8GetFrameLevel(Th8_Interp *interp);
TH8_INTERNAL void th8SetFrameNsPtr(Th8_Interp *interp, void *pNs);

/*
 * Parsing (th8_core.c, th8_vars.c, th8_expr.c).
 */
TH8_INTERNAL int th8ParseCommand(
    Th8_Interp *interp,
    const char *zScript,
    size_t nScript,
    int nLine,
    Th8_Parse *pParse);
TH8_INTERNAL int th8ParseVarName(
    Th8_Interp *interp,
    const char *zString,
    size_t nString,
    Th8_Value *pToken);
TH8_INTERNAL int th8ParseExpr(
    Th8_Interp *interp,
    const char *zExpr,
    size_t nExpr,
    Th8_Parse *pParse);
TH8_INTERNAL void th8FreeParse(Th8_Interp *interp, Th8_Parse *pParse);

/*
 * Math (th8_expr.c).
 */
TH8_INTERNAL int
th8MathOp(Th8_Interp *interp, int op, double *pResult, double a, double b);

/*
 * Line tracking (th8_core.c).
 */
TH8_INTERNAL void th8SetLine(Th8_Interp *interp, int nLine);

/*
 * Platform libs (th8_core.c).
 */
TH8_INTERNAL void *th8GetPlatformLibs(Th8_Interp *interp);
TH8_INTERNAL void th8SetPlatformLibs(Th8_Interp *interp, void *pLibs);

/*
 * Unload state (th8_load.c).
 */
#if defined(TH8_ENABLE_LOAD)
TH8_INTERNAL int th8IsUnloadEnabled(Th8_Interp *interp);
TH8_INTERNAL int th8IsUnloadDangerous(Th8_Interp *interp);
TH8_INTERNAL int th8LoadNameMatch(
    Th8_Interp *interp,
    const char *zA,
    size_t nA,
    const char *zB,
    size_t nB);
TH8_INTERNAL void th8XorInterpLoadToken(Th8_Interp *interp, th8_int64_t mask);
TH8_INTERNAL void
th8XorInterpUnloadToken(Th8_Interp *interp, th8_int64_t mask);
TH8_INTERNAL void th8ClearInterpUnloadFlags(Th8_Interp *interp);
#endif

/*
 * Token-XOR accessors (th8_core.c) -- used by the testlib plugin
 * to drive (T, F) MC/DC vectors on Th8_IsBigintEnabled /
 * Th8_IsSignedOnlyEnabled by flipping the token without zeroing it.
 */
TH8_INTERNAL void
th8XorInterpBigintToken(Th8_Interp *interp, th8_int64_t mask);
TH8_INTERNAL void
th8XorInterpSignedToken(Th8_Interp *interp, th8_int64_t mask);
TH8_INTERNAL void
th8XorInterpSecurePersistToken(Th8_Interp *interp, th8_int64_t mask);
TH8_INTERNAL void
th8XorInterpSecurePersistOk(Th8_Interp *interp, th8_int64_t mask);

/*
 * th8BigintCacheStore (th8_bigint.c) -- exposed as TH8_INTERNAL so
 * test-only NULL-arg exercisers in th8_testlib.c can drive the
 * Bug-26-family `!interp || !pSrc` NULL guards at L246 from
 * outside the bigint translation unit.  The in-tree caller always
 * passes non-NULL for both.  The pSrc parameter is typed as
 * `const void *` here so callers do not need tommath.h; the
 * implementation casts to `const mp_int *` internally.
 */
TH8_INTERNAL void th8BigintCacheStore(
    Th8_Interp *interp,
    const char *z,
    size_t n,
    const void *pSrc);

/*
 * th8IsDeviceName (plugins/th8_filesystems.c) -- exposed as
 * TH8_INTERNAL so test-only exercisers can drive the Win32-device-
 * name table on POSIX, where the function is otherwise unreachable
 * (its sole call site is gated by TH8_IS_SEP('\\')).
 */
TH8_INTERNAL int th8IsDeviceName(const char *z, size_t n);

/*
 * Th8_Interp field accessors (defined in th8_core.c).  Exposed for
 * test-only code in th8_testlib.c which does not include
 * th8_int_core.h.  All trivial wrappers.
 */
TH8_INTERNAL Th8_Hash *th8GetInterpCmdToken(Th8_Interp *interp);
TH8_INTERNAL void
th8SetInterpCmdToken(Th8_Interp *interp, Th8_Hash *paCmdToken);
TH8_INTERNAL Th8_Namespace *th8GetInterpCurrentNs(Th8_Interp *interp);
TH8_INTERNAL Th8_Hash *th8GetFramePaVar(Th8_Interp *interp);
TH8_INTERNAL Th8_Hash *th8GetInterpPaChannels(Th8_Interp *interp);

/*
 * Th8_AsyncState field perturber (test-only) -- XOR the
 * bMutexReady flag with the supplied mask so testlib can drive
 * the partial-init defensive guards at th8_core.c
 * L2177 / L2250.  Returns the OLD value so the caller can
 * restore it after the consumer call.
 */
TH8_INTERNAL int
th8AsyncStateXorBMutexReady(Th8_AsyncState *pState, int mask);

/*
 * Th8_Interp pPlatform exchanger (test-only) -- replace the
 * interp's pPlatform with a (possibly NULL) pointer, returning
 * the old one so testlib can drive the L967 / L1585 (F,T)
 * platform-NULL guards.  Caller MUST restore the original
 * platform immediately after the targeted call.
 */
TH8_INTERNAL Th8_Platform *
th8XchgInterpPlatform(Th8_Interp *interp, Th8_Platform *pNew);

/*
 * Th8_AsyncState finalize-field scrubber (test-only) -- zero
 * one of the finalize-path fields so testlib can drive the
 * (F,-) / (T,F) MC/DC vectors at th8_core.c L2645 + L2651
 * by calling Th8_FinalizeAsyncState afterward.  Caller does
 * NOT restore; each scrubbed call may leak the corresponding
 * platform handle, which is acceptable for the bounded test
 * run.
 */
TH8_INTERNAL void th8AsyncStateScrubField(Th8_AsyncState *pState, int field);

/*
 * Expansion-prefix scan (th8_core.c).  Inspects a candidate word
 * for the `{tag}rest` form.  Shared by th8SplitCommand (parser)
 * and th8NRSubstAndBuild (substitution).
 */
TH8_INTERNAL int th8CheckExpansionPrefix(
    Th8_Interp *interp,
    const char *zInput,
    size_t nWord,
    int *pbExpand,
    size_t *pnTag,
    Th8_ExpansionProc *pxExpand,
    void **ppExpandCtx);

/*
 * Fault-filter helpers (th8_fault.c).  Exposed to testlib so the
 * NULL-input and no-separator-boundary arms can be driven directly,
 * since the in-tree callers always gate inputs through fault-filter
 * setup that supplies non-NULL pointers / separator-suffixed paths.
 */
TH8_INTERNAL int th8FaultStrEqAscii(const char *a, const char *b);
TH8_INTERNAL int
th8FaultPathMatchesBaseName(const char *zPath, const char *zBase);

/*
 * Posix path defense-in-depth helper (th8_posix.c).  Exposed to
 * testlib so the `.`/`..` segment-detection arms at L4101 can be
 * driven directly with crafted paths.  All in-tree callers pass
 * paths that have been through realpath() / normalization, so the
 * defensive segment-detection arms never fire in production.
 */
TH8_INTERNAL int
th8PosixIsUnderBase(const char *zAbs, const char *zBase, size_t nBase);
TH8_INTERNAL int th8PosixIsPathUnderBase(const char *zPath);
TH8_INTERNAL void th8PosixCallUnloadProc(
    Th8_Interp *interp,
    void *hLib,
    const char *zName,
    size_t nName,
    int cbFlags);

/*
 * Harpy policy URI helper (plugins/harpy/th8_policy.c).  Exposed so
 * testlib can drive the L295/L296 2-condition compounds for
 * `http://` and `https://` prefix detection -- the function is
 * 0-hit because in-tree harpy callers reach it only through a
 * key-fetch path that requires the prod key to be registered.
 */
TH8_INTERNAL int
th8PolicyIsHttpUri(Th8_Interp *interp, const char *z, size_t n);
TH8_INTERNAL int th8PolicyDaysInMonth(int year, int month);
TH8_INTERNAL int th8SecureCheckCanary(Th8_Interp *interp, const void *pKSv);
TH8_INTERNAL void th8NtpSortTimes(th8_int64_t *a, int n);
TH8_INTERNAL int th8AfParseHexKey(const char *z, size_t n, th8_int64_t *pKey);
TH8_INTERNAL int th8RsaParseCapi(
    Th8_Interp *interp,
    const unsigned char *z,
    size_t n,
    void *pKeyv);
TH8_INTERNAL int th8HttpsTimeVerifySignature(
    Th8_Interp *interp,
    const char *zSignedData,
    size_t nSignedData,
    const char *zSigB64,
    size_t nSigB64);
TH8_INTERNAL const char *th8HttpsTimeFindField(
    Th8_Interp *interp,
    char **azElem,
    size_t *anElem,
    int nCount,
    const char *zKey,
    size_t nKey,
    size_t *pnVal);
TH8_INTERNAL int th8SecureHasMasterKey(Th8_Interp *interp);
TH8_INTERNAL void th8PolicyResetCachedKeys(void);
TH8_INTERNAL int th8PolicyVerifyData(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    const char *zData,
    size_t nData,
    void *pCtx);
TH8_INTERNAL void th8AfFlagSetAdd(Th8_FlagSet *p, char c);
TH8_INTERNAL void th8AfFlagSetRemove(Th8_FlagSet *p, char c);
TH8_INTERNAL Th8_FlagSet *
th8AfMapGet(Th8_Interp *interp, Th8_AfMap *p, th8_int64_t key, int bCreate);
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
TH8_INTERNAL void th8TestRsaKeyClearPubBlob(
    Th8_RsaKey *pKey,
    unsigned char **ppSavedBlob,
    size_t *pSavedN);
TH8_INTERNAL void th8TestRsaKeyRestorePubBlob(
    Th8_RsaKey *pKey,
    unsigned char *pSavedBlob,
    size_t nSaved);
#endif

/*
 * Namespace internals (th8_core.c).
 */
TH8_INTERNAL void *th8GetCurrentNsPtr(Th8_Interp *interp);
TH8_INTERNAL void th8SetCurrentNsPtr(Th8_Interp *interp, void *pNs);
TH8_INTERNAL const char *
th8GetNsParent(Th8_Interp *interp, const char *zNs, size_t nNs);
TH8_INTERNAL const char *
th8NsGetExport(Th8_Interp *interp, const char *zNs, size_t nNs);

/*
 * Cache internals (th8_cache.c).
 */
TH8_INTERNAL void th8ClearCache(Th8_Interp *interp);
TH8_INTERNAL void th8RemoveFromCache(
    Th8_Interp *interp,
    int cacheType,
    const char *z,
    size_t n);
TH8_INTERNAL Th8_Value *th8FindListInCache(
    Th8_Interp *interp,
    int cacheType,
    int nElem,
    const char **azElem,
    const size_t *anElem);

/*
 * Protected memory (th8_protect.c).
 */
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
TH8_INTERNAL int
th8ProtectedAlloc(Th8_Interp *interp, Th8_ProtectedRegion *pRegion);
TH8_INTERNAL void
th8ProtectedFree(Th8_Interp *interp, Th8_ProtectedRegion *pRegion);
TH8_INTERNAL int th8ProtectedCheckCanary(
    Th8_Interp *interp,
    const Th8_ProtectedRegion *pRegion);
TH8_INTERNAL unsigned char *
th8ProtectedData(const Th8_ProtectedRegion *pRegion);
TH8_INTERNAL size_t th8ProtectedPageSize(const Th8_ProtectedRegion *pRegion);
TH8_INTERNAL size_t th8ProtectedCanarySize(void);

/*
 * th8GetProtectedResultRegion --
 *	Lazy-allocate and return the per-interp Th8_ProtectedRegion
 *	used both as the backing store for sensitive interpreter
 *	results (Th8_SetResultSensitive) and as the decryption
 *	destination for secure-variable operations (th8SecureDecrypt).
 *	Returns NULL on allocation failure with the interp result
 *	set to a descriptive error.
 */
TH8_INTERNAL Th8_ProtectedRegion *
th8GetProtectedResultRegion(Th8_Interp *interp);

/*
 * th8FinalizeSensitiveResult --
 *	After bytes (nLen of them) have been written into the
 *	protected result region's data area (offset by the canary),
 *	finalize them as the current sensitive interpreter result:
 *	NUL-terminate, set zResult/nResult, set bResultSensitive=1
 *	and bResultBorrowed=1.  Caller must have ensured the region
 *	exists and the canary is valid.  Returns TH8_OK.
 */
TH8_INTERNAL int th8FinalizeSensitiveResult(Th8_Interp *interp, size_t nLen);
#endif

/*
 * Secure variable internals (crypto/th8_secure.c, th8_core.c).
 */
#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
TH8_INTERNAL int th8SecureInit(Th8_Interp *interp);
TH8_INTERNAL void th8SecureFinish(Th8_Interp *interp);
TH8_INTERNAL int th8SecureVarCreate(
    Th8_Interp *interp,
    const char *zVar,
    size_t nVar,
    const char *zVal,
    size_t nVal);
TH8_INTERNAL int
th8SecureVarDelete(Th8_Interp *interp, const char *zVar, size_t nVar);
TH8_INTERNAL int
th8IsSecureVar(Th8_Interp *interp, const char *zVar, size_t nVar);
TH8_INTERNAL void *th8GetSecureKeyStore(Th8_Interp *interp);
TH8_INTERNAL void th8SetSecureKeyStore(Th8_Interp *interp, void *p);
TH8_INTERNAL Th8_Hash *th8GetSecureVarHash(Th8_Interp *interp);
TH8_INTERNAL void th8SetSecureVarHash(Th8_Interp *interp, Th8_Hash *p);
TH8_INTERNAL int
th8SecureSave(Th8_Interp *interp, const char *zVar, size_t nVar);
TH8_INTERNAL int
th8SecureLoad(Th8_Interp *interp, const char *zVar, size_t nVar);
#endif

/*
 * th8FaultStashSite --
 *	Record the call-site (zFile, nLine) of an upcoming
 *	allocation into the active fault config (if any).  Called
 *	by Th8_Safe* allocation wrappers immediately before they
 *	dispatch to the platform.  Stub when fault injection is
 *	compiled out.  See th8_fault.c.
 */
TH8_INTERNAL void
th8FaultStashSite(Th8_Interp *interp, const char *zFile, int nLine);

/*
 * Array search registry declarations live in th8_vars.h
 * (private internal header for the variables subsystem).
 */

/*
 * NTP/Time (plugins/harpy/th8_time.c, th8_core.c).
 */
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
TH8_INTERNAL int th8NtpQuery(
    Th8_Interp *interp,
    const char **azServers,
    int nServers,
    int timeoutMs,
    int maxDisagreeSec,
    th8_int64_t *pEpochSec);
/*
 * Validate a received NTP packet (const void * = 48-byte
 * Th8_NtpPacket wire buffer) against the request and derive epoch
 * seconds.  Factored out of th8NtpQueryOne for direct MC/DC
 * driving with crafted packets; see th8_time.c.
 */
TH8_INTERNAL int th8NtpValidateResponse(
    Th8_Interp *interp,
    const void *respv,
    const void *reqv,
    th8_int64_t *pEpochSec);
TH8_INTERNAL int th8HttpsTimeQuery(
    Th8_Interp *interp,
    const char *zUrl,
    size_t nUrl,
    th8_int64_t *pEpochSec);
TH8_INTERNAL th8_int64_t th8GetLastNtpSec(Th8_Interp *interp);
TH8_INTERNAL void th8SetLastNtpSec(Th8_Interp *interp, th8_int64_t sec);
TH8_INTERNAL th8_int64_t th8GetLastLocalMs(Th8_Interp *interp);
TH8_INTERNAL void th8SetLastLocalMs(Th8_Interp *interp, th8_int64_t ms);
#endif

#endif /* TH8_INT_H */
