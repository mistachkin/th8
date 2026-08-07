/*
 * th8_core.c -- Core interpreter engine for the TH8 scripting language.
 *
 * ARCHITECTURE OVERVIEW
 *
 *   This file contains the core interpreter engine.  The major
 *   subsystems, in rough dependency order, are:
 *
 *   1. Platform-routed memory (Th8_Malloc/Free/Realloc) -- all
 *      allocation goes through the Th8_Platform function pointer
 *      table, so TH8 never calls libc directly.
 *
 *   2. Hash table (Th8_Hash) -- simple chaining hash used for
 *      variables, commands, namespace children, and packages.
 *
 *   3. Namespace management (Th8_Namespace) -- hierarchical
 *      "::" separated tree of command/variable containers.
 *
 *   4. Frame management (Th8_Frame) -- linked-list call stack
 *      with per-frame variable scopes.
 *
 *   5. Variable system (Th8_Variable) -- reference-counted
 *      scalars and arrays, with upvar/global linking.
 *
 *   6. Result management -- interpreter result string with
 *      size-limit enforcement.
 *
 *   7. NRE trampoline (Th8_Callback) -- non-recursive
 *      evaluation engine using a LIFO callback chain.
 *
 *   8. Tokenizer/parser -- scans script text into words,
 *      performs backslash/variable/command substitution.
 *
 *   9. Evaluator (th8EvalLocal + NRE callbacks) -- NRE-driven
 *      eval loop that splits commands, dispatches them via the
 *      trampoline, and builds error traces without C stack
 *      nesting.
 *
 *  10. Expression evaluator (Th8_Expr) -- operator-precedence
 *      parser that builds and evaluates an expression tree.
 *
 *  11. Spilornis bridge -- CRT shim functions that allow the
 *      Eagle list parser (Spilornis.c) to operate without libc
 *      by routing through the TH8 platform.
 *
 *  12. Interpreter lifecycle (Th8_CreateInterp/DeleteInterp) --
 *      single-allocation construction and ordered teardown.
 *
 * Together with th8.h and th8_lang.c, this constitutes the complete
 * TH8 interpreter.  TH8 calls no C standard library functions
 * directly; all external dependencies are routed through the
 * Th8_Platform function pointer table.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_meta_defs.h"
#include "th8_meta_libc.h"
#include "th8_meta_posix.h"
#include "th8_plat.h"
#include "th8.h"
#include "th8_int.h"
#include "th8_int_core.h"
#include "th8_vars.h"
#include "th8_mem.h"
#include "th8_plugin.h"

#if defined(TH8_ENABLE_BIGINT)
#  include "th8_bigint.h"
#endif

#if defined(TH8_USE_MIMALLOC)
#  include "mimalloc.h"  /* mi_thread_init / mi_thread_done */
#endif

/*
 * Include standard UTF-8 / UTF-16 / UTF-32 conversions functions.
 */

#include "ConvertUTF_v2.h"


/*
 *----------------------------------------------------------------------
 *
 * Internal buffer --
 *
 *	Growable byte buffer used during parsing and substitution.
 *
 * Why / How:
 *	Dispatches on the character after the backslash to determine
 *	the escape length: fixed-length for named escapes (\n, etc.),
 *	variable-length for hex (\x), Unicode (\u, \U), octal, and
 *	backslash-newline continuation sequences.  Used by Th8_CmdBuild
 *	for NRE word accumulation, and by various internal "scratch"
 *	paths.
 *----------------------------------------------------------------------
 */
typedef struct Th8_Buffer Th8_Buffer;
struct Th8_Buffer {
    char *zBuf;
    size_t nBuf;   /* Raw byte count (never carries tag bits). */
    size_t nAlloc; /* Raw allocated capacity. */
    size_t nTag;   /* Accumulated tag bits (TH8_TAG_BITS: taint and
                    * sensitive) across all appended data.  Kept separate
                    * from nBuf/nAlloc so buffer arithmetic stays raw;
                    * th8BufWrite ORs each write's tags here.  When the
                    * buffer is freed, sensitive content is securely zeroed
                    * (th8BufFree). */
    int bFail;     /* Sticky: set when an append could not complete (growth
                    * allocation failure or size-guard overflow).  Once set,
                    * further appends are no-ops so the buffer cannot recover
                    * into a plausible-but-truncated state.  Finalizers MUST
                    * check this and fail the operation with an error rather
                    * than publish the truncated contents (Bug 61). */
};

/*
 * Th8_EvalState --
 *	Per-eval iteration state for the NRE eval loop.  Allocated
 *	once at the start of Th8_Eval and freed at completion;
 *	dereferenced from th8NRCmdDispatch / th8EvalIteration /
 *	th8EvalPostCmd.
 */
typedef struct Th8_EvalState {
    const char *zProgram; /* Original script (for error traces). */
    const char *zInput;  /* Current position in script. */
    const char *zFirst;  /* Start of current command text. */
    size_t nInput;  /* Remaining bytes. */
    size_t nOrigInput;  /* Original script length (for POST cb). */
    int nSavedLine;  /* Line counter before this eval. */
    int flags;   /* Eval flags (for POST callback). */
    const char *zName;  /* Script origin name (or NULL). */
    size_t nName;  /* Origin name length. */
} Th8_EvalState;

/*
 * Th8_CmdBuild --
 *	Heap-allocated state for NRE-aware command building.
 *	Persists across NRE callbacks during word splitting so that
 *	[yield] inside [...] substitution preserves the in-progress
 *	command rather than discarding it.
 */
typedef struct Th8_CmdBuild Th8_CmdBuild;
struct Th8_CmdBuild {
    /* Argv accumulation (mirrors th8SplitCommand's buffers). */
    Th8_Buffer strbuf;  /* NUL-terminated word strings. */
    Th8_Buffer lenbuf;  /* Word lengths (size_t each). */
    int nCount;   /* Number of words accumulated. */

    /* Word iteration within the command text. */
    const char *zInput;  /* Next unprocessed position. */
    size_t nInput;  /* Remaining command bytes. */
    size_t nOrigWord;  /* Original word length (for advance). */

    /* Current word being substituted. */
    Th8_Buffer wordBuf;  /* Accumulator for current word. */
    const char *zWord;  /* Current word text pointer. */
    size_t nWord;  /* Current word length. */
    size_t wordPos;  /* Scan position within word. */
    size_t wordEnd;  /* End position (after quote strip). */
    int bWordActive;  /* Non-zero if a word is in progress. */

    /* Expansion state for current word. */
    int bExpand;
    Th8_ExpansionProc xExpand;
    void *pExpandCtx;

    /* Saved interpreter state. */
    int wasListMode;

    /* Back-pointer to eval state (for zName/nName). */
    Th8_EvalState *pState;
};

/*
 * Th8_ExecCtx --
 *	Saved interpreter execution context: NRE callback chain,
 *	frame stack, current namespace, eval depth, line counter,
 *	suspended callbacks, and saved frame.  Snapshot/restore
 *	via th8SaveExecCtx/th8RestoreExecCtx during a coroutine
 *	context switch.
 */
typedef struct Th8_ExecCtx {
    Th8_Callback *pCallbacks; /* NRE callback chain. */
    Th8_Frame *pFrame;  /* Call frame stack. */
    Th8_Namespace *pCurrentNs; /* Current namespace. */
    int nEvalDepth;  /* Eval nesting depth. */
    int nLine;   /* Current line number. */
    Th8_Callback *pSuspendedCallbacks;
    Th8_Frame *pSavedFrame;
} Th8_ExecCtx;

/*
 * Th8_CoroState --
 *	Per-coroutine state: saved execution context plus the values
 *	that flow through [yield] / resume.  zBody is owned to keep
 *	the script text alive across the yield (zProgram references
 *	it).
 */
typedef struct Th8_CoroState Th8_CoroState;
struct Th8_CoroState {
    Th8_ExecCtx ctx;  /* Saved execution context. */
    char *zYieldValue;  /* Value from [yield]. */
    size_t nYieldValue;  /* Byte length of yield value. */
    char *zResumeValue;  /* Value passed when resuming. */
    size_t nResumeValue; /* Byte length of resume value. */
    int bDone;   /* Body finished (no more yields). */
    char *zName;  /* Coroutine command name. */
    size_t nName;  /* Byte length of name. */
    char *zBody;  /* Script body (kept alive for
				 * EvalState->zProgram reference). */
};

/*
 * Th8_ImportCtx --
 *	Hash-iteration context for [namespace import]: source
 *	namespace, glob pattern, and -force flag.
 */
typedef struct {
    Th8_Interp *interp;
    Th8_Namespace *pSrcNs;
    const char *zImportPat;
    size_t nImportPat;
    int bForce;   /* True if -force was specified. */
} Th8_ImportCtx;

/*
 * Th8_CancelSave --
 *	Snapshot of the interpreter's cancellation state, taken
 *	before [try ... finally ...] runs the finally block so the
 *	finally starts in a clean (non-canceled) state and the
 *	original state can be restored after.
 */
typedef struct {
    int bCanceled;
    int cancelFlags;
    int bCancelMsgOwned;
    char *zCancelMsg;
    size_t nCancelMsg;
} Th8_CancelSave;

/*
 * TH8_PARSE_NEST_INLINE / Th8ParseStack --
 *	Generic parser nesting stack with inline-then-heap storage.
 *	Used by the tokenizer to track open braces, brackets, etc.
 *	without forcing a heap allocation in the common case.
 */
#define TH8_PARSE_NEST_INLINE 256

typedef struct Th8ParseStack Th8ParseStack;
struct Th8ParseStack {
    char *p;     /* Active storage. */
    size_t cap;     /* Capacity (entries). */
    size_t top;     /* Used entries. */
    char inlineBuf[TH8_PARSE_NEST_INLINE]; /* Inline storage. */
};

/*
 * Th8_ExpansionEntry --
 *	One row in the expansion-operator registry.  Looked up by
 *	operator name when {*}word is parsed; default xProc is
 *	th8BuiltinExpand which calls Th8_SplitList.
 */
typedef struct Th8_ExpansionEntry Th8_ExpansionEntry;
struct Th8_ExpansionEntry {
    Th8_ExpansionProc xProc;
    void *pCtx;
};

/*
 * th8ArraySearchIterCtx --
 *	Glue context for Th8_IterateArraySearches: forwards
 *	(zArray, zSid) pairs from an internal Th8_HashIterate to
 *	the caller-provided xCallback.
 */
typedef struct th8ArraySearchIterCtx {
    int (*xCallback)(
        const char *zArray,
        size_t nArray,
        const char *zSid,
        size_t nSid,
        void *pCtx);
    void *pCtx;
    int rc;
} th8ArraySearchIterCtx;


/* Forward declaration: global platform (defined in Th8_Initialize section). */
extern Th8_Platform th8GlobalPlatform;

/* Forward declaration for secondary-index cleanup. */
static void th8RemoveCmdTokenEntry(Th8_Interp *, Th8_Command *);

/* Forward declaration for debug check (used in Th8_Ready). */
static int th8DebugCheck(Th8_Interp *);


/*
 *----------------------------------------------------------------------
 *
 * Cache accessor helpers --
 *
 *	These small functions are defined here (where the full
 *	Th8_Interp layout is visible) and called from th8_cache.c
 *	via extern declarations.  They let the cache module read
 *	and write the per-interp cache fields without exposing the
 *	interpreter struct to a separate compilation unit.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8CacheHashPtr --
 *
 *	Return a pointer to the per-interpreter cache hash table
 *	pointer field.  The caller (th8_cache.c) dereferences this
 *	to read or write the cache hash without exposing the full
 *	Th8_Interp layout.
 *
 * Why / How:
 *	The cache module is compiled separately and does not include
 *	the internal interpreter header.  This accessor bridges the
 *	information-hiding boundary.
 *
 * Results:
 *	Address of interp->paCache.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Th8_Hash **
th8CacheHashPtr(Th8_Interp *interp)
{
    /* Bug 68: paCache sits IMMEDIATELY after paSystemVar in Th8_Interp.
     * This helper hands &interp->paCache to th8_cache.c -- a translation
     * unit that does NOT include the struct definition and is therefore
     * blind to that adjacency.  Consumers MUST dereference the returned
     * pointer with NO arithmetic: a pp[-1] would land on paSystemVar
     * (offset 4656) exactly and silently corrupt it.  Pin the adjacency
     * so any future field reorder becomes a compile error right here.
     * (sizeof(char[-1]) is a portable c99/c11 compile-time assertion
     * with no runtime cost and no unused-symbol warning.) */
    (void)sizeof(char
                     [(offsetof(Th8_Interp, paCache) ==
                       offsetof(Th8_Interp, paSystemVar) + sizeof(Th8_Hash *))
                          ? 1
                          : -1]);
    return &interp->paCache;
}

/*
 *----------------------------------------------------------------------
 *
 * th8CacheMutexReady --
 *
 *	Query whether the per-interpreter cache mutex has been
 *	initialized and is safe to lock.
 *
 * Why / How:
 *	The cache module must check this before any mutex operation
 *	to avoid calling xMutexEnter on an uninitialized mutex,
 *	which would be undefined behavior on every platform.
 *
 * Results:
 *	Non-zero if the mutex is initialized; zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8CacheMutexReady(Th8_Interp *interp)
{
    return interp->bCacheMutexReady;
}

/*
 *----------------------------------------------------------------------
 *
 * th8CacheMutexLock --
 *
 *	Acquire the per-interpreter cache mutex.  If the platform
 *	does not provide a mutex implementation, this is a no-op,
 *	which is safe because single-threaded embedders do not need
 *	locking.
 *
 * Why / How:
 *	The cache hash table may be accessed from multiple threads
 *	when the embedding application evaluates scripts concurrently.
 *	This mutex serializes those accesses.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The calling thread blocks until the mutex is acquired.
 *
 *----------------------------------------------------------------------
 */

void
th8CacheMutexLock(Th8_Interp *interp)
{
    /* Bug 52 (2026-06-09): kept as ALWAYS -- in production the
     * cache subsystem is only entered with a valid pPlatform
     * (Th8_FindInCache is called from within Th8_ToInt/ToDouble
     * etc. paths whose callers hold a live interp).  The
     * platform-NULL window from the testlib swap pattern is
     * NOT meant to reach the cache layer; per-function
     * platform-NULL fast-fail at the entry of those functions
     * (e.g. th8MathClassify Bug 52 reorder) is the correct fix
     * rather than NULL-tolerating the cache mutex. */
    if (ALWAYS(interp->pPlatform) && interp->pPlatform->xMutexEnter) {
	interp->pPlatform->xMutexEnter(
	    interp, interp->pPlatform->pCtx, &interp->cacheMutex);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8CacheMutexUnlock --
 *
 *	Release the per-interpreter cache mutex previously acquired
 *	by th8CacheMutexLock.
 *
 * Why / How:
 *	Paired with th8CacheMutexLock to bound the critical section
 *	around cache hash table operations.  Like Lock, this is a
 *	no-op when the platform omits mutex support.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The mutex is released; blocked threads may proceed.
 *
 *----------------------------------------------------------------------
 */

void
th8CacheMutexUnlock(Th8_Interp *interp)
{
    /* Bug 52 (2026-06-09): kept as ALWAYS, see th8CacheMutexLock
     * comment above. */
    if (ALWAYS(interp->pPlatform) && interp->pPlatform->xMutexLeave) {
	interp->pPlatform->xMutexLeave(
	    interp, interp->pPlatform->pCtx, &interp->cacheMutex);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8CacheMutexSetup --
 *
 *	Initialize the per-interpreter cache mutex during interpreter
 *	creation.  After this call, th8CacheMutexReady() returns
 *	non-zero and Lock/Unlock are effective.
 *
 * Why / How:
 *	The cache mutex must be initialized exactly once per
 *	interpreter, before any cache operation.  The ready flag
 *	prevents double-init and guards Lock/Unlock calls that
 *	might occur before setup completes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->cacheMutex is initialized via xMutexInit;
 *	interp->bCacheMutexReady is set to 1.
 *
 *----------------------------------------------------------------------
 */

void
th8CacheMutexSetup(Th8_Interp *interp)
{
    if (ALWAYS(interp->pPlatform) && interp->pPlatform->xMutexInit) {
	interp->pPlatform->xMutexInit(
	    interp, interp->pPlatform->pCtx, &interp->cacheMutex);
	interp->bCacheMutexReady = 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * th8CacheMutexTeardown --
 *
 *	Destroy the per-interpreter cache mutex during interpreter
 *	deletion.  After this call, th8CacheMutexReady() returns
 *	zero and any Lock/Unlock calls are no-ops.
 *
 * Why / How:
 *	Ordered teardown: the cache is freed before the mutex is
 *	destroyed (th8_cache.c handles that ordering).  The ready
 *	flag is cleared to prevent use-after-destroy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->cacheMutex is finalized via xMutexFinal;
 *	interp->bCacheMutexReady is set to 0.
 *
 *----------------------------------------------------------------------
 */

void
th8CacheMutexTeardown(Th8_Interp *interp)
{
    /* If bCacheMutexReady is set, the mutex was initialized via
     * th8CacheMutexSetup, which requires interp->pPlatform to
     * be non-NULL (otherwise setup early-returns).  C2 is
     * ALWAYS T when C1 is T at this point. */
    if (interp->bCacheMutexReady && ALWAYS(interp->pPlatform) &&
        interp->pPlatform->xMutexFinal) {
	interp->pPlatform->xMutexFinal(
	    interp, interp->pPlatform->pCtx, &interp->cacheMutex);
	interp->bCacheMutexReady = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SecureZero --
 *
 *	Securely zero a memory region so that sensitive material
 *	(keys, passwords, nonces) is not left in memory after use.
 *
 * Why / How:
 *	Uses OPENSSL_cleanse when available, which is hardened
 *	against dead-store elimination by the compiler.  Falls back
 *	to a volatile-write loop otherwise.  A memory barrier
 *	follows to prevent CPU store-buffer reordering from leaving
 *	stale key material observable via speculative execution.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The first n bytes at p are zeroed; a memory barrier is
 *	issued.  No-op if p is NULL or n is zero.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SecureZero(Th8_Interp *interp, void *p, size_t n)
{
    if (!interp) return;
    (void)interp;
    if (n == 0 || !p) return;

    /*
     * Primary: OPENSSL_cleanse is hardened against dead-store
     * elimination.  Fallback: volatile-write loop for builds
     * without OpenSSL headers (should not happen when
     * TH8_ENABLE_CRYPTOGRAPHY is defined, but defense in depth).
     */

#if defined(OPENSSL_VERSION_NUMBER)
    OPENSSL_cleanse(p, n);
#else
    {
	volatile unsigned char *vp = (volatile unsigned char *)p;
	while (n--) {
	    unsigned char c2 = *vp;
	    *vp++ = c2 ^ c2;
	}
    }
#endif

    /*
     * Memory barrier: ensure the zero writes are globally
     * visible before any subsequent memory access.
     */

#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Spilornis CRT bridge functions.
 *
 *	These are called by Spilornis.c via #define macros in
 *	th8_spilornis.h.  Each function checks th8_spilornis_interp
 *	and, if set, routes through the TH8 platform.  If NULL
 *	(should never happen during normal operation), returns a
 *	safe default (0 or no-op).
 *
 *----------------------------------------------------------------------
 */

/* Forward declarations (prototypes satisfy -Wmissing-prototypes). */
void *th8_spilornis_calloc(size_t count, size_t size);
void th8_spilornis_free(void *p);
void *th8_spilornis_memcpy(void *d, const void *s, size_t n);
void *th8_spilornis_memset(void *d, int c, size_t n);
int th8_spilornis_memcmp(const void *a, const void *b, size_t n);
size_t th8_spilornis_strlen(const char *s);
int th8_spilornis_strncmp(const char *a, const char *b, size_t n);
char *th8_spilornis_strncpy(char *d, const char *s, size_t n);
int th8_spilornis_snprintf(char *buf, size_t size, const char *fmt, ...);
int
th8_spilornis_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
size_t th8_spilornis_memsize(void *p);


#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)

/*
 *----------------------------------------------------------------------
 *
 * th8GetSecureKeyStore --
 *
 *	Return the opaque secure-key-store pointer cached on the
 *	interpreter.  The key store is the mlock'd page that holds
 *	per-variable AES keys and the master key slot used by the
 *	[secure] command family.
 *
 *	The crypto subsystem (`src/plugins/crypto/th8_secure.c`)
 *	allocates and owns the store; this accessor isolates it
 *	from the `Th8_Interp` struct layout.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The stored pointer, or NULL if no key store has been
 *	installed on this interpreter.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void *
th8GetSecureKeyStore(Th8_Interp *interp)
{
    return interp->pSecureKeyStore;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetSecureKeyStore --
 *
 *	Store an opaque secure-key-store pointer on the interpreter.
 *	The pointer is set by the crypto subsystem when the per-
 *	interpreter key store is allocated, and cleared (to NULL)
 *	on teardown.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	p      -- new key-store pointer; may be NULL to clear.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Overwrites `interp->pSecureKeyStore`.  Does NOT free any
 *	previously-stored pointer; the caller owns the lifecycle.
 *
 *----------------------------------------------------------------------
 */
void
th8SetSecureKeyStore(Th8_Interp *interp, void *p)
{
    interp->pSecureKeyStore = p;
}

/*
 *----------------------------------------------------------------------
 *
 * th8GetSecureVarHash --
 *
 *	Return the per-interpreter hash table mapping secure-variable
 *	names to their encrypted-value metadata (slot index, nonce,
 *	authentication tag).  The hash is consulted by [set] and
 *	[unset] to dispatch the secure-variable path transparently.
 *
 *	The crypto subsystem owns the hash; this accessor isolates
 *	it from the `Th8_Interp` struct layout.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The stored `Th8_Hash *`, or NULL if no secure-variable hash
 *	has been installed on this interpreter.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
Th8_Hash *
th8GetSecureVarHash(Th8_Interp *interp)
{
    return interp->paSecureVar;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetSecureVarHash --
 *
 *	Store a secure-variable hash pointer on the interpreter.
 *	Called by the crypto subsystem during initialisation and
 *	cleared (to NULL) on teardown.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	p      -- new hash pointer; may be NULL to clear.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Overwrites `interp->paSecureVar`.  Does NOT free any
 *	previously-stored hash; the caller owns the lifecycle.
 *
 *----------------------------------------------------------------------
 */
void
th8SetSecureVarHash(Th8_Interp *interp, Th8_Hash *p)
{
    interp->paSecureVar = p;
}

#endif /* TH8_ENABLE_VARIABLES && TH8_ENABLE_CRYPTOGRAPHY */


#if defined(TH8_ENABLE_CRYPTOGRAPHY)

/*
 *----------------------------------------------------------------------
 *
 * th8GetLastNtpSec --
 *
 *	Return the most recent NTP-derived epoch timestamp
 *	(seconds since 1970-01-01 UTC) cached on the interpreter.
 *	Used by the Harpy time-validation subsystem to detect
 *	clock-skew attacks on signed scripts; paired with
 *	`th8GetLastLocalMs` to compute cache freshness.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The cached epoch second, or 0 if no NTP query has ever
 *	been recorded on this interpreter.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
th8_int64_t
th8GetLastNtpSec(Th8_Interp *interp)
{
    return interp->nLastNtpSec;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetLastNtpSec --
 *
 *	Record the epoch timestamp returned by the most recent
 *	successful NTP query.  Callers must pair this with
 *	`th8SetLastLocalMs(monotonic-millisecond-at-query)` so the
 *	freshness comparison works correctly.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	sec    -- epoch seconds since 1970-01-01 UTC.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Overwrites `interp->nLastNtpSec`.
 *
 *----------------------------------------------------------------------
 */
void
th8SetLastNtpSec(Th8_Interp *interp, th8_int64_t sec)
{
    interp->nLastNtpSec = sec;
}

/*
 *----------------------------------------------------------------------
 *
 * th8GetLastLocalMs --
 *
 *	Return the local monotonic timestamp (milliseconds) recorded
 *	at the time of the last NTP query.  Paired with
 *	`th8GetLastNtpSec` to determine cache freshness: the live
 *	monotonic clock is compared against this value to decide
 *	whether the cached NTP result has expired and a fresh query
 *	is needed.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The cached monotonic millisecond, or 0 if no NTP query has
 *	ever been recorded on this interpreter.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
th8_int64_t
th8GetLastLocalMs(Th8_Interp *interp)
{
    return interp->nLastLocalMs;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetLastLocalMs --
 *
 *	Record the local monotonic millisecond at the time of the
 *	most recent NTP query.  See `th8GetLastLocalMs` for how
 *	this value is used in the cache-freshness check.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	ms     -- monotonic milliseconds (typically from `xTimeMs`).
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Overwrites `interp->nLastLocalMs`.
 *
 *----------------------------------------------------------------------
 */
void
th8SetLastLocalMs(Th8_Interp *interp, th8_int64_t ms)
{
    interp->nLastLocalMs = ms;
}

#endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * th8GetPluginList --
 *
 *	Return the head of the plugin-registration linked list on
 *	the interpreter.  Plugins are kept as a chain of
 *	`Th8_PluginEntry` records owned by `src/th8_plugin.c`;
 *	this accessor isolates the plugin code from the
 *	`Th8_Interp` struct layout.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The list head, or NULL if no plugins are registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void *
th8GetPluginList(Th8_Interp *interp)
{
    return interp->pPlugins;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetPluginList --
 *
 *	Store the head pointer of the plugin-registration linked
 *	list on the interpreter.  Called by `src/th8_plugin.c` when
 *	a plugin is added at the head of the list and on teardown
 *	to clear the chain.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	p      -- new list head; may be NULL to clear.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Overwrites `interp->pPlugins`.  Does NOT free the previous
 *	chain; the caller owns the lifecycle of every entry.
 *
 *----------------------------------------------------------------------
 */
void
th8SetPluginList(Th8_Interp *interp, void *p)
{
    interp->pPlugins = p;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NextCmdToken --
 *
 *	Allocate and return the next unique command registration
 *	token from the interpreter's monotonic counter.
 *
 * Why / How:
 *	Each command registered with Th8_CreateCommand receives a
 *	unique token so that [info commands] and rename/delete
 *	operations can detect stale references.  The counter is
 *	a simple post-increment of an unsigned 64-bit integer on
 *	the interpreter.
 *
 * Results:
 *	The next token value (pre-increment value).
 *
 * Side effects:
 *	Increments the interpreter's command token counter.
 *
 *----------------------------------------------------------------------
 */

th8_uint64_t
th8NextCmdToken(Th8_Interp *interp)
{
    return interp->nNextCmdToken++;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetCmdToken --
 *
 *	Set the registration token on an existing command entry.
 *
 * Why / How:
 *	Used by [rename] and command-copy operations to update the
 *	token on a command that has been moved to a new name or
 *	namespace.  Resolves the command via the namespace-aware
 *	qualified-name lookup and patches the nToken field.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the nToken field of the command's hash entry.
 *
 *----------------------------------------------------------------------
 */

void
th8SetCmdToken(Th8_Interp *interp, const char *zName, th8_uint64_t token)
{
    Th8_HashEntry *pEntry;
    Th8_Namespace *pNs;
    Th8_Hash *paCmd;
    const char *zNsPath, *zTail;
    size_t nNsPath, nTail, nName;

    nName = Th8_Strlen(interp, zName);
    th8SplitQualName(zName, nName, &zNsPath, &nNsPath, &zTail, &nTail);

    if (zNsPath) {
	pNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
	if (!pNs) return;
	paCmd = pNs->paCmd;
    } else {
	paCmd = interp->pCurrentNs->paCmd;
	zTail = zName;
	nTail = nName;
    }

    pEntry = Th8_HashFind(interp, paCmd, zTail, nTail, 0);
    /* Bug 26 family: plain guard; Th8_HashDelete tombstones
     * (pData = NULL), so a found command entry may be a tombstone. */
    if (pEntry && pEntry->pData) {
	((Th8_Command *)pEntry->pData)->nToken = token;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetCmdToken --
 *
 *	Retrieve the registration token for a named command.
 *
 * Why / How:
 *	Used by [info] subcommands and the debugger to identify
 *	which generation of a command is active after renames or
 *	re-registrations.  Resolves the name via the namespace-
 *	aware qualified-name lookup.
 *
 * Results:
 *	The command's nToken value, or 0 if the command does not
 *	exist.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

th8_uint64_t
th8GetCmdToken(Th8_Interp *interp, const char *zName)
{
    Th8_HashEntry *pEntry;
    Th8_Namespace *pNs;
    Th8_Hash *paCmd;
    const char *zNsPath, *zTail;
    size_t nNsPath, nTail, nName;

    nName = Th8_Strlen(interp, zName);
    th8SplitQualName(zName, nName, &zNsPath, &nNsPath, &zTail, &nTail);

    if (zNsPath) {
	pNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
	if (!pNs) return 0;
	paCmd = pNs->paCmd;
    } else {
	paCmd = interp->pCurrentNs->paCmd;
	zTail = zName;
	nTail = nName;
    }

    pEntry = Th8_HashFind(interp, paCmd, zTail, nTail, 0);
    /* Bug 26 family: plain guard; tombstoned command entries have
     * pData == NULL after Th8_HashDelete. */
    if (pEntry && pEntry->pData) {
	return ((Th8_Command *)pEntry->pData)->nToken;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Platform-routed memory --
 *
 *	All memory allocation goes through the platform.  If the
 *	platform returns NULL, we call xPanic.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8MallocCommon --
 *
 *	Common implementation for Th8_Malloc and Th8_AttemptMalloc.
 *	Allocates and zero-fills memory via the platform.
 *
 *	When bPanic is non-zero (Th8_Malloc), allocation failure or
 *	limit exhaustion calls xPanic.  When bPanic is zero
 *	(Th8_AttemptMalloc), returns NULL without panicking.
 *
 * Why / How:
 *	Centralizes allocation logic so that both Th8_Malloc and
 *	Th8_AttemptMalloc share the same limit check, platform
 *	dispatch (xMalloc), zero-fill (xMemset), and allocation
 *	tracking (nAllocBytes) code path.
 *
 *----------------------------------------------------------------------
 */

static void *
th8MallocCommon(
    Th8_Interp *interp, /* Interpreter for platform access. */
    size_t nByte, /* Number of bytes to allocate. */
    int bPanic, /* Non-zero: panic on failure. */
    const char *zFile, /* Caller __FILE__ for fault filter. */
    int nLine) /* Caller __LINE__ for fault filter. */
{
    void *p;

    /* Bug 26 family: plain interp/platform guards (Bug 31
     * pattern) -- under TH8_OMIT the NEVERs collapse and
     * the subsequent pPlatform->xMalloc deref would crash. */
    if (!interp || !interp->pPlatform) {
	TH8_TRACE_ERR(interp, "cannot malloc: NULL interpreter or platform");
	return NULL;
    }

    /*
     * Sandbox memory limit check.
     */

    if (interp->nAllocLimit > 0 &&
        interp->nAllocBytes + nByte > interp->nAllocLimit) {
	TH8_TRACE_ERR(interp, "memory limit exceeded");
	if (bPanic && interp->pPlatform->xPanic) {
	    interp->pPlatform->xPanic(
	        interp, interp->pPlatform->pCtx, "memory limit exceeded", 21);
	}
	return NULL;
    }

    /*
     * Stash call-site for the fault layer's per-site filter.  No-op
     * when fault injection is not installed on this interp.  Done
     * after the size/limit checks so non-allocating rejections don't
     * appear in nHit counts.  Bug 35: gated on TH8_ENABLE_FAULT_INJECTION
     * because the helper lives in th8_fault.c which is gated wholesale
     * on that flag.
     */
#if defined(TH8_ENABLE_FAULT_INJECTION)
    th8FaultStashSite(interp, zFile, nLine);
#endif

    p = interp->pPlatform->xMalloc(interp, interp->pPlatform->pCtx, nByte);
    if (p) {
	if (interp->pPlatform->xMemset) {
	    interp->pPlatform
	        ->xMemset(interp, interp->pPlatform->pCtx, p, 0, nByte);
	}

	/*
	 * Track the ACTUAL allocation size (which may be
	 * larger than requested due to allocator rounding).
	 */

	if (interp->pPlatform->xMemorySize) {
	    interp->nAllocBytes += interp->pPlatform->xMemorySize(
	        interp, interp->pPlatform->pCtx, p);
	} else {
	    interp->nAllocBytes += nByte;
	}
#if defined(TH8_MEM_DEBUG)
	th8MemTrackAlloc(interp, p, nByte);
#endif
    }
    if (!p) {
	TH8_TRACE_ERR(interp, "memory allocation failed");
	if (bPanic && interp->pPlatform->xPanic) {
	    interp->pPlatform->xPanic(
	        interp, interp->pPlatform->pCtx, "out of memory", 13);
	}
    }
    return p;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Malloc --
 *
 *	Allocate and zero-fill memory via the platform.  Calls xPanic
 *	on failure.  Use for bounded internal allocations (struct
 *	sizes, small fixed buffers).
 *
 * Why / How:
 *	Thin wrapper around th8MallocCommon with bPanic=1.  On
 *	failure, xPanic is invoked (which typically aborts), so
 *	the caller never sees a NULL return.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_Malloc(Th8_Interp *interp, size_t nByte)
{
    return th8MallocCommon(interp, nByte, 1, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_AttemptMalloc --
 *
 *	Allocate and zero-fill memory via the platform.  Returns NULL
 *	on failure without panicking.  Use for script-reachable
 *	allocations with unbounded sizes (string data, list buffers,
 *	file contents, user input).
 *
 *	Callers MUST check for NULL and return TH8_ERROR.
 *
 * Why / How:
 *	Thin wrapper around th8MallocCommon with bPanic=0.  Returns
 *	NULL on failure, allowing the caller to set an error message
 *	and propagate TH8_ERROR gracefully.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_AttemptMalloc(Th8_Interp *interp, size_t nByte)
{
    return th8MallocCommon(interp, nByte, 0, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAlloc --
 *
 *	Overflow-checked, traceable allocation with second-chance
 *	recovery.  This is the implementation behind TH8_ALLOC().
 *
 * Why / How:
 *	Validates nByte against TH8_MX_ALLOC (rejects pathological
 *	sizes before they reach the platform allocator), calls the
 *	normal allocation path, and on failure invokes the optional
 *	xNeedMemory callback for second-chance recovery.  In debug
 *	builds, failures are traced with the call site's file/line.
 *
 * Results:
 *	Pointer to allocated memory, or NULL on failure.
 *
 * Side effects:
 *	May call xNeedMemory.  Tracks allocation in nAllocBytes.
 *	May emit trace diagnostics in debug builds.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAlloc(Th8_Interp *interp, size_t nByte, const char *zFile, int nLine)
{
    void *p;

    /* Reject zero-byte and pathologically large requests. */
    if (nByte == 0 || nByte > TH8_MX_ALLOC) {
	TH8_TRACE_ERR(interp, "allocation size rejected");
	return NULL;
    }

    /* Normal allocation path.  Site (zFile, nLine) is passed
     * through so the fault layer can apply its per-site filter
     * (no-op when fault injection is not installed). */
    p = th8MallocCommon(interp, nByte, 0, zFile, nLine);

    /* Second-chance: xNeedMemory callback. */
    if (!p && interp->pPlatform->xNeedMemory) {
	p = interp->pPlatform->xNeedMemory(interp, nByte);
	if (p) {
	    /* Zero-fill and track, same as th8MallocCommon. */
	    if (interp->pPlatform->xMemset) {
		interp->pPlatform
		    ->xMemset(interp, interp->pPlatform->pCtx, p, 0, nByte);
	    }
	    if (interp->pPlatform->xMemorySize) {
		interp->nAllocBytes += interp->pPlatform->xMemorySize(
		    interp, interp->pPlatform->pCtx, p);
	    } else {
		interp->nAllocBytes += nByte;
	    }
	}
    }

#if defined(TH8_DEBUG)
    if (!p) {
	char buf[128];

	th8_spilornis_snprintf(
	    buf, sizeof(buf), "OOM: %lu bytes at %s:%d", (unsigned long)nByte,
	    zFile, nLine);
	TH8_TRACE_ERR(interp, buf);
    }
#else
    (void)zFile;
    (void)nLine;
#endif

    return p;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAllocStr --
 *
 *	Allocate nLen+1 bytes for a NUL-terminated string with
 *	overflow protection.
 *
 * Why / How:
 *	Guards against nLen == SIZE_MAX (where nLen+1 would wrap to 0).
 *	Delegates to th8SafeAlloc for size validation, platform
 *	dispatch, and second-chance recovery.
 *
 * Results:
 *	Pointer to allocated memory (nLen+1 bytes), or NULL.
 *
 * Side effects:
 *	Same as th8SafeAlloc.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAllocStr(
    Th8_Interp *interp,
    size_t nLen,
    const char *zFile,
    int nLine)
{
    size_t nByte = 0;

    if (TH8_SAFE_ADD_SIZE(nLen, 1, &nByte)) return NULL;
    return Th8_SafeAlloc(interp, nByte, zFile, nLine);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAllocMul --
 *
 *	Allocate a*b bytes with overflow-safe multiplication.
 *
 * Why / How:
 *	Validates that a*b does not overflow size_t before calling
 *	th8SafeAlloc.  This catches the common pattern of
 *	nElements * sizeof(Element) where a crafted element count
 *	could wrap the product to a small value.
 *
 * Results:
 *	Pointer to allocated memory (a*b bytes), or NULL.
 *
 * Side effects:
 *	Same as th8SafeAlloc.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAllocMul(
    Th8_Interp *interp,
    size_t a,
    size_t b,
    const char *zFile,
    int nLine)
{
    size_t nByte = 0;

    if (TH8_SAFE_MUL_SIZE(a, b, &nByte)) return NULL;
    return Th8_SafeAlloc(interp, nByte, zFile, nLine);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAllocAdd --
 *
 *	Allocate a+b bytes with overflow-safe addition.
 *
 * Why / How:
 *	Validates that a+b does not overflow size_t before calling
 *	th8SafeAlloc.  This catches the common pattern of
 *	baseSize + extraBytes where a script-controlled base could
 *	wrap the sum.
 *
 * Results:
 *	Pointer to allocated memory (a+b bytes), or NULL.
 *
 * Side effects:
 *	Same as th8SafeAlloc.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAllocAdd(
    Th8_Interp *interp,
    size_t a,
    size_t b,
    const char *zFile,
    int nLine)
{
    size_t nByte = 0;

    if (TH8_SAFE_ADD_SIZE(a, b, &nByte)) return NULL;
    return Th8_SafeAlloc(interp, nByte, zFile, nLine);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAllocMulAdd --
 *
 *	Allocate a*b+c bytes with overflow-safe arithmetic.
 *
 * Why / How:
 *	The most common allocation pattern is:
 *	    nElements * sizeof(Element) + headerSize
 *	This function validates both the multiplication and the
 *	subsequent addition for overflow.  If either overflows,
 *	returns NULL without allocating.
 *
 * Results:
 *	Pointer to allocated memory (a*b+c bytes), or NULL.
 *
 * Side effects:
 *	Same as th8SafeAlloc.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAllocMulAdd(
    Th8_Interp *interp,
    size_t a,
    size_t b,
    size_t c,
    const char *zFile,
    int nLine)
{
    size_t nProduct = 0, nByte = 0;

    if (TH8_SAFE_MUL_SIZE(a, b, &nProduct)) return NULL;
    if (TH8_SAFE_ADD_SIZE(nProduct, c, &nByte)) return NULL;
    return Th8_SafeAlloc(interp, nByte, zFile, nLine);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAllocStrAdd --
 *
 *	Allocate k + n + 1 bytes (overflow-checked at every step).
 *
 * Why / How:
 *	The "prefix + tail + NUL" string buffer pattern.  Replaces the
 *	anti-pattern Th8_SafeAllocAdd(interp, k, n + 1, ...), where the
 *	inner (n + 1) addition is plain C size_t arithmetic that wraps
 *	silently to 0 on overflow.  Two safe-add steps inside this
 *	function ensure both (k + n) and (k + n + 1) are validated.
 *
 * Results:
 *	Pointer to allocated memory (k+n+1 bytes), or NULL on overflow
 *	or allocation failure.
 *
 * Side effects:
 *	Same as Th8_SafeAlloc.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAllocStrAdd(
    Th8_Interp *interp,
    size_t k,
    size_t n,
    const char *zFile,
    int nLine)
{
    size_t nSum = 0, nByte = 0;

    if (TH8_SAFE_ADD_SIZE(k, n, &nSum)) return NULL;
    if (TH8_SAFE_ADD_SIZE(nSum, 1, &nByte)) return NULL;
    return Th8_SafeAlloc(interp, nByte, zFile, nLine);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAllocStrMul --
 *
 *	Allocate (n + 1) * sz bytes (overflow-checked at every step).
 *
 * Why / How:
 *	The "wide-string buffer with NUL slot" pattern.  Replaces the
 *	anti-pattern Th8_SafeAllocMul(interp, n + 1, sz, ...), where the
 *	inner (n + 1) addition is plain C size_t arithmetic that wraps
 *	silently to 0 on overflow.  Algebraic identity:
 *	    (n + 1) * sz == n * sz + sz
 *	One safe-mul step (n * sz) plus one safe-add step (+ sz)
 *	validates both the product and the per-element NUL slot.
 *
 * Results:
 *	Pointer to allocated memory ((n+1)*sz bytes), or NULL on
 *	overflow or allocation failure.
 *
 * Side effects:
 *	Same as Th8_SafeAlloc.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAllocStrMul(
    Th8_Interp *interp,
    size_t n,
    size_t sz,
    const char *zFile,
    int nLine)
{
    size_t nProduct = 0, nByte = 0;

    if (TH8_SAFE_MUL_SIZE(n, sz, &nProduct)) return NULL;
    if (TH8_SAFE_ADD_SIZE(nProduct, sz, &nByte)) return NULL;
    return Th8_SafeAlloc(interp, nByte, zFile, nLine);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAllocMulAdd2 --
 *
 *	Allocate (a*b) + (c*d) + e bytes with four sequential overflow
 *	checks: (a*b), (c*d), (sum of products), (+ e).
 *
 * Why / How:
 *	The packed-record allocation pattern, e.g.,
 *	    nE * sizeof(char *) + nE * sizeof(size_t) + nStrTotal
 *	for buffers that pack pointer arrays, length arrays, and string
 *	data into a single allocation.  Pre-computing this size in plain
 *	C size_t arithmetic risks silent overflow at any of the four
 *	internal steps; this function validates each step explicitly.
 *
 * Results:
 *	Pointer to allocated memory ((a*b)+(c*d)+e bytes), or NULL on
 *	overflow at any step or allocation failure.
 *
 * Side effects:
 *	Same as Th8_SafeAlloc.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAllocMulAdd2(
    Th8_Interp *interp,
    size_t a,
    size_t b,
    size_t c,
    size_t d,
    size_t e,
    const char *zFile,
    int nLine)
{
    size_t t1 = 0, t2 = 0, t3 = 0, nByte = 0;

    if (TH8_SAFE_MUL_SIZE(a, b, &t1)) return NULL;
    if (TH8_SAFE_MUL_SIZE(c, d, &t2)) return NULL;
    if (TH8_SAFE_ADD_SIZE(t1, t2, &t3)) return NULL;
    if (TH8_SAFE_ADD_SIZE(t3, e, &nByte)) return NULL;
    return Th8_SafeAlloc(interp, nByte, zFile, nLine);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeMul --
 *
 *	Compute a * b into *pOut with size_t overflow detection.
 *
 * Why / How:
 *	Public wrapper around the internal TH8_SAFE_MUL_SIZE primitive.
 *	Exposed for plugins and embedders that need to perform safe
 *	arithmetic on size_t values prior to (or independent of)
 *	allocation -- for example, computing buffer offsets, capacity
 *	doublings, or composite size calculations that must be passed
 *	to a single TH8_ALLOC at the end.
 *
 * Results:
 *	TH8_OK on success (*pOut filled in), TH8_ERROR if the
 *	multiplication would overflow (*pOut left unchanged).
 *	If pOut is NULL, returns TH8_ERROR without storing anything.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SafeMul(Th8_Interp *interp, size_t a, size_t b, size_t *pOut)
{
    (void)interp;
    if (!pOut) return TH8_ERROR;
    if (TH8_SAFE_MUL_SIZE(a, b, pOut)) return TH8_ERROR;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAdd --
 *
 *	Compute a + b into *pOut with size_t overflow detection.
 *
 * Why / How:
 *	Public wrapper around the internal TH8_SAFE_ADD_SIZE primitive.
 *	Companion to Th8_SafeMul.  Use these primitives when chaining
 *	safe arithmetic for sizes that don't fit any of the existing
 *	allocation macros (e.g., complex four-or-more-term sums) or
 *	for non-allocation size math (offsets, capacity, indices).
 *
 * Results:
 *	TH8_OK on success (*pOut filled in), TH8_ERROR if the
 *	addition would overflow (*pOut left unchanged).
 *	If pOut is NULL, returns TH8_ERROR without storing anything.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SafeAdd(Th8_Interp *interp, size_t a, size_t b, size_t *pOut)
{
    (void)interp;
    if (!pOut) return TH8_ERROR;
    if (TH8_SAFE_ADD_SIZE(a, b, pOut)) return TH8_ERROR;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Free --
 *
 *	Free memory via the platform.
 *
 * Why / How:
 *	Dispatches to xFree through the platform vtable.  Before
 *	freeing, queries xMemorySize to subtract the actual block
 *	size from nAllocBytes, keeping the allocation tracker
 *	accurate.  NULL pointers are silently ignored.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is released.
 *
 *----------------------------------------------------------------------
 */

void
Th8_Free(
    Th8_Interp *interp, /* Interpreter for platform access. */
    void *p) /* Block to free (may be NULL). */
{
    if (!interp) return;
    /* Bug 26 (2026-06-09): plain check rather than ALWAYS --
     * interp->pPlatform CAN be NULL during teardown, and the
     * subsequent pPlatform->xFree deref would crash under
     * TH8_OMIT collapse.  Surfaced by the platform-exchange
     * MC/DC drive against Th8_Load L750. */
    if (p && interp->pPlatform) {
	void (*xFree)(Th8_Interp *, void *, void *) = NULL;
	xFree = interp->pPlatform->xFree;
	if (xFree) {
	    size_t (*xMemorySize)(Th8_Interp *, void *, void *) = NULL;
#if defined(TH8_MEM_DEBUG)
	    /* Untrack while the address is still valid, before xFree. */
	    th8MemTrackFree(interp, p);
#endif
	    xMemorySize = interp->pPlatform->xMemorySize;
	    if (xMemorySize) {
		size_t
		    nBlock = xMemorySize(interp, interp->pPlatform->pCtx, p);
		if (nBlock <= interp->nAllocBytes) {
		    interp->nAllocBytes -= nBlock;
		} else {
		    interp->nAllocBytes = 0;
		}
	    }
	    xFree(interp, interp->pPlatform->pCtx, p);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8ReallocCommon --
 *
 *	Common implementation for Th8_Realloc and Th8_AttemptRealloc.
 *
 * Why / How:
 *	Centralizes realloc logic: checks the sandbox memory limit
 *	(only when growing), dispatches to xRealloc, and updates
 *	nAllocBytes by subtracting the old block size and adding the
 *	new.  The bPanic flag controls whether failure invokes xPanic
 *	or returns NULL.
 *
 *----------------------------------------------------------------------
 */

static void *
th8ReallocCommon(
    Th8_Interp *interp,
    void *p,
    size_t nByte,
    int bPanic,
    const char *zFile, /* Caller __FILE__ for fault filter. */
    int nLine) /* Caller __LINE__ for fault filter. */
{
    void *pNew;
    size_t nOldSize = 0;

    /* Bug 26 family: plain guards (Bug 31 pattern). */
    if (!interp || !interp->pPlatform) {
	return 0;
    }

    if (p && ALWAYS(interp->pPlatform->xMemorySize)) {
	nOldSize = interp->pPlatform
	               ->xMemorySize(interp, interp->pPlatform->pCtx, p);
    }

    if (interp->nAllocLimit > 0 && nByte > nOldSize) {
	size_t nDelta = nByte - nOldSize;

	if (interp->nAllocBytes + nDelta > interp->nAllocLimit) {
	    TH8_TRACE_ERR(interp, "memory limit exceeded");
	    if (bPanic && interp->pPlatform->xPanic) {
		interp->pPlatform->xPanic(
		    interp, interp->pPlatform->pCtx, "memory limit exceeded",
		    21);
	    }
	    return NULL;
	}
    }

#if defined(TH8_ENABLE_FAULT_INJECTION)
    /* Stash call-site for the fault layer's per-site filter. */
    th8FaultStashSite(interp, zFile, nLine);
#endif

    pNew = interp->pPlatform
               ->xRealloc(interp, interp->pPlatform->pCtx, p, nByte);
    if (pNew) {
	if (ALWAYS(nOldSize <= interp->nAllocBytes)) {
	    interp->nAllocBytes -= nOldSize;
	} else {
	    interp->nAllocBytes = 0;
	}
	if (interp->pPlatform->xMemorySize) {
	    interp->nAllocBytes += interp->pPlatform->xMemorySize(
	        interp, interp->pPlatform->pCtx, pNew);
	} else {
	    interp->nAllocBytes += nByte;
	}
#if defined(TH8_MEM_DEBUG)
	th8MemTrackRealloc(interp, p, pNew, nByte);
#endif
    }
    if (!pNew) {
	TH8_TRACE_ERR(interp, "memory allocation failed");
	if (bPanic && interp->pPlatform->xPanic) {
	    interp->pPlatform->xPanic(
	        interp, interp->pPlatform->pCtx, "out of memory", 13);
	}
    }
    return pNew;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Realloc --
 *
 *	Resize a previously allocated memory block, panicking on
 *	failure.  This is the standard reallocation entry point for
 *	internal engine code that cannot tolerate allocation failure.
 *
 * Why / How:
 *	Delegates to th8ReallocCommon with bPanic=1.  On failure,
 *	xPanic is invoked (which typically aborts the process),
 *	so the caller never sees a NULL return.
 *
 * Results:
 *	Pointer to the resized block.  Never returns NULL (panics
 *	instead).
 *
 * Side effects:
 *	The old block may be moved; interp->nAllocBytes is updated.
 *	On failure, xPanic is called (process may abort).
 *
 *----------------------------------------------------------------------
 */

void *
Th8_Realloc(Th8_Interp *interp, void *p, size_t nByte)
{
    return th8ReallocCommon(interp, p, nByte, 1, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_AttemptRealloc --
 *
 *	Resize a previously allocated block.  Returns NULL on failure
 *	without panicking.  The original block is NOT freed on failure.
 *
 *	Callers MUST check for NULL and return TH8_ERROR.
 *
 * Why / How:
 *	Thin wrapper around th8ReallocCommon with bPanic=0.  Returns
 *	NULL on failure, leaving the original block untouched so
 *	the caller can free it after setting an error.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_AttemptRealloc(Th8_Interp *interp, void *p, size_t nByte)
{
    return th8ReallocCommon(interp, p, nByte, 0, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeRealloc --
 *
 *	Reallocate with size sanity checks and __FILE__/__LINE__
 *	tracing.  Behaviour mirrors Th8_Realloc (panics via xPanic on
 *	OOM); use via the TH8_REALLOC macro so call sites capture
 *	__FILE__/__LINE__ automatically.
 *
 *	Pathological sizes (zero, or larger than TH8_MX_ALLOC) are
 *	rejected with a trace and a NULL return.  This matches the
 *	Th8_SafeAlloc contract for non-realloc allocations.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeRealloc(
    Th8_Interp *interp,
    void *p,
    size_t nByte,
    const char *zFile,
    int nLine)
{
    if (nByte == 0 || nByte > TH8_MX_ALLOC) {
	TH8_TRACE_ERR(interp, "realloc size rejected");
#if defined(TH8_DEBUG)
	{
	    char buf[128];
	    th8_spilornis_snprintf(
	        buf, sizeof(buf), "realloc rejected: %lu bytes at %s:%d",
	        (unsigned long)nByte, zFile, nLine);
	    TH8_TRACE_ERR(interp, buf);
	}
#else
	(void)zFile;
	(void)nLine;
#endif
	return NULL;
    }
    {
	void *pNew = th8ReallocCommon(interp, p, nByte, 1, zFile, nLine);
#if defined(TH8_DEBUG)
	if (!pNew) {
	    char buf[128];
	    th8_spilornis_snprintf(
	        buf, sizeof(buf), "OOM (realloc): %lu bytes at %s:%d",
	        (unsigned long)nByte, zFile, nLine);
	    TH8_TRACE_ERR(interp, buf);
	}
#else
	(void)zFile;
	(void)nLine;
#endif
	return pNew;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SafeAttemptRealloc --
 *
 *	Like Th8_SafeRealloc but returns NULL on OOM rather than
 *	panicking.  Mirrors Th8_AttemptRealloc.  Use via the
 *	TH8_ATTEMPT_REALLOC macro.  The original block is NOT freed
 *	on failure; the caller can recover or report TH8_ERROR.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_SafeAttemptRealloc(
    Th8_Interp *interp,
    void *p,
    size_t nByte,
    const char *zFile,
    int nLine)
{
    if (nByte == 0 || nByte > TH8_MX_ALLOC) {
	TH8_TRACE_ERR(interp, "attempt-realloc size rejected");
	(void)zFile;
	(void)nLine;
	return NULL;
    }
    {
	void *pNew = th8ReallocCommon(interp, p, nByte, 0, zFile, nLine);
#if defined(TH8_DEBUG)
	if (!pNew) {
	    char buf[128];
	    th8_spilornis_snprintf(
	        buf, sizeof(buf), "OOM (attempt-realloc): %lu bytes at %s:%d",
	        (unsigned long)nByte, zFile, nLine);
	    TH8_TRACE_ERR(interp, buf);
	}
#else
	(void)zFile;
	(void)nLine;
#endif
	return pNew;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPlatform --
 *
 *	Return the platform pointer for the given interpreter.
 *
 * Why / How:
 *	Provides read-only access to the platform vtable so that
 *	modules outside th8_core.c can call platform callbacks
 *	without accessing interpreter internals directly.
 *
 *----------------------------------------------------------------------
 */

const Th8_Platform *
Th8_GetPlatform(Th8_Interp *interp)
{
    return interp->pPlatform;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPackageHash --
 *
 *	Return the interpreter's package registry hash table.
 *
 * Why / How:
 *	The package subsystem (th8_pkg.c) stores loaded package
 *	records in a hash keyed by package name.  This accessor
 *	hides the interpreter struct layout from that module.
 *
 *----------------------------------------------------------------------
 */

Th8_Hash *
Th8_GetPackageHash(Th8_Interp *interp) /* Interpreter. */
{
    return interp->paPackage;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_GetMathFuncHash --
 *
 *	Return (and lazily create) the interpreter's math function
 *	registry hash table.
 *
 * Why / How:
 *	Math functions are optional (expr extensions), so the hash
 *	is only allocated on first use to avoid overhead when
 *	no math functions are registered.
 *
 *----------------------------------------------------------------------
 */

Th8_Hash *
Th8_GetMathFuncHash(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp->paMathFunc) {
	interp->paMathFunc = Th8_HashNew(interp);
    }
    return interp->paMathFunc;
}

/*
 *----------------------------------------------------------------------
 *
 * th8GetArraySearchHash --
 *
 *	Return (and lazily create) the per-interp array search
 *	registry.  Internal only; no public TH8_API.
 *
 * Why / How:
 *	The [array startsearch] / nextelement / anymore /
 *	donesearch commands store per-search state here, keyed by
 *	the search-id string.  The hash is per-interpreter for
 *	thread-safety (each Th8_Interp is single-threaded but
 *	multiple interps may run concurrently in different
 *	threads).  Allocation is lazy so interpreters that never
 *	use the search API pay no overhead.
 *
 *----------------------------------------------------------------------
 */

Th8_Hash *
th8GetArraySearchHash(
    Th8_Interp *interp, /* Interpreter. */
    int bCreate) /* Non-zero: allocate if absent. */
{
    if (!interp->paArraySearch && bCreate) {
	interp->paArraySearch = Th8_HashNew(interp);
    }
    return interp->paArraySearch;
}

/*
 *----------------------------------------------------------------------
 *
 * th8NextArraySearchId --
 *
 *	Return the next monotonic counter for array search IDs.
 *
 * Why / How:
 *	Each [array startsearch] generates a unique search-id of
 *	the form "s-N-arrayName" where N is per-interp monotonic.
 *	Using a per-interp counter (instead of process-static)
 *	keeps search IDs from one interpreter from colliding with
 *	another's, even if interpreters share a process.
 *
 *----------------------------------------------------------------------
 */

int
th8NextArraySearchId(Th8_Interp *interp) /* Interpreter. */
{
    return ++interp->iArraySearchCounter;
}

/*
 *----------------------------------------------------------------------
 *
 * Per-pState event queue --
 *
 *	The thread-safe event queue lives entirely on each
 *	Th8_AsyncState (see th8_int_core.h).  Producers
 *	(Th8_QueueEvent, callable from any thread) hand work to
 *	one specific pState; the consumer (the interp's owning
 *	thread, via Th8_DrainQueueEvents or [update] / [vwait])
 *	walks every live pState on the interp and drains each.
 *
 *	The functions in this section are split into three
 *	groups:
 *
 *	  - Setup / lifecycle: Th8_CreateAsyncState,
 *	    Th8_FinalizeAsyncState, th8EventQueueAvailable.
 *	  - Cross-thread producer: Th8_QueueEvent and the
 *	    th8SignalEvent helper used by cancel/freeze hooks.
 *	  - Same-thread consumer: th8DrainOneStateEvent,
 *	    th8DrainAll, th8AnyEventQueued, th8PStateQueueLen,
 *	    th8FreePStateEvents, plus Th8_DrainQueueEvents (the
 *	    public high-level operation) and th8ResetEvent /
 *	    th8WaitEvent (used by [vwait]).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8EventOverflowConsHead / th8EventOverflowPopHead --
 *
 *	Helper macros for the per-pState overflow linked list.
 *	The list is a singly-linked queue with O(1) tail insert
 *	(via pOverflowTail) and O(1) head pop.  Both must be
 *	called with the pState's queueMutex held.
 *
 *	ConsHead inserts pEv at the tail (so existing entries
 *	pop first - FIFO).  PopHead removes the head and
 *	returns it via pOut, NULL if empty.  These are macros
 *	rather than functions so they can be inlined trivially
 *	at call sites that already hold the lock.
 *
 *----------------------------------------------------------------------
 */

#define th8EventOverflowAppend(pState, pEv)                                  \
    do {                                                                     \
	(pEv)->pNext = NULL;                                                 \
	if ((pState)->pOverflowTail) {                                       \
	    (pState)->pOverflowTail->pNext = (pEv);                          \
	} else {                                                             \
	    (pState)->pOverflowHead = (pEv);                                 \
	}                                                                    \
	(pState)->pOverflowTail = (pEv);                                     \
	(pState)->nOverflow++;                                               \
    } while (0)

#define th8EventOverflowPopHead(pState, pOut)                                \
    do {                                                                     \
	Th8_Event *pH = (pState)->pOverflowHead;                             \
	(pOut) = pH;                                                         \
	if (pH) {                                                            \
	    (pState)->pOverflowHead = pH->pNext;                             \
	    if (!(pState)->pOverflowHead) (pState)->pOverflowTail = NULL;    \
	    (pState)->nOverflow--;                                           \
	}                                                                    \
    } while (0)


/*
 *----------------------------------------------------------------------
 *
 * th8EventQueueAvailable --
 *
 *	Called from Th8_CreateAsyncState ONLY.  Validates that the
 *	interp's platform supplies every callback the event queue
 *	depends on, and atomically populates the pState's cached
 *	pointer set from the platform.  This is the single point
 *	at which a pState's event-queue dependencies become
 *	"locked in" - after this returns TH8_OK, the pState's own
 *	cached pointers are the only thing cross-thread code reads.
 *
 *	This function is thread-confined to the interp's owning
 *	thread (the only thread allowed to call
 *	Th8_CreateAsyncState).  It does NOT take any lock, because
 *	by contract no concurrent producer exists yet.
 *
 * Results:
 *	TH8_OK if every required platform callback is present and
 *	the pState's cached pointers were populated.  TH8_ERROR if
 *	any required callback is NULL.  No partial population: on
 *	error, the pState's cached fields are left untouched.
 *
 * Side effects:
 *	On TH8_OK, sets every cached function pointer + pPlatCtx
 *	on pState.  Sets nothing else (queue, handle, mutex are
 *	the caller's responsibility).
 *
 *----------------------------------------------------------------------
 */

int
th8EventQueueAvailable(Th8_Interp *interp, Th8_AsyncState *pState)
{
    Th8_Platform *p;
    /* Bug 26 family: plain guards (Bug 31 pattern). */
    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!pState) return TH8_ERROR;
    p = interp->pPlatform;
    if (!p) return TH8_ERROR;
    if (!TH8_CHECK_EVENT_CALLBACKS(p)) return TH8_ERROR;

    pState->xMalloc = p->xMalloc;
    pState->xFree = p->xFree;
    pState->xMutexInit = p->xMutexInit;
    pState->xMutexFinal = p->xMutexFinal;
    pState->xMutexEnter = p->xMutexEnter;
    pState->xMutexLeave = p->xMutexLeave;
    pState->xEventCreate = p->xEventCreate;
    pState->xEventDestroy = p->xEventDestroy;
    pState->xEventSet = p->xEventSet;
    pState->xEventReset = p->xEventReset;
    pState->xEventWait = p->xEventWait;
    pState->xIntCmpXchg = p->xIntCmpXchg;
    pState->pPlatCtx = p->pCtx;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SignalEvent --
 *
 *	Set the pState's manual-reset event handle to "signaled".
 *	Wakes any thread currently in xEventWait on this handle.
 *	Thread-safe: the underlying xEventSet is required to be
 *	thread-safe by the platform contract, and the handle
 *	pointer is stable for the lifetime of the pState (created
 *	in Th8_CreateAsyncState, destroyed in
 *	Th8_FinalizeAsyncState - the embedder ensures no concurrent
 *	finalize).
 *
 *	Used by Th8_QueueEvent itself (under the queue mutex), by
 *	Th8_CancelEval / Th8_Freeze (via th8SignalAllStates), and
 *	by [vwait]'s wake-up path indirectly through xEventWait.
 *
 * Side effects:
 *	Transitions pEventHandle to "signaled" state.  Subsequent
 *	xEventWait calls on this handle return immediately until
 *	xEventReset is called.
 *
 *----------------------------------------------------------------------
 */

void
th8SignalEvent(Th8_AsyncState *pState)
{
    /* Bug 26: pState->pEventHandle is NULLed by Th8_FinalizeAsyncState
     * (L2645); a concurrent th8SignalEvent on the same pState after
     * finalize started would observe NULL.  xEventSet is set once at
     * th8_state_init but the field is independent.  Use plain `if`.
     * Split per Finding 005 -- each guard is a single-condition
     * decision so the OOM/finalize-race C-pairs do not live in
     * the MC/DC denominator. */
    if (!pState) return;
    if (!pState->xEventSet) return;
    if (!pState->pEventHandle) return;
    pState->xEventSet(NULL, pState->pPlatCtx, pState->pEventHandle);
}


/*
 *----------------------------------------------------------------------
 *
 * th8ResetEvent --
 *
 *	Transition the pState's event handle back to "non-signaled"
 *	so a subsequent xEventWait can block.  Called by [vwait]
 *	immediately before each xEventWait, after the queue and
 *	predicates have been re-checked (the standard manual-reset
 *	event idiom: Reset => re-check => Wait, to close the
 *	signal-during-Reset window).
 *
 *	Same-thread only.  Reset is meaningful only on the consumer
 *	thread; producers Set without ever Resetting.
 *
 *----------------------------------------------------------------------
 */

void
th8ResetEvent(Th8_AsyncState *pState)
{
    /* Bug 26: same race-with-finalize hazard as th8SignalEvent above.
     * Split per Finding 005. */
    if (!pState) return;
    if (!pState->xEventReset) return;
    if (!pState->pEventHandle) return;
    pState->xEventReset(NULL, pState->pPlatCtx, pState->pEventHandle);
}


/*
 *----------------------------------------------------------------------
 *
 * th8WaitEvent --
 *
 *	Block on the pState's event handle until signaled, the
 *	timeout expires, or an early-wake condition fires.
 *	Returns one of the TH8_WAIT_* constants (defined in
 *	th8.h):
 *
 *	   TH8_WAIT_OK       - the handle was signaled.
 *	   TH8_WAIT_RETRY    - early wake (APC on Win32, EINTR on
 *	                       POSIX); the caller should re-poll.
 *	   TH8_WAIT_ERROR    - the wait failed (handle invalid);
 *	                       the caller should bail out.
 *	   TH8_WAIT_TIMEOUT  - the deadline elapsed without signal.
 *
 *	nTimeoutMs of -1 means infinite wait.  Hard-fails with
 *	TH8_WAIT_ERROR if pState lacks the required cached
 *	xEventWait/pEventHandle (this should never happen since
 *	Th8_CreateAsyncState validates these, but the check lets
 *	the failure surface as a distinct return code rather than
 *	a NULL-deref).
 *
 *----------------------------------------------------------------------
 */

int
th8WaitEvent(Th8_AsyncState *pState, int nTimeoutMs)
{
    /* Bug 26: same race-with-finalize hazard as th8SignalEvent above.
     * Split per Finding 005. */
    if (!pState) return TH8_WAIT_ERROR;
    if (!pState->xEventWait) return TH8_WAIT_ERROR;
    if (!pState->pEventHandle) return TH8_WAIT_ERROR;
    return pState->xEventWait(
        NULL, pState->pPlatCtx, pState->pEventHandle, nTimeoutMs);
}


/*
 *----------------------------------------------------------------------
 *
 * th8PStateQueueLen --
 *
 *	Return the number of pending callbacks queued on a single
 *	pState (sum of static + overflow).  Acquires the pState's
 *	queue mutex briefly.  Used internally for FIFO drain
 *	bookkeeping; not exposed publicly because callers should
 *	use the high-level Th8_DrainQueueEvents instead of polling
 *	a length.
 *
 *----------------------------------------------------------------------
 */

int
th8PStateQueueLen(Th8_AsyncState *pState)
{
    int n;
    if (!pState || !pState->bMutexReady) return 0;
    pState->xMutexEnter(NULL, pState->pPlatCtx, &pState->queueMutex);
    n = pState->nStatic + pState->nOverflow;
    pState->xMutexLeave(NULL, pState->pPlatCtx, &pState->queueMutex);
    return n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8AnyEventQueued --
 *
 *	Predicate: TRUE if any pState registered with the interp
 *	has at least one queued callback.  Used by [vwait]'s wait
 *	loop to decide whether to drain or sleep.  Walks the
 *	registry list; skips finalized nodes (those whose pState
 *	is gone) and reads each live pState's count under its
 *	own mutex.  Single-threaded: must be called on the
 *	interp's owning thread.
 *
 *----------------------------------------------------------------------
 */

int
th8AnyEventQueued(Th8_Interp *interp)
{
    Th8_AsyncStateNode *pNode;
    if (!interp) return 0;
    for (pNode = interp->pAsyncStateHead; pNode; pNode = pNode->pNext) {
	Th8_AsyncState *pState;
	if (pNode->nFinalized) continue;
	pState = pNode->pState;
	if (!pState) continue;
	if (th8PStateQueueLen(pState) > 0) return 1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8DrainOneStateEvent --
 *
 *	Pop one callback from a pState's queue (under the pState's
 *	mutex) and invoke it on the calling thread (with the mutex
 *	released).  When the static portion is non-empty it pops
 *	from the head of the static circular buffer; if the
 *	overflow list is non-empty afterward, the freed slot is
 *	immediately backfilled with the overflow head, preserving
 *	FIFO order across the static/overflow boundary.
 *
 *	*pbDrained is set to 1 if a callback was popped (and
 *	invoked), 0 if the queue was empty.  This lets the caller
 *	loop without re-checking length.
 *
 * Results:
 *	The callback's return code on success.  TH8_OK if the
 *	queue was empty (with *pbDrained==0).  TH8_ERROR only on
 *	input validation (NULL pState).
 *
 *----------------------------------------------------------------------
 */

int
th8DrainOneStateEvent(Th8_AsyncState *pState, int *pbDrained)
{
    int (*xCb)(Th8_Interp *, void *) = NULL;
    Th8_Interp *interp;
    void *pCtx;

    if (pbDrained) *pbDrained = 0;
    if (!pState || !pState->bMutexReady) return TH8_ERROR;

    pState->xMutexEnter(NULL, pState->pPlatCtx, &pState->queueMutex);
    if (pState->nStatic > 0) {
	xCb = pState->aStatic[pState->iHead];
	pState->iHead = (pState->iHead + 1) % TH8_EVENT_QUEUE_STATIC_N;
	pState->nStatic--;
	/* Backfill the freed slot from the overflow head, if any. */
	if (pState->pOverflowHead) {
	    Th8_Event *pEv;
	    int iSlot;
	    th8EventOverflowPopHead(pState, pEv);
	    iSlot = (pState->iHead + pState->nStatic) %
	            TH8_EVENT_QUEUE_STATIC_N;
	    pState->aStatic[iSlot] = pEv->xCallback;
	    pState->nStatic++;
	    pState->xFree(NULL, pState->pPlatCtx, pEv);
	}
    }
    pState->xMutexLeave(NULL, pState->pPlatCtx, &pState->queueMutex);

    if (!xCb) return TH8_OK;
    if (pbDrained) *pbDrained = 1;

    /* Invoke OUTSIDE the mutex; the callback may itself queue
     * more events on this pState (or any other), and may
     * take other locks.  pInterp is required for the call;
     * if it's been NULL'd by interp delete, drop the event
     * silently - we're about to be torn down.              */
    interp = pState->pInterp;
    if (!interp) return TH8_OK;
    pCtx = pState->pCtx;
    return xCb(interp, pCtx);
}


/*
 *----------------------------------------------------------------------
 *
 * th8DrainAll --
 *
 *	Walk every live pState on the interp and drain it in
 *	FIFO order.  Used by Th8_DrainQueueEvents (with no
 *	limit) and by the [update] command (with optional
 *	-limit N).  Single-threaded (interp's owning thread).
 *
 *	nLimit:
 *	  < 0  unlimited - drain everything currently queued.
 *	  == 0 no callbacks invoked (no-op other than the walk).
 *	  > 0  drain at most this many callbacks total across
 *	       all pStates.
 *
 *	*pnProcessed (optional, may be NULL): set to the number
 *	of callbacks actually invoked.
 *
 *	If a callback returns non-OK, drain stops at that point
 *	and returns that code; later events stay queued.
 *
 *	A callback may itself enqueue events.  New events on the
 *	same pState appear after the drain pointer and will be
 *	processed on the same walk.  New events on a pState
 *	already drained on this walk are NOT picked up; they
 *	wait for the next drain call.  This keeps the walk
 *	bounded.
 *
 *----------------------------------------------------------------------
 */

int
th8DrainAll(Th8_Interp *interp, int nLimit, int *pnProcessed)
{
    int nProcessed = 0;
    if (pnProcessed) *pnProcessed = 0;
    if (!interp) return TH8_ERROR;

    /* Round-robin walk: in each pass, visit every live pState
     * and drain at most ONE event from it.  Looping back to
     * the registry list each pass means we always re-fetch
     * pState through pNode (which is interp-owned and stable);
     * a callback that finalized its OWN pState during the
     * previous step is safely detected via pNode->nFinalized
     * on the next visit.  Without the re-fetch, the pState
     * pointer in our stack frame would dangle.
     *
     * The pass is repeated until a full pass drains nothing,
     * which means the queues are empty (or the limit is hit).
     */
    for (;;) {
	Th8_AsyncStateNode *pNode;
	int bAnyDrained = 0;
	for (pNode = interp->pAsyncStateHead; pNode; pNode = pNode->pNext) {
	    Th8_AsyncState *pState;
	    int bDrained = 0;
	    int rc;
	    if (nLimit >= 0 && nProcessed >= nLimit) {
		if (pnProcessed) *pnProcessed = nProcessed;
		return TH8_OK;
	    }
	    if (pNode->nFinalized) continue;
	    pState = pNode->pState;
	    if (!pState) continue;
	    rc = th8DrainOneStateEvent(pState, &bDrained);
	    if (!bDrained) continue;
	    nProcessed++;
	    bAnyDrained = 1;
	    if (rc != TH8_OK) {
		if (pnProcessed) *pnProcessed = nProcessed;
		return rc;
	    }
	}
	if (!bAnyDrained) break;
    }
    if (pnProcessed) *pnProcessed = nProcessed;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8PlatformHasEventQueue --
 *
 *	Predicate: TRUE if the interp's platform has every
 *	callback the event queue depends on.  Used by the
 *	[update] / [vwait] commands to fail loud at dispatch
 *	time when the platform doesn't support the queue, so
 *	scripts get a clear error rather than a silent no-op.
 *	Same-thread only.
 *
 *----------------------------------------------------------------------
 */

int
th8PlatformHasEventQueue(Th8_Interp *interp)
{
    Th8_Platform *p;
    if (!interp) return 0;
    p = interp->pPlatform;
    if (!p) return 0;
    return TH8_CHECK_EVENT_CALLBACKS(p) ? 1 : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DrainQueueEvents --
 *
 *	Public wrapper around th8DrainAll with no limit.  Drains
 *	every queued event on every live pState registered with
 *	the interp.  Single-threaded: must be called on the
 *	interp's owning thread.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DrainQueueEvents(Th8_Interp *interp)
{
    return th8DrainAll(interp, -1, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreePStateEvents --
 *
 *	Drop all queued callbacks on a pState WITHOUT invoking
 *	them.  Frees every overflow Th8_Event node and resets
 *	the static buffer's count.  Used by Th8_FinalizeAsyncState
 *	(the pState is going away - callbacks would have nowhere
 *	to run) and by Th8_DeleteInterp's per-pState teardown.
 *
 *	Caller must ensure no producer is concurrently in
 *	Th8_QueueEvent on this pState.  In practice this is
 *	achieved by joining all worker threads before calling
 *	Finalize.
 *
 *----------------------------------------------------------------------
 */

void
th8FreePStateEvents(Th8_AsyncState *pState)
{
    if (!pState) return;
    if (pState->bMutexReady) {
	pState->xMutexEnter(NULL, pState->pPlatCtx, &pState->queueMutex);
    }
    while (pState->pOverflowHead) {
	Th8_Event *pEv = pState->pOverflowHead;
	pState->pOverflowHead = pEv->pNext;
	if (pState->xFree) {
	    pState->xFree(NULL, pState->pPlatCtx, pEv);
	}
    }
    pState->pOverflowTail = NULL;
    pState->nOverflow = 0;
    pState->nStatic = 0;
    pState->iHead = 0;
    if (pState->bMutexReady) {
	pState->xMutexLeave(NULL, pState->pPlatCtx, &pState->queueMutex);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8SignalAllStates --
 *
 *	Signal every live pState's event handle on the interp.
 *	Used by Th8_CancelEval and Th8_Freeze to wake any
 *	[vwait] currently sleeping in xEventWait so it can
 *	observe the cancel/freeze flag promptly.  Single-threaded
 *	(interp's owning thread).
 *
 *----------------------------------------------------------------------
 */

void
th8SignalAllStates(Th8_Interp *interp)
{
    Th8_AsyncStateNode *pNode;
    if (!interp) return;
    for (pNode = interp->pAsyncStateHead; pNode; pNode = pNode->pNext) {
	if (pNode->nFinalized) continue;
	if (!pNode->pState) continue;
	th8SignalEvent(pNode->pState);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_CreateAsyncState --
 *
 *	Allocate an embedder-owned pState bound to this interp.
 *	Single-threaded: must be called on the interp's owning
 *	thread.
 *
 *	The pState owns its own queue + mutex + signal handle;
 *	multiple pStates on the same interp are independent and
 *	contend with each other only at the interp-walk level
 *	(Th8_DrainQueueEvents).  After this call the pState is
 *	fully ready: cross-thread Th8_QueueEvent calls work
 *	immediately; no lazy initialisation.
 *
 *	Steps performed:
 *	  1. Validate the platform via TH8_CHECK_EVENT_CALLBACKS.
 *	  2. Allocate pState (+ a registry node owned by interp).
 *	  3. Capture all required platform pointers into pState.
 *	  4. Initialize the queueMutex.
 *	  5. Create the per-pState event handle.
 *	  6. Link the registry node into interp->pAsyncStateHead.
 *
 *	On any failure the partial state is unwound completely.
 *
 * Results:
 *	The new pState on success; NULL if the platform is
 *	missing required callbacks or any allocation fails.
 *
 *----------------------------------------------------------------------
 */

void *
Th8_CreateAsyncState(Th8_Interp *interp, void *pCtx)
{
    Th8_Platform *p;
    Th8_AsyncState *pState;
    Th8_AsyncStateNode *pNode;

    if (!interp) return NULL;
    p = interp->pPlatform;
    if (!p) return NULL;
    /*
     * Hard-fail if any required callback is missing - no
     * fallback path.  th8EventQueueAvailable also populates
     * the cached pointers, so we don't have to repeat that
     * here.
     */
    if (!TH8_CHECK_EVENT_CALLBACKS(p)) return NULL;

    pState = (Th8_AsyncState *)TH8_ALLOC(interp, sizeof(Th8_AsyncState));
    if (!pState) return NULL;
    Th8_Memset(interp, pState, 0, sizeof(*pState));
    pNode = (Th8_AsyncStateNode *)
        TH8_ALLOC(interp, sizeof(Th8_AsyncStateNode));
    if (!pNode) {
	Th8_Free(interp, pState);
	return NULL;
    }

    /* Wire identity fields. */
    pState->nDeleted = 0;
    pState->pInterp = interp;
    pState->pCtx = pCtx;
    pState->pNode = pNode;

    /*
     * Capture the platform's required function pointers and
     * pCtx onto pState.  After this returns, no cross-thread
     * code path will ever read interp->pPlatform.
     */
    if (th8EventQueueAvailable(interp, pState) != TH8_OK) {
	Th8_Free(interp, pNode);
	Th8_Free(interp, pState);
	return NULL;
    }

    /* Initialize the per-pState queue mutex. */
    pState->xMutexInit(interp, pState->pPlatCtx, &pState->queueMutex);
    pState->bMutexReady = 1;

    /* Create the per-pState manual-reset event handle. */
    pState->pEventHandle = pState->xEventCreate(interp, pState->pPlatCtx);
    if (!pState->pEventHandle) {
	pState->xMutexFinal(interp, pState->pPlatCtx, &pState->queueMutex);
	pState->bMutexReady = 0;
	Th8_Free(interp, pNode);
	Th8_Free(interp, pState);
	return NULL;
    }

    /* Link into the registry under interp's single-thread
     * ownership (no lock needed - same-thread only).
     *
     * APPEND at the tail so async sources are drained in registration
     * (FIFO) order: an event queued earlier fires before one queued
     * later, matching the intuitive / Tcl event-queue semantics.
     * (th8DrainAll round-robins the registry from the head, and each
     * source's own event queue is already FIFO.)  Prepending at the
     * head (LIFO) would fire the most-recently-registered source first,
     * reversing observable cross-source event order.  The registry is
     * short -- one node per live async source -- so the O(n) walk to
     * the tail is negligible and avoids threading a tail pointer
     * through finalize/removal. */
    pNode->pState = pState;
    pNode->nFinalized = 0;
    pNode->pNext = NULL;

    if (!interp->pAsyncStateHead) {
	interp->pAsyncStateHead = pNode;
    } else {
	Th8_AsyncStateNode *pTail = interp->pAsyncStateHead;

	while (pTail->pNext)
	    pTail = pTail->pNext;
	pTail->pNext = pNode;
    }

    return (void *)pState;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FinalizeAsyncState --
 *
 *	Tear down a pState and free it.  Safe to call on a pState
 *	whose owning interp has already been deleted (the registry
 *	node will have been freed; pNode is NULL'd by Th8_DeleteInterp's
 *	walk so we know to skip the flag-set step).
 *
 *	Steps performed (in order):
 *	  1. Set nFinalized=1 on the registry node atomically (if
 *	     the node still exists).  Th8_DeleteInterp's walk
 *	     reads this and skips the pState; that interlocks
 *	     concurrent delete vs. finalize on the same pState.
 *	  2. Drop any remaining queued callbacks (they will not
 *	     be invoked - by definition they ran "later" and
 *	     "later" never comes).
 *	  3. Destroy the per-pState event handle.
 *	  4. Finalize the per-pState queue mutex.
 *	  5. Free the pState struct itself.
 *
 *	NOT thread-safe: the embedder MUST ensure no worker is
 *	concurrently inside Th8_QueueEvent on this pState.
 *	Typical pattern: signal workers to stop, join them, then
 *	call Th8_FinalizeAsyncState.
 *
 *	NEVER dereferences interp internals - that's what makes
 *	it safe after Th8_DeleteInterp.
 *
 *----------------------------------------------------------------------
 */

int
Th8_FinalizeAsyncState(void *pStateAny)
{
    Th8_AsyncState *pState = (Th8_AsyncState *)pStateAny;
    Th8_AsyncStateNode *pNode;

    if (!pState) return TH8_ERROR;

    /* Step 1: mark the registry node, if any.  If pNode is
     * NULL the interp has already deleted us; skip.
     *
     * th8EventQueueAvailable (L2027) validates xIntCmpXchg
     * via TH8_CHECK_EVENT_CALLBACKS before populating
     * pState's cached pointers, so xIntCmpXchg is ALWAYS
     * non-NULL once pState exists.  The C2 check is a
     * defensive belt-and-braces test, never F at runtime. */
    pNode = pState->pNode;
    if (pNode && ALWAYS(pState->xIntCmpXchg)) {
	pState->xIntCmpXchg(NULL, pState->pPlatCtx, &pNode->nFinalized, 1, 0);
    }

    /* Step 2: drain any remaining callbacks without invoking. */
    th8FreePStateEvents(pState);

    /* Step 3: destroy the event handle. */
    if (pState->pEventHandle && pState->xEventDestroy) {
	pState->xEventDestroy(NULL, pState->pPlatCtx, pState->pEventHandle);
	pState->pEventHandle = NULL;
    }

    /* Step 4: finalize the queue mutex. */
    if (pState->bMutexReady && pState->xMutexFinal) {
	pState->xMutexFinal(NULL, pState->pPlatCtx, &pState->queueMutex);
	pState->bMutexReady = 0;
    }

    /* Step 5: free pState.  Use the cached xFree (no interp
     * accounting - pState was allocated via TH8_ALLOC but
     * the interp may already be gone). */
    if (pState->xFree) {
	pState->xFree(NULL, pState->pPlatCtx, pState);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_QueueEvent --
 *
 *	Enqueue an event for later drain.  One of the only two
 *	public Th8_* APIs callable from any thread (the other is
 *	Th8_CancelEval).
 *
 *	Operates entirely on the supplied pState - never touches
 *	the interp's pPlatform from the caller's thread.  The
 *	cached function pointers, mutex, and event handle on the
 *	pState are everything this function needs.
 *
 *	Steps performed:
 *	  1. Atomic check: is pState->nDeleted set?  If so, the
 *	     interp has been torn down; bail out cleanly without
 *	     dereferencing anything else.
 *	  2. Lock the pState's queue mutex.
 *	  3. If the static buffer has room, append into it
 *	     (no heap activity).  Otherwise allocate a Th8_Event
 *	     overflow node and append to the overflow list.
 *	  4. Signal the pState's event handle (under the mutex,
 *	     for the lost-wakeup race close).
 *	  5. Unlock.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if pState/xCallback is NULL,
 *	the interp has been deleted, the pState was somehow (?)
 *	created without a complete platform, or an allocation fails.
 *
 *----------------------------------------------------------------------
 */

int
Th8_QueueEvent(void *pStateAny, int (*xCallback)(Th8_Interp *, void *))
{
    Th8_AsyncState *pState = (Th8_AsyncState *)pStateAny;
    int nDeleted;

    /* Split per Finding 005. */
    if (!pState) return TH8_ERROR;
    if (!xCallback) return TH8_ERROR;
    if (!TH8_CHECK_EVENT_CALLBACKS(pState)) return TH8_ERROR;
    if (!pState->bMutexReady || !pState->pEventHandle) return TH8_ERROR;

    nDeleted =
        pState->xIntCmpXchg(NULL, pState->pPlatCtx, &pState->nDeleted, 0, 0);
    if (nDeleted != 0) return TH8_ERROR; /* Non-zero == interp gone. */

    pState->xMutexEnter(NULL, pState->pPlatCtx, &pState->queueMutex);
    if (pState->nStatic < TH8_EVENT_QUEUE_STATIC_N) {
	int iSlot = (pState->iHead + pState->nStatic) %
	            TH8_EVENT_QUEUE_STATIC_N;
	pState->aStatic[iSlot] = xCallback;
	pState->nStatic++;
    } else {
	Th8_Event
	    *pEv = (Th8_Event *)pState
	               ->xMalloc(NULL, pState->pPlatCtx, sizeof(Th8_Event));
	if (!pEv) {
	    pState->xMutexLeave(NULL, pState->pPlatCtx, &pState->queueMutex);
	    return TH8_ERROR;
	}
	pEv->xCallback = xCallback;
	pEv->pNext = NULL;
	th8EventOverflowAppend(pState, pEv);
    }

    /*
     * Signal under the mutex (manual-reset events tolerate this and
     * it closes the lost-wakeup window on Reset => Wait sequencing
     * on the consumer side).
     */
    pState->xEventSet(NULL, pState->pPlatCtx, pState->pEventHandle);
    pState->xMutexLeave(NULL, pState->pPlatCtx, &pState->queueMutex);

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ArraySearchIterEntry --
 *
 *	Th8_HashIterate visitor for Th8_IterateArraySearches.  For
 *	one array-search hash entry, unwraps the th8ArraySearch and
 *	forwards the (arrayName, searchId) pair to the user callback.
 *
 * Why / How:
 *	Th8_HashIterate speaks in raw Th8_HashEntry pointers; this
 *	adapter translates each entry into the public callback's
 *	(zArray, nArray, zKey, nKey, pCtx) signature and stashes the
 *	user callback's result in the shared th8ArraySearchIterCtx so
 *	the outer loop can propagate it.  Tombstoned entries (NULL
 *	pEntry / pData) are skipped with a plain guard that survives
 *	TH8_OMIT, per Bug 26.
 *
 * Results:
 *	TH8_OK to continue iterating (including for skipped entries or
 *	when the user callback returned TH8_OK); TH8_ERROR to stop when
 *	the user callback returned non-TH8_OK.
 *
 * Side effects:
 *	Stores the user callback's return code in the context's rc
 *	field; otherwise whatever the user callback does.
 *
 *----------------------------------------------------------------------
 */

static int
th8ArraySearchIterEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    th8ArraySearchIterCtx *p = (th8ArraySearchIterCtx *)pCtx;
    th8ArraySearch *pSearch;

    /* Bug 26: hash iterator callback contract -- use plain `if`
     * so the guard survives TH8_OMIT for tombstoned entries. */
    if (!pEntry || !pEntry->pData) return TH8_OK;
    pSearch = (th8ArraySearch *)pEntry->pData;
    p->rc = p->xCallback(
        pSearch->zArray, pSearch->nArray, pEntry->zKey, pEntry->nKey,
        p->pCtx);
    return p->rc == TH8_OK ? TH8_OK : TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IterateArraySearches --
 *
 *	Invoke `xCallback` once per active `[array startsearch]`
 *	cursor in the interpreter, passing the parent array name,
 *	the search id, and the caller-supplied context.  Used by
 *	`[info commands]`-style introspection and by teardown
 *	paths that need to drain stale cursors.
 *
 *	Iteration stops on the first callback that returns
 *	non-`TH8_OK`; that return code propagates to this
 *	function's caller.
 *
 * Parameters:
 *	interp    -- interpreter holding the array-search hash.
 *	             Must be non-NULL.
 *	xCallback -- visitor called once per cursor; must be
 *	             non-NULL.  Receives `(zArray, nArray, zSid,
 *	             nSid, pCtx)`.  Return `TH8_OK` to continue
 *	             iteration, any other code to stop.
 *	pCtx      -- opaque pointer passed through to the
 *	             callback.  May be NULL.
 *
 * Returns:
 *	`TH8_OK` if every callback returned `TH8_OK` (or no
 *	cursors exist).
 *	`TH8_ERROR` if `interp` or `xCallback` is NULL, or if any
 *	callback returned non-`TH8_OK`.
 *
 * Side effects:
 *	Whatever the callback chooses to do.  Iteration itself
 *	allocates nothing.
 *
 *----------------------------------------------------------------------
 */
int
Th8_IterateArraySearches(
    Th8_Interp *interp,
    int (*xCallback)(
        const char *zArray,
        size_t nArray,
        const char *zSid,
        size_t nSid,
        void *pCtx),
    void *pCtx)
{
    th8ArraySearchIterCtx iter;
    Th8_Hash *pHash;

    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!xCallback) return TH8_ERROR;
    pHash = th8GetArraySearchHash(interp, 0);
    if (!pHash) return TH8_OK;

    iter.xCallback = xCallback;
    iter.pCtx = pCtx;
    iter.rc = TH8_OK;
    Th8_HashIterate(interp, pHash, th8ArraySearchIterEntry, (void *)&iter);
    return iter.rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPackageUnknown --
 *
 *	Return the package unknown handler script, or "" if none.
 *
 * Why / How:
 *	When [package require] fails to find a package, it evaluates
 *	the unknown handler script (if set) to give the application
 *	a chance to locate and load the package.  Returns "" rather
 *	than NULL so callers never need a NULL check.
 *
 * Results:
 *	Pointer to the handler script string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_GetPackageUnknown(Th8_Interp *interp)
{
    return interp->zPkgUnknown ? interp->zPkgUnknown : "";
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetPackageUnknown --
 *
 *	Set the package unknown handler script.  Pass NULL or empty
 *	to clear.
 *
 * Why / How:
 *	Frees the previous handler (if any), then stores a
 *	Th8_Strdup copy of the new script.  Passing NULL or an
 *	empty string clears the handler entirely.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Previous handler string is freed.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetPackageUnknown(Th8_Interp *interp, const char *zCmd, size_t nCmd)
{
    if (!interp) return;
    Th8_Free(interp, interp->zPkgUnknown);
    if (zCmd && TH8_LEN(nCmd) > 0) {
	interp->zPkgUnknown = Th8_Strdup(interp, zCmd, nCmd);
	interp->nPkgUnknown = TH8_LEN(nCmd);
    } else {
	interp->zPkgUnknown = 0;
	interp->nPkgUnknown = 0;
    }
}


/*
 * Forward declarations for namespace helpers used by
 * the public namespace APIs below.
 */

static void th8FreeNamespace(Th8_Interp *, Th8_Namespace *);
static int th8FreeChildEntry(Th8_HashEntry *, void *);
static int th8FreeCmdEntry(Th8_HashEntry *, void *);
static void th8QueuePendingCmd(Th8_Interp *, Th8_Command *);
static void th8QueuePendingNs(Th8_Interp *, Th8_Namespace *);
static void th8DrainPendingDeletes(Th8_Interp *);


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetCurrentNamespace --
 *
 *	Return the fully qualified name of the current namespace.
 *	Returns "::" for the global namespace.
 *
 * Why / How:
 *	Reads interp->pCurrentNs and returns its stored zName.
 *	The global namespace has no explicit name, so "::" is
 *	returned as a special case.  Used by [namespace current]
 *	and namespace-qualified command resolution.
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_GetCurrentNamespace(Th8_Interp *interp) /* Interpreter. */
{
    if (interp->pCurrentNs == interp->pGlobalNs) {
	return "::";
    }
    if (!interp->pCurrentNs->zName) {
	return "::";
    }
    return interp->pCurrentNs->zName;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetCurrentNsPtr --
 *
 *	Return the interpreter's current namespace as an opaque
 *	void pointer, for use by modules that lack the full
 *	Th8_Namespace type definition.
 *
 * Why / How:
 *	Callers outside th8_core.c (e.g. th8_lang.c) need to
 *	save and restore the current namespace across scope
 *	changes.  The opaque pointer avoids exposing internal
 *	struct layout.
 *
 * Results:
 *	The current namespace pointer (cast to void*).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
th8GetCurrentNsPtr(Th8_Interp *interp) /* Interpreter. */
{
    return (void *)interp->pCurrentNs;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetCurrentNsPtr --
 *
 *	Set the interpreter's current namespace from an opaque
 *	pointer.  NULL values are silently ignored to prevent
 *	clearing the namespace accidentally.
 *
 * Why / How:
 *	Used by [namespace eval] and uplevel to switch the current
 *	namespace context.  The NULL guard ensures a failed namespace
 *	lookup does not corrupt interpreter state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->pCurrentNs is updated (if pNs is non-NULL).
 *
 *----------------------------------------------------------------------
 */

void
th8SetCurrentNsPtr(
    Th8_Interp *interp, /* Interpreter. */
    void *pNs) /* Namespace pointer (opaque). */
{
    if (pNs) {
	interp->pCurrentNs = (Th8_Namespace *)pNs;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetFrameNsPtr --
 *
 *	Set the namespace associated with the current call frame
 *	from an opaque pointer.  This determines which namespace
 *	context the frame's variable lookups and command resolution
 *	use.
 *
 * Why / How:
 *	When [namespace eval] pushes a new call frame, the frame
 *	must inherit the target namespace so that variable and
 *	command resolution happen in the correct scope.  The
 *	ALWAYS() guard documents the invariant that a frame is
 *	always active during evaluation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->pFrame->pNs is updated.
 *
 *----------------------------------------------------------------------
 */

void
th8SetFrameNsPtr(
    Th8_Interp *interp, /* Interpreter. */
    void *pNs) /* Namespace pointer (opaque). */
{
    if (ALWAYS(interp->pFrame)) {
	interp->pFrame->pNs = (Th8_Namespace *)pNs;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_PushSourceName --
 *
 *	Push a script name onto the source stack.  Called by
 *	source_command before evaluating a file.
 *
 * Why / How:
 *	Maintains a fixed-depth stack of source file names so
 *	that [info script] can report which file is currently
 *	being evaluated.  Silently stops pushing at
 *	TH8_MAX_SOURCE_DEPTH to prevent overflow.
 *
 *----------------------------------------------------------------------
 */

void
Th8_PushSourceName(Th8_Interp *interp, const char *zName, size_t nName)
{
    if (!interp) return;
    if (interp->nSourceDepth < TH8_MAX_SOURCE_DEPTH) {
	interp->azSourceName[interp->nSourceDepth] = zName;
	interp->anSourceName[interp->nSourceDepth] = nName;
	interp->nSourceDepth++;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_PopSourceName --
 *
 *	Pop the topmost script name from the source stack.
 *	Called by source_command after evaluation completes.
 *
 * Why / How:
 *	Decrements nSourceDepth and clears the vacated slot.
 *	Paired with Th8_PushSourceName to bracket file
 *	evaluation so the source stack stays balanced.
 *
 *----------------------------------------------------------------------
 */

void
Th8_PopSourceName(Th8_Interp *interp)
{
    if (!interp) return;
    if (interp->nSourceDepth > 0) {
	interp->nSourceDepth--;
	interp->azSourceName[interp->nSourceDepth] = 0;
	interp->anSourceName[interp->nSourceDepth] = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetSourceName --
 *
 *	Return the name of the innermost active [source] script.
 *	Returns "" if no source is active.
 *
 * Why / How:
 *	Peeks at the top of the source-name stack without popping.
 *	Returns "" rather than NULL so callers never need a NULL
 *	check.  Implements [info script].
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_GetSourceName(Th8_Interp *interp, size_t *pnName)
{
    if (interp->nSourceDepth > 0) {
	int top = interp->nSourceDepth - 1;

	if (pnName) {
	    *pnName = interp->anSourceName[top];
	}
	return interp->azSourceName[top] ? interp->azSourceName[top] : "";
    }
    if (pnName) {
	*pnName = 0;
    }
    return "";
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FindNamespace --
 *
 *	Check whether a namespace exists, optionally creating it.
 *
 * Why / How:
 *	Public boolean wrapper around the internal th8FindNamespace,
 *	which returns a pointer.  Converts the pointer to a 0/1
 *	result for callers that only need existence information.
 *
 * Results:
 *	1 if found (or created), 0 if not found.
 *
 *----------------------------------------------------------------------
 */

int
Th8_FindNamespace(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    int bCreate)
{
    if (!interp) return TH8_ERROR;
    return th8FindNamespace(interp, zName, nName, bCreate) != 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DeleteNamespace --
 *
 *	Delete a namespace and all its contents.  It is an error
 *	to delete the global namespace "::".
 *
 * Why / How:
 *	Looks up the namespace via th8FindNamespace, then recursively
 *	frees children, commands, and variables.  For the global
 *	namespace, the struct survives but its contents are emptied
 *	and hashes reinitialized.  If the current namespace is being
 *	deleted, pCurrentNs is reset to global to avoid dangling
 *	pointers.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if not found or is global.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DeleteNamespace(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_Namespace *pNs;
    Th8_Namespace *pParent;

    if (!interp) return TH8_ERROR;
    pNs = th8FindNamespace(interp, zName, nName, 0);
    if (!pNs) {
	Th8_SetResult(interp, "namespace not found", TH8_NOLEN);
	return TH8_ERROR;
    }
    /*
     * If deleting the current namespace, reset pCurrentNs to
     * the global namespace so subsequent operations don't use
     * freed memory.
     */

    if (pNs == interp->pCurrentNs) {
	interp->pCurrentNs = interp->pGlobalNs;
    }

    /*
     * Deleting the global namespace removes all commands,
     * variables, and child namespaces.  The struct itself
     * survives (it is embedded in the interpreter allocation).
     * The hashes are re-initialized so the interpreter
     * remains usable (though empty).
     */

    if (pNs == interp->pGlobalNs) {
	/* Free child namespaces recursively. */
	Th8_HashIterate(
	    interp, pNs->paChild, th8FreeChildEntry, (void *)interp);
	Th8_HashDelete(interp, pNs->paChild);

	/* Free all commands (invoke delete callbacks). */
	Th8_HashIterate(interp, pNs->paCmd, th8FreeCmdEntry, (void *)interp);
	Th8_HashDelete(interp, pNs->paCmd);

#if defined(TH8_ENABLE_VARIABLES)
	/* Free namespace variables. */
	Th8_HashIterate(interp, pNs->paVar, th8FreeVarEntry, (void *)interp);
	Th8_HashDelete(interp, pNs->paVar);
#endif

	/* Bug 33: zExport was allocated via Th8_StringAppend (which
	 * routes through th8BufferAlloc / the per-interp pool), so it
	 * must be returned to the pool via th8BufferFree using the
	 * same size convention as Th8_StringAppend
	 * (TH8_LEN(nExport) + 1).  A plain Th8_Free desyncs the pool
	 * bookkeeping. */
	if (pNs->zExport) {
	    th8BufferFree(interp, pNs->zExport, TH8_LEN(pNs->nExport) + 1);
	}
	pNs->zExport = 0;
	pNs->nExport = 0;

	/* Re-initialize empty hashes. */
	pNs->paCmd = Th8_HashNew(interp);
	if (!pNs->paCmd) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
#if defined(TH8_ENABLE_VARIABLES)
	pNs->paVar = Th8_HashNew(interp);
	if (!pNs->paVar) {
	    Th8_HashDelete(interp, pNs->paCmd);
	    pNs->paCmd = 0;
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
#endif
	pNs->paChild = Th8_HashNew(interp);
	if (!pNs->paChild) {
#if defined(TH8_ENABLE_VARIABLES)
	    Th8_HashDelete(interp, pNs->paVar);
	    pNs->paVar = 0;
#endif
	    Th8_HashDelete(interp, pNs->paCmd);
	    pNs->paCmd = 0;
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	interp->pCurrentNs = interp->pGlobalNs;
	return TH8_OK;
    }

    pParent = pNs->pParent;
    if (pParent && pNs->zName) {
	/*
	 * Remove from parent's paChild hash by tail (component)
	 * name.  The child is keyed under the last "::"-separated
	 * component; th8SplitQualName computes it correctly even
	 * for namespace names that contain a single ":" (e.g.
	 * "::_pkg:sub", whose component tail is "_pkg:sub", NOT
	 * "sub").  Bug 5: the previous hand-rolled backward scan
	 * stopped at the last single ':', so for a single-colon
	 * name it removed the WRONG key (the entry stayed) and
	 * left the parent's paChild pointing at the just-freed
	 * namespace -- a dangling entry that hung interpreter
	 * teardown (and made `namespace delete` a silent no-op).
	 */
	const char *zNsPart, *zTail;
	size_t nNsPart, nTail;

	th8SplitQualName(
	    pNs->zName, pNs->nName, &zNsPart, &nNsPart, &zTail, &nTail);
	(void)zNsPart;
	(void)nNsPart;
	Th8_HashRemove(interp, pParent->paChild, zTail, nTail);
    }
    /*
     * Defer the recursive free when inside eval so that any
     * command currently executing within this namespace does
     * not have its context freed out from under it.
     */

    if (interp->nEvalDepth > 0) {
	th8QueuePendingNs(interp, pNs);
    } else {
	th8FreeNamespace(interp, pNs);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NsEval --
 *
 *	Evaluate a script in the context of a named namespace.
 *	The namespace is created if it does not exist.
 *	pCurrentNs is switched for the duration of the eval.
 *
 * Why / How:
 *	Saves pCurrentNs, switches to the target namespace, pushes
 *	a new call frame tagged with pNs, evaluates the script via
 *	Th8_Eval, then pops the frame and restores the previous
 *	namespace.  This implements [namespace eval].
 *
 * Results:
 *	Return code from the script.
 *
 *----------------------------------------------------------------------
 */

static int th8PushFrame(Th8_Interp *, Th8_Frame *);
static void th8PopFrame(Th8_Interp *);

/*
 *----------------------------------------------------------------------
 *
 * Th8_NsEval --
 *
 *	Evaluate a script in the named namespace, creating the
 *	namespace if it does not already exist.  Implements the
 *	C-level entry point for `[namespace eval]`.
 *
 *	Pushes a call frame whose `pNs` field records the target
 *	namespace so that nested `[uplevel]` can resolve the
 *	namespace scope correctly.  On return, restores the
 *	previously-current namespace and pops the frame regardless
 *	of evaluation success or failure.
 *
 * Parameters:
 *	interp  -- interpreter.  Must be non-NULL.
 *	zNs     -- namespace name bytes (may be fully-qualified
 *	           with `::` separators).
 *	nNs     -- length of `zNs`.  Pass `TH8_NOLEN` to use
 *	           `Th8_Strlen`.
 *	zScript -- script to evaluate inside the namespace.
 *	nScript -- length of `zScript`.  Pass `TH8_NOLEN` to use
 *	           `Th8_Strlen`.
 *
 * Returns:
 *	`TH8_OK` on successful eval (interp result holds the
 *	script's value).
 *	`TH8_ERROR` if `interp` is NULL, if the namespace could
 *	not be created (interp result: "can't create namespace"),
 *	or if the script itself raised an error.
 *	Other return codes (`TH8_RETURN`, `TH8_BREAK`,
 *	`TH8_CONTINUE`) propagate from the script.
 *
 * Side effects:
 *	May create a new namespace.  Allocates and frees one call
 *	frame.  Mutates `interp->pCurrentNs` for the duration of
 *	the eval (restored before return).
 *
 *----------------------------------------------------------------------
 */
int
Th8_NsEval(
    Th8_Interp *interp,
    const char *zNs,
    size_t nNs,
    const char *zScript,
    size_t nScript)
{
    Th8_Namespace *pTarget;
    Th8_Namespace *pSaved;
    int rc;

    if (!interp) return TH8_ERROR;
    pTarget = th8FindNamespace(interp, zNs, nNs, 1);
    if (!pTarget) {
	Th8_SetResult(interp, "can't create namespace", TH8_NOLEN);
	return TH8_ERROR;
    }

    pSaved = interp->pCurrentNs;
    interp->pCurrentNs = pTarget;

    /*
     * Push a frame for the namespace eval so that
     * [uplevel] can identify the namespace context.
     * The frame's pNs records the target namespace.
     */

    {
	Th8_Frame *pFrame;

	pFrame = (Th8_Frame *)TH8_ALLOC(interp, sizeof(Th8_Frame));
	if (!pFrame) {
	    interp->pCurrentNs = pSaved;
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (th8PushFrame(interp, pFrame) != TH8_OK) {
	    Th8_Free(interp, pFrame);
	    interp->pCurrentNs = pSaved;
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pFrame->pNs = pTarget;
	rc = Th8_Eval(interp, 0, zScript, nScript, NULL, 0);
	th8PopFrame(interp);
	Th8_Free(interp, pFrame);
    }

    interp->pCurrentNs = pSaved;
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendNsChildren --
 *
 *	Append the fully-qualified names of all child namespaces
 *	to a list.
 *
 * Why / How:
 *	Public entry point for [namespace children].  Resolves the
 *	target namespace, then walks its child hash via
 *	th8AppendNsChildKeys, optionally filtering by a glob pattern.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8AppendNsChildKeys --
 *
 *	Hash iteration callback: append a child namespace's
 *	fully-qualified name to a list.
 *
 * Why / How:
 *	Called via Th8_HashIterate on paChild.  Extracts the
 *	Th8_Namespace pointer from pEntry->pData and appends
 *	its zName to the caller's list buffer.
 *
 * Results:
 *	Always returns TH8_OK (continue iterating).
 *
 * Side effects:
 *	Appends to the list buffer via Th8_ListAppend.
 *
 *----------------------------------------------------------------------
 */

static int
th8AppendNsChildKeys(Th8_HashEntry *pEntry, void *pContext)
{
    void **aCtx = (void **)pContext;
    Th8_Interp *interp = (Th8_Interp *)aCtx[0];
    char **pz = (char **)aCtx[1];
    size_t *pn = (size_t *)aCtx[2];
    Th8_Namespace *pChild = (Th8_Namespace *)pEntry->pData;

    if (pChild && pChild->zName) {
	Th8_ListAppend(interp, pz, pn, pChild->zName, pChild->nName);
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendNsChildren --
 *
 *	Append the fully-qualified names of all child namespaces
 *	to a list buffer.
 *
 * Why / How:
 *	Resolves the namespace (defaulting to pCurrentNs if not
 *	specified), then iterates paChild via th8AppendNsChildKeys
 *	to collect all child names.  Implements [namespace children].
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	List buffer is modified.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendNsChildren(
    Th8_Interp *interp,
    const char *zNs,
    size_t nNs,
    char **pz,
    size_t *pn)
{
    Th8_Namespace *pNs;
    void *aCtx[3];

    if (!interp) return TH8_ERROR;
    if (!zNs || nNs == 0) {
	pNs = interp->pCurrentNs;
    } else {
	pNs = th8FindNamespace(interp, zNs, nNs, 0);
    }
    if (!pNs) return TH8_OK;

    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(interp, pNs->paChild, th8AppendNsChildKeys, (void *)aCtx);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetNsParent --
 *
 *	Return the fully-qualified name of a namespace's parent.
 *	Returns "" for the global namespace.
 *
 * Why / How:
 *	Walks up one level in the namespace tree via pParent.
 *	Returns "::" when the parent is the global namespace
 *	and "" when there is no parent (i.e. the namespace is
 *	global itself).  Implements [namespace parent].
 *
 *----------------------------------------------------------------------
 */

const char *
th8GetNsParent(Th8_Interp *interp, const char *zNs, size_t nNs)
{
    Th8_Namespace *pNs;

    pNs = th8FindNamespace(interp, zNs, nNs, 0);
    if (!pNs || !pNs->pParent) {
	return "";
    }
    if (pNs->pParent == interp->pGlobalNs) {
	return "::";
    }
    return pNs->pParent->zName ? pNs->pParent->zName : "::";
}


/*
 *----------------------------------------------------------------------
 *
 * th8SimpleGlob --
 *
 *	Minimal glob matcher for namespace export/import patterns.
 *	Supports only trailing "*" (e.g., "foo*", "*").  This is
 *	sufficient for the standard [namespace export] patterns.
 *
 * Why / How:
 *	Handles three cases: "*" matches everything, "prefix*"
 *	matches any string starting with prefix, and otherwise
 *	performs an exact length-and-byte comparison.  No
 *	recursion or backtracking is needed.
 *
 *----------------------------------------------------------------------
 */

static int
th8SimpleGlob(
    const char *zPat, /* Glob pattern. */
    size_t nPat, /* Pattern length. */
    const char *zStr, /* String to match. */
    size_t nStr) /* String length. */
{
    size_t i;

    if (nPat == 1 && zPat[0] == '*') {
	return 1; /* "*" matches everything. */
    }

    /* Trailing "*": prefix match. */
    if (nPat > 0 && zPat[nPat - 1] == '*') {
	size_t nPrefix = nPat - 1;

	if (nStr < nPrefix) return 0;
	for (i = 0; i < nPrefix; i++) {
	    if (zPat[i] != zStr[i]) return 0;
	}
	return 1;
    }

    /* Exact match. */
    if (nPat != nStr) return 0;
    for (i = 0; i < nPat; i++) {
	if (zPat[i] != zStr[i]) return 0;
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NsExport --
 *
 *	Add export patterns to a namespace.  Multiple calls append
 *	to the pattern list (space-separated).
 *
 * Why / How:
 *	Appends the new pattern to the namespace's zExport string
 *	(space-separated).  Import operations later walk this
 *	list to determine which commands are visible for import.
 *	Implements [namespace export].
 *
 *----------------------------------------------------------------------
 */

int
Th8_NsExport(
    Th8_Interp *interp,
    const char *zNs,
    size_t nNs,
    const char *zPattern,
    size_t nPattern)
{
    Th8_Namespace *pNs;

    if (!interp) return TH8_ERROR;
    if (!zNs || nNs == 0) {
	pNs = interp->pCurrentNs;
    } else {
	pNs = th8FindNamespace(interp, zNs, nNs, 0);
    }
    if (!pNs) {
	Th8_SetResult(interp, "namespace not found", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (nPattern == TH8_NOLEN) {
	nPattern = Th8_Strlen(interp, zPattern);
    }

    /*
     * Append to the existing export pattern list.
     */

    if (pNs->nExport > 0) {
	TH8_STR_APPEND(interp, &pNs->zExport, &pNs->nExport, " ", 1);
    }
    TH8_STR_APPEND(interp, &pNs->zExport, &pNs->nExport, zPattern, nPattern);
    return TH8_OK;

oom:
    /* pNs->zExport is owned by the namespace and left at its last good
     * state; nothing to free here.  "out of memory" already set. */
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NsGetExport --
 *
 *	Return the export pattern string for a namespace, or "".
 *
 * Why / How:
 *	Looks up the namespace and returns its zExport field.
 *	Returns "" rather than NULL when no exports are set,
 *	so callers can use the result directly.
 *
 *----------------------------------------------------------------------
 */

const char *
th8NsGetExport(Th8_Interp *interp, const char *zNs, size_t nNs)
{
    Th8_Namespace *pNs;

    if (!zNs || nNs == 0) {
	pNs = interp->pCurrentNs;
    } else {
	pNs = th8FindNamespace(interp, zNs, nNs, 0);
    }
    /* Bug 26 family: plain pNs guard.  th8FindNamespace can
     * return NULL on lookup miss; the prior NEVER would
     * have collapsed under TH8_OMIT and let pNs->zExport
     * deref NULL. */
    if (!pNs || !pNs->zExport) {
	return "";
    }
    return pNs->zExport;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ImportCallback --
 *
 *	Hash iteration callback for Th8_NsImport: for each command
 *	in the source namespace, check if it matches the export
 *	patterns and the import pattern, and if so, create an alias
 *	in the current namespace.
 *
 * Why / How:
 *	For each command entry, first checks the source namespace's
 *	export patterns (th8SimpleGlob), then the caller's import
 *	pattern.  Matching commands are cloned into the current
 *	namespace via xCopy (deep copy) or shared pointer (shallow).
 *	The -force flag allows overwriting existing commands.
 *
 *----------------------------------------------------------------------
 */

static int
th8ImportCallback(Th8_HashEntry *pEntry, void *pContext)
{
    Th8_ImportCtx *pCtx = (Th8_ImportCtx *)pContext;
    Th8_Interp *interp = pCtx->interp;
    Th8_Namespace *pSrcNs = pCtx->pSrcNs;
    const char *zName = pEntry->zKey;
    size_t nName = pEntry->nKey;
    Th8_Command *pSrc;
    int exported = 0;

    /*
     * Check if this command is exported by the source namespace.
     */

    if (pSrcNs->zExport && pSrcNs->nExport > 0) {
	/*
	 * Walk the space-separated export patterns.
	 */

	const char *z = pSrcNs->zExport;
	size_t n = pSrcNs->nExport;
	size_t i = 0;

	while (i < n) {
	    size_t start = i;

	    while (i < n && z[i] != ' ')
		i++;
	    if (i > start) {
		if (th8SimpleGlob(&z[start], i - start, zName, nName)) {
		    exported = 1;
		    break;
		}
	    }
	    i++; /* skip space */
	}
    }

    if (!exported) return TH8_OK; /* Not exported: skip. */

    /*
     * Check if the import pattern matches this command name.
     */

    if (!th8SimpleGlob(pCtx->zImportPat, pCtx->nImportPat, zName, nName)) {
	return TH8_OK; /* Doesn't match import pattern: skip. */
    }

    /*
     * Create the command in the current namespace.
     * It points to the same proc/context (shared registration).
     */

    pSrc = (Th8_Command *)pEntry->pData;
    if (pSrc) {
	Th8_HashEntry *pDst;

	pDst =
	    Th8_HashFind(interp, interp->pCurrentNs->paCmd, zName, nName, 1);
	if (!pDst->pData || pCtx->bForce) {
	    Th8_Command *pNew;

	    if (pDst->pData) {
		/*
		 * -force: free the old import before
		 * replacing it.
		 */

		Th8_Command *pOld;

		pOld = (Th8_Command *)pDst->pData;
		th8RemoveCmdTokenEntry(interp, pOld);
		if (pOld->xDel) {
		    pOld->xDel(interp, pOld->pContext);
		}
		Th8_Free(interp, pOld->zQualName);
		Th8_Free(interp, pOld);
	    }

	    pNew = (Th8_Command *)TH8_ALLOC(interp, sizeof(Th8_Command));
	    if (!pNew) {
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    pNew->xProc = pSrc->xProc;

	    if (pSrc->xCopy) {
		/*
		 * Deep copy: the imported command gets its
		 * own independent context.  Safe even if the
		 * source namespace is later deleted.
		 */

		pNew->pContext = pSrc->xCopy(interp, pSrc->pContext);
		pNew->xDel = pSrc->xDel;
	    } else {
		/*
		 * No copy function: share the context pointer.
		 * The imported command does NOT own the context
		 * and must not call xDel.
		 */

		pNew->pContext = pSrc->pContext;
		pNew->xDel = 0;
	    }
	    pNew->xCopy = pSrc->xCopy;
	    pNew->pDefNs = pSrc->pDefNs;
	    pDst->pData = (void *)pNew;
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NsImport --
 *
 *	Import commands from a namespace into the current namespace.
 *	zPattern is a qualified glob like "::foo::*" or "::foo::bar".
 *
 * Why / How:
 *	Splits the qualified pattern into a namespace path and tail
 *	glob, looks up the source namespace, then iterates its
 *	command hash via th8ImportCallback.  Implements
 *	[namespace import].
 *
 *----------------------------------------------------------------------
 */

int
Th8_NsImport(
    Th8_Interp *interp,
    const char *zPattern,
    size_t nPattern,
    int bForce)
{
    const char *zNsPath;
    size_t nNsPath;
    const char *zTail;
    size_t nTail;
    Th8_Namespace *pSrcNs;
    Th8_ImportCtx ctx;

    if (!interp) return TH8_ERROR;
    if (nPattern == TH8_NOLEN) {
	nPattern = Th8_Strlen(interp, zPattern);
    }

    /*
     * Split "::foo::bar::*" into namespace "::foo::bar" and
     * tail pattern "*".
     */

    th8SplitQualName(zPattern, nPattern, &zNsPath, &nNsPath, &zTail, &nTail);

    if (!zNsPath || nNsPath == 0) {
	Th8_SetResult(interp, "import pattern must be qualified", TH8_NOLEN);
	return TH8_ERROR;
    }

    pSrcNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
    if (!pSrcNs) {
	Th8_ErrorMessage(interp, "unknown namespace \"", zNsPath, nNsPath);
	return TH8_ERROR;
    }

    ctx.interp = interp;
    ctx.pSrcNs = pSrcNs;
    ctx.zImportPat = zTail;
    ctx.nImportPat = nTail;
    ctx.bForce = bForce;
    Th8_HashIterate(interp, pSrcNs->paCmd, th8ImportCallback, (void *)&ctx);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Strlen --
 *
 *	Compute string length without calling libc strlen.
 *
 * Why / How:
 *	Prefers the platform's xStrlen callback when available.
 *	Falls back to a built-in byte-counting loop for bootstrap
 *	contexts (before the interpreter is fully initialized) or
 *	when the platform provides no xStrlen.
 *
 * Results:
 *	Number of bytes before the NUL terminator.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
Th8_Strlen(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *z) /* NUL-terminated string. */
{
    if (interp) {
	const Th8_Platform *p = Th8_GetPlatform(interp);

	if (p && p->xStrlen) {
	    return p->xStrlen(interp, p->pCtx, z);
	}
    }
    /* Built-in fallback (pre-interp or no xStrlen callback). */
    {
	size_t n = 0;

	if (z) {
	    while (z[n]) {
		n++;
	    }
	}
	return n;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Namespace helper functions --
 *
 *	Utilities for looking up, creating, splitting qualified names,
 *	building fully qualified names, and freeing namespaces.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8SplitQualName --
 *
 *	Split a possibly qualified name into a namespace path and a
 *	simple tail component.  Finds the LAST "::" separator.
 *
 *	ALGORITHM:
 *
 *	Scans the entire name looking for "::" pairs.  Records the
 *	position of the last one found.  The portion before the last
 *	"::" is the namespace path (*pzNs / *pnNs), and the portion
 *	after is the simple tail (*pzTail / *pnTail).
 *
 *	Examples:
 *	  "foo"         -> ns=NULL, tail="foo"
 *	  "::foo"       -> ns="::", tail="foo"  (nNs=0, global implied)
 *	  "::a::b::c"   -> ns="::a::b", tail="c"
 *
 *	If no "::" is found, *pzNs is set to NULL and the entire
 *	name is the tail.  The caller checks *pzNs to determine
 *	whether the name is qualified.
 *
 * Why / How:
 *	A single left-to-right scan records the position of every
 *	"::" pair; the last one found becomes the split point.
 *	No allocation is performed -- outputs point into the
 *	original name buffer.
 *
 * Results:
 *	None.  Outputs are set via pointer parameters.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
th8SplitQualName(
    const char *zName, /* Full name to split. */
    size_t nName, /* Length of zName. */
    const char **pzNs, /* OUT: namespace path (or NULL). */
    size_t *pnNs, /* OUT: namespace path length. */
    const char **pzTail, /* OUT: simple tail name. */
    size_t *pnTail) /* OUT: tail name length. */
{
    size_t i;
    size_t nLast = 0;
    int bFound = 0;

    if (nName == TH8_NOLEN) {
	size_t n = 0;

	while (zName[n]) {
	    n++;
	}
	nName = n;
    }

    /*
     * Scan for the last "::" separator.
     */

    for (i = 0; i + 1 < nName; i++) {
	if (zName[i] == ':' && zName[i + 1] == ':') {
	    nLast = i;
	    bFound = 1;
	}
    }

    if (bFound) {
	/*
	 * Bug 17: a run of three or more colons (e.g.
	 * "vctest1:::ab", produced when a single-colon-prefixed
	 * variable name like ":ab" is qualified with its
	 * namespace) must not leave a trailing colon on the
	 * namespace part -- that would create a spurious
	 * "::vctest1:" namespace.  Back the separator up to the
	 * FIRST colon of the run so the extra leading colon(s)
	 * stay with the tail (ns "vctest1", tail ":ab"), matching
	 * Tcl, which trims trailing colons from the namespace
	 * qualifier.
	 */
	while (nLast > 0 && zName[nLast - 1] == ':') {
	    nLast--;
	}
	*pzNs = zName;
	*pnNs = nLast;
	*pzTail = &zName[nLast + 2];
	*pnTail = nName - nLast - 2;
    } else {
	*pzNs = 0;
	*pnNs = 0;
	*pzTail = zName;
	*pnTail = nName;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8ResolveNsPattern --
 *
 *	Centralized namespace pattern resolution for info commands,
 *	info vars, and info procs.  Detects namespace separators
 *	(::) anywhere in the pattern.  If the pattern is not fully
 *	qualified (no leading ::), prepends :: to make it absolute,
 *	matching Tcl 8.x behavior where "foo::*" is equivalent to
 *	"::foo::*".
 *
 * Why / How:
 *	Scans for "::" in the pattern; if absent, returns 0
 *	(unqualified).  For relative patterns, prepends the current
 *	namespace path.  Then delegates to th8SplitQualName to
 *	separate the namespace and tail components.
 *
 * Results:
 *	Non-zero if the pattern was namespace-qualified; zero if
 *	it was a simple unqualified pattern.  On qualified return,
 *	*pzNs + *pnNs and *pzTail + *pnTail are set.
 *
 *----------------------------------------------------------------------
 */

int
th8ResolveNsPattern(
    Th8_Interp *interp,
    const char *zPat,
    size_t nPat,
    const char **pzNs,
    size_t *pnNs,
    const char **pzTail,
    size_t *pnTail,
    char **pzBuf)
{
    size_t i;
    int bHasNsSep = 0;

    *pzBuf = 0;
    *pzNs = 0;
    *pnNs = 0;
    *pzTail = zPat;
    *pnTail = nPat;

    for (i = 0; i + 1 < nPat; i++) {
	if (zPat[i] == ':' && zPat[i + 1] == ':') {
	    bHasNsSep = 1;
	    break;
	}
    }
    if (!bHasNsSep) return 0;

    /* Loop invariant: the scan above only sets bHasNsSep when
     * i + 1 < nPat, i.e. nPat >= 2.  Wrap with ALWAYS so MC/DC
     * folds the dead C1 condition away. */
    if (!(ALWAYS(nPat >= 2) && zPat[0] == ':' && zPat[1] == ':')) {
	/*
	 * Relative pattern (e.g., "foo::*").  Resolve relative
	 * to the current namespace, matching Tcl 8.x semantics.
	 * Inside "namespace eval bar { info vars foo::* }",
	 * "foo::*" resolves to "::bar::foo::*".
	 */
	const char *zCurNs = Th8_GetCurrentNamespace(interp);
	size_t nBuf = 0;

	*pzBuf = 0;
	if (zCurNs && zCurNs[0] == ':' && zCurNs[1] == ':') {
	    TH8_STR_APPEND(interp, pzBuf, &nBuf, zCurNs, TH8_NOLEN);
	    /* Add :: separator unless curNs is "::" itself. */
	    if (!(zCurNs[2] == '\0')) {
		TH8_STR_APPEND(interp, pzBuf, &nBuf, "::", 2);
	    }
	} else {
	    TH8_STR_APPEND(interp, pzBuf, &nBuf, "::", 2);
	}
	TH8_STR_APPEND(interp, pzBuf, &nBuf, zPat, nPat);
	zPat = *pzBuf;
	nPat = nBuf;
    }

    th8SplitQualName(zPat, nPat, pzNs, pnNs, pzTail, pnTail);

    if (*pnNs == 0) {
	*pzNs = "::";
	*pnNs = 2;
    }
    return 1;

oom:
    /* Growth failed while building the resolved pattern buffer.  Release
     * the partial buffer and report "not resolved" (this predicate
     * returns 0/1, not TH8_OK/ERROR); "out of memory" is already set. */
    Th8_Free(interp, *pzBuf);
    *pzBuf = 0;
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetFullNsName --
 *
 *	Build the fully qualified name for a child namespace.
 *	If the parent is the global namespace, the result is
 *	"::childname"; otherwise "parentpath::childname".
 *
 * Why / How:
 *	Allocates a buffer sized for the concatenation of the
 *	parent path, "::", and the child component, then
 *	assembles the string with Th8_Memcpy.  Special-cases
 *	the global namespace (parent is "::") to avoid a
 *	double-separator ("::::child").
 *
 * Results:
 *	Newly allocated NUL-terminated string.  Caller must free
 *	with Th8_Free.
 *
 * Side effects:
 *	Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

static char *
th8GetFullNsName(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Namespace *pParent, /* Parent namespace. */
    const char *zChild, /* Child component name. */
    size_t nChild) /* Length of zChild. */
{
    char *zFull;
    size_t nFull;

    if (pParent == interp->pGlobalNs) {
	/*
	 * Parent is "::" so result is "::childname".
	 */

	nFull = 2 + nChild;
	zFull = (char *)TH8_ALLOC_STR(interp, nFull);
	if (!zFull) return NULL;
	Th8_Memcpy(interp, zFull, "::", 2);
	Th8_Memcpy(interp, &zFull[2], zChild, nChild);
	zFull[nFull] = 0;
    } else {
	/*
	 * Result is "parentpath::childname".
	 */

	nFull = pParent->nName + 2 + nChild;
	zFull = (char *)TH8_ALLOC_STR(interp, nFull);
	if (!zFull) return NULL;
	Th8_Memcpy(interp, zFull, pParent->zName, pParent->nName);
	Th8_Memcpy(interp, &zFull[pParent->nName], "::", 2);
	Th8_Memcpy(interp, &zFull[pParent->nName + 2], zChild, nChild);
	zFull[nFull] = 0;
    }
    return zFull;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FindNamespace --
 *
 *	Find (or create) a namespace by qualified name.  Walks the
 *	"::" separated path components starting from the global or
 *	current namespace.
 *
 *	RESOLUTION ALGORITHM:
 *
 *	1. Empty name or "::" alone -> return the global namespace.
 *	2. If the name starts with "::", begin at pGlobalNs and
 *	   skip the leading "::".  Otherwise begin at pCurrentNs.
 *	3. For each "::" separated component:
 *	   a. Look up the component in the current namespace's
 *	      paChild hash.
 *	   b. If found, descend into that child namespace.
 *	   c. If not found and bCreate is true, create a new child
 *	      namespace with its own paCmd, paVar, and paChild
 *	      hashes, compute its fully qualified name, and insert
 *	      it into the parent's paChild hash.
 *	   d. If not found and bCreate is false, return NULL.
 *	4. Return the final namespace reached.
 *
 * Why / How:
 *	Implements a path-walking algorithm over the namespace tree.
 *	Each "::" separated component is looked up in the current
 *	node's paChild hash.  When bCreate is set, missing nodes are
 *	allocated with their own paCmd, paVar, and paChild hashes
 *	and inserted into the parent's child hash.
 *
 * Results:
 *	Pointer to the namespace, or NULL if not found and bCreate
 *	is false.
 *
 * Side effects:
 *	If bCreate is true, missing intermediate namespaces are
 *	created.
 *
 *----------------------------------------------------------------------
 */

Th8_Namespace *
th8FindNamespace(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName, /* Namespace path (e.g. "::foo::bar"). */
    size_t nName, /* Length of zName. */
    int bCreate) /* Create if not found? */
{
    Th8_Namespace *pNs;
    const char *z;
    size_t n;

    if (nName == TH8_NOLEN) {
	size_t k = 0;

	while (zName[k]) {
	    k++;
	}
	nName = k;
    }

    /*
     * Empty name or "::" alone means the global namespace.
     */

    if (nName == 0) {
	return interp->pGlobalNs;
    }
    if (nName == 2 && zName[0] == ':' && zName[1] == ':') {
	return interp->pGlobalNs;
    }

    /*
     * Determine the starting namespace and skip any leading "::".
     */

    if (nName >= 2 && zName[0] == ':' && zName[1] == ':') {
	pNs = interp->pGlobalNs;
	z = &zName[2];
	n = nName - 2;
    } else {
	pNs = interp->pCurrentNs;
	z = zName;
	n = nName;
    }

    /*
     * Walk each "::" separated component.
     */

    while (n > 0) {
	const char *zComp = z;
	size_t nComp = 0;
	Th8_HashEntry *pEntry;

	/*
	 * Find the next "::" or end of string.
	 */

	while (nComp < n) {
	    if (nComp + 1 < n && z[nComp] == ':' && z[nComp + 1] == ':') {
		break;
	    }
	    nComp++;
	}

	if (nComp == 0) {
	    /*
	     * Skip empty component (e.g. leading "::").
	     */

	    z += 2;
	    n -= 2;
	    continue;
	}

	/*
	 * Look up the component in the current namespace's
	 * child hash.
	 */

	pEntry = Th8_HashFind(interp, pNs->paChild, zComp, nComp, 0);

	if (!pEntry) {
	    if (!bCreate) {
		return 0;
	    }

	    /*
	     * Create a new child namespace.
	     */

	    {
		Th8_Namespace *pChild;
		char *zFull;

		pChild = (Th8_Namespace *)
		    TH8_ALLOC(interp, sizeof(Th8_Namespace));
		if (!pChild) return 0;
		zFull = th8GetFullNsName(interp, pNs, zComp, nComp);
		if (!zFull) {
		    Th8_Free(interp, pChild);
		    return 0;
		}
		pChild->zName = zFull;
		pChild->nName = Th8_Strlen(interp, zFull);
		pChild->pParent = pNs;
		pChild->paCmd = Th8_HashNew(interp);
		if (!pChild->paCmd) {
		    Th8_Free(interp, zFull);
		    Th8_Free(interp, pChild);
		    return 0;
		}
#if defined(TH8_ENABLE_VARIABLES)
		pChild->paVar = Th8_HashNew(interp);
		if (!pChild->paVar) {
		    Th8_HashDelete(interp, pChild->paCmd);
		    Th8_Free(interp, zFull);
		    Th8_Free(interp, pChild);
		    return 0;
		}
#endif
		pChild->paChild = Th8_HashNew(interp);
		if (!pChild->paChild) {
#if defined(TH8_ENABLE_VARIABLES)
		    Th8_HashDelete(interp, pChild->paVar);
#endif
		    Th8_HashDelete(interp, pChild->paCmd);
		    Th8_Free(interp, zFull);
		    Th8_Free(interp, pChild);
		    return 0;
		}

		pEntry = Th8_HashFind(interp, pNs->paChild, zComp, nComp, 1);
		if (!pEntry) {
		    Th8_HashDelete(interp, pChild->paChild);
#if defined(TH8_ENABLE_VARIABLES)
		    Th8_HashDelete(interp, pChild->paVar);
#endif
		    Th8_HashDelete(interp, pChild->paCmd);
		    Th8_Free(interp, zFull);
		    Th8_Free(interp, pChild);
		    return 0;
		}
		pEntry->pData = (void *)pChild;
	    }
	}

	pNs = (Th8_Namespace *)pEntry->pData;

	/*
	 * Advance past the component and any "::" separator.
	 */

	z += nComp;
	n -= nComp;
	/* The inner component scanner above only breaks early
	 * when it finds "::" at z[nComp]; otherwise it exits
	 * via nComp >= n leaving n == 0 here.  So whenever
	 * n >= 2 at this point, z[0] and z[1] are invariantly
	 * the ':' bytes of the separator. */
	if (n >= 2 && ALWAYS(z[0] == ':') && ALWAYS(z[1] == ':')) {
	    z += 2;
	    n -= 2;
	}
    }

    return pNs;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreeNamespace --
 *
 *	Recursively free a namespace and all of its children.
 *	Commands have their delete callbacks invoked.
 *
 * Why / How:
 *	Depth-first recursive walk: each child namespace is freed
 *	before the parent.  Command delete callbacks are invoked
 *	before freeing command entries.  The export list and all
 *	hash tables (commands, variables, children) are cleaned up.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All memory for the namespace tree is released.
 *
 *----------------------------------------------------------------------
 */

static int th8FreeCmdEntry(Th8_HashEntry *pEntry, void *pCtx);
static void th8FreeNamespace(Th8_Interp *interp, Th8_Namespace *pNs);

/*
 *----------------------------------------------------------------------
 *
 * th8FreeChildEntry --
 *
 *	Hash iteration callback: free a child namespace entry.
 *
 * Why / How:
 *	Called via Th8_HashIterate when destroying a parent
 *	namespace's paChild hash.  Delegates the recursive
 *	cleanup to th8FreeNamespace, then NULLs the hash entry
 *	to prevent double-free.
 *
 * Results:
 *	Always returns TH8_OK (continue iterating).
 *
 * Side effects:
 *	The child namespace is recursively freed.
 *
 *----------------------------------------------------------------------
 */

static int
th8FreeChildEntry(
    Th8_HashEntry *pEntry, /* Hash entry to process. */
    void *pCtx) /* Interpreter (as void*). */
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;

    if (pEntry->pData) {
	th8FreeNamespace(interp, (Th8_Namespace *)pEntry->pData);
	pEntry->pData = 0;
    }
    return TH8_OK;
}


/*
 * th8FreeDataEntry --
 *
 *	Hash iteration callback: free a generic pData entry.
 */

static int
th8FreeDataEntry(Th8_HashEntry *pEntry, void *pCtx)
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;

    if (pEntry->pData) {
	Th8_Free(interp, pEntry->pData);
	pEntry->pData = 0;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreeNamespace --
 *
 *	Recursively free a namespace and all of its children,
 *	commands, and variables.
 *
 * Why / How:
 *	Performs a depth-first traversal: iterates paChild to free
 *	all descendants, then frees commands (invoking xDel callbacks),
 *	variables, hash tables, the export string, the name string,
 *	and finally the Th8_Namespace struct itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All memory for the namespace tree is released.  Command
 *	delete callbacks are invoked.
 *
 *----------------------------------------------------------------------
 */

static void
th8FreeNamespace(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Namespace *pNs) /* Namespace to free. */
{
    if (!pNs) {
	return;
    }

    /*
     * Recursively free all child namespaces.
     */

    Th8_HashIterate(interp, pNs->paChild, th8FreeChildEntry, (void *)interp);

    /*
     * Free all commands (invoke delete callbacks).
     */

    Th8_HashIterate(interp, pNs->paCmd, th8FreeCmdEntry, (void *)interp);

    /*
     * Free all namespace variables.
     */

#if defined(TH8_ENABLE_VARIABLES)
    Th8_HashIterate(interp, pNs->paVar, th8FreeVarEntry, (void *)interp);
#endif

    /*
     * Delete the hash tables.
     */

    Th8_HashDelete(interp, pNs->paCmd);

#if defined(TH8_ENABLE_VARIABLES)
    Th8_HashDelete(interp, pNs->paVar);
#endif

    Th8_HashDelete(interp, pNs->paChild);

    /*
     * Free expansion operators.
     */

    if (pNs->paExpansion) {
	Th8_HashIterate(
	    interp, pNs->paExpansion, th8FreeDataEntry, (void *)interp);
	Th8_HashDelete(interp, pNs->paExpansion);
    }

    /*
     * Free the name and the namespace struct itself.
     */

    /* Bug 33: pool-allocated; see th8ClearNamespace for the
     * matching reset path. */
    if (pNs->zExport) {
	th8BufferFree(interp, pNs->zExport, TH8_LEN(pNs->nExport) + 1);
    }
    Th8_Free(interp, pNs->zName);
    Th8_Free(interp, pNs);
}


/*
 *----------------------------------------------------------------------
 *
 * th8QueuePendingCmd --
 *
 *	Defer a command deletion until the eval stack fully unwinds.
 *	The command has already been removed from the namespace hash
 *	and token index; only the xDel callback and memory free remain.
 *
 *----------------------------------------------------------------------
 */

static void
th8QueuePendingCmd(Th8_Interp *interp, Th8_Command *pCmd)
{
    Th8_PendingDelete *pEntry;

    pEntry = (Th8_PendingDelete *)
        TH8_ALLOC(interp, sizeof(Th8_PendingDelete));
    if (!pEntry) {
	/*
	 * Out of memory -- fall back to immediate deletion.
	 * Better to risk the use-after-free than to leak the
	 * command permanently.
	 */
	if (pCmd->xDel) {
	    pCmd->xDel(interp, pCmd->pContext);
	}
	Th8_Free(interp, pCmd->zQualName);
	Th8_Free(interp, pCmd);
	return;
    }
    pEntry->eType = TH8_PENDING_CMD;
    pEntry->u.cmd.pCmd = pCmd;
    pEntry->pNext = 0;

    if (interp->pPendingTail) {
	interp->pPendingTail->pNext = pEntry;
    } else {
	interp->pPendingHead = pEntry;
    }
    interp->pPendingTail = pEntry;
}


/*
 *----------------------------------------------------------------------
 *
 * th8QueuePendingNs --
 *
 *	Defer a namespace deletion until the eval stack fully unwinds.
 *	The namespace has already been detached from its parent's
 *	child hash; only the recursive free (commands, variables,
 *	children) remains.
 *
 *----------------------------------------------------------------------
 */

static void
th8QueuePendingNs(Th8_Interp *interp, Th8_Namespace *pNs)
{
    Th8_PendingDelete *pEntry;

    pEntry = (Th8_PendingDelete *)
        TH8_ALLOC(interp, sizeof(Th8_PendingDelete));
    if (!pEntry) {
	/* Out of memory -- immediate deletion as fallback. */
	th8FreeNamespace(interp, pNs);
	return;
    }
    pEntry->eType = TH8_PENDING_NS;
    pEntry->u.ns.pNs = (void *)pNs;
    pEntry->pNext = 0;

    if (interp->pPendingTail) {
	interp->pPendingTail->pNext = pEntry;
    } else {
	interp->pPendingHead = pEntry;
    }
    interp->pPendingTail = pEntry;
}


/*
 *----------------------------------------------------------------------
 *
 * th8DrainPendingDeletes --
 *
 *	Process all deferred deletions.  Called when nEvalDepth drops
 *	to 0 (outermost eval returns) and during interpreter teardown.
 *
 *	The queue is FIFO so deletions run in the order they were
 *	requested.  New entries may be added during draining (e.g. a
 *	command xDel that deletes another command); the loop continues
 *	until the queue is empty.
 *
 *----------------------------------------------------------------------
 */

static void
th8DrainPendingDeletes(Th8_Interp *interp)
{
    while (interp->pPendingHead) {
	Th8_PendingDelete *pEntry = interp->pPendingHead;

	interp->pPendingHead = pEntry->pNext;
	if (!interp->pPendingHead) {
	    interp->pPendingTail = 0;
	}

	switch (pEntry->eType) {
	case TH8_PENDING_CMD: {
	    Th8_Command *pCmd = pEntry->u.cmd.pCmd;

	    if (pCmd) {
		if (pCmd->xDel) {
		    pCmd->xDel(interp, pCmd->pContext);
		}
		Th8_Free(interp, pCmd->zQualName);
		Th8_Free(interp, pCmd);
	    }
	    break;
	}
	case TH8_PENDING_NS: {
	    Th8_Namespace *pNs;

	    pNs = (Th8_Namespace *)pEntry->u.ns.pNs;
	    th8FreeNamespace(interp, pNs);
	    break;
	}
	default:
	    break;
	}
	Th8_Free(interp, pEntry);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Frame management --
 *
 *	Frames are heap-allocated.  The global frame is allocated as
 *	part of the interpreter struct (single allocation).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8PushFrame --
 *
 *	Push a new call frame onto the interpreter's frame stack.
 *	Initializes the frame's variable hash and links it to the
 *	current frame.
 *
 * Why / How:
 *	Allocates a variable hash for the frame, records the caller
 *	frame (pCaller) and current namespace (pNs), then sets
 *	interp->pFrame to the new frame.  This forms the linked
 *	list that [uplevel] and [upvar] traverse.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->pFrame is updated to point to the new frame.
 *
 *----------------------------------------------------------------------
 */

static int
th8PushFrame(
    Th8_Interp *interp, /* Interpreter. */
    Th8_Frame *pFrame) /* Frame to push. */
{
#if defined(TH8_ENABLE_VARIABLES)
    pFrame->paVar = Th8_HashNew(interp);
    if (!pFrame->paVar) return TH8_ERROR;
#endif
    pFrame->pCaller = interp->pFrame;
    pFrame->pNs = interp->pCurrentNs;
    pFrame->argc = 0;
    pFrame->argv = 0;
    pFrame->argl = 0;
    interp->pFrame = pFrame;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8PopFrame --
 *
 *	Pop the current call frame, freeing all its variables and
 *	its variable hash table.
 *
 * Why / How:
 *	Iterates the frame's paVar hash to free all local variables,
 *	deletes the hash, then restores interp->pFrame to the
 *	caller frame.  The frame struct itself is freed by the
 *	caller (who allocated it).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->pFrame is restored to the caller's frame.
 *
 *----------------------------------------------------------------------
 */

static void
th8PopFrame(Th8_Interp *interp) /* Interpreter. */
{
    Th8_Frame *pFrame = interp->pFrame;

#if defined(TH8_ENABLE_VARIABLES)
    Th8_HashIterate(interp, pFrame->paVar, th8FreeVarEntry, (void *)interp);
    Th8_HashDelete(interp, pFrame->paVar);
#endif

    interp->pFrame = pFrame->pCaller;
}


/*
 *----------------------------------------------------------------------
 *
 * Result management --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8ReleaseOldResult --
 *
 *	Release the current interpreter result in preparation for
 *	a new one.  Centralizes the cleanup logic for the four
 *	result-setting paths (Th8_SetResult, Th8_SetResultStatic,
 *	Th8_ClearResult, th8SetResultBorrowed) so that all paths
 *	uniformly handle:
 *	  - Borrowed results (no free, no secure-zero).
 *	  - Sensitive results in the protected backing region
 *	    (zero data area in place, retain region for reuse).
 *	  - Sensitive results in regular heap (secure-zero, then
 *	    free).
 *	  - Regular results (free).
 *
 * Why / How:
 *	After this call, the caller MUST overwrite zResult/nResult
 *	with the new value or set them to 0/0.  This function does
 *	NOT clear those fields itself because some callers want to
 *	skip clearing them when assigning the new value immediately.
 *	bResultBorrowed is reset to 0 on every non-borrowed path (and
 *	on the protected-region path) so the new result starts in a
 *	clean state.  Sensitivity is not a separate flag: it rides in
 *	nResult's tag bits and is cleared when the caller overwrites
 *	nResult immediately after this call.
 *
 *----------------------------------------------------------------------
 */

static void
th8ReleaseOldResult(Th8_Interp *interp)
{
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /*
     * Sensitive result in the per-interp protected region: zero the
     * data area in place, leave the region allocated for reuse on
     * the next sensitive result.
     */

    if (TH8_SENSITIVE(interp->nResult) && interp->bResultBorrowed &&
        interp->pProtectedResult && interp->zResult) {
	Th8_ProtectedRegion *pPR = (Th8_ProtectedRegion *)
	                               interp->pProtectedResult;
	unsigned char *pData = th8ProtectedData(pPR);
	size_t nUsable = th8ProtectedPageSize(pPR);
	size_t nCanary = th8ProtectedCanarySize();

	if (pData && nUsable >= nCanary) {
	    Th8_SecureZero(interp, pData, nUsable - nCanary);
	}
	interp->bResultBorrowed = 0;
	/* Sensitivity rode in nResult; the caller overwrites nResult
	 * immediately after this release, clearing the classification. */
	return;
    }
#endif

    if (interp->bResultBorrowed) {
	/*
	 * Result is borrowed (cache buffer or other caller-owned
	 * memory).  Do NOT free or zero.
	 */
	interp->bResultBorrowed = 0;
	return;
    }

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /*
     * Sensitive result that lives in regular heap (set via
     * Th8_MarkResultSensitive after a Th8_SetResult).  Secure-zero
     * the buffer before freeing it.
     */

    if (TH8_SENSITIVE(interp->nResult) && interp->zResult) {
	/* nResult may carry tag bits; use the raw byte length for the
	 * secure-zero span (an unmasked length would zero far past the
	 * buffer).  The caller overwrites nResult right after, clearing
	 * the sensitive classification. */
	Th8_SecureZero(interp, interp->zResult, TH8_LEN(interp->nResult) + 1);
    }
#endif

    Th8_Free(interp, interp->zResult);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResult --
 *
 *	Set the current interpreter result by copying a buffer.
 *
 * Why / How:
 *	Frees (or releases) the old result, then allocates a new
 *	buffer and copies the input.  Handles borrowed results
 *	(no free) and sensitive results (secure zero before free).
 *	This is the primary result-setting API.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Previous result is freed.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetResult(
    Th8_Interp *interp, /* Interpreter. */
    const char *z, /* Result string (may be NULL). */
    size_t n) /* Length (TH8_NOLEN = NUL-term). */
{
    if (!interp) return TH8_ERROR;

    TH8_ASSERT_OWNER(interp);

    /*
     * If the outgoing result was marked sensitive (decrypted
     * secure-variable data), securely zero it before freeing
     * to minimize the window for plaintext exposure in
     * pageable heap memory.
     */

    th8ReleaseOldResult(interp);
    interp->zResult = 0;
    interp->nResult = 0;
    if (z) {
	size_t nRaw, nTag = 0;

	/*
	 * Split the incoming length into its raw byte count and its tag
	 * bits (taint and/or sensitive).  TH8_NOLEN (all bits set) MUST be
	 * resolved before inspecting the tags.  Allocation, copying, and
	 * indexing use the raw length; the stored nResult carries the tags
	 * so the value stays classified -- tainted for the evaluator's
	 * security gate and `string is tainted`, sensitive for
	 * Th8_IsResultSensitive and the rendering/export boundaries.
	 */

	if (n == TH8_NOLEN) {
	    nRaw = Th8_Strlen(interp, z);
	} else {
	    nRaw = TH8_LEN(n);
	    nTag = n & TH8_TAG_BITS;
	}
	interp->zResult = (char *)TH8_ALLOC_STR(interp, nRaw);
	if (!interp->zResult) {
	    return TH8_ERROR;
	}
	Th8_Memcpy(interp, interp->zResult, z, nRaw);
	interp->zResult[nRaw] = 0;
	interp->nResult = nRaw | nTag;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResultStatic --
 *
 *	Set the interpreter result to a static (compile-time constant)
 *	string.  The string is NOT copied - the pointer is stored
 *	directly.  The caller guarantees that the string lives for
 *	the lifetime of the interpreter (e.g., a C string literal).
 *
 *	This function never allocates memory and cannot fail.  It is
 *	the ONLY safe way to set an error message inside an out-of-
 *	memory error path.
 *
 * Why / How:
 *	Stores the pointer directly (cast away const) and marks the
 *	result as borrowed so that the next Th8_SetResult will not
 *	attempt to free it.  Zero allocation makes it safe to use
 *	when the allocator itself has failed.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetResultStatic(
    Th8_Interp *interp, /* Interpreter. */
    const char *z, /* Static string (NOT freed). */
    size_t n) /* Length (TH8_NOLEN = NUL-term). */
{
    if (!interp) return;
    th8ReleaseOldResult(interp);

    if (z) {
	size_t nRaw, nTag = 0;

	/*
	 * Preserve the taint bit in nResult.  The static pointer is
	 * stored as-is (no allocation), so only the stored length is
	 * affected.  Resolve TH8_NOLEN before inspecting the taint bit.
	 */

	if (n == TH8_NOLEN) {
	    nRaw = Th8_Strlen(interp, z);
	} else {
	    nRaw = TH8_LEN(n);
	    nTag = n & TH8_TAG_BITS;
	}
	interp->zResult = (char *)(size_t)z; /* cast away const */
	interp->nResult = nRaw | nTag;
	interp->bResultBorrowed = 1; /* do NOT free on next SetResult */
    } else {
	interp->zResult = 0;
	interp->nResult = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ClearResult --
 *
 *	Reset the interpreter result to NULL (no result).  Frees any
 *	previously allocated or borrowed result.
 *
 * Why / How:
 *	Follows the same free/secure-zero logic as Th8_SetResult
 *	but leaves both zResult and nResult at zero rather than
 *	installing a new value.  Called at the start of command
 *	evaluation to discard stale results.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The previous result is freed (or released if borrowed).
 *	zResult is set to NULL, nResult to 0.
 *
 *----------------------------------------------------------------------
 */

void
Th8_ClearResult(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return;
    th8ReleaseOldResult(interp);
    interp->zResult = 0;
    interp->nResult = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetResult --
 *
 *	Get a pointer to the current interpreter result.
 *
 * Why / How:
 *	Returns zResult if non-NULL, or a static empty string ""
 *	otherwise.  This guarantees the caller never receives NULL,
 *	simplifying all result consumers.
 *
 * Results:
 *	Pointer to the result string (never NULL).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8SetResultBorrowed --
 *
 *	Set the interpreter result to a borrowed pointer.  The
 *	caller retains ownership of the memory - the interpreter
 *	will NOT free it.  The pointer must remain valid until the
 *	next Th8_SetResult or th8SetResultBorrowed call.
 *
 *	This eliminates the malloc+memcpy overhead of Th8_SetResult
 *	for results that point into long-lived buffers (e.g., the
 *	append command's cache buffer).
 *
 * Why / How:
 *	Frees the old result, then stores the caller's pointer
 *	directly and sets bResultBorrowed=1.  The interpreter will
 *	not free this pointer on the next result change.  The
 *	caller must ensure the pointer stays valid until the next
 *	result-modifying call.
 *
 *----------------------------------------------------------------------
 */

int
th8SetResultBorrowed(Th8_Interp *interp, const char *z, size_t n)
{
    size_t nRaw, nTag = 0;

    th8ReleaseOldResult(interp);

    if (n == TH8_NOLEN) {
	nRaw = Th8_Strlen(interp, z);
    } else {
	nRaw = TH8_LEN(n);
	nTag = n & TH8_TAG_BITS;
    }

    interp->zResult = (char *)z;
    interp->nResult = nRaw | nTag;
    interp->bResultBorrowed = 1;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetResult --
 *
 *	Retrieve the current interpreter result string and its
 *	byte length.  If the result is NULL (cleared state), an
 *	empty string is returned instead, so the caller never
 *	needs to check for NULL.
 *
 * Why / How:
 *	This is the primary result-reading API.  It does not
 *	transfer ownership; the returned pointer remains valid
 *	until the next result-modifying call.
 *
 * Results:
 *	Pointer to the result string (never NULL).  If pN is
 *	non-NULL, *pN is set to the byte length.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_GetResult(
    Th8_Interp *interp, /* Interpreter. */
    size_t *pN) /* OUT: length (may be NULL). */
{
    if (pN) {
	*pN = interp->nResult;
    }
    return interp->zResult ? interp->zResult : "";
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_TakeResult --
 *
 *	Take ownership of the interpreter result.  The caller must
 *	free the returned buffer with Th8_Free().
 *
 * Why / How:
 *	Detaches zResult from the interpreter and returns it.  For
 *	borrowed results, a copy is allocated first since the caller
 *	expects to own (and free) the returned buffer.  After the
 *	call, the interpreter result is NULL.
 *
 * Results:
 *	Pointer to the result string (may be NULL).
 *
 * Side effects:
 *	Interpreter result is cleared.
 *
 *----------------------------------------------------------------------
 */

char *
Th8_TakeResult(
    Th8_Interp *interp, /* Interpreter. */
    size_t *pN) /* OUT: length (may be NULL). */
{
    char *z = interp->zResult;
    size_t n = interp->nResult;
    size_t nRaw = TH8_LEN(n); /* raw byte length for allocation/copy/index */

    /*
     * Sensitive results MUST be consumed in-place; transferring
     * ownership would either copy plaintext into an unprotected
     * buffer or hand the caller a pointer into a protected region
     * that they cannot legitimately free.  Refuse and set an
     * error so callers see the violation immediately.
     */

    if (TH8_SENSITIVE(n)) {
	if (pN) *pN = 0;
	Th8_SetResultStatic(
	    interp,
	    "sensitive result cannot be detached "
	    "(use Th8_GetResult and consume in place)",
	    TH8_NOLEN);
	return NULL;
    }

    if (pN) {
	*pN =
	    n; /* return the tagged length so the taken value stays tainted */
    }

    if (interp->bResultBorrowed) {
	/*
	 * Result is borrowed - caller expects to own it.
	 * Copy into a fresh allocation.  Allocation, copying, and the
	 * NUL index use the RAW length; the taint bit travels via *pN.
	 */
	char *zCopy = (char *)TH8_ALLOC_STR(interp, nRaw);
	if (zCopy) {
	    Th8_Memcpy(interp, zCopy, z, nRaw);
	    zCopy[nRaw] = 0;
	}
	interp->bResultBorrowed = 0;
	z = zCopy;
    }

    interp->zResult = 0;
    interp->nResult = 0;
    return z;
}


/*
 *----------------------------------------------------------------------
 *
 * th8TakeResultInternal --
 *
 *	Like Th8_TakeResult, but for INTERNAL consumers (the expression
 *	evaluator, the eval loop) that must be able to process a value
 *	regardless of its classification.  The public Th8_TakeResult
 *	refuses a sensitive result so an external caller cannot detach
 *	secret plaintext (R-32296-63095); internal consumers instead
 *	receive a regular-heap COPY of the sensitive plaintext, tagged
 *	sensitive in the returned length so the classification survives
 *	downstream, and the source result is securely cleared.
 *
 * Results:
 *	A newly-owned buffer the caller must Th8_Free (may be NULL on
 *	allocation failure, exactly as TH8_ALLOC_STR).  For a
 *	non-sensitive result this is the ordinary Th8_TakeResult return
 *	(which may transfer ownership without a copy).
 *
 *----------------------------------------------------------------------
 */

char *
th8TakeResultInternal(Th8_Interp *interp, size_t *pN)
{
    if (interp && TH8_SENSITIVE(interp->nResult)) {
	size_t n = 0;
	const char *z = Th8_GetResult(interp, &n);
	size_t nRaw = TH8_LEN(n);
	char *zCopy = (char *)TH8_ALLOC_STR(interp, nRaw);

	if (zCopy) {
	    Th8_Memcpy(interp, zCopy, z, nRaw);
	    zCopy[nRaw] = 0;
	}
	if (pN) *pN = n; /* keep the sensitive (and any taint) tag */
	Th8_ClearResult(interp); /* securely zero the sensitive source */
	return zCopy;
    }
    return Th8_TakeResult(interp, pN);
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreeSensitive --
 *
 *	Free a heap buffer, securely zeroing it first when its tagged
 *	length marks it sensitive.  Defense in depth for secret
 *	plaintext that propagated (via the sensitive tag bit) into
 *	regular-heap holders -- variable data, argv words, expression
 *	operands -- so the plaintext does not linger in freed, pageable
 *	memory.  z may be NULL.  nTagged is the value's length with its
 *	tag bits still set; TH8_LEN(nTagged) + 1 (payload + NUL) is
 *	zeroed, matching the TH8_ALLOC_STR allocation size.
 *
 *----------------------------------------------------------------------
 */

void
th8FreeSensitive(Th8_Interp *interp, char *z, size_t nTagged)
{
    if (z && TH8_SENSITIVE(nTagged)) {
	Th8_SecureZero(interp, z, TH8_LEN(nTagged) + 1);
    }
    Th8_Free(interp, z);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IsResultSensitive --
 *
 *	Returns whether the current result is sensitive, derived from
 *	the sensitive tag bit of nResult (TH8_SENSITIVE).
 *
 * Why / How:
 *	Lets external callers (commands, plugins, embedders) check
 *	whether the current result is sensitive before performing
 *	operations that would copy or detach it.  Always returns 0
 *	on builds without TH8_ENABLE_CRYPTOGRAPHY (the flag is never
 *	set in that configuration).
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsResultSensitive(Th8_Interp *interp)
{
    if (!interp) return 0;
    return TH8_SENSITIVE(interp->nResult);
}


#if defined(TH8_ENABLE_CRYPTOGRAPHY)
/*
 *----------------------------------------------------------------------
 *
 * Th8_MarkResultSensitive --
 *
 *	Mark the current interpreter result as containing sensitive
 *	plaintext.  When the result is later overwritten or cleared,
 *	the buffer SHALL be securely zeroed before being freed.
 *	Idempotent.
 *
 * Why / How:
 *	Promoted from the previous internal th8MarkResultSensitive so
 *	that embedder code (custom commands handling decrypted
 *	tokens, hashed credentials, etc.) can opt the result into the
 *	secure-zero-on-overwrite path without depending on internal
 *	APIs.  This call only sets a flag; it does not move the
 *	result into protected memory.
 *
 *----------------------------------------------------------------------
 */

void
Th8_MarkResultSensitive(Th8_Interp *interp)
{
    if (!interp) return;
    interp->nResult = TH8_ADD_SENSITIVE(interp->nResult);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResultSensitive --
 *
 *	Set the interpreter result to a copy of z (n bytes) stored
 *	in a per-interpreter mlock'd, guard-paged backing region.
 *	The result is automatically marked sensitive; subsequent
 *	overwrites or clears zero the protected region's data area
 *	in place rather than freeing it.
 *
 * Why / How:
 *	The protected region is allocated lazily on first use via
 *	th8ProtectedAlloc and reused for the lifetime of the
 *	interpreter (freed by Th8_DeleteInterp).  This amortizes the
 *	page-aligned allocation cost across all sensitive result
 *	transitions.  After copying, zResult/nResult point INTO the
 *	region and bResultBorrowed is set so the standard free path
 *	does not call Th8_Free on the region pointer.
 *
 *	Capacity: one OS page minus the canary (typically 4088 bytes).
 *	If n exceeds capacity, this function returns TH8_ERROR with
 *	a descriptive error; callers SHOULD prefer this hard failure
 *	over silently degrading to unprotected memory.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR on overflow, mlock failure, or
 *	allocation failure.
 *
 * Side effects:
 *	First call allocates the per-interpreter protected region.
 *	Replaces the current result; old result (if any) is freed
 *	or zeroed-in-place per its ownership.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8GetProtectedResultRegion --
 *
 *	Lazy-allocate and return the per-interp Th8_ProtectedRegion
 *	used both as the backing store for sensitive interpreter
 *	results and as the decryption destination for secure-
 *	variable operations.  Reused for the lifetime of the
 *	interpreter; freed by Th8_DeleteInterp.
 *
 * Why / How:
 *	Centralizing the lazy-alloc here means Th8_SetResultSensitive,
 *	th8SecureDecrypt callers (getVar, save), and any future
 *	consumer share one mlock'd page rather than each allocating
 *	their own.  Caller is responsible for canary verification
 *	and for clearing/zeroing the prior contents before reuse.
 *
 *----------------------------------------------------------------------
 */

Th8_ProtectedRegion *
th8GetProtectedResultRegion(Th8_Interp *interp)
{
    Th8_ProtectedRegion *pPR;

    if (!interp) return NULL;
    if (interp->pProtectedResult) {
	return (Th8_ProtectedRegion *)interp->pProtectedResult;
    }
    pPR = (Th8_ProtectedRegion *)
        TH8_ALLOC(interp, sizeof(Th8_ProtectedRegion));
    if (!pPR) {
	Th8_SetResultStatic(
	    interp, "out of memory (protected result region)", TH8_NOLEN);
	return NULL;
    }
    if (th8ProtectedAlloc(interp, pPR) != TH8_OK) {
	Th8_Free(interp, pPR);
	Th8_SetResultStatic(
	    interp, "cannot allocate protected memory for result", TH8_NOLEN);
	return NULL;
    }
    interp->pProtectedResult = pPR;
    return pPR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FinalizeSensitiveResult --
 *
 *	Make the bytes already living in the protected result
 *	region's data area into the current sensitive interpreter
 *	result.  Caller has written nLen bytes starting at
 *	(th8ProtectedData(pPR) + 0) (i.e., the same place
 *	Th8_SetResultSensitive copies into).  This helper writes
 *	the trailing NUL and updates zResult/nResult/flags.
 *
 *----------------------------------------------------------------------
 */

int
th8FinalizeSensitiveResult(Th8_Interp *interp, size_t nLen)
{
    Th8_ProtectedRegion *pPR;
    unsigned char *pData;

    /* Bug 26 family: plain interp guard (Bug 31 pattern). */
    if (!interp || !interp->pProtectedResult) return TH8_ERROR;
    pPR = (Th8_ProtectedRegion *)interp->pProtectedResult;
    pData = th8ProtectedData(pPR);
    if (!pData) return TH8_ERROR;

    /* nLen may carry the taint tag: sensitivity and trust (taint) are
     * independent classifications, both now riding in the length's high
     * bits.  Mask for the NUL-terminator offset, force the sensitive bit
     * on (this helper's whole purpose is a sensitive result), and preserve
     * any incoming taint tag. */
    pData[TH8_LEN(nLen)] = '\0';
    interp->zResult = (char *)pData;
    interp->nResult = TH8_ADD_SENSITIVE(nLen);
    interp->bResultBorrowed = 1; /* memory owned by protected region */
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResultSensitive --
 *
 *	Set the interpreter result to a copy of `z` (`n` bytes)
 *	stored in the per-interpreter protected memory region
 *	(see `Th8_ProtectedRegion`), and mark the result as
 *	sensitive.  The protected region is `mlock`'d and guard-
 *	paged so the plaintext is never written to swap, never
 *	appears in core dumps, and is flanked by `PROT_NONE` pages
 *	that catch buffer overruns.
 *
 *	The region is allocated lazily on first use and reused
 *	for the interpreter's lifetime; subsequent calls
 *	overwrite the previous contents in place after a canary
 *	check.  Use this for cryptographic plaintext (decrypted
 *	secure variables, harpy verification material, etc.)
 *	rather than `Th8_SetResult`.
 *
 *	Available only when TH8 is built with
 *	`TH8_ENABLE_CRYPTOGRAPHY`.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	z      -- pointer to the source bytes; may be NULL only
 *	          when `n == 0`.
 *	n      -- length of `z` in bytes, or `TH8_NOLEN` to call
 *	          `Th8_Strlen(interp, z)`.
 *
 * Returns:
 *	`TH8_OK` on success.
 *	`TH8_ERROR` with the interpreter result set to a
 *	descriptive message when:
 *	  - the protected region could not be allocated,
 *	  - the existing region's canary check failed,
 *	  - `n` exceeds the region's usable capacity (one OS page
 *	    minus the canary).
 *
 * Side effects:
 *	May allocate the per-interpreter protected region on
 *	first use.  Overwrites the region's data area with `z`
 *	plus the trailing canary.  Marks the interpreter result
 *	as sensitive so later `Th8_ClearResult` securely zeroes
 *	the region before reuse.
 *
 *----------------------------------------------------------------------
 */
int
Th8_SetResultSensitive(Th8_Interp *interp, const char *z, size_t n)
{
    Th8_ProtectedRegion *pPR;
    unsigned char *pData;
    size_t nUsable, nCanary;
    size_t nTag = 0; /* trust tag, preserved independently of sensitivity */

    if (!interp) return TH8_ERROR;

    pPR = th8GetProtectedResultRegion(interp);
    if (!pPR) return TH8_ERROR;

    /*
     * Verify the canary is intact (detects overflow, use-after-
     * free, or external memory corruption of the protected region
     * since the previous use).
     */

    if (th8ProtectedCheckCanary(interp, pPR) != TH8_OK) {
	/* th8ProtectedCheckCanary set the result already. */
	return TH8_ERROR;
    }

    nCanary = th8ProtectedCanarySize();
    nUsable = th8ProtectedPageSize(pPR);
    if (nUsable < nCanary) {
	Th8_SetResultStatic(
	    interp, "protected region too small for canary", TH8_NOLEN);
	return TH8_ERROR;
    }
    nUsable -= nCanary;

    if (n == TH8_NOLEN) {
	n = Th8_Strlen(interp, z);
    } else {
	/* Capture the trust tag before masking; a sensitive result that
	 * came from untrusted input stays tainted. */
	nTag = n & TH8_TAG_BITS;
	n = TH8_LEN(n);
    }

    /*
     * The data buffer must hold n bytes of payload plus a NUL
     * terminator (so callers receive a C-string just like for
     * regular results).  Reject anything that does not fit.
     */

    if (n + 1 < n || n + 1 > nUsable) {
	Th8_SetResultStatic(
	    interp, "sensitive result exceeds protected region capacity",
	    TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Release the prior result.  Th8_ClearResult handles both
     * borrowed-region and regular-heap cases, including securely
     * zeroing the prior protected-region payload in place.
     */

    Th8_ClearResult(interp);

    pData = th8ProtectedData(pPR);
    if (!pData) {
	Th8_SetResultStatic(
	    interp, "protected region has no data pointer", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (n > 0) {
	Th8_Memcpy(interp, pData, z, n);
    }
    return th8FinalizeSensitiveResult(interp, n | nTag);
}
#endif /* TH8_ENABLE_CRYPTOGRAPHY */


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResultInt --
 *
 *	Set the interpreter result to the string representation of
 *	an integer.  No libc dependency (no sprintf).
 *
 * Why / How:
 *	Converts the integer to decimal digits in a stack buffer
 *	using repeated division.  Handles negative values via
 *	unsigned arithmetic to avoid undefined behavior on INT_MIN.
 *	Then delegates to Th8_SetResult to install the string.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Previous result is freed.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetResultInt(
    Th8_Interp *interp, /* Interpreter. */
    int iVal) /* Integer value. */
{
    char zBuf[30];
    int neg = 0;
    unsigned int u;
    char *z;

    if (!interp) return TH8_ERROR;
    if (iVal < 0) {
	neg = 1;
	u = (unsigned int)(-(iVal + 1)) + 1u;
    } else {
	u = (unsigned int)iVal;
    }
    z = &zBuf[sizeof(zBuf) - 1];
    *z = 0;
    do {
	*(--z) = (char)('0' + (u % 10));
	u /= 10;
    } while (u > 0);
    if (neg) {
	*(--z) = '-';
    }
    return Th8_SetResult(interp, z, (size_t)(&zBuf[sizeof(zBuf) - 1] - z));
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetErrorLine --
 *
 *	Return the line number where the most recent error originated.
 *
 * Why / How:
 *	When an error occurs, th8EvalPostCmd stores the current line
 *	in nErrorLine.  This accessor lets error-reporting code
 *	(e.g. [info errorline]) read it without exposing interpreter
 *	internals.
 *
 * Results:
 *	The error line number (0 if no error has occurred).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetErrorLine(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;
    return interp->nErrorLine;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetErrorLine --
 *
 *	Record the line number where an error originated.
 *
 * Why / How:
 *	Called by th8EvalPostCmd when a command returns TH8_ERROR,
 *	capturing the current line so that error-reporting code can
 *	display the exact line of the failure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->nErrorLine is updated.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetErrorLine(
    Th8_Interp *interp, /* Interpreter. */
    int nLine) /* Error line number. */
{
    if (!interp) return;
    interp->nErrorLine = nLine;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetEvalDepth --
 *
 *	Return the current eval nesting depth.  Each call to
 *	th8EvalLocal increments this counter; the cleanup callback
 *	decrements it.
 *
 * Why / How:
 *	Debuggers and embedding hosts use this to display or limit
 *	recursion.  The counter is also used as a secondary defense
 *	against C stack overflow (th8EvalLocal rejects depth > 1000).
 *
 * Results:
 *	The current eval depth (0 at the top level).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetEvalDepth(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;
    return interp->nEvalDepth;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetPlatformLibs --
 *
 *	Return the opaque pointer to the list of dynamically loaded
 *	platform libraries associated with this interpreter.
 *
 * Why / How:
 *	The plugin and package subsystems store a linked list of
 *	loaded shared libraries on the interpreter.  This accessor
 *	hides the interpreter struct from those modules.
 *
 * Results:
 *	The platform library list pointer (may be NULL).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
th8GetPlatformLibs(Th8_Interp *interp)
{
    return interp->pPlatformLibs;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetPlatformLibs --
 *
 *	Store or update the opaque platform library list pointer
 *	on the interpreter.
 *
 * Why / How:
 *	Called by [load] and [unload] to maintain the list of
 *	dynamically loaded libraries.  The pointer is opaque here;
 *	the platform layer manages the actual list structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->pPlatformLibs is updated.
 *
 *----------------------------------------------------------------------
 */

void
th8SetPlatformLibs(Th8_Interp *interp, void *pLibs)
{
    interp->pPlatformLibs = pLibs;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetLine --
 *
 *	Return the current source line number being evaluated.
 *	This tracks the position of the parser within the active
 *	script and is updated as each newline is consumed.
 *
 * Why / How:
 *	Used by error reporting, the debugger, and [info frame] to
 *	identify the script location of the currently executing
 *	command.  Distinct from the error line (nErrorLine), which
 *	records where the most recent error originated.
 *
 * Results:
 *	The current line number (1-based during evaluation).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetLine(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;
    return interp->nLine;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetLine --
 *
 *	Set the interpreter's current source line number.
 *
 * Why / How:
 *	The evaluator calls this as it advances through a script.
 *	It is also used by save/restore operations (e.g. coroutine
 *	context switches) to rewind the line counter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->nLine is updated.
 *
 *----------------------------------------------------------------------
 */

void
th8SetLine(
    Th8_Interp *interp, /* Interpreter. */
    int nLine) /* Current line number. */
{
    interp->nLine = nLine;
}


/*
 *----------------------------------------------------------------------
 *
 * Step counter --
 *
 *	Bounds total interpreter work.  th8Step() is called at
 *	every low-level work-unit boundary.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8Step --
 *
 *	Increment the step counter and check the limit.  This is
 *	the single choke-point for all work-unit accounting.
 *
 * Why / How:
 *	Increments nStepCount unconditionally, then compares against
 *	nStepLimit (when non-zero).  This provides a hard upper bound
 *	on total interpreter work, protecting against infinite loops
 *	and runaway scripts.
 *
 * Results:
 *	TH8_OK if under the limit, TH8_ERROR if exceeded.
 *
 * Side effects:
 *	Sets interpreter result on limit exceeded.
 *
 *----------------------------------------------------------------------
 */

int
th8Step(Th8_Interp *interp) /* Interpreter. */
{
    interp->nStepCount++;
    if (interp->nStepLimit > 0 && interp->nStepCount > interp->nStepLimit) {
	Th8_SetResult(interp, "step limit exceeded", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_SetStepLimit --
 *
 *	Set the maximum number of steps the interpreter may execute.
 *
 * Why / How:
 *	Stores the limit in interp->nStepLimit.  A value of 0
 *	disables the limit, allowing unlimited execution.  The
 *	limit is enforced by th8Step at every work-unit boundary.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the step limit.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetStepLimit(
    Th8_Interp *interp, /* Interpreter. */
    th8_int64_t nLimit) /* Max steps (0 = unlimited). */
{
    if (!interp) return;
    interp->nStepLimit = nLimit;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetStepLimit --
 *
 *	Return the current step limit.
 *
 * Why / How:
 *	Simple accessor for interp->nStepLimit.  Embedding hosts
 *	use this to query the configured limit for diagnostics
 *	or adaptive scheduling.
 *
 * Results:
 *	The step limit (0 = unlimited).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

th8_int64_t
Th8_GetStepLimit(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return 0;
    return interp->nStepLimit;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetStepCount --
 *
 *	Return the number of steps executed since last reset.
 *
 * Why / How:
 *	Simple accessor for interp->nStepCount.  Embedding hosts
 *	use this for profiling and to determine how much budget
 *	remains before the step limit is reached.
 *
 * Results:
 *	The step count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

th8_int64_t
Th8_GetStepCount(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return 0;
    return interp->nStepCount;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ResetStepCount --
 *
 *	Reset the step counter to zero.
 *
 * Why / How:
 *	Sets nStepCount to 0, effectively granting a fresh budget
 *	of nStepLimit steps.  Typically called between top-level
 *	evaluations in interactive or event-loop contexts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Step counter is zeroed.
 *
 *----------------------------------------------------------------------
 */

void
Th8_ResetStepCount(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return;
    interp->nStepCount = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetStepCount --
 *
 *	Set the per-interpreter step counter to an explicit value.
 *
 * Why / How:
 *	Companion to Th8_ResetStepCount.  Embedders and test fixtures
 *	can stage the counter at a specific boundary so a subsequent
 *	Th8_Ready() check fires deterministically -- e.g. setting
 *	nStepCount to nStepLimit-1 so the next per-iteration check
 *	inside a loop body trips on the FIRST iteration.  Used by the
 *	testlib Bug 16 driver to drive the (T,T) MC/DC vector at
 *	src/th8_glob.c L90 (the per-iteration `interp &&
 *	Th8_Ready(interp) != TH8_OK` compound that the entry-check at
 *	L80 short-circuits when started fresh).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->nStepCount is set to MAX(nCount, 0).
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetStepCount(
    Th8_Interp *interp, /* Interpreter (may be NULL: no-op). */
    th8_int64_t nCount) /* New step counter value. */
{
    if (!interp) return;
    interp->nStepCount = (nCount < 0) ? 0 : nCount;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Ready --
 *
 *	Unified readiness check.  Combines cancellation, step
 *	counter, and native stack checking into a single call.
 *	This is the one choke-point that all work-unit boundaries
 *	should call.
 *
 * Results:
 *	TH8_OK if the interpreter is ready to continue.
 *	TH8_ERROR if cancelled, step limit exceeded, or stack
 *	overflow imminent.
 *
 * Why / How:
 *	Runs checks in priority order: stack overflow (must come
 *	first since later checks consume stack), exit flag,
 *	cancellation, suspension, debug breakpoints, and finally
 *	the step counter.  Returns the first non-OK status.
 *
 * Side effects:
 *	Increments the step counter.  Sets interpreter result on
 *	error.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Ready(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;

    /*
     * Stack check first: the other checks consume stack
     * themselves, so verify we have room before calling them.
     */

    if (th8CheckStack(interp) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_IsExited(interp)) {
	Th8_SetResultStatic(interp, "exit", 4);
	return TH8_ERROR;
    }
    if (Th8_IsCanceled(interp, 0) != TH8_OK) {
	return TH8_ERROR;
    }
    if (Th8_IsSuspended(interp)) {
	return TH8_SUSPEND;
    }

    /*
     * Debug check: if a debug callback is installed, check for
     * breakpoints and step mode.  Zero overhead when xDebug is NULL.
     */

    if (interp->xDebug) {
	int dbgRc = th8DebugCheck(interp);
	if (dbgRc != TH8_OK) return dbgRc;
    }

    if (th8Step(interp) != TH8_OK) {
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Result size limit --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResultLimit --
 *
 *	Set the maximum result/string size in bytes.
 *
 * Why / How:
 *	Stores the limit in interp->nResultLimit.  String-building
 *	operations (Th8_StringAppend, Th8_ListAppend) check this
 *	limit before growing a buffer.  A value of 0 means use the
 *	compile-time default (MX_STRLEN).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the result size limit.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetResultLimit(
    Th8_Interp *interp, /* Interpreter. */
    size_t nLimit) /* Max result size (0 = MX_STRLEN). */
{
    if (!interp) return;
    interp->nResultLimit = nLimit;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetResultLimit --
 *
 *	Return the current result size limit.
 *
 * Why / How:
 *	Simple accessor for interp->nResultLimit.  Embedding hosts
 *	use this to query the configured string size cap.
 *
 * Results:
 *	The limit in bytes (0 = compile-time default).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
Th8_GetResultLimit(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return 0;
    return interp->nResultLimit;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetOverflowCheck --
 *
 *	Enable or disable integer overflow checking in expressions.
 *
 * Why / How:
 *	When enabled (default), arithmetic operations that overflow
 *	a 32-bit or 64-bit integer return TH8_ERROR.  When disabled,
 *	overflow silently wraps.  The flag is checked by the
 *	expression evaluator at each arithmetic operation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the overflow check flag.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetOverflowCheck(
    Th8_Interp *interp, /* Interpreter. */
    int bEnable) /* 1=check (default), 0=wrap. */
{
    if (!interp) return;
    interp->bOverflowCheck = bEnable;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetOverflowCheck --
 *
 *	Return whether integer overflow checking is enabled.
 *
 * Why / How:
 *	Simple accessor for interp->bOverflowCheck.  Used by
 *	embedding hosts to query the current arithmetic mode.
 *
 * Results:
 *	1 if overflow checking is on, 0 if wrapping.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetOverflowCheck(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;
    return interp->bOverflowCheck;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetAllocLimit --
 *
 *	Set the maximum total allocation in bytes (sandbox limit).
 *
 * Why / How:
 *	Stores the limit in interp->nAllocLimit.  th8MallocCommon
 *	and th8ReallocCommon check this before every allocation,
 *	preventing untrusted scripts from consuming unbounded
 *	memory.  A value of 0 disables the limit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the allocation limit.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetAllocLimit(Th8_Interp *interp, size_t nLimit)
{
    if (!interp) return;
    interp->nAllocLimit = nLimit;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetAllocLimit --
 *
 *	Return the current allocation limit.
 *
 * Why / How:
 *	Simple accessor for interp->nAllocLimit.  Embedding hosts
 *	use this to query the configured memory sandbox cap.
 *
 * Results:
 *	The limit in bytes (0 = unlimited).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
Th8_GetAllocLimit(Th8_Interp *interp)
{
    if (!interp) return 0;
    return interp->nAllocLimit;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetAllocBytes --
 *
 *	Return the total bytes currently allocated by the interpreter.
 *
 * Why / How:
 *	Simple accessor for interp->nAllocBytes.  This counter is
 *	maintained by th8MallocCommon, th8ReallocCommon, and
 *	Th8_Free.  Embedding hosts compare it against the alloc
 *	limit for diagnostics and resource monitoring.
 *
 * Results:
 *	The allocation count in bytes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
Th8_GetAllocBytes(Th8_Interp *interp)
{
    if (!interp) return 0;
    return interp->nAllocBytes;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EnableBigint --
 *
 *	Enable or disable arbitrary precision integers.
 *	Same random-token pattern as Th8_EnableLoad.
 *
 *	When disabled, integer overflow follows the Tcl 8.4
 *	behavior: error (if overflow check is on) or silent
 *	wrap (if overflow check is off).
 *
 * Why / How:
 *	Uses the random-token gate pattern: generates a non-trivial
 *	64-bit random token via xRandomBytes and stores it in both
 *	nBigintToken and nBigintOk.  The feature is active only when
 *	the two fields match, preventing script-level forgery.
 *	Disabling zeroes both fields.
 *
 *----------------------------------------------------------------------
 */

#if defined(TH8_ENABLE_BIGINT)

int
Th8_EnableBigint(Th8_Interp *interp, int bEnable)
{
    if (!interp) return TH8_ERROR;
    if (bEnable) {
	th8_int64_t tok = 0;

	if (interp->pPlatform->xRandomBytes) {
	    int retries = 0;
	    unsigned char buf[8];

	    do {
		if (++retries > 100) {
		    return TH8_ERROR;
		}
		if (TH8_OK == interp->pPlatform->xRandomBytes(
		                  interp, interp->pPlatform->pCtx, buf, 8)) {
		    size_t j;

		    tok = 0;
		    for (j = 0; j < 8; j++) {
			tok |= ((th8_uint64_t)buf[j]) << (j * 8);
		    }
		}
	    } while (tok == 0 || tok == ~(th8_int64_t)0 || tok == 1);
	} else {
	    return TH8_ERROR;
	}
	interp->nBigintToken = tok;
	interp->nBigintOk = tok;
    } else {
	interp->nBigintToken = 0;
	interp->nBigintOk = 0;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IsBigintEnabled --
 *
 *	Return whether arbitrary precision integers are enabled.
 *
 * Why / How:
 *	Checks the random-token gate: bigint is enabled only when
 *	nBigintOk is non-zero and matches nBigintToken exactly.
 *	This prevents scripts from enabling the feature by setting
 *	a single field.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsBigintEnabled(Th8_Interp *interp)
{
    if (!interp) return TH8_ERROR;
    return interp->nBigintOk != 0 &&
           interp->nBigintOk == interp->nBigintToken;
}

#endif /* TH8_ENABLE_BIGINT */


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetExprFeatures --
 *
 *	Read the per-interpreter expression-grammar feature flag
 *	set.  Default is TH8_EXPR_NONE (strict Tcl 8.6 expr(n)
 *	compliance).  See th8.h for the flag definitions and
 *	semantic guarantees.
 *
 * Why / How:
 *	The flag set is stored in a plain int field on the
 *	Th8_Interp struct (interp->nExprFeatures), zero-initialised
 *	by Th8_CreateInterp via xMemset, so this is a trivial
 *	guarded read.  Unlike Th8_EnableLoad / Th8_EnableBigint,
 *	the expr-feature API does NOT use the random-token gate
 *	pattern: the features it exposes are syntactic extensions,
 *	so a corrupted flag bit at worst makes the parser accept
 *	an operator the embedder did not opt into, which is not a
 *	privilege escalation.
 *
 * Results:
 *	The current flag set (TH8_EXPR_NONE / 0 = strict), or
 *	TH8_EXPR_NONE when `interp` is NULL (Bug 26 / Bug 31 guard).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetExprFeatures(Th8_Interp *interp)
{
    /* Bug 26 family: plain interp guard (Bug 31 pattern). */
    if (!interp) return TH8_EXPR_NONE;
    return interp->nExprFeatures;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_SetExprFeatures --
 *
 *	Replace the interpreter's expression-feature flag set with
 *	`flags` and return the previous value so callers can save
 *	and restore around scoped enables.  Each bit of `flags`
 *	corresponds to a `TH8_EXPR_*` constant; bits without a
 *	defined meaning in the calling library are silently masked
 *	off so embedder code built against a future TH8 release
 *	continues to work when linked against an older library
 *	(see R-56775-06795).
 *
 *	The expression feature flags are NOT script-visible -- this
 *	is a C-embedder-only switch.  An untrusted script cannot
 *	enable extensions on its own interpreter.
 *
 * Parameters:
 *	interp -- interpreter.  If NULL, returns `TH8_EXPR_NONE`
 *	          without effect (Bug 26 / Bug 31 family guard).
 *	flags  -- bitwise-OR of `TH8_EXPR_*` constants; unknown
 *	          bits are masked off.
 *
 * Returns:
 *	The previous flag set (so the caller can restore it),
 *	or `TH8_EXPR_NONE` when `interp` is NULL.
 *
 * Side effects:
 *	Updates `interp->nExprFeatures` to the masked `flags`.
 *
 *----------------------------------------------------------------------
 */
int
Th8_SetExprFeatures(Th8_Interp *interp, int flags)
{
    int old;

    /* Bug 26 family: plain interp guard (Bug 31 pattern). */
    if (!interp) return TH8_EXPR_NONE;
    old = interp->nExprFeatures;
    interp->nExprFeatures = flags & TH8_EXPR_ALL;
    return old;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EnableSignedOnly --
 *
 *	Enable or disable signed-only mode.  When enabled, only
 *	scripts that pass the policy callback are allowed to execute.
 *
 * Why / How:
 *	Uses a random-token gate: two 64-bit fields (nSignedToken
 *	and nSignedOk) are set to the same random value to enable
 *	the gate, or both zeroed to disable it.  The random token
 *	is obtained from xRandomBytes and retried up to 100 times
 *	to avoid trivially guessable values (0, ~0, 1).  This
 *	prevents script-level code from forging the gate.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if xRandomBytes is not
 *	available or fails to produce a valid token.
 *
 * Side effects:
 *	interp->nSignedToken and interp->nSignedOk are updated.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EnableSignedOnly(Th8_Interp *interp, int bEnable)
{
    if (!interp) return TH8_ERROR;
    if (bEnable) {
	th8_int64_t tok = 0;

	if (interp->pPlatform->xRandomBytes) {
	    int retries = 0;
	    unsigned char buf[8];

	    do {
		if (++retries > 100) return TH8_ERROR;
		if (TH8_OK == interp->pPlatform->xRandomBytes(
		                  interp, interp->pPlatform->pCtx, buf, 8)) {
		    size_t j;

		    tok = 0;
		    for (j = 0; j < 8; j++) {
			tok |= ((th8_uint64_t)buf[j]) << (j * 8);
		    }
		}
	    } while (tok == 0 || tok == ~(th8_int64_t)0 || tok == 1);
	} else {
	    return TH8_ERROR;
	}
	interp->nSignedToken = tok;
	interp->nSignedOk = tok;
    } else {
	interp->nSignedToken = 0;
	interp->nSignedOk = 0;
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_IsSignedOnlyEnabled --
 *
 *	Query whether signed-only mode is currently active.
 *
 * Why / How:
 *	The gate is "armed" when both token fields are non-zero
 *	and equal.  Comparing both fields prevents a partial-write
 *	race from falsely indicating enabled/disabled.
 *
 * Results:
 *	Non-zero if signed-only mode is enabled; zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsSignedOnlyEnabled(Th8_Interp *interp)
{
    if (!interp) return TH8_ERROR;
    return interp->nSignedOk != 0 &&
           interp->nSignedOk == interp->nSignedToken;
}


/*
 *----------------------------------------------------------------------
 *
 * th8TestPerturbBigintToken / th8TestPerturbSignedToken --
 *
 *	Test-only helpers that perturb the corresponding gate
 *	token so that nXxxOk != nXxxToken without zeroing
 *	either, driving the (T, F) MC/DC vector at the
 *	Th8_IsBigintEnabled / Th8_IsSignedOnlyEnabled
 *	decisions.  Production callers MUST NOT use these:
 *	they are reachable only via the internal-stubs route
 *	used by the testlib plugin and must be scoped to a
 *	throwaway child interpreter, because the resulting
 *	state keeps the corresponding feature disabled until
 *	the tokens are re-aligned.
 *
 * Why / How:
 *	XORs the token with a non-zero constant that avoids
 *	the {0, 1, ~0} defensive patterns guarded by
 *	Th8_EnableBigint / Th8_EnableSignedOnly retry loops.
 *	Both interp->nBigintToken and interp->nSignedToken
 *	are unconditional members of the interp struct
 *	(declared regardless of TH8_ENABLE_BIGINT), so these
 *	helpers compile in every build configuration.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Mutates interp->nBigintToken or interp->nSignedToken.
 *
 *----------------------------------------------------------------------
 */

/* th8TestPerturbBigintToken / th8TestPerturbSignedToken were moved
 * to src/test/th8_testlib.c (2026-06-08) -- their entire body is one
 * XOR on a public field exposed via th8XorInterpBigintToken /
 * th8XorInterpSignedToken accessors above.  See the testlib block
 * "Bigint/Signed token perturbers" for the relocated implementations. */


/*
 *----------------------------------------------------------------------
 *
 * Th8_SaveSignedOnly --
 *
 *	Save the full signed-only policy state into a caller-
 *	provided buffer.  The state is saved but NOT cleared, so
 *	the policy remains active after the snapshot.
 *
 * Why / How:
 *	When the embedding host needs to temporarily disable
 *	signed-only mode (e.g. to evaluate trusted boot scripts),
 *	it saves the state, disables the gate, runs the trusted
 *	code, then restores.  The saved buffer layout is:
 *
 *	  [0..7]    nSignedToken   (th8_int64_t)
 *	  [8..15]   nSignedOk      (th8_int64_t)
 *	  [16..]    xPolicyCb      (function pointer)
 *	  [..]      pPolicyCbCtx   (void *)
 *
 *	The callback is saved but not cleared because the disabled
 *	gate is sufficient to bypass policy enforcement.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The buffer at pSaved (TH8_SIGNED_SAVE_SIZE bytes) is
 *	filled with the current policy state.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SaveSignedOnly(Th8_Interp *interp, void *pSaved)
{
    char *p = (char *)pSaved;

    if (!interp) return;

    /* Gate tokens (save only, do not clear). */
    Th8_Memcpy(interp, p, &interp->nSignedToken, sizeof(th8_int64_t));
    p += sizeof(th8_int64_t);
    Th8_Memcpy(interp, p, &interp->nSignedOk, sizeof(th8_int64_t));
    p += sizeof(th8_int64_t);

    /* Callback (save only, do not clear). */
    Th8_Memcpy(interp, p, &interp->xPolicyCb, sizeof(interp->xPolicyCb));
    p += sizeof(interp->xPolicyCb);
    Th8_Memcpy(
        interp, p, &interp->pPolicyCbCtx, sizeof(interp->pPolicyCbCtx));
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RestoreSignedOnly --
 *
 *	Restore the signed-only policy state from a buffer
 *	previously filled by Th8_SaveSignedOnly.
 *
 * Why / How:
 *	Reverses the effect of a temporary policy disable.  The
 *	token fields and callback pointer are written back from the
 *	saved buffer, re-arming the signed-only gate and restoring
 *	the original policy callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->nSignedToken, nSignedOk, xPolicyCb, and
 *	pPolicyCbCtx are overwritten from pSaved.
 *
 *----------------------------------------------------------------------
 */

void
Th8_RestoreSignedOnly(Th8_Interp *interp, const void *pSaved)
{
    const char *p = (const char *)pSaved;

    if (!interp) return;

    /* Gate tokens. */
    Th8_Memcpy(interp, &interp->nSignedToken, p, sizeof(th8_int64_t));
    p += sizeof(th8_int64_t);
    Th8_Memcpy(interp, &interp->nSignedOk, p, sizeof(th8_int64_t));
    p += sizeof(th8_int64_t);

    /* Callback. */
    Th8_Memcpy(interp, &interp->xPolicyCb, p, sizeof(interp->xPolicyCb));
    p += sizeof(interp->xPolicyCb);
    Th8_Memcpy(
        interp, &interp->pPolicyCbCtx, p, sizeof(interp->pPolicyCbCtx));
}


/*
 *----------------------------------------------------------------------
 *
 * th8SaveExecCtx --
 *
 *	Snapshot the interpreter's mutable execution context into
 *	a Th8_ExecCtx struct.  The saved fields are the NRE callback
 *	chain, frame stack, current namespace, eval depth, line
 *	number, suspended callbacks, and saved frame.
 *
 * Why / How:
 *	Coroutine context switches need to swap the entire execution
 *	context atomically.  By copying all fields in a single
 *	function, no field can be missed.  "Atomically" here means
 *	all-or-nothing at the C level (no concurrency).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	pCtx is filled with the current interpreter state.
 *
 *----------------------------------------------------------------------
 */

static void
th8SaveExecCtx(Th8_Interp *interp, Th8_ExecCtx *pCtx)
{
    pCtx->pCallbacks = interp->pCallbacks;
    pCtx->pFrame = interp->pFrame;
    pCtx->pCurrentNs = interp->pCurrentNs;
    pCtx->nEvalDepth = interp->nEvalDepth;
    pCtx->nLine = interp->nLine;
    pCtx->pSuspendedCallbacks = interp->pSuspendedCallbacks;
    pCtx->pSavedFrame = interp->pSavedFrame;
}

/*
 *----------------------------------------------------------------------
 *
 * th8RestoreExecCtx --
 *
 *	Restore the interpreter's execution context from a
 *	previously saved Th8_ExecCtx struct.
 *
 * Why / How:
 *	Paired with th8SaveExecCtx.  During coroutine resume, the
 *	caller's context is saved, the coroutine's context is
 *	restored, and evaluation continues where the coroutine
 *	yielded.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The interpreter's callback chain, frame stack, namespace,
 *	eval depth, line number, and suspension state are all
 *	overwritten from pCtx.
 *
 *----------------------------------------------------------------------
 */

static void
th8RestoreExecCtx(Th8_Interp *interp, const Th8_ExecCtx *pCtx)
{
    interp->pCallbacks = pCtx->pCallbacks;
    interp->pFrame = pCtx->pFrame;
    interp->pCurrentNs = pCtx->pCurrentNs;
    interp->nEvalDepth = pCtx->nEvalDepth;
    interp->nLine = pCtx->nLine;
    interp->pSuspendedCallbacks = pCtx->pSuspendedCallbacks;
    interp->pSavedFrame = pCtx->pSavedFrame;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_CoroState --
 *
 *	Per-coroutine state.  Stores the suspended NRE callback
 *	chain and frame for a single coroutine.  Created by
 *	[coroutine], resumed by the generated coroutine command,
 *	freed when the coroutine body finishes or the interpreter
 *	is deleted.
 *
 * Why / How:
 *	A coroutine is a pausable evaluation context.  Each
 *	Th8_CoroState holds a full Th8_ExecCtx snapshot so the
 *	interpreter can switch between the caller's and the
 *	coroutine's execution state via save/restore pairs.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8CheckResultSize --
 *
 *	Internal helper: check if a proposed result size exceeds
 *	the per-interpreter limit.
 *
 * Why / How:
 *	Guards against unbounded string growth from hostile or
 *	runaway scripts.  The limit is the interpreter's nResultLimit
 *	if set, otherwise the compile-time TH8_MX_STRLEN ceiling.
 *
 * Results:
 *	TH8_OK if within limits, TH8_ERROR if exceeded.
 *
 * Side effects:
 *	Sets interpreter result on error.
 *
 *----------------------------------------------------------------------
 */

static int
th8CheckResultSize(
    Th8_Interp *interp, /* Interpreter. */
    size_t n) /* Proposed size. */
{
    size_t nLimit;

    nLimit = interp->nResultLimit;
    if (nLimit == 0) {
	nLimit = TH8_MX_STRLEN;
    }
    if (n > nLimit) {
	Th8_SetResult(interp, "result size limit exceeded", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CheckStack --
 *
 *	Check whether the C stack has grown dangerously close to
 *	its limit.  Uses the address of a local variable as a proxy
 *	for the current stack pointer.
 *
 *	This works on all architectures because it only compares
 *	pointer distances, not actual SP register values.  The
 *	direction of growth is detected once at interpreter creation.
 *
 * Why / How:
 *	Deep recursion (e.g. [proc a {} {a}]) would exhaust the C
 *	stack and crash.  A volatile local variable's address serves
 *	as the stack probe; the distance from the recorded base is
 *	compared against (nStackSize - nStackGuard).  The guard zone
 *	leaves headroom for the error-reporting path itself.
 *
 * Results:
 *	TH8_OK if the stack is safe.
 *	TH8_ERROR if the stack is within the guard zone.
 *	TH8_OK if stack checking is disabled (no bounds provided).
 *
 * Side effects:
 *	Sets the interpreter result on error.
 *
 *----------------------------------------------------------------------
 */

int
th8CheckStack(Th8_Interp *interp) /* Interpreter. */
{
    volatile char marker; /* Address is our stack probe. */
    size_t nUsed;

    if (!interp->bStackCheckEnabled) {
	return TH8_OK;
    }

    /*
     * Compute how much stack we've consumed.
     *
     * On most architectures (x86, x64, ARM, AArch64, MIPS,
     * RISC-V, PowerPC) the stack grows downward: base is a
     * high address, current is lower.
     *
     * On PA-RISC (and some historical architectures) the stack
     * grows upward.  The bStackGrowsDown flag (detected at
     * init) handles both.
     */

    if (interp->bStackGrowsDown) {
	if ((char *)interp->pStackBase > &marker) {
	    nUsed = (size_t)((char *)interp->pStackBase - &marker);
	} else {
	    nUsed = 0;
	}
    } else {
	if (&marker > (char *)interp->pStackBase) {
	    nUsed = (size_t)(&marker - (char *)interp->pStackBase);
	} else {
	    nUsed = 0;
	}
    }

    if (nUsed + interp->nStackGuard > interp->nStackSize) {
	Th8_SetResult(
	    interp, "C stack overflow (TH8 stack limit reached)", TH8_NOLEN);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ErrorMessage --
 *
 *	Set an error message as the interpreter result and clear
 *	the error trace.
 *
 * Why / How:
 *	Centralizes the prefix + detail pattern used by most error
 *	paths (e.g. "no such command: foo").  If the prefix ends
 *	with a double-quote, the detail is appended inline and a
 *	closing quote is added; otherwise a single space separates
 *	them.  If the caller-supplied prefix already ends with a
 *	space, no additional space is inserted -- this defends
 *	embedders, plugins, and future maintainers against the
 *	"prefix ends in space + function adds another" double-space
 *	formatting bug.
 *	Clearing ::errorInfo prevents stale traces from leaking
 *	across unrelated errors.
 *
 * Results:
 *	TH8_ERROR (setting an error message is a failure path; this
 *	lets callers write `return Th8_ErrorMessage(interp, ...)`).
 *
 * Side effects:
 *	Sets ::errorInfo to empty.  Sets interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ErrorMessage(
    Th8_Interp *interp, /* Interpreter. */
    const char *zPre, /* Prefix string. */
    const char *z, /* Detail string. */
    size_t n) /* Length of detail (TH8_NOLEN = NUL). */
{
    char *zRes = 0;
    size_t nRes = 0;
    char cLast;

    if (!interp) return TH8_ERROR;

    /*
     * Sensitivity boundary: an error diagnostic is user-visible egress.
     * If the detail value is sensitive (the tag bit rides in the length
     * n, which callers pass straight from argl[]), redact it so no
     * plaintext byte leaks into the message.  The prefix is always a
     * fixed literal and is never redacted.
     *
     * TH8_NOLEN ((size_t)-1) must be excluded: it is the "compute the
     * NUL-terminated length" sentinel, and being all-ones it has the
     * sensitive tag bit set incidentally -- it is NOT a sensitive
     * value.  Only a real length carrying the bit means sensitive.
     */
    if (n != TH8_NOLEN && TH8_SENSITIVE(n)) {
	z = "<sensitive value withheld>";
	n = TH8_NOLEN;
    }

#if defined(TH8_ENABLE_VARIABLES)
    Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, "", 0);
#endif
    TH8_STR_APPEND(interp, &zRes, &nRes, zPre, TH8_NOLEN);
    cLast = (nRes > 0) ? zRes[nRes - 1] : 0;
    if (cLast == '"') {
	TH8_STR_APPEND(interp, &zRes, &nRes, z, n);
	TH8_STR_APPEND(interp, &zRes, &nRes, "\"", 1);
    } else {
	if (cLast != ' ') {
	    TH8_STR_APPEND(interp, &zRes, &nRes, " ", 1);
	}
	TH8_STR_APPEND(interp, &zRes, &nRes, z, n);
    }
    Th8_SetResult(interp, zRes, nRes);
    Th8_Free(interp, zRes);
    /* Returns TH8_ERROR (per the public contract in th8.h) so callers
     * can write `return Th8_ErrorMessage(interp, ...)`; setting an error
     * message is always a failure path. */
    return TH8_ERROR;

oom:
    /* TH8_STR_APPEND growth failed; "out of memory" already set. */
    Th8_Free(interp, zRes);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_StringAppend --
 *
 *	Append a string to a dynamically allocated buffer.
 *
 * Why / How:
 *	Allocates a fresh buffer of exactly (old + new + 1) bytes,
 *	copies both halves, and frees the old buffer.  The result
 *	size limit is checked before allocation to prevent runaway
 *	growth from hostile scripts.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Buffer is reallocated.
 *
 *----------------------------------------------------------------------
 */

int
Th8_StringAppend(
    Th8_Interp *interp, /* Interpreter for memory. */
    char **pzStr, /* IN/OUT: buffer pointer. */
    size_t *pnStr, /* IN/OUT: buffer length. */
    const char *zApp, /* String to append. */
    size_t nApp) /* Length (TH8_NOLEN = NUL-term). */
{
    size_t nNew;
    size_t nTag;
    char *zNew;

    if (!interp) return TH8_ERROR;
    if (nApp == TH8_NOLEN) {
	nApp = Th8_Strlen(interp, zApp);
    }
    /* Output taint is the old buffer's taint OR the appended taint
     * (TH8_XFER_TAINT semantics); captured before masking either. */
    nTag = (*pnStr & TH8_TAG_BITS) | (nApp & TH8_TAG_BITS);
    nApp = TH8_LEN(nApp);
    nNew = TH8_LEN(*pnStr) + nApp;
    TH8_SIZECHECK(interp, nNew);

    /*
     * Security: result size limit check.
     */

    if (th8CheckResultSize(interp, nNew) != TH8_OK) {
	return TH8_ERROR;
    }
    zNew = (char *)th8BufferAlloc(interp, nNew + 1);
    if (!zNew) {
	/* Growth failed: leave *pzStr / *pnStr unmodified (last good
	 * state) and report the error.  Callers must check this return
	 * (see the TH8_STR_APPEND macro) and fail closed instead of
	 * publishing a truncated string (Bug 61). */
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (*pzStr) {
	Th8_Memcpy(interp, zNew, *pzStr, TH8_LEN(*pnStr));
    }
    Th8_Memcpy(interp, &zNew[TH8_LEN(*pnStr)], zApp, nApp);
    zNew[nNew] = 0;
    th8BufferFree(interp, *pzStr, TH8_LEN(*pnStr) + 1);
    *pzStr = zNew;
    *pnStr = nNew | nTag;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Phase callbacks --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_SetPolicyCallback --
 *
 *	Register or remove the unified policy callback.  The
 *	callback is invoked with phase bitmasks combining timing
 *	(TH8_PHASE_PRE/POST) and operation (TH8_PHASE_EVAL/READ).
 *
 * Why / How:
 *	The policy callback is the single interception point for
 *	the embedding host to approve or deny script operations.
 *	Passing NULL for xProc disables policy enforcement without
 *	disturbing the signed-only gate.
 * Why / How:
 *	The debug callback fires at each command boundary.  Step
 *	mode (into/over/out) is checked first using the current
 *	frame depth vs. the saved step depth.  If no step event
 *	triggers, the breakpoint hash table is probed with a
 *	composite key of (script name + 4-byte line number).  If
 *	the callback returns TH8_BREAK, the interpreter is frozen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the interpreter's policy hook.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetPolicyCallback(
    Th8_Interp *interp, /* Interpreter. */
    Th8_PolicyProc xProc, /* Callback (NULL to remove). */
    void *pCtx) /* Context for callback. */
{
    if (!interp) return;
    interp->xPolicyCb = xProc;
    interp->pPolicyCbCtx = pCtx;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetPolicyCallback --
 *
 *	Retrieve the current policy callback and context.
 *
 * Why / How:
 *	Lets the embedding host inspect the active policy hook
 *	without reaching into interpreter internals.  Either
 *	output pointer may be NULL if the caller does not need
 *	that field.
 *
 *----------------------------------------------------------------------
 */

void
Th8_GetPolicyCallback(
    Th8_Interp *interp,
    Th8_PolicyProc *pxProc,
    void **ppCtx)
{
    if (!interp) return;
    if (pxProc) *pxProc = interp->xPolicyCb;
    if (ppCtx) *ppCtx = interp->pPolicyCbCtx;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_CancelEval --
 *
 *	Request cancellation of the running script.  Thread-safe.
 *
 * Why / How:
 *	The bCanceled flag is set via atomic CAS so that any thread
 *	(including signal handlers when TH8_CANCEL_SIGNAL is used)
 *	can request cancellation safely.  A memory barrier follows
 *	so the evaluator's Th8_Ready check sees the flag promptly.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Sets the bCanceled flag.
 *
 *----------------------------------------------------------------------
 */

int
Th8_CancelEval(
    Th8_Interp *interp, /* Interpreter. */
    const char *zMsg, /* Error message (may be NULL). */
    size_t nMsg, /* Message length (TH8_NOLEN ok). */
    int flags) /* TH8_CANCEL_UNWIND, TH8_CANCEL_SIGNAL,
				 * or both (OR'd together). */
{
    int bSignal = (flags & TH8_CANCEL_SIGNAL);
    if (!interp) return TH8_ERROR;
    interp->cancelFlags = flags;
    if (zMsg) {
	if (nMsg == TH8_NOLEN) {
	    nMsg = Th8_Strlen(interp, zMsg);
	}

	/*
	 * Two modes depending on TH8_CANCEL_SIGNAL:
	 *
	 *   With TH8_CANCEL_SIGNAL: the zMsg pointer is stored directly
	 *   (not copied).  No memory allocation occurs.  This mode is
	 *   async-signal-safe and MUST be used from signal handlers.
	 *
	 *   Without TH8_CANCEL_SIGNAL (default): zMsg is copied into a
	 *   Th8_Malloc'd buffer owned by the interpreter.  This mode is
	 *   NOT signal-safe but allows dynamic messages.
	 */

	if (bSignal) {
	    /*
	     * Static message: store pointer directly.
	     * Safe to call from signal handlers.
	     */

	    if (!interp->zSavedCancelMsg && interp->bCancelMsgOwned &&
	        interp->zCancelMsg) {
		interp->zSavedCancelMsg = interp->zCancelMsg;
		interp->zCancelMsg = 0;
		interp->nCancelMsg = 0;
		interp->bCancelMsgOwned = 0;
	    }

	    if (!interp->zCancelMsg) {
		interp->zCancelMsg = (char *)zMsg;
		interp->nCancelMsg = nMsg;
		interp->bCancelMsgOwned = 0;
	    }
	} else {
	    /*
	     * Dynamic message: copy into owned buffer.
	     * NOT async-signal-safe.
	     */

	    if (interp->zSavedCancelMsg) {
		Th8_Free(interp, interp->zSavedCancelMsg);
		interp->zSavedCancelMsg = 0;
	    }

	    /* State invariant: bCancelMsgOwned=1 only when
	     * zCancelMsg is a valid owned buffer; the two flags
	     * move together in every other Cancel-related path,
	     * so `zCancelMsg` is ALWAYS non-NULL whenever
	     * `bCancelMsgOwned` is set. */
	    if (interp->bCancelMsgOwned && ALWAYS(interp->zCancelMsg)) {
		Th8_Free(interp, interp->zCancelMsg);
		interp->zCancelMsg = 0;
		interp->nCancelMsg = 0;
		interp->bCancelMsgOwned = 0;
	    }

	    interp->zCancelMsg = (char *)TH8_ALLOC_STR(interp, nMsg);
	    if (interp->zCancelMsg) {
		Th8_Memcpy(interp, interp->zCancelMsg, zMsg, nMsg);
		interp->zCancelMsg[nMsg] = 0;
		interp->nCancelMsg = nMsg;
		interp->bCancelMsgOwned = 1;
	    }
	}
    }
    Th8_IntCmpXchg(interp, &interp->bCanceled, 1, 0);
    th8MemBarrier(interp);

    if (!bSignal) th8SignalAllStates(interp);

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Freeze --
 *
 *	Suspend the interpreter at the next command boundary.  The
 *	NRE callback chain, frame stack, variables, and result are
 *	preserved on the heap.  The interpreter can be resumed later
 *	with Th8_Thaw.
 *
 *	Like Th8_CancelEval, this is safe to call from any thread.
 *
 * Why / How:
 *	Uses an atomic CAS to set bSuspended, followed by a memory
 *	barrier, making the request visible to the evaluator's
 *	Th8_Ready check on any thread.  No lock is needed because
 *	the flag is a single-word atomic.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Sets the bSuspended flag.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Freeze(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;
    Th8_IntCmpXchg(interp, &interp->bSuspended, 1, 0);
    th8MemBarrier(interp);
    th8SignalAllStates(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ThreadInit --
 *
 *	Register the calling thread with TH8's allocator.  Worker
 *	threads that will call a thread-safe TH8 API which
 *	allocates (notably Th8_QueueEvent) MUST call this once
 *	at thread entry on builds whose allocator requires
 *	per-thread initialisation.  The contract is uniform
 *	across builds: a no-op on configurations that don't
 *	need it (so embedder code stays portable).
 *
 *	On TH8_USE_MIMALLOC builds, dispatches to mi_thread_init.
 *	mimalloc auto-initialises threads lazily on first call in
 *	release builds, but DEBUG builds assert that the per-thread
 *	state is set up before the first allocation; this routine
 *	is what guarantees that.
 *
 * Side effects:
 *	On mimalloc builds, sets up the calling thread's mimalloc
 *	heap state.  No-op otherwise.
 *
 *----------------------------------------------------------------------
 */

void
Th8_ThreadInit(void)
{
#if defined(TH8_USE_MIMALLOC)
    mi_thread_init(); /* per-thread allocator state */
    th8MiHeapInit(); /* per-thread dedicated heap */
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ThreadDone --
 *
 *	Unregister the calling thread from TH8's allocator.
 *	Must pair with a prior Th8_ThreadInit call on the same
 *	thread.  Idempotent.  Optional in the sense that the
 *	allocator backend will eventually clean up via thread-
 *	exit hooks; calling Th8_ThreadDone explicitly returns
 *	memory to the allocator's free pools earlier.
 *
 *	On mimalloc builds, the per-thread dedicated heap is torn
 *	down via mi_heap_delete (NOT mi_heap_destroy): blocks
 *	already allocated remain valid until they're freed by
 *	whichever thread eventually frees them.  This is the
 *	property that makes the per-thread heap safe for the
 *	cross-thread allocate-here / free-there pattern used by
 *	the event queue (Th8_QueueEvent producer thread allocates
 *	overflow nodes; main thread frees them on drain).
 *
 *----------------------------------------------------------------------
 */

void
Th8_ThreadDone(void)
{
#if defined(TH8_USE_MIMALLOC)
    th8MiHeapDone(); /* lazy heap teardown */
    mi_thread_done(); /* per-thread allocator state */
#endif
}


/* Forward declaration (defined below). */
static int th8RunCallbacks(Th8_Interp *, Th8_Callback *, int);

/*
 *----------------------------------------------------------------------
 *
 * Th8_Thaw --
 *
 *	Resume a frozen interpreter.  Clears the suspended flag so
 *	subsequent Th8_Ready calls return TH8_OK.  If NRE callbacks
 *	were preserved during the freeze, the caller may re-enter
 *	the NRE trampoline via th8EvalTrampoline to resume them.
 *
 *	This function intentionally does NOT call th8EvalTrampoline
 *	itself, because the trampoline drains ALL pending callbacks
 *	including those belonging to outer eval frames.  The caller
 *	is responsible for re-entering the trampoline at the correct
 *	level (e.g., via th8EvalTrampoline from the outermost eval
 *	boundary).
 *
 * Why / How:
 *	Clears the bSuspended flag via atomic CAS, then re-attaches
 *	any suspended NRE callbacks and drains them through
 *	th8RunCallbacks to complete the interrupted evaluation.
 *	The outer callback chain and frame are saved and restored
 *	so the thaw is transparent to the calling context.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Clears the bSuspended flag.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Thaw(Th8_Interp *interp) /* Interpreter. */
{
    int rc = TH8_OK;

    if (!interp) return TH8_ERROR;
    Th8_IntCmpXchg(interp, &interp->bSuspended, 0, 1);
    th8MemBarrier(interp);

    /*
     * If Th8_Eval detached suspended callbacks during the freeze,
     * re-attach them and drain them now to complete the interrupted
     * evaluation.
     */

    if (interp->pSuspendedCallbacks) {
	/*
	 * Re-attach the suspended callbacks and drain them.
	 * The chain is self-contained (ends with NULL) so we
	 * drain to NULL.  The eval iteration will process the
	 * remaining commands, and th8EvalStateCleanup at the bottom
	 * will decrement nEvalDepth and free the EvalState.
	 *
	 * The frame is already correct (Th8_Eval skipped
	 * frame restoration on TH8_SUSPEND).  After draining,
	 * we restore the saved outer frame.
	 */

	Th8_Callback *pSaved = interp->pCallbacks;

	interp->pCallbacks = interp->pSuspendedCallbacks;
	interp->pSuspendedCallbacks = 0;
	rc = th8RunCallbacks(interp, 0, TH8_OK);

	/*
	 * Restore the outer callback chain (should be
	 * unchanged since nothing was pushed onto it
	 * during the drain).
	 */

	interp->pCallbacks = pSaved;

	/*
	 * Restore the outer frame that Th8_Eval saved
	 * before the freeze.
	 */

	if (interp->pSavedFrame) {
	    interp->pFrame = interp->pSavedFrame;
	    interp->pSavedFrame = 0;
	}
    }

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IsSuspended --
 *
 *	Return non-zero if the interpreter has a pending suspension.
 *
 * Why / How:
 *	Uses a CAS-read (compare 0 with 0) preceded by a memory
 *	barrier to read the bSuspended flag without modifying it.
 *	The barrier ensures visibility of writes from the thread
 *	that called Th8_Freeze.
 *
 * Results:
 *	1 if suspended, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsSuspended(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;
    th8MemBarrier(interp);
    return Th8_IntCmpXchg(interp, &interp->bSuspended, 0, 0) != 0;
}


/*
 *======================================================================
 *
 * Script Debugging.
 *
 *======================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * th8DebugCheck --
 *
 *	Called from Th8_Ready when a debug callback is installed.
 *	Determines whether to fire the callback based on step mode
 *	and breakpoint table.
 *
 * Why / How:
 *	The debug callback fires at each command boundary.  Step
 *	mode (into/over/out) is checked first using the current
 *	frame depth vs. the saved step depth.  If no step event
 *	triggers, the breakpoint hash table is probed with a
 *	composite key of (script name + 4-byte line number).  If
 *	the callback returns TH8_BREAK, the interpreter is frozen.
 *
 *----------------------------------------------------------------------
 */

static int
th8DebugCheck(Th8_Interp *interp)
{
    int nLine = Th8_GetLine(interp);
    int nDepth = th8GetFrameLevel(interp);
    const char *zScript;
    size_t nScript = 0;
    int event = 0;

    zScript = Th8_GetSourceName(interp, &nScript);

    /*
     * Check step mode.
     */

    switch (interp->nStepMode) {
    case TH8_STEP_INTO:
	event = TH8_DEBUG_STEP;
	break;
    case TH8_STEP_OVER:
	if (nDepth <= interp->nStepDepth) {
	    event = TH8_DEBUG_STEP;
	}
	break;
    case TH8_STEP_OUT:
	if (nDepth < interp->nStepDepth) {
	    event = TH8_DEBUG_STEP;
	}
	break;
    }

    /*
     * Check breakpoint table (if no step event already pending).
     */

    if (!event && interp->paBreakpoints && ALWAYS(zScript) &&
        ALWAYS(nLine > 0)) {
	/*
	 * Build a key from script name + NUL + line number bytes.
	 */

	char zKey[512];
	size_t nKey;

	if (nScript < sizeof(zKey) - 5) {
	    Th8_Memcpy(interp, zKey, zScript, nScript);
	    zKey[nScript] = '\0';
	    zKey[nScript + 1] = (char)(nLine & 0xff);
	    zKey[nScript + 2] = (char)((nLine >> 8) & 0xff);
	    zKey[nScript + 3] = (char)((nLine >> 16) & 0xff);
	    zKey[nScript + 4] = (char)((nLine >> 24) & 0xff);
	    nKey = nScript + 5;

	    if (Th8_HashFind(interp, interp->paBreakpoints, zKey, nKey, 0) !=
	        NULL) {
		event = TH8_DEBUG_BREAKPOINT;
	    }
	}
    }

    if (!event) return TH8_OK;

    /*
     * Fire the debug callback.
     */

    {
	int dbgRc = interp->xDebug(
	    interp, event, zScript, nScript, nLine, nDepth,
	    interp->pDebugCtx);

	if (dbgRc == TH8_BREAK) {
	    Th8_Freeze(interp);
	    return TH8_SUSPEND;
	}
	return dbgRc;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetDebugCallback --
 *
 *	Register (or clear) the debug callback invoked at each
 *	command boundary when debugging is active.
 *
 * Why / How:
 *	The debug callback is the integration point for external
 *	debuggers and IDEs.  It receives the current script,
 *	line number, and eval depth before each command executes.
 *	Setting xDebug to NULL disables debug callbacks.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if interp is NULL.
 *
 * Side effects:
 *	interp->xDebug and interp->pDebugCtx are updated.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetDebugCallback(Th8_Interp *interp, Th8_DebugProc xDebug, void *pCtx)
{
    if (!interp) return TH8_ERROR;
    interp->xDebug = xDebug;
    interp->pDebugCtx = pCtx;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetStepMode --
 *
 *	Set the debugger step mode (step-into, step-over, step-out,
 *	or none).  The step depth is captured at the time of the
 *	call so that step-over and step-out know when to trigger.
 *
 * Why / How:
 *	th8DebugCheck() reads nStepMode/nStepDepth to decide whether
 *	to fire the debug callback.  By capturing the frame level
 *	here, step-over only fires in the same or shallower frame,
 *	and step-out only fires in a shallower frame.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if interp is NULL.
 *
 * Side effects:
 *	interp->nStepMode and interp->nStepDepth are updated.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetStepMode(Th8_Interp *interp, int mode)
{
    if (!interp) return TH8_ERROR;
    interp->nStepMode = mode;
    interp->nStepDepth = th8GetFrameLevel(interp);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetStepMode --
 *
 *	Query the current debugger step mode.
 *
 * Why / How:
 *	Allows the embedding host to inspect the active step mode
 *	without reaching into interpreter internals.
 *
 * Results:
 *	The current step mode constant (TH8_STEP_NONE, _INTO,
 *	_OVER, or _OUT); TH8_STEP_NONE if interp is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetStepMode(Th8_Interp *interp)
{
    if (!interp) return TH8_STEP_NONE;
    return interp->nStepMode;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetBreakpoint --
 *
 *	Register a breakpoint at a specific script name and line
 *	number.  The breakpoint is stored in a hash table keyed
 *	by (scriptName + lineNumber) and assigned a unique ID.
 *
 * Why / How:
 *	th8DebugCheck() probes the breakpoint hash at each command
 *	boundary.  The composite key (script name bytes followed
 *	by a 4-byte little-endian line number) ensures breakpoints
 *	are script-specific.  IDs are monotonically increasing.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on invalid args or OOM.
 *	If pBreakpointId is non-NULL, *pBreakpointId receives the
 *	assigned ID.
 *
 * Side effects:
 *	A new entry is added to interp->paBreakpoints (hash table
 *	created lazily on first call).
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetBreakpoint(
    Th8_Interp *interp,
    const char *zScript,
    size_t nScript,
    int nLine,
    int *pBreakpointId)
{
    Th8_HashEntry *pEntry;
    char zKey[512];
    size_t nKey;
    int id;

    /* Defensive guards split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!zScript) return TH8_ERROR;
    if (nLine < 1) return TH8_ERROR;
    if (nScript >= sizeof(zKey) - 5) return TH8_ERROR;

    if (!interp->paBreakpoints) {
	interp->paBreakpoints = Th8_HashNew(interp);
	if (!interp->paBreakpoints) return TH8_ERROR;
    }

    Th8_Memcpy(interp, zKey, zScript, nScript);
    zKey[nScript] = '\0';
    zKey[nScript + 1] = (char)(nLine & 0xff);
    zKey[nScript + 2] = (char)((nLine >> 8) & 0xff);
    zKey[nScript + 3] = (char)((nLine >> 16) & 0xff);
    zKey[nScript + 4] = (char)((nLine >> 24) & 0xff);
    nKey = nScript + 5;

    pEntry = Th8_HashFind(interp, interp->paBreakpoints, zKey, nKey, 1);
    if (!pEntry) return TH8_ERROR;

    id = ++(interp->nNextBreakpointId);
    pEntry->pData = (void *)(size_t)id;

    if (pBreakpointId) *pBreakpointId = id;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ClearBreakpoint --
 *
 *	Remove a previously registered breakpoint by its unique ID.
 *
 * Why / How:
 *	Since breakpoints are keyed by (script+line), removal by ID
 *	requires a linear scan of the hash table.  This is acceptable
 *	because breakpoint counts are typically very small (< 100).
 *
 * Results:
 *	TH8_OK if the breakpoint was found and removed; TH8_ERROR
 *	if the ID is unknown or interp is NULL.
 *
 * Side effects:
 *	The matching hash entry is removed from
 *	interp->paBreakpoints.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ClearBreakpoint(Th8_Interp *interp, int breakpointId)
{
    if (!interp || !interp->paBreakpoints) return TH8_ERROR;

    /*
     * Iterate to find the entry with the matching ID, then
     * remove it.  This is O(N) but breakpoint counts are small.
     */

    {
	Th8_HashEntry *pFound = NULL;

	/* Manual iteration to find and remove. */
	int i;
	for (i = 0; i < 257 /* TH8_HASH_SIZE */; i++) {
	    Th8_HashEntry *p = interp->paBreakpoints->aBucket[i];
	    while (p) {
		if ((int)(size_t)p->pData == breakpointId) {
		    pFound = p;
		    break;
		}
		p = p->pNext;
	    }
	    if (pFound) break;
	}

	if (pFound) {
	    Th8_HashRemove(
	        interp, interp->paBreakpoints, pFound->zKey, pFound->nKey);
	    return TH8_OK;
	}
    }

    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ClearAllBreakpoints --
 *
 *	Remove all breakpoints from the interpreter at once.
 *
 * Why / How:
 *	Used when the debugger is detached or reset.  Destroying the
 *	entire hash table is O(N) and faster than removing entries
 *	one by one.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if interp is NULL.
 *
 * Side effects:
 *	interp->paBreakpoints is destroyed and set to NULL.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ClearAllBreakpoints(Th8_Interp *interp)
{
    if (!interp) return TH8_ERROR;

    if (interp->paBreakpoints) {
	Th8_HashDelete(interp, interp->paBreakpoints);
	interp->paBreakpoints = NULL;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetFrameCount --
 *
 *	Return the total number of call frames currently on the
 *	interpreter's frame stack, including the global frame.
 *
 * Why / How:
 *	Debuggers use this to determine the stack depth for
 *	rendering call-stack displays.  The count is the frame
 *	level plus one (level 0 = one frame).
 *
 * Results:
 *	The frame count (>= 1 during evaluation); 0 if interp
 *	is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetFrameCount(Th8_Interp *interp)
{
    if (!interp) return 0;
    return th8GetFrameLevel(interp) + 1;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetFrameInfo --
 *
 *	Retrieve diagnostic information about a specific call frame,
 *	identified by a zero-based index from the top of the stack
 *	(0 = topmost frame).
 *
 * Why / How:
 *	Debuggers need to display the procedure name, source file,
 *	and line number for each frame in a backtrace.  The index
 *	is converted to an absolute level, and the frame's objv
 *	(argument vector) is queried for the procedure name.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if interp is NULL or the
 *	frameIndex is out of range.
 *
 * Side effects:
 *	Output parameters (*pzProc, *pzScript, *pnLine) are
 *	filled if non-NULL.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetFrameInfo(
    Th8_Interp *interp,
    int frameIndex,
    const char **pzProc,
    size_t *pnProc,
    const char **pzScript,
    size_t *pnScript,
    int *pnLine)
{
    int nLevel;
    int targetLevel;
    int argc = 0;
    const char **argv = NULL;
    size_t *argl = NULL;

    if (!interp) return TH8_ERROR;

    nLevel = th8GetFrameLevel(interp);
    targetLevel = nLevel - frameIndex;
    if (targetLevel < 0) return TH8_ERROR;

    if (pzProc) {
	/* Compound condition split per Finding 005 sec. 5b.  C1-Pair
	 * (GetFrameObjv result) was the only drivable pair from
	 * script; argc/argv/argv[0] conditions are intrinsic-dead
	 * because Th8_GetFrameObjv returns TH8_OK only when all
	 * three are valid (post-its-own-split sweep).  Sequential
	 * single-condition guards remove the dead C-pairs without
	 * changing runtime semantics. */
	int gotFrame = 0;

	if (Th8_GetFrameObjv(interp, targetLevel, &argc, &argv, &argl) ==
	    TH8_OK) {
	    if (argc > 0) {
		if (argv) {
		    if (argv[0]) gotFrame = 1;
		}
	    }
	}
	if (gotFrame) {
	    *pzProc = argv[0];
	    if (pnProc) *pnProc = argl[0];
	} else {
	    *pzProc = "";
	    if (pnProc) *pnProc = 0;
	}
    }

    if (pzScript) {
	*pzScript = Th8_GetSourceName(interp, pnScript);
    }

    if (pnLine) {
	*pnLine = Th8_GetLine(interp);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EvalAtFrame --
 *
 *	Evaluate a script in the context of a specific call frame,
 *	identified by a zero-based index from the top of the stack.
 *	This is the debugger's equivalent of [uplevel].
 *
 * Why / How:
 *	Debuggers need to evaluate watch expressions and ad-hoc
 *	commands in arbitrary stack frames.  The frameIndex is
 *	translated to an absolute level, then Th8_Eval is called
 *	with that level.
 *
 * Results:
 *	A TH8 return code.
 *
 * Side effects:
 *	The script is evaluated; side effects depend on the script
 *	content.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EvalAtFrame(
    Th8_Interp *interp,
    int frameIndex,
    const char *zScript,
    size_t nScript)
{
    int nLevel;
    int targetLevel;

    if (!interp) return TH8_ERROR;

    nLevel = th8GetFrameLevel(interp);
    targetLevel = nLevel - frameIndex;
    if (targetLevel < 0) return TH8_ERROR;

    return Th8_Eval(interp, targetLevel, zScript, nScript, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Coroutine implementation.
 *
 *----------------------------------------------------------------------
 */

/*
 * coro_delete_proc --
 *
 *	Command delete callback: frees the coroutine state and
 *	any saved NRE callbacks.
 */

static void
coro_delete_proc(Th8_Interp *interp, void *pCtx)
{
    Th8_CoroState *pCoro = (Th8_CoroState *)pCtx;

    /* Bug 26 family: plain pCoro guard.  This is the command
     * delete callback; the framework should always pass the
     * coroutine context pointer it was registered with, but
     * defending against a NULL ctx is cheap and matches the
     * Bug 31 pattern. */
    if (!pCoro) return;

    /*
     * Drain saved NRE callbacks by restoring the coroutine's
     * execution context and running the callbacks with TH8_ERROR.
     * This causes each callback to execute its cleanup logic:
     *
     *   - th8EvalPostCmd frees argv
     *   - th8EvalStateCleanup frees Th8_EvalState
     *   - th8NRCmdDispatch frees Th8_CmdBuild
     *   - proc frame cleanup pops and frees the call frame
     *     (including the variable hash table)
     *
     * Simply freeing the callback nodes (as we did before) leaks
     * all the resources held in the pData[] slots.
     */

    if (pCoro->ctx.pCallbacks) {
	Th8_ExecCtx outerCtx;

	th8SaveExecCtx(interp, &outerCtx);
	th8RestoreExecCtx(interp, &pCoro->ctx);
	pCoro->ctx.pCallbacks = 0;

	/*
	 * Drain with TH8_CLEANUP, a dedicated return code
	 * for coroutine teardown.  Like TH8_BREAK, it:
	 *   - frees argv in th8EvalPostCmd
	 *   - frees EvalState in th8EvalStateCleanup
	 *   - pops/frees frame in th8FrameCleanup
	 *   - does NOT build error traces (avoids reading
	 *     pState->zFirst, which may point into a freed
	 *     ProcDefn if the proc was deleted before the
	 *     coroutine)
	 *   - does NOT push next iterations
	 */

	th8RunCallbacks(interp, 0, TH8_CLEANUP);

	th8RestoreExecCtx(interp, &outerCtx);
    }

    /* Free any suspended callbacks as well. */
    if (pCoro->ctx.pSuspendedCallbacks) {
	Th8_ExecCtx outerCtx;

	th8SaveExecCtx(interp, &outerCtx);
	interp->pCallbacks = pCoro->ctx.pSuspendedCallbacks;
	pCoro->ctx.pSuspendedCallbacks = 0;

	th8RunCallbacks(interp, 0, TH8_CLEANUP);

	th8RestoreExecCtx(interp, &outerCtx);
    }

    Th8_Free(interp, pCoro->zBody);
    Th8_Free(interp, pCoro->zYieldValue);
    Th8_Free(interp, pCoro->zResumeValue);
    Th8_Free(interp, pCoro->zName);
    Th8_Free(interp, pCoro);
}


/*
 * coro_resume_command --
 *
 *	The dynamically created command that resumes a coroutine.
 */

static int
coro_resume_command(
    Th8_Interp *interp,
    void *ctx,
    int argc,
    const char **argv,
    size_t *argl)
{
    Th8_CoroState *pCoro = (Th8_CoroState *)ctx;
    int rc;

    (void)argv;
    (void)argl;

    if (argc > 2) {
	return Th8_WrongNumArgs(interp, "coroutineName ?value?");
    }
    /* Bug 26 family: plain pCoro guard. */
    if (!pCoro || pCoro->bDone) {
	Th8_SetResult(interp, "invalid coroutine", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (!pCoro->ctx.pCallbacks) {
	Th8_SetResult(interp, "coroutine not suspended", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Store the resume value.
     */

    Th8_Free(interp, pCoro->zResumeValue);
    if (argc == 2) {
	pCoro->zResumeValue = (char *)TH8_ALLOC_STR(interp, argl[1]);
	if (pCoro->zResumeValue) {
	    Th8_Memcpy(interp, pCoro->zResumeValue, argv[1], argl[1]);
	    pCoro->zResumeValue[argl[1]] = 0;
	}
	pCoro->nResumeValue = argl[1];
    } else {
	pCoro->zResumeValue = 0;
	pCoro->nResumeValue = 0;
    }

    /*
     * Resume via full context switch.
     *
     * Save the caller's entire execution context, restore
     * the coroutine's context, drain the coroutine's
     * callbacks, then switch back.
     */

    {
	Th8_ExecCtx outerCtx;
	/* Bug 69: pYieldingCoro is a per-activation prompt, not a global
	 * slot.  If this resume happens from within another coroutine's
	 * body (nested coroutines), save that outer coroutine's prompt
	 * and RESTORE it below instead of clearing to 0 -- otherwise the
	 * outer coroutine can no longer [yield] after this inner resume
	 * returns ("yield can only be called inside a coroutine"). */
	Th8_CoroState *pOuterYielding = interp->pYieldingCoro;

	th8SaveExecCtx(interp, &outerCtx);
	th8RestoreExecCtx(interp, &pCoro->ctx);
	pCoro->ctx.pCallbacks = 0; /* consumed */

	Th8_SetResult(
	    interp, pCoro->zResumeValue ? pCoro->zResumeValue : "",
	    pCoro->nResumeValue);

	interp->pYieldingCoro = pCoro;
	rc = th8RunCallbacks(interp, 0, TH8_OK);

	if (rc == TH8_YIELD) {
	    /*
	     * The body yielded again.  Combine the suspended
	     * callbacks (from the inner Th8_Eval) with the
	     * remaining drain callbacks into one chain.
	     */

	    if (interp->pSuspendedCallbacks) {
		/* Chain: suspended (top) ==> remaining (bottom) */
		Th8_Callback *pTail = interp->pSuspendedCallbacks;

		while (pTail->pNext) {
		    pTail = pTail->pNext;
		}
		pTail->pNext = interp->pCallbacks;
		interp->pCallbacks = interp->pSuspendedCallbacks;
		interp->pSuspendedCallbacks = 0;
	    }
	    th8SaveExecCtx(interp, &pCoro->ctx);
	    interp
	        ->pYieldingCoro = pOuterYielding; /* Bug 69: restore outer */
	    Th8_IntCmpXchg(interp, &interp->bSuspended, 0, 1);
	} else {
	    interp
	        ->pYieldingCoro = pOuterYielding; /* Bug 69: restore outer */
	}

	/* Restore caller's context, preserving the result */
	{
	    size_t nRes;
	    const char *zRes = Th8_GetResult(interp, &nRes);
	    char *zCopy = 0;
	    int savedRc = rc;

	    /* Copy result before context switch.  Th8_GetResult
	     * returns the empty-string pointer (never NULL) when
	     * no result was set.
	     *
	     * Bug 26 (2026-06-07): plain check rather than ALWAYS --
	     * under TH8_OMIT collapse, a NULL zRes paired with
	     * nRes>0 (refactor/platform bug) would Memcpy from
	     * NULL.  Short-circuit is safer. */
	    if (zRes != NULL && nRes > 0) {
		zCopy = (char *)TH8_ALLOC_STR(interp, nRes);
		if (zCopy) {
		    Th8_Memcpy(interp, zCopy, zRes, nRes);
		    zCopy[nRes] = 0;
		}
	    }
	    th8RestoreExecCtx(interp, &outerCtx);
	    Th8_SetResult(interp, zCopy ? zCopy : "", zCopy ? nRes : 0);
	    Th8_Free(interp, zCopy);
	    rc = savedRc;
	}
    }

    if (rc == TH8_YIELD && ALWAYS(pCoro->ctx.pCallbacks)) {

	Th8_SetResult(
	    interp, pCoro->zYieldValue ? pCoro->zYieldValue : "",
	    pCoro->nYieldValue);
	rc = TH8_OK;
    } else {
	pCoro->bDone = 1;
	/* Bug 69: pYieldingCoro was already restored to the outer
	 * coroutine's prompt above; do not clear it here. */

	/*
	 * Per Tcl 8.6: "Once command returns normally or with an
	 * exception the coroutine context name is deleted."
	 *
	 * Th8_RenameCommand with empty name removes the command
	 * from the hash immediately but defers the xDel callback
	 * (coro_delete_proc) via the pending-delete queue.  This
	 * is safe because pCoro is not accessed after this point.
	 */
	if (pCoro->zName) {
	    Th8_RenameCommand(interp, pCoro->zName, pCoro->nName, "", 0);
	}
    }

    return rc;
}


/*
 * Th8_CoroYield --
 *
 *	Suspend the current coroutine, returning zValue to the caller.
 */

int
Th8_CoroYield(Th8_Interp *interp, const char *zValue, size_t nValue)
{
    Th8_CoroState *pCoro;

    if (!interp) return TH8_ERROR;
    pCoro = interp->pYieldingCoro;

    if (!pCoro) {
	Th8_SetResult(
	    interp, "yield can only be called inside a coroutine", TH8_NOLEN);
	return TH8_ERROR;
    }

    Th8_Free(interp, pCoro->zYieldValue);
    if (zValue && nValue > 0) {
	pCoro->zYieldValue = (char *)TH8_ALLOC_STR(interp, nValue);
	if (pCoro->zYieldValue) {
	    Th8_Memcpy(interp, pCoro->zYieldValue, zValue, nValue);
	    pCoro->zYieldValue[nValue] = 0;
	}
	pCoro->nYieldValue = nValue;
    } else {
	pCoro->zYieldValue = 0;
	pCoro->nYieldValue = 0;
    }

    /*
     * Return TH8_YIELD.  This propagates synchronously through
     * the NRE chain and eval loop - no global bSuspended flag,
     * no Th8_Ready check.  The eval loop treats TH8_YIELD like
     * TH8_ERROR (stops processing commands), and Th8_Eval
     * returns it to the caller.  The coroutine handler in
     * Th8_CoroCreate / coro_resume_command checks for TH8_YIELD
     * and saves the context.
     */

    return TH8_YIELD;
}


/*
 * Th8_CoroCreate --
 *
 *	Create a coroutine named zName, evaluating zBody.
 */

int
Th8_CoroCreate(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    const char *zBody,
    size_t nBody)
{
    Th8_CoroState *pCoro;
    Th8_CoroState *pOuterYielding; /* Bug 69: saved outer prompt */
    Th8_CommandProc xExisting = NULL;
    void *pExistingCtx = NULL;
    int rc;

    if (!interp) return TH8_ERROR;

    /* R-34122-15052: refuse to create the coroutine if a command
     * with the requested name already exists.  Match reference
     * Tcl behaviour rather than silently overwriting. */
    if (Th8_GetCommandInfo(interp, zName, nName, &xExisting, &pExistingCtx) ==
        TH8_OK) {
	Th8_ErrorMessage(
	    interp,
	    "can't create coroutine: command already exists with name \"",
	    zName, nName);
	return TH8_ERROR;
    }
    /* Th8_GetCommandInfo sets an error result on not-found; clear it. */
    Th8_SetResultStatic(interp, "", 0);

    pCoro = (Th8_CoroState *)TH8_ALLOC(interp, sizeof(Th8_CoroState));
    if (!pCoro) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    pCoro->zName = (char *)TH8_ALLOC_STR(interp, nName);
    if (!pCoro->zName) {
	Th8_Free(interp, pCoro);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, pCoro->zName, zName, nName);
    pCoro->zName[nName] = 0;
    pCoro->nName = nName;

    /* Register the coroutine resume command */
    Th8_CreateCommand(
        interp, pCoro->zName, coro_resume_command, pCoro, coro_delete_proc,
        NULL);

    /*
     * Make a copy of the body that lives as long as the
     * coroutine.  The EvalState stores a pointer (not a copy)
     * of the script text, so the original must stay alive
     * until the coroutine's callbacks are fully drained.
     */

    pCoro->zBody = (char *)TH8_ALLOC_STR(interp, nBody);
    if (!pCoro->zBody) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Memcpy(interp, pCoro->zBody, zBody, nBody);
    pCoro->zBody[nBody] = 0;

    /* Bug 69: nested coroutines -- if this first resume runs from
     * within another coroutine's body, save that outer coroutine's
     * prompt and restore it below (see coro_resume_command). */
    pOuterYielding = interp->pYieldingCoro;
    interp->pYieldingCoro = pCoro;

    /* Evaluate the body (using the coroutine's copy) */
    rc = Th8_Eval(interp, 0, pCoro->zBody, nBody, NULL, 0);

    if (rc == TH8_YIELD) {
	/*
	 * Body yielded.  Save the entire interpreter context
	 * (including the suspended callbacks and saved frame
	 * from Th8_Eval) into the coroutine.
	 */

	th8SaveExecCtx(interp, &pCoro->ctx);

	/*
	 * The suspended callbacks are in pSuspendedCallbacks
	 * (detached by Th8_Eval).  Move them to the coroutine's
	 * main callback chain for later resume.
	 */

	pCoro->ctx.pCallbacks = pCoro->ctx.pSuspendedCallbacks;
	pCoro->ctx.pSuspendedCallbacks = 0;

	interp->pYieldingCoro = pOuterYielding; /* Bug 69: restore outer */
	Th8_IntCmpXchg(interp, &interp->bSuspended, 0, 1);

	/*
	 * Restore the interpreter to its pre-Th8_Eval state.
	 * Th8_Eval saved pSavedFrame = the pre-eval frame, and
	 * skipped restoring pFrame.  Use pSavedFrame to restore.
	 */

	interp->pFrame = interp->pSavedFrame;
	interp->pSavedFrame = 0;
	interp->pSuspendedCallbacks = 0;

	/*
	 * Th8_Eval incremented nEvalDepth but th8EvalStateCleanup
	 * (which decrements it) is in the coroutine's saved
	 * chain.  Decrement manually to balance.
	 */

	interp->nEvalDepth--;

	Th8_SetResult(
	    interp, pCoro->zYieldValue ? pCoro->zYieldValue : "",
	    pCoro->nYieldValue);
	rc = TH8_OK;
    } else {
	/* Body finished without yielding */
	interp->pYieldingCoro = pOuterYielding; /* Bug 69: restore outer */
	pCoro->bDone = 1;
    }

    return rc;
}


static int th8CheckCancel(Th8_Interp *interp); /* forward */

/*
 *----------------------------------------------------------------------
 *
 * Th8_IsCanceled --
 *
 *	Check if the interpreter has a pending cancellation.
 *
 * Why / How:
 *	Reads the bCanceled flag via a memory barrier for cross-
 *	thread visibility, then sets the interpreter result to
 *	the cancel message (or a default).  Returns TH8_ERROR
 *	so callers can write: return Th8_IsCanceled(interp, 0).
 *
 * Results:
 *	TH8_OK if not canceled, TH8_ERROR if canceled.
 *
 * Side effects:
 *	Sets the interpreter result to the cancel message.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsCanceled(
    Th8_Interp *interp, /* Interpreter. */
    int flags) /* Reserved for future use. */
{
    if (!interp) return TH8_ERROR;
    th8MemBarrier(interp);
    if (th8CheckCancel(interp)) {
	const char *zMsg = interp->zCancelMsg;
	size_t nMsg = interp->nCancelMsg;

	if (!zMsg) {
	    zMsg = "eval canceled";
	    nMsg = 13;
	}
	Th8_SetResult(interp, zMsg, nMsg);
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IsBeingUnwound --
 *
 *	Check whether the active cancellation has the unwind flag
 *	set.  When unwinding, catch must NOT intercept the error.
 *
 * Why / How:
 *	Reads the cancel flag via a memory barrier, then checks
 *	the TH8_CANCEL_UNWIND bit in cancelFlags.  When both are
 *	set, [catch] must NOT intercept the error, allowing the
 *	cancellation to propagate to the outermost eval.
 *
 * Results:
 *	Non-zero if cancel is active AND TH8_CANCEL_UNWIND is set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsBeingUnwound(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return TH8_ERROR;
    th8MemBarrier(interp);
    return th8CheckCancel(interp) &&
           (interp->cancelFlags & TH8_CANCEL_UNWIND);
}


static void th8ClearCancel(Th8_Interp *interp); /* forward */

/*
 *----------------------------------------------------------------------
 *
 * Th8_ResetCancel --
 *
 *	Clear the interpreter's cancellation state so that execution
 *	can continue.  Called by [catch] when it intercepts a
 *	non-unwind cancellation.
 *
 * Why / How:
 *	Delegates to th8ClearCancel which zeroes all cancel-related
 *	flags and frees the cancel message.  This is the public API
 *	used by embedders (e.g., LadyBird's event loop) and by the
 *	debugger resume path.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears all cancellation flags and frees interp->zCancelMsg.
 *
 *----------------------------------------------------------------------
 */

void
Th8_ResetCancel(Th8_Interp *interp)
{
    if (!interp) return;
    th8ClearCancel(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * th8SaveCancel --
 *
 *	Snapshot the interpreter's cancellation state into an opaque
 *	buffer and clear the cancel fields so that the [finally]
 *	block starts in a clean (non-canceled) state.
 *
 * Why / How:
 *	[try ... finally ...] must execute the finally block even
 *	when the try body was canceled.  The cancel state is saved
 *	(including the cancel message pointer -- NOT freed) and
 *	then cleared so the finally block can run without
 *	immediately re-triggering the cancel path.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	savedCancel (TH8_CANCEL_SAVE_SIZE bytes) is filled;
 *	the interpreter's cancel fields are zeroed.
 *
 *----------------------------------------------------------------------
 */

void
th8SaveCancel(Th8_Interp *interp, char savedCancel[TH8_CANCEL_SAVE_SIZE])
{
    Th8_CancelSave *pSave = (Th8_CancelSave *)savedCancel;

    pSave->bCanceled = interp->bCanceled;
    pSave->cancelFlags = interp->cancelFlags;
    pSave->bCancelMsgOwned = interp->bCancelMsgOwned;
    pSave->zCancelMsg = interp->zCancelMsg;
    pSave->nCancelMsg = interp->nCancelMsg;

    /*
     * Clear cancel state so the finally block is not
     * pre-canceled.  Do NOT free the cancel message -
     * it's saved for later restoration.
     */
    interp->bCanceled = 0;
    interp->cancelFlags = 0;
    interp->bCancelMsgOwned = 0;
    interp->zCancelMsg = 0;
    interp->nCancelMsg = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8RestoreCancel --
 *
 *	Restore the interpreter's cancellation state from a buffer
 *	previously filled by th8SaveCancel, unless the finally block
 *	triggered its own cancellation (which takes priority).
 *
 * Why / How:
 *	After the [finally] block completes, the original cancel
 *	state must be re-applied so that the error propagates to
 *	the caller.  However, if the finally block itself was
 *	canceled, that new cancellation is honored instead of
 *	overwriting it with the saved state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The interpreter's cancel fields are restored from
 *	savedCancel (unless a new cancel occurred during finally).
 *
 *----------------------------------------------------------------------
 */

void
th8RestoreCancel(
    Th8_Interp *interp,
    const char savedCancel[TH8_CANCEL_SAVE_SIZE])
{
    const Th8_CancelSave *pSave = (const Th8_CancelSave *)savedCancel;

    /*
     * If the finally block triggered its OWN cancellation,
     * honor it - don't overwrite with the saved state.
     */
    if (interp->bCanceled) return;

    /*
     * Restore the pre-finally cancel state.
     */
    interp->bCanceled = pSave->bCanceled;
    interp->cancelFlags = pSave->cancelFlags;
    interp->bCancelMsgOwned = pSave->bCancelMsgOwned;
    interp->zCancelMsg = pSave->zCancelMsg;
    interp->nCancelMsg = pSave->nCancelMsg;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetFinallyResult --
 *
 *	Retrieve the saved result string from the most recent
 *	[try] body, for use during [finally] block execution.
 *
 * Why / How:
 *	The [finally] implementation saves the try-body result
 *	before evaluating the finally block, then restores it
 *	afterward.  This accessor lets the finally handler read
 *	the saved result without exposing interpreter internals.
 *
 * Results:
 *	The saved result string (never NULL; empty string if none).
 *	If pn is non-NULL, *pn receives the byte length.
 *
 * Side effects:
 * Why / How:
 *	This is the core NRE engine.  Each iteration pops the top
 *	callback, invokes it, and threads the return code to the
 *	next.  Cancellation is checked between callbacks: with
 *	-unwind all remaining callbacks are discarded; without
 *	-unwind the cancel flag is consumed and callbacks (including
 *	[catch]) continue running.  Suspension and yield preserve
 *	the remaining chain for later resumption.
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
th8GetFinallyResult(Th8_Interp *interp, size_t *pn)
{
    if (pn) *pn = interp->nFinallyResult;
    return interp->zFinallyResult ? interp->zFinallyResult : "";
}

/*
 *----------------------------------------------------------------------
 *
 * th8GetFinallyRc --
 *
 *	Retrieve the saved return code from the most recent [try]
 *	body.
 *
 * Why / How:
 *	After the [finally] block executes, the [try] implementation
 *	needs to decide whether to re-raise the original try-body
 *	error.  This accessor provides the original return code.
 *
 * Results:
 *	The saved return code (TH8_OK, TH8_ERROR, etc.).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8GetFinallyRc(Th8_Interp *interp)
{
    return interp->nFinallyRc;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetFinallyState --
 *
 *	Save the try-body result string and return code before
 *	executing the [finally] block.  The previous saved result
 *	is freed.
 *
 * Why / How:
 *	[try ... finally ...] must preserve the try-body outcome
 *	across the finally block.  This function makes a private
 *	copy of the result via Th8_Strdup so the finally block can
 *	freely modify the interpreter result without losing the
 *	try-body's output.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->zFinallyResult is freed and replaced;
 *	interp->nFinallyResult and interp->nFinallyRc are updated.
 *
 *----------------------------------------------------------------------
 */

void
th8SetFinallyState(Th8_Interp *interp, const char *z, size_t n, int rc)
{
    Th8_Free(interp, interp->zFinallyResult);
    interp->zFinallyResult = Th8_Strdup(interp, z, n);
    interp->nFinallyResult = n;
    interp->nFinallyRc = rc;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetAllocBytes --
 *
 *	Directly set the interpreter's allocation byte counter.
 *
 * Why / How:
 *	Used during interpreter initialization and restoration to
 *	establish a known allocation baseline.  Normal allocation
 *	tracking is handled automatically by Th8_Malloc/Free/Realloc;
 * Why / How:
 *	This is the outermost trampoline entry point, called at the
 *	top level to drain all pending NRE callbacks.  It passes
 *	NULL as the bottom marker so th8RunCallbacks processes every
 *	callback in the chain.
 *	this function is for administrative reset only.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->nAllocBytes is set to n.
 *
 *----------------------------------------------------------------------
 */

void
th8SetAllocBytes(Th8_Interp *interp, size_t n)
{
    interp->nAllocBytes = n;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Exit --
 *
 *	Mark the interpreter as having exited.  The [exit] command
 *	calls this to set the sticky bExit flag, which causes all
 *	subsequent evaluation attempts to fail immediately.
 * Why / How:
 *	This is the trampoline-side entry point for NRE eval.
 *	It unpacks the script pointer and length from the callback's
 *	pData slots and calls th8EvalLocal to set up the per-command
 *	iteration callbacks.
 *
 * Why / How:
 *	Uses an atomic compare-and-swap (CAS) to set the flag,
 *	followed by a memory barrier so the flag is visible to
 *	other threads.  This makes cross-thread exit signaling
 *	safe without requiring a mutex.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->bExit is set to 1 (atomically).
 *
 *----------------------------------------------------------------------
 */

void
Th8_Exit(Th8_Interp *interp)
{
    if (!interp) return;
    Th8_IntCmpXchg(interp, &interp->bExit, 1, 0);
    th8MemBarrier(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_IsExited --
 *
 *	Check whether [exit] has been called on the interpreter.
 *	The bExit flag is sticky: once set, it remains set until
 *	the host calls Th8_ResetExit.
 *
 * Why / How:
 * Why / How:
 *	Instead of calling Th8_Eval (which blocks), NRE-converted
 *	commands push a th8NREvalCallback via this function.  The
 *	trampoline later invokes th8EvalLocal, which pushes its own
 *	iteration callbacks.  The caller's continuation (pushed
 *	before Th8_NREval) then sees the eval result.
 *	Uses an atomic CAS-read (compare 0 with 0) to read the
 *	flag without modifying it, preceded by a memory barrier
 *	to ensure visibility of writes from other threads.
 *
 * Results:
 *	Non-zero if the interpreter has been exited; zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_IsExited(Th8_Interp *interp)
{
    if (!interp) return TH8_ERROR;
    th8MemBarrier(interp);
    return Th8_IntCmpXchg(interp, &interp->bExit, 0, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ResetExit --
 *
 *	Clear the bExit flag so the interpreter can continue
 *	executing commands.  This is the ONLY way to clear the
 *	exit state.
 *
 * Why / How:
 *	Uses an atomic CAS to flip the flag from 1 back to 0.
 *	This is the only way to clear the exit state, ensuring
 *	the host explicitly opts in to continued execution.
 *
 *----------------------------------------------------------------------
 */

void
Th8_ResetExit(Th8_Interp *interp)
{
    if (!interp) return;
    Th8_IntCmpXchg(interp, &interp->bExit, 0, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * NRE trampoline --
 *
 *	Non-Recursive Evaluation (NRE) is the mechanism that allows
 *	TH8 to evaluate arbitrarily deep scripts without consuming
 *	unbounded C stack.  Instead of recursing in C, commands push
 *	continuation callbacks onto a LIFO chain.  The trampoline
 *	(th8RunCallbacks) iteratively pops and invokes them.
 *
 *	Key functions:
 *	  Th8_NRAddCallback -- push a continuation onto the chain.
 *	  th8RunCallbacks   -- drain callbacks down to a bottom marker.
 *	  Th8_NREval        -- schedule a script eval as a callback.
 *	  th8NRInFrame     -- push a new frame + work + cleanup.
 *	  th8EvalTrampoline -- drain all callbacks (outermost level).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_NRAddCallback --
 *
 *	Push a continuation callback onto the NRE chain.
 *
 * Results:
 * Why / How:
 *	Converts the frame number (absolute or relative) to a frame
 *	pointer by walking the caller chain, switches the interpreter
 *	to that frame, then uses the NRE LIFO ordering to ensure
 *	the restore callback (th8FrameRestoreCallback) runs after
 *	the eval completes.
 *	TH8_OK on success; TH8_ERROR on allocation failure.
 *
 * Side effects:
 *	A Th8_Callback node is allocated and prepended to the chain.
 *
 *----------------------------------------------------------------------
 */

int
Th8_NRAddCallback(
    Th8_Interp *interp, /* Interpreter. */
    Th8_CallbackProc xProc, /* Continuation function. */
    void *p0, /* Client data slot 0. */
    void *p1, /* Client data slot 1. */
    void *p2, /* Client data slot 2. */
    void *p3) /* Client data slot 3. */
{
    Th8_Callback *pCb;

    if (!interp) return TH8_ERROR;
    pCb = (Th8_Callback *)TH8_ALLOC(interp, sizeof(Th8_Callback));
    if (!pCb) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    pCb->xProc = xProc;
    pCb->pData[0] = p0;
    pCb->pData[1] = p1;
    pCb->pData[2] = p2;
    pCb->pData[3] = p3;
    pCb->pNext = interp->pCallbacks;
    interp->pCallbacks = pCb;
    return TH8_OK;
}


/*
 * Forward declaration: th8EvalLocal is used by th8NREvalCallback
 * but defined later in this file.
 */

static int
th8EvalLocal(Th8_Interp *, const char *, size_t, const char *, size_t, int);


/*
 *----------------------------------------------------------------------
 *
 * th8RunCallbacks --
 *
 *	Run the NRE trampoline until the callback chain reaches
 *	the given bottom marker (or NULL to drain all).  This is
 *	the core engine that drives NRE continuations.
 *
 *	THE BOTTOM-MARKER PATTERN:
 *
 *	Callers that need blocking semantics (e.g., Th8_Eval) save
 *	the current pCallbacks head as "pBottom" before calling
 *	th8EvalLocal, which pushes NRE callbacks.  After th8EvalLocal
 *	returns, the caller calls th8RunCallbacks(interp, pBottom,
 *	rc) to drain ONLY the callbacks that were pushed during that
 *	single command invocation.  This prevents one command's
 *	callbacks from leaking into the next command's execution.
 *
 *	When pBottom is NULL (as in th8EvalTrampoline), all pending
 *	callbacks are drained -- this is used at the outermost level.
 *
 *	LIFO ORDERING AND PUSH SEQUENCE:
 *
 *	Because the chain is LIFO, the LAST callback pushed is the
 *	FIRST to execute.  NRE-aware commands exploit this by pushing
 *	their continuation (post-processing) callback FIRST, then
 *	pushing the work callback (e.g., Th8_NREval).  The work runs
 *	first, and when it returns, the continuation sees the result.
 *
 *	CANCELLATION:
 *
 *	If cancellation is active, all remaining callbacks above
 *	pBottom are freed without invoking them, and the cancel
 *	error is propagated as the return code.
 *
 * Why / How:
 *	This is the core NRE engine.  Each iteration pops the top
 *	callback, invokes it, and threads the return code to the
 *	next.  Cancellation is checked between callbacks: with
 *	-unwind all remaining callbacks are discarded; without
 *	-unwind the cancel flag is consumed and callbacks (including
 *	[catch]) continue running.  Suspension and yield preserve
 *	the remaining chain for later resumption.
 *
 * Results:
 *	Return code from the last callback.
 *
 * Side effects:
 *	All callbacks above pBottom are invoked and freed.
 *
 *----------------------------------------------------------------------
 */

static int
th8RunCallbacks(
    Th8_Interp *interp, /* Interpreter. */
    Th8_Callback *pBottom, /* Stop marker (NULL = drain all). */
    int rc) /* Initial return code. */
{
    while (interp->pCallbacks != pBottom) {
	Th8_Callback *pCb;

	/*
	 * Check for cancellation between each step.
	 *
	 * Without -unwind: consume the cancel flag (one-shot)
	 * and set rc to TH8_ERROR, but continue running
	 * callbacks so that [catch] can intercept the error.
	 *
	 * With -unwind: discard all remaining callbacks without
	 * invoking them, ensuring the error propagates to the
	 * outermost Th8_Eval caller.
	 *
	 * Bug 66 (BY DESIGN, do NOT "fix"): this bare-free deliberately
	 * does NOT invoke the callbacks -- not even with TH8_CLEANUP --
	 * because invoking them clobbers the propagating "unwound"
	 * cancellation state (verified: doing so fails suspend-5.7 /
	 * suspend-6.4).  The pData[] payloads (argv/azNew,
	 * Th8_EvalState, frames, [update]/[vwait] state) are therefore
	 * leaked on the -unwind path.  That is an accepted tradeoff on
	 * this rare path: clean error propagation outranks reclaiming a
	 * few allocations from an interpreter that is unwinding.  (The
	 * coroutine teardown drain CAN use TH8_CLEANUP because it has no
	 * error to propagate; the -unwind path does.)
	 */

	th8MemBarrier(interp);
	if (th8CheckCancel(interp)) {
	    if (Th8_IsBeingUnwound(interp)) {
		rc = Th8_IsCanceled(interp, 0);
		while (interp->pCallbacks != pBottom) {
		    pCb = interp->pCallbacks;
		    interp->pCallbacks = pCb->pNext;
		    Th8_Free(interp, pCb);
		}
		break;
	    }
	    /* Non-unwind: one-shot - consume flag, set error,
	     * let callbacks (including catch) still run. */
	    rc = Th8_IsCanceled(interp, 0);
	    th8ClearCancel(interp);
	}

	/*
	 * Check for suspension.  Unlike cancellation, the callback
	 * chain is preserved so Th8_Thaw can resume later.
	 */

	if (Th8_IsSuspended(interp)) {
	    rc = TH8_SUSPEND;
	    break;
	}

	/*
	 * Pop the top callback and invoke it, threading the
	 * return code through so each continuation can inspect
	 * or transform the result of the previous step.
	 */

	pCb = interp->pCallbacks;
	interp->pCallbacks = pCb->pNext;
	rc = pCb->xProc(interp, pCb->pData, rc);
	Th8_Free(interp, pCb);

	/*
	 * Check for yield.  Like suspension, the remaining
	 * callback chain must be preserved so the coroutine
	 * resume can re-attach and drain them later.  Unlike
	 * suspension, yield uses a return code (TH8_YIELD)
	 * rather than a global flag (bSuspended).
	 */

	if (rc == TH8_YIELD) {
	    break;
	}
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8EvalTrampoline --
 *
 *	Run the NRE trampoline until the callback chain is empty.
 *
 * Why / How:
 *	This is the outermost trampoline entry point, called at the
 *	top level to drain all pending NRE callbacks.  It passes
 *	NULL as the bottom marker so th8RunCallbacks processes every
 *	callback in the chain.
 *
 * Results:
 *	Return code from the last callback.
 *
 * Side effects:
 *	All pending callbacks are invoked and freed.
 *
 *----------------------------------------------------------------------
 */

int
th8EvalTrampoline(Th8_Interp *interp) /* Interpreter. */
{
    return th8RunCallbacks(interp, 0, TH8_OK);
}


/*
 *----------------------------------------------------------------------
 *
 * th8NREvalCallback --
 *
 *	NRE callback that sets up script evaluation via th8EvalLocal.
 *	th8EvalLocal pushes its own NRE callbacks onto the chain;
 *	the trampoline drains them after this callback returns.
 *
 * Why / How:
 *	This is the trampoline-side entry point for NRE eval.
 *	It unpacks the script pointer and length from the callback's
 *	pData slots and calls th8EvalLocal to set up the per-command
 *	iteration callbacks.
 *
 *----------------------------------------------------------------------
 */

static int
th8NREvalCallback(
    Th8_Interp *interp, /* Interpreter. */
    void *pData[], /* [0]=zProg, [1]=nProg, [2]=zName,
				 * [3]=nName. */
    int rc) /* Return code from previous step. */
{
    const char *zProg = (const char *)pData[0];
    size_t nProg = (size_t)(pData[1]);
    const char *zName = (const char *)pData[2];
    size_t nName = (size_t)(pData[3]);

    (void)rc;
    return th8EvalLocal(interp, zProg, nProg, zName, nName, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NREval --
 *
 *	Non-recursive eval: pushes a script evaluation onto the NRE
 *	callback chain.  The script will be evaluated when the
 *	trampoline drains the callbacks.
 *
 *	This is used by NRE-converted commands instead of calling
 *	Th8_Eval() directly.  The command pushes its continuation
 *	callback first, then calls Th8_NREval to push the eval.
 *	The eval runs first (LIFO), then the continuation.
 *
 * Why / How:
 *	Instead of calling Th8_Eval (which blocks), NRE-converted
 *	commands push a th8NREvalCallback via this function.  The
 *	trampoline later invokes th8EvalLocal, which pushes its own
 *	iteration callbacks.  The caller's continuation (pushed
 *	before Th8_NREval) then sees the eval result.
 *
 * Results:
 *	Always TH8_OK.
 *
 * Side effects:
 *	A callback is pushed onto the NRE chain.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8EvalCleanup --
 *
 *	General-purpose NRE cleanup callback that frees pData[0]
 *	and propagates the incoming rc unchanged.  Used by both the
 *	control plugin ([eval]) and the procedures plugin ([apply]
 *	/ [napply]) to free heap-allocated script buffers / ProcDefn
 *	contexts after Th8_NREval completes.  Lives in th8_core.c
 *	because the procedures plugin must keep working even when
 *	PLUGIN_CONTROL is gated off (Bug 35: ENABLE_EXPRESSIONS=0
 *	auto-disables PLUGIN_CONTROL via the Makefile, leaving
 *	procedures with a dangling reference if this helper lived
 *	in the control plugin).
 *
 *----------------------------------------------------------------------
 */

int
th8EvalCleanup(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_Free(interp, pData[0]);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NREval --
 *
 *	Schedule a script for non-recursive evaluation by pushing
 *	an `th8NREvalCallback` onto the interpreter's NRE callback
 *	chain.  Returns immediately; the trampoline (typically
 *	`Th8_Eval` or `Th8_NRExec`) drives the script asynchronously
 *	when control returns to it.
 *
 *	This is the embedder-facing primitive used by control-flow
 *	commands (`[eval]`, `[apply]`, `[uplevel]`) to enqueue
 *	work without growing the C call stack.  Pair with
 *	`Th8_NREvalInFrame` when the eval needs a different frame.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	zProg  -- script bytes to evaluate.
 *	nProg  -- length of `zProg` in bytes, or `TH8_NOLEN` to
 *	          use `Th8_Strlen`.
 *	zName  -- origin name (used for `[info script]` and error
 *	          messages); may be NULL when no origin is known.
 *	nName  -- length of `zName`.
 *
 * Returns:
 *	`TH8_OK` on success.
 *	`TH8_ERROR` if `interp` is NULL, or if scheduling the callback
 *	fails (allocation failure -- interpreter result "out of
 *	memory"); the deferred eval is NOT scheduled in that case.
 *	Errors that arise during the deferred evaluation surface
 *	from whichever NRE trampoline drives the callback.
 *
 * Side effects:
 *	Pushes one entry onto `interp`'s NRE callback chain.  Does
 *	NOT copy `zProg` or `zName`; the caller must keep them
 *	alive until the callback executes.
 *
 *----------------------------------------------------------------------
 */
int
Th8_NREval(
    Th8_Interp *interp, /* Interpreter. */
    const char *zProg, /* Script to evaluate. */
    size_t nProg, /* Script length (TH8_NOLEN=NUL). */
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName) /* Origin name length. */
{
    if (!interp) return TH8_ERROR;
    if (nProg == TH8_NOLEN) {
	nProg = Th8_Strlen(interp, zProg);
    }
    /* Propagate the scheduling result: Th8_NRAddCallback returns
     * TH8_ERROR (with an "out of memory" result) if the callback
     * allocation fails.  Returning TH8_OK unconditionally would report
     * a failed schedule as success, so the deferred eval would silently
     * never run. */
    return Th8_NRAddCallback(
        interp, th8NREvalCallback, (void *)zProg, TH8_INT2PTR(nProg),
        (void *)zName, TH8_INT2PTR(nName));
}


/*
 *----------------------------------------------------------------------
 *
 * th8FrameRestoreCallback --
 *
 *	NRE callback that restores the interpreter's frame pointer
 *	to a previously saved value.  Used by Th8_NREvalInFrame
 *	to undo a frame switch after evaluation completes.
 *
 * Why / How:
 *	Part of the NRE callback chain for frame-switching eval.
 *	The saved frame pointer is stored in pData[0].  This
 *	callback fires after the evaluated script returns, restoring
 *	the frame regardless of the return code.
 *
 *----------------------------------------------------------------------
 */

static int
th8FrameRestoreCallback(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_Frame *pSaved = (Th8_Frame *)pData[0];

    interp->pDownlevelFrame = 0;
    interp->pFrame = pSaved;
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_NREvalInFrame --
 *
 *	Non-recursive eval in a different frame.  Switches to the
 *	target frame, pushes a restore callback, then pushes the
 *	eval.  Used by [uplevel].
 *
 *	iFrame semantics are the same as Th8_Eval:
 *	  0       = current frame (no switch needed).
 *	  negative = relative offset (e.g., -1 = caller).
 *	  positive = absolute frame number (converted internally).
 *
 * Why / How:
 *	Splits the name on "::" to find the target namespace,
 *	creating intermediate namespaces as needed.  If a command
 *	with the same name already exists, its delete callback is
 *	fired and the Th8_Command struct is reused.  A secondary
 *	hash (token -> command) provides O(1) deletion by token.
 * Results:
 *	TH8_OK, or TH8_ERROR if the frame is invalid.
 *
 *----------------------------------------------------------------------
 */

static int th8NsRestoreCallback(Th8_Interp *, void *[], int);

int
Th8_NREvalInFrame(
    Th8_Interp *interp,
    int iFrame,
    const char *zProg,
    size_t nProg,
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName) /* Origin name length. */
{
    Th8_Frame *pSavedFrame;
    Th8_Frame *pTarget;

    if (!interp) return TH8_ERROR;
    pSavedFrame = interp->pFrame;
    pTarget = interp->pFrame;

    if (iFrame != 0) {
	int i;

	/*
	 * Convert positive (absolute) frame number to a
	 * negative (relative) offset.
	 */

	if (iFrame > 0) {
	    int nFrames = 0;
	    Th8_Frame *p;

	    for (p = interp->pFrame; p; p = p->pCaller) {
		nFrames++;
	    }
	    iFrame = -nFrames + iFrame;
	    pTarget = interp->pFrame;
	}

	/*
	 * Walk up the frame chain by (-iFrame) steps.
	 */

	for (i = 0; pTarget && i < (-iFrame); i++) {
	    pTarget = pTarget->pCaller;
	}
	if (!pTarget) {
	    Th8_SetResult(interp, "no such frame", TH8_NOLEN);
	    return TH8_ERROR;
	}
	interp->pDownlevelFrame = pSavedFrame;
	interp->pFrame = pTarget;
    }

    /*
     * Push frame restore FIRST (runs last due to LIFO), then
     * push the eval (runs first).  When the eval completes,
     * th8FrameRestoreCallback restores the original frame.
     */

    Th8_NRAddCallback(
        interp, th8FrameRestoreCallback, (void *)pSavedFrame, 0, 0, 0);

    /*
     * Restore the namespace context from the target frame
     * ONLY for non-global namespace-eval frames.  For the
     * global frame and proc frames, keep the calling proc's
     * pCurrentNs.
     *
     * Also stamp the target frame's pNs with the active
     * namespace so that any procs called from the uplevel'd
     * code that do their own [uplevel] back to this frame
     * will see the correct namespace context.
     *
     * th8PushFrame (L4747) always initialises pFrame->pNs to
     * interp->pCurrentNs (which is non-NULL by construction:
     * pGlobalNs at startup, then proc/ns-eval frames push
     * their own).  The global frame is initialised at L18776.
     * So every reachable pTarget has pNs != NULL; the first
     * sub-condition is ALWAYS T at runtime. */

    if (ALWAYS(pTarget->pNs) && pTarget->pNs != interp->pCurrentNs) {
	Th8_NRAddCallback(
	    interp, th8NsRestoreCallback, (void *)interp->pCurrentNs, 0, 0,
	    0);
	interp->pCurrentNs = pTarget->pNs;
    }
    return Th8_NREval(interp, zProg, nProg, zName, nName);
}


/*
 *----------------------------------------------------------------------
 *
 * th8NsRestoreCallback --
 *
 *	NRE callback that restores pCurrentNs after a command
 *	finishes executing in its defining namespace.  Pushed
 *	by the evaluator before invoking a command whose pDefNs
 *	differs from the current namespace.
 *
 * Why / How:
 *	When a command is invoked in its defining namespace (pDefNs),
 *	pCurrentNs is temporarily switched.  This callback reverses
 *	that switch after the command returns, passing the return
 *	code through unchanged.
 *
 *----------------------------------------------------------------------
 */

static int
th8NsRestoreCallback(
    Th8_Interp *interp, /* Interpreter. */
    void *pData[], /* [0]=saved Th8_Namespace*. */
    int rc) /* Return code (passed through). */
{
    interp->pCurrentNs = (Th8_Namespace *)pData[0];
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FrameCleanup --
 *
 *	NRE callback that pops and frees a heap-allocated frame.
 *	Pushed by th8NRInFrame before the work callback so that
 *	it runs AFTER the work is done (LIFO ordering).
 *
 * Why / How:
 *	This is the proc-frame teardown callback.  It annotates
 *	::errorInfo with the procedure name and line number on
 *	error, pops the frame, frees the heap-allocated Th8_Frame,
 *	and translates TH8_RETURN -> TH8_OK and TH8_RETURN2 ->
 *	TH8_RETURN (Tcl's multi-level return unwinding).
 *
 *----------------------------------------------------------------------
 */

static int
th8FrameCleanup(
    Th8_Interp *interp, /* Interpreter. */
    void *pData[], /* [0]=pFrame. */
    int rc) /* Return code from work callback. */
{
    Th8_Frame *pFrame = (Th8_Frame *)pData[0];
    char *zSavedRes = 0; /* function-scope so oom can free it */
    size_t nSavedRes = 0;
#if defined(TH8_ENABLE_VARIABLES)
    char *zInfo = 0;
    size_t nInfo = 0;
#endif

    /*
     * On error, annotate ::errorInfo with the procedure name
     * and line number BEFORE the frame is popped.
     */

    if (rc == TH8_ERROR && ALWAYS(pFrame->argc > 0) && ALWAYS(pFrame->argv)) {
	/*
	 * Save the error result so our annotation work
	 * doesn't clobber it.
	 */

	zSavedRes = Th8_TakeResult(interp, &nSavedRes);

#if defined(TH8_ENABLE_VARIABLES)
	if (TH8_OK == Th8_GetVar(interp, "::errorInfo", TH8_NOLEN)) {
	    size_t nOld;
	    const char *zOld;
	    char zLineBuf[20];
	    int nLine = interp->nLine;
	    int li = 0;

	    zOld = Th8_GetResult(interp, &nOld);
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, zOld, nOld);
	    TH8_STR_APPEND(
	        interp, &zInfo, &nInfo, "\n    (procedure \"", TH8_NOLEN);
	    TH8_STR_APPEND(
	        interp, &zInfo, &nInfo, pFrame->argv[0], pFrame->argl[0]);
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, "\" line ", TH8_NOLEN);

	    /* Format line number without touching result. */
	    if (nLine <= 0) nLine = 1;
	    {
		char tmp[20];
		int tn = 0;

		do {
		    tmp[tn++] = '0' + (nLine % 10);
		    nLine /= 10;
		} while (nLine > 0);
		while (tn > 0) {
		    zLineBuf[li++] = tmp[--tn];
		}
		zLineBuf[li] = 0;
	    }
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, zLineBuf, (size_t)li);
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, ")", 1);
	    Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, zInfo, nInfo);
	    Th8_Free(interp, zInfo);
	}
#endif

	/* Restore the error result. */
	Th8_SetResult(interp, zSavedRes, nSavedRes);
	Th8_Free(interp, zSavedRes);
    }

    th8PopFrame(interp);
    Th8_Free(interp, pFrame);
    if (rc == TH8_RETURN) {
	rc = TH8_OK;
    }
    if (rc == TH8_RETURN2) {
	rc = TH8_RETURN;
    }
    return rc;

#if defined(TH8_ENABLE_VARIABLES)
oom:
    /* A TH8_STR_APPEND growth failed while annotating the error trace;
     * "out of memory" already set.  Still pop the frame and free it (as
     * the normal path does) so the frame stack is not corrupted. */
    Th8_Free(interp, zInfo);
    Th8_Free(interp, zSavedRes);
    th8PopFrame(interp);
    Th8_Free(interp, pFrame);
    return TH8_ERROR;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8NRInFrame --
 *
 *	Non-recursive InFrame: heap-allocates a frame, pushes a
 *	cleanup callback, then pushes the work callback.  The work
 *	callback runs first (LIFO), then cleanup pops the frame.
 *
 * Why / How:
 *	Encapsulates the push-frame / push-cleanup / push-work
 *	pattern used by proc dispatch.  The LIFO ordering ensures
 *	the work callback executes first and th8FrameCleanup runs
 *	afterward to pop the frame and translate return codes.
 *
 * Results:
 *	Always TH8_OK.
 *
 * Side effects:
 *	A frame is pushed and two callbacks are added to the chain.
 *
 *----------------------------------------------------------------------
 */

int
th8NRInFrame(
    Th8_Interp *interp, /* Interpreter. */
    Th8_CallbackProc xCall, /* Work callback. */
    void *p0, /* Client data slot 0. */
    void *p1, /* Client data slot 1. */
    void *p2, /* Client data slot 2. */
    void *p3) /* Client data slot 3. */
{
    Th8_Frame *pFrame;

    pFrame = (Th8_Frame *)TH8_ALLOC(interp, sizeof(Th8_Frame));
    if (!pFrame) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (th8PushFrame(interp, pFrame) != TH8_OK) {
	Th8_Free(interp, pFrame);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Push cleanup first (runs last due to LIFO), then work.
     */

    Th8_NRAddCallback(interp, th8FrameCleanup, (void *)pFrame, 0, 0, 0);
    Th8_NRAddCallback(interp, xCall, p0, p1, p2, p3);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Command management --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8RemoveCmdTokenEntry --
 *
 *	Remove the secondary index entry for a command's token.
 *	Called from every path that frees a Th8_Command*.
 *
 * Why / How:
 *	The secondary token index maps token -> Th8_Command* for
 *	O(1) deletion.  This helper removes the stale entry
 *	whenever a command is deleted or replaced.
 *
 *----------------------------------------------------------------------
 */

static void
th8RemoveCmdTokenEntry(Th8_Interp *interp, Th8_Command *pCmd)
{
    /* th8RemoveCmdTokenEntry is invoked only as part of command
     * teardown paths where pCmd is just-resolved (never NULL),
     * and the paCmdToken hash is allocated at interp setup; both
     * sub-conditions are ALWAYS T at runtime, leaving the
     * pCmd->nToken == 0 case as the only reachable false vector
     * (commands created before nToken was added). */
    if (ALWAYS(pCmd) && pCmd->nToken && ALWAYS(interp->paCmdToken)) {
	Th8_HashFind(
	    interp, interp->paCmdToken, (const char *)&pCmd->nToken,
	    sizeof(th8_uint64_t), -1);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_CreateCommand --
 *
 *	Register a new command.  If the name contains "::", the
 *	command is placed in the indicated namespace (creating it
 *	if necessary).  Otherwise it is placed in the current
 *	namespace.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	If a command with the same name exists, its delete callback
 *	is called and it is replaced.
 *
 * Why / How:
 *	Wraps Th8_ErrorMessage with the standard Tcl "wrong # args"
 *	prefix and always returns TH8_ERROR so callers can write
 *	return Th8_WrongNumArgs(...).
 *----------------------------------------------------------------------
 */

int
Th8_CreateCommand(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName, /* Command name. */
    Th8_CommandProc xProc, /* Command procedure. */
    void *pContext, /* Context for command proc. */
    void (*xDel)(Th8_Interp *, void *),
    /* Destructor (may be NULL). */
    th8_uint64_t *pToken) /* OUT: command token (NULL ok). */
{
    Th8_HashEntry *pEntry;
    Th8_Command *pCmd;
    Th8_Hash *paCmd;
    const char *zNs;
    size_t nNs;
    const char *zTail;
    size_t nTail;
    size_t nName;

    if (!interp) return TH8_ERROR;

    TH8_ASSERT_OWNER(interp);

    nName = Th8_Strlen(interp, zName);

    /*
     * Determine the target namespace and the simple tail name.
     */

    th8SplitQualName(zName, nName, &zNs, &nNs, &zTail, &nTail);

    if (zNs) {
	/*
	 * Name contains "::" -- resolve the namespace path,
	 * creating intermediate namespaces as needed.
	 */

	Th8_Namespace *pNs;

	pNs = th8FindNamespace(interp, zNs, nNs, 1);
	paCmd = pNs->paCmd;
    } else {
	/*
	 * Simple name -- insert into the current namespace.
	 */

	paCmd = interp->pCurrentNs->paCmd;
	zTail = zName;
	nTail = nName;
    }

    pEntry = Th8_HashFind(interp, paCmd, zTail, nTail, 1);
    if (pEntry->pData) {
	pCmd = (Th8_Command *)pEntry->pData;
	th8RemoveCmdTokenEntry(interp, pCmd);
	if (pCmd->xDel) {
	    pCmd->xDel(interp, pCmd->pContext);
	}
    } else {
	pCmd = (Th8_Command *)TH8_ALLOC(interp, sizeof(Th8_Command));
	if (!pCmd) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	pCmd->zQualName = 0;
	pCmd->nQualName = 0;
    }
    pCmd->xProc = xProc;
    pCmd->pContext = pContext;
    pCmd->xDel = xDel;
    pCmd->nToken = interp->nNextCmdToken++;

    /* Store the fully qualified name for O(1) token-based delete. */
    Th8_Free(interp, pCmd->zQualName);
    {
	size_t nQ = Th8_Strlen(interp, zName);

	pCmd->zQualName = (char *)TH8_ALLOC_STR(interp, nQ);
	if (pCmd->zQualName) {
	    Th8_Memcpy(interp, pCmd->zQualName, zName, nQ);
	    pCmd->zQualName[nQ] = 0;
	    pCmd->nQualName = nQ;
	} else {
	    pCmd->nQualName = 0;
	}
    }

    /*
     * Record the defining namespace.  When the command is
     * invoked (even via import), the evaluator will set
     * pCurrentNs to pDefNs so that [variable] and
     * [namespace current] resolve correctly.
     *
     * Only set pDefNs for commands defined in a non-global
     * namespace.  Commands in the global namespace use
     * pDefNs = NULL, which tells the evaluator to leave
     * pCurrentNs unchanged (so [namespace eval] works).
     */

    {
	Th8_Namespace *pNs;

	if (zNs) {
	    pNs = th8FindNamespace(interp, zNs, nNs, 0);
	} else {
	    pNs = interp->pCurrentNs;
	}

	/*
	 * Only set pDefNs for commands in a non-global
	 * namespace.  Global commands (the vast majority)
	 * use pDefNs = NULL so the evaluator leaves the
	 * namespace context unchanged.
	 *
	 * pNs is ALWAYS non-NULL at this point: if zNs is
	 * non-NULL the earlier L9454 call uses flag=1 (find
	 * or create), so the same find-only at L9519 finds
	 * the just-created namespace; if zNs is NULL,
	 * pCurrentNs is non-NULL by construction
	 * (pGlobalNs at startup). */

	pCmd->pDefNs = (ALWAYS(pNs) && pNs != interp->pGlobalNs) ? pNs : 0;
    }
    pEntry->pData = (void *)pCmd;

    /*
     * Invalidate the cached resolution for this command name.
     */
    th8RemoveFromCache(interp, TH8_CACHE_COMMAND, zTail, nTail);

    /*
     * Secondary index: map token ==> Th8_Command*.
     * If the command was replaced (already existed), remove
     * the old token entry first.
     */

    if (!interp->paCmdToken) {
	interp->paCmdToken = Th8_HashNew(interp);
    }
    if (interp->paCmdToken) {
	Th8_HashEntry *pTokEntry;

	pTokEntry = Th8_HashFind(
	    interp, interp->paCmdToken, (const char *)&pCmd->nToken,
	    sizeof(th8_uint64_t), 1);
	if (pTokEntry) {
	    pTokEntry->pData = pCmd;
	}
    }

    if (pToken) *pToken = pCmd->nToken;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DeleteCommand --
 *
 *	Delete a command by its unique token.  Searches all commands
 *	in all namespaces for a matching token, then removes it via
 *	rename-to-empty.
 *
 * Why / How:
 *	Uses the secondary token-to-command hash index for O(1)
 *	lookup, then delegates to Th8_RenameCommand with an empty
 *	destination name (which triggers deletion).  The stored
 *	qualified name avoids a linear scan of all namespaces.
 *
 * Results:
 *	TH8_OK if found and deleted, TH8_ERROR if not found.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DeleteCommand(Th8_Interp *interp, th8_uint64_t token)
{
    Th8_HashEntry *pTokEntry;
    Th8_Command *pCmd;

    TH8_ASSERT_OWNER(interp);

    if (!interp->paCmdToken) {
	Th8_SetResult(
	    interp, "command not found (no token index)", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * O(1) lookup in the secondary token index.
     */

    pTokEntry = Th8_HashFind(
        interp, interp->paCmdToken, (const char *)&token,
        sizeof(th8_uint64_t), 0);

    if (!pTokEntry || !pTokEntry->pData) {
	Th8_SetResult(
	    interp, "command not found (token not matched)", TH8_NOLEN);
	return TH8_ERROR;
    }

    pCmd = (Th8_Command *)pTokEntry->pData;

    /*
     * O(1) deletion: the Th8_Command stores its current
     * qualified name (updated on rename), so we can delete
     * directly via Th8_RenameCommand without scanning.
     */

    if (pCmd->zQualName && pCmd->nQualName > 0) {
	return Th8_RenameCommand(
	    interp, pCmd->zQualName, pCmd->nQualName, "", 0);
    }

    Th8_SetResult(interp, "command not found (no stored name)", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetCommandInfo --
 *
 *	Look up a command by name and return its procedure pointer
 *	and context.  For qualified names (containing "::"), the
 *	specific namespace is searched.  For simple names, the
 *	current namespace is tried first, then the global namespace.
 *
 * Why / How:
 *	Uses the same qualified-name splitting as Th8_CreateCommand.
 *	For simple names, the current namespace is searched first;
 *	if not found, the global namespace is tried as a fallback
 *	(matching Tcl 8.4 command resolution order).
 *
 * Results:
 *	TH8_OK if found, TH8_ERROR if not.
 *
 * Side effects:
 *	On error, sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetCommandInfo(
    Th8_Interp *interp, /* Interpreter. */
    const char *zName, /* Command name. */
    size_t nName, /* Name length (or TH8_NOLEN). */
    Th8_CommandProc *pxProc, /* OUT: command procedure. */
    void **ppContext) /* OUT: command context. */
{
    Th8_HashEntry *pEntry = 0;
    Th8_Command *pCmd;
    const char *zNs;
    size_t nNs;
    const char *zTail;
    size_t nTail;

    if (!interp) return TH8_ERROR;

    /*
     * Resolve the name length.  TH8_NOLEN is the documented "NUL-
     * terminated, compute the length" sentinel; it is (size_t)-1, so it
     * MUST be detected before TH8_LEN() masks it -- TH8_LEN(TH8_NOLEN)
     * yields TH8_LEN_MASK (0x0fffffff, ~256 MiB), which would make
     * th8SplitQualName scan far past the end of the name and fault.
     * Explicit lengths still pass through TH8_LEN to strip any high
     * flag bits, matching every other length-taking entry point.
     */
    if (nName == TH8_NOLEN) {
	nName = Th8_Strlen(interp, zName);
    } else {
	nName = TH8_LEN(nName);
    }

    th8SplitQualName(zName, nName, &zNs, &nNs, &zTail, &nTail);

    if (zNs) {
	/*
	 * Qualified name -- look up in the specific namespace.
	 */

	Th8_Namespace *pNs;

	pNs = th8FindNamespace(interp, zNs, nNs, 0);
	if (pNs) {
	    pEntry = Th8_HashFind(interp, pNs->paCmd, zTail, nTail, 0);
	}
    } else {
	/*
	 * Simple name -- try current namespace, then global.
	 */

	pEntry =
	    Th8_HashFind(interp, interp->pCurrentNs->paCmd, zName, nName, 0);
	if (!pEntry && interp->pCurrentNs != interp->pGlobalNs) {
	    pEntry = Th8_HashFind(
	        interp, interp->pGlobalNs->paCmd, zName, nName, 0);
	}
    }

    if (!pEntry) {
	Th8_ErrorMessage(interp, "no such command:", zName, nName);
	return TH8_ERROR;
    }
    pCmd = (Th8_Command *)pEntry->pData;
    if (pxProc) {
	*pxProc = pCmd->xProc;
    }
    if (ppContext) {
	*ppContext = pCmd->pContext;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_WrongNumArgs --
 *
 *	Set a "wrong # args" error message.
 *
 * Why / How:
 *	Wraps Th8_ErrorMessage with the standard Tcl "wrong # args"
 *	prefix and always returns TH8_ERROR so callers can write
 *	return Th8_WrongNumArgs(...).
 *
 * Results:
 *	TH8_ERROR.
 *
 * Side effects:
 *	Sets interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
Th8_WrongNumArgs(
    Th8_Interp *interp, /* Interpreter. */
    const char *zMsg) /* Usage string. */
{
    if (!interp) return TH8_ERROR;
    Th8_ErrorMessage(interp, "wrong # args: should be \"", zMsg, TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_CallSubCommand --
 *
 *	Dispatch a sub-command from an ensemble table.
 *
 * Why / How:
 *	Looks up argv[1] in the sub-command table by exact match.
 *	On mismatch, builds a Tcl-style error listing all valid
 *	sub-commands separated by commas with "or" before the last.
 *	On missing sub-command, uses the "wrong # args" format.
 *
 * Results:
 *	Return code of the matched sub-command, or TH8_ERROR.
 *
 * Side effects:
 *	Sets an error message listing valid sub-commands if no
 *	match is found.
 *
 *----------------------------------------------------------------------
 */

int
Th8_CallSubCommand(
    Th8_Interp *interp, /* Interpreter. */
    void *ctx, /* Command context. */
    int argc, /* Argument count. */
    const char **argv, /* Argument values. */
    size_t *argl, /* Argument lengths. */
    const Th8_SubCommand *aSub) /* Sub-command table. */
{
    char *zMsg = 0; /* function-scope so the oom label can free it */
    size_t nMsg = 0;
    char *z = 0;

    if (!interp) return TH8_ERROR;
    if (argc > 1) {
	int i;

	for (i = 0; aSub[i].zName; i++) {
	    size_t nName = Th8_Strlen(interp, aSub[i].zName);

	    if (nName == TH8_LEN(argl[1]) &&
	        0 == Th8_Memcmp(
	                 interp, aSub[i].zName, argv[1], TH8_LEN(argl[1]))) {
		return aSub[i].xProc(interp, ctx, argc, argv, argl);
	    }
	}
    }

    if (argc < 2) {
	/*
	 * No sub-command given.
	 */

	Th8_ErrorMessage(
	    interp, "wrong # args: should be \"", argv[0], TH8_LEN(argl[0]));
	z = Th8_TakeResult(interp, 0);
	TH8_STR_APPEND(interp, &zMsg, &nMsg, z, TH8_NOLEN);
	TH8_STR_APPEND(
	    interp, &zMsg, &nMsg, " subcommand ?arg ...?\"", TH8_NOLEN);
	Th8_SetResult(interp, zMsg, nMsg);
	Th8_Free(interp, zMsg);
	Th8_Free(interp, z);
    } else {
	/*
	 * Unknown sub-command -- list valid ones.
	 */

	int i;

	TH8_STR_APPEND(
	    interp, &zMsg, &nMsg, "unknown or ambiguous subcommand \"",
	    TH8_NOLEN);
	TH8_STR_APPEND(interp, &zMsg, &nMsg, argv[1], TH8_LEN(argl[1]));
	TH8_STR_APPEND(interp, &zMsg, &nMsg, "\": must be ", TH8_NOLEN);
	for (i = 0; aSub[i].zName; i++) {
	    if (i > 0) {
		if (aSub[i + 1].zName) {
		    TH8_STR_APPEND(interp, &zMsg, &nMsg, ", ", TH8_NOLEN);
		} else {
		    TH8_STR_APPEND(interp, &zMsg, &nMsg, ", or ", TH8_NOLEN);
		}
	    }
	    TH8_STR_APPEND(interp, &zMsg, &nMsg, aSub[i].zName, TH8_NOLEN);
	}
	Th8_SetResult(interp, zMsg, nMsg);
	Th8_Free(interp, zMsg);
    }
    return TH8_ERROR;

oom:
    /* TH8_STR_APPEND growth failed; "out of memory" already set. */
    Th8_Free(interp, zMsg);
    Th8_Free(interp, z);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * UTF-8 utilities --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_Utf8Decode --
 *
 *	Decode one UTF-8 code point.
 *
 * Results:
 *	The code point value, or -1 on invalid sequence.
 *	*pnByte is set to the number of bytes consumed (1-4).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 * Why / How:
 *	Scans horizontal whitespace used as word separators within a
 *	command line.  Backslash-newline continuations are treated as
 *	whitespace (consuming the backslash, newline, and any
 *	following spaces/tabs).  Newlines themselves are NOT consumed
 *	because they serve as command terminators.
 */

int
Th8_Utf8Decode(
    const char *z, /* Input bytes. */
    size_t n, /* Number of bytes available. */
    int *pnByte) /* OUT: bytes consumed. */
{
    unsigned char c;

    if (n == 0) {
	return -1;
    }
    c = (unsigned char)z[0];
    if (c < 0x80) {
	*pnByte = 1;
	return (int)c;
    } else if ((c & 0xE0) == 0xC0 && n >= 2) {
	*pnByte = 2;
	return ((c & 0x1F) << 6) | ((unsigned char)z[1] & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && n >= 3) {
	*pnByte = 3;
	return ((c & 0x0F) << 12) | (((unsigned char)z[1] & 0x3F) << 6) |
	       ((unsigned char)z[2] & 0x3F);
    } else if ((c & 0xF8) == 0xF0 && n >= 4) {
	*pnByte = 4;
	return ((c & 0x07) << 18) | (((unsigned char)z[1] & 0x3F) << 12) |
	       (((unsigned char)z[2] & 0x3F) << 6) |
	       ((unsigned char)z[3] & 0x3F);
    }
    *pnByte = 1;
    return -1;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Utf8Len --
 *
 *	Count the number of code points in a UTF-8 string.
 *
 * Why / How:
 *	Walks the byte string using Th8_Utf8Decode, which returns
 *	the byte width of each code point (1-4).  Invalid sequences
 *	still advance by one byte so the count is always defined.
 *
 * Results:
 *	The code point count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Utf8Len(
    const char *z, /* UTF-8 string. */
    size_t n) /* Byte length (TH8_NOLEN = NUL). */
{
    int nChar = 0;
    size_t i = 0;

    if (n == TH8_NOLEN) {
	n = Th8_Strlen(NULL, z);
    }
    n = TH8_LEN(n);
    while (i < n) {
	int nByte;

	Th8_Utf8Decode(&z[i], n - i, &nByte);
	i += nByte;
	nChar++;
    }
    return nChar;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BufInit --
 *
 *	Initialize a growable byte buffer to empty.
 *
 * Why / How:
 *	Zeroes all fields so the buffer is in a known empty state.
 *	No allocation occurs until the first th8BufWrite call.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Buffer fields are zeroed.
 *
 *----------------------------------------------------------------------
 */

static void
th8BufInit(Th8_Buffer *pBuf) /* Buffer to initialize. */
{
    pBuf->zBuf = 0;
    pBuf->nBuf = 0;
    pBuf->nAlloc = 0;
    pBuf->nTag = 0;
    pBuf->bFail = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BufFree --
 *
 *	Free a growable byte buffer's storage.
 *
 * Why / How:
 *	Delegates to th8BufferFree (the interpreter-level allocator)
 *	and zeros all fields so the buffer can be safely reused or
 *	the struct can go out of scope without a double-free.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Buffer memory is released and fields are zeroed.
 *
 *----------------------------------------------------------------------
 */

static void
th8BufFree(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Buffer *pBuf) /* Buffer to free. */
{
    /* Defense in depth: if any appended data was sensitive (secret
     * plaintext propagated through substitution/list building), securely
     * zero the whole allocation before releasing it so the plaintext does
     * not linger in freed, pageable heap. */
    if (pBuf->zBuf && TH8_SENSITIVE(pBuf->nTag)) {
	Th8_SecureZero(interp, pBuf->zBuf, pBuf->nAlloc);
    }
    th8BufferFree(interp, pBuf->zBuf, pBuf->nAlloc);
    pBuf->zBuf = 0;
    pBuf->nBuf = 0;
    pBuf->nAlloc = 0;
    pBuf->nTag = 0;
    pBuf->bFail = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BufWrite --
 *
 *	Append data to a growable byte buffer, reallocating as needed.
 *
 * Why / How:
 *	Uses a doubling-plus-constant growth strategy: new capacity
 *	is (used + needed) * 2 + 32.  This gives amortized O(1)
 *	append cost.  If the append cannot complete -- the growth
 *	allocation fails, or the size-guard arithmetic overflows /
 *	exceeds TH8_MX_STRLEN -- the buffer's sticky `bFail` flag is
 *	set and the write is dropped.  Once `bFail` is set every later
 *	append is a no-op, so the buffer cannot recover into a
 *	plausible-but-truncated state; finalizers detect `bFail` and
 *	fail the operation instead of publishing the truncated bytes.
 *
 * Results:
 *	None (failure is recorded in pBuf->bFail).
 *
 * Side effects:
 *	Buffer may be reallocated to a larger size; pBuf->bFail may be
 *	set on failure.
 *
 *----------------------------------------------------------------------
 */

static void
th8BufWrite(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Buffer *pBuf, /* Buffer to append to. */
    const char *z, /* Data to append. */
    size_t n) /* Number of bytes (may carry TH8_TAINT_BIT). */
{
    /*
     * The incoming count may carry tag bits (TH8_TAG_BITS: taint, when the
     * data came from an untrusted source, and/or sensitive, when it is
     * secret plaintext).  Record the tags in the buffer's nTag and strip
     * them so all size arithmetic below uses the RAW byte count --
     * otherwise a tagged length (~256+ MiB) would blow past the growth
     * guard and silently drop the append.  Callers that finalize a buffer
     * into a value combine nBuf with nTag to keep the classification.
     */
    pBuf->nTag |= (n & TH8_TAG_BITS);
    n = TH8_LEN(n);

    /* Already poisoned: drop the append so a truncated buffer cannot
     * partially recover into a plausible-but-wrong value. */
    if (pBuf->bFail) return;

    if (n == 0) return;
    if (n > pBuf->nAlloc - pBuf->nBuf) {
	size_t nSum = 0, nDbl = 0, nNew = 0;
	char *zNew;

	if (TH8_SAFE_ADD_SIZE(pBuf->nBuf, n, &nSum)) {
	    pBuf->bFail = 1;
	    return;
	}
	if (TH8_SAFE_MUL_SIZE(nSum, 2, &nDbl)) {
	    pBuf->bFail = 1;
	    return;
	}
	if (TH8_SAFE_ADD_SIZE(nDbl, 32, &nNew)) {
	    pBuf->bFail = 1;
	    return;
	}
	if (nNew > TH8_MX_STRLEN) {
	    pBuf->bFail = 1;
	    return;
	}
	zNew = (char *)th8BufferAlloc(interp, nNew);
	if (!zNew) {
	    pBuf->bFail = 1;
	    return;
	}
	if (pBuf->zBuf) {
	    Th8_Memcpy(interp, zNew, pBuf->zBuf, pBuf->nBuf);
	    th8BufferFree(interp, pBuf->zBuf, pBuf->nAlloc);
	}
	pBuf->zBuf = zNew;
	pBuf->nAlloc = nNew;
    }
    if (!pBuf->zBuf) {
	pBuf->bFail = 1;
	return;
    }
    Th8_Memcpy(interp, &pBuf->zBuf[pBuf->nBuf], z, n);
    pBuf->nBuf += n;
}


/*
 *----------------------------------------------------------------------
 *
 * th8BufAddChar --
 *
 *	Append a single character to a growable byte buffer.
 *
 * Why / How:
 *	Convenience wrapper around th8BufWrite for the common case
 *	of appending a single byte.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Buffer may be reallocated.
 *
 *----------------------------------------------------------------------
 */

static void
th8BufAddChar(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Buffer *pBuf, /* Buffer to append to. */
    char c) /* Character to append. */
{
    th8BufWrite(interp, pBuf, &c, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * Character classification --
 *
 *----------------------------------------------------------------------
 */

static const unsigned char th8CharProp[256] = {
    /* 0x00 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x08 */ 0,    1,    1,    1,    1,    1,    0,    0,
    /* 0x10 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x18 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x20 */ 1,    0,    0x10, 0,    0x10, 0,    0,    0,
    /* 0x28 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /* 0x30 */ 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    /* 0x38 */ 0x22, 0x22, 0,    0x10, 0,    0,    0,    0,
    /* 0x40 */ 0,    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x08,
    /* 0x48 */ 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    /* 0x50 */ 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    /* 0x58 */ 0x08, 0x08, 0x08, 0x10, 0x10, 0x10, 0,    0x08,
    /* 0x60 */ 0,    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x08,
    /* 0x68 */ 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    /* 0x70 */ 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    /* 0x78 */ 0x08, 0x08, 0x08, 0x10, 0,    0x10, 0,    0,
};

/* Bit 0: whitespace.  Bit 1: digit.  Bit 2: hex digit extension.
 * Bit 3: alpha/underscore.  Bit 4: list-special (";[]\{}$). */

/*
 * Character-classification helpers (`th8IsSpace`, `th8IsDigit`,
 * `th8IsAlpha`, `th8IsAlnum`, `th8IsSpecial`, `th8IsHexDig`,
 * `th8IsOctDig`, `th8IsBinDig`).
 *
 * Each function takes a single `int c` and returns non-zero when
 * `c` (interpreted as a single byte 0..255) has the named property.
 * Implementations table-lookup `th8CharProp[c]` and AND with the
 * appropriate property bit; the table itself is initialised above
 * this comment.  Two callers (Oct / Bin) use direct range checks
 * rather than the table because their property is contiguous and
 * not needed in other classifiers.
 *
 * All eight helpers share these contract details:
 *   * No libc dependency; safe to call before platform init.
 *   * Bytes outside the 0..255 range (negative or > 255) return 0.
 *   * No side effects, no allocation, no errors.
 */

/*
 *----------------------------------------------------------------------
 *
 * th8IsSpace --
 *
 *	Whitespace classification.  Returns non-zero for the ASCII
 *	whitespace set Tcl treats as command/argument separators
 *	(space, tab, newline, carriage return, vertical tab, form
 *	feed).
 *
 * Parameters:
 *	c -- byte value in `[0, 255]`; values outside this range
 *	     return 0.
 *
 * Returns:
 *	Non-zero if `c` is whitespace, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsSpace(int c)
{
    return c >= 0 && c < 256 && (th8CharProp[c] & 0x01);
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsDigit --
 *
 *	Decimal-digit classification.  Returns non-zero iff `c` is
 *	one of `'0'..'9'`.
 *
 * Parameters:
 *	c -- byte value in `[0, 255]`; values outside this range
 *	     return 0.
 *
 * Returns:
 *	Non-zero if `c` is a decimal digit, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsDigit(int c)
{
    return c >= 0 && c < 256 && (th8CharProp[c] & 0x02);
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsAlpha --
 *
 *	Alphabetic / underscore classification.  Returns non-zero
 *	iff `c` is an ASCII letter (`A`..`Z`, `a`..`z`) or `_`,
 *	matching the Tcl identifier-start character set.
 *
 * Parameters:
 *	c -- byte value in `[0, 255]`; values outside this range
 *	     return 0.
 *
 * Returns:
 *	Non-zero if `c` is alphabetic or underscore, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsAlpha(int c)
{
    return c >= 0 && c < 256 && (th8CharProp[c] & 0x08);
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsAlnum --
 *
 *	Alphanumeric / underscore classification.  Returns non-zero
 *	iff `c` matches `th8IsAlpha` OR `th8IsDigit`; this is the
 *	Tcl identifier-continuation character set.
 *
 * Parameters:
 *	c -- byte value in `[0, 255]`; values outside this range
 *	     return 0.
 *
 * Returns:
 *	Non-zero if `c` is alphanumeric or underscore, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsAlnum(int c)
{
    return c >= 0 && c < 256 && (th8CharProp[c] & 0x0A);
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsSpecial --
 *
 *	List-special / whitespace classification used by the list
 *	tokeniser.  Returns non-zero for whitespace OR for any of
 *	the list-grammar special characters (`;`, `[`, `]`, `\`,
 *	`{`, `}`, `$`).
 *
 * Parameters:
 *	c -- byte value in `[0, 255]`; values outside this range
 *	     return 0.
 *
 * Returns:
 *	Non-zero if `c` is whitespace or a list-special character,
 *	0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsSpecial(int c)
{
    return c >= 0 && c < 256 && (th8CharProp[c] & 0x11);
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsHexDig --
 *
 *	Hexadecimal-digit classification.  Returns non-zero iff `c`
 *	is one of `0..9`, `A..F`, or `a..f`.
 *
 * Parameters:
 *	c -- byte value in `[0, 255]`; values outside this range
 *	     return 0.
 *
 * Returns:
 *	Non-zero if `c` is a hex digit, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsHexDig(int c)
{
    return c >= 0 && c < 256 && (th8CharProp[c] & 0x22);
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsOctDig --
 *
 *	Octal-digit classification.  Returns non-zero iff `c` is
 *	one of `'0'..'7'`.  Direct range check (no table lookup --
 *	octal is the only Tcl-relevant property that fits the
 *	contiguous-range pattern).
 *
 * Parameters:
 *	c -- byte value.
 *
 * Returns:
 *	Non-zero if `c` is an octal digit, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsOctDig(int c)
{
    return c >= '0' && c <= '7';
}

/*
 *----------------------------------------------------------------------
 *
 * th8IsBinDig --
 *
 *	Binary-digit classification.  Returns non-zero iff `c` is
 *	`'0'` or `'1'`.  Direct comparison (no table lookup).
 *
 * Parameters:
 *	c -- byte value.
 *
 * Returns:
 *	Non-zero if `c` is `'0'` or `'1'`, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
th8IsBinDig(int c)
{
    return c == '0' || c == '1';
}


/*
 * Field accessors and test-side perturbers for the `Th8_Interp`
 * struct (functions below this comment).  All are declared
 * `TH8_INTERNAL` and exposed via the internal stubs table so
 * test-only code in `src/test/th8_testlib.c` (which does not
 * include `th8_int_core.h`) can read, swap, or perturb these
 * fields when constructing MC/DC drive vectors.  Trivial wrappers
 * over struct-member load / store / xor; no decisions, no
 * allocation, no errors.
 */

/*
 *----------------------------------------------------------------------
 *
 * th8GetInterpCmdToken --
 *
 *	Return the per-interpreter command-token hash table that
 *	maps opaque command tokens to their owning `Th8_Command`
 *	records.  Used by `Th8_GetCommandInfo` and the rename /
 *	delete paths; test code reads it to inspect the live
 *	command registry.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The `Th8_Hash *` (may be NULL during teardown).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL Th8_Hash *
th8GetInterpCmdToken(Th8_Interp *interp)
{
    return interp->paCmdToken;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SetInterpCmdToken --
 *
 *	Store a command-token hash-table pointer on the
 *	interpreter.  Test code uses this to swap in a custom
 *	registry for MC/DC drives that need to observe command-
 *	registration error paths.
 *
 * Parameters:
 *	interp     -- interpreter.  Must be non-NULL.
 *	paCmdToken -- replacement hash pointer; may be NULL.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Overwrites `interp->paCmdToken`.  Does NOT free the
 *	previous hash; the caller owns the lifecycle.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8SetInterpCmdToken(Th8_Interp *interp, Th8_Hash *paCmdToken)
{
    interp->paCmdToken = paCmdToken;
}

/*
 *----------------------------------------------------------------------
 *
 * th8GetInterpCurrentNs --
 *
 *	Return the interpreter's "current namespace" pointer (the
 *	namespace at the bottom of the script-eval stack).  Test
 *	code reads it to assert that namespace-walk operations
 *	settle on the expected scope.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The current `Th8_Namespace *`.  Always non-NULL for a
 *	fully-constructed interpreter (the global namespace is the
 *	bottom of the stack).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL Th8_Namespace *
th8GetInterpCurrentNs(Th8_Interp *interp)
{
    return interp->pCurrentNs;
}

/*
 *----------------------------------------------------------------------
 *
 * th8GetFramePaVar --
 *
 *	Return the current call-frame's variable hash table (the
 *	hash that backs script-level `[set]`, `[unset]`,
 *	`[info exists]`, etc., at the innermost scope).
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The frame's `Th8_Hash *`, or NULL when:
 *	  - `interp->pFrame` is NULL (no eval frame on the stack), or
 *	  - the build was compiled without `TH8_ENABLE_VARIABLES`
 *	    (in which case there are no per-frame variables).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL Th8_Hash *
th8GetFramePaVar(Th8_Interp *interp)
{
#if defined(TH8_ENABLE_VARIABLES)
    return interp->pFrame ? interp->pFrame->paVar : NULL;
#else
    (void)interp;
    return NULL;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * th8GetInterpPaChannels --
 *
 *	Return the interpreter's channel-table hash, mapping
 *	channel names (e.g., `stdout`, `stderr`, file tempnames)
 *	to their `Th8_Channel *` records.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *
 * Returns:
 *	The `Th8_Hash *` (may be NULL during early init or
 *	teardown).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL Th8_Hash *
th8GetInterpPaChannels(Th8_Interp *interp)
{
    return interp->paChannels;
}

/*
 * Per-interpreter security-token XOR perturbers (test-only).
 * Each helper below XOR-perturbs a single token field by the
 * supplied mask.  The mutation is pulled out of `th8TestPerturb*`
 * in `src/test/th8_testlib.c` so the body lives next to the
 * field it touches; the testlib helpers compose them into
 * higher-level MC/DC drive sequences.  Each function is one
 * statement -- no decisions, no errors, no side effects beyond
 * the named field.
 */

/*
 *----------------------------------------------------------------------
 *
 * th8XorInterpBigintToken --
 *
 *	XOR-perturb the interpreter's bigint-enable token by `mask`.
 *	Used to drive token-tamper detection paths in MC/DC tests
 *	without touching the token via the normal API.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	mask   -- 64-bit XOR mask.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	`interp->nBigintToken ^= mask`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8XorInterpBigintToken(Th8_Interp *interp, th8_int64_t mask)
{
    interp->nBigintToken ^= mask;
}

/*
 *----------------------------------------------------------------------
 *
 * th8XorInterpSignedToken --
 *
 *	XOR-perturb the interpreter's signed-only-policy token by
 *	`mask`.  Drives tamper detection on the signed-only state
 *	field; tests rely on this to verify that the loader
 *	rejects scripts when the token has been corrupted.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	mask   -- 64-bit XOR mask.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	`interp->nSignedToken ^= mask`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8XorInterpSignedToken(Th8_Interp *interp, th8_int64_t mask)
{
    interp->nSignedToken ^= mask;
}

/*
 *----------------------------------------------------------------------
 *
 * th8XorInterpSecurePersistToken --
 *
 *	XOR-perturb the secure-persist-enable token by `mask`.
 *	Drives tamper detection on the secure-variable persistence
 *	state.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	mask   -- 64-bit XOR mask.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	`interp->nSecurePersistToken ^= mask`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8XorInterpSecurePersistToken(Th8_Interp *interp, th8_int64_t mask)
{
    interp->nSecurePersistToken ^= mask;
}

/*
 *----------------------------------------------------------------------
 *
 * th8XorInterpSecurePersistOk --
 *
 *	XOR-perturb the secure-persist-OK confirmation token by
 *	`mask`.  This is the second of the two paired tokens that
 *	guard secure-variable persistence; tampering it must be
 *	detected by the loader.
 *
 * Parameters:
 *	interp -- interpreter.  Must be non-NULL.
 *	mask   -- 64-bit XOR mask.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	`interp->nSecurePersistOk ^= mask`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL void
th8XorInterpSecurePersistOk(Th8_Interp *interp, th8_int64_t mask)
{
    interp->nSecurePersistOk ^= mask;
}

/*
 * Th8_AsyncState field perturbers (test-only).  Drive the
 * partial-init / mid-teardown defensive guards at:
 *   th8_core.c L2177 / L2250 (`!pState->bMutexReady`)
 *   th8_core.c L2645      (`pEventHandle && xEventDestroy`)
 *   th8_core.c L2651      (`bMutexReady && xMutexFinal`)
 *   th8_core.c L2709      (TH8_CHECK_EVENT_CALLBACKS)
 *
 * The struct Th8_AsyncState is opaque outside src/th8_int_core.h,
 * so testlib cannot mutate fields directly.  These XOR helpers
 * provide a scoped perturbation that testlib calls once to enter
 * the bad-state window and again to exit it.
 *
 * Each helper is one line (no MC/DC decisions), so no production
 * coverage is lost by moving the mutation here.
 */

/*
 *----------------------------------------------------------------------
 *
 * th8AsyncStateXorBMutexReady --
 *
 *	Test-only perturber for the async-state `bMutexReady`
 *	flag.  XORs `mask` into `pState->bMutexReady`, letting
 *	testlib toggle the flag into (and back out of) the
 *	partial-init / mid-teardown window that drives the
 *	`!pState->bMutexReady` and `bMutexReady && xMutexFinal`
 *	defensive guards.
 *
 * Parameters:
 *	pState -- async state to perturb.  Must be non-NULL.
 *	mask   -- XOR mask applied to bMutexReady.
 *
 * Returns:
 *	The previous value of `bMutexReady`, so the caller can
 *	restore or assert on it.
 *
 * Side effects:
 *	`pState->bMutexReady ^= mask`.
 *
 *----------------------------------------------------------------------
 */
TH8_INTERNAL int
th8AsyncStateXorBMutexReady(Th8_AsyncState *pState, int mask)
{
    int old = pState->bMutexReady;
    pState->bMutexReady ^= mask;
    return old;
}

/*
 * th8XchgInterpPlatform --
 *	Exchange interp->pPlatform with a new (possibly NULL)
 *	pointer.  Returns the old platform pointer so testlib can
 *	NULL the field, immediately invoke a malloc / realloc to
 *	drive the L967 / L1585 (F,T) defensive guards, then
 *	restore the original platform.  Must be called in
 *	balanced pairs with no intervening platform-dependent
 *	work (any allocation in the NULL window will fail).
 */

TH8_INTERNAL Th8_Platform *
th8XchgInterpPlatform(Th8_Interp *interp, Th8_Platform *pNew)
{
    Th8_Platform *pOld = interp->pPlatform;
    interp->pPlatform = pNew;
    return pOld;
}

/*
 * th8AsyncStateScrubField --
 *	Zero one of pState's finalize-time defensive fields so
 *	testlib can drive the (F,-) / (T,F) MC/DC vectors at
 *	th8_core.c L2645 + L2651 + L2709 by calling
 *	Th8_FinalizeAsyncState afterward.  Caller does NOT restore
 *	(finalize frees the pState); each test-only call leaks at
 *	most one mutex or event handle on the platform side, which
 *	is acceptable for the bounded test run.
 *	field == 0: pEventHandle  -> NULL
 *	field == 1: xEventDestroy -> NULL
 *	field == 2: xMutexFinal   -> NULL
 */
TH8_INTERNAL void
th8AsyncStateScrubField(Th8_AsyncState *pState, int field)
{
    switch (field) {
    case 0:
	pState->pEventHandle = NULL;
	break;
    case 1:
	pState->xEventDestroy = NULL;
	break;
    case 2:
	pState->xMutexFinal = NULL;
	break;
    default:
	break;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Tokenizer --
 *
 * Why / How:
 *	Tracks brace and bracket nesting in a single pass.  Braces
 *	inside brackets and brackets inside braces are treated as
 *	literal characters.  Backslash-escaped characters are
 *	skipped unconditionally.  Returns error on unmatched
 *	delimiters.
 *	Functions to scan script text and identify structural
 *	elements: whitespace, words, escape sequences, variable
 *	references, and command boundaries.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8NextSpace --
 *
 *	Count leading whitespace characters (excluding newlines).
 *
 * Why / How:
 *	Scans horizontal whitespace used as word separators within a
 *	command line.  Backslash-newline continuations are treated as
 *	whitespace (consuming the backslash, newline, and any
 *	following spaces/tabs).  Newlines themselves are NOT consumed
 *	because they serve as command terminators.
 *
 * Results:
 *	TH8_OK.  *pnSpace set to byte count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8NextSpace(
    Th8_Interp *interp, /* Interpreter (unused). */
    const char *zInput, /* Input string. */
    size_t nInput, /* Input length. */
    size_t *pnSpace) /* OUT: bytes of whitespace. */
{
    size_t i;

    (void)interp;
    i = 0;
    while (i < nInput) {
	if (zInput[i] == '\\' && i + 1 < nInput && zInput[i + 1] == '\n') {
	    /*
	     * Backslash-newline continuation: skip the
	     * backslash, newline, and following spaces/tabs.
	     */

	    i += 2;
	    while (i < nInput && (zInput[i] == ' ' || zInput[i] == '\t')) {
		i++;
	    }
	} else if (th8IsSpace(zInput[i])) {
	    i++;
	} else {
	    break;
	}
    }
    *pnSpace = i;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8EndOfLine --
 *
 *	Check if remaining input (after skipping horizontal
 *	whitespace) is a newline or end-of-input.
 *
 * Results:
 * Why / How:
 *	Handles three word forms: double-quoted (scan to unescaped
 *	closing quote), brace-delimited (track brace nesting), and
 *	bare (terminated by whitespace or backslash-newline at the
 *	top nesting level).  Bracket nesting inside braces is
 *	ignored and vice versa.
 *	1 if at end of line, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8EndOfLine(
    const char *zInput, /* Input string. */
    size_t nInput) /* Input length. */
{
    size_t i;

    for (i = 0; i < nInput && zInput[i] != '\n' && th8IsSpace(zInput[i]);
         i++) {
	/* skip horizontal whitespace */
    }
    return (i == nInput || zInput[i] == '\n') ? 1 : 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NextEscape --
 *
 *	Determine the byte length of a backslash escape sequence.
 *
 * Why / How:
 *	Dispatches on the character after the backslash to determine
 *	the escape length: fixed-length for named escapes (\\n, etc.),
 *	variable-length for hex (\\x), Unicode (\\u, \\U), octal, and
 *	backslash-newline continuation sequences.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if truncated.
 *	*pnEscape set to byte count of the escape sequence.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8NextEscape(
    Th8_Interp *interp, /* Interpreter. */
    const char *zInput, /* Input (starts with '\'). */
    size_t nInput, /* Bytes available. */
    size_t *pnEscape) /* OUT: bytes in escape seq. */
{
    size_t i = 2;

    if (nInput <= 1) {
	return TH8_ERROR;
    }

    switch (zInput[1]) {
    case 'a':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
    case 'v':
    case '\\':
	i = 2;
	break;
    case 'x':
	/*
	 * \xhh -- consume all hex digits, but only last
	 * two are significant (Tcl 8.4 rule).
	 */
	i = 2;
	while (i < nInput && th8IsHexDig(zInput[i])) {
	    i++;
	}
	if (i == 2) {
	    i = 2; /* \x with no digits = literal 'x' */
	}
	break;
    case 'u':
	/*
	 * \uhhhh -- 1 to 4 hex digits.
	 */
	i = 2;
	while (i < nInput && i < 6 && th8IsHexDig(zInput[i])) {
	    i++;
	}
	break;
    case 'U':
	/*
	 * \Uhhhhhhhh -- 1 to 8 hex digits (full Unicode).
	 */
	i = 2;
	while (i < nInput && i < 10 && th8IsHexDig(zInput[i])) {
	    i++;
	}
	break;
    case '\n':
	/*
	 * Backslash-newline continuation: consume the newline
	 * and all following spaces/tabs.
	 */
	i = 2;
	while (i < nInput && (zInput[i] == ' ' || zInput[i] == '\t')) {
	    i++;
	}
	break;
    default:
	if (th8IsOctDig(zInput[1])) {
	    /*
	     * \ooo -- 1 to 3 octal digits.
	     */
	    i = 2;
	    while (i < nInput && i < 4 && th8IsOctDig(zInput[i])) {
		i++;
	    }
	} else {
	    i = 2; /* \c = literal c */
	}
	break;
    }

    if (i > nInput) {
	return TH8_ERROR;
    }
    *pnEscape = i;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NextVarName --
 *
 *	Determine the byte length of a variable reference starting
 *	with '$'.
 *
 * Why / How:
 *	Handles three forms: ${name}, $name, and $name(index).
 *	Namespace separators (::) within the name are consumed so
 *	qualified variable references like $::foo::bar work.
 *	Array subscripts are scanned with paren-depth tracking
 *	and backslash-escape awareness.
 *
 * Results:
 *	TH8_OK on success.  *pnVar set to byte count.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8NextVarName(
    Th8_Interp *interp, /* Interpreter. */
    const char *zInput, /* Input (starts with '$'). */
    size_t nInput, /* Bytes available. */
    size_t *pnVar) /* OUT: bytes in var ref. */
{
    size_t i;

    if (nInput < 2) {
	*pnVar = 1;
	return TH8_OK;
    }

    /*
     * Form: ${name}
     */

    if (zInput[1] == '{') {
	for (i = 2; i < nInput && zInput[i] != '}'; i++) {
	    /* scan to closing brace */
	}
	if (i >= nInput) {
	    return TH8_ERROR;
	}
	*pnVar = i + 1;
	return TH8_OK;
    }

    /*
     * Optional :: prefix for global/qualified variables.
     */

    i = 1;
    if (nInput > 2 && zInput[1] == ':' && zInput[2] == ':') {
	i = 3;
    }

    /*
     * Scan alphanumeric/underscore characters and "::"
     * separators for namespace-qualified variable names
     * like $::foo::bar::x.
     */

    while (i < nInput) {
	if (th8IsAlnum(zInput[i]) || NEVER(zInput[i] == '_')) {
	    i++;
	} else if (
	    i + 1 < nInput && zInput[i] == ':' && zInput[i + 1] == ':') {
	    i += 2;
	} else {
	    break;
	}
    }

    /*
     * Check for array subscript: name(index)
     */

    if (i < nInput && zInput[i] == '(') {
	size_t depth = 1;

	i++;
	while (i < nInput && depth > 0) {
	    if (zInput[i] == ')') {
		depth--;
	    } else if (zInput[i] == '\\' && i + 1 < nInput) {
		i++; /* skip escaped char in subscript */
	    }
	    if (depth > 0) {
		i++;
	    }
	}
	if (depth != 0) {
	    return TH8_ERROR;
	}
	i++; /* skip closing ')' */
    }

    *pnVar = i;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * TH8 SCRIPT PARSER --
 *
 *	The two functions below (th8NextCommand and th8NextWord),
 *	together with the Th8ParseStack helper, implement the
 *	twelve numbered parsing rules of the Tcl(n) reference at:
 *
 *	    https://www.tcl-lang.org/man/tcl8.6/TclCmd/Tcl.htm
 *
 *	Throughout the parser, comments cite these rules by number
 *	(e.g. "Rule [6]") and quote the man page verbatim where the
 *	exact wording matters.  The numbering and wording are taken
 *	from the Tcl 8.6 man page; the same rules are unchanged in
 *	8.4 -> 8.6 -> 9.x except for "Rule [5] argument expansion"
 *	which was added in 8.5 and "Rule [12] substitution and word
 *	boundaries" which is a clarification of pre-existing
 *	behavior.
 *
 *	Rule summary, paraphrased for navigation:
 *
 *	  [1]  Commands.  Semi-colons and newlines are command
 *	       separators unless quoted; close brackets are command
 *	       terminators during command substitution.
 *	  [2]  Evaluation.  Word splitting + substitution, then the
 *	       first word names a command procedure.
 *	  [3]  Words.  Words are separated by whitespace (newlines
 *	       are command separators, not word separators).
 *	  [4]  Double quotes.  Word starts with `"`, ends at the
 *	       next `"`; substitutions ARE performed inside.
 *	  [5]  Argument expansion.  `{*}` followed by a non-
 *	       whitespace character expands a list into the command.
 *	  [6]  Braces.  Word starts with `{`, ends at the matching
 *	       `}`; nested braces nest; `\{` and `\}` do NOT count
 *	       for nesting; NO substitutions inside braces except
 *	       backslash-newline.
 *	  [7]  Command substitution.  `[...]` invokes the parser
 *	       recursively; the result replaces the brackets.
 *	  [8]  Variable substitution.  `$name`, `$name(idx)`, or
 *	       `${name}` is replaced by the variable's value.
 *	  [9]  Backslash substitution.  `\X` where X is a recognised
 *	       escape produces the corresponding byte; otherwise
 *	       the backslash is dropped and X is taken literally.
 *	  [10] Comments.  `#` at command position runs to newline.
 *	  [11] Order of substitution.  Each char is processed
 *	       exactly once; substitution proceeds left-to-right.
 *	  [12] Substitution and word boundaries.  Substitutions do
 *	       NOT split or merge words (except Rule [5]).
 *
 *	Rules [1], [2], [10], [11], [12] are properties of the
 *	overall eval loop rather than the byte-by-byte parsers in
 *	this file; the parsers below implement [3], [4], [5], [6],
 *	[7], [8] (delimiter recognition only -- value substitution
 *	happens in th8SubstWord), and [9] (skip-only -- value
 *	substitution happens in th8SubstWord).
 *
 *----------------------------------------------------------------------
 */


/*
 *----------------------------------------------------------------------
 *
 * Th8ParseStack -- nesting context stack for the parser
 *
 *	A small bounded LIFO of context delimiters used by the
 *	Tcl-style parsers (th8NextCommand and th8NextWord) to track
 *	whether the current byte position is inside braces (Rule
 *	[6]), brackets (Rule [7]), or double quotes (Rule [4]).
 *	The first TH8_PARSE_NEST_INLINE entries live in an inline
 *	byte array on the caller's stack frame so the common
 *	(shallow-nesting) case allocates nothing on the heap.  When
 *	that capacity is exceeded the storage falls back to a
 *	doubling heap allocation -- there is no hard ceiling on
 *	nesting depth other than what Th8_AttemptRealloc accepts.
 *
 *	The stack pushes one byte per scope: '{', '[' or '"', so the
 *	innermost still-open scope tells the parser which rule's
 *	character classification applies to the next input byte.
 *	This is the only structural difference from the recursive
 *	parser implied by the Tcl(n) prose; the reachable language
 *	is identical.
 *
 *	Lifecycle: th8ParseStackInit sets the inline array as the
 *	active storage; th8ParseStackPush grows on demand;
 *	th8ParseStackFree releases any heap allocation made by push
 *	(and is a no-op if the inline array is still in use).
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8ParseStackInit --
 *
 *	Initialise a Th8ParseStack to empty, backed by its caller-
 *	provided inline buffer.  Must be called before any
 *	th8ParseStackPush so the parsers start with a valid,
 *	heap-free stack.
 *
 * Why / How:
 *	Points `s->p` at the inline array embedded in the struct,
 *	sets `s->cap` to TH8_PARSE_NEST_INLINE, and clears `s->top`
 *	to zero.  No allocation occurs; the shallow-nesting common
 *	case therefore touches only stack memory (th8ParseStackPush
 *	promotes to a heap buffer only when this capacity is
 *	exceeded).
 *
 * Parameters:
 *	s -- parse stack to initialise.  Must be non-NULL and own a
 *	     valid `inlineBuf`.
 *
 * Returns:
 *	None.
 *
 * Side effects:
 *	Sets `s->p`, `s->cap`, and `s->top`.  Does not allocate.
 *
 *----------------------------------------------------------------------
 */
static void
th8ParseStackInit(Th8ParseStack *s)
{
    s->p = s->inlineBuf;
    s->cap = TH8_PARSE_NEST_INLINE;
    s->top = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ParseStackPush --
 *
 *	Push one scope-marker byte (`'{'`, `'['`, or `'"'`) onto the
 *	parse stack `s`.  Grows the backing storage on demand: when
 *	the in-place inline buffer is exhausted, allocates a heap
 *	buffer; subsequent overflows double the heap buffer.  The
 *	doubling capacity-check guards against `size_t` overflow.
 *
 * Parameters:
 *	interp -- interpreter used for the (attempted) allocation.
 *	s      -- parse stack to push onto.  Must have been
 *		  initialised via `th8ParseStackInit`.
 *	c      -- scope marker byte to push.
 *
 * Returns:
 *	TH8_OK on success.
 *	TH8_ERROR if the doubling capacity would overflow `size_t`,
 *	  or if the underlying `TH8_ATTEMPT_REALLOC` returns NULL
 *	  (out-of-memory).  On TH8_ERROR the stack is left
 *	  unchanged.
 *
 * Side effects:
 *	May allocate or reallocate `s->p` (when transitioning from
 *	inline to heap, or when growing the heap buffer).  Increments
 *	`s->top` and updates `s->cap` on success.
 *
 *----------------------------------------------------------------------
 */
static int
th8ParseStackPush(Th8_Interp *interp, Th8ParseStack *s, char c)
{
    if (s->top >= s->cap) {
	size_t newCap;
	char *newP;

	/* Bug 26 (2026-06-07): plain if -- overflow IS the hazard. */
	if (s->cap > ((size_t)-1) / 2) {
	    return TH8_ERROR;
	}
	newCap = s->cap * 2;

	if (s->p == s->inlineBuf) {
	    newP = TH8_ATTEMPT_REALLOC(interp, NULL, newCap);
	    if (!newP) {
		return TH8_ERROR;
	    }
	    Th8_Memcpy(interp, newP, s->inlineBuf, s->top);
	} else {
	    newP = TH8_ATTEMPT_REALLOC(interp, s->p, newCap);
	    if (!newP) {
		return TH8_ERROR;
	    }
	}
	s->p = newP;
	s->cap = newCap;
    }
    s->p[s->top++] = c;
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * th8ParseStackFree --
 *
 *	Release any heap allocation made by `th8ParseStackPush`
 *	and reset the stack to its inline-buffer state.  Idempotent
 *	when the stack never spilled to the heap (i.e., when the
 *	inline capacity was sufficient for the parser's whole run);
 *	in that case it is a no-op.
 *
 * Parameters:
 *	interp -- interpreter used for the (possible) free.
 *	s      -- parse stack to release.  Must have been
 *		  initialised; double-free is safe because the
 *		  reset below leaves `s->p == s->inlineBuf`, which
 *		  guards the next call.
 *
 * Returns:
 *	Nothing.
 *
 * Side effects:
 *	Frees `s->p` if it points outside the inline buffer.
 *	Resets `s->p` to the inline buffer and `s->cap` to
 *	`TH8_PARSE_NEST_INLINE`.  Does NOT touch `s->top`; callers
 *	are expected to discard the stack contents at this point.
 *
 *----------------------------------------------------------------------
 */
static void
th8ParseStackFree(Th8_Interp *interp, Th8ParseStack *s)
{
    if (s->p != s->inlineBuf) {
	Th8_Free(interp, s->p);
	s->p = s->inlineBuf;
	s->cap = TH8_PARSE_NEST_INLINE;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8NextCommand --
 *
 *	Determine the byte length of a bracketed or braced block.
 *	Input must start with '[' or '{'.  Used by the substitution
 *	engine to find the matching close-bracket of a command
 *	substitution (Rule [7]) and the matching close-brace of a
 *	brace-quoted word (Rule [6]).
 *
 * Why / How:
 *	Tracks nesting with a context stack so that brace, bracket,
 *	and quote scopes interleave correctly per the Tcl(n) man
 *	page rules.  Each stack entry records which delimiter
 *	opened the current scope, and the rule for that scope
 *	dictates which subsequent characters are significant:
 *
 *	  '{' -- Rule [6], brace-quoted.  Quoting Tcl(n):
 *	         "No substitutions are performed on the characters
 *	         between the braces except for backslash-newline
 *	         substitutions described below, nor do semi-colons,
 *	         newlines, close brackets, or white space receive
 *	         any special interpretation."  Only '\', '{', '}'
 *	         affect parsing; everything else is literal.  Per
 *	         Rule [6] continuation: "if an open brace or close
 *	         brace within the word is quoted with a backslash
 *	         then it is not counted in locating the matching
 *	         close brace" -- implemented by the '\\'-then-skip
 *	         branch.
 *
 *	  '[' -- Rule [7], command substitution.  Quoting Tcl(n):
 *	         "Tcl performs command substitution. To do this it
 *	         invokes the Tcl interpreter recursively to process
 *	         the characters following the open bracket as a Tcl
 *	         script.  The script may contain any number of
 *	         commands and must be terminated by a close bracket
 *	         ("]")."  Inside `[...]` the FULL Tcl rules apply,
 *	         so '"' (Rule [4]), '{' (Rule [6]), '[' (nested
 *	         Rule [7]) all open further scopes.
 *
 *	  '"' -- Rule [4], double-quoted.  Quoting Tcl(n):  "If
 *	         semi-colons, close brackets, or white space
 *	         characters (including newlines) appear between the
 *	         quotes then they are treated as ordinary characters
 *	         and included in the word.  Command substitution,
 *	         variable substitution, and backslash substitution
 *	         are performed on the characters between the quotes
 *	         as described below."  Only '\', '"', and '[' are
 *	         significant for parsing; ']' is literal because no
 *	         matching '[' was opened from inside the quote.
 *
 *	Backslash handling everywhere is Rule [9]: "the backslash
 *	is dropped and the following character is treated as an
 *	ordinary character."  At parse time the parser only needs
 *	to skip the next byte; full backslash substitution (octal,
 *	hex, \n etc.) is deferred to th8SubstWord.
 *
 *	Empty input or input that does not start with '{' or '[' is
 *	a programming-contract violation and reported as an error.
 *
 *	A context stack (not a flat counter) is required to handle
 *	interleaved cases such as `[string match {[abc} a]`, where
 *	the bracket inside the brace must be treated as literal.
 *
 * Results:
 *	TH8_OK on success.  *pnCmd set to byte count including
 *	delimiters.
 *
 * Side effects:
 *	Sets error message on unmatched delimiters, contract
 *	violations, or out-of-memory while growing the nesting
 *	stack.
 *
 *----------------------------------------------------------------------
 */

int
th8NextCommand(
    Th8_Interp *interp, /* Interpreter. */
    const char *zInput, /* Input (starts with '[' or '{'). */
    size_t nInput, /* Bytes available. */
    size_t *pnCmd) /* OUT: bytes including delimiters. */
{
    Th8ParseStack stack;
    size_t i;
    char first;
    int rc = TH8_OK;

    th8ParseStackInit(&stack);

    if (nInput == 0) {
	Th8_ErrorMessage(interp, "Empty input to th8NextCommand:", "", 0);
	return TH8_ERROR;
    }

    first = zInput[0];
    if (first != '{' && first != '[') {
	Th8_ErrorMessage(
	    interp, "Expected '{' or '[' at start of:", zInput, nInput);
	return TH8_ERROR;
    }

    if (th8ParseStackPush(interp, &stack, first) != TH8_OK) {
	Th8_ErrorMessage(interp, "Parser nesting too deep:", zInput, nInput);
	return TH8_ERROR;
    }

    for (i = 1; i < nInput && stack.top > 0; i++) {
	char c = zInput[i];
	char ctx = stack.p[stack.top - 1];

	if (ctx == '{') {
	    /*
	     * Brace context (Rule [6]).  Per Tcl(n): "No
	     * substitutions are performed on the characters
	     * between the braces ... nor do semi-colons,
	     * newlines, close brackets, or white space receive
	     * any special interpretation."  Only '\', '{', '}'
	     * affect parsing.
	     */
	    if (c == '\\') {
		/*
		 * Rule [6] continuation: "if an open brace or
		 * close brace within the word is quoted with a
		 * backslash then it is not counted in locating
		 * the matching close brace."  We achieve that
		 * generally by skipping ANY byte after a
		 * backslash -- if it happened to be '{' or '}'
		 * the brace counter is correctly bypassed; for
		 * any other byte the skip is a no-op for
		 * counting purposes.
		 */
		if (i + 1 < nInput) i++;
	    } else if (c == '{') {
		/* Rule [6]: "Braces nest within the word: for
		 * each additional open brace there must be an
		 * additional close brace." */
		if (th8ParseStackPush(interp, &stack, '{') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    } else if (c == '}') {
		/* Rule [6]: matching close-brace; pop one level. */
		stack.top--;
	    }
	} else if (ctx == '[') {
	    /*
	     * Bracket context (Rule [7], command substitution).
	     * Per Tcl(n): "it invokes the Tcl interpreter
	     * recursively to process the characters following
	     * the open bracket as a Tcl script."  The full Tcl
	     * grammar applies inside, so all three sub-scope
	     * openers ('"', '{', '[') and the backslash escape
	     * are honored.
	     */
	    if (c == '\\') {
		/* Rule [9] backslash skip; full substitution is
		 * deferred to th8SubstWord. */
		if (i + 1 < nInput) i++;
	    } else if (c == '"') {
		/* Rule [4] opens a double-quoted sub-word. */
		if (th8ParseStackPush(interp, &stack, '"') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    } else if (c == '{') {
		/* Rule [6] opens a brace-quoted sub-word. */
		if (th8ParseStackPush(interp, &stack, '{') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    } else if (c == '[') {
		/* Rule [7] nested command substitution. */
		if (th8ParseStackPush(interp, &stack, '[') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    } else if (c == ']') {
		/* Rule [7]: "must be terminated by a close
		 * bracket"; pop the bracket scope. */
		stack.top--;
	    }
	} else {
	    /*
	     * Double-quote context (Rule [4]).  Per Tcl(n): "If
	     * semi-colons, close brackets, or white space
	     * characters (including newlines) appear between
	     * the quotes then they are treated as ordinary
	     * characters and included in the word.  Command
	     * substitution, variable substitution, and backslash
	     * substitution are performed on the characters
	     * between the quotes."  Only '\', '"', and '[' are
	     * significant for parsing; ']' explicitly listed
	     * above is "ordinary" and therefore literal.
	     */
	    if (c == '\\') {
		if (i + 1 < nInput) i++;
	    } else if (c == '"') {
		/* Rule [4]: "the word is terminated by the next
		 * double-quote character." */
		stack.top--;
	    } else if (c == '[') {
		/* Rule [7] command substitution can appear inside
		 * a double-quoted word. */
		if (th8ParseStackPush(interp, &stack, '[') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    }
	}
    }

    if (stack.top > 0) {
	/*
	 * Report based on the innermost still-open scope so the
	 * error message points at what is actually missing
	 * rather than the outermost wrapper.  This matches the
	 * recursive-descent parser implied by Tcl(n), which
	 * surfaces the deepest mismatch first ("missing
	 * close-brace", "missing close-bracket", "missing \"").
	 */
	char inner = stack.p[stack.top - 1];

	if (inner == '{') {
	    Th8_ErrorMessage(interp, "Unmatched braces:", zInput, nInput);
	} else if (inner == '[') {
	    Th8_ErrorMessage(interp, "Unmatched brackets:", zInput, nInput);
	} else {
	    Th8_ErrorMessage(interp, "Unmatched quote:", zInput, nInput);
	}
	rc = TH8_ERROR;
	goto done;
    }

    *pnCmd = i;

done:
    th8ParseStackFree(interp, &stack);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NextWord --
 *
 *	Determine the byte length of the next word in the input.
 *	Implements word boundary detection per Rule [3] (Words):
 *	"Words of a command are separated by white space (except
 *	for newlines, which are command separators)."  In addition
 *	to whitespace, when isCmd is true, ';' (Rule [1] command
 *	separator) terminates the word.
 *
 *	The first character of the word selects which Rule applies:
 *
 *	  '"' -- Rule [4] double quotes.
 *	  '{' -- Rule [6] braces (and Rule [5] argument-expansion
 *	         prefix `{*}`, which the eval loop in th8EvalEx
 *	         recognises after this function returns the entire
 *	         word's byte length).
 *	  anything else -- a bare word; the parser scans until the
 *	         first unescaped Rule [3] separator at top level.
 *	         Substitutions inside a bare word (Rules [7] command
 *	         sub, [8] variable sub, [9] backslash sub) are
 *	         performed by th8SubstWord on the bytes this
 *	         function counts.
 *
 *	The same context-stack approach as th8NextCommand is used
 *	so that interleaved brace/bracket/quote sub-scopes nest
 *	correctly.  See the th8NextCommand block comment for the
 *	per-context character-significance tables and the
 *	verbatim Tcl(n) excerpts they implement.
 *
 *	Strict conformance: every unmatched opener (brace, bracket,
 *	or quote) is reported as an error.  The previous lenient
 *	special case that returned success on an unterminated
 *	quote-delimited word at end-of-input has been removed --
 *	Tcl proper raises "missing \"" in the same situation per
 *	Rule [4], and masking that here would defeat callers'
 *	ability to surface the syntax error to the user.
 *
 *	Tcl 8.6 reference: https://www.tcl-lang.org/man/tcl8.6/TclCmd/Tcl.htm
 *
 * Results:
 *	TH8_OK on success.  *pnWord set to byte count.
 *
 * Side effects:
 *	Sets error message on mismatched delimiters or out-of-memory
 *	while growing the nesting stack.
 *
 *----------------------------------------------------------------------
 */

static int
th8NextWord(
    Th8_Interp *interp, /* Interpreter. */
    const char *zInput, /* Input string. */
    size_t nInput, /* Bytes available. */
    size_t *pnWord, /* OUT: bytes in word. */
    int isCmd) /* True if ';' terminates word. */
{
    Th8ParseStack stack;
    size_t iEnd = 0;
    char first;
    int rc = TH8_OK;

    th8ParseStackInit(&stack);

    if (nInput == 0) {
	*pnWord = 0;
	return TH8_OK;
    }

    first = zInput[0];
    if (first == '"' || first == '{') {
	/*
	 * Word starts with '"' (Rule [4]) or '{' (Rule [6]); push
	 * the matching scope onto the stack and advance past the
	 * opening delimiter.  A '{' here may be the start of a
	 * brace-quoted word OR the prefix of Rule [5] argument
	 * expansion (`{*}`); both forms scan to the matching '}'
	 * the same way -- the eval loop in th8EvalEx inspects the
	 * returned bytes to decide whether to apply Rule [5] vs.
	 * just dropping the outer braces per Rule [6].
	 */
	if (th8ParseStackPush(interp, &stack, first) != TH8_OK) {
	    Th8_ErrorMessage(
	        interp, "Parser nesting too deep:", zInput, nInput);
	    return TH8_ERROR;
	}
	iEnd = 1;
    }

    while (iEnd < nInput) {
	char c = zInput[iEnd];

	if (stack.top == 0) {
	    /*
	     * Top level (bare word).  Per Rule [3]: "Words of a
	     * command are separated by white space (except for
	     * newlines, which are command separators)."  Newlines
	     * are returned by th8IsSpace as whitespace because at
	     * the WORD level they end the word too -- the eval
	     * loop separately recognises them as command
	     * separators per Rule [1].  When isCmd is set, ';'
	     * also ends the word per Rule [1].
	     */

	    if (th8IsSpace(c)) break;
	    if (isCmd && c == ';') break;

	    if (c == '\\') {
		/*
		 * Rule [9] backslash substitution.  The man page
		 * lists "\<newline>whiteSpace" as a special
		 * sequence that "is replaced by a single space
		 * character"; in practice that single space ends
		 * the bare word at the same position regardless,
		 * so we simply break here and let th8SubstWord
		 * fold the continuation into a separator.
		 *
		 * For all other "\X" sequences inside a bare
		 * word, Rule [9] says: "the backslash is dropped
		 * and the following character is treated as an
		 * ordinary character and included in the word."
		 * The parser's job is just to skip the next byte
		 * so that an escaped whitespace, '{', '[', '"',
		 * or ';' does NOT terminate the word; full
		 * substitution happens later in th8SubstWord.
		 */
		if (iEnd + 1 < nInput) {
		    if (zInput[iEnd + 1] == '\n') break;
		    iEnd++;
		}
	    } else if (c == '{') {
		/*
		 * Rule [6] open-brace mid-bare-word also pushes
		 * a brace scope here so the matching close-brace
		 * is found inside its own context (e.g. the
		 * `{a b}` operand seen mid-word).  Tcl proper
		 * treats mid-word '{' as literal but TH8 has
		 * historically extended Rule [6] to apply at any
		 * point in a bare word, and existing scripts
		 * rely on that extension.
		 */
		if (th8ParseStackPush(interp, &stack, '{') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    } else if (c == '[') {
		/*
		 * Rule [7] open-bracket triggers command
		 * substitution at any point in a bare word.
		 * Per Tcl(n): "There may be any number of command
		 * substitutions in a single word."
		 */
		if (th8ParseStackPush(interp, &stack, '[') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    } else if (c == '"') {
		/* Rule [4] double-quote mid-bare-word; scan to
		 * the matching close-quote within its own
		 * context (the same Tcl-extension as the brace
		 * case above). */
		if (th8ParseStackPush(interp, &stack, '"') != TH8_OK) {
		    Th8_ErrorMessage(
		        interp, "Parser nesting too deep:", zInput, nInput);
		    rc = TH8_ERROR;
		    goto done;
		}
	    }
	    /*
	     * Stray '}' or ']' at top level are literal bytes of
	     * the bare word.  Per Rule [3] only whitespace ends
	     * the word, and ']' is only special as a command-sub
	     * terminator (Rule [7]) which requires a matching
	     * '[' to be open -- there is none here.  '}' has no
	     * special meaning outside a brace scope.
	     */
	} else {
	    char ctx = stack.p[stack.top - 1];

	    if (ctx == '{') {
		/*
		 * Brace context (Rule [6]).  See th8NextCommand
		 * for the verbatim Tcl(n) excerpt; here it
		 * suffices to note that only '\', '{', '}'
		 * affect parsing inside braces.
		 */
		if (c == '\\') {
		    /* Rule [6] cont.: "if an open brace or close
		     * brace within the word is quoted with a
		     * backslash then it is not counted in
		     * locating the matching close brace." */
		    if (iEnd + 1 < nInput) iEnd++;
		} else if (c == '{') {
		    /* Rule [6]: "Braces nest within the word." */
		    if (th8ParseStackPush(interp, &stack, '{') != TH8_OK) {
			Th8_ErrorMessage(
			    interp, "Parser nesting too deep:", zInput,
			    nInput);
			rc = TH8_ERROR;
			goto done;
		    }
		} else if (c == '}') {
		    /* Rule [6] matching close-brace; pop. */
		    stack.top--;
		}
	    } else if (ctx == '[') {
		/*
		 * Bracket context (Rule [7]).  Full Tcl
		 * grammar applies; '"', '{', '[' all open
		 * sub-scopes; ']' closes; '\' skips one byte.
		 */
		if (c == '\\') {
		    /* Rule [9] backslash skip. */
		    if (iEnd + 1 < nInput) iEnd++;
		} else if (c == '"') {
		    /* Rule [4] inside Rule [7]. */
		    if (th8ParseStackPush(interp, &stack, '"') != TH8_OK) {
			Th8_ErrorMessage(
			    interp, "Parser nesting too deep:", zInput,
			    nInput);
			rc = TH8_ERROR;
			goto done;
		    }
		} else if (c == '{') {
		    /* Rule [6] inside Rule [7]. */
		    if (th8ParseStackPush(interp, &stack, '{') != TH8_OK) {
			Th8_ErrorMessage(
			    interp, "Parser nesting too deep:", zInput,
			    nInput);
			rc = TH8_ERROR;
			goto done;
		    }
		} else if (c == '[') {
		    /* Rule [7] nested command sub. */
		    if (th8ParseStackPush(interp, &stack, '[') != TH8_OK) {
			Th8_ErrorMessage(
			    interp, "Parser nesting too deep:", zInput,
			    nInput);
			rc = TH8_ERROR;
			goto done;
		    }
		} else if (c == ']') {
		    /* Rule [7]: "must be terminated by a close
		     * bracket"; pop the bracket scope. */
		    stack.top--;
		}
	    } else {
		/*
		 * Quote context (Rule [4]).  Per Tcl(n): "If
		 * semi-colons, close brackets, or white space
		 * characters (including newlines) appear between
		 * the quotes then they are treated as ordinary
		 * characters and included in the word."  ']' is
		 * therefore literal here.  Backslash, command
		 * sub, and the closing '"' are the only
		 * significant characters at parse time.
		 */
		if (c == '\\') {
		    /* Rule [9] backslash skip. */
		    if (iEnd + 1 < nInput) iEnd++;
		} else if (c == '"') {
		    /* Rule [4] matching close-quote; pop. */
		    stack.top--;
		} else if (c == '[') {
		    /* Rule [7]: "Command substitution ... is
		     * performed on the characters between the
		     * quotes." */
		    if (th8ParseStackPush(interp, &stack, '[') != TH8_OK) {
			Th8_ErrorMessage(
			    interp, "Parser nesting too deep:", zInput,
			    nInput);
			rc = TH8_ERROR;
			goto done;
		    }
		}
	    }
	}

	iEnd++;
    }

    if (stack.top > 0) {
	/*
	 * Innermost-first error reporting (mirrors th8NextCommand
	 * and matches the recursive parser implied by Tcl(n)):
	 * report based on the deepest still-open scope so the
	 * message names what is actually missing.  The Tcl 8.6
	 * reference parser raises "missing close-brace" (Rule
	 * [6]), "missing close-bracket" (Rule [7]), or
	 * "missing \"" (Rule [4]) in these cases respectively;
	 * the wording differs but the discrimination is the same.
	 */
	char inner = stack.p[stack.top - 1];

	if (inner == '{') {
	    Th8_ErrorMessage(interp, "Unmatched braces:", zInput, nInput);
	} else if (inner == '[') {
	    Th8_ErrorMessage(interp, "Unmatched brackets:", zInput, nInput);
	} else {
	    Th8_ErrorMessage(interp, "Unmatched quote:", zInput, nInput);
	}
	rc = TH8_ERROR;
	goto done;
    }

    *pnWord = iEnd;

done:
    th8ParseStackFree(interp, &stack);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Substitution engine --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8HexVal --
 *
 *	Convert a hex digit character to its integer value.
 *
 * Why / How:
 *	Branchless table-free conversion: three range checks cover
 *	0-9, a-f, and A-F.  Returns -1 for non-hex characters so
 *	callers can detect invalid input.
 *
 * Results:
 *	0-15, or -1 if not a hex digit.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
th8HexVal(char c) /* Character to convert. */
{
    if (ALWAYS(c >= '0') && c <= '9') return c - '0';
    if (c >= 'a' && ALWAYS(c <= 'f')) return c - 'a' + 10;
    if (ALWAYS(c >= 'A' && c <= 'F')) return c - 'A' + 10;
    return -1;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SubstEscape --
 *
 *	Perform backslash substitution on an escape sequence and
 *	write the result to the output buffer.
 *
 * Why / How:
 *	Maps the escape character after the backslash to the
 *	corresponding byte value (\\n, \\t, \\x, \\u, \\U, octal).
 *	For \\x, only the last two hex digits are significant per
 *	Tcl 8.4 rules.  Unicode escapes are clamped to U+10FFFF and
 *	surrogates are replaced with U+FFFD before UTF-8 encoding.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends the substituted character(s) to pOutput.
 *
 *----------------------------------------------------------------------
 */

static void
th8SubstEscape(
    Th8_Interp *interp, /* Interpreter for memory. */
    Th8_Buffer *pOutput, /* Output buffer. */
    const char *zEsc, /* Escape sequence (starts with '\'). */
    size_t nEsc) /* Byte length of escape sequence. */
{
    char c;

    /*
     * Defensive: nEsc must be >= 2 (backslash + at least one char).
     * The caller (th8NextEscape) guarantees this, but we guard
     * against misuse to prevent out-of-bounds read.
     */

    if (nEsc < 2) {
	th8BufAddChar(interp, pOutput, '\\');
	return;
    }

    switch (zEsc[1]) {
    case 'a':
	c = '\007';
	th8BufAddChar(interp, pOutput, c);
	break;
    case 'b':
	c = '\010';
	th8BufAddChar(interp, pOutput, c);
	break;
    case 'f':
	c = '\014';
	th8BufAddChar(interp, pOutput, c);
	break;
    case 'n':
	c = '\012';
	th8BufAddChar(interp, pOutput, c);
	break;
    case 'r':
	c = '\015';
	th8BufAddChar(interp, pOutput, c);
	break;
    case 't':
	c = '\011';
	th8BufAddChar(interp, pOutput, c);
	break;
    case 'v':
	c = '\013';
	th8BufAddChar(interp, pOutput, c);
	break;
    case '\\':
	th8BufAddChar(interp, pOutput, '\\');
	break;
    case '\n':
	/* Backslash-newline continuation -> single space */
	th8BufAddChar(interp, pOutput, ' ');
	break;
    case 'x': {
	/*
	 * \xhh -- only the last two hex digits are used.
	 */
	int val = 0;
	size_t i;

	for (i = 2; i < nEsc; i++) {
	    val = (val << 4) | th8HexVal(zEsc[i]);
	}
	val &= 0xFF; /* Only last two hex digits */
	c = (char)val;
	th8BufAddChar(interp, pOutput, c);
	break;
    }
    case 'u':
    case 'U': {
	/*
	 * \uhhhh or \Uhhhhhhhh -- Unicode code point.
	 * Encode as UTF-8.
	 */
	int val = 0;
	size_t i;
	char utf8[4];
	int nBytes;

	for (i = 2; i < nEsc; i++) {
	    val = (val << 4) | th8HexVal(zEsc[i]);
	}
	/* Clamp to valid Unicode range */
	if (val > 0x10FFFF) val = 0xFFFD;
	if (val >= 0xD800 && val <= 0xDFFF) val = 0xFFFD;
	nBytes = Th8_Utf8Encode(val, utf8);
	th8BufWrite(interp, pOutput, utf8, (size_t)nBytes);
	break;
    }
    default:
	if (th8IsOctDig(zEsc[1])) {
	    /*
	     * \ooo -- octal value.
	     */
	    int val = 0;
	    size_t i;

	    for (i = 1; i < nEsc; i++) {
		val = (val << 3) | (zEsc[i] - '0');
	    }
	    val &= 0xFF;
	    c = (char)val;
	    th8BufAddChar(interp, pOutput, c);
	} else {
	    /* \c -> literal c */
	    th8BufAddChar(interp, pOutput, zEsc[1]);
	}
	break;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Utf8Encode --
 *
 *	Encode a Unicode code point as UTF-8.
 *
 * Why / How:
 *	Encodes a single code point using the standard UTF-8 bit
 *	patterns (1-4 bytes).  The caller must provide a buffer of
 *	at least 4 bytes.
 *
 * Results:
 *	Number of bytes written (1-4).
 *
 * Side effects:
 *	Writes to z[].
 *
 *----------------------------------------------------------------------
 */

int
Th8_Utf8Encode(
    int codepoint, /* Unicode code point. */
    char *z) /* Output buffer (>= 4 bytes). */
{
    if (codepoint < 0x80) {
	z[0] = (char)codepoint;
	return 1;
    } else if (codepoint < 0x800) {
	z[0] = (char)(0xC0 | (codepoint >> 6));
	z[1] = (char)(0x80 | (codepoint & 0x3F));
	return 2;
    } else if (codepoint < 0x10000) {
	z[0] = (char)(0xE0 | (codepoint >> 12));
	z[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
	z[2] = (char)(0x80 | (codepoint & 0x3F));
	return 3;
    } else {
	z[0] = (char)(0xF0 | (codepoint >> 18));
	z[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
	z[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
	z[3] = (char)(0x80 | (codepoint & 0x3F));
	return 4;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8SubstCommand --
 *
 *	Perform command substitution on a [...] sequence.
 *
 * Why / How:
 *	Strips the outer brackets, then delegates to Th8_Eval for
 *	the enclosed script.  The result of the evaluation becomes
 *	the substituted value.
 *
 * Results:
 *	Return code from evaluating the enclosed script.
 *
 * Side effects:
 *	Sets the interpreter result.
 *
 *----------------------------------------------------------------------
 */

static int
th8SubstCommand(
    Th8_Interp *interp, /* Interpreter. */
    const char *zWord, /* Word (starts with '['). */
    size_t nWord, /* Byte length including brackets. */
    const char *zName, /* Script origin (or NULL). */
    size_t nName) /* Origin name length. */
{
    /*
     * Extract the script between [ and ] and evaluate it.
     */

    return Th8_Eval(interp, 0, &zWord[1], nWord - 2, zName, nName);
}


/*
 *----------------------------------------------------------------------
 *
 * th8SubstWord --
 *
 *	Perform all applicable substitutions on a single word.
 *
 * Why / How:
 *	Dispatches on the first character of the word to choose the
 *	substitution strategy: braces suppress all substitution,
 *	quotes and bare words undergo backslash, variable ($), and
 *	command ([]) substitution in a single left-to-right pass.
 *	Results accumulate in a Th8_Buffer to avoid repeated realloc.
 *
 * Results:
 *	TH8_OK on success.  The interpreter result is set to the
 *	fully substituted word value.
 *
 * Side effects:
 *	May recursively evaluate scripts (for [...] substitution).
 *
 *----------------------------------------------------------------------
 */

int
th8SubstWord(
    Th8_Interp *interp, /* Interpreter. */
    const char *zWord, /* Word text. */
    size_t nWord, /* Byte length. */
    const char *zName, /* Script origin (or NULL). */
    size_t nName) /* Origin name length. */
{
    size_t nn = TH8_LEN(nWord);
    Th8_Buffer output;
    int rc = TH8_OK;

    th8BufInit(&output);

    /*
     * Brace-delimited word: no substitution except
     * backslash-newline.
     */

    if (nn > 1 && zWord[0] == '{' && ALWAYS(zWord[nn - 1] == '}')) {
	size_t i;

	for (i = 1; i < nn - 1; i++) {
	    if (zWord[i] == '\\' && i + 1 < nn - 1 && zWord[i + 1] == '\n') {
		/* Backslash-newline -> space */
		th8BufAddChar(interp, &output, ' ');
		i += 2;
		while (i < nn - 1 && (zWord[i] == ' ' || zWord[i] == '\t')) {
		    i++;
		}
		i--; /* loop will increment */
	    } else {
		th8BufAddChar(interp, &output, zWord[i]);
	    }
	}
	goto done;
    }

    /*
     * Quote-delimited or bare word.  Strip outer quotes if
     * present.
     */

    {
	size_t start = 0;
	size_t end = nn;

	if (nn > 1 && zWord[0] == '"' && zWord[nn - 1] == '"') {
	    start = 1;
	    end = nn - 1;
	}

	while (start < end) {
	    char c = zWord[start];

	    if (c == '\\') {
		/*
		 * Backslash substitution.
		 */

		size_t nEsc;

		if (th8NextEscape(
		        interp, &zWord[start], end - start, &nEsc) !=
		    TH8_OK) {
		    rc = TH8_ERROR;
		    goto done;
		}
		th8SubstEscape(interp, &output, &zWord[start], nEsc);
		start += nEsc;
	    } else if (c == '[' && ALWAYS(!interp->isListMode)) {
		/*
		 * Command substitution.
		 */

		size_t nCmd;
		size_t nRes;
		const char *zRes;

		if (th8NextCommand(
		        interp, &zWord[start], end - start, &nCmd) !=
		    TH8_OK) {
		    rc = TH8_ERROR;
		    goto done;
		}
		rc = th8SubstCommand(
		    interp, &zWord[start], nCmd, zName, nName);
		if (rc != TH8_OK) {
		    goto done;
		}
		zRes = Th8_GetResult(interp, &nRes);
		th8BufWrite(interp, &output, zRes, nRes);
		start += nCmd;
	    } else if (c == '$' && ALWAYS(!interp->isListMode)) {
		/*
		 * Variable substitution.
		 */

		size_t nVar;
		size_t nRes;
		const char *zRes;

		if (th8NextVarName(
		        interp, &zWord[start], end - start, &nVar) !=
		    TH8_OK) {
		    rc = TH8_ERROR;
		    goto done;
		}
		if (nVar <= 1) {
		    /* Bare '$' with no valid name -> literal */
		    th8BufAddChar(interp, &output, '$');
		    start++;
		} else {
#if defined(TH8_ENABLE_VARIABLES)
		    rc = th8SubstVarName(interp, &zWord[start], nVar);
#else
		    Th8_SetResult(
		        interp, "variable resolution not available",
		        TH8_NOLEN);
		    rc = TH8_ERROR;
#endif
		    if (rc != TH8_OK) {
			goto done;
		    }
		    zRes = Th8_GetResult(interp, &nRes);
		    th8BufWrite(interp, &output, zRes, nRes);
		    start += nVar;
		}
	    } else {
		th8BufAddChar(interp, &output, c);
		start++;
	    }
	}
    }

done:
    if (rc == TH8_OK && output.bFail) {
	/* A buffer append could not complete: fail instead of
	 * publishing a truncated word (Bug 61). */
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	rc = TH8_ERROR;
    }
    if (rc == TH8_OK) {
	/* Carry any taint accumulated from variable/command
	 * substitutions into the substituted word's result.  Propagate
	 * a publish (result-copy) allocation failure. */
	rc = Th8_SetResult(interp, output.zBuf, output.nBuf | output.nTag);
    }
    th8BufFree(interp, &output);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Subst --
 *
 *	Perform Tcl-style substitutions on a string.  Unlike the
 *	evaluator's th8SubstWord (which processes a single command
 *	word with quoting rules), this function processes arbitrary
 *	text -- braces have no special meaning, and substitution
 *	types can be selectively disabled via flags.
 *
 *	Command substitutions that return TH8_BREAK stop processing
 *	(result is everything substituted so far).  TH8_CONTINUE
 *	replaces the command result with the empty string and
 *	continues.  TH8_RETURN uses the return value and continues.
 *	TH8_ERROR propagates out.
 *
 * Why / How:
 *	Unlike th8SubstWord (which obeys quoting rules for a single
 *	command word), Th8_Subst processes arbitrary text where braces
 *	are literal.  Each substitution type can be independently
 *	disabled via the flags bitmask, and special return codes
 *	from command substitutions (break, continue, return) are
 *	handled per the Tcl 8.4 [subst] specification.
 *
 * Results:
 *	TH8_OK on success.  The interpreter result is set to the
 *	substituted string.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Subst(
    Th8_Interp *interp, /* Interpreter. */
    const char *z, /* Input string. */
    size_t n, /* Byte length, or TH8_NOLEN. */
    int flags) /* TH8_SUBST_* bitmask. */
{
    Th8_Buffer buf;
    size_t i;
    size_t nTag = 0; /* taint of the input template, applied to output */
    int rc = TH8_OK;

    if (!interp) return TH8_ERROR;
    if (n == TH8_NOLEN) {
	n = Th8_Strlen(interp, z);
    } else {
	/* A tainted template taints the substituted output; strip the
	 * tag before n is used as a loop bound / byte count so the scan
	 * does not run past the buffer. */
	nTag = n & TH8_TAG_BITS;
	n = TH8_LEN(n);
    }

    th8BufInit(&buf);

    for (i = 0; i < n;) {
	char c = z[i];

	if (Th8_Ready(interp) != TH8_OK) {
	    rc = TH8_ERROR;
	    break;
	}

	if (c == '\\' && (flags & TH8_SUBST_BACKSLASHES)) {
	    /*
	     * Backslash substitution.
	     */
	    size_t nEsc = 0;

	    th8NextEscape(interp, &z[i], n - i, &nEsc);
	    if (nEsc == 0) {
		th8BufWrite(interp, &buf, &z[i], 1);
		i++;
	    } else {
		th8SubstEscape(interp, &buf, &z[i], nEsc);
		i += nEsc;
	    }
	} else if (c == '$' && (flags & TH8_SUBST_VARIABLES)) {
	    /*
	     * Variable substitution.
	     */
	    size_t nVar = 0;

	    th8NextVarName(interp, &z[i], n - i, &nVar);
	    if (nVar <= 1) {
		/* Bare '$' with no valid name -- literal. */
		th8BufWrite(interp, &buf, &z[i], 1);
		i++;
	    } else {
#if defined(TH8_ENABLE_VARIABLES)
		rc = th8SubstVarName(interp, &z[i], nVar);
#else
		Th8_SetResult(
		    interp, "variable resolution not available", TH8_NOLEN);
		rc = TH8_ERROR;
#endif
		if (rc != TH8_OK) break;
		{
		    size_t nRes;
		    const char *zRes;

		    zRes = Th8_GetResult(interp, &nRes);
		    th8BufWrite(interp, &buf, zRes, nRes);
		}
		i += nVar;
	    }
	} else if (c == '[' && (flags & TH8_SUBST_COMMANDS)) {
	    /*
	     * Command substitution.
	     */
	    size_t nCmd = 0;

	    rc = th8NextCommand(interp, &z[i], n - i, &nCmd);
	    if (rc != TH8_OK) break;
	    if (nCmd == 0) {
		th8BufWrite(interp, &buf, &z[i], 1);
		i++;
	    } else {
		rc = th8SubstCommand(interp, &z[i], nCmd, NULL, 0);

		if (rc == TH8_BREAK) {
		    /*
		     * [break] in command substitution: stop
		     * processing, return what we have so far.
		     */
		    rc = TH8_OK;
		    break;
		} else if (rc == TH8_CONTINUE) {
		    /*
		     * [continue]: replace with empty string,
		     * keep going.
		     */
		    rc = TH8_OK;
		} else if (rc == TH8_RETURN) {
		    /*
		     * [return val]: use the return value.
		     */
		    size_t nRes;
		    const char *zRes;

		    zRes = Th8_GetResult(interp, &nRes);
		    th8BufWrite(interp, &buf, zRes, nRes);
		    rc = TH8_OK;
		} else if (rc == TH8_OK) {
		    size_t nRes;
		    const char *zRes;

		    zRes = Th8_GetResult(interp, &nRes);
		    th8BufWrite(interp, &buf, zRes, nRes);
		} else {
		    /* TH8_ERROR or other: propagate. */
		    break;
		}
		i += nCmd;
	    }
	} else {
	    /*
	     * Ordinary character -- copy as-is.
	     */
	    th8BufWrite(interp, &buf, &z[i], 1);
	    i++;
	}
    }

    if (rc == TH8_OK && buf.bFail) {
	/* A buffer append could not complete: fail instead of
	 * publishing a truncated result (Bug 61). */
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	rc = TH8_ERROR;
    }
    if (rc == TH8_OK) {
	/* Output is tainted if the template was tainted (nTag) or any
	 * substitution contributed tainted bytes (buf.nTag).  Propagate
	 * a publish (result-copy) allocation failure rather than
	 * returning success with a truncated result. */
	rc = Th8_SetResult(
	    interp, buf.zBuf ? buf.zBuf : "", buf.nBuf | buf.nTag | nTag);
    }
    th8BufFree(interp, &buf);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8CheckExpansionPrefix --
 *
 *	Inspect a candidate word for the TH8 expansion-prefix form
 *	`{tag}rest` and report what was found.  Pure I/O: no
 *	Th8_CmdBuild or other parser-internal struct exposure.
 *
 *	On entry zInput[0..nWord-1] is the raw word bytes (as
 *	produced by th8NextWord).  On exit:
 *	  *pbExpand   - 1 iff a registered expansion tag was found.
 *	  *pnTag      - tag byte count (0 if no tag matched).  Caller
 *	                uses this to advance past `{tag}` if it wants
 *	                to consume the prefix (zInput[0..nTag+1]).
 *	  *pxExpand   - expansion callback (only meaningful when
 *	                *pbExpand == 1).
 *	  *ppExpandCtx- expansion context (likewise).
 *
 *	Both th8SplitCommand (the script parser) and th8NRSubstAndBuild
 *	(the substitution-time word builder) call this helper so the
 *	identical 30+-line scan exists in exactly one place.  Exposed
 *	to test/diagnostic plugins via the internal stubs table so
 *	testlib can drive the (T,F) "{}rest" branch directly.
 *
 * Results:
 *	TH8_OK     - scan succeeded.  *pbExpand reports whether the
 *	             word starts with a registered `{tag}` prefix.
 *	TH8_ERROR  - the word is malformed.  Two cases:
 *	               * `{tag}rest` where tag is non-empty but the
 *	                 tag is not registered ("unknown expansion
 *	                 operator:") -- interp result is the error
 *	                 string.
 *	               * `{}rest` (empty brace body with trailing
 *	                 bytes) -- interp result is "extra characters
 *	                 after close-brace".
 *
 * Side effects:
 *	On TH8_ERROR sets the interp result.  No other observable
 *	state changes.
 *
 *----------------------------------------------------------------------
 */

int
th8CheckExpansionPrefix(
    Th8_Interp *interp, /* Interpreter (for FindExpansion + result). */
    const char *zInput, /* Raw word bytes. */
    size_t nWord, /* Bytes in zInput. */
    int *pbExpand, /* OUT: 1 iff expansion tag matched. */
    size_t *pnTag, /* OUT: tag byte count, or 0. */
    Th8_ExpansionProc *pxExpand, /* OUT: expansion callback. */
    void **ppExpandCtx) /* OUT: expansion context. */
{
    int bExpand = 0;
    size_t nTag = 0;
    Th8_ExpansionProc xExpand = 0;
    void *pExpandCtx = 0;
    int rc = TH8_OK;

    if (nWord > 2 && zInput[0] == '{') {
	size_t k;

	for (k = 1; k < nWord; k++) {
	    if (zInput[k] == '}') {
		if (k + 1 < nWord && k > 1) {
		    /* {tag} followed by more content. */
		    const char *zTag = &zInput[1];
		    nTag = k - 1;
		    if (Th8_FindExpansion(
		            interp, zTag, nTag, &xExpand, &pExpandCtx) ==
		        TH8_OK) {
			bExpand = 1;
		    } else {
			Th8_ErrorMessage(
			    interp, "unknown expansion operator:", zTag,
			    nTag);
			rc = TH8_ERROR;
			nTag = 0;
		    }
		} else if (k == 1 && ALWAYS(k + 1 < nWord)) {
		    /* "{}rest": empty brace body with trailing
		     * characters.  Tcl 8.6 rejects this as
		     * "extra characters after close-brace".  Bug 13.
		     * Outer guard `nWord > 2` ensures nWord >= 3;
		     * with k == 1, k + 1 == 2 < nWord is intrinsic-
		     * true (hence the ALWAYS wrap). */
		    Th8_SetResultStatic(
		        interp, "extra characters after close-brace",
		        TH8_NOLEN);
		    rc = TH8_ERROR;
		}
		break;
	    }
	    /* Tag chars: alnum, underscore, asterisk. */
	    if (!th8IsAlnum(zInput[k]) && ALWAYS(zInput[k] != '_') &&
	        zInput[k] != '*') {
		break;
	    }
	}
    }

    if (pbExpand) *pbExpand = bExpand;
    if (pnTag) *pnTag = nTag;
    if (pxExpand) *pxExpand = xExpand;
    if (ppExpandCtx) *ppExpandCtx = pExpandCtx;
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SplitCommand --
 *
 *	Split a single command string into words, performing
 *	substitution on each word.  This is the core of the
 *	word-level parser.
 *
 * Why / How:
 *	Iterates over the command text, splitting it into words via
 *	th8NextWord, substituting each word via th8SubstWord, and
 *	building a contiguous argv/argl/argc triple.  Expansion
 *	prefixes ({*}list) are handled by splicing expanded elements
 *	into the argument vector inline.
 *
 * Results:
 *	TH8_OK on success.  *pArgv, *pArgl, *pArgc are set.
 *	Caller must free *pArgv with Th8_Free.
 *
 * Side effects:
 *	May recursively evaluate scripts (for command substitution).
 *
 *----------------------------------------------------------------------
 */

static int
th8SplitCommand(
    Th8_Interp *interp, /* Interpreter. */
    const char *zCmd, /* Command text. */
    size_t nCmd, /* Byte length. */
    char ***pArgv, /* OUT: argument values. */
    size_t **pArgl, /* OUT: argument lengths. */
    int *pArgc, /* OUT: argument count. */
    const char *zName, /* Script origin (or NULL). */
    size_t nName) /* Origin name length. */
{
    Th8_Buffer strbuf;
    Th8_Buffer lenbuf;
    int nCount = 0;
    int rc = TH8_OK;
    const char *zInput = zCmd;
    size_t nInput = nCmd;
    int wasListMode;

    th8BufInit(&strbuf);
    th8BufInit(&lenbuf);

    /*
     * Parse in "command" mode: substitutions are performed.
     */

    wasListMode = interp->isListMode;
    interp->isListMode = 0;

    /* Loop invariant: every error path inside the body sets rc
     * and immediately breaks, so rc == TH8_OK on re-entry is a
     * defensive belt-and-braces check, never F here. */
    while (ALWAYS(rc == TH8_OK) && nInput > 0) {
	size_t nSpace = 0;
	size_t nWord = 0;

	/*
	 * Security: unified readiness check per word split.
	 * TH8_SUSPEND is preserved so that freeze propagates
	 * cleanly without being converted to an error.
	 */

	{
	    int readyRc = Th8_Ready(interp);

	    if (readyRc != TH8_OK) {
		rc = (readyRc == TH8_SUSPEND) ? TH8_SUSPEND : TH8_ERROR;
		break;
	    }
	}

	th8NextSpace(interp, zInput, nInput, &nSpace);
	zInput += nSpace;
	nInput -= nSpace;
	if (nInput == 0) break;

	rc = th8NextWord(interp, zInput, nInput, &nWord, 0);
	if (rc != TH8_OK) break;
	if (nWord == 0) break;

	/*
	 * Check for expansion prefix: {tag}remaining
	 *
	 * An expansion prefix is recognized when:
	 *   1. The word starts with '{'
	 *   2. The content between { and } is an identifier
	 *      (alphanumeric + underscore + '*')
	 *   3. The closing '}' is NOT the last byte of the word
	 *      (i.e. there is more content after the '}')
	 *   4. The tag is registered as an expansion operator
	 *
	 * If all conditions are met, the remainder of the word
	 * (after the closing '}') is substituted, and the result
	 * is passed through the expansion callback.  The callback
	 * produces zero or more elements that are spliced into
	 * the argument vector.
	 */

	{
	    size_t nTag = 0;
	    Th8_ExpansionProc xExpand = 0;
	    void *pExpandCtx = 0;
	    int bExpand = 0;

	    rc = th8CheckExpansionPrefix(
	        interp, zInput, nWord, &bExpand, &nTag, &xExpand,
	        &pExpandCtx);

	    if (rc != TH8_OK) break;

	    if (bExpand) {
		/*
		 * Expansion word: substitute the part after {tag},
		 * then call the expansion callback.
		 */

		size_t nPrefix = nTag + 2; /* {tag} */
		const char *zRest = zInput + nPrefix;
		size_t nRest = nWord - nPrefix;

		rc = th8SubstWord(interp, zRest, nRest, zName, nName);
		if (rc != TH8_OK) break;

		{
		    size_t nRes;
		    const char *zRes = Th8_GetResult(interp, &nRes);
		    char **azExpanded = 0;
		    size_t *anExpanded = 0;
		    int nExpanded = 0;

		    rc = xExpand(
		        interp, zRes, nRes, &azExpanded, &anExpanded,
		        &nExpanded, pExpandCtx);
		    if (rc != TH8_OK) break;

		    /* Splice expanded elements into buffers */
		    {
			int e;

			for (e = 0; e < nExpanded; e++) {
			    th8BufWrite(
			        interp, &strbuf, azExpanded[e],
			        anExpanded[e]);
			    th8BufAddChar(interp, &strbuf, 0);
			    th8BufWrite(
			        interp, &lenbuf, (const char *)&anExpanded[e],
			        sizeof(size_t));
			    nCount++;
			}
		    }
		    Th8_Free(interp, azExpanded);
		}
	    } else {
		/*
		 * Normal word: substitute and add as one argument.
		 */

		rc = th8SubstWord(interp, zInput, nWord, zName, nName);
		if (rc != TH8_OK) break;

		{
		    size_t nRes;
		    const char *zRes = Th8_GetResult(interp, &nRes);

		    /* The byte count copied into strbuf must be the RAW
		     * length; nRes may carry a taint bit (~256 MiB) that
		     * would otherwise overrun th8BufWrite's size guard and
		     * drop the word.  The taint travels intact in the
		     * lenbuf entry, which becomes this argument's argl[]
		     * length. */
		    th8BufWrite(interp, &strbuf, zRes, TH8_LEN(nRes));
		    th8BufAddChar(interp, &strbuf, 0);
		    th8BufWrite(
		        interp, &lenbuf, (const char *)&nRes, sizeof(size_t));
		    nCount++;
		}
	    }
	}

	zInput += nWord;
	nInput -= nWord;
    }

    interp->isListMode = wasListMode;

    if (rc == TH8_OK && (strbuf.bFail || lenbuf.bFail)) {
	/* A word buffer append could not complete: fail instead of
	 * building an argv from truncated word bytes (Bug 61). */
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	rc = TH8_ERROR;
    }

    if (rc == TH8_OK && nCount > 0) {
	size_t i;
	char *zElem;
	size_t *anElem;
	char **azElem;
	size_t nAlloc = 0;

	/*
	 * Allocate a single block for pointers + lengths + data.
	 */

	{
	    size_t t1 = 0, t2 = 0;

	    /* Sequenced as an else-if ladder so each leg is a
	     * single-condition decision under clang MC/DC.  See
	     * FINDINGS.md Finding 005. */
	    if (TH8_SAFE_MUL_SIZE(sizeof(char *), (size_t)nCount, &t1)) {
		nAlloc = 0;
	    } else if (
	        TH8_SAFE_MUL_SIZE(sizeof(size_t), (size_t)nCount, &t2)) {
		nAlloc = 0;
	    } else if (TH8_SAFE_ADD_SIZE(t1, t2, &nAlloc)) {
		nAlloc = 0;
	    } else if (TH8_SAFE_ADD_SIZE(nAlloc, strbuf.nBuf, &nAlloc)) {
		nAlloc = 0;
	    }
	}

	/*
	 * Overflow check.
	 */

	if (nAlloc == 0) {
	    rc = TH8_ERROR;
	    Th8_SetResult(interp, "command too large", TH8_NOLEN);
	    goto done;
	}

	azElem = (char **)TH8_ALLOC(interp, nAlloc);
	if (!azElem) {
	    rc = TH8_ERROR;
	    Th8_SetResult(interp, "out of memory", TH8_NOLEN);
	    goto done;
	}
	anElem = (size_t *)&azElem[nCount];
	zElem = (char *)&anElem[nCount];

	Th8_Memcpy(interp, anElem, lenbuf.zBuf, lenbuf.nBuf);
	Th8_Memcpy(interp, zElem, strbuf.zBuf, strbuf.nBuf);

	for (i = 0; i < (size_t)nCount; i++) {
	    azElem[i] = zElem;
	    zElem += TH8_LEN(anElem[i]) + 1;
	}

	*pArgv = azElem;
	*pArgl = anElem;
    } else {
	*pArgv = 0;
	*pArgl = 0;
    }
    *pArgc = nCount;

done:
    th8BufFree(interp, &strbuf);
    th8BufFree(interp, &lenbuf);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * NRE-aware command substitution.
 *
 *	Forward declarations for types and callbacks defined later
 *	in the evaluator section but referenced by the NRE word-
 *	splitting code below.
 */

/*
 * Th8_EvalState --
 *
 *	Heap-allocated state for the NRE-driven eval loop.  One
 *	instance exists per active script evaluation.  The trampoline
 *	invokes th8EvalIteration repeatedly, each invocation
 *	processing one command.  When the script is exhausted, the
 *	continuation frees this state and returns.
 *
 *	Defined here (before the NRE word-splitting code) so that
 *	th8NRCmdDispatch can dereference pState for error traces
 *	and next-iteration pushes.
 */

static int th8EvalIteration(Th8_Interp *, void *[], int);
static int th8EvalPostCmd(Th8_Interp *, void *[], int);

/*
 *	When a command contains [...] substitution, the synchronous
 *	th8SplitCommand path calls Th8_Eval (blocking), which creates
 *	a nested trampoline.  If [yield] is called inside that eval,
 *	the nested trampoline breaks but the C stack frames of
 *	th8SubstWord and th8SplitCommand are destroyed - their loop
 *	state, partial word buffers, and parse positions are lost.
 *
 *	These functions replace the synchronous path when [...] is
 *	present.  Word-splitting state is stored on the heap in a
 *	Th8_CmdBuild struct, and each [...] substitution is pushed
 *	via Th8_NREval onto the SAME callback chain.  This means:
 *
 *	  - Yield inside [...] breaks ONE trampoline, preserving
 *	    the entire callback chain (eval cleanup ==> word continue
 *	    ==> command dispatch ==> eval iteration ==> ...).
 *	  - On resume, the eval result (the resume value) flows
 *	    through the word-continuation callback, which appends
 *	    it to the word buffer and continues where it left off.
 *	  - The set x [yield val] pattern works: the resume value
 *	    becomes the substitution result for [yield val].
 *
 *----------------------------------------------------------------------
 */

/* Forward declarations. */
static int th8NRSubstAndBuild(Th8_Interp *, Th8_CmdBuild *);
static int th8NRCmdSubstDone(Th8_Interp *, void *[], int);
static int th8NRCmdDispatch(Th8_Interp *, void *[], int);

/*
 * th8FreeCmdBuild --
 *
 *	Free a Th8_CmdBuild and all its internal buffers.
 */

static void
th8FreeCmdBuild(Th8_Interp *interp, Th8_CmdBuild *p)
{
    if (!p) return;
    interp->isListMode = p->wasListMode;
    th8BufFree(interp, &p->strbuf);
    th8BufFree(interp, &p->lenbuf);
    th8BufFree(interp, &p->wordBuf);
    Th8_Free(interp, p);
}

/*
 * th8CmdBuildAddWord --
 *
 *	Add a fully substituted word to the build buffers.
 */

static void
th8CmdBuildAddWord(
    Th8_Interp *interp,
    Th8_CmdBuild *p,
    const char *z,
    size_t n)
{
    th8BufWrite(interp, &p->strbuf, z, n);
    th8BufAddChar(interp, &p->strbuf, 0);
    th8BufWrite(interp, &p->lenbuf, (const char *)&n, sizeof(size_t));
    p->nCount++;
}

/*
 * th8CmdHasBracket --
 *
 *	Quick scan: does the command text contain '[' ?
 *	Used to decide whether the NRE word-splitting path
 *	is needed.  False positives ([ inside braces) are
 *	harmless - the NRE path handles them correctly.
 */

static int
th8CmdHasBracket(const char *z, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	if (z[i] == '[') return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * th8CmdNameTainted --
 *
 *	Test whether a command NAME is tainted and, if so, report it.
 *
 * Why / How:
 *	Choosing which command to run from untrusted data is code
 *	selection and must never occur, even when the surrounding
 *	script is clean.  BOTH dispatch paths -- the synchronous
 *	th8EvalIteration and the NRE / command-substitution
 *	th8NRCmdDispatch (Bug 64, which previously lacked the check)
 *	-- gate the command name through this single helper so the
 *	two stay in lock-step.  Factoring it here keeps the callers
 *	perfectly consistent AND makes each call site a single-
 *	condition (fully MC/DC-coverable) decision, with the one
 *	TH8_TAINTED short-circuit living here (it keeps the common
 *	clean case a cheap bit test off the eval hot path;
 *	Th8_ReportTaint returns nonzero only when actually tainted).
 *
 * Results:
 *	Nonzero if the name is tainted (and has been reported); the
 *	caller must fail closed.  Zero otherwise.
 *
 * Side effects:
 *	On taint, sets the interpreter result via Th8_ReportTaint.
 *
 *----------------------------------------------------------------------
 */

static int
th8CmdNameTainted(Th8_Interp *interp, const char *zName, size_t nName)
{
    return TH8_TAINTED(nName) &&
           Th8_ReportTaint(interp, "command name", zName, nName);
}

/*
 * th8NRSubstAndBuild --
 *
 *	Process words from pBuild->zInput, performing substitution.
 *	Handles backslash and variable substitution synchronously.
 *	For each [...] command substitution, pushes an NRE eval
 *	callback and a continuation (th8NRCmdSubstDone), then
 *	returns TH8_OK to let the trampoline handle them.
 *
 *	Called initially from th8EvalIteration (to start processing)
 *	and from th8NRCmdSubstDone (to continue after a [...] eval).
 *
 * Results:
 *	TH8_OK if all words are processed (or an async eval was
 *	pushed).  Error code on failure.
 *
 * Side effects:
 *	Words are added to pBuild's buffers.  NRE callbacks may
 *	be pushed for [...] substitution.
 */

static int
th8NRSubstAndBuild(Th8_Interp *interp, Th8_CmdBuild *pBuild)
{
    int rc = TH8_OK;

    /*
     * If a word is already in progress (continuing after [cmd]),
     * jump directly to the word substitution loop.
     */

    if (pBuild->bWordActive) {
	goto continue_word;
    }

    /*
     * Main word loop: iterate over remaining words in the
     * command text.  Loop invariant: every error path inside
     * the body sets rc and either breaks or goto-error, so
     * rc == TH8_OK on re-entry is a defensive belt-and-braces
     * check, never F here.
     */

    while (ALWAYS(rc == TH8_OK) && pBuild->nInput > 0) {
	size_t nSpace = 0;
	size_t nWord = 0;

	/*
	 * Readiness check per word.
	 */

	{
	    int readyRc = Th8_Ready(interp);

	    if (readyRc != TH8_OK) {
		rc = (readyRc == TH8_SUSPEND) ? TH8_SUSPEND : TH8_ERROR;
		break;
	    }
	}

	th8NextSpace(interp, pBuild->zInput, pBuild->nInput, &nSpace);
	pBuild->zInput += nSpace;
	pBuild->nInput -= nSpace;
	if (pBuild->nInput == 0) break;

	rc = th8NextWord(interp, pBuild->zInput, pBuild->nInput, &nWord, 0);
	if (rc != TH8_OK) break;
	if (nWord == 0) break;

	pBuild->zWord = pBuild->zInput;
	pBuild->nWord = nWord;
	pBuild->nOrigWord = nWord;

	/*
	 * Check for expansion prefix: {tag}rest
	 */

	pBuild->bExpand = 0;
	{
	    size_t nTag = 0;
	    int bExpand = 0;

	    rc = th8CheckExpansionPrefix(
	        interp, pBuild->zInput, nWord, &bExpand, &nTag,
	        &pBuild->xExpand, &pBuild->pExpandCtx);
	    if (bExpand) {
		pBuild->bExpand = 1;
		pBuild->zWord = pBuild->zInput + nTag + 2;
		pBuild->nWord = nWord - nTag - 2;
	    }
	}

	if (rc != TH8_OK) break;

	/*
	 * Start substituting this word.
	 */

	th8BufInit(&pBuild->wordBuf);
	pBuild->bWordActive = 1;

	/*
	 * Brace-delimited: no substitution except
	 * backslash-newline.
	 */

	if (pBuild->nWord > 1 && pBuild->zWord[0] == '{' &&
	    ALWAYS(pBuild->zWord[pBuild->nWord - 1] == '}')) {
	    size_t i;
	    size_t nn = pBuild->nWord;

	    for (i = 1; i < nn - 1; i++) {
		if (pBuild->zWord[i] == '\\' && i + 1 < nn - 1 &&
		    pBuild->zWord[i + 1] == '\n') {
		    th8BufAddChar(interp, &pBuild->wordBuf, ' ');
		    i += 2;
		    while (i < nn - 1 && (pBuild->zWord[i] == ' ' ||
		                          pBuild->zWord[i] == '\t')) {
			i++;
		    }
		    i--;
		} else {
		    th8BufAddChar(interp, &pBuild->wordBuf, pBuild->zWord[i]);
		}
	    }
	    goto word_done;
	}

	/*
	 * Quote-delimited or bare word: set up scan range.
	 */

	{
	    size_t nn = pBuild->nWord;

	    pBuild->wordPos = 0;
	    pBuild->wordEnd = nn;
	    if (nn > 1 && pBuild->zWord[0] == '"' &&
	        ALWAYS(pBuild->zWord[nn - 1] == '"')) {
		pBuild->wordPos = 1;
		pBuild->wordEnd = nn - 1;
	    }
	}

continue_word:
	/*
	 * Character-by-character substitution within the word.
	 */

	while (pBuild->wordPos < pBuild->wordEnd) {
	    char c = pBuild->zWord[pBuild->wordPos];

	    if (c == '\\') {
		size_t nEsc;

		if (th8NextEscape(
		        interp, &pBuild->zWord[pBuild->wordPos],
		        pBuild->wordEnd - pBuild->wordPos, &nEsc) != TH8_OK) {
		    rc = TH8_ERROR;
		    goto error;
		}
		th8SubstEscape(
		    interp, &pBuild->wordBuf, &pBuild->zWord[pBuild->wordPos],
		    nEsc);
		pBuild->wordPos += nEsc;
	    } else if (c == '[' && ALWAYS(!interp->isListMode)) {
		/*
		 * Command substitution: push NRE eval.
		 *
		 * 1. Find matching ].
		 * 2. Advance wordPos past the ].
		 * 3. Push th8NRCmdSubstDone (continuation).
		 * 4. Push Th8_NREval for the [cmd] content.
		 * 5. Return TH8_OK - the trampoline takes
		 *    over.
		 */

		{
		    size_t nCmd;

		    if (th8NextCommand(
		            interp, &pBuild->zWord[pBuild->wordPos],
		            pBuild->wordEnd - pBuild->wordPos,
		            &nCmd) != TH8_OK) {
			rc = TH8_ERROR;
			goto error;
		    }

		    /*
		     * Push continuation FIRST (LIFO: runs
		     * AFTER the eval).
		     */

		    Th8_NRAddCallback(
		        interp, th8NRCmdSubstDone, pBuild, 0, 0, 0);

		    /*
		     * Push eval of [cmd] content (runs
		     * FIRST due to LIFO).  Skip the
		     * surrounding brackets.
		     */

		    Th8_NREval(
		        interp, &pBuild->zWord[pBuild->wordPos + 1], nCmd - 2,
		        pBuild->pState ? pBuild->pState->zName : NULL,
		        pBuild->pState ? pBuild->pState->nName : 0);

		    pBuild->wordPos += nCmd;
		    return TH8_OK;
		}
	    } else if (c == '$' && ALWAYS(!interp->isListMode)) {
		size_t nVar;

		if (th8NextVarName(
		        interp, &pBuild->zWord[pBuild->wordPos],
		        pBuild->wordEnd - pBuild->wordPos, &nVar) != TH8_OK) {
		    rc = TH8_ERROR;
		    goto error;
		}
		if (nVar <= 1) {
		    th8BufAddChar(interp, &pBuild->wordBuf, '$');
		    pBuild->wordPos++;
		} else {
#if defined(TH8_ENABLE_VARIABLES)
		    rc = th8SubstVarName(
		        interp, &pBuild->zWord[pBuild->wordPos], nVar);
#else
		    Th8_SetResult(
		        interp, "variable resolution not available",
		        TH8_NOLEN);
		    rc = TH8_ERROR;
#endif
		    if (rc != TH8_OK) goto error;
		    {
			size_t nRes;
			const char *zRes;

			zRes = Th8_GetResult(interp, &nRes);
			th8BufWrite(interp, &pBuild->wordBuf, zRes, nRes);
		    }
		    pBuild->wordPos += nVar;
		}
	    } else {
		th8BufAddChar(interp, &pBuild->wordBuf, c);
		pBuild->wordPos++;
	    }
	}

word_done:
	/*
	 * Word fully substituted.  Handle expansion or
	 * add directly to the argv buffers.
	 */

	/* If a wordBuf append could not complete, the word is
	 * truncated -- fail here rather than expand / dispatch the
	 * truncated word (Bug 61). */
	if (pBuild->wordBuf.bFail) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    rc = TH8_ERROR;
	    goto error;
	}

	if (pBuild->bExpand) {
	    char **azExpanded = 0;
	    size_t *anExpanded = 0;
	    int nExpanded = 0;

	    rc = pBuild->xExpand(
	        interp, pBuild->wordBuf.zBuf, pBuild->wordBuf.nBuf,
	        &azExpanded, &anExpanded, &nExpanded, pBuild->pExpandCtx);
	    if (rc == TH8_OK) {
		int e;

		/* A tainted expansion word conservatively taints every
		 * element it expands to. */
		for (e = 0; e < nExpanded; e++) {
		    th8CmdBuildAddWord(
		        interp, pBuild, azExpanded[e],
		        anExpanded[e] | pBuild->wordBuf.nTag);
		}
	    }
	    Th8_Free(interp, azExpanded);
	} else {
	    /* Carry the word's accumulated tags (taint/sensitive) into its
	     * argl[] length. */
	    th8CmdBuildAddWord(
	        interp, pBuild, pBuild->wordBuf.zBuf,
	        pBuild->wordBuf.nBuf | pBuild->wordBuf.nTag);
	}
	th8BufFree(interp, &pBuild->wordBuf);
	pBuild->bWordActive = 0;
	pBuild->bExpand = 0;

	/*
	 * Advance past this word in the command text.
	 */

	pBuild->zInput += pBuild->nOrigWord;
	pBuild->nInput -= pBuild->nOrigWord;
    }

    return rc;

error:
    th8BufFree(interp, &pBuild->wordBuf);
    pBuild->bWordActive = 0;
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8NRCmdSubstDone --
 *
 *	NRE callback that runs after a [...] command substitution
 *	eval completes.  Captures the eval result, appends it to
 *	the current word buffer, and continues word processing.
 *
 * Why / How:
 *	When the NRE trampoline finishes evaluating the [cmd] pushed
 *	by th8NRSubstAndBuild, this callback receives the result.
 *	It appends the eval result to the word-in-progress buffer,
 *	then re-enters th8NRSubstAndBuild to continue scanning the
 *	remaining characters of the current word (and any subsequent
 *	words).
 *
 * Results:
 *	Return code from continued word processing.
 *
 * Side effects:
 *	Word buffer is updated.  More NRE callbacks may be pushed.
 *
 *----------------------------------------------------------------------
 */

static int
th8NRCmdSubstDone(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_CmdBuild *pBuild = (Th8_CmdBuild *)pData[0];

    if (rc != TH8_OK) {
	return rc;
    }

    /*
     * Capture the eval result and append to the word buffer.
     */

    {
	size_t nRes;
	const char *zRes = Th8_GetResult(interp, &nRes);

	th8BufWrite(interp, &pBuild->wordBuf, zRes, nRes);
    }

    /*
     * Continue processing the rest of this word (and
     * subsequent words).
     */

    return th8NRSubstAndBuild(interp, pBuild);
}


/*
 *----------------------------------------------------------------------
 *
 * th8NRCmdDispatch --
 *
 *	NRE callback that runs after all words have been processed
 *	by th8NRSubstAndBuild.  Assembles the final argv array and
 *	dispatches the command - the same logic as the second half
 *	of th8EvalIteration.
 *
 * Why / How:
 *	Pushed by th8EvalIteration as the bottom callback beneath
 *	the NRE word-splitting chain.  When all words are processed,
 *	this callback assembles the argv from CmdBuild buffers,
 *	performs namespace-aware command lookup, and dispatches via
 *	xProc -- mirroring the synchronous path in th8EvalIteration.
 *
 * Results:
 *	Return code from command dispatch.
 *
 * Side effects:
 *	Command is dispatched.  th8EvalPostCmd is pushed.
 *
 *----------------------------------------------------------------------
 */

static int
th8NRCmdDispatch(Th8_Interp *interp, void *pData[], int rc)
{
    Th8_CmdBuild *pBuild = (Th8_CmdBuild *)pData[0];
    Th8_EvalState *pState = (Th8_EvalState *)pData[1];
#if defined(TH8_ENABLE_VARIABLES)
    /* Function-scope so the oom label (which also frees pBuild) can
     * free the error-trace accumulators. */
    char *zRes = 0;
    size_t nRes = 0;
    char *zInfo = 0;
    size_t nInfo = 0;
#endif

    if (rc != TH8_OK) {
	/*
	 * Word splitting failed.  Build errorInfo and
	 * propagate the error.
	 */

	if (rc == TH8_ERROR) {
	    interp->nErrorLine = interp->nLine;
#if defined(TH8_ENABLE_VARIABLES)
	    {
		zRes = Th8_TakeResult(interp, &nRes);
		TH8_STR_APPEND(
		    interp, &zInfo, &nInfo, "\n    while executing\n\"",
		    TH8_NOLEN);
		{
		    size_t nCmdLen;

		    nCmdLen = (size_t)(pState->zInput - pState->zFirst);
		    if (nCmdLen > 150) nCmdLen = 150;
		    TH8_STR_APPEND(
		        interp, &zInfo, &nInfo, pState->zFirst, nCmdLen);
		}
		TH8_STR_APPEND(interp, &zInfo, &nInfo, "\"", 1);
		Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, zInfo, nInfo);
		Th8_SetResult(interp, zRes, nRes);
		Th8_Free(interp, zRes);
		Th8_Free(interp, zInfo);
	    }
#endif
	}
	th8FreeCmdBuild(interp, pBuild);
	return rc;
    }

    /*
     * If a word buffer append could not complete, the assembled argv
     * would contain truncated command / argument bytes -- fail instead
     * of dispatching them (Bug 61).  Mirrors the check in
     * th8SplitCommand's synchronous path.
     */
    if (pBuild->strbuf.bFail || pBuild->lenbuf.bFail) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	th8FreeCmdBuild(interp, pBuild);
	return TH8_ERROR;
    }

    /*
     * Assemble the final argv from the build buffers.
     * This is the same logic as the end of th8SplitCommand.
     */

    if (pBuild->nCount == 0) {
	/*
	 * Empty command (all words resolved to nothing).
	 * Push next iteration and continue.
	 */

	th8FreeCmdBuild(interp, pBuild);
	Th8_NRAddCallback(interp, th8EvalIteration, pState, 0, 0, 0);
	return TH8_OK;
    }

    {
	char *zElem;
	size_t *anElem;
	char **azElem;
	size_t nAlloc = 0;
	size_t i;
	int argc = pBuild->nCount;
	Th8_HashEntry *pEntry;

	/*
	 * Single allocation: pointers + lengths + data.
	 */

	{
	    size_t t1 = 0, t2 = 0;

	    /* Else-if ladder for single-condition MC/DC.  See
	     * FINDINGS.md Finding 005. */
	    if (TH8_SAFE_MUL_SIZE(sizeof(char *), (size_t)argc, &t1)) {
		nAlloc = 0;
	    } else if (TH8_SAFE_MUL_SIZE(sizeof(size_t), (size_t)argc, &t2)) {
		nAlloc = 0;
	    } else if (TH8_SAFE_ADD_SIZE(t1, t2, &nAlloc)) {
		nAlloc = 0;
	    } else if (
	        TH8_SAFE_ADD_SIZE(nAlloc, pBuild->strbuf.nBuf, &nAlloc)) {
		nAlloc = 0;
	    }
	}
	if (nAlloc == 0) {
	    th8FreeCmdBuild(interp, pBuild);
	    Th8_SetResult(interp, "command too large", TH8_NOLEN);
	    return TH8_ERROR;
	}

	azElem = (char **)TH8_ALLOC(interp, nAlloc);
	if (!azElem) {
	    th8FreeCmdBuild(interp, pBuild);
	    Th8_SetResult(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	anElem = (size_t *)&azElem[argc];
	zElem = (char *)&anElem[argc];

	Th8_Memcpy(interp, anElem, pBuild->lenbuf.zBuf, pBuild->lenbuf.nBuf);
	Th8_Memcpy(interp, zElem, pBuild->strbuf.zBuf, pBuild->strbuf.nBuf);

	for (i = 0; i < (size_t)argc; i++) {
	    azElem[i] = zElem;
	    zElem += TH8_LEN(anElem[i]) + 1;
	}

	th8FreeCmdBuild(interp, pBuild);

	/*
	 * Reject a tainted command name.  A command name assembled
	 * through "[...]" substitution reaches this async path, so the
	 * check MUST be here too (Bug 64 -- it was previously only on
	 * the sync th8EvalIteration path).  Shared with that path via
	 * th8CmdNameTainted so the two stay in lock-step.  Fail closed;
	 * azElem is freed by th8EvalPostCmd like any other dispatch
	 * error.
	 */

	if (th8CmdNameTainted(interp, azElem[0], anElem[0])) {
	    Th8_NRAddCallback(interp, th8EvalPostCmd, pState, azElem, 0, 0);
	    return TH8_ERROR;
	}

	/*
	 * Command lookup and dispatch - same logic as
	 * th8EvalIteration's second half.
	 */

	pEntry = 0;

	{
	    const char *zCmdNs;
	    size_t nCmdNs;
	    const char *zCmdTail;
	    size_t nCmdTail;

	    th8SplitQualName(
	        azElem[0], TH8_LEN(anElem[0]), &zCmdNs, &nCmdNs, &zCmdTail,
	        &nCmdTail);

	    if (zCmdNs) {
		Th8_Namespace *pNs;

		pNs = th8FindNamespace(interp, zCmdNs, nCmdNs, 0);
		if (pNs) {
		    pEntry = Th8_HashFind(
		        interp, pNs->paCmd, zCmdTail, nCmdTail, 0);
		}
	    } else {
		pEntry = Th8_HashFind(
		    interp, interp->pCurrentNs->paCmd, azElem[0],
		    TH8_LEN(anElem[0]), 0);
		if (!pEntry && interp->pCurrentNs != interp->pGlobalNs) {
		    pEntry = Th8_HashFind(
		        interp, interp->pGlobalNs->paCmd, azElem[0],
		        TH8_LEN(anElem[0]), 0);
		}
	    }
	}

	if (!pEntry) {
	    Th8_HashEntry *pUnk = 0;

	    if (!interp->bInUnknown) {
		pUnk = Th8_HashFind(
		    interp, interp->pGlobalNs->paCmd, "unknown", 7, 0);
	    }
	    if (pUnk) {
		Th8_Command *pCmd;
		char **azNew;
		size_t *anNew;
		int nNew = argc + 1;
		size_t nUA = 0;
		int k;

		{
		    size_t u1 = 0, u2 = 0;

		    /* Else-if ladder for single-condition MC/DC.
		     * See FINDINGS.md Finding 005. */
		    int overflowed = 0;

		    if (TH8_SAFE_MUL_SIZE(
		            sizeof(char *), (size_t)nNew, &u1)) {
			overflowed = 1;
		    } else if (TH8_SAFE_MUL_SIZE(
		                   sizeof(size_t), (size_t)nNew, &u2)) {
			overflowed = 1;
		    } else if (TH8_SAFE_ADD_SIZE(u1, u2, &nUA)) {
			overflowed = 1;
		    }
		    if (overflowed) {
			Th8_Free(interp, azElem);
			Th8_SetResult(
			    interp, "too many arguments", TH8_NOLEN);
			return TH8_ERROR;
		    }
		}
		azNew = (char **)TH8_ALLOC(interp, nUA);
		if (!azNew) {
		    Th8_Free(interp, azElem);
		    Th8_SetResult(interp, "out of memory", TH8_NOLEN);
		    return TH8_ERROR;
		}
		anNew = (size_t *)&azNew[nNew];
		azNew[0] = (char *)"unknown";
		anNew[0] = 7;
		for (k = 0; k < argc; k++) {
		    azNew[k + 1] = azElem[k];
		    anNew[k + 1] = anElem[k];
		}
		/*
		 * pData[3] = azNew so th8EvalPostCmd frees it AFTER all
		 * of the command's NRE callbacks have finished using the
		 * argument pointers it borrows from azElem.  azNew must
		 * NOT be freed synchronously after xProc returns: the
		 * unknown handler may push NRE callbacks (e.g.
		 * proc_call_nr) that still reference azNew/anNew once
		 * xProc yields (Bug 62 -- this async path previously
		 * freed azNew here, a use-after-free confirmed by ASan:
		 * heap-use-after-free in proc_call_nr reading azNew).
		 * Mirrors the synchronous th8EvalIteration path.
		 */
		Th8_NRAddCallback(
		    interp, th8EvalPostCmd, pState, azElem, TH8_INT2PTR(1),
		    azNew);
		pCmd = (Th8_Command *)pUnk->pData;
		interp->bInUnknown = 1;
		rc = pCmd->xProc(
		    interp, pCmd->pContext, nNew, (const char **)azNew,
		    anNew);
		return rc;
	    }
	    Th8_ErrorMessage(
	        interp, "no such command:", azElem[0], TH8_LEN(anElem[0]));
	    rc = TH8_ERROR;
	}

	if (rc == TH8_OK) {
	    rc = Th8_Ready(interp);
	}

	if (rc == TH8_OK) {
	    Th8_Command *p;

	    p = (Th8_Command *)pEntry->pData;
	    Th8_NRAddCallback(interp, th8EvalPostCmd, pState, azElem, 0, 0);
	    rc = p->xProc(
	        interp, p->pContext, argc, (const char **)azElem, anElem);
	    return rc;
	}

	Th8_NRAddCallback(interp, th8EvalPostCmd, pState, azElem, 0, 0);
	return rc;
    }

#if defined(TH8_ENABLE_VARIABLES)
oom:
    /* A TH8_STR_APPEND growth failed while building the error trace;
     * "out of memory" already set.  Free the trace accumulators and the
     * command build (the normal error path above frees pBuild too). */
    Th8_Free(interp, zRes);
    Th8_Free(interp, zInfo);
    th8FreeCmdBuild(interp, pBuild);
    return TH8_ERROR;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Evaluator --
 *
 *	The NRE-driven script evaluation engine.
 *
 * Why / How:
 *	This section contains the core evaluation loop, command
 *	dispatch, and NRE callback chain management.  All script
 *	execution flows through th8EvalLocal, which sets up the
 *	NRE callback chain, and th8EvalIteration, which processes
 *	one command per trampoline iteration.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8EvalLocal --
 *
 *	Set up NRE-driven evaluation of a TH8 script in the current
 *	stack frame.  THIS IS THE MOST CRITICAL FUNCTION IN THE
 *	INTERPRETER.
 *
 *	OVERALL FLOW:
 *
 *	  1. Security gate: reject tainted scripts.
 *	  2. Recursion depth check (counter-based, limit 10000).
 *	  3. Unified readiness check (stack, cancel, step counter).
 *	  4. PRE-phase policy callback (host tracing/security hook).
 *	  5. Push NRE callbacks to drive the eval loop:
 *	     - th8EvalStateCleanup (bottom -- runs last): restores
 *	       nEvalDepth and nLine, frees EvalState.
 *	     - th8EvalIteration (top -- runs first): processes one
 *	       command per invocation, then pushes th8EvalPostCmd
 *	       and dispatches the command.  th8EvalPostCmd frees
 *	       argv, builds error traces, and pushes the next
 *	       th8EvalIteration.
 *
 *	  Each th8EvalIteration does:
 *	     a. Skip semicolons.
 *	     b. Skip whitespace (counting newlines for line tracking).
 *	     c. Skip comments (lines starting with '#').
 *	     d. Scan to end of command (newline or ';' at depth 0).
 *	     e. Split the command text into words with substitution
 *	        (th8SplitCommand -> th8SubstWord for each word).
 *	     f. Look up the command name in the namespace hierarchy:
 *	        - Qualified names: resolve via th8SplitQualName +
 *	          th8FindNamespace.
 *	        - Simple names: search current namespace, then global.
 *	     g. If not found: try the "unknown" handler (with
 *	        bInUnknown recursion guard to prevent infinite loops).
 *	     h. Readiness check before each command dispatch.
 *	     i. Push th8EvalPostCmd, then invoke xProc.  The
 *	        command's NRE callbacks run before th8EvalPostCmd
 *	        (LIFO ordering).
 *
 *	  th8EvalPostCmd does:
 *	     j. Free argv.
 *	     k. On TH8_ERROR: build the error trace by appending to
 *	        ::errorInfo, record the error line number at the
 *	        innermost error site, and truncate the command text
 *	        to 150 bytes for safety.
 *	     l. On TH8_OK: push the next th8EvalIteration.
 *
 *	This design eliminates C stack nesting during evaluation.
 *	The trampoline drives the callbacks iteratively.
 *
 *	LINE COUNTING:
 *
 *	  The interpreter tracks the current line number (nLine)
 *	  throughout the eval loop by counting '\n' characters in
 *	  whitespace spans, comment spans, and word spans via
 *	  th8CountNewlines.  nLine is saved/restored across nested
 *	  th8EvalLocal calls so that each script starts at line 1.
 *
 * Why / How:
 *	This is the single entry point for all script evaluation.
 *	It does NOT evaluate anything itself - it pushes NRE
 *	callbacks onto the trampoline chain and returns.  The
 *	actual command-by-command evaluation happens later in
 *	th8EvalIteration, driven by the trampoline loop in
 *	th8RunCallbacks.  This design avoids C-stack recursion
 *	for nested evals (NRE = Non-Recursive Engine).
 *
 * Results:
 *	TH8_OK on successful setup (callbacks pushed), or an
 *	error code if a pre-flight check (taint, depth, readiness,
 *	PRE-phase policy callback) fails.  Callers that need a blocking
 *	result must drain the NRE callback chain via th8RunCallbacks or
 *	th8EvalTrampoline after this function returns.
 *
 * Side effects:
 *	NRE callbacks are pushed.  nEvalDepth is incremented.
 *	nLine is reset to 1.  An EvalState is heap-allocated.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8CountNewlines --
 *
 *	Count newline characters in a text span and add to the
 *	interpreter's current line counter.
 *
 * Why / How:
 *	The evaluator must track line numbers for error messages
 *	without using the C runtime.  This function scans a byte
 *	range (whitespace, comment, or word span) and increments
 *	interp->nLine for each '\n' found.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	interp->nLine is incremented for each '\n' found.
 *
 *----------------------------------------------------------------------
 */

static void
th8CountNewlines(Th8_Interp *interp, const char *z, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	if (z[i] == '\n') {
	    interp->nLine++;
	}
    }
}


/*
 * Forward declaration for NRE eval cleanup callback
 * (Th8_EvalState and other forwards are above, in the
 * NRE-aware command substitution section).
 */

static int th8EvalStateCleanup(Th8_Interp *, void *[], int);


/*
 *----------------------------------------------------------------------
 *
 * th8EvalStateCleanup --
 *
 *	NRE callback that runs after the entire script has been
 *	evaluated (or aborted).  Restores the eval depth counter
 *	and the saved line number, then frees the EvalState.
 *
 *	This is the BOTTOM callback -- pushed first by th8EvalLocal,
 *	so it runs last due to LIFO ordering.
 *
 * Why / How:
 *	LIFO ordering ensures this runs after all iteration and
 *	post-command callbacks.  It fires the POST-phase policy
 *	callback (giving the host the final return code), then
 *	decrements nEvalDepth and restores nLine to the value
 *	saved before this eval began.
 *
 * Results:
 *	Passes through the return code from the last command.
 *
 * Side effects:
 *	nEvalDepth decremented, nLine restored, EvalState freed.
 *
 *----------------------------------------------------------------------
 */

static int
th8EvalStateCleanup(
    Th8_Interp *interp, /* Interpreter. */
    void *pData[], /* [0]=Th8_EvalState*. */
    int rc) /* Return code from previous step. */
{
    Th8_EvalState *pState = (Th8_EvalState *)pData[0];

    /*
     * POST phase policy callback (EVAL).  The eval is fully
     * complete; pass the actual return code.  The callback's
     * own return value is ignored -- the eval already happened.
     */

    if (interp->xPolicyCb) {
	interp->xPolicyCb(
	    interp, TH8_PHASE_POST | TH8_PHASE_EVAL, pState->zName,
	    pState->nName, pState->zProgram, pState->nOrigInput,
	    pState->flags, rc, interp->pPolicyCbCtx);
    }

    interp->nEvalDepth--;

    /*
     * When the eval depth reaches 0, no script code remains on
     * the call stack.  Drain any commands or namespaces whose
     * deletion was deferred during eval.
     */

    if (interp->nEvalDepth == 0 && interp->pPendingHead) {
	th8DrainPendingDeletes(interp);
    }

    interp->nLine = pState->nSavedLine;
    Th8_Free(interp, pState);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8EvalPostCmd --
 *
 *	NRE callback that runs after a single command (and all of
 *	its NRE callbacks) has completed.  This callback:
 *
 *	  1. Frees the argv array from the completed command.
 *	  2. If rc == TH8_ERROR, builds the error trace (appends
 *	     to ::errorInfo with command text).
 *	  3. Pushes th8EvalIteration for the next command if
 *	     rc == TH8_OK.
 *
 *	LIFO ordering guarantee: the command's own NRE callbacks
 *	were pushed above this callback by th8EvalIteration, so
 *	they all complete before this callback runs.
 *
 * Why / How:
 *	This is the per-command cleanup in the NRE eval chain.
 *	On TH8_ERROR it builds a Tcl-style error trace by appending
 *	the faulting command text (truncated to 150 bytes) to
 *	::errorInfo.  On TH8_OK it pushes the next th8EvalIteration
 *	to continue the script.  On other codes (break/return/continue)
 *	it lets the return code propagate to th8EvalStateCleanup.
 *
 * Results:
 *	The (possibly transformed) return code.
 *
 * Side effects:
 *	argv freed, error trace may be built, next iteration pushed.
 *
 *----------------------------------------------------------------------
 */

static int
th8EvalPostCmd(
    Th8_Interp *interp, /* Interpreter. */
    void *pData[], /* [0]=EvalState, [1]=argv, [2]=flags,
				 * [3]=azNew (unknown path only). */
    int rc) /* Return code from the command. */
{
    Th8_EvalState *pState = (Th8_EvalState *)pData[0];
    char **argv = (char **)pData[1];
    int bWasUnknown = (pData[2] != 0);
#if defined(TH8_ENABLE_VARIABLES)
    /* Function-scope so the oom label can free them (the error-trace
     * builder below appends via TH8_STR_APPEND). */
    char *zRes = 0;
    size_t nRes = 0;
    char *zInfo = 0;
    size_t nInfo = 0;
#endif

    /*
     * Reset the unknown-handler guard if this command was
     * dispatched through the 'unknown' path.
     */

    if (bWasUnknown) {
	interp->bInUnknown = 0;
    }

    /*
     * Free the "unknown" wrapper argv (azNew) if present.
     * This must happen before freeing the original argv
     * because azNew contains pointers that borrow from argv.
     * Deferred to here (instead of freeing synchronously after
     * xProc) because NRE callbacks (e.g. proc_call_nr) may
     * still be reading from azNew when xProc returns.
     */

    if (pData[3]) {
	Th8_Free(interp, pData[3]);
    }

    /*
     * Free the argv array from the completed command.
     */

    Th8_Free(interp, argv);

    /*
     * Error trace: append to ::errorInfo.
     * This builds a Tcl-style stack trace on error.
     */

    if (rc == TH8_ERROR) {
#if defined(TH8_ENABLE_VARIABLES)
	int bInnerError = 0;

	zRes = Th8_TakeResult(interp, &nRes);
	if (TH8_OK == Th8_GetVar(interp, "::errorInfo", TH8_NOLEN)) {
	    size_t nOld;
	    const char *zOld;

	    zOld = Th8_GetResult(interp, &nOld);
	    if (nOld > 0) {
		bInnerError = 1;
	    }
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, zOld, nOld);
	}

	if (!bInnerError) {
	    interp->nErrorLine = interp->nLine;
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, zRes, nRes);
	    TH8_STR_APPEND(
	        interp, &zInfo, &nInfo, "\n    while executing\n\"",
	        TH8_NOLEN);
	} else {
	    TH8_STR_APPEND(
	        interp, &zInfo, &nInfo, "\n    invoked from within\n\"",
	        TH8_NOLEN);
	}

	{
	    size_t nCmdLen;

	    nCmdLen = (size_t)(pState->zInput - pState->zFirst);
	    if (nCmdLen > 150) nCmdLen = 150;
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, pState->zFirst, nCmdLen);
	}
	TH8_STR_APPEND(interp, &zInfo, &nInfo, "\"", 1);
	Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, zInfo, nInfo);
	Th8_SetResult(interp, zRes, nRes);
	Th8_Free(interp, zRes);
	Th8_Free(interp, zInfo);
#else
	interp->nErrorLine = interp->nLine;
#endif
    }

    /*
     * If the command succeeded, push the next iteration.
     * On error (or break/return/continue), fall through --
     * the th8EvalCleanup callback below us will handle
     * depth/line restoration.
     */

    if (rc == TH8_OK) {
	Th8_NRAddCallback(interp, th8EvalIteration, pState, 0, 0, 0);
    }
    return rc;

#if defined(TH8_ENABLE_VARIABLES)
oom:
    /* A TH8_STR_APPEND growth failed while building the error trace;
     * "out of memory" already set.  argv was already freed above.  The
     * command had already errored, so return the error. */
    Th8_Free(interp, zRes);
    Th8_Free(interp, zInfo);
    return TH8_ERROR;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8EvalIteration --
 *
 *	NRE callback that processes ONE command from the script.
 *	This replaces the body of the old while loop in th8EvalLocal.
 *
 *	The function:
 *	  1. Skips semicolons, whitespace, and comments.
 *	  2. Scans to the end of the command.
 *	  3. Splits the command into words (with substitution).
 *	  4. Looks up the command name.
 *	  5. Pushes th8EvalPostCmd (runs after the command).
 *	  6. Dispatches the command via xProc.
 *
 *	The command's NRE callbacks (if any) are pushed above
 *	th8EvalPostCmd.  LIFO ordering ensures the command's
 *	callbacks complete first, then th8EvalPostCmd frees argv
 *	and pushes the next iteration.
 *
 * Why / How:
 *	Each invocation processes exactly one command, keeping the
 *	C stack constant regardless of script length.  If the command
 *	text contains '[', the NRE-aware word-splitting path is used
 *	(heap-allocated CmdBuild + NRE callbacks); otherwise the
 *	synchronous th8SplitCommand is used for speed.  After word
 *	splitting, command lookup walks current-then-global namespace
 *	with an "unknown" handler fallback.
 *
 * Results:
 *	Return code from the command dispatch (or error).
 *
 * Side effects:
 *	One command is parsed and dispatched.  Callbacks are pushed.
 *
 *----------------------------------------------------------------------
 */

static int
th8EvalIteration(
    Th8_Interp *interp, /* Interpreter. */
    void *pData[], /* [0]=Th8_EvalState*. */
    int rc) /* Return code from previous step. */
{
    Th8_EvalState *pState = (Th8_EvalState *)pData[0];
    Th8_HashEntry *pEntry;
    size_t nSpace;
    char **argv = NULL;
    size_t *argl = NULL;
    int argc = 0;
#if defined(TH8_ENABLE_VARIABLES)
    /* Function-scope so the oom label can free the error-trace
     * accumulators (reached only from the substitution-error handler,
     * where argv/argl are still NULL). */
    char *zRes = 0;
    size_t nRes = 0;
    char *zInfo = 0;
    size_t nInfo = 0;
#endif

    /*
     * If the previous step failed, don't process more commands.
     * Fall through to th8EvalCleanup which is below us.
     */

    if (rc != TH8_OK) {
	return rc;
    }

    /*
     * Skip semicolons, whitespace, and comments until we find
     * a real command or exhaust the input.
     */

    for (;;) {
	/*
	 * Skip semicolons.
	 */

	if (pState->nInput > 0 && *pState->zInput == ';') {
	    pState->zInput++;
	    pState->nInput--;
	}

	/*
	 * Skip whitespace, counting newlines for line tracking.
	 */

	th8NextSpace(interp, pState->zInput, pState->nInput, &nSpace);
	th8CountNewlines(interp, pState->zInput, nSpace);
	pState->zInput += nSpace;
	pState->nInput -= nSpace;
	if (pState->nInput == 0) {
	    /*
	     * Script exhausted -- fall through to th8EvalCleanup.
	     */

	    return TH8_OK;
	}
	pState->zFirst = pState->zInput;

	/*
	 * Check for comment.
	 */

	if (pState->zInput[0] == '#') {
	    while (pState->nInput > 0 &&
	           !th8EndOfLine(pState->zInput, pState->nInput)) {
		pState->zInput++;
		pState->nInput--;
	    }
	    th8CountNewlines(
	        interp, pState->zFirst,
	        (size_t)(pState->zInput - pState->zFirst));
	    continue;
	}

	/* Found a real command -- break out of the skip loop. */
	break;
    }

    /*
     * Scan to end of command (newline or semicolon at
     * nesting depth 0).
     */

    {
	size_t nWord = 0;

	while (rc == TH8_OK && pState->nInput > 0 && *pState->zInput != ';' &&
	       !th8EndOfLine(pState->zInput, pState->nInput)) {
	    th8NextSpace(interp, pState->zInput, pState->nInput, &nSpace);
	    th8CountNewlines(interp, pState->zInput, nSpace);
	    rc = th8NextWord(
	        interp, &pState->zInput[nSpace], pState->nInput - nSpace,
	        &nWord, 1);
	    th8CountNewlines(interp, &pState->zInput[nSpace], nWord);
	    pState->zInput += nSpace + nWord;
	    pState->nInput -= nSpace + nWord;
	}
	if (rc != TH8_OK) {
	    /*
	     * Scan error.  Return the error code directly;
	     * th8EvalCleanup (below us in the chain) will
	     * handle depth/line restoration.
	     */

	    return rc;
	}
    }

    /*
     * Split the command into words with substitution.
     *
     * If the command text contains '[', use the NRE-aware
     * path that pushes Th8_NREval callbacks for each [cmd]
     * substitution.  This allows yield inside [cmd] to
     * correctly save/restore the word-splitting state.
     *
     * Otherwise, use the synchronous th8SplitCommand for
     * maximum performance (no heap allocation for the
     * CmdBuild state, no extra callbacks).
     */

    {
	const char *zCmd = pState->zFirst;
	size_t nCmd = (size_t)(pState->zInput - pState->zFirst);

	if (th8CmdHasBracket(zCmd, nCmd)) {
	    Th8_CmdBuild *pBuild;

	    pBuild = (Th8_CmdBuild *)TH8_ALLOC(interp, sizeof(Th8_CmdBuild));
	    if (!pBuild) {
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    th8BufInit(&pBuild->strbuf);
	    th8BufInit(&pBuild->lenbuf);
	    th8BufInit(&pBuild->wordBuf);
	    pBuild->nCount = 0;
	    pBuild->zInput = zCmd;
	    pBuild->nInput = nCmd;
	    pBuild->nOrigWord = 0;
	    pBuild->wordPos = 0;
	    pBuild->wordEnd = 0;
	    pBuild->bWordActive = 0;
	    pBuild->bExpand = 0;
	    pBuild->wasListMode = interp->isListMode;
	    pBuild->pState = pState;
	    interp->isListMode = 0;

	    /*
	     * Push dispatch callback (bottom - runs AFTER all
	     * words are processed, assembles argv, dispatches
	     * the command).
	     */

	    Th8_NRAddCallback(interp, th8NRCmdDispatch, pBuild, pState, 0, 0);

	    /*
	     * Start NRE word processing.  Returns TH8_OK
	     * immediately if a [cmd] is found (eval pushed
	     * onto the chain).  Returns TH8_OK when all
	     * words are done (falls through to th8NRCmdDispatch).
	     */

	    return th8NRSubstAndBuild(interp, pBuild);
	}
    }

    rc = th8SplitCommand(
        interp, pState->zFirst, (size_t)(pState->zInput - pState->zFirst),
        &argv, &argl, &argc, pState->zName, pState->nName);
    if (rc == TH8_SUSPEND) {
	/*
	 * Freeze detected during word splitting -- a debug breakpoint
	 * (R-54392) or an async Th8_Freeze -- BEFORE this command was
	 * dispatched.  This is the boundary Th8_Ready in th8SplitCommand;
	 * th8SplitCommand allocates no argv on the suspend path, so there
	 * is nothing to free.  To honor "resume from the exact point of
	 * suspension", rewind pState to the START of the current command
	 * (this iteration's top scan already advanced pState->zInput to
	 * the command's END) and push a fresh th8EvalIteration.  After
	 * Th8_Thaw re-attaches and drains the suspended chain, iteration
	 * re-processes this not-yet-run command and everything after it.
	 * Without the rewind + re-push, only the bottom cleanup callbacks
	 * survive the suspend-detach, silently dropping every remaining
	 * command (the deeper half of Bug 73).  The newline preceding the
	 * command was consumed before zFirst, so the re-scan does not
	 * re-count it.
	 */

	pState->nInput += (size_t)(pState->zInput - pState->zFirst);
	pState->zInput = pState->zFirst;
	if (Th8_NRAddCallback(interp, th8EvalIteration, pState, 0, 0, 0) !=
	    TH8_OK) {
	    /* Push failed: the cleanup callback below us still frees
	     * pState when the caller drains the chain. */
	    return TH8_ERROR;
	}
	return TH8_SUSPEND;
    }
    if (rc == TH8_YIELD) {
	/*
	 * Defensive: word splitting itself does not yield (yield is a
	 * command that suspends from dispatch, where the continuation is
	 * already on the chain).  Propagate -- th8EvalCleanup handles it.
	 */

	return rc;
    }
    if (rc != TH8_OK) {
	/*
	 * Substitution error (e.g., bad variable reference).
	 * Record the error line and build errorInfo so that
	 * outer evals don't overwrite nErrorLine.
	 */

	interp->nErrorLine = interp->nLine;

#if defined(TH8_ENABLE_VARIABLES)
	{
	    zRes = Th8_TakeResult(interp, &nRes);
	    TH8_STR_APPEND(
	        interp, &zInfo, &nInfo, "\n    while executing\n\"",
	        TH8_NOLEN);
	    {
		size_t nCmdLen;

		nCmdLen = (size_t)(pState->zInput - pState->zFirst);
		if (nCmdLen > 150) nCmdLen = 150;
		TH8_STR_APPEND(
		    interp, &zInfo, &nInfo, pState->zFirst, nCmdLen);
	    }
	    TH8_STR_APPEND(interp, &zInfo, &nInfo, "\"", 1);
	    Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, zInfo, nInfo);
	    Th8_SetResult(interp, zRes, nRes);
	    Th8_Free(interp, zRes);
	    Th8_Free(interp, zInfo);
	}
#endif

	/*
	 * Return the error code directly.  th8EvalCleanup
	 * (below us in the chain) handles depth/line
	 * restoration.
	 */

	return rc;
    }

    if (argc == 0) {
	/*
	 * Empty command (e.g., blank line after substitution).
	 * Free argv and push the next iteration.
	 */

	Th8_Free(interp, argv);
	Th8_NRAddCallback(interp, th8EvalIteration, pState, 0, 0, 0);
	return TH8_OK;
    }

    /*
     * Reject a tainted command name.  Shared with th8NRCmdDispatch via
     * th8CmdNameTainted so the sync and async paths stay in lock-step.
     * Report and fail closed; argv is freed by th8EvalPostCmd like any
     * other dispatch error.
     */

    if (th8CmdNameTainted(interp, argv[0], argl[0])) {
	Th8_NRAddCallback(interp, th8EvalPostCmd, pState, argv, 0, 0);
	return TH8_ERROR;
    }

    /*
     * Look up the command name.  If the name contains
     * "::", resolve it in the specific namespace.
     * Otherwise, search current namespace first, then
     * fall back to the global namespace.
     */

    {
	const char *zCmdNs;
	size_t nCmdNs;
	const char *zCmdTail;
	size_t nCmdTail;

	th8SplitQualName(
	    argv[0], TH8_LEN(argl[0]), &zCmdNs, &nCmdNs, &zCmdTail,
	    &nCmdTail);

	if (zCmdNs) {
	    /*
	     * Qualified name -- look up in the
	     * specific namespace only.
	     */

	    Th8_Namespace *pNs;

	    pNs = th8FindNamespace(interp, zCmdNs, nCmdNs, 0);
	    if (pNs) {
		pEntry =
		    Th8_HashFind(interp, pNs->paCmd, zCmdTail, nCmdTail, 0);
	    } else {
		pEntry = 0;
	    }
	} else {
	    /*
	     * Simple name -- try current namespace,
	     * then global.
	     */

	    pEntry = Th8_HashFind(
	        interp, interp->pCurrentNs->paCmd, argv[0], TH8_LEN(argl[0]),
	        0);
	    if (!pEntry && interp->pCurrentNs != interp->pGlobalNs) {
		pEntry = Th8_HashFind(
		    interp, interp->pGlobalNs->paCmd, argv[0],
		    TH8_LEN(argl[0]), 0);
	    }
	}
    }

    if (!pEntry) {
	/*
	 * Try the 'unknown' handler.  Build an
	 * extended argv with "unknown" prepended.
	 */

	Th8_HashEntry *pUnk = 0;

	if (!interp->bInUnknown) {
	    pUnk = Th8_HashFind(
	        interp, interp->pGlobalNs->paCmd, "unknown", 7, 0);
	}
	if (pUnk) {
	    Th8_Command *pCmd;
	    char **azNew;
	    size_t *anNew;
	    int nNew = argc + 1;
	    size_t nAlloc = 0;
	    int k;

	    /*
	     * Build new argv: {"unknown", argv[0],
	     * argv[1], ...}
	     */

	    {
		size_t t1 = 0, t2 = 0;

		/* Else-if ladder for single-condition MC/DC.  See
		 * FINDINGS.md Finding 005. */
		int overflowed = 0;

		if (TH8_SAFE_MUL_SIZE(sizeof(char *), (size_t)nNew, &t1)) {
		    overflowed = 1;
		} else if (
		    TH8_SAFE_MUL_SIZE(sizeof(size_t), (size_t)nNew, &t2)) {
		    overflowed = 1;
		} else if (TH8_SAFE_ADD_SIZE(t1, t2, &nAlloc)) {
		    overflowed = 1;
		}
		if (overflowed) {
		    Th8_Free(interp, argv);
		    Th8_SetResult(interp, "too many arguments", TH8_NOLEN);
		    return TH8_ERROR;
		}
	    }
	    azNew = (char **)TH8_ALLOC(interp, nAlloc);
	    if (!azNew) {
		Th8_Free(interp, argv);
		Th8_SetResult(interp, "out of memory", TH8_NOLEN);
		return TH8_ERROR;
	    }
	    anNew = (size_t *)&azNew[nNew];
	    azNew[0] = (char *)"unknown";
	    anNew[0] = 7;
	    for (k = 0; k < argc; k++) {
		azNew[k + 1] = argv[k];
		anNew[k + 1] = argl[k];
	    }

	    /*
	     * Push th8EvalPostCmd to run after the unknown
	     * handler completes.  It frees argv, resets
	     * bInUnknown, and handles error traces.
	     *
	     * pData[2] = (void*)1 signals the unknown path
	     * so th8EvalPostCmd resets bInUnknown after the
	     * command's NRE callbacks have all completed.
	     *
	     * pData[3] = azNew so th8EvalPostCmd frees it
	     * AFTER all NRE callbacks have finished using
	     * the argument pointers it contains.  azNew must
	     * NOT be freed synchronously because the command
	     * may push NRE callbacks (e.g. proc_call_nr)
	     * that reference the argv/argl arrays.
	     */

	    Th8_NRAddCallback(
	        interp, th8EvalPostCmd, pState, argv, TH8_INT2PTR(1), azNew);

	    pCmd = (Th8_Command *)pUnk->pData;
	    interp->bInUnknown = 1;
	    rc = pCmd->xProc(
	        interp, pCmd->pContext, nNew, (const char **)azNew, anNew);

	    /*
	     * Do NOT reset bInUnknown here -- the command
	     * may have pushed NRE callbacks that need the
	     * guard active.  th8EvalPostCmd resets it after
	     * all callbacks complete.
	     *
	     * Do NOT free azNew here -- it is freed by
	     * th8EvalPostCmd via pData[3].
	     */

	    return rc;
	}
	if (!pUnk) {
	    Th8_ErrorMessage(
	        interp, "no such command:", argv[0], TH8_LEN(argl[0]));
	    rc = TH8_ERROR;
	}
    }

    /*
     * Readiness check per command invocation.
     * This catches cancellation and step-limit between
     * the command lookup and the actual invocation.
     */

    if (rc == TH8_OK) {
	rc = Th8_Ready(interp);
    }

    /*
     * Invoke the command via NRE.
     *
     * Push th8EvalPostCmd FIRST (runs AFTER the command due
     * to LIFO).  Then dispatch the command.  The command may
     * push its own NRE callbacks above th8EvalPostCmd; those
     * will run first.  When they all complete, th8EvalPostCmd
     * frees argv, builds error traces, and pushes the next
     * iteration.
     */

    if (rc == TH8_OK) {
	Th8_Command *p;

	p = (Th8_Command *)pEntry->pData;

	Th8_NRAddCallback(interp, th8EvalPostCmd, pState, argv, 0, 0);
	rc = p->xProc(interp, p->pContext, argc, (const char **)argv, argl);

	/*
	 * Return rc to the trampoline.  The command's NRE
	 * callbacks (if any) are above th8EvalPostCmd and
	 * will be drained first.  We do NOT call
	 * th8RunCallbacks here -- the trampoline handles it.
	 */

	return rc;
    }

    /*
     * If we get here, the command lookup or readiness check
     * failed.  Free argv and let th8EvalPostCmd handle the
     * error trace.  (We push th8EvalPostCmd so error tracing
     * is consistent.)
     */

    Th8_NRAddCallback(interp, th8EvalPostCmd, pState, argv, 0, 0);
    return rc;

#if defined(TH8_ENABLE_VARIABLES)
oom:
    /* A TH8_STR_APPEND growth failed while building the error trace in
     * the substitution-error handler (argv/argl are NULL there);
     * "out of memory" already set. */
    Th8_Free(interp, zRes);
    Th8_Free(interp, zInfo);
    return TH8_ERROR;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * th8EvalLocal --
 *
 *	Core script evaluation entry point.  Sets up the NRE callback
 *	chain that will evaluate a script one command at a time via
 *	the trampoline.
 *
 * Why / How:
 *	This is the engine's internal eval.  It performs four phases
 *	before scheduling NRE callbacks:
 *
 *	  1. Security gate -- reject tainted scripts.
 *	  2. Recursion depth limit (counter-based, max 1000).
 *	  3. Unified readiness check (stack, cancel, suspend, step).
 *	  4. Policy callback (PRE phase).
 *
 *	Then it allocates an EvalState, pushes the cleanup callback
 *	(bottom of LIFO), and pushes the first iteration callback
 *	(top of LIFO).  The trampoline drains these callbacks,
 *	evaluating one command per iteration.
 *
 * Results:
 *	A TH8 return code (typically TH8_OK or TH8_ERROR).
 *
 * Side effects:
 *	NRE callbacks are pushed; nEvalDepth is incremented (the
 *	cleanup callback decrements it).  Commands in the script
 *	are executed when the trampoline drains the chain.
 *
 *----------------------------------------------------------------------
 */

static int
th8EvalLocal(
    Th8_Interp *interp, /* Interpreter. */
    const char *zProgram, /* Script to evaluate. */
    size_t nProgram, /* Script length. */
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName, /* Origin name length. */
    int flags) /* Eval flags (TH8_EVAL_TRUSTED etc). */
{
    int rc = TH8_OK;
    size_t nInput = TH8_LEN(nProgram);
    int nSavedLine;
    Th8_EvalState *pState;

    /*
     * PHASE 1: Security gate -- reject tainted scripts.
     * The TH8_TAINTED macro checks a high bit in the length
     * field; tainted strings come from untrusted sources.
     */

    if (TH8_TAINTED(nProgram) &&
        Th8_ReportTaint(interp, "script", zProgram, nProgram)) {
	return TH8_ERROR;
    }

    /*
     * PHASE 2: Recursion depth limit (counter-based).
     * This is a second line of defense against C stack overflow,
     * independent of the native stack check in Th8_Ready.
     * 1000 is deliberately conservative; deeply nested scripts
     * hit this before the C stack is threatened.
     */

    if (interp->nEvalDepth > 1000) {
	Th8_SetResult(interp, "TH8 recursion too deep", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * PHASE 3: Unified readiness check.
     * Th8_Ready calls, in order:
     *   1. th8CheckStack -- native stack guard zone.
     *   2. Th8_IsCanceled   -- pending cancellation.
     *   3. bSuspended     -- pending suspension.
     *   4. th8Step       -- step counter limit.
     *
     * TH8_SUSPEND is preserved as-is so the caller can detect
     * the freeze; all other non-TH8_OK codes map to TH8_ERROR.
     */

    {
	int readyRc = Th8_Ready(interp);

	if (readyRc == TH8_SUSPEND) {
	    return TH8_SUSPEND;
	}
	if (readyRc != TH8_OK) {
	    return TH8_ERROR;
	}
    }

    nSavedLine = interp->nLine;
    interp->nEvalDepth++;
    interp->nLine = 1;

    /*
     * PHASE 4: PRE-phase policy callback (EVAL).
     * The host can register a hook (via Th8_SetPolicyCallback)
     * to inspect or reject scripts before parsing begins.
     * If the hook returns non-TH8_OK, evaluation is aborted.
     */

    if (interp->xPolicyCb) {
	rc = interp->xPolicyCb(
	    interp, TH8_PHASE_PRE | TH8_PHASE_EVAL, zName, nName, zProgram,
	    nInput, flags, TH8_OK, interp->pPolicyCbCtx);
	if (rc != TH8_OK) {
	    interp->nEvalDepth--;
	    if (interp->nEvalDepth == 0 && interp->pPendingHead) {
		th8DrainPendingDeletes(interp);
	    }
	    interp->nLine = nSavedLine;
	    return rc;
	}
    }

    /*
     * PHASE 5: NRE-driven eval loop.
     *
     * Allocate the EvalState on the heap and push NRE callbacks:
     *
     *   Bottom: th8EvalCleanup  (restores depth/line, frees state)
     *   Top:    th8EvalIteration (processes the first command)
     *
     * th8EvalLocal returns TH8_OK immediately.  The trampoline
     * (or the caller's th8RunCallbacks) drives the iteration
     * callbacks.  Each th8EvalIteration processes one command,
     * pushes th8EvalPostCmd for cleanup, and dispatches.  After
     * the command completes, th8EvalPostCmd pushes the next
     * th8EvalIteration.  When the script is exhausted,
     * th8EvalIteration returns without pushing more callbacks,
     * and th8EvalCleanup runs to finalize.
     */

    pState = (Th8_EvalState *)TH8_ALLOC(interp, sizeof(Th8_EvalState));
    if (!pState) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    pState->zProgram = zProgram;
    pState->zInput = zProgram;
    pState->zFirst = zProgram;
    pState->nInput = nInput;
    pState->nOrigInput = nInput;
    pState->nSavedLine = nSavedLine;
    pState->flags = flags;
    pState->zName = zName;
    pState->nName = nName;

    /*
     * Push cleanup first (LIFO -- runs last), then first iteration.
     *
     * Propagate a callback-scheduling failure instead of reporting a
     * false success (Bug 61 Sibling 2).  Th8_NRAddCallback returns
     * TH8_ERROR with an "out of memory" result when the callback
     * allocation fails; returning TH8_OK unconditionally would leave
     * that OOM result in place while claiming the eval succeeded, and
     * the script would silently never run.
     */

    if (Th8_NRAddCallback(interp, th8EvalStateCleanup, pState, 0, 0, 0) !=
        TH8_OK) {
	/*
	 * The cleanup callback itself could not be scheduled, so no
	 * callback will run to release pState or undo this frame's
	 * depth/line bookkeeping.  Do it here, mirroring the
	 * PRE-policy reject path above.
	 */
	interp->nEvalDepth--;
	if (interp->nEvalDepth == 0 && interp->pPendingHead) {
	    th8DrainPendingDeletes(interp);
	}
	interp->nLine = nSavedLine;
	Th8_Free(interp, pState);
	return TH8_ERROR;
    }
    if (Th8_NRAddCallback(interp, th8EvalIteration, pState, 0, 0, 0) !=
        TH8_OK) {
	/*
	 * The cleanup callback IS on the chain; returning TH8_ERROR
	 * lets the caller's th8RunCallbacks drain it, which frees
	 * pState and restores depth/line.  Do not free pState here.
	 */
	return TH8_ERROR;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * List operations --
 *
 *	Bootstrap implementations.  Full Spilornis.c-based versions
 *	in a later phase.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_SplitList --
 *
 *	Split a list string into elements.
 *
 * Why / How:
 *	Bootstrap path for interpreters that don't have the Spilornis
 *	bridge compiled in.  Checks the internal-representation cache
 *	first.  On a cache miss, falls through to the Spilornis path
 *	(if available) or returns an error.  The cached-path element
 *	count is limited to TH8_MAX_LIST_ELEMENTS and the allocation
 *	uses overflow-safe arithmetic.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on malformed list.
 *
 * Side effects:
 *	Allocates one combined block (pointer array + length array +
 *	element bytes) when pazElem is non-NULL; *panElem points INTO
 *	it.  The caller frees the whole result with Th8_Free(*pazElem)
 *	only -- never *panElem or the individual strings.  See the
 *	public contract in th8.h.
 *
 *----------------------------------------------------------------------
 */

/*
 * Spilornis (Eagle list parser) forward declarations.
 * Spilornis.c is compiled separately with th8_spilornis.h.
 * In TH8's narrow/UTF-8 mode, LPCWSTR = const char*.
 */

/*
 * Spilornis CRT bridge -- global interpreter pointer.
 *
 * WHY A GLOBAL IS NEEDED:
 *
 * Spilornis.c (the Eagle list parser) was designed against standard
 * CRT functions (calloc, free, memcpy, etc.).  In TH8, those are
 * not available -- all memory and string operations must go through
 * the Th8_Platform function pointer table, which requires a
 * Th8_Interp*.  However, Spilornis's API does not accept an
 * interpreter parameter.
 *
 * THE SET/CLEAR PATTERN:
 *
 * Before each call to Eagle_SplitList or Eagle_JoinList, the caller
 * sets th8_spilornis_interp to the active interpreter.  This allows
 * the bridge functions below (th8_spilornis_calloc, etc.) to route
 * through the platform.  Immediately after the Spilornis call
 * returns, th8_spilornis_interp is set back to NULL to prevent
 * stale use.
 *
 * This is safe because the variable uses thread-local storage, so
 * each thread has its own copy.  No mutex is needed.
 *
 * NOTE: This pattern is inherently non-reentrant within a single
 * thread.  If a future version of Spilornis supports a context
 * parameter, this thread-local should be eliminated.
 */

static TH8_THREAD_LOCAL Th8_Interp *volatile th8_spilornis_interp = 0;


/*
 *----------------------------------------------------------------------
 *
 * th8SpilornisSetup --
 *
 *	Prepare the thread-local Spilornis context for a bridge call.
 *	Acquires the global mutex (on platforms without real TLS)
 *	and sets the thread-local interpreter pointer so that the
 *	Spilornis CRT shim functions can route through the TH8
 *	platform allocator.
 *
 * Why / How:
 *	Spilornis.c is a pure-C library with no context parameter;
 *	it reaches the TH8 allocator through the thread-local
 *	th8_spilornis_interp.  The mutex serializes access on
 *	platforms that emulate TLS with a global.  The saved
 *	pointer is returned so the caller can restore it for
 *	nested calls.
 *
 * Results:
 *	The previous th8_spilornis_interp value (for restoration).
 *
 * Side effects:
 *	th8_spilornis_interp is set to interp; the global mutex
 *	may be acquired.
 *
 *----------------------------------------------------------------------
 */

static Th8_Interp *
th8SpilornisSetup(Th8_Interp *interp)
{
    Th8_Interp *pSaved;

    th8MaybeGlobalMutexEnter(interp);
    pSaved = th8_spilornis_interp;
    th8_spilornis_interp = interp;
    return pSaved;
}

/*
 *----------------------------------------------------------------------
 *
 * th8SpilornisTeardown --
 *
 *	Restore the thread-local Spilornis context after a bridge
 *	call.  Releases the global mutex that th8SpilornisSetup
 *	acquired.
 *
 * Why / How:
 *	The saved pointer from th8SpilornisSetup is restored so
 *	that nested calls (e.g. Th8_SplitList inside a list
 *	operation) see the correct interpreter.  The mutex release
 *	must happen after the pointer restoration to prevent a
 *	TOCTOU race.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	th8_spilornis_interp is restored; the global mutex may
 *	be released.
 *
 *----------------------------------------------------------------------
 */

static void
th8SpilornisTeardown(Th8_Interp *pSaved)
{
    th8_spilornis_interp = pSaved;
    th8MaybeGlobalMutexLeave(pSaved);
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_calloc --
 *
 *	Spilornis CRT bridge: allocate zero-filled memory via the
 *	TH8 platform allocator.
 *
 * Why / How:
 *	Spilornis.c calls calloc() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp (set by
 *	th8SpilornisSetup) and routes through Th8_AttemptMalloc,
 *	which zero-fills the block.  Returns NULL if the thread-
 *	local interp is not set.
 *
 * Results:
 *	Pointer to the allocated block, or NULL.
 *
 * Side effects:
 *	Memory is allocated via Th8_AttemptMalloc.
 *
 *----------------------------------------------------------------------
 */

void *
th8_spilornis_calloc(size_t count, size_t size)
{
    /* TH8_ALLOC_MUL zero-fills (calloc semantics). */
    if (th8_spilornis_interp) {
	return TH8_ALLOC_MUL(th8_spilornis_interp, count, size);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_free --
 *
 *	Spilornis CRT bridge: free memory via the TH8 platform.
 *
 * Why / How:
 *	Spilornis.c calls free() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp and routes the
 *	deallocation through Th8_Free.  If the thread-local interp
 *	is NULL, the call is silently ignored (defensive no-op).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed via Th8_Free.
 *
 *----------------------------------------------------------------------
 */

void
th8_spilornis_free(void *p)
{
    if (th8_spilornis_interp) {
	Th8_Free(th8_spilornis_interp, p);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_memcpy --
 *
 *	Spilornis CRT bridge: copy memory via the TH8 platform.
 *
 * Why / How:
 *	Spilornis.c calls memcpy() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp and routes
 *	through Th8_Memcpy.  If the interp is NULL, returns dst
 *	unchanged (no-op fallback).
 *
 * Results:
 *	Pointer to the destination buffer.
 *
 * Side effects:
 *	Memory is copied.
 *
 *----------------------------------------------------------------------
 */

void *
th8_spilornis_memcpy(void *d, const void *s, size_t n)
{
    if (th8_spilornis_interp) {
	return Th8_Memcpy(th8_spilornis_interp, d, s, n);
    }
    return d;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_memset --
 *
 *	Spilornis CRT bridge: fill memory via the TH8 platform.
 *
 * Why / How:
 *	Spilornis.c calls memset() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp, obtains the
 *	platform's xMemset callback, and invokes it directly.
 *	If the interp or callback is NULL, no fill occurs.
 *
 * Results:
 *	Pointer to the destination buffer.
 *
 * Side effects:
 *	Memory is filled.
 *
 *----------------------------------------------------------------------
 */

void *
th8_spilornis_memset(void *d, int c, size_t n)
{
    if (th8_spilornis_interp) {
	Th8_Platform *p = th8_spilornis_interp->pPlatform;

	if (p->xMemset) {
	    p->xMemset(th8_spilornis_interp, p->pCtx, d, c, n);
	}
    }
    return d;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_memcmp --
 *
 *	Spilornis CRT bridge: compare memory via the TH8 platform.
 *
 * Why / How:
 *	Spilornis.c calls memcmp() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp and routes
 *	through Th8_Memcmp.  Returns 0 (equal) as a safe default
 *	if the interp is NULL.
 *
 * Results:
 *	Negative, zero, or positive integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_spilornis_memcmp(const void *a, const void *b, size_t n)
{
    if (th8_spilornis_interp) {
	return Th8_Memcmp(th8_spilornis_interp, a, b, n);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_strlen --
 *
 *	Spilornis CRT bridge: compute string length via the TH8
 *	platform, with a fallback byte-scan loop.
 *
 * Why / How:
 *	Spilornis.c calls strlen() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp and uses the
 *	platform's xStrlen if available.  If the interp is NULL
 *	or xStrlen is missing, a manual byte-scan loop provides
 *	a CRT-free fallback.
 *
 * Results:
 *	Number of bytes before the NUL terminator.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
th8_spilornis_strlen(const char *s)
{
    if (th8_spilornis_interp) {
	Th8_Platform *p = th8_spilornis_interp->pPlatform;

	if (p->xStrlen) {
	    return p->xStrlen(th8_spilornis_interp, p->pCtx, s);
	}
    }
    {
	size_t n = 0;
	while (s[n])
	    n++;
	return n;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_strncmp --
 *
 *	Spilornis CRT bridge: compare strings (up to n bytes) via the
 *	TH8 platform, with a fallback byte-by-byte comparison loop.
 *
 * Why / How:
 *	Spilornis.c calls strncmp() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp and uses the
 *	platform's xMemcmp for the comparison.  If the interp is
 *	NULL or xMemcmp is missing, a manual unsigned-byte
 *	comparison loop provides a CRT-free fallback.
 *
 * Results:
 *	Negative, zero, or positive integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8_spilornis_strncmp(const char *a, const char *b, size_t n)
{
    if (th8_spilornis_interp) {
	Th8_Platform *p = th8_spilornis_interp->pPlatform;

	if (p->xMemcmp) {
	    return p->xMemcmp(th8_spilornis_interp, p->pCtx, a, b, n);
	}
    }
    while (n-- > 0) {
	if ((unsigned char)*a != (unsigned char)*b) {
	    return (unsigned char)*a - (unsigned char)*b;
	}
	if (*a == 0) break;
	a++;
	b++;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_strncpy --
 *
 *	Spilornis CRT bridge: copy a string with NUL padding (strncpy
 *	semantics).  Implemented without libc.
 *
 * Why / How:
 *	Spilornis.c calls strncpy() through the CRT bridge macro.
 *	Unlike the other bridge functions, this one does not need
 *	th8_spilornis_interp because it uses a simple inline loop
 *	that copies bytes until NUL and then zero-pads the remainder
 *	-- pure CRT-free implementation.
 *
 * Results:
 *	Pointer to the destination buffer.
 *
 * Side effects:
 *	Destination is written.
 *
 *----------------------------------------------------------------------
 */

char *
th8_spilornis_strncpy(char *d, const char *s, size_t n)
{
    size_t i;

    for (i = 0; i < n && s[i]; i++) {
	d[i] = s[i];
    }
    for (; i < n; i++) {
	d[i] = 0;
    }
    return d;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_snprintf --
 *
 *	Spilornis CRT bridge: formatted output via the TH8 platform's
 *	xVsnprintf callback.
 *
 * Why / How:
 *	Spilornis.c calls snprintf() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp, converts the
 *	variadic arguments to a va_list, and delegates to the
 *	platform's xVsnprintf.  Returns 0 if the interp or
 *	callback is unavailable.
 *
 * Results:
 *	Number of characters written, or 0 if unavailable.
 *
 * Side effects:
 *	Writes to the output buffer.
 *
 *----------------------------------------------------------------------
 */

int
th8_spilornis_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    int ret = 0;

    /*
     * Match C99 snprintf semantics: when the buffer has any size at
     * all, the result must be NUL-terminated even on the failure
     * paths below.  Without this, callers that ignore the return
     * value (treating buf as a valid C string) would feed
     * uninitialised stack memory into downstream %s formatters --
     * exactly the failure pattern that valgrind flagged in the
     * OOM trace path of th8SafeAlloc.
     */
    if (size > 0) {
	buf[0] = '\0';
    }

    if (th8_spilornis_interp) {
	Th8_Platform *p = th8_spilornis_interp->pPlatform;

	if (p->xVsnprintf) {
	    va_list ap;

	    va_start(ap, fmt);
	    ret = p->xVsnprintf(
	        th8_spilornis_interp, p->pCtx, buf, size, fmt, ap);
	    va_end(ap);
	    return ret;
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_vsnprintf --
 *
 *	Spilornis CRT bridge: va_list formatted output via the TH8
 *	platform's xVsnprintf callback.
 *
 * Why / How:
 *	Spilornis.c calls vsnprintf() through the CRT bridge macro.
 *	This function reads th8_spilornis_interp and delegates
 *	directly to the platform's xVsnprintf with the caller's
 *	va_list.  Returns 0 if the interp or callback is unavailable.
 *
 * Results:
 *	Number of characters written, or 0 if unavailable.
 *
 * Side effects:
 *	Writes to the output buffer.
 *
 *----------------------------------------------------------------------
 */

int
th8_spilornis_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    /*
     * NUL-terminate before any failure-return path so callers that
     * ignore the return value still get a valid (empty) C string.
     * See th8_spilornis_snprintf for full rationale.
     */
    if (size > 0) {
	buf[0] = '\0';
    }

    if (th8_spilornis_interp) {
	Th8_Platform *p = th8_spilornis_interp->pPlatform;

	if (p->xVsnprintf) {
	    return p->xVsnprintf(
	        th8_spilornis_interp, p->pCtx, buf, size, fmt, ap);
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8_spilornis_memsize --
 *
 *	Spilornis CRT bridge: return the allocation size.  Not needed
 *	when using the TH8 platform allocator; always returns 0.
 *
 * Why / How:
 *	Spilornis.c may call _msize() (MSVC) or malloc_usable_size()
 *	through the CRT bridge macro.  The TH8 platform allocator
 *	does not expose allocation sizes, so this always returns 0.
 *	Spilornis tolerates a zero return gracefully.
 *
 * Results:
 *	Always 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

size_t
th8_spilornis_memsize(void *p)
{
    /* Memory size tracking is not needed when using the
     * TH8 platform allocator.  Return 0. */
    (void)p;
    return 0;
}


/*
 * Spilornis (Eagle list parser) function prototypes from Spilornis.h.
 * th8_spilornis.h sets up the narrow-char type overrides (WCHAR=char)
 * so that Spilornis.h's prototypes use the correct se_* types.
 */
#include "th8_spilornis.h"
#include "Spilornis.h"

/*
 *----------------------------------------------------------------------
 *
 * Th8_SplitList --
 *
 *	Split a Tcl list string into elements using the Eagle
 *	(Spilornis) list parser.
 *
 * Why / How:
 *	First checks the internal-representation cache for a prior
 *	split of the same string.  On a miss, sets the thread-local
 *	th8_spilornis_interp (via th8SpilornisSetup) so that the
 *	Spilornis CRT bridge functions can route through the TH8
 *	platform allocator, then calls Eagle_SplitList.  Results
 *	are copied from Spilornis-allocated buffers into a single
 *	Th8_AttemptMalloc block (pointers + lengths + string data)
 *	so callers can free with Th8_Free.  The split result is
 *	then stored in the cache for future lookups.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on malformed list.
 *
 * Side effects:
 *	Allocates element arrays via Th8_Malloc if pazElem is non-NULL.
 *	Caller must free with Th8_Free.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SplitList(
    Th8_Interp *interp, /* Interpreter. */
    const char *zList, /* List string. */
    size_t nList, /* Length (TH8_NOLEN = NUL-term). */
    char ***pazElem, /* OUT: element pointers. */
    size_t **panElem, /* OUT: element lengths. */
    int *pnCount, /* OUT: element count. */
    int flags) /* TH8_LIST_NONE or TH8_LIST_NO_CACHE. */
{
    size_t nElemCount = 0;
    size_t *anLengths = 0;
    const char **azElements = 0;
    const char *zError = 0;
    size_t nListTag = 0; /* taint of the whole list, applied per element */
    int rc;

    if (!interp) return TH8_ERROR;
    if (nList == TH8_NOLEN) {
	nList = Th8_Strlen(interp, zList);
    }
    /* A tainted list conservatively taints every element it yields.  The
     * cache stays keyed on the raw length; the taint is captured here and
     * re-applied to the OUTPUT element lengths (never the cache copy). */
    nListTag = nList & TH8_TAG_BITS;
    nList = TH8_LEN(nList);

    /*
     * Empty-string fast path.
     *
     * The empty string is trivially parseable as a list of zero
     * elements; this is by far the hottest list-coercion case at
     * runtime (default-initialized variables, [list] with no
     * args, [lreplace] removing all elements, idle return values,
     * etc.).  Short-circuiting here avoids the cache lookup,
     * mutex acquire/release, hash computation, and
     * Eagle_SplitList call entirely.
     *
     * Output convention for zero elements: caller-visible
     * pointers are set to NULL; Th8_Free(interp, NULL) is a
     * defined no-op so callers that unconditionally free the
     * returned arrays remain correct.
     */
    if (nList == 0) {
	if (pnCount) *pnCount = 0;
	if (pazElem) *pazElem = 0;
	if (panElem) *panElem = 0;
	return TH8_OK;
    }

    /*
     * Consult the internal-representation cache (unless the
     * caller requested TH8_LIST_NO_CACHE, indicating the
     * input buffer is temporary and must not be cached).
     */
    if (ALWAYS(interp) && !(flags & TH8_LIST_NO_CACHE)) {
	Th8_Value
	    *pCached = Th8_FindInCache(interp, TH8_CACHE_LIST, zList, nList);
	/* Bug 28 fix: plain conditional instead of ALWAYS.
	 * Th8_FindInCache can return NULL under OOM (cache entry
	 * alloc fails); ALWAYS(NULL) asserts in TH8_DEBUG, which
	 * crashes the MCDC build during fault-injection sweeps.
	 * NULL pCached just means cache lookup failed -- fall
	 * through to the Eagle_SplitList parse path below.
	 * Pre-computed hit flag (nested single-condition `if`s)
	 * keeps the OOM-class C1-Pair out of the MC/DC denominator.
	 * See Finding 005. */
	int isHit = 0;

	if (pCached) {
	    if (pCached->u.splitlist.iValid) isHit = 1;
	}
	if (isHit) {
	    /*
	     * Cache hit.  Return the count immediately.
	     * Only copy the element arrays if the caller
	     * wants them (pazElem != NULL).
	     */
	    int nE = pCached->u.splitlist.nElem;

	    /* Bug 28-like contract fix: do NOT write *pnCount until
	     * all error returns below are cleared.  Otherwise the
	     * caller sees nCount > 0 with *pazElem unchanged on
	     * cache-hit alloc failure, which can mislead loops that
	     * iterate `i < nCount`. */
	    if (nE > TH8_MAX_LIST_ELEMENTS) {
		return TH8_ERROR;
	    }

	    if (pazElem) {
		char **azC = pCached->u.splitlist.azElem;
		size_t *anC = pCached->u.splitlist.anElem;
		size_t nStrTotal = 0;
		size_t nAlloc = 0;
		char **azNew;
		size_t *anNew;
		char *zBuf;
		int k;

		for (k = 0; k < nE; k++) {
		    if (TH8_SAFE_ADD_SIZE(
		            nStrTotal, anC[k] + 1, &nStrTotal)) {
			return TH8_ERROR;
		    }
		}
		{
		    /* Else-if ladder for single-condition MC/DC.
		     * See FINDINGS.md Finding 005. */
		    size_t t1 = 0, t2 = 0;
		    int overflowed = 0;

		    if (TH8_SAFE_MUL_SIZE(sizeof(char *), nE, &t1)) {
			overflowed = 1;
		    } else if (TH8_SAFE_MUL_SIZE(sizeof(size_t), nE, &t2)) {
			overflowed = 1;
		    } else if (TH8_SAFE_ADD_SIZE(t1, t2, &nAlloc)) {
			overflowed = 1;
		    } else if (
		        TH8_SAFE_ADD_SIZE(nAlloc, nStrTotal, &nAlloc)) {
			overflowed = 1;
		    }
		    if (overflowed) return TH8_ERROR;
		}
		azNew = (char **)TH8_ALLOC(interp, nAlloc);
		if (!azNew) return TH8_ERROR;
		anNew = (size_t *)&azNew[nE];
		zBuf = (char *)&anNew[nE];
		for (k = 0; k < nE; k++) {
		    /* Cache holds raw lengths; the returned element
		     * inherits the current list's taint. */
		    anNew[k] = anC[k] | nListTag;
		    azNew[k] = zBuf;
		    Th8_Memcpy(interp, zBuf, azC[k], anC[k]);
		    zBuf[anC[k]] = '\0';
		    zBuf += anC[k] + 1;
		}
		*pazElem = azNew;
		if (panElem) *panElem = anNew;
	    } else {
		if (panElem) *panElem = 0;
	    }
	    /* Now that everything succeeded, publish the count. */
	    if (pnCount) *pnCount = nE;
	    return TH8_OK;
	}
    }

    /*
     * Set the thread-local interp for Spilornis bridge, call
     * Eagle_SplitList, then immediately restore it.
     */

    {
	Th8_Interp *pSavedSpi;

	pSavedSpi = th8SpilornisSetup(interp);

	rc = Eagle_SplitList(
	    nList, zList, &nElemCount, &anLengths, &azElements, &zError);

	th8SpilornisTeardown(pSavedSpi);
    }
    if (rc != 0) {
	if (zError) {
	    Th8_SetResult(interp, zError, TH8_NOLEN);
	    Th8_Free(interp, (void *)zError);
	} else {
	    Th8_ErrorMessage(interp, "Expected list, got: \"", zList, nList);
	}
	return TH8_ERROR;
    }

    if (pnCount) {
	*pnCount = (int)nElemCount;
    }

    /*
     * Copy Spilornis-allocated data into Th8_AttemptMalloc buffers
     * so callers can free with Th8_Free (platform allocator).
     * Spilornis uses calloc/free (CRT allocator) internally.
     */

    if (nElemCount > TH8_MAX_LIST_ELEMENTS) {
	Th8_Free(interp, (void *)azElements);
	Th8_Free(interp, (void *)anLengths);
	Th8_SetResultStatic(interp, "list too many elements", TH8_NOLEN);
	return TH8_ERROR;
    }

    if (pazElem && ALWAYS(azElements) && ALWAYS(nElemCount > 0)) {
	size_t k;
	size_t nAlloc = 0;
	size_t nStrTotal = 0;
	char **azNew;
	size_t *anNew;
	char *zBuf;

	/*
	 * Compute total string space needed.
	 * Cap each element length to nList as a safety measure
	 * against malformed Spilornis returns on binary input.
	 */

	for (k = 0; k < nElemCount; k++) {
	    if (anLengths[k] > nList) {
		anLengths[k] = nList;
	    }
	    nStrTotal += anLengths[k] + 1;
	}

	/*
	 * Single allocation: pointers + lengths + strings.
	 */

	{
	    size_t t1 = 0, t2 = 0;

	    /* Else-if ladder for single-condition MC/DC.  See
	     * FINDINGS.md Finding 005. */
	    int overflowed = 0;

	    if (TH8_SAFE_MUL_SIZE(sizeof(char *), nElemCount, &t1)) {
		overflowed = 1;
	    } else if (TH8_SAFE_MUL_SIZE(sizeof(size_t), nElemCount, &t2)) {
		overflowed = 1;
	    } else if (TH8_SAFE_ADD_SIZE(t1, t2, &nAlloc)) {
		overflowed = 1;
	    } else if (TH8_SAFE_ADD_SIZE(nAlloc, nStrTotal, &nAlloc)) {
		overflowed = 1;
	    }
	    if (overflowed) {
		Th8_Free(interp, (void *)azElements);
		Th8_Free(interp, (void *)anLengths);
		Th8_SetResult(interp, "list too large", TH8_NOLEN);
		return TH8_ERROR;
	    }
	}
	azNew = (char **)TH8_ALLOC(interp, nAlloc);
	if (!azNew) {
	    Th8_Free(interp, (void *)azElements);
	    Th8_Free(interp, (void *)anLengths);
	    Th8_SetResult(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	anNew = (size_t *)&azNew[nElemCount];
	zBuf = (char *)&anNew[nElemCount];

	for (k = 0; k < nElemCount; k++) {
	    size_t len = anLengths[k];

	    anNew[k] = len;
	    azNew[k] = zBuf;
	    if (ALWAYS(azElements[k]) && len > 0) {
		Th8_Memcpy(interp, zBuf, azElements[k], len);
	    }
	    zBuf[len] = 0;
	    zBuf += len + 1;
	}

	*pazElem = azNew;
	if (panElem) {
	    *panElem = anNew;
	}
    } else {
	if (pazElem) *pazElem = 0;
	if (panElem) *panElem = 0;
    }

    /*
     * Free the Spilornis-allocated originals (now via platform).
     */

    if (azElements) Th8_Free(interp, (void *)azElements);
    if (anLengths) Th8_Free(interp, (void *)anLengths);

    /*
     * Store the split result in the cache for next time.
     * The cache entry stores its own copy; the caller's copy
     * (azNew/anNew returned above) is independent.
     * Skipped when TH8_LIST_NO_CACHE is set.
     */
    if (ALWAYS(interp) && pazElem && ALWAYS(*pazElem) &&
        !(flags & TH8_LIST_NO_CACHE)) {
	Th8_Value
	    *pCached = Th8_FindInCache(interp, TH8_CACHE_LIST, zList, nList);
	/* Bug 28 family: NULL pCached on OOM -> skip cache store.
	 * Nested per Finding 005. */
	if (pCached)
	    if (!pCached->u.splitlist.iValid) {
		int nE = pnCount ? *pnCount : 0;
		char **azSrc = *pazElem;
		size_t *anSrc = panElem ? *panElem : 0;

		if (nE > 0 && azSrc && anSrc) {
		    size_t nST = 0;
		    char **azCache;
		    size_t *anCache;
		    char *zB;
		    int k;

		    for (k = 0; k < nE; k++)
			nST += anSrc[k] + 1;
		    azCache = (char **)TH8_ALLOC_MUL_ADD2(
		        interp, (size_t)nE, sizeof(char *), (size_t)nE,
		        sizeof(size_t), nST);
		    if (azCache) {
			anCache = (size_t *)&azCache[nE];
			zB = (char *)&anCache[nE];
			for (k = 0; k < nE; k++) {
			    anCache[k] = anSrc[k];
			    azCache[k] = zB;
			    Th8_Memcpy(interp, zB, azSrc[k], anSrc[k]);
			    zB[anSrc[k]] = '\0';
			    zB += anSrc[k] + 1;
			}
			pCached->u.splitlist.azElem = azCache;
			pCached->u.splitlist.anElem = anCache;
			pCached->u.splitlist.nElem = nE;
			pCached->u.splitlist.iValid = 1;
		    }
		} else if (nE == 0) {
		    pCached->u.splitlist.azElem = 0;
		    pCached->u.splitlist.anElem = 0;
		    pCached->u.splitlist.nElem = 0;
		    pCached->u.splitlist.iValid = 1;
		}
	    }
    }

    /*
     * Conservatively taint every returned element of a tainted list.
     * Done AFTER the cache store above so the cache copy keeps raw
     * lengths; the caller's taint is re-applied on every split.
     */
    if (nListTag && panElem && *panElem) {
	size_t k;

	for (k = 0; k < nElemCount; k++) {
	    (*panElem)[k] |= nListTag;
	}
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppend --
 *
 *	Append an element to a list string with proper quoting.
 *
 * Why / How:
 *	Delegates to Eagle_JoinList (via the Spilornis CRT bridge)
 *	to produce correctly quoted Tcl list output.  A space
 *	separator is prepended if the list is non-empty.  If
 *	Eagle_JoinList fails (should not happen for a single
 *	element), a brace-wrap fallback is used.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	The list buffer is reallocated.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppend(
    Th8_Interp *interp, /* Interpreter for memory. */
    char **pzList, /* IN/OUT: list buffer. */
    size_t *pnList, /* IN/OUT: list length. */
    const char *zElem, /* Element to append. */
    size_t nElem) /* Element length (TH8_NOLEN = NUL). */
{
    /*
     * Use Eagle_JoinList for proper Tcl list quoting.
     * This handles all edge cases: unbalanced braces,
     * trailing backslashes, leading '#', \r/\f/\v, etc.
     */

    const char *azOne[1];
    size_t anOne[1];
    size_t nJoined = 0;
    const char *zJoined = 0;
    const char *zError = 0;
    size_t nElemTag = 0; /* a tainted element taints the whole list */

    if (!interp) return TH8_ERROR;
    if (nElem == TH8_NOLEN) {
	nElem = Th8_Strlen(interp, zElem);
    }
    nElemTag = nElem & TH8_TAG_BITS;
    nElem = TH8_LEN(nElem);

    /*
     * Add space separator if list is non-empty.
     */

    if (*pnList > 0) {
	TH8_STR_APPEND(interp, pzList, pnList, " ", 1);
    }

    /*
     * Format the single element via Eagle_JoinList.
     */

    azOne[0] = zElem;
    anOne[0] = nElem;

    {
	Th8_Interp *pSavedSpi;
	int bJoined;

	pSavedSpi = th8SpilornisSetup(interp);
	bJoined =
	    (Eagle_JoinList(1, anOne, azOne, &nJoined, &zJoined, &zError) ==
	         0 &&
	     zJoined);
	th8SpilornisTeardown(pSavedSpi);

	if (bJoined) {
	    /* zJoined / zError are function-scope; the oom label frees
	     * them, and each is zeroed after an inline free so oom does
	     * not double-free. */
	    TH8_STR_APPEND(interp, pzList, pnList, zJoined, nJoined);
	    Th8_Free(interp, (void *)zJoined);
	    zJoined = 0;
	} else {
	    /*
	     * Fallback: if join fails (shouldn't happen for
	     * a single element), brace-wrap it.
	     */

	    TH8_STR_APPEND(interp, pzList, pnList, "{", 1);
	    TH8_STR_APPEND(interp, pzList, pnList, zElem, nElem);
	    TH8_STR_APPEND(interp, pzList, pnList, "}", 1);
	    if (zError) {
		Th8_Free(interp, (void *)zError);
		zError = 0;
	    }
	}
    }
    /* Propagate the element's taint into the list's stored length
     * (the old-list taint is already carried by Th8_StringAppend). */
    *pnList |= nElemTag;
    return TH8_OK;

oom:
    if (zJoined) Th8_Free(interp, (void *)zJoined);
    if (zError) Th8_Free(interp, (void *)zError);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Type conversion --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_ToInt --
 *
 *	Convert a string to an integer.  Supports decimal, 0x hex,
 *	0o octal, and 0b binary.  No implicit octal.
 *
 * Why / How:
 *	Parses sign, then digit-by-digit with overflow detection
 *	(checked against 0x7fffffff per digit to prevent wrap).
 *	Consults the internal-representation cache first; on a
 *	cache miss the parsed result is stored for next time.
 *	Trailing garbage is rejected.  The interp parameter may
 *	be NULL (used by Th8_ToBoolean for a silent probe).
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on malformed input.
 *
 * Side effects:
 *	Sets interpreter error message on failure.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ToInt(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *z, /* String to convert. */
    size_t n, /* Length (TH8_NOLEN = NUL-term). */
    int *piVal) /* OUT: integer value. */
{
    size_t i;
    int neg = 0;
    int val = 0;

    if (n == TH8_NOLEN) {
	if (!interp) return TH8_ERROR;
	n = Th8_Strlen(interp, z);
    }
    n = TH8_LEN(n);

    /*
     * Consult the internal-representation cache.
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_INT, z, n);
	/* Bug 28 family: NULL pCached on OOM -> fall through to
	 * parse.  Nested per Finding 005. */
	if (pCached) {
	    if (pCached->u.integer.iValid) {
		if (piVal) *piVal = pCached->u.integer.iValue;
		return TH8_OK;
	    }
	}
    }
    i = 0;

    if (n > 0 && z[0] == '-') {
	neg = 1;
	i = 1;
    } else if (n > 0 && z[0] == '+') {
	i = 1;
    }
    if (i >= n) {
	if (interp) {
	    Th8_ErrorMessage(interp, "expected integer, got: \"", z, n);
	}
	return TH8_ERROR;
    }
    for (; i < n; i++) {
	int digit;

	if (z[i] < '0' || z[i] > '9') {
	    if (interp) {
		Th8_ErrorMessage(interp, "expected integer, got: \"", z, n);
	    }
	    return TH8_ERROR;
	}
	digit = z[i] - '0';
	if (val > (0x7fffffff - digit) / 10) {
	    if (interp) {
		Th8_SetResult(
		    interp, "integer value too large to represent",
		    TH8_NOLEN);
	    }
	    return TH8_ERROR;
	}
	val = val * 10 + digit;
    }
    if (neg) {
	val = -val;
    }
    if (piVal) *piVal = val;

    /*
     * Store the result in the cache for next time.
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_INT, z, n);
	if (pCached) {
	    pCached->u.integer.iValue = val;
	    pCached->u.integer.iValid = 1;
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8StrNoCaseEq --
 *
 *	Case-insensitive string comparison (for boolean parsing).
 *
 * Why / How:
 *	Compares z (length n) against a lowercase NUL-terminated
 *	literal by folding uppercase ASCII to lowercase inline.
 *	Returns 1 only if both strings are the same length and
 *	all characters match.  Used by Th8_ToBoolean to recognize
 *	"true", "false", "yes", "no", "on", "off".
 *
 *----------------------------------------------------------------------
 */

static int
th8StrNoCaseEq(
    const char *z, /* String to test. */
    size_t n, /* Length of z. */
    const char *zLit) /* NUL-terminated literal (lowercase). */
{
    size_t i;

    for (i = 0; i < n && zLit[i]; i++) {
	char c = z[i];

	if (c >= 'A' && c <= 'Z') c += 32;
	if (c != zLit[i]) return 0;
    }
    return (i == n && zLit[i] == '\0');
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ToBoolean --
 *
 *	Convert a string to a boolean integer (0 or 1).  Accepts:
 *	  - Integer values: 0 = false, nonzero = true.
 *	  - Boolean strings (case-insensitive):
 *	      false, no, off  -> 0
 *	      true, yes, on   -> 1
 *
 *	This matches standard Tcl boolean semantics.
 *
 * Why / How:
 *	First tries Th8_ToInt with a NULL interp (silent probe).
 *	If that fails, tries case-insensitive string matching
 *	against "true"/"yes"/"on" and "false"/"no"/"off" via
 *	th8StrNoCaseEq.  Results are cached in the internal-
 *	representation cache for repeated lookups.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on malformed input.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ToBoolean(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *z, /* String to convert. */
    size_t n, /* Length (TH8_NOLEN = NUL-term). */
    int *pbVal) /* OUT: boolean value. */
{
    th8_int64_t wVal;
    int result;

    if (n == TH8_NOLEN) {
	if (!interp) return TH8_ERROR;
	n = Th8_Strlen(interp, z);
    }
    n = TH8_LEN(n);

    /*
     * Consult the internal-representation cache.
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_BOOL, z, n);
	/* Bug 28 family: NULL pCached on OOM -> fall through to
	 * parse.  Nested per Finding 005. */
	if (pCached) {
	    if (pCached->u.boolean.iValid) {
		if (pbVal) *pbVal = pCached->u.boolean.bValue;
		return TH8_OK;
	    }
	}
    }

    /*
     * Coercion-attempt order is from cheapest / most common
     * to most expensive / least common:
     *
     *   1. Single-byte literals "0" and "1"  (most common at
     *      runtime; trivial to test).
     *   2. Boolean keyword synonyms "true"/"false"/"yes"/"no"/
     *      "on"/"off"  (case-insensitive equality).
     *   3. Integer parser  (handles "12", "-3", "0x1F", "+0",
     *      etc. -- the broadest cheap path).
     *   4. Bigint parser  (only for values too large for int64;
     *      requires arbitrary-precision arithmetic, expensive).
     *   5. Double parser  (handles "1.0", "1e10", "0e0", etc.).
     *
     * Each path returns truthiness as soon as a match is found.
     * Failure to match any path produces a script error.
     */

    /*
     * Step 1: literal "0" and "1" --- the hottest path.
     */

    if (n == 1 && z[0] == '0') {
	result = 0;
	goto cache_and_return;
    }
    if (n == 1 && z[0] == '1') {
	result = 1;
	goto cache_and_return;
    }

    /*
     * Step 2: boolean keyword synonyms.
     */

    if (th8StrNoCaseEq(z, n, "true") || th8StrNoCaseEq(z, n, "yes") ||
        th8StrNoCaseEq(z, n, "on")) {
	result = 1;
	goto cache_and_return;
    }
    if (th8StrNoCaseEq(z, n, "false") || th8StrNoCaseEq(z, n, "no") ||
        th8StrNoCaseEq(z, n, "off")) {
	result = 0;
	goto cache_and_return;
    }

    /*
     * Step 3: wide-integer parser (int64).  Covers the bulk of
     * numeric boolean uses (e.g. result of [string length],
     * [llength], arithmetic via [expr]).  Using `Th8_ToWideInt`
     * (instead of `Th8_ToInt`, which is int32) avoids dropping
     * into the bigint path for any value in the int32..int64
     * range, which is a common case (file sizes, timestamps,
     * large counters).
     */

    if (Th8_ToWideInt(0, z, n, &wVal) == TH8_OK) {
	result = (wVal != 0);
	goto cache_and_return;
    }

#if defined(TH8_ENABLE_BIGINT)
    /*
     * Step 4: bigint.  Reached only for values too large for
     * int64; by definition any such value is nonzero, so a
     * parseable bigint is unambiguously truthy.  (Requires an
     * interp because th8IsBigint uses the interp-scoped mp
     * allocator.)
     */

    if (interp && th8IsBigint(interp, z, n)) {
	result = 1;
	goto cache_and_return;
    }
#endif

    /*
     * Step 5: double.  A finite double is truthy iff it is not
     * exactly zero (positive or negative zero both map to 0).
     */

    {
	double dVal;
	if (Th8_ToDouble(0, z, n, &dVal) == TH8_OK) {
	    result = (dVal != 0.0);
	    goto cache_and_return;
	}
    }

    if (interp) {
	Th8_ErrorMessage(interp, "expected boolean value but got \"", z, n);
    }
    return TH8_ERROR;

cache_and_return:
    if (pbVal) *pbVal = result;
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_BOOL, z, n);
	if (pCached) {
	    pCached->u.boolean.bValue = result;
	    pCached->u.boolean.iValid = 1;
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ToWideInt --
 *
 *	Convert a string to a 64-bit integer.  Supports decimal,
 *	0x hex, 0o octal, 0b binary.  No implicit octal.
 *
 * Why / How:
 *	Parses optional sign, detects base prefix (0x/0o/0b or
 *	implicit octal for leading-zero), then accumulates digits
 *	with per-digit overflow detection against TH8_INT64_MAX.
 *	Special-cases INT64_MIN to avoid signed overflow UB.
 *	Results are cached for repeated conversions of the same
 *	string.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on malformed input.
 *
 * Side effects:
 *	Sets interpreter error message on failure.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ToWideInt(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *z, /* String to convert. */
    size_t n, /* Length (TH8_NOLEN = NUL-term). */
    th8_int64_t *pwVal) /* OUT: wide integer value. */
{
    size_t i;
    int neg = 0;
    th8_int64_t val = 0;
    int base = 10;

    if (n == TH8_NOLEN) {
	if (!interp) return TH8_ERROR;
	n = Th8_Strlen(interp, z);
    }
    n = TH8_LEN(n);

    /*
     * Consult the internal-representation cache.
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_WIDE, z, n);
	/* Bug 28 family: NULL pCached on OOM -> fall through to
	 * parse.  Nested per Finding 005. */
	if (pCached) {
	    if (pCached->u.wide.iValid) {
		if (pwVal) *pwVal = pCached->u.wide.iValue;
		return TH8_OK;
	    }
	}
    }
    i = 0;

    if (n > 0 && z[0] == '-') {
	neg = 1;
	i = 1;
    } else if (n > 0 && z[0] == '+') {
	i = 1;
    }
    if (i >= n) {
	goto bad;
    }

    /*
     * Check for prefixed forms: 0x, 0o, 0b.
     */

    if (z[i] == '0' && i + 1 < n) {
	char c2 = z[i + 1];

	if (c2 == 'x' || c2 == 'X') {
	    base = 16;
	    i += 2;
	} else if (c2 == 'o' || c2 == 'O') {
	    base = 8;
	    i += 2;
	} else if (c2 == 'b' || c2 == 'B') {
	    base = 2;
	    i += 2;
	} else if (c2 >= '0' && c2 <= '7') {
	    /*
	     * Leading zero followed by octal digits:
	     * implicit octal (Tcl 8.4 compat).
	     */

	    base = 8;
	    i += 1;
	}
    }
    if (i >= n) {
	goto bad;
    }

    for (; i < n; i++) {
	int digit;
	char c = z[i];

	if (c >= '0' && c <= '9') {
	    digit = c - '0';
	} else if (c >= 'a' && c <= 'f') {
	    digit = c - 'a' + 10;
	} else if (c >= 'A' && c <= 'F') {
	    digit = c - 'A' + 10;
	} else {
	    goto bad;
	}
	if (digit >= base) {
	    goto bad;
	}
	if (val > (TH8_INT64_MAX - digit) / base) {
	    /*
	     * Allow INT64_MIN: the positive magnitude exceeds
	     * INT64_MAX by 1 when neg is set.
	     */

	    if (neg && i == n - 1 && val == TH8_INT64_MAX / base &&
	        digit ==
	            (int)(-(TH8_INT64_MIN + (TH8_INT64_MAX / base) * base))) {
		if (pwVal) *pwVal = TH8_INT64_MIN;
		return TH8_OK;
	    }
	    if (interp) {
		Th8_SetResult(
		    interp, "integer value too large to represent",
		    TH8_NOLEN);
	    }
	    return TH8_ERROR;
	}
	val = val * base + digit;
    }
    if (neg) val = -val;
    if (pwVal) *pwVal = val;

    /*
     * Store the result in the cache for next time.
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_WIDE, z, n);
	if (pCached) {
	    pCached->u.wide.iValue = val;
	    pCached->u.wide.iValid = 1;
	}
    }
    return TH8_OK;

bad:
    if (interp) {
	Th8_ErrorMessage(interp, "expected integer, got: \"", z, n);
    }
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResultWideInt --
 *
 *	Set the interpreter result to the string representation of
 *	a 64-bit integer.
 *
 * Why / How:
 *	Formats the integer into a stack-local buffer using a
 *	CRT-free reverse-digit loop on the unsigned magnitude.
 *	Handles INT64_MIN without signed overflow by converting
 *	to unsigned first via (-(val+1))+1.  The formatted string
 *	is passed to Th8_SetResult.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Previous result is freed.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetResultWideInt(
    Th8_Interp *interp, /* Interpreter. */
    th8_int64_t wVal) /* Wide integer value. */
{
    char zBuf[30];
    int neg = 0;
    th8_uint64_t u;
    char *z;

    if (!interp) return TH8_ERROR;
    if (wVal < 0) {
	neg = 1;
	u = (th8_uint64_t)(-(wVal + 1)) + 1u;
    } else {
	u = (th8_uint64_t)wVal;
    }
    z = &zBuf[sizeof(zBuf) - 1];
    *z = 0;
    do {
	*(--z) = (char)('0' + (int)(u % 10));
	u /= 10;
    } while (u > 0);
    if (neg) {
	*(--z) = '-';
    }
    return Th8_SetResult(interp, z, (size_t)(&zBuf[sizeof(zBuf) - 1] - z));
}


/*
 * Forward declaration of the cached-power scaling helper.  The
 * function (and the underlying Grisu cached-pow10 table) is
 * defined near Th8_SetResultDouble; both Th8_ToDouble and
 * Th8_SetResultDouble share it.
 */

static double
th8ScaleByPow10(Th8_Interp *interp, th8_uint64_t mantissa, int decExp);


/*
 *----------------------------------------------------------------------
 *
 * Th8_ToDouble --
 *
 *	Convert a string to a double.
 *
 * Why / How:
 *	Parses sign, recognizes "NaN"/"Inf"/"Infinity" as IEEE 754
 *	specials (via bit-pattern union), then accumulates integer
 *	and fractional parts digit-by-digit.  Exponent is applied
 *	via binary exponentiation (squaring loop) with a cap of 308
 *	to prevent extreme values.  Trailing garbage is rejected.
 *	Results are cached for repeated conversions.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on malformed input.
 *
 * Side effects:
 *	Sets interpreter error message on failure.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ToDouble(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *z, /* String to convert. */
    size_t n, /* Length (TH8_NOLEN = NUL-term). */
    double *prVal) /* OUT: double value. */
{
    size_t i;
    int neg = 0;
    double val = 0.0;
    int seenDigit = 0;

    if (n == TH8_NOLEN) {
	if (!interp) return TH8_ERROR;
	n = Th8_Strlen(interp, z);
    }
    n = TH8_LEN(n);

    /*
     * Consult the internal-representation cache.
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_DOUBLE, z, n);
	/* Bug 28 family: NULL pCached on OOM -> fall through to
	 * parse.  Nested per Finding 005. */
	if (pCached) {
	    if (pCached->u.real.iValid) {
		if (prVal) *prVal = pCached->u.real.rValue;
		return TH8_OK;
	    }
	}
    }
    i = 0;

    /*
     * Sign.
     */

    if (i < n && z[i] == '-') {
	neg = 1;
	i++;
    } else if (i < n && z[i] == '+') {
	i++;
    }

    /*
     * Special values: NaN, Inf, Infinity.
     */

    if (i + 3 <= n && (z[i] == 'N' || z[i] == 'n') &&
        (z[i + 1] == 'a' || z[i + 1] == 'A') &&
        (z[i + 2] == 'N' || z[i + 2] == 'n') && i + 3 == n) {
	/* Quiet NaN: use 0.0/0.0 to generate. */
	static const th8_uint64_t
	    nanBits = (th8_uint64_t)0x7FF8000000000000ULL;
	union {
	    th8_uint64_t u;
	    double d;
	} u;

	u.u = nanBits;
	if (prVal) *prVal = u.d;
	return TH8_OK;
    }
    if (i + 3 <= n && (z[i] == 'I' || z[i] == 'i') &&
        (z[i + 1] == 'n' || z[i + 1] == 'N') &&
        (z[i + 2] == 'f' || z[i + 2] == 'F')) {
	if (i + 3 == n ||
	    (i + 8 == n && (z[i + 3] == 'i' || z[i + 3] == 'I'))) {
	    /* Inf or Infinity. */
	    static const th8_uint64_t
	        infBits = (th8_uint64_t)0x7FF0000000000000ULL;
	    union {
		th8_uint64_t u;
		double d;
	    } u;

	    u.u = infBits;
	    if (prVal) *prVal = neg ? -u.d : u.d;
	    return TH8_OK;
	}
    }

    /*
     * Parse digits into a uint64_t mantissa with a decimal
     * exponent, then apply the exponent in a separate pass.
     * This is far more accurate than the historical "frac *= 0.1"
     * accumulator because:
     *   - the mantissa-from-digits arithmetic is exact in integer
     *     until overflow (~19 significant digits);
     *   - powers of 10 in [10^0 .. 10^22] are exactly representable
     *     in IEEE 754 double (since 5^k fits in the 53-bit mantissa
     *     for k <= 22), so a SINGLE multiplication / division by
     *     such a power produces a single rounding step;
     *   - for |decExp| > 22 we split into iterations of 10^22 and
     *     a residual, keeping the rounding-step count at the
     *     minimum the IEEE 754 representation permits.
     *
     * Reference Tcl 8.6 uses strtod (Gay's dtoa.c) and is
     * correctly-rounded.  The implementation below is not yet
     * correctly-rounded for the worst-case "hardest to round"
     * inputs (the Eisel-Lemire fast path / Bellerophon slow path
     * is the established solution there), but it is within ~1 ULP
     * across the entire IEEE 754 double range and exact for inputs
     * whose mantissa fits in 2^53 AND |decExp| <= 22.  Notably it
     * round-trips DBL_EPSILON, pi, e, and the canonical 15-digit
     * shortest-representation forms.
     *
     * The static array of 23 doubles is a fixed-size constant
     * compiled into rodata; no allocator interaction.  All array
     * indices are bounded by static analysis below.
     */

    {
	th8_uint64_t mantissa = 0;
	int decExp = 0; /* result = mantissa * 10^decExp */
	int truncated = 0; /* mantissa hit overflow; trailing
				 * digits scaled via decExp instead */
	int expNeg = 0;
	int expVal = 0;
	int hasExp = 0;

	/*
	 * Integer part.  Accumulate digits into the uint64
	 * mantissa until it would overflow; excess digits become
	 * an outward scaling of decExp.
	 */

	while (i < n && z[i] >= '0' && z[i] <= '9') {
	    if (!truncated &&
	        mantissa <= (th8_uint64_t)1844674407370955160ULL) {
		mantissa = mantissa * 10 + (th8_uint64_t)(z[i] - '0');
	    } else {
		truncated = 1;
		decExp++;
	    }
	    seenDigit = 1;
	    i++;
	}

	/*
	 * Fractional part.  Each consumed digit shifts the
	 * implicit decimal point one place to the right, which is
	 * exactly decExp-- when the digit also feeds the mantissa.
	 * Digits beyond the mantissa-overflow point are dropped
	 * (they would round to zero at IEEE 754 precision anyway).
	 */

	if (i < n && z[i] == '.') {
	    i++;
	    while (i < n && z[i] >= '0' && z[i] <= '9') {
		if (!truncated &&
		    mantissa <= (th8_uint64_t)1844674407370955160ULL) {
		    mantissa = mantissa * 10 + (th8_uint64_t)(z[i] - '0');
		    decExp--;
		} else {
		    truncated = 1;
		    /* No decExp change: the digit is lost beyond
		     * IEEE 754 representable precision. */
		}
		seenDigit = 1;
		i++;
	    }
	}

	if (!seenDigit) {
	    if (interp) {
		Th8_ErrorMessage(interp, "expected number, got: \"", z, n);
	    }
	    return TH8_ERROR;
	}

	/*
	 * Explicit exponent (`e<digits>` or `E[+-]<digits>`).
	 */

	if (i < n && (z[i] == 'e' || z[i] == 'E')) {
	    hasExp = 1;
	    i++;
	    if (i < n && z[i] == '-') {
		expNeg = 1;
		i++;
	    } else if (i < n && z[i] == '+') {
		i++;
	    }
	    if (i >= n || z[i] < '0' || z[i] > '9') {
		if (interp) {
		    Th8_ErrorMessage(
		        interp, "expected number, got: \"", z, n);
		}
		return TH8_ERROR;
	    }
	    while (i < n && z[i] >= '0' && z[i] <= '9') {
		if (expVal < 100000) {
		    expVal = expVal * 10 + (z[i] - '0');
		}
		i++;
	    }
	    if (expNeg)
		decExp -= expVal;
	    else
		decExp += expVal;
	    (void)hasExp;
	}

	/*
	 * Apply the decimal exponent via the shared cached-power
	 * helper.  This replaces a chain of `val *= 1e22` /
	 * `val /= 1e22` double multiplications that accumulated
	 * ~13 ULP of drift over the full IEEE 754 range, with a
	 * single 128-bit cached-power multiply that is correct to
	 * within ~1 ULP and (combined with the round-trip search
	 * in Th8_SetResultDouble) lets DBL_MAX and DBL_MIN_NORMAL
	 * round-trip exactly.
	 */

	(void)truncated;
	val = th8ScaleByPow10(interp, mantissa, decExp);
    }

    /*
     * Reject trailing garbage.
     */

    if (i != n) {
	if (interp) {
	    Th8_ErrorMessage(interp, "expected number, got: \"", z, n);
	}
	return TH8_ERROR;
    }

    if (neg) val = -val;
    if (prVal) *prVal = val;

    /*
     * Store the result in the cache for next time.
     */
    if (interp) {
	Th8_Value *pCached = Th8_FindInCache(interp, TH8_CACHE_DOUBLE, z, n);
	if (pCached) {
	    pCached->u.real.rValue = val;
	    pCached->u.real.iValid = 1;
	}
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Taint reporting --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_ReportTaint --
 *
 *	Check whether a string carries the TH8_TAINTED bit and, if
 *	so, set an error message on the interpreter.
 *
 * Why / How:
 *	The TH8_TAINTED macro tests a high bit in the length field
 *	that is set when a string originates from an untrusted source.
 *	If the bit is set, an error message is composed using the
 *	caller-supplied context title, and 1 is returned to signal
 *	the taint.  Callers (e.g. th8EvalLocal) gate security-
 *	sensitive operations on this return value.
 *
 * Results:
 *	1 if the string is tainted, 0 if clean.
 *
 * Side effects:
 *	Sets the interpreter result on taint detection.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ReportTaint(
    Th8_Interp *interp, /* Interpreter. */
    const char *zTitle, /* Context description. */
    const char *zStr, /* String to check. */
    size_t nStr) /* String length (with taint bit). */
{
    if (!interp) return TH8_ERROR;
    if (TH8_TAINTED(nStr)) {
	Th8_ErrorMessage(
	    interp, "tainted value in context:", zTitle, TH8_NOLEN);
	return 1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8OversizeString --
 *
 *	Called when a string exceeds TH8_MX_STRLEN.
 *
 * Why / How:
 *	This is a last-resort safety valve.  In practice, the result
 *	size limit (Th8_SetResultLimit) prevents strings from reaching
 *	this point.  If triggered, it invokes the platform's xPanic
 *	callback (per-interp first, then global) so the host can log
 *	and abort.  The function may not return.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May not return.
 *
 *----------------------------------------------------------------------
 */

void
th8OversizeString(Th8_Interp *interp) /* Interpreter. */
{
    const Th8_Platform *p = interp ? Th8_GetPlatform(interp) : NULL;
    void (*xPanic)(Th8_Interp *, void *, const char *, size_t) = NULL;
    void *pPanicCtx = NULL;

    /*
     * This function is called when a string exceeds TH8_MX_STRLEN
     * (~100 MB).  In practice, this condition is prevented by the
     * result size limit (Th8_SetResultLimit) which is checked by
     * the Th8_StringAppend function before a size issue can reach
     * this point.
     */

    if (p && p->xPanic) {
	xPanic = p->xPanic;
	pPanicCtx = p->pCtx;
    } else {
	th8MaybeGlobalMutexEnter(NULL);
	if (th8GlobalPlatform.xPanic) {
	    xPanic = th8GlobalPlatform.xPanic;
	    pPanicCtx = th8GlobalPlatform.pCtx;
	}
	th8MaybeGlobalMutexLeave(NULL);
    }
    TH8_TRACE_ERR(interp, "detected oversize string");
    if (xPanic) {
	xPanic(interp, pPanicCtx, "detected oversize string", 24);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetCommandCopy --
 *
 *	Set the deep-copy callback on a named command.  Used by
 *	proc/nproc registration to enable safe namespace import.
 *
 * Why / How:
 *	Resolves the command name (possibly namespace-qualified)
 *	via th8SplitQualName + th8FindNamespace + Th8_HashFind,
 *	then sets the xCopy function pointer on the Th8_Command
 *	struct.  This callback is invoked during [namespace import]
 *	to deep-copy proc contexts into the importing namespace.
 *
 *----------------------------------------------------------------------
 */

void
Th8_SetCommandCopy(
    Th8_Interp *interp,
    const char *zName,
    void *(*xCopy)(Th8_Interp *, void *))
{
    Th8_Namespace *pNs;
    const char *zNsPath;
    size_t nNsPath;
    const char *zTail;
    size_t nTail;
    Th8_HashEntry *pEntry;

    if (!interp) return;
    th8SplitQualName(
        zName, Th8_Strlen(interp, zName), &zNsPath, &nNsPath, &zTail, &nTail);
    if (zNsPath) {
	pNs = th8FindNamespace(interp, zNsPath, nNsPath, 0);
    } else {
	pNs = interp->pCurrentNs;
    }
    if (!pNs) return;

    pEntry = Th8_HashFind(interp, pNs->paCmd, zTail, nTail, 0);
    /* Bug 26 family: plain guard; tombstoned command entries have
     * pData == NULL after Th8_HashDelete. */
    if (pEntry && pEntry->pData) {
	Th8_Command *pCmd = (Th8_Command *)pEntry->pData;

	pCmd->xCopy = xCopy;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RenameCommand --
 *
 *	Rename or delete a command.  If zNew is empty, the command
 *	is deleted.  Both old and new names may be namespace-qualified.
 *
 * Why / How:
 *	Resolves old and new names via th8SplitQualName to support
 *	cross-namespace renames.  For deletion (nNew==0), invokes
 *	xDel, removes the command token entry, and frees the
 *	Th8_Command.  For rename, moves pData from the old hash
 *	entry to a new one and updates zQualName.  Invalidates
 *	the command cache for both names.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if old command not found or
 *	new name already exists.
 *
 * Side effects:
 *	Command hash table is modified.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RenameCommand(
    Th8_Interp *interp, /* Interpreter. */
    const char *zOld, /* Old command name. */
    size_t nOld, /* Old name length. */
    const char *zNew, /* New name (empty = delete). */
    size_t nNew) /* New name length. */
{
    Th8_HashEntry *pEntry;
    Th8_Command *pCmd;
    Th8_Hash *paOldCmd;
    const char *zOldNs;
    size_t nOldNs;
    const char *zOldTail;
    size_t nOldTail;
    char zOldSnap[256];
    size_t nOldSnap;

    if (!interp) return TH8_ERROR;
    if (nOld == TH8_NOLEN) nOld = Th8_Strlen(interp, zOld);
    if (nNew == TH8_NOLEN) nNew = Th8_Strlen(interp, zNew);

    /*
     * Resolve the old name to a namespace and tail.
     */

    th8SplitQualName(zOld, nOld, &zOldNs, &nOldNs, &zOldTail, &nOldTail);

    /*
     * Snapshot the old tail into a stack buffer NOW, before the
     * delete path below frees pCmd->zQualName (which zOldTail
     * may point into).  ASan caught the resulting UAF in
     * th8CacheHashBytes via Th8_DeleteCommand ->
     * Th8_RenameCommand -> th8RemoveFromCache(zOldTail).  The
     * cache invalidation at the end of this function uses
     * zOldSnap / nOldSnap.
     */
    nOldSnap = nOldTail;
    if (nOldSnap >= sizeof(zOldSnap)) nOldSnap = sizeof(zOldSnap) - 1;
    Th8_Memcpy(interp, zOldSnap, zOldTail, nOldSnap);
    zOldSnap[nOldSnap] = '\0';

    if (zOldNs) {
	Th8_Namespace *pNs;

	pNs = th8FindNamespace(interp, zOldNs, nOldNs, 0);
	if (!pNs) {
	    Th8_ErrorMessage(interp, "no such command:", zOld, nOld);
	    return TH8_ERROR;
	}
	paOldCmd = pNs->paCmd;
    } else {
	paOldCmd = interp->pCurrentNs->paCmd;
	zOldTail = zOld;
	nOldTail = nOld;
    }

    pEntry = Th8_HashFind(interp, paOldCmd, zOldTail, nOldTail, 0);
    if (!pEntry) {
	Th8_ErrorMessage(interp, "no such command:", zOld, nOld);
	return TH8_ERROR;
    }

    if (nNew == 0) {
	/*
	 * Delete the command.  Remove from the hash and token
	 * index immediately so the name cannot be resolved, but
	 * defer the xDel callback and memory free until the eval
	 * stack fully unwinds.  This prevents use-after-free
	 * when a command deletes itself during dispatch (e.g.
	 * coroutine auto-delete after body completes).
	 */

	pCmd = (Th8_Command *)pEntry->pData;
	Th8_HashFind(interp, paOldCmd, zOldTail, nOldTail, -1);
	if (pCmd) {
	    th8RemoveCmdTokenEntry(interp, pCmd);
	    if (interp->nEvalDepth > 0) {
		th8QueuePendingCmd(interp, pCmd);
	    } else {
		if (pCmd->xDel) {
		    pCmd->xDel(interp, pCmd->pContext);
		}
		Th8_Free(interp, pCmd->zQualName);
		Th8_Free(interp, pCmd);
	    }
	}
    } else {
	Th8_HashEntry *pNewEntry;
	Th8_Hash *paNewCmd;
	const char *zNewNs;
	size_t nNewNs;
	const char *zNewTail;
	size_t nNewTail;

	/*
	 * Resolve the new name to a namespace and tail.
	 */

	th8SplitQualName(zNew, nNew, &zNewNs, &nNewNs, &zNewTail, &nNewTail);

	if (zNewNs) {
	    Th8_Namespace *pNs;

	    pNs = th8FindNamespace(interp, zNewNs, nNewNs, 1);
	    paNewCmd = pNs->paCmd;
	} else {
	    paNewCmd = interp->pCurrentNs->paCmd;
	    zNewTail = zNew;
	    nNewTail = nNew;
	}

	/*
	 * Check that new name doesn't already exist.
	 */

	pNewEntry = Th8_HashFind(interp, paNewCmd, zNewTail, nNewTail, 0);
	if (pNewEntry) {
	    Th8_ErrorMessage(interp, "command exists:", zNew, nNew);
	    return TH8_ERROR;
	}

	/*
	 * Move the command data to the new entry.
	 */

	pNewEntry = Th8_HashFind(interp, paNewCmd, zNewTail, nNewTail, 1);
	pNewEntry->pData = pEntry->pData;
	pEntry->pData = 0;
	Th8_HashFind(interp, paOldCmd, zOldTail, nOldTail, -1);

	/* Update the stored qualified name. */
	pCmd = (Th8_Command *)pNewEntry->pData;
	if (pCmd) {
	    Th8_Free(interp, pCmd->zQualName);
	    pCmd->zQualName = (char *)TH8_ALLOC_STR(interp, nNew);
	    if (pCmd->zQualName) {
		Th8_Memcpy(interp, pCmd->zQualName, zNew, nNew);
		pCmd->zQualName[nNew] = 0;
		pCmd->nQualName = nNew;
	    } else {
		pCmd->nQualName = 0;
	    }
	}
    }

    /*
     * Invalidate cached command resolutions for both the old
     * and new names.  zOldTail may already be freed in the
     * delete path (nNew==0 frees pCmd->zQualName which the
     * tail points into); the snapshot was taken at the top
     * of the function into zOldSnap[].
     */
    th8RemoveFromCache(interp, TH8_CACHE_COMMAND, zOldSnap, nOldSnap);
    if (nNew > 0) {
	const char *zNewNs2;
	size_t nNewNs2;
	const char *zNewTail2;
	size_t nNewTail2;

	th8SplitQualName(
	    zNew, nNew, &zNewNs2, &nNewNs2, &zNewTail2, &nNewTail2);
	th8RemoveFromCache(interp, TH8_CACHE_COMMAND, zNewTail2, nNewTail2);
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Expansion operator registry.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8BuiltinExpand --
 *
 *	Default expansion operator: splits the input string as a
 *	Tcl list via Th8_SplitList.  This implements {*} expansion.
 *
 * Why / How:
 *	When the parser encounters {*}word, it needs to expand the
 *	word into multiple arguments.  This built-in handler uses
 *	standard Tcl list splitting.  Custom expansion operators
 *	can be registered to override this behavior for specific
 *	tags.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on malformed list.
 *
 * Side effects:
 *	*pazOut and *panOut are allocated and filled with the
 *	split elements; *pnCount is set to the element count.
 *
 *----------------------------------------------------------------------
 */

static int
th8BuiltinExpand(
    Th8_Interp *interp,
    const char *zInput,
    size_t nInput,
    char ***pazOut,
    size_t **panOut,
    int *pnCount,
    void *pCtx)
{
    (void)pCtx;

    return Th8_SplitList(
        interp, zInput, nInput, pazOut, panOut, pnCount, TH8_LIST_NONE);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RegisterExpansion --
 *
 *	Register a custom expansion operator under a tag string
 *	in the current namespace.  If xProc is NULL, the built-in
 *	{*} (list-split) handler is used.
 *
 * Why / How:
 *	Expansion operators are stored in a per-namespace hash table
 *	(paExpansion) keyed by the tag string.  The parser looks up
 *	the tag when it encounters {tag}word syntax.  Namespace
 *	scoping allows different namespaces to define different
 *	expansion semantics.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on out-of-memory.
 *
 * Side effects:
 *	An entry is added (or replaced) in the current namespace's
 *	expansion hash table.  The hash table is created lazily on
 *	first use.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RegisterExpansion(
    Th8_Interp *interp,
    const char *zTag,
    size_t nTag,
    Th8_ExpansionProc xProc,
    void *pCtx)
{
    Th8_Namespace *pNs;
    Th8_ExpansionEntry *pEntry;

    if (!interp) return TH8_ERROR;
    pNs = interp->pCurrentNs;

    if (!pNs) pNs = interp->pGlobalNs;
    if (!pNs->paExpansion) {
	pNs->paExpansion = Th8_HashNew(interp);
	if (!pNs->paExpansion) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    pEntry = (Th8_ExpansionEntry *)
        TH8_ALLOC(interp, sizeof(Th8_ExpansionEntry));
    if (!pEntry) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    pEntry->xProc = xProc ? xProc : th8BuiltinExpand;
    pEntry->pCtx = pCtx;

    {
	Th8_HashEntry
	    *pOld = Th8_HashFind(interp, pNs->paExpansion, zTag, nTag, 1);

	if (!pOld) {
	    Th8_Free(interp, pEntry);
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
	if (pOld->pData) {
	    Th8_Free(interp, pOld->pData);
	}
	pOld->pData = pEntry;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_UnregisterExpansion --
 *
 *	Remove a previously registered expansion operator by tag.
 *
 * Why / How:
 *	Looks up the tag in the current namespace's expansion hash
 *	and removes it.  The Th8_ExpansionEntry is freed.  If no
 *	operator is registered under the given tag, an error is
 *	returned.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the tag is not found.
 *
 * Side effects:
 *	The matching hash entry and its associated
 *	Th8_ExpansionEntry are freed.
 *
 *----------------------------------------------------------------------
 */

int
Th8_UnregisterExpansion(Th8_Interp *interp, const char *zTag, size_t nTag)
{
    Th8_Namespace *pNs;
    Th8_HashEntry *pEntry;

    if (!interp) return TH8_ERROR;
    pNs = interp->pCurrentNs;

    if (!pNs) pNs = interp->pGlobalNs;
    if (!pNs->paExpansion) {
	Th8_SetResult(interp, "no such expansion operator", TH8_NOLEN);
	return TH8_ERROR;
    }

    pEntry = Th8_HashFind(interp, pNs->paExpansion, zTag, nTag, 0);
    if (!pEntry) {
	Th8_SetResult(interp, "no such expansion operator", TH8_NOLEN);
	return TH8_ERROR;
    }
    Th8_Free(interp, pEntry->pData);
    Th8_HashRemove(interp, pNs->paExpansion, zTag, nTag);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FindExpansion --
 *
 *	Look up an expansion operator by tag, walking the namespace
 *	hierarchy from the current namespace up to the global.
 *
 * Why / How:
 *	The parser calls this when it encounters {tag}word syntax.
 *	Namespace scoping means a child namespace inherits expansion
 *	operators from its ancestors.  The search walks current,
 *	parent, grandparent, ..., global, stopping at the first
 *	match.
 *
 * Results:
 *	TH8_OK if found (pxProc and ppCtx are filled);
 *	TH8_ERROR if no matching operator exists in any ancestor.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_FindExpansion(
    Th8_Interp *interp,
    const char *zTag,
    size_t nTag,
    Th8_ExpansionProc *pxProc,
    void **ppCtx)
{
    Th8_Namespace *pNs;

    if (!interp) return TH8_ERROR;

    /*
     * Walk the namespace hierarchy: current ==> parent ==> global.
     */

    for (pNs = interp->pCurrentNs; pNs; pNs = pNs->pParent) {
	if (pNs->paExpansion) {
	    Th8_HashEntry *pEntry =
	        Th8_HashFind(interp, pNs->paExpansion, zTag, nTag, 0);

	    /* Bug 26 family: plain guard; tombstoned expansion
	     * entries have pData == NULL after Th8_HashDelete. */
	    if (pEntry && pEntry->pData) {
		Th8_ExpansionEntry *pExp = (Th8_ExpansionEntry *)
		                               pEntry->pData;

		*pxProc = pExp->xProc;
		if (ppCtx) *ppCtx = pExp->pCtx;
		return TH8_OK;
	    }
	}
    }
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendExpansions --
 *
 *	Append registered expansion operator tag names to a list
 *	string.  Iterates the current namespace's paExpansion hash,
 *	optionally filtering by a glob pattern.
 *
 * Why / How:
 *	Provides the enumeration behind [info expansions ?pattern?].
 *	Only the current namespace's operators are listed (the
 *	namespace hierarchy walk is for lookup, not enumeration).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends to *pzList / *pnList.
 *
 *----------------------------------------------------------------------
 */

/*
 * th8ExpansionListCallback --
 *
 *	Hash iteration callback for Th8_ListAppendExpansions.
 */

static int
th8ExpansionListCallback(Th8_HashEntry *pEntry, void *pCtx)
{
    void **aCtx = (void **)pCtx;
    Th8_Interp *interp = (Th8_Interp *)aCtx[0];
    char **pzList = (char **)aCtx[1];
    size_t *pnList = (size_t *)aCtx[2];
    const char *zPat = (const char *)aCtx[3];
    size_t nPat = (size_t)(th8_int64_t)aCtx[4];

    if (!zPat ||
        Th8_GlobMatch(interp, zPat, nPat, pEntry->zKey, pEntry->nKey)) {
	Th8_ListAppend(interp, pzList, pnList, pEntry->zKey, pEntry->nKey);
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendExpansions --
 *
 *	Append the names of every `[namespace ensemble]` expansion
 *	defined in the interpreter's current namespace (falling
 *	back to the global namespace) that matches the glob
 *	pattern `zPat`/`nPat` to the caller's Tcl-list buffer.
 *	Used by `[info expansions]` to enumerate expansion-style
 *	commands.
 *
 *	If `zPat` is empty (`nPat == 0`), every expansion name is
 *	appended.  Pattern matching follows the standard
 *	`Th8_GlobMatch` rules.  A no-expansions namespace (no
 *	`paExpansion` table installed) yields no entries, not an
 *	error.
 *
 * Parameters:
 *	interp -- interpreter holding the namespace.  No-op if NULL.
 *	pzList -- output: pointer to the Tcl-list buffer.  No-op
 *		  if NULL.
 *	pnList -- output: pointer to the buffer length.  No-op
 *		  if NULL.
 *	zPat   -- glob pattern to match against expansion names.
 *	nPat   -- length of `zPat` in bytes (0 means "match all").
 *
 * Returns:
 *	Nothing.  Errors from inner `Th8_ListAppend` surface as an
 *	error result on `interp` for the upper layer.
 *
 * Side effects:
 *	Appends matching expansion names to `*pzList` / `*pnList`,
 *	which may grow / reallocate the buffer.
 *
 *----------------------------------------------------------------------
 */
void
Th8_ListAppendExpansions(
    Th8_Interp *interp,
    char **pzList,
    size_t *pnList,
    const char *zPat,
    size_t nPat)
{
    Th8_Namespace *pNs;

    /* Defensive guards split per Finding 005. */
    if (!interp) return;
    if (!pzList) return;
    if (!pnList) return;
    pNs = interp->pCurrentNs;
    if (!pNs) pNs = interp->pGlobalNs;
    /* pGlobalNs is set during interpreter init and stays non-NULL
     * for the interp's lifetime, so the fallback at the previous
     * line guarantees pNs is non-NULL here.  The !pNs sub-check
     * is a defensive belt-and-braces test, never T at re-entry. */
    /* Bug 26 family: plain pNs guard.  Caller can pass a
     * NULL namespace pointer when the lookup missed; the
     * paExpansion deref would crash under TH8_OMIT. */
    if (!pNs || !pNs->paExpansion) return;

    {
	void *aCtx[5];

	aCtx[0] = interp;
	aCtx[1] = pzList;
	aCtx[2] = pnList;
	aCtx[3] = (void *)zPat;
	aCtx[4] = TH8_INT2PTR(nPat);
	Th8_HashIterate(
	    interp, pNs->paExpansion, th8ExpansionListCallback, (void *)aCtx);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendBreakpoints --
 *
 *	Append breakpoint descriptions to a list string.  Each
 *	breakpoint contributes three list elements: the breakpoint
 *	ID (integer), the script name, and the line number.
 *
 * Why / How:
 *	Provides the enumeration behind [info breakpoints].  The
 *	breakpoint hash key encodes the script name followed by a
 *	NUL byte and a 4-byte little-endian line number.  This
 *	function decodes each key to extract the human-readable
 *	script name and line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends to *pzList / *pnList.
 *
 *----------------------------------------------------------------------
 */

/*
 * th8IntToStr --
 *
 *	Format an int into a stack buffer.  Returns a pointer into
 *	zBuf (which must be at least 20 bytes).
 */

static const char *
th8IntToStr(int v, char *zBuf)
{
    int neg = 0;
    unsigned int u;
    char *z = &zBuf[19];

    *z = 0;
    if (v < 0) {
	neg = 1;
	u = (unsigned int)(-(v + 1)) + 1u;
    } else {
	u = (unsigned int)v;
    }
    do {
	*(--z) = (char)('0' + (u % 10));
	u /= 10;
    } while (u > 0);
    if (neg) *(--z) = '-';
    return z;
}

/*
 *----------------------------------------------------------------------
 *
 * th8BreakpointListCallback --
 *
 *	`Th8_HashIterate` visitor for `Th8_ListAppendBreakpoints`.
 *	Each entry in the breakpoint hash maps a packed
 *	`scriptName\0 + line` key to a numeric breakpoint id; this
 *	callback decodes the key and appends the
 *	`{id name line}` triple to the caller's list.
 *
 *	Key format (`pEntry->zKey`, `pEntry->nKey`):
 *	    scriptName bytes,
 *	    a NUL byte,
 *	    four bytes of little-endian line number.
 *	Total length must be at least 6 bytes (1-byte name + NUL +
 *	4 line bytes); shorter keys are silently skipped, matching
 *	the iterate-anyway contract.
 *
 * Parameters:
 *	pEntry -- hash entry to decode.
 *	pCtx   -- caller's `void *[3]`:
 *	         [0] `Th8_Interp *` for `Th8_ListAppend`,
 *	         [1] `char **` output list pointer,
 *	         [2] `size_t *` output list length.
 *
 * Returns:
 *	`TH8_OK` always (the iterate continues even when one entry
 *	is malformed, by design).
 *
 * Side effects:
 *	Appends the decoded triple to `*pzList` / `*pnList` via
 *	`Th8_ListAppend`, which may grow / reallocate `*pzList`.
 *
 *----------------------------------------------------------------------
 */
static int
th8BreakpointListCallback(Th8_HashEntry *pEntry, void *pCtx)
{
    void **aCtx = (void **)pCtx;
    Th8_Interp *interp = (Th8_Interp *)aCtx[0];
    char **pzList = (char **)aCtx[1];
    size_t *pnList = (size_t *)aCtx[2];
    int id = (int)(size_t)pEntry->pData;
    const char *zKey = pEntry->zKey;
    size_t nKey = pEntry->nKey;
    char zBuf[20];

    /*
     * Key format: scriptName\0 + 4 bytes line (little-endian).
     */
    if (nKey >= 6) {
	size_t nName = nKey - 5;
	const unsigned char *pLine = (const unsigned char *)&zKey[nName + 1];
	int nLine = pLine[0] | (pLine[1] << 8) | (pLine[2] << 16) |
	            (pLine[3] << 24);

	Th8_ListAppend(
	    interp, pzList, pnList, th8IntToStr(id, zBuf), TH8_NOLEN);
	Th8_ListAppend(interp, pzList, pnList, zKey, nName);
	Th8_ListAppend(
	    interp, pzList, pnList, th8IntToStr(nLine, zBuf), TH8_NOLEN);
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendBreakpoints --
 *
 *	Append a Tcl-list serialisation of every active breakpoint
 *	to `*pzList` / `*pnList`.  Each breakpoint becomes three
 *	consecutive list elements: `id`, `scriptName`, `line`.
 *	Used by the `[info breakpoints]` introspection path.
 *
 *	If the interpreter has no breakpoint hash installed (the
 *	common case for builds without `xDebug`), the call is a
 *	no-op.  NULL arguments are tolerated (silently ignored)
 *	per the Bug 26 defensive-guard policy.
 *
 * Parameters:
 *	interp -- interpreter holding the breakpoint hash.  No-op
 *		  if NULL.
 *	pzList -- output: pointer to the Tcl-list buffer.  No-op
 *		  if NULL.
 *	pnList -- output: pointer to the buffer length.  No-op
 *		  if NULL.
 *
 * Returns:
 *	Nothing.  Cannot fail to the caller: malformed entries are
 *	skipped (see `th8BreakpointListCallback`) and allocation
 *	errors inside `Th8_ListAppend` surface as an error result
 *	on `interp` for the upper layer.
 *
 * Side effects:
 *	Appends to `*pzList` / `*pnList` via `Th8_ListAppend`,
 *	which may grow / reallocate the buffer.
 *
 *----------------------------------------------------------------------
 */
void
Th8_ListAppendBreakpoints(Th8_Interp *interp, char **pzList, size_t *pnList)
{
    /* Defensive guards split per Finding 005. */
    if (!interp) return;
    if (!pzList) return;
    if (!pnList) return;
    if (!interp->paBreakpoints) return;

    {
	void *aCtx[3];

	aCtx[0] = interp;
	aCtx[1] = pzList;
	aCtx[2] = pnList;
	Th8_HashIterate(
	    interp, interp->paBreakpoints, th8BreakpointListCallback,
	    (void *)aCtx);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8InFrame --
 *
 *	Push a new frame, call a function, pop the frame.  The
 *	frame is heap-allocated.
 *
 * Why / How:
 *	Allocates a Th8_Frame on the heap, pushes it as the current
 *	frame (creating a new variable scope), invokes the callback,
 *	then pops and frees the frame.  This ensures the callback
 *	executes in an isolated scope regardless of how it returns.
 *
 * Results:
 *	Return code from the callback.
 *
 * Side effects:
 *	A new variable scope is created and destroyed.
 *
 *----------------------------------------------------------------------
 */

int
th8InFrame(
    Th8_Interp *interp, /* Interpreter. */
    int (*xCall)(Th8_Interp *, void *, void *),
    /* Callback to invoke. */
    void *pContext1, /* First context argument. */
    void *pContext2) /* Second context argument. */
{
    Th8_Frame *pFrame;
    int rc;

    pFrame = (Th8_Frame *)TH8_ALLOC(interp, sizeof(Th8_Frame));
    if (!pFrame) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (th8PushFrame(interp, pFrame) != TH8_OK) {
	Th8_Free(interp, pFrame);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    rc = xCall(interp, pContext1, pContext2);
    th8PopFrame(interp);
    Th8_Free(interp, pFrame);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8SetFrameObjv --
 *
 *	Store the invocation (command name + arguments) on the
 *	current call frame.  This is used by proc dispatch so
 *	that [info level] can report what command was called.
 *
 *	The pointers are borrowed -- they must remain valid for
 *	the lifetime of the frame (which is guaranteed because
 *	the frame only lives while the command dispatch is active).
 *
 * Why / How:
 *	Simply stores the argc/argv/argl triple on interp->pFrame.
 *	The proc dispatch (proc_call_nr) calls this before entering
 *	the proc body so that [info level 0] can reconstruct the
 *	invocation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets argc/argv/argl on the current frame.
 *
 *----------------------------------------------------------------------
 */

void
th8SetFrameObjv(
    Th8_Interp *interp, /* Interpreter. */
    int argc, /* Number of arguments. */
    const char **argv, /* Argument values. */
    size_t *argl) /* Argument lengths. */
{
    interp->pFrame->argc = argc;
    interp->pFrame->argv = argv;
    interp->pFrame->argl = argl;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GetFrameLevel --
 *
 *	Return the current procedure nesting level.  The global
 *	frame is level 0, the first proc call is level 1, etc.
 *
 * Why / How:
 *	Walks the pCaller chain from interp->pFrame toward the
 *	global frame, counting only frames that have argc > 0
 *	(indicating a proc invocation).  Non-proc frames (e.g.
 *	scope frames from [namespace eval]) are skipped.
 *
 * Results:
 *	Non-negative integer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8GetFrameLevel(Th8_Interp *interp) /* Interpreter. */
{
    int nLevel = 0;
    Th8_Frame *p;

    for (p = interp->pFrame; p; p = p->pCaller) {
	if (p->argc > 0) {
	    nLevel++;
	}
    }
    return nLevel;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetFrameObjv --
 *
 *	Return the invocation info for the frame at a given level.
 *	Absolute level N (positive) counts from the global frame.
 *	Relative level -N counts upward from the current frame.
 *
 * Why / How:
 *	Implements Tcl's [info level] semantics.  Positive levels
 *	are absolute (1 = outermost proc); non-positive levels are
 *	relative to the current frame (0 = current, -1 = caller).
 *	The function computes the current level via th8GetFrameLevel,
 *	resolves the target, then walks the pCaller chain counting
 *	proc-like frames until the target level is reached.
 *
 * Results:
 *	TH8_OK on success, TH8_ERROR if the level is out of range
 *	or the frame has no invocation info.
 *
 * Side effects:
 *	Sets *pArgc, *pArgv, *pArgl to the frame's invocation.
 *
 *----------------------------------------------------------------------
 */

int
Th8_GetFrameObjv(
    Th8_Interp *interp, /* Interpreter. */
    int iLevel, /* Level to query. */
    int *pArgc, /* OUT: Number of arguments. */
    const char ***pArgv, /* OUT: Argument values. */
    size_t **pArgl) /* OUT: Argument lengths. */
{
    Th8_Frame *p;
    int nCurrent;
    int nTarget;
    int nSeen;

    if (!interp) return TH8_ERROR;
    nCurrent = th8GetFrameLevel(interp);

    /*
     * Tcl semantics: if the argument is <= 0, it is relative
     * to the current level.  If positive, it is absolute.
     *
     *   info level 0    => current frame (0 + current = current)
     *   info level -1   => caller (-1 + current)
     *   info level 1    => absolute level 1 (outermost proc)
     */

    if (iLevel <= 0) {
	nTarget = nCurrent + iLevel;
    } else {
	nTarget = iLevel;
    }

    if (nTarget <= 0 || nTarget > nCurrent) {
	Th8_SetResult(interp, "bad level", TH8_NOLEN);
	return TH8_ERROR;
    }

    /*
     * Walk from the current frame toward the global frame,
     * counting only proc-like frames (argc > 0).  Stop when
     * we reach the target level.
     */

    nSeen = nCurrent;
    for (p = interp->pFrame; p; p = p->pCaller) {
	if (p->argc > 0) {
	    if (nSeen == nTarget) {
		break;
	    }
	    nSeen--;
	}
    }

    /* Defensive post-walk guards split per Finding 005.  The
     * walk above only stops when p->argc > 0, so the C-pairs
     * on the original 3-condition compound were intrinsic-dead
     * (and !p->argv would only arise from a freed/torn-down
     * frame, OOM-class).  Sequencing as singles keeps the
     * runtime check intact while removing the dead C-pairs
     * from the MC/DC denominator. */
    if (!p) goto badLevel;
    if (p->argc == 0) goto badLevel;
    if (!p->argv) goto badLevel;

    *pArgc = p->argc;
    *pArgv = p->argv;
    *pArgl = p->argl;
    return TH8_OK;

badLevel:
    Th8_SetResult(interp, "bad level", TH8_NOLEN);
    return TH8_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * th8DoubleToDiyFp --
 *
 *	Decompose a positive IEEE 754 double into a normalized
 *	"do-it-yourself floating point" representation: a 64-bit
 *	mantissa with the top bit set, paired with a binary
 *	exponent such that value = mantissa * 2^exponent.
 *
 * Why / How:
 *	Backing primitive for the Grisu digit-extraction path
 *	in Th8_SetResultDouble.  By working in normalized DiyFp
 *	form we can perform a single 64x64 multiply-and-truncate
 *	against a cached power of ten to land the value in an
 *	integer-friendly range, replacing the historical
 *	`mant /= 1e16` cascade that lost precision near DBL_MAX
 *	and DBL_MIN_NORMAL.
 *
 *	Assumes rVal > 0, finite, non-NaN.  Subnormals (biased
 *	exponent 0) are handled with the IEEE 754 unbiased rule
 *	v = frac * 2^-1074.
 *
 *----------------------------------------------------------------------
 */

static void
th8DoubleToDiyFp(
    double rVal, /* Positive finite double. */
    th8_uint64_t *pF, /* OUT: normalized 64-bit mantissa. */
    int *pE) /* OUT: binary exponent. */
{
    union {
	double d;
	th8_uint64_t u;
    } cvt;
    th8_uint64_t bits;
    th8_uint64_t f;
    int biased;
    int e;

    cvt.d = rVal;
    bits = cvt.u;
    biased = (int)((bits >> 52) & 0x7FF);
    if (biased == 0) {
	f = bits & 0xFFFFFFFFFFFFFULL;
	e = -1074;
    } else {
	f = (bits & 0xFFFFFFFFFFFFFULL) | (1ULL << 52);
	e = biased - 1075;
    }
    while (!(f & (1ULL << 63))) {
	f <<= 1;
	e--;
    }
    *pF = f;
    *pE = e;
}


/*
 *----------------------------------------------------------------------
 *
 * th8Mul64Top --
 *
 *	Return the top 64 bits of the 128-bit product (x * y),
 *	rounded to nearest.
 *
 * Why / How:
 *	Used to multiply two normalized 64-bit mantissas in the
 *	Grisu path without requiring a 128-bit integer type.
 *	Decomposes each input into 32-bit halves and accumulates
 *	the four partial products.  Adds 2^31 to the central
 *	partial sum before truncating to obtain round-to-nearest
 *	on the dropped low 64 bits.  The reconstructed result is
 *	correct within 1 unit-in-the-last-place, which is the
 *	error budget Grisu accounts for.
 *
 *----------------------------------------------------------------------
 */

static th8_uint64_t
th8Mul64Top(
    th8_uint64_t x, /* First factor. */
    th8_uint64_t y) /* Second factor. */
{
    th8_uint64_t xl = x & 0xFFFFFFFFULL;
    th8_uint64_t xh = x >> 32;
    th8_uint64_t yl = y & 0xFFFFFFFFULL;
    th8_uint64_t yh = y >> 32;
    th8_uint64_t ll = xl * yl;
    th8_uint64_t hl = xh * yl;
    th8_uint64_t lh = xl * yh;
    th8_uint64_t hh = xh * yh;
    th8_uint64_t mid;

    mid = (ll >> 32) + (hl & 0xFFFFFFFFULL) + (lh & 0xFFFFFFFFULL) +
          (1ULL << 31);
    return hh + (hl >> 32) + (lh >> 32) + (mid >> 32);
}


/*
 *----------------------------------------------------------------------
 *
 * Cached powers of ten for the Grisu digit extractor --
 *
 *	Each entry i represents 10^K with K = -348 + i*8.  The
 *	significand is a 64-bit value with the top bit set; the
 *	binary exponent is stored separately.  Together they form
 *	a DiyFp such that significand * 2^exponent approximates
 *	10^K within a few ULP.
 *
 *	The table spans decimal exponents -348..340 in steps of
 *	8, which is sufficient for every finite double (decimal
 *	range -324..308) plus the safety margin Grisu needs at
 *	the boundaries.
 *
 *----------------------------------------------------------------------
 */

static const th8_uint64_t th8Pow10Sig[87] =
    {0xfa8fd5a0081c0288ULL, 0xbaaee17fa23ebf76ULL, 0x8b16fb203055ac76ULL,
     0xcf42894a5dce35eaULL, 0x9a6bb0aa55653b2dULL, 0xe61acf033d1a45dfULL,
     0xab70fe17c79ac6caULL, 0xff77b1fcbebcdc4fULL, 0xbe5691ef416bd60cULL,
     0x8dd01fad907ffc3cULL, 0xd3515c2831559a83ULL, 0x9d71ac8fada6c9b5ULL,
     0xea9c227723ee8bcbULL, 0xaecc49914078536dULL, 0x823c12795db6ce57ULL,
     0xc21094364dfb5637ULL, 0x9096ea6f3848984fULL, 0xd77485cb25823ac7ULL,
     0xa086cfcd97bf97f4ULL, 0xef340a98172aace5ULL, 0xb23867fb2a35b28eULL,
     0x84c8d4dfd2c63f3bULL, 0xc5dd44271ad3cdbaULL, 0x936b9fcebb25c996ULL,
     0xdbac6c247d62a584ULL, 0xa3ab66580d5fdaf6ULL, 0xf3e2f893dec3f126ULL,
     0xb5b5ada8aaff80b8ULL, 0x87625f056c7c4a8bULL, 0xc9bcff6034c13053ULL,
     0x964e858c91ba2655ULL, 0xdff9772470297ebdULL, 0xa6dfbd9fb8e5b88fULL,
     0xf8a95fcf88747d94ULL, 0xb94470938fa89bcfULL, 0x8a08f0f8bf0f156bULL,
     0xcdb02555653131b6ULL, 0x993fe2c6d07b7facULL, 0xe45c10c42a2b3b06ULL,
     0xaa242499697392d3ULL, 0xfd87b5f28300ca0eULL, 0xbce5086492111aebULL,
     0x8cbccc096f5088ccULL, 0xd1b71758e219652cULL, 0x9c40000000000000ULL,
     0xe8d4a51000000000ULL, 0xad78ebc5ac620000ULL, 0x813f3978f8940984ULL,
     0xc097ce7bc90715b3ULL, 0x8f7e32ce7bea5c70ULL, 0xd5d238a4abe98068ULL,
     0x9f4f2726179a2245ULL, 0xed63a231d4c4fb27ULL, 0xb0de65388cc8ada8ULL,
     0x83c7088e1aab65dbULL, 0xc45d1df942711d9aULL, 0x924d692ca61be758ULL,
     0xda01ee641a708deaULL, 0xa26da3999aef774aULL, 0xf209787bb47d6b85ULL,
     0xb454e4a179dd1877ULL, 0x865b86925b9bc5c2ULL, 0xc83553c5c8965d3dULL,
     0x952ab45cfa97a0b3ULL, 0xde469fbd99a05fe3ULL, 0xa59bc234db398c25ULL,
     0xf6c69a72a3989f5cULL, 0xb7dcbf5354e9beceULL, 0x88fcf317f22241e2ULL,
     0xcc20ce9bd35c78a5ULL, 0x98165af37b2153dfULL, 0xe2a0b5dc971f303aULL,
     0xa8d9d1535ce3b396ULL, 0xfb9b7cd9a4a7443cULL, 0xbb764c4ca7a44410ULL,
     0x8bab8eefb6409c1aULL, 0xd01fef10a657842cULL, 0x9b10a4e5e9913129ULL,
     0xe7109bfba19c0c9dULL, 0xac2820d9623bf429ULL, 0x80444b5e7aa7cf85ULL,
     0xbf21e44003acdd2dULL, 0x8e679c2f5e44ff8fULL, 0xd433179d9c8cb841ULL,
     0x9e19db92b4e31ba9ULL, 0xeb96bf6ebadf77d9ULL, 0xaf87023b9bf0ee6bULL};

static const int th8Pow10E[87] =
    {-1220, -1193, -1166, -1140, -1113, -1087, -1060, -1034, -1007, -980,
     -954,  -927,  -901,  -874,  -847,  -821,  -794,  -768,  -741,  -715,
     -688,  -661,  -635,  -608,  -582,  -555,  -529,  -502,  -475,  -449,
     -422,  -396,  -369,  -343,  -316,  -289,  -263,  -236,  -210,  -183,
     -157,  -130,  -103,  -77,   -50,   -24,   3,     30,    56,    83,
     109,   136,   162,   189,   216,   242,   269,   295,   322,   348,
     375,   402,   428,   455,   481,   508,   534,   561,   588,   614,
     641,   667,   694,   720,   747,   774,   800,   827,   853,   880,
     907,   933,   960,   986,   1013,  1039,  1066};


/*
 *----------------------------------------------------------------------
 *
 * th8GetCachedPow10 --
 *
 *	Select the cached power 10^K whose binary exponent, when
 *	added to e_in + 64, lands in the Grisu working range
 *	[-60, -32].  This ensures the scaled product has its
 *	integer part fitting in a 32-bit-ish slice, suitable for
 *	a per-digit loop.
 *
 * Why / How:
 *	The Grisu scaling step needs the result's binary exponent
 *	near zero with the integer part comfortably under 2^32 so
 *	the digit-extraction loop can divide by powers of ten.
 *	A linear scan of 87 entries is bounded; binary search is
 *	not warranted at this size.
 *
 *	Returns the cached significand, its binary exponent, and
 *	the corresponding decimal exponent K.
 *
 *----------------------------------------------------------------------
 */

static void
th8GetCachedPow10(
    int eIn, /* Binary exponent of input. */
    th8_uint64_t *pSig, /* OUT: cached significand. */
    int *pE, /* OUT: cached binary exponent. */
    int *pK) /* OUT: corresponding decimal exponent. */
{
    int i;

    for (i = 0; i < 87; i++) {
	int prodE = eIn + th8Pow10E[i] + 64;

	if (prodE >= -60 && prodE <= -32) {
	    *pSig = th8Pow10Sig[i];
	    *pE = th8Pow10E[i];
	    *pK = -348 + i * 8;
	    return;
	}
    }

    /*
     * Out-of-range input.  Defensive fallback; for any valid
     * finite double the loop above selects an entry.
     */

    *pSig = 1ULL << 63;
    *pE = -63;
    *pK = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ExtractDigits17 --
 *
 *	Extract the first 17 decimal digits of |rVal| into zDig
 *	and store the decimal exponent of the leading digit in
 *	*pExpn.
 *
 * Why / How:
 *	Implements the digit-generation half of Grisu using DiyFp
 *	arithmetic with a cached power-of-ten table.  Steps:
 *	  1. Decompose rVal into a normalized DiyFp (v.f, v.e).
 *	  2. Select cached 10^K such that v * 10^K has its binary
 *	     exponent in [-60, -32].
 *	  3. Form w = v * (cached significand) >> 64; w now has
 *	     about 17 leading decimal digits in its integer part.
 *	  4. Walk the integer part with successive divide-by-10s
 *	     to harvest the leading digits, then shift the
 *	     fractional part by 10 each step for the trailing
 *	     digits, until 17 are collected.
 *
 *	The 17 digits agree with |rVal| to within ~1 ULP at the
 *	last digit position, which is well inside the precision
 *	the caller's round-trip-check loop needs to find the
 *	shortest correct representation.
 *
 *	Assumes rVal > 0, finite, non-NaN, not zero.
 *
 *----------------------------------------------------------------------
 */

static void
th8ExtractDigits17(
    double rVal, /* Positive finite non-zero double. */
    char *zDig, /* OUT: 17 digit characters. */
    int *pExpn) /* OUT: decimal exponent of zDig[0]. */
{
    static const th8_uint64_t aPow10[11] = {1ULL,          10ULL,
                                            100ULL,        1000ULL,
                                            10000ULL,      100000ULL,
                                            1000000ULL,    10000000ULL,
                                            100000000ULL,  1000000000ULL,
                                            10000000000ULL};
    th8_uint64_t vF, cF, wF, oneF, oneMask, p1, p2;
    int vE, cE, K, wE, shift, initialKappa, kappa, len;

    th8DoubleToDiyFp(rVal, &vF, &vE);
    th8GetCachedPow10(vE, &cF, &cE, &K);

    wF = th8Mul64Top(vF, cF);
    wE = vE + cE + 64;
    shift = -wE;
    oneF = 1ULL << shift;
    oneMask = oneF - 1;
    p1 = wF >> shift;
    p2 = wF & oneMask;

    /*
     * Find the digit count of p1 (always in [1..10] given
     * the constraints chosen by th8GetCachedPow10).
     */

    for (initialKappa = 10; initialKappa > 0; initialKappa--) {
	if (aPow10[initialKappa - 1] <= p1) break;
    }
    if (initialKappa == 0) initialKappa = 1;

    /*
     * Extract 18 digits, one more than the IEEE 754 round-trip
     * minimum.  Digit 18 is used to round digit 17: if digit 18
     * is >= '5', increment digit 17 (with carry propagation).
     * This ensures the 17-digit prefix the caller sees is the
     * correctly-rounded representation, not the truncated one
     * -- critical for values like DBL_MIN_NORMAL whose exact
     * value lies between digit-17 boundaries.
     */

    len = 0;
    kappa = initialKappa;
    /* initialKappa is bounded by the 11-entry aPow10 table at
     * L17040 (max value 10), so this loop runs at most 10
     * iterations and len < 18 is intrinsic-true throughout.
     * The explicit bound is a defensive safety against table
     * growth; wrap with ALWAYS so MC/DC folds it away. */
    while (kappa > 0 && ALWAYS(len < 18)) {
	th8_uint64_t div;
	unsigned int d;

	kappa--;
	div = aPow10[kappa];
	d = (unsigned int)(p1 / div);
	p1 = p1 - (th8_uint64_t)d * div;
	zDig[len++] = (char)('0' + d);
    }
    while (len < 18) {
	unsigned int d;

	p2 *= 10;
	d = (unsigned int)(p2 >> shift);
	p2 &= oneMask;
	zDig[len++] = (char)('0' + d);
    }

    /*
     * Decimal exponent of the leading digit.  The integer part
     * of w has initialKappa digits, and w = rVal * 10^K, so
     * rVal = w * 10^-K, whose leading digit has place value
     * 10^(initialKappa - 1 - K).
     */

    *pExpn = initialKappa - 1 - K;

    /*
     * Round digit 17 using digit 18, with carry propagation.
     * If a carry propagates all the way past digit 0, prepend
     * '1' and bump the decimal exponent (handles 9.99...9e17
     * -> 1.00e18 style overflow).
     */

    if (zDig[17] >= '5') {
	int i;
	int carry = 1;

	for (i = 16; i >= 0 && carry; i--) {
	    zDig[i] = (char)(zDig[i] + 1);
	    if (zDig[i] > '9') {
		zDig[i] = '0';
		carry = 1;
	    } else {
		carry = 0;
	    }
	}
	if (carry) {
	    /* Cascaded all the way: digits become "10000...0",
	     * decimal exponent bumps by 1. */
	    int j;

	    for (j = 16; j > 0; j--)
		zDig[j] = zDig[j - 1];
	    zDig[0] = '1';
	    (*pExpn)++;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Eisel-Lemire cached power-of-ten table --
 *
 *	128-bit cached values of 10^q for q in [-342..308], indexed
 *	by (q - TH8_POW10_Q_MIN).  Each entry's significand is
 *	normalized so the top bit of `hi` is set; together with the
 *	binary exponent `e`, the entry represents
 *	  10^q ~= (hi * 2^64 + lo) * 2^e
 *	to within 0.5 ULP at the 128-bit position.
 *
 * Why / How:
 *	Used by `th8ScaleByPow10` (Th8_ToDouble's correctly-rounded
 *	scaling step) for the Eisel-Lemire fast path -- a single
 *	full-precision 64x128 multiplication that produces a
 *	correctly-rounded IEEE 754 double for ~99.5% of inputs.  The
 *	remaining halfway cases fall back to a libtommath exact
 *	comparison when TH8_ENABLE_BIGINT is on.
 *
 *	The table size (651 entries x 24 bytes ~= 15.6 KB rodata)
 *	covers every finite double's decimal exponent range plus
 *	enough margin that we never have to extrapolate.
 *
 *----------------------------------------------------------------------
 */

static const struct {
    th8_uint64_t hi;
    th8_uint64_t lo;
    int e;
} th8Pow10Lemire[651] = {
    {0xeef453d6923bd65aULL, 0x113faa2906a13b40ULL, -1264}, /* 10^-342 */
    {0x9558b4661b6565f8ULL, 0x4ac7ca59a424c508ULL, -1260}, /* 10^-341 */
    {0xbaaee17fa23ebf76ULL, 0x5d79bcf00d2df64aULL, -1257}, /* 10^-340 */
    {0xe95a99df8ace6f53ULL, 0xf4d82c2c107973dcULL, -1254}, /* 10^-339 */
    {0x91d8a02bb6c10594ULL, 0x79071b9b8a4be86aULL, -1250}, /* 10^-338 */
    {0xb64ec836a47146f9ULL, 0x9748e2826cdee284ULL, -1247}, /* 10^-337 */
    {0xe3e27a444d8d98b7ULL, 0xfd1b1b2308169b25ULL, -1244}, /* 10^-336 */
    {0x8e6d8c6ab0787f72ULL, 0xfe30f0f5e50e20f7ULL, -1240}, /* 10^-335 */
    {0xb208ef855c969f4fULL, 0xbdbd2d335e51a935ULL, -1237}, /* 10^-334 */
    {0xde8b2b66b3bc4723ULL, 0xad2c788035e61382ULL, -1234}, /* 10^-333 */
    {0x8b16fb203055ac76ULL, 0x4c3bcb5021afcc31ULL, -1230}, /* 10^-332 */
    {0xaddcb9e83c6b1793ULL, 0xdf4abe242a1bbf3eULL, -1227}, /* 10^-331 */
    {0xd953e8624b85dd78ULL, 0xd71d6dad34a2af0dULL, -1224}, /* 10^-330 */
    {0x87d4713d6f33aa6bULL, 0x8672648c40e5ad68ULL, -1220}, /* 10^-329 */
    {0xa9c98d8ccb009506ULL, 0x680efdaf511f18c2ULL, -1217}, /* 10^-328 */
    {0xd43bf0effdc0ba48ULL, 0x0212bd1b2566def3ULL, -1214}, /* 10^-327 */
    {0x84a57695fe98746dULL, 0x014bb630f7604b58ULL, -1210}, /* 10^-326 */
    {0xa5ced43b7e3e9188ULL, 0x419ea3bd35385e2eULL, -1207}, /* 10^-325 */
    {0xcf42894a5dce35eaULL, 0x52064cac828675b9ULL, -1204}, /* 10^-324 */
    {0x818995ce7aa0e1b2ULL, 0x7343efebd1940994ULL, -1200}, /* 10^-323 */
    {0xa1ebfb4219491a1fULL, 0x1014ebe6c5f90bf9ULL, -1197}, /* 10^-322 */
    {0xca66fa129f9b60a6ULL, 0xd41a26e077774ef7ULL, -1194}, /* 10^-321 */
    {0xfd00b897478238d0ULL, 0x8920b098955522b5ULL, -1191}, /* 10^-320 */
    {0x9e20735e8cb16382ULL, 0x55b46e5f5d5535b1ULL, -1187}, /* 10^-319 */
    {0xc5a890362fddbc62ULL, 0xeb2189f734aa831dULL, -1184}, /* 10^-318 */
    {0xf712b443bbd52b7bULL, 0xa5e9ec7501d523e4ULL, -1181}, /* 10^-317 */
    {0x9a6bb0aa55653b2dULL, 0x47b233c92125366fULL, -1177}, /* 10^-316 */
    {0xc1069cd4eabe89f8ULL, 0x999ec0bb696e840aULL, -1174}, /* 10^-315 */
    {0xf148440a256e2c76ULL, 0xc00670ea43ca250dULL, -1171}, /* 10^-314 */
    {0x96cd2a865764dbcaULL, 0x380406926a5e5728ULL, -1167}, /* 10^-313 */
    {0xbc807527ed3e12bcULL, 0xc605083704f5ecf2ULL, -1164}, /* 10^-312 */
    {0xeba09271e88d976bULL, 0xf7864a44c633682fULL, -1161}, /* 10^-311 */
    {0x93445b8731587ea3ULL, 0x7ab3ee6afbe0211dULL, -1157}, /* 10^-310 */
    {0xb8157268fdae9e4cULL, 0x5960ea05bad82965ULL, -1154}, /* 10^-309 */
    {0xe61acf033d1a45dfULL, 0x6fb92487298e33beULL, -1151}, /* 10^-308 */
    {0x8fd0c16206306babULL, 0xa5d3b6d479f8e057ULL, -1147}, /* 10^-307 */
    {0xb3c4f1ba87bc8696ULL, 0x8f48a4899877186cULL, -1144}, /* 10^-306 */
    {0xe0b62e2929aba83cULL, 0x331acdabfe94de87ULL, -1141}, /* 10^-305 */
    {0x8c71dcd9ba0b4925ULL, 0x9ff0c08b7f1d0b15ULL, -1137}, /* 10^-304 */
    {0xaf8e5410288e1b6fULL, 0x07ecf0ae5ee44ddaULL, -1134}, /* 10^-303 */
    {0xdb71e91432b1a24aULL, 0xc9e82cd9f69d6150ULL, -1131}, /* 10^-302 */
    {0x892731ac9faf056eULL, 0xbe311c083a225cd2ULL, -1127}, /* 10^-301 */
    {0xab70fe17c79ac6caULL, 0x6dbd630a48aaf407ULL, -1124}, /* 10^-300 */
    {0xd64d3d9db981787dULL, 0x092cbbccdad5b108ULL, -1121}, /* 10^-299 */
    {0x85f0468293f0eb4eULL, 0x25bbf56008c58ea5ULL, -1117}, /* 10^-298 */
    {0xa76c582338ed2621ULL, 0xaf2af2b80af6f24eULL, -1114}, /* 10^-297 */
    {0xd1476e2c07286faaULL, 0x1af5af660db4aee2ULL, -1111}, /* 10^-296 */
    {0x82cca4db847945caULL, 0x50d98d9fc890ed4dULL, -1107}, /* 10^-295 */
    {0xa37fce126597973cULL, 0xe50ff107bab528a1ULL, -1104}, /* 10^-294 */
    {0xcc5fc196fefd7d0cULL, 0x1e53ed49a96272c9ULL, -1101}, /* 10^-293 */
    {0xff77b1fcbebcdc4fULL, 0x25e8e89c13bb0f7bULL, -1098}, /* 10^-292 */
    {0x9faacf3df73609b1ULL, 0x77b191618c54e9adULL, -1094}, /* 10^-291 */
    {0xc795830d75038c1dULL, 0xd59df5b9ef6a2418ULL, -1091}, /* 10^-290 */
    {0xf97ae3d0d2446f25ULL, 0x4b0573286b44ad1eULL, -1088}, /* 10^-289 */
    {0x9becce62836ac577ULL, 0x4ee367f9430aec33ULL, -1084}, /* 10^-288 */
    {0xc2e801fb244576d5ULL, 0x229c41f793cda73fULL, -1081}, /* 10^-287 */
    {0xf3a20279ed56d48aULL, 0x6b43527578c1110fULL, -1078}, /* 10^-286 */
    {0x9845418c345644d6ULL, 0x830a13896b78aaaaULL, -1074}, /* 10^-285 */
    {0xbe5691ef416bd60cULL, 0x23cc986bc656d554ULL, -1071}, /* 10^-284 */
    {0xedec366b11c6cb8fULL, 0x2cbfbe86b7ec8aa9ULL, -1068}, /* 10^-283 */
    {0x94b3a202eb1c3f39ULL, 0x7bf7d71432f3d6aaULL, -1064}, /* 10^-282 */
    {0xb9e08a83a5e34f07ULL, 0xdaf5ccd93fb0cc54ULL, -1061}, /* 10^-281 */
    {0xe858ad248f5c22c9ULL, 0xd1b3400f8f9cff69ULL, -1058}, /* 10^-280 */
    {0x91376c36d99995beULL, 0x23100809b9c21fa2ULL, -1054}, /* 10^-279 */
    {0xb58547448ffffb2dULL, 0xabd40a0c2832a78aULL, -1051}, /* 10^-278 */
    {0xe2e69915b3fff9f9ULL, 0x16c90c8f323f516dULL, -1048}, /* 10^-277 */
    {0x8dd01fad907ffc3bULL, 0xae3da7d97f6792e4ULL, -1044}, /* 10^-276 */
    {0xb1442798f49ffb4aULL, 0x99cd11cfdf41779dULL, -1041}, /* 10^-275 */
    {0xdd95317f31c7fa1dULL, 0x40405643d711d584ULL, -1038}, /* 10^-274 */
    {0x8a7d3eef7f1cfc52ULL, 0x482835ea666b2572ULL, -1034}, /* 10^-273 */
    {0xad1c8eab5ee43b66ULL, 0xda3243650005eecfULL, -1031}, /* 10^-272 */
    {0xd863b256369d4a40ULL, 0x90bed43e40076a83ULL, -1028}, /* 10^-271 */
    {0x873e4f75e2224e68ULL, 0x5a7744a6e804a292ULL, -1024}, /* 10^-270 */
    {0xa90de3535aaae202ULL, 0x711515d0a205cb36ULL, -1021}, /* 10^-269 */
    {0xd3515c2831559a83ULL, 0x0d5a5b44ca873e04ULL, -1018}, /* 10^-268 */
    {0x8412d9991ed58091ULL, 0xe858790afe9486c2ULL, -1014}, /* 10^-267 */
    {0xa5178fff668ae0b6ULL, 0x626e974dbe39a873ULL, -1011}, /* 10^-266 */
    {0xce5d73ff402d98e3ULL, 0xfb0a3d212dc81290ULL, -1008}, /* 10^-265 */
    {0x80fa687f881c7f8eULL, 0x7ce66634bc9d0b9aULL, -1004}, /* 10^-264 */
    {0xa139029f6a239f72ULL, 0x1c1fffc1ebc44e80ULL, -1001}, /* 10^-263 */
    {0xc987434744ac874eULL, 0xa327ffb266b56220ULL, -998}, /* 10^-262 */
    {0xfbe9141915d7a922ULL, 0x4bf1ff9f0062baa8ULL, -995}, /* 10^-261 */
    {0x9d71ac8fada6c9b5ULL, 0x6f773fc3603db4a9ULL, -991}, /* 10^-260 */
    {0xc4ce17b399107c22ULL, 0xcb550fb4384d21d4ULL, -988}, /* 10^-259 */
    {0xf6019da07f549b2bULL, 0x7e2a53a146606a48ULL, -985}, /* 10^-258 */
    {0x99c102844f94e0fbULL, 0x2eda7444cbfc426dULL, -981}, /* 10^-257 */
    {0xc0314325637a1939ULL, 0xfa911155fefb5309ULL, -978}, /* 10^-256 */
    {0xf03d93eebc589f88ULL, 0x793555ab7eba27cbULL, -975}, /* 10^-255 */
    {0x96267c7535b763b5ULL, 0x4bc1558b2f3458dfULL, -971}, /* 10^-254 */
    {0xbbb01b9283253ca2ULL, 0x9eb1aaedfb016f16ULL, -968}, /* 10^-253 */
    {0xea9c227723ee8bcbULL, 0x465e15a979c1cadcULL, -965}, /* 10^-252 */
    {0x92a1958a7675175fULL, 0x0bfacd89ec191ecaULL, -961}, /* 10^-251 */
    {0xb749faed14125d36ULL, 0xcef980ec671f667cULL, -958}, /* 10^-250 */
    {0xe51c79a85916f484ULL, 0x82b7e12780e7401bULL, -955}, /* 10^-249 */
    {0x8f31cc0937ae58d2ULL, 0xd1b2ecb8b0908811ULL, -951}, /* 10^-248 */
    {0xb2fe3f0b8599ef07ULL, 0x861fa7e6dcb4aa15ULL, -948}, /* 10^-247 */
    {0xdfbdcece67006ac9ULL, 0x67a791e093e1d49aULL, -945}, /* 10^-246 */
    {0x8bd6a141006042bdULL, 0xe0c8bb2c5c6d24e0ULL, -941}, /* 10^-245 */
    {0xaecc49914078536dULL, 0x58fae9f773886e19ULL, -938}, /* 10^-244 */
    {0xda7f5bf590966848ULL, 0xaf39a475506a899fULL, -935}, /* 10^-243 */
    {0x888f99797a5e012dULL, 0x6d8406c952429603ULL, -931}, /* 10^-242 */
    {0xaab37fd7d8f58178ULL, 0xc8e5087ba6d33b84ULL, -928}, /* 10^-241 */
    {0xd5605fcdcf32e1d6ULL, 0xfb1e4a9a90880a65ULL, -925}, /* 10^-240 */
    {0x855c3be0a17fcd26ULL, 0x5cf2eea09a55067fULL, -921}, /* 10^-239 */
    {0xa6b34ad8c9dfc06fULL, 0xf42faa48c0ea481fULL, -918}, /* 10^-238 */
    {0xd0601d8efc57b08bULL, 0xf13b94daf124da27ULL, -915}, /* 10^-237 */
    {0x823c12795db6ce57ULL, 0x76c53d08d6b70858ULL, -911}, /* 10^-236 */
    {0xa2cb1717b52481edULL, 0x54768c4b0c64ca6eULL, -908}, /* 10^-235 */
    {0xcb7ddcdda26da268ULL, 0xa9942f5dcf7dfd0aULL, -905}, /* 10^-234 */
    {0xfe5d54150b090b02ULL, 0xd3f93b35435d7c4cULL, -902}, /* 10^-233 */
    {0x9efa548d26e5a6e1ULL, 0xc47bc5014a1a6db0ULL, -898}, /* 10^-232 */
    {0xc6b8e9b0709f109aULL, 0x359ab6419ca1091bULL, -895}, /* 10^-231 */
    {0xf867241c8cc6d4c0ULL, 0xc30163d203c94b62ULL, -892}, /* 10^-230 */
    {0x9b407691d7fc44f8ULL, 0x79e0de63425dcf1dULL, -888}, /* 10^-229 */
    {0xc21094364dfb5636ULL, 0x985915fc12f542e5ULL, -885}, /* 10^-228 */
    {0xf294b943e17a2bc4ULL, 0x3e6f5b7b17b2939eULL, -882}, /* 10^-227 */
    {0x979cf3ca6cec5b5aULL, 0xa705992ceecf9c43ULL, -878}, /* 10^-226 */
    {0xbd8430bd08277231ULL, 0x50c6ff782a838353ULL, -875}, /* 10^-225 */
    {0xece53cec4a314ebdULL, 0xa4f8bf5635246428ULL, -872}, /* 10^-224 */
    {0x940f4613ae5ed136ULL, 0x871b7795e136be99ULL, -868}, /* 10^-223 */
    {0xb913179899f68584ULL, 0x28e2557b59846e3fULL, -865}, /* 10^-222 */
    {0xe757dd7ec07426e5ULL, 0x331aeada2fe589cfULL, -862}, /* 10^-221 */
    {0x9096ea6f3848984fULL, 0x3ff0d2c85def7622ULL, -858}, /* 10^-220 */
    {0xb4bca50b065abe63ULL, 0x0fed077a756b53aaULL, -855}, /* 10^-219 */
    {0xe1ebce4dc7f16dfbULL, 0xd3e8495912c62894ULL, -852}, /* 10^-218 */
    {0x8d3360f09cf6e4bdULL, 0x64712dd7abbbd95dULL, -848}, /* 10^-217 */
    {0xb080392cc4349decULL, 0xbd8d794d96aacfb4ULL, -845}, /* 10^-216 */
    {0xdca04777f541c567ULL, 0xecf0d7a0fc5583a1ULL, -842}, /* 10^-215 */
    {0x89e42caaf9491b60ULL, 0xf41686c49db57245ULL, -838}, /* 10^-214 */
    {0xac5d37d5b79b6239ULL, 0x311c2875c522ced6ULL, -835}, /* 10^-213 */
    {0xd77485cb25823ac7ULL, 0x7d633293366b828bULL, -832}, /* 10^-212 */
    {0x86a8d39ef77164bcULL, 0xae5dff9c02033197ULL, -828}, /* 10^-211 */
    {0xa8530886b54dbdebULL, 0xd9f57f830283fdfdULL, -825}, /* 10^-210 */
    {0xd267caa862a12d66ULL, 0xd072df63c324fd7cULL, -822}, /* 10^-209 */
    {0x8380dea93da4bc60ULL, 0x4247cb9e59f71e6dULL, -818}, /* 10^-208 */
    {0xa46116538d0deb78ULL, 0x52d9be85f074e609ULL, -815}, /* 10^-207 */
    {0xcd795be870516656ULL, 0x67902e276c921f8bULL, -812}, /* 10^-206 */
    {0x806bd9714632dff6ULL, 0x00ba1cd8a3db53b7ULL, -808}, /* 10^-205 */
    {0xa086cfcd97bf97f3ULL, 0x80e8a40eccd228a5ULL, -805}, /* 10^-204 */
    {0xc8a883c0fdaf7df0ULL, 0x6122cd128006b2ceULL, -802}, /* 10^-203 */
    {0xfad2a4b13d1b5d6cULL, 0x796b805720085f81ULL, -799}, /* 10^-202 */
    {0x9cc3a6eec6311a63ULL, 0xcbe3303674053bb1ULL, -795}, /* 10^-201 */
    {0xc3f490aa77bd60fcULL, 0xbedbfc4411068a9dULL, -792}, /* 10^-200 */
    {0xf4f1b4d515acb93bULL, 0xee92fb5515482d44ULL, -789}, /* 10^-199 */
    {0x991711052d8bf3c5ULL, 0x751bdd152d4d1c4bULL, -785}, /* 10^-198 */
    {0xbf5cd54678eef0b6ULL, 0xd262d45a78a0635dULL, -782}, /* 10^-197 */
    {0xef340a98172aace4ULL, 0x86fb897116c87c35ULL, -779}, /* 10^-196 */
    {0x9580869f0e7aac0eULL, 0xd45d35e6ae3d4da1ULL, -775}, /* 10^-195 */
    {0xbae0a846d2195712ULL, 0x8974836059cca109ULL, -772}, /* 10^-194 */
    {0xe998d258869facd7ULL, 0x2bd1a438703fc94bULL, -769}, /* 10^-193 */
    {0x91ff83775423cc06ULL, 0x7b6306a34627ddcfULL, -765}, /* 10^-192 */
    {0xb67f6455292cbf08ULL, 0x1a3bc84c17b1d543ULL, -762}, /* 10^-191 */
    {0xe41f3d6a7377eecaULL, 0x20caba5f1d9e4a94ULL, -759}, /* 10^-190 */
    {0x8e938662882af53eULL, 0x547eb47b7282ee9cULL, -755}, /* 10^-189 */
    {0xb23867fb2a35b28dULL, 0xe99e619a4f23aa43ULL, -752}, /* 10^-188 */
    {0xdec681f9f4c31f31ULL, 0x6405fa00e2ec94d4ULL, -749}, /* 10^-187 */
    {0x8b3c113c38f9f37eULL, 0xde83bc408dd3dd05ULL, -745}, /* 10^-186 */
    {0xae0b158b4738705eULL, 0x9624ab50b148d446ULL, -742}, /* 10^-185 */
    {0xd98ddaee19068c76ULL, 0x3badd624dd9b0957ULL, -739}, /* 10^-184 */
    {0x87f8a8d4cfa417c9ULL, 0xe54ca5d70a80e5d6ULL, -735}, /* 10^-183 */
    {0xa9f6d30a038d1dbcULL, 0x5e9fcf4ccd211f4cULL, -732}, /* 10^-182 */
    {0xd47487cc8470652bULL, 0x7647c3200069671fULL, -729}, /* 10^-181 */
    {0x84c8d4dfd2c63f3bULL, 0x29ecd9f40041e073ULL, -725}, /* 10^-180 */
    {0xa5fb0a17c777cf09ULL, 0xf468107100525890ULL, -722}, /* 10^-179 */
    {0xcf79cc9db955c2ccULL, 0x7182148d4066eeb4ULL, -719}, /* 10^-178 */
    {0x81ac1fe293d599bfULL, 0xc6f14cd848405531ULL, -715}, /* 10^-177 */
    {0xa21727db38cb002fULL, 0xb8ada00e5a506a7dULL, -712}, /* 10^-176 */
    {0xca9cf1d206fdc03bULL, 0xa6d90811f0e4851cULL, -709}, /* 10^-175 */
    {0xfd442e4688bd304aULL, 0x908f4a166d1da663ULL, -706}, /* 10^-174 */
    {0x9e4a9cec15763e2eULL, 0x9a598e4e043287feULL, -702}, /* 10^-173 */
    {0xc5dd44271ad3cdbaULL, 0x40eff1e1853f29feULL, -699}, /* 10^-172 */
    {0xf7549530e188c128ULL, 0xd12bee59e68ef47dULL, -696}, /* 10^-171 */
    {0x9a94dd3e8cf578b9ULL, 0x82bb74f8301958ceULL, -692}, /* 10^-170 */
    {0xc13a148e3032d6e7ULL, 0xe36a52363c1faf02ULL, -689}, /* 10^-169 */
    {0xf18899b1bc3f8ca1ULL, 0xdc44e6c3cb279ac2ULL, -686}, /* 10^-168 */
    {0x96f5600f15a7b7e5ULL, 0x29ab103a5ef8c0b9ULL, -682}, /* 10^-167 */
    {0xbcb2b812db11a5deULL, 0x7415d448f6b6f0e8ULL, -679}, /* 10^-166 */
    {0xebdf661791d60f56ULL, 0x111b495b3464ad21ULL, -676}, /* 10^-165 */
    {0x936b9fcebb25c995ULL, 0xcab10dd900beec35ULL, -672}, /* 10^-164 */
    {0xb84687c269ef3bfbULL, 0x3d5d514f40eea742ULL, -669}, /* 10^-163 */
    {0xe65829b3046b0afaULL, 0x0cb4a5a3112a5113ULL, -666}, /* 10^-162 */
    {0x8ff71a0fe2c2e6dcULL, 0x47f0e785eaba72acULL, -662}, /* 10^-161 */
    {0xb3f4e093db73a093ULL, 0x59ed216765690f57ULL, -659}, /* 10^-160 */
    {0xe0f218b8d25088b8ULL, 0x306869c13ec3532cULL, -656}, /* 10^-159 */
    {0x8c974f7383725573ULL, 0x1e414218c73a13fcULL, -652}, /* 10^-158 */
    {0xafbd2350644eeacfULL, 0xe5d1929ef90898fbULL, -649}, /* 10^-157 */
    {0xdbac6c247d62a583ULL, 0xdf45f746b74abf39ULL, -646}, /* 10^-156 */
    {0x894bc396ce5da772ULL, 0x6b8bba8c328eb784ULL, -642}, /* 10^-155 */
    {0xab9eb47c81f5114fULL, 0x066ea92f3f326565ULL, -639}, /* 10^-154 */
    {0xd686619ba27255a2ULL, 0xc80a537b0efefebeULL, -636}, /* 10^-153 */
    {0x8613fd0145877585ULL, 0xbd06742ce95f5f37ULL, -632}, /* 10^-152 */
    {0xa798fc4196e952e7ULL, 0x2c48113823b73704ULL, -629}, /* 10^-151 */
    {0xd17f3b51fca3a7a0ULL, 0xf75a15862ca504c5ULL, -626}, /* 10^-150 */
    {0x82ef85133de648c4ULL, 0x9a984d73dbe722fbULL, -622}, /* 10^-149 */
    {0xa3ab66580d5fdaf5ULL, 0xc13e60d0d2e0ebbaULL, -619}, /* 10^-148 */
    {0xcc963fee10b7d1b3ULL, 0x318df905079926a9ULL, -616}, /* 10^-147 */
    {0xffbbcfe994e5c61fULL, 0xfdf17746497f7053ULL, -613}, /* 10^-146 */
    {0x9fd561f1fd0f9bd3ULL, 0xfeb6ea8bedefa634ULL, -609}, /* 10^-145 */
    {0xc7caba6e7c5382c8ULL, 0xfe64a52ee96b8fc1ULL, -606}, /* 10^-144 */
    {0xf9bd690a1b68637bULL, 0x3dfdce7aa3c673b1ULL, -603}, /* 10^-143 */
    {0x9c1661a651213e2dULL, 0x06bea10ca65c084fULL, -599}, /* 10^-142 */
    {0xc31bfa0fe5698db8ULL, 0x486e494fcff30a62ULL, -596}, /* 10^-141 */
    {0xf3e2f893dec3f126ULL, 0x5a89dba3c3efccfbULL, -593}, /* 10^-140 */
    {0x986ddb5c6b3a76b7ULL, 0xf89629465a75e01dULL, -589}, /* 10^-139 */
    {0xbe89523386091465ULL, 0xf6bbb397f1135824ULL, -586}, /* 10^-138 */
    {0xee2ba6c0678b597fULL, 0x746aa07ded582e2dULL, -583}, /* 10^-137 */
    {0x94db483840b717efULL, 0xa8c2a44eb4571cdcULL, -579}, /* 10^-136 */
    {0xba121a4650e4ddebULL, 0x92f34d62616ce413ULL, -576}, /* 10^-135 */
    {0xe896a0d7e51e1566ULL, 0x77b020baf9c81d18ULL, -573}, /* 10^-134 */
    {0x915e2486ef32cd60ULL, 0x0ace1474dc1d122fULL, -569}, /* 10^-133 */
    {0xb5b5ada8aaff80b8ULL, 0x0d819992132456bbULL, -566}, /* 10^-132 */
    {0xe3231912d5bf60e6ULL, 0x10e1fff697ed6c69ULL, -563}, /* 10^-131 */
    {0x8df5efabc5979c8fULL, 0xca8d3ffa1ef463c2ULL, -559}, /* 10^-130 */
    {0xb1736b96b6fd83b3ULL, 0xbd308ff8a6b17cb2ULL, -556}, /* 10^-129 */
    {0xddd0467c64bce4a0ULL, 0xac7cb3f6d05ddbdfULL, -553}, /* 10^-128 */
    {0x8aa22c0dbef60ee4ULL, 0x6bcdf07a423aa96bULL, -549}, /* 10^-127 */
    {0xad4ab7112eb3929dULL, 0x86c16c98d2c953c6ULL, -546}, /* 10^-126 */
    {0xd89d64d57a607744ULL, 0xe871c7bf077ba8b8ULL, -543}, /* 10^-125 */
    {0x87625f056c7c4a8bULL, 0x11471cd764ad4973ULL, -539}, /* 10^-124 */
    {0xa93af6c6c79b5d2dULL, 0xd598e40d3dd89bcfULL, -536}, /* 10^-123 */
    {0xd389b47879823479ULL, 0x4aff1d108d4ec2c3ULL, -533}, /* 10^-122 */
    {0x843610cb4bf160cbULL, 0xcedf722a585139baULL, -529}, /* 10^-121 */
    {0xa54394fe1eedb8feULL, 0xc2974eb4ee658829ULL, -526}, /* 10^-120 */
    {0xce947a3da6a9273eULL, 0x733d226229feea33ULL, -523}, /* 10^-119 */
    {0x811ccc668829b887ULL, 0x0806357d5a3f5260ULL, -519}, /* 10^-118 */
    {0xa163ff802a3426a8ULL, 0xca07c2dcb0cf26f8ULL, -516}, /* 10^-117 */
    {0xc9bcff6034c13052ULL, 0xfc89b393dd02f0b6ULL, -513}, /* 10^-116 */
    {0xfc2c3f3841f17c67ULL, 0xbbac2078d443ace3ULL, -510}, /* 10^-115 */
    {0x9d9ba7832936edc0ULL, 0xd54b944b84aa4c0eULL, -506}, /* 10^-114 */
    {0xc5029163f384a931ULL, 0x0a9e795e65d4df11ULL, -503}, /* 10^-113 */
    {0xf64335bcf065d37dULL, 0x4d4617b5ff4a16d6ULL, -500}, /* 10^-112 */
    {0x99ea0196163fa42eULL, 0x504bced1bf8e4e46ULL, -496}, /* 10^-111 */
    {0xc06481fb9bcf8d39ULL, 0xe45ec2862f71e1d7ULL, -493}, /* 10^-110 */
    {0xf07da27a82c37088ULL, 0x5d767327bb4e5a4dULL, -490}, /* 10^-109 */
    {0x964e858c91ba2655ULL, 0x3a6a07f8d510f870ULL, -486}, /* 10^-108 */
    {0xbbe226efb628afeaULL, 0x890489f70a55368cULL, -483}, /* 10^-107 */
    {0xeadab0aba3b2dbe5ULL, 0x2b45ac74ccea842fULL, -480}, /* 10^-106 */
    {0x92c8ae6b464fc96fULL, 0x3b0b8bc90012929dULL, -476}, /* 10^-105 */
    {0xb77ada0617e3bbcbULL, 0x09ce6ebb40173745ULL, -473}, /* 10^-104 */
    {0xe55990879ddcaabdULL, 0xcc420a6a101d0516ULL, -470}, /* 10^-103 */
    {0x8f57fa54c2a9eab6ULL, 0x9fa946824a12232eULL, -466}, /* 10^-102 */
    {0xb32df8e9f3546564ULL, 0x47939822dc96abf9ULL, -463}, /* 10^-101 */
    {0xdff9772470297ebdULL, 0x59787e2b93bc56f7ULL, -460}, /* 10^-100 */
    {0x8bfbea76c619ef36ULL, 0x57eb4edb3c55b65bULL, -456}, /* 10^-99 */
    {0xaefae51477a06b03ULL, 0xede622920b6b23f1ULL, -453}, /* 10^-98 */
    {0xdab99e59958885c4ULL, 0xe95fab368e45ecedULL, -450}, /* 10^-97 */
    {0x88b402f7fd75539bULL, 0x11dbcb0218ebb414ULL, -446}, /* 10^-96 */
    {0xaae103b5fcd2a881ULL, 0xd652bdc29f26a11aULL, -443}, /* 10^-95 */
    {0xd59944a37c0752a2ULL, 0x4be76d3346f04960ULL, -440}, /* 10^-94 */
    {0x857fcae62d8493a5ULL, 0x6f70a4400c562ddcULL, -436}, /* 10^-93 */
    {0xa6dfbd9fb8e5b88eULL, 0xcb4ccd500f6bb953ULL, -433}, /* 10^-92 */
    {0xd097ad07a71f26b2ULL, 0x7e2000a41346a7a8ULL, -430}, /* 10^-91 */
    {0x825ecc24c873782fULL, 0x8ed400668c0c28c9ULL, -426}, /* 10^-90 */
    {0xa2f67f2dfa90563bULL, 0x728900802f0f32fbULL, -423}, /* 10^-89 */
    {0xcbb41ef979346bcaULL, 0x4f2b40a03ad2ffbaULL, -420}, /* 10^-88 */
    {0xfea126b7d78186bcULL, 0xe2f610c84987bfa8ULL, -417}, /* 10^-87 */
    {0x9f24b832e6b0f436ULL, 0x0dd9ca7d2df4d7c9ULL, -413}, /* 10^-86 */
    {0xc6ede63fa05d3143ULL, 0x91503d1c79720dbbULL, -410}, /* 10^-85 */
    {0xf8a95fcf88747d94ULL, 0x75a44c6397ce912aULL, -407}, /* 10^-84 */
    {0x9b69dbe1b548ce7cULL, 0xc986afbe3ee11abaULL, -403}, /* 10^-83 */
    {0xc24452da229b021bULL, 0xfbe85badce996169ULL, -400}, /* 10^-82 */
    {0xf2d56790ab41c2a2ULL, 0xfae27299423fb9c3ULL, -397}, /* 10^-81 */
    {0x97c560ba6b0919a5ULL, 0xdccd879fc967d41aULL, -393}, /* 10^-80 */
    {0xbdb6b8e905cb600fULL, 0x5400e987bbc1c921ULL, -390}, /* 10^-79 */
    {0xed246723473e3813ULL, 0x290123e9aab23b69ULL, -387}, /* 10^-78 */
    {0x9436c0760c86e30bULL, 0xf9a0b6720aaf6521ULL, -383}, /* 10^-77 */
    {0xb94470938fa89bceULL, 0xf808e40e8d5b3e6aULL, -380}, /* 10^-76 */
    {0xe7958cb87392c2c2ULL, 0xb60b1d1230b20e04ULL, -377}, /* 10^-75 */
    {0x90bd77f3483bb9b9ULL, 0xb1c6f22b5e6f48c3ULL, -373}, /* 10^-74 */
    {0xb4ecd5f01a4aa828ULL, 0x1e38aeb6360b1af3ULL, -370}, /* 10^-73 */
    {0xe2280b6c20dd5232ULL, 0x25c6da63c38de1b0ULL, -367}, /* 10^-72 */
    {0x8d590723948a535fULL, 0x579c487e5a38ad0eULL, -363}, /* 10^-71 */
    {0xb0af48ec79ace837ULL, 0x2d835a9df0c6d852ULL, -360}, /* 10^-70 */
    {0xdcdb1b2798182244ULL, 0xf8e431456cf88e66ULL, -357}, /* 10^-69 */
    {0x8a08f0f8bf0f156bULL, 0x1b8e9ecb641b5900ULL, -353}, /* 10^-68 */
    {0xac8b2d36eed2dac5ULL, 0xe272467e3d222f40ULL, -350}, /* 10^-67 */
    {0xd7adf884aa879177ULL, 0x5b0ed81dcc6abb10ULL, -347}, /* 10^-66 */
    {0x86ccbb52ea94baeaULL, 0x98e947129fc2b4eaULL, -343}, /* 10^-65 */
    {0xa87fea27a539e9a5ULL, 0x3f2398d747b36224ULL, -340}, /* 10^-64 */
    {0xd29fe4b18e88640eULL, 0x8eec7f0d19a03aadULL, -337}, /* 10^-63 */
    {0x83a3eeeef9153e89ULL, 0x1953cf68300424acULL, -333}, /* 10^-62 */
    {0xa48ceaaab75a8e2bULL, 0x5fa8c3423c052dd7ULL, -330}, /* 10^-61 */
    {0xcdb02555653131b6ULL, 0x3792f412cb06794dULL, -327}, /* 10^-60 */
    {0x808e17555f3ebf11ULL, 0xe2bbd88bbee40bd0ULL, -323}, /* 10^-59 */
    {0xa0b19d2ab70e6ed6ULL, 0x5b6aceaeae9d0ec4ULL, -320}, /* 10^-58 */
    {0xc8de047564d20a8bULL, 0xf245825a5a445275ULL, -317}, /* 10^-57 */
    {0xfb158592be068d2eULL, 0xeed6e2f0f0d56713ULL, -314}, /* 10^-56 */
    {0x9ced737bb6c4183dULL, 0x55464dd69685606cULL, -310}, /* 10^-55 */
    {0xc428d05aa4751e4cULL, 0xaa97e14c3c26b887ULL, -307}, /* 10^-54 */
    {0xf53304714d9265dfULL, 0xd53dd99f4b3066a8ULL, -304}, /* 10^-53 */
    {0x993fe2c6d07b7fabULL, 0xe546a8038efe4029ULL, -300}, /* 10^-52 */
    {0xbf8fdb78849a5f96ULL, 0xde98520472bdd033ULL, -297}, /* 10^-51 */
    {0xef73d256a5c0f77cULL, 0x963e66858f6d4440ULL, -294}, /* 10^-50 */
    {0x95a8637627989aadULL, 0xdde7001379a44aa8ULL, -290}, /* 10^-49 */
    {0xbb127c53b17ec159ULL, 0x5560c018580d5d52ULL, -287}, /* 10^-48 */
    {0xe9d71b689dde71afULL, 0xaab8f01e6e10b4a7ULL, -284}, /* 10^-47 */
    {0x9226712162ab070dULL, 0xcab3961304ca70e8ULL, -280}, /* 10^-46 */
    {0xb6b00d69bb55c8d1ULL, 0x3d607b97c5fd0d22ULL, -277}, /* 10^-45 */
    {0xe45c10c42a2b3b05ULL, 0x8cb89a7db77c506bULL, -274}, /* 10^-44 */
    {0x8eb98a7a9a5b04e3ULL, 0x77f3608e92adb243ULL, -270}, /* 10^-43 */
    {0xb267ed1940f1c61cULL, 0x55f038b237591ed3ULL, -267}, /* 10^-42 */
    {0xdf01e85f912e37a3ULL, 0x6b6c46dec52f6688ULL, -264}, /* 10^-41 */
    {0x8b61313bbabce2c6ULL, 0x2323ac4b3b3da015ULL, -260}, /* 10^-40 */
    {0xae397d8aa96c1b77ULL, 0xabec975e0a0d081bULL, -257}, /* 10^-39 */
    {0xd9c7dced53c72255ULL, 0x96e7bd358c904a21ULL, -254}, /* 10^-38 */
    {0x881cea14545c7575ULL, 0x7e50d64177da2e55ULL, -250}, /* 10^-37 */
    {0xaa242499697392d2ULL, 0xdde50bd1d5d0b9eaULL, -247}, /* 10^-36 */
    {0xd4ad2dbfc3d07787ULL, 0x955e4ec64b44e864ULL, -244}, /* 10^-35 */
    {0x84ec3c97da624ab4ULL, 0xbd5af13bef0b113fULL, -240}, /* 10^-34 */
    {0xa6274bbdd0fadd61ULL, 0xecb1ad8aeacdd58eULL, -237}, /* 10^-33 */
    {0xcfb11ead453994baULL, 0x67de18eda5814af2ULL, -234}, /* 10^-32 */
    {0x81ceb32c4b43fcf4ULL, 0x80eacf948770ced7ULL, -230}, /* 10^-31 */
    {0xa2425ff75e14fc31ULL, 0xa1258379a94d028dULL, -227}, /* 10^-30 */
    {0xcad2f7f5359a3b3eULL, 0x096ee45813a04330ULL, -224}, /* 10^-29 */
    {0xfd87b5f28300ca0dULL, 0x8bca9d6e188853fcULL, -221}, /* 10^-28 */
    {0x9e74d1b791e07e48ULL, 0x775ea264cf55347eULL, -217}, /* 10^-27 */
    {0xc612062576589ddaULL, 0x95364afe032a819dULL, -214}, /* 10^-26 */
    {0xf79687aed3eec551ULL, 0x3a83ddbd83f52205ULL, -211}, /* 10^-25 */
    {0x9abe14cd44753b52ULL, 0xc4926a9672793543ULL, -207}, /* 10^-24 */
    {0xc16d9a0095928a27ULL, 0x75b7053c0f178294ULL, -204}, /* 10^-23 */
    {0xf1c90080baf72cb1ULL, 0x5324c68b12dd6338ULL, -201}, /* 10^-22 */
    {0x971da05074da7beeULL, 0xd3f6fc16ebca5e03ULL, -197}, /* 10^-21 */
    {0xbce5086492111aeaULL, 0x88f4bb1ca6bcf584ULL, -194}, /* 10^-20 */
    {0xec1e4a7db69561a5ULL, 0x2b31e9e3d06c32e5ULL, -191}, /* 10^-19 */
    {0x9392ee8e921d5d07ULL, 0x3aff322e62439fcfULL, -187}, /* 10^-18 */
    {0xb877aa3236a4b449ULL, 0x09befeb9fad487c3ULL, -184}, /* 10^-17 */
    {0xe69594bec44de15bULL, 0x4c2ebe687989a9b4ULL, -181}, /* 10^-16 */
    {0x901d7cf73ab0acd9ULL, 0x0f9d37014bf60a10ULL, -177}, /* 10^-15 */
    {0xb424dc35095cd80fULL, 0x538484c19ef38c94ULL, -174}, /* 10^-14 */
    {0xe12e13424bb40e13ULL, 0x2865a5f206b06fbaULL, -171}, /* 10^-13 */
    {0x8cbccc096f5088cbULL, 0xf93f87b7442e45d4ULL, -167}, /* 10^-12 */
    {0xafebff0bcb24aafeULL, 0xf78f69a51539d749ULL, -164}, /* 10^-11 */
    {0xdbe6fecebdedd5beULL, 0xb573440e5a884d1bULL, -161}, /* 10^-10 */
    {0x89705f4136b4a597ULL, 0x31680a88f8953031ULL, -157}, /* 10^-9 */
    {0xabcc77118461cefcULL, 0xfdc20d2b36ba7c3dULL, -154}, /* 10^-8 */
    {0xd6bf94d5e57a42bcULL, 0x3d32907604691b4dULL, -151}, /* 10^-7 */
    {0x8637bd05af6c69b5ULL, 0xa63f9a49c2c1b110ULL, -147}, /* 10^-6 */
    {0xa7c5ac471b478423ULL, 0x0fcf80dc33721d54ULL, -144}, /* 10^-5 */
    {0xd1b71758e219652bULL, 0xd3c36113404ea4a9ULL, -141}, /* 10^-4 */
    {0x83126e978d4fdf3bULL, 0x645a1cac083126e9ULL, -137}, /* 10^-3 */
    {0xa3d70a3d70a3d70aULL, 0x3d70a3d70a3d70a4ULL, -134}, /* 10^-2 */
    {0xccccccccccccccccULL, 0xcccccccccccccccdULL, -131}, /* 10^-1 */
    {0x8000000000000000ULL, 0x0000000000000000ULL, -127}, /* 10^0 */
    {0xa000000000000000ULL, 0x0000000000000000ULL, -124}, /* 10^1 */
    {0xc800000000000000ULL, 0x0000000000000000ULL, -121}, /* 10^2 */
    {0xfa00000000000000ULL, 0x0000000000000000ULL, -118}, /* 10^3 */
    {0x9c40000000000000ULL, 0x0000000000000000ULL, -114}, /* 10^4 */
    {0xc350000000000000ULL, 0x0000000000000000ULL, -111}, /* 10^5 */
    {0xf424000000000000ULL, 0x0000000000000000ULL, -108}, /* 10^6 */
    {0x9896800000000000ULL, 0x0000000000000000ULL, -104}, /* 10^7 */
    {0xbebc200000000000ULL, 0x0000000000000000ULL, -101}, /* 10^8 */
    {0xee6b280000000000ULL, 0x0000000000000000ULL, -98}, /* 10^9 */
    {0x9502f90000000000ULL, 0x0000000000000000ULL, -94}, /* 10^10 */
    {0xba43b74000000000ULL, 0x0000000000000000ULL, -91}, /* 10^11 */
    {0xe8d4a51000000000ULL, 0x0000000000000000ULL, -88}, /* 10^12 */
    {0x9184e72a00000000ULL, 0x0000000000000000ULL, -84}, /* 10^13 */
    {0xb5e620f480000000ULL, 0x0000000000000000ULL, -81}, /* 10^14 */
    {0xe35fa931a0000000ULL, 0x0000000000000000ULL, -78}, /* 10^15 */
    {0x8e1bc9bf04000000ULL, 0x0000000000000000ULL, -74}, /* 10^16 */
    {0xb1a2bc2ec5000000ULL, 0x0000000000000000ULL, -71}, /* 10^17 */
    {0xde0b6b3a76400000ULL, 0x0000000000000000ULL, -68}, /* 10^18 */
    {0x8ac7230489e80000ULL, 0x0000000000000000ULL, -64}, /* 10^19 */
    {0xad78ebc5ac620000ULL, 0x0000000000000000ULL, -61}, /* 10^20 */
    {0xd8d726b7177a8000ULL, 0x0000000000000000ULL, -58}, /* 10^21 */
    {0x878678326eac9000ULL, 0x0000000000000000ULL, -54}, /* 10^22 */
    {0xa968163f0a57b400ULL, 0x0000000000000000ULL, -51}, /* 10^23 */
    {0xd3c21bcecceda100ULL, 0x0000000000000000ULL, -48}, /* 10^24 */
    {0x84595161401484a0ULL, 0x0000000000000000ULL, -44}, /* 10^25 */
    {0xa56fa5b99019a5c8ULL, 0x0000000000000000ULL, -41}, /* 10^26 */
    {0xcecb8f27f4200f3aULL, 0x0000000000000000ULL, -38}, /* 10^27 */
    {0x813f3978f8940984ULL, 0x4000000000000000ULL, -34}, /* 10^28 */
    {0xa18f07d736b90be5ULL, 0x5000000000000000ULL, -31}, /* 10^29 */
    {0xc9f2c9cd04674edeULL, 0xa400000000000000ULL, -28}, /* 10^30 */
    {0xfc6f7c4045812296ULL, 0x4d00000000000000ULL, -25}, /* 10^31 */
    {0x9dc5ada82b70b59dULL, 0xf020000000000000ULL, -21}, /* 10^32 */
    {0xc5371912364ce305ULL, 0x6c28000000000000ULL, -18}, /* 10^33 */
    {0xf684df56c3e01bc6ULL, 0xc732000000000000ULL, -15}, /* 10^34 */
    {0x9a130b963a6c115cULL, 0x3c7f400000000000ULL, -11}, /* 10^35 */
    {0xc097ce7bc90715b3ULL, 0x4b9f100000000000ULL, -8}, /* 10^36 */
    {0xf0bdc21abb48db20ULL, 0x1e86d40000000000ULL, -5}, /* 10^37 */
    {0x96769950b50d88f4ULL, 0x1314448000000000ULL, -1}, /* 10^38 */
    {0xbc143fa4e250eb31ULL, 0x17d955a000000000ULL, 2}, /* 10^39 */
    {0xeb194f8e1ae525fdULL, 0x5dcfab0800000000ULL, 5}, /* 10^40 */
    {0x92efd1b8d0cf37beULL, 0x5aa1cae500000000ULL, 9}, /* 10^41 */
    {0xb7abc627050305adULL, 0xf14a3d9e40000000ULL, 12}, /* 10^42 */
    {0xe596b7b0c643c719ULL, 0x6d9ccd05d0000000ULL, 15}, /* 10^43 */
    {0x8f7e32ce7bea5c6fULL, 0xe4820023a2000000ULL, 19}, /* 10^44 */
    {0xb35dbf821ae4f38bULL, 0xdda2802c8a800000ULL, 22}, /* 10^45 */
    {0xe0352f62a19e306eULL, 0xd50b2037ad200000ULL, 25}, /* 10^46 */
    {0x8c213d9da502de45ULL, 0x4526f422cc340000ULL, 29}, /* 10^47 */
    {0xaf298d050e4395d6ULL, 0x9670b12b7f410000ULL, 32}, /* 10^48 */
    {0xdaf3f04651d47b4cULL, 0x3c0cdd765f114000ULL, 35}, /* 10^49 */
    {0x88d8762bf324cd0fULL, 0xa5880a69fb6ac800ULL, 39}, /* 10^50 */
    {0xab0e93b6efee0053ULL, 0x8eea0d047a457a00ULL, 42}, /* 10^51 */
    {0xd5d238a4abe98068ULL, 0x72a4904598d6d880ULL, 45}, /* 10^52 */
    {0x85a36366eb71f041ULL, 0x47a6da2b7f864750ULL, 49}, /* 10^53 */
    {0xa70c3c40a64e6c51ULL, 0x999090b65f67d924ULL, 52}, /* 10^54 */
    {0xd0cf4b50cfe20765ULL, 0xfff4b4e3f741cf6dULL, 55}, /* 10^55 */
    {0x82818f1281ed449fULL, 0xbff8f10e7a8921a4ULL, 59}, /* 10^56 */
    {0xa321f2d7226895c7ULL, 0xaff72d52192b6a0dULL, 62}, /* 10^57 */
    {0xcbea6f8ceb02bb39ULL, 0x9bf4f8a69f764490ULL, 65}, /* 10^58 */
    {0xfee50b7025c36a08ULL, 0x02f236d04753d5b5ULL, 68}, /* 10^59 */
    {0x9f4f2726179a2245ULL, 0x01d762422c946591ULL, 72}, /* 10^60 */
    {0xc722f0ef9d80aad6ULL, 0x424d3ad2b7b97ef5ULL, 75}, /* 10^61 */
    {0xf8ebad2b84e0d58bULL, 0xd2e0898765a7deb2ULL, 78}, /* 10^62 */
    {0x9b934c3b330c8577ULL, 0x63cc55f49f88eb2fULL, 82}, /* 10^63 */
    {0xc2781f49ffcfa6d5ULL, 0x3cbf6b71c76b25fbULL, 85}, /* 10^64 */
    {0xf316271c7fc3908aULL, 0x8bef464e3945ef7aULL, 88}, /* 10^65 */
    {0x97edd871cfda3a56ULL, 0x97758bf0e3cbb5acULL, 92}, /* 10^66 */
    {0xbde94e8e43d0c8ecULL, 0x3d52eeed1cbea317ULL, 95}, /* 10^67 */
    {0xed63a231d4c4fb27ULL, 0x4ca7aaa863ee4bddULL, 98}, /* 10^68 */
    {0x945e455f24fb1cf8ULL, 0x8fe8caa93e74ef6aULL, 102}, /* 10^69 */
    {0xb975d6b6ee39e436ULL, 0xb3e2fd538e122b45ULL, 105}, /* 10^70 */
    {0xe7d34c64a9c85d44ULL, 0x60dbbca87196b616ULL, 108}, /* 10^71 */
    {0x90e40fbeea1d3a4aULL, 0xbc8955e946fe31ceULL, 112}, /* 10^72 */
    {0xb51d13aea4a488ddULL, 0x6babab6398bdbe41ULL, 115}, /* 10^73 */
    {0xe264589a4dcdab14ULL, 0xc696963c7eed2dd2ULL, 118}, /* 10^74 */
    {0x8d7eb76070a08aecULL, 0xfc1e1de5cf543ca3ULL, 122}, /* 10^75 */
    {0xb0de65388cc8ada8ULL, 0x3b25a55f43294bccULL, 125}, /* 10^76 */
    {0xdd15fe86affad912ULL, 0x49ef0eb713f39ebfULL, 128}, /* 10^77 */
    {0x8a2dbf142dfcc7abULL, 0x6e3569326c784337ULL, 132}, /* 10^78 */
    {0xacb92ed9397bf996ULL, 0x49c2c37f07965405ULL, 135}, /* 10^79 */
    {0xd7e77a8f87daf7fbULL, 0xdc33745ec97be906ULL, 138}, /* 10^80 */
    {0x86f0ac99b4e8dafdULL, 0x69a028bb3ded71a4ULL, 142}, /* 10^81 */
    {0xa8acd7c0222311bcULL, 0xc40832ea0d68ce0dULL, 145}, /* 10^82 */
    {0xd2d80db02aabd62bULL, 0xf50a3fa490c30190ULL, 148}, /* 10^83 */
    {0x83c7088e1aab65dbULL, 0x792667c6da79e0faULL, 152}, /* 10^84 */
    {0xa4b8cab1a1563f52ULL, 0x577001b891185939ULL, 155}, /* 10^85 */
    {0xcde6fd5e09abcf26ULL, 0xed4c0226b55e6f87ULL, 158}, /* 10^86 */
    {0x80b05e5ac60b6178ULL, 0x544f8158315b05b4ULL, 162}, /* 10^87 */
    {0xa0dc75f1778e39d6ULL, 0x696361ae3db1c721ULL, 165}, /* 10^88 */
    {0xc913936dd571c84cULL, 0x03bc3a19cd1e38eaULL, 168}, /* 10^89 */
    {0xfb5878494ace3a5fULL, 0x04ab48a04065c724ULL, 171}, /* 10^90 */
    {0x9d174b2dcec0e47bULL, 0x62eb0d64283f9c76ULL, 175}, /* 10^91 */
    {0xc45d1df942711d9aULL, 0x3ba5d0bd324f8394ULL, 178}, /* 10^92 */
    {0xf5746577930d6500ULL, 0xca8f44ec7ee36479ULL, 181}, /* 10^93 */
    {0x9968bf6abbe85f20ULL, 0x7e998b13cf4e1eccULL, 185}, /* 10^94 */
    {0xbfc2ef456ae276e8ULL, 0x9e3fedd8c321a67fULL, 188}, /* 10^95 */
    {0xefb3ab16c59b14a2ULL, 0xc5cfe94ef3ea101eULL, 191}, /* 10^96 */
    {0x95d04aee3b80ece5ULL, 0xbba1f1d158724a13ULL, 195}, /* 10^97 */
    {0xbb445da9ca61281fULL, 0x2a8a6e45ae8edc98ULL, 198}, /* 10^98 */
    {0xea1575143cf97226ULL, 0xf52d09d71a3293beULL, 201}, /* 10^99 */
    {0x924d692ca61be758ULL, 0x593c2626705f9c56ULL, 205}, /* 10^100 */
    {0xb6e0c377cfa2e12eULL, 0x6f8b2fb00c77836cULL, 208}, /* 10^101 */
    {0xe498f455c38b997aULL, 0x0b6dfb9c0f956447ULL, 211}, /* 10^102 */
    {0x8edf98b59a373fecULL, 0x4724bd4189bd5eacULL, 215}, /* 10^103 */
    {0xb2977ee300c50fe7ULL, 0x58edec91ec2cb658ULL, 218}, /* 10^104 */
    {0xdf3d5e9bc0f653e1ULL, 0x2f2967b66737e3edULL, 221}, /* 10^105 */
    {0x8b865b215899f46cULL, 0xbd79e0d20082ee74ULL, 225}, /* 10^106 */
    {0xae67f1e9aec07187ULL, 0xecd8590680a3aa11ULL, 228}, /* 10^107 */
    {0xda01ee641a708de9ULL, 0xe80e6f4820cc9496ULL, 231}, /* 10^108 */
    {0x884134fe908658b2ULL, 0x3109058d147fdcdeULL, 235}, /* 10^109 */
    {0xaa51823e34a7eedeULL, 0xbd4b46f0599fd415ULL, 238}, /* 10^110 */
    {0xd4e5e2cdc1d1ea96ULL, 0x6c9e18ac7007c91aULL, 241}, /* 10^111 */
    {0x850fadc09923329eULL, 0x03e2cf6bc604ddb0ULL, 245}, /* 10^112 */
    {0xa6539930bf6bff45ULL, 0x84db8346b786151dULL, 248}, /* 10^113 */
    {0xcfe87f7cef46ff16ULL, 0xe612641865679a64ULL, 251}, /* 10^114 */
    {0x81f14fae158c5f6eULL, 0x4fcb7e8f3f60c07eULL, 255}, /* 10^115 */
    {0xa26da3999aef7749ULL, 0xe3be5e330f38f09eULL, 258}, /* 10^116 */
    {0xcb090c8001ab551cULL, 0x5cadf5bfd3072cc5ULL, 261}, /* 10^117 */
    {0xfdcb4fa002162a63ULL, 0x73d9732fc7c8f7f7ULL, 264}, /* 10^118 */
    {0x9e9f11c4014dda7eULL, 0x2867e7fddcdd9afaULL, 268}, /* 10^119 */
    {0xc646d63501a1511dULL, 0xb281e1fd541501b9ULL, 271}, /* 10^120 */
    {0xf7d88bc24209a565ULL, 0x1f225a7ca91a4227ULL, 274}, /* 10^121 */
    {0x9ae757596946075fULL, 0x3375788de9b06958ULL, 278}, /* 10^122 */
    {0xc1a12d2fc3978937ULL, 0x0052d6b1641c83aeULL, 281}, /* 10^123 */
    {0xf209787bb47d6b84ULL, 0xc0678c5dbd23a49aULL, 284}, /* 10^124 */
    {0x9745eb4d50ce6332ULL, 0xf840b7ba963646e0ULL, 288}, /* 10^125 */
    {0xbd176620a501fbffULL, 0xb650e5a93bc3d898ULL, 291}, /* 10^126 */
    {0xec5d3fa8ce427affULL, 0xa3e51f138ab4cebeULL, 294}, /* 10^127 */
    {0x93ba47c980e98cdfULL, 0xc66f336c36b10137ULL, 298}, /* 10^128 */
    {0xb8a8d9bbe123f017ULL, 0xb80b0047445d4185ULL, 301}, /* 10^129 */
    {0xe6d3102ad96cec1dULL, 0xa60dc059157491e6ULL, 304}, /* 10^130 */
    {0x9043ea1ac7e41392ULL, 0x87c89837ad68db30ULL, 308}, /* 10^131 */
    {0xb454e4a179dd1877ULL, 0x29babe4598c311fcULL, 311}, /* 10^132 */
    {0xe16a1dc9d8545e94ULL, 0xf4296dd6fef3d67bULL, 314}, /* 10^133 */
    {0x8ce2529e2734bb1dULL, 0x1899e4a65f58660dULL, 318}, /* 10^134 */
    {0xb01ae745b101e9e4ULL, 0x5ec05dcff72e7f90ULL, 321}, /* 10^135 */
    {0xdc21a1171d42645dULL, 0x76707543f4fa1f74ULL, 324}, /* 10^136 */
    {0x899504ae72497ebaULL, 0x6a06494a791c53a8ULL, 328}, /* 10^137 */
    {0xabfa45da0edbde69ULL, 0x0487db9d17636892ULL, 331}, /* 10^138 */
    {0xd6f8d7509292d603ULL, 0x45a9d2845d3c42b7ULL, 334}, /* 10^139 */
    {0x865b86925b9bc5c2ULL, 0x0b8a2392ba45a9b2ULL, 338}, /* 10^140 */
    {0xa7f26836f282b732ULL, 0x8e6cac7768d7141fULL, 341}, /* 10^141 */
    {0xd1ef0244af2364ffULL, 0x3207d795430cd927ULL, 344}, /* 10^142 */
    {0x8335616aed761f1fULL, 0x7f44e6bd49e807b8ULL, 348}, /* 10^143 */
    {0xa402b9c5a8d3a6e7ULL, 0x5f16206c9c6209a6ULL, 351}, /* 10^144 */
    {0xcd036837130890a1ULL, 0x36dba887c37a8c10ULL, 354}, /* 10^145 */
    {0x802221226be55a64ULL, 0xc2494954da2c978aULL, 358}, /* 10^146 */
    {0xa02aa96b06deb0fdULL, 0xf2db9baa10b7bd6cULL, 361}, /* 10^147 */
    {0xc83553c5c8965d3dULL, 0x6f92829494e5acc7ULL, 364}, /* 10^148 */
    {0xfa42a8b73abbf48cULL, 0xcb772339ba1f17f9ULL, 367}, /* 10^149 */
    {0x9c69a97284b578d7ULL, 0xff2a760414536efcULL, 371}, /* 10^150 */
    {0xc38413cf25e2d70dULL, 0xfef5138519684abbULL, 374}, /* 10^151 */
    {0xf46518c2ef5b8cd1ULL, 0x7eb258665fc25d69ULL, 377}, /* 10^152 */
    {0x98bf2f79d5993802ULL, 0xef2f773ffbd97a62ULL, 381}, /* 10^153 */
    {0xbeeefb584aff8603ULL, 0xaafb550ffacfd8faULL, 384}, /* 10^154 */
    {0xeeaaba2e5dbf6784ULL, 0x95ba2a53f983cf39ULL, 387}, /* 10^155 */
    {0x952ab45cfa97a0b2ULL, 0xdd945a747bf26184ULL, 391}, /* 10^156 */
    {0xba756174393d88dfULL, 0x94f971119aeef9e4ULL, 394}, /* 10^157 */
    {0xe912b9d1478ceb17ULL, 0x7a37cd5601aab85eULL, 397}, /* 10^158 */
    {0x91abb422ccb812eeULL, 0xac62e055c10ab33bULL, 401}, /* 10^159 */
    {0xb616a12b7fe617aaULL, 0x577b986b314d6009ULL, 404}, /* 10^160 */
    {0xe39c49765fdf9d94ULL, 0xed5a7e85fda0b80bULL, 407}, /* 10^161 */
    {0x8e41ade9fbebc27dULL, 0x14588f13be847307ULL, 411}, /* 10^162 */
    {0xb1d219647ae6b31cULL, 0x596eb2d8ae258fc9ULL, 414}, /* 10^163 */
    {0xde469fbd99a05fe3ULL, 0x6fca5f8ed9aef3bbULL, 417}, /* 10^164 */
    {0x8aec23d680043beeULL, 0x25de7bb9480d5855ULL, 421}, /* 10^165 */
    {0xada72ccc20054ae9ULL, 0xaf561aa79a10ae6aULL, 424}, /* 10^166 */
    {0xd910f7ff28069da4ULL, 0x1b2ba1518094da05ULL, 427}, /* 10^167 */
    {0x87aa9aff79042286ULL, 0x90fb44d2f05d0843ULL, 431}, /* 10^168 */
    {0xa99541bf57452b28ULL, 0x353a1607ac744a54ULL, 434}, /* 10^169 */
    {0xd3fa922f2d1675f2ULL, 0x42889b8997915ce9ULL, 437}, /* 10^170 */
    {0x847c9b5d7c2e09b7ULL, 0x69956135febada11ULL, 441}, /* 10^171 */
    {0xa59bc234db398c25ULL, 0x43fab9837e699096ULL, 444}, /* 10^172 */
    {0xcf02b2c21207ef2eULL, 0x94f967e45e03f4bbULL, 447}, /* 10^173 */
    {0x8161afb94b44f57dULL, 0x1d1be0eebac278f5ULL, 451}, /* 10^174 */
    {0xa1ba1ba79e1632dcULL, 0x6462d92a69731732ULL, 454}, /* 10^175 */
    {0xca28a291859bbf93ULL, 0x7d7b8f7503cfdcffULL, 457}, /* 10^176 */
    {0xfcb2cb35e702af78ULL, 0x5cda735244c3d43fULL, 460}, /* 10^177 */
    {0x9defbf01b061adabULL, 0x3a0888136afa64a7ULL, 464}, /* 10^178 */
    {0xc56baec21c7a1916ULL, 0x088aaa1845b8fdd1ULL, 467}, /* 10^179 */
    {0xf6c69a72a3989f5bULL, 0x8aad549e57273d45ULL, 470}, /* 10^180 */
    {0x9a3c2087a63f6399ULL, 0x36ac54e2f678864bULL, 474}, /* 10^181 */
    {0xc0cb28a98fcf3c7fULL, 0x84576a1bb416a7deULL, 477}, /* 10^182 */
    {0xf0fdf2d3f3c30b9fULL, 0x656d44a2a11c51d5ULL, 480}, /* 10^183 */
    {0x969eb7c47859e743ULL, 0x9f644ae5a4b1b325ULL, 484}, /* 10^184 */
    {0xbc4665b596706114ULL, 0x873d5d9f0dde1fefULL, 487}, /* 10^185 */
    {0xeb57ff22fc0c7959ULL, 0xa90cb506d155a7eaULL, 490}, /* 10^186 */
    {0x9316ff75dd87cbd8ULL, 0x09a7f12442d588f3ULL, 494}, /* 10^187 */
    {0xb7dcbf5354e9beceULL, 0x0c11ed6d538aeb2fULL, 497}, /* 10^188 */
    {0xe5d3ef282a242e81ULL, 0x8f1668c8a86da5fbULL, 500}, /* 10^189 */
    {0x8fa475791a569d10ULL, 0xf96e017d694487bdULL, 504}, /* 10^190 */
    {0xb38d92d760ec4455ULL, 0x37c981dcc395a9acULL, 507}, /* 10^191 */
    {0xe070f78d3927556aULL, 0x85bbe253f47b1417ULL, 510}, /* 10^192 */
    {0x8c469ab843b89562ULL, 0x93956d7478ccec8eULL, 514}, /* 10^193 */
    {0xaf58416654a6babbULL, 0x387ac8d1970027b2ULL, 517}, /* 10^194 */
    {0xdb2e51bfe9d0696aULL, 0x06997b05fcc0319fULL, 520}, /* 10^195 */
    {0x88fcf317f22241e2ULL, 0x441fece3bdf81f03ULL, 524}, /* 10^196 */
    {0xab3c2fddeeaad25aULL, 0xd527e81cad7626c4ULL, 527}, /* 10^197 */
    {0xd60b3bd56a5586f1ULL, 0x8a71e223d8d3b075ULL, 530}, /* 10^198 */
    {0x85c7056562757456ULL, 0xf6872d5667844e49ULL, 534}, /* 10^199 */
    {0xa738c6bebb12d16cULL, 0xb428f8ac016561dbULL, 537}, /* 10^200 */
    {0xd106f86e69d785c7ULL, 0xe13336d701beba52ULL, 540}, /* 10^201 */
    {0x82a45b450226b39cULL, 0xecc0024661173473ULL, 544}, /* 10^202 */
    {0xa34d721642b06084ULL, 0x27f002d7f95d0190ULL, 547}, /* 10^203 */
    {0xcc20ce9bd35c78a5ULL, 0x31ec038df7b441f4ULL, 550}, /* 10^204 */
    {0xff290242c83396ceULL, 0x7e67047175a15271ULL, 553}, /* 10^205 */
    {0x9f79a169bd203e41ULL, 0x0f0062c6e984d387ULL, 557}, /* 10^206 */
    {0xc75809c42c684dd1ULL, 0x52c07b78a3e60868ULL, 560}, /* 10^207 */
    {0xf92e0c3537826145ULL, 0xa7709a56ccdf8a83ULL, 563}, /* 10^208 */
    {0x9bbcc7a142b17ccbULL, 0x88a66076400bb692ULL, 567}, /* 10^209 */
    {0xc2abf989935ddbfeULL, 0x6acff893d00ea436ULL, 570}, /* 10^210 */
    {0xf356f7ebf83552feULL, 0x0583f6b8c4124d43ULL, 573}, /* 10^211 */
    {0x98165af37b2153deULL, 0xc3727a337a8b704aULL, 577}, /* 10^212 */
    {0xbe1bf1b059e9a8d6ULL, 0x744f18c0592e4c5dULL, 580}, /* 10^213 */
    {0xeda2ee1c7064130cULL, 0x1162def06f79df74ULL, 583}, /* 10^214 */
    {0x9485d4d1c63e8be7ULL, 0x8addcb5645ac2ba8ULL, 587}, /* 10^215 */
    {0xb9a74a0637ce2ee1ULL, 0x6d953e2bd7173693ULL, 590}, /* 10^216 */
    {0xe8111c87c5c1ba99ULL, 0xc8fa8db6ccdd0437ULL, 593}, /* 10^217 */
    {0x910ab1d4db9914a0ULL, 0x1d9c9892400a22a2ULL, 597}, /* 10^218 */
    {0xb54d5e4a127f59c8ULL, 0x2503beb6d00cab4bULL, 600}, /* 10^219 */
    {0xe2a0b5dc971f303aULL, 0x2e44ae64840fd61eULL, 603}, /* 10^220 */
    {0x8da471a9de737e24ULL, 0x5ceaecfed289e5d3ULL, 607}, /* 10^221 */
    {0xb10d8e1456105dadULL, 0x7425a83e872c5f47ULL, 610}, /* 10^222 */
    {0xdd50f1996b947518ULL, 0xd12f124e28f77719ULL, 613}, /* 10^223 */
    {0x8a5296ffe33cc92fULL, 0x82bd6b70d99aaa70ULL, 617}, /* 10^224 */
    {0xace73cbfdc0bfb7bULL, 0x636cc64d1001550cULL, 620}, /* 10^225 */
    {0xd8210befd30efa5aULL, 0x3c47f7e05401aa4fULL, 623}, /* 10^226 */
    {0x8714a775e3e95c78ULL, 0x65acfaec34810a71ULL, 627}, /* 10^227 */
    {0xa8d9d1535ce3b396ULL, 0x7f1839a741a14d0dULL, 630}, /* 10^228 */
    {0xd31045a8341ca07cULL, 0x1ede48111209a051ULL, 633}, /* 10^229 */
    {0x83ea2b892091e44dULL, 0x934aed0aab460432ULL, 637}, /* 10^230 */
    {0xa4e4b66b68b65d60ULL, 0xf81da84d5617853fULL, 640}, /* 10^231 */
    {0xce1de40642e3f4b9ULL, 0x36251260ab9d668fULL, 643}, /* 10^232 */
    {0x80d2ae83e9ce78f3ULL, 0xc1d72b7c6b426019ULL, 647}, /* 10^233 */
    {0xa1075a24e4421730ULL, 0xb24cf65b8612f820ULL, 650}, /* 10^234 */
    {0xc94930ae1d529cfcULL, 0xdee033f26797b628ULL, 653}, /* 10^235 */
    {0xfb9b7cd9a4a7443cULL, 0x169840ef017da3b1ULL, 656}, /* 10^236 */
    {0x9d412e0806e88aa5ULL, 0x8e1f289560ee864fULL, 660}, /* 10^237 */
    {0xc491798a08a2ad4eULL, 0xf1a6f2bab92a27e3ULL, 663}, /* 10^238 */
    {0xf5b5d7ec8acb58a2ULL, 0xae10af696774b1dbULL, 666}, /* 10^239 */
    {0x9991a6f3d6bf1765ULL, 0xacca6da1e0a8ef29ULL, 670}, /* 10^240 */
    {0xbff610b0cc6edd3fULL, 0x17fd090a58d32af3ULL, 673}, /* 10^241 */
    {0xeff394dcff8a948eULL, 0xddfc4b4cef07f5b0ULL, 676}, /* 10^242 */
    {0x95f83d0a1fb69cd9ULL, 0x4abdaf101564f98eULL, 680}, /* 10^243 */
    {0xbb764c4ca7a4440fULL, 0x9d6d1ad41abe37f2ULL, 683}, /* 10^244 */
    {0xea53df5fd18d5513ULL, 0x84c86189216dc5eeULL, 686}, /* 10^245 */
    {0x92746b9be2f8552cULL, 0x32fd3cf5b4e49bb5ULL, 690}, /* 10^246 */
    {0xb7118682dbb66a77ULL, 0x3fbc8c33221dc2a2ULL, 693}, /* 10^247 */
    {0xe4d5e82392a40515ULL, 0x0fabaf3feaa5334aULL, 696}, /* 10^248 */
    {0x8f05b1163ba6832dULL, 0x29cb4d87f2a7400eULL, 700}, /* 10^249 */
    {0xb2c71d5bca9023f8ULL, 0x743e20e9ef511012ULL, 703}, /* 10^250 */
    {0xdf78e4b2bd342cf6ULL, 0x914da9246b255417ULL, 706}, /* 10^251 */
    {0x8bab8eefb6409c1aULL, 0x1ad089b6c2f7548eULL, 710}, /* 10^252 */
    {0xae9672aba3d0c320ULL, 0xa184ac2473b529b2ULL, 713}, /* 10^253 */
    {0xda3c0f568cc4f3e8ULL, 0xc9e5d72d90a2741eULL, 716}, /* 10^254 */
    {0x8865899617fb1871ULL, 0x7e2fa67c7a658893ULL, 720}, /* 10^255 */
    {0xaa7eebfb9df9de8dULL, 0xddbb901b98feeab8ULL, 723}, /* 10^256 */
    {0xd51ea6fa85785631ULL, 0x552a74227f3ea565ULL, 726}, /* 10^257 */
    {0x8533285c936b35deULL, 0xd53a88958f87275fULL, 730}, /* 10^258 */
    {0xa67ff273b8460356ULL, 0x8a892abaf368f137ULL, 733}, /* 10^259 */
    {0xd01fef10a657842cULL, 0x2d2b7569b0432d85ULL, 736}, /* 10^260 */
    {0x8213f56a67f6b29bULL, 0x9c3b29620e29fc73ULL, 740}, /* 10^261 */
    {0xa298f2c501f45f42ULL, 0x8349f3ba91b47b90ULL, 743}, /* 10^262 */
    {0xcb3f2f7642717713ULL, 0x241c70a936219a74ULL, 746}, /* 10^263 */
    {0xfe0efb53d30dd4d7ULL, 0xed238cd383aa0111ULL, 749}, /* 10^264 */
    {0x9ec95d1463e8a506ULL, 0xf4363804324a40abULL, 753}, /* 10^265 */
    {0xc67bb4597ce2ce48ULL, 0xb143c6053edcd0d5ULL, 756}, /* 10^266 */
    {0xf81aa16fdc1b81daULL, 0xdd94b7868e94050aULL, 759}, /* 10^267 */
    {0x9b10a4e5e9913128ULL, 0xca7cf2b4191c8327ULL, 763}, /* 10^268 */
    {0xc1d4ce1f63f57d72ULL, 0xfd1c2f611f63a3f0ULL, 766}, /* 10^269 */
    {0xf24a01a73cf2dccfULL, 0xbc633b39673c8cecULL, 769}, /* 10^270 */
    {0x976e41088617ca01ULL, 0xd5be0503e085d814ULL, 773}, /* 10^271 */
    {0xbd49d14aa79dbc82ULL, 0x4b2d8644d8a74e19ULL, 776}, /* 10^272 */
    {0xec9c459d51852ba2ULL, 0xddf8e7d60ed1219fULL, 779}, /* 10^273 */
    {0x93e1ab8252f33b45ULL, 0xcabb90e5c942b503ULL, 783}, /* 10^274 */
    {0xb8da1662e7b00a17ULL, 0x3d6a751f3b936244ULL, 786}, /* 10^275 */
    {0xe7109bfba19c0c9dULL, 0x0cc512670a783ad5ULL, 789}, /* 10^276 */
    {0x906a617d450187e2ULL, 0x27fb2b80668b24c5ULL, 793}, /* 10^277 */
    {0xb484f9dc9641e9daULL, 0xb1f9f660802dedf6ULL, 796}, /* 10^278 */
    {0xe1a63853bbd26451ULL, 0x5e7873f8a0396974ULL, 799}, /* 10^279 */
    {0x8d07e33455637eb2ULL, 0xdb0b487b6423e1e8ULL, 803}, /* 10^280 */
    {0xb049dc016abc5e5fULL, 0x91ce1a9a3d2cda63ULL, 806}, /* 10^281 */
    {0xdc5c5301c56b75f7ULL, 0x7641a140cc7810fbULL, 809}, /* 10^282 */
    {0x89b9b3e11b6329baULL, 0xa9e904c87fcb0a9dULL, 813}, /* 10^283 */
    {0xac2820d9623bf429ULL, 0x546345fa9fbdcd44ULL, 816}, /* 10^284 */
    {0xd732290fbacaf133ULL, 0xa97c177947ad4095ULL, 819}, /* 10^285 */
    {0x867f59a9d4bed6c0ULL, 0x49ed8eabcccc485dULL, 823}, /* 10^286 */
    {0xa81f301449ee8c70ULL, 0x5c68f256bfff5a75ULL, 826}, /* 10^287 */
    {0xd226fc195c6a2f8cULL, 0x73832eec6fff3112ULL, 829}, /* 10^288 */
    {0x83585d8fd9c25db7ULL, 0xc831fd53c5ff7eabULL, 833}, /* 10^289 */
    {0xa42e74f3d032f525ULL, 0xba3e7ca8b77f5e56ULL, 836}, /* 10^290 */
    {0xcd3a1230c43fb26fULL, 0x28ce1bd2e55f35ebULL, 839}, /* 10^291 */
    {0x80444b5e7aa7cf85ULL, 0x7980d163cf5b81b3ULL, 843}, /* 10^292 */
    {0xa0555e361951c366ULL, 0xd7e105bcc3326220ULL, 846}, /* 10^293 */
    {0xc86ab5c39fa63440ULL, 0x8dd9472bf3fefaa8ULL, 849}, /* 10^294 */
    {0xfa856334878fc150ULL, 0xb14f98f6f0feb952ULL, 852}, /* 10^295 */
    {0x9c935e00d4b9d8d2ULL, 0x6ed1bf9a569f33d3ULL, 856}, /* 10^296 */
    {0xc3b8358109e84f07ULL, 0x0a862f80ec4700c8ULL, 859}, /* 10^297 */
    {0xf4a642e14c6262c8ULL, 0xcd27bb612758c0faULL, 862}, /* 10^298 */
    {0x98e7e9cccfbd7dbdULL, 0x8038d51cb897789cULL, 866}, /* 10^299 */
    {0xbf21e44003acdd2cULL, 0xe0470a63e6bd56c3ULL, 869}, /* 10^300 */
    {0xeeea5d5004981478ULL, 0x1858ccfce06cac74ULL, 872}, /* 10^301 */
    {0x95527a5202df0ccbULL, 0x0f37801e0c43ebc9ULL, 876}, /* 10^302 */
    {0xbaa718e68396cffdULL, 0xd30560258f54e6bbULL, 879}, /* 10^303 */
    {0xe950df20247c83fdULL, 0x47c6b82ef32a2069ULL, 882}, /* 10^304 */
    {0x91d28b7416cdd27eULL, 0x4cdc331d57fa5442ULL, 886}, /* 10^305 */
    {0xb6472e511c81471dULL, 0xe0133fe4adf8e952ULL, 889}, /* 10^306 */
    {0xe3d8f9e563a198e5ULL, 0x58180fddd97723a7ULL, 892}, /* 10^307 */
    {0x8e679c2f5e44ff8fULL, 0x570f09eaa7ea7648ULL, 896} /* 10^308 */
};

/* Decimal-exponent range of th8Pow10Lemire[]. */
#define TH8_POW10_Q_MIN (-342)
#define TH8_POW10_Q_MAX 308


/*
 *----------------------------------------------------------------------
 *
 * th8Mul64x64 --
 *
 *	Compute the full 128-bit product of two 64-bit unsigned
 *	values, returning (hi, lo).
 *
 * Why / How:
 *	Eisel-Lemire needs both halves of the 128-bit product
 *	(unlike th8Mul64Top which only returns the top half with
 *	rounding).  Decomposes each input into 32-bit halves and
 *	assembles the four partial products with carry tracking.
 *
 *----------------------------------------------------------------------
 */

static void
th8Mul64x64(
    th8_uint64_t x, /* First factor. */
    th8_uint64_t y, /* Second factor. */
    th8_uint64_t *pHi, /* OUT: high 64 bits of product. */
    th8_uint64_t *pLo) /* OUT: low 64 bits of product. */
{
    th8_uint64_t xl = x & 0xFFFFFFFFULL;
    th8_uint64_t xh = x >> 32;
    th8_uint64_t yl = y & 0xFFFFFFFFULL;
    th8_uint64_t yh = y >> 32;
    th8_uint64_t ll = xl * yl;
    th8_uint64_t hl = xh * yl;
    th8_uint64_t lh = xl * yh;
    th8_uint64_t hh = xh * yh;
    th8_uint64_t mid = (ll >> 32) + (hl & 0xFFFFFFFFULL) +
                       (lh & 0xFFFFFFFFULL);

    *pLo = (ll & 0xFFFFFFFFULL) | (mid << 32);
    *pHi = hh + (hl >> 32) + (lh >> 32) + (mid >> 32);
}


#if defined(TH8_ENABLE_BIGINT)
/*
 *----------------------------------------------------------------------
 *
 * th8BignumDecideRound --
 *
 *	When Eisel-Lemire detects a halfway-rounding ambiguity,
 *	compute the exact integer comparison between M * 10^q and
 *	the midpoint (mantissa, biased_exp) <-> (mantissa+1,
 *	biased_exp) to decide the rounding direction.
 *
 * Why / How:
 *	The 128-bit cached pow10 carries ~0.5 ULP of error, so when
 *	the Eisel-Lemire product lands at the boundary (round_bit=1,
 *	sticky=0) the fast path cannot tell whether the true value
 *	is above or below the midpoint.  We resolve it by computing
 *	both sides as exact arbitrary-precision integers via
 *	libtommath and comparing.
 *
 *	Returns 1 if the rounding should go up (round to mantissa+1)
 *	or 0 for round-down (keep mantissa).  Halfway ties resolve
 *	to even per IEEE 754 round-to-nearest-even.
 *
 *	On allocator failure, returns 0 (conservative: keep the
 *	round-down candidate, which is within 1 ULP of correct).
 *
 *----------------------------------------------------------------------
 */

static int
th8BignumDecideRound(
    Th8_Interp *interp, /* Interpreter (for the bigint allocator). */
    th8_uint64_t M, /* Original parsed mantissa. */
    int q, /* Original decimal exponent. */
    th8_uint64_t mantissa, /* Candidate IEEE 754 mantissa (round-down). */
    int biased_exp) /* Candidate IEEE 754 biased exponent. */
{
    mp_int A, B, scale;
    mp_err err;
    int result = 0;
    int eD;
    th8_uint64_t midM;
    int midE;

    /*
     * Convert the candidate (mantissa, biased_exp) into a binary
     * scaling exponent eD such that value = mantissa * 2^eD.
     * Normal numbers: mantissa is a 53-bit integer with bit 52 set,
     * and eD = biased_exp - 1075.  Subnormal numbers store the
     * full 52-bit fraction with biased_exp == 0, so eD = -1074.
     */

    if (biased_exp > 0) {
	eD = biased_exp - 1075;
    } else {
	eD = -1074;
    }
    midM = 2 * mantissa +
           1; /* odd, halfway between mantissa and mantissa+1 */
    midE = eD - 1;

    /*
     * Bracket all libtommath usage with setup/teardown so the
     * allocator bridge has a valid interp; the caller has already
     * verified Th8_IsBigintEnabled(interp).
     */

    th8BigintSetup(interp);

    if (mp_init_multi(&A, &B, &scale, NULL) != MP_OKAY) {
	th8BigintTeardown();
	return 0;
    }

    mp_set_u64(&A, M);
    mp_set_u64(&B, midM);

    if (q >= 0) {
	/*
	 * Compare M * 10^q vs midM * 2^midE.
	 * Multiply A by 10^q (or B by 2^|midE| if midE < 0).
	 */

	mp_set_u32(&scale, 10);
	err = mp_expt_n(&scale, q, &scale);
	if (err == MP_OKAY) err = mp_mul(&A, &scale, &A);
	if (err == MP_OKAY) {
	    if (midE >= 0) {
		err = mp_mul_2d(&B, midE, &B);
	    } else {
		err = mp_mul_2d(&A, -midE, &A);
	    }
	}
    } else {
	/*
	 * q < 0: M / 10^|q| vs midM * 2^midE.
	 * Cross-multiply by 10^|q| = 5^|q| * 2^|q|:
	 *   M vs midM * 5^|q| * 2^(midE + |q|)
	 * Then place the residual power of 2 on whichever side
	 * still has a negative exponent.
	 */

	int absq = -q;
	int extraE = midE + absq;

	mp_set_u32(&scale, 5);
	err = mp_expt_n(&scale, absq, &scale);
	if (err == MP_OKAY) err = mp_mul(&B, &scale, &B);
	if (err == MP_OKAY) {
	    if (extraE >= 0) {
		err = mp_mul_2d(&B, extraE, &B);
	    } else {
		err = mp_mul_2d(&A, -extraE, &A);
	    }
	}
    }

    if (err == MP_OKAY) {
	mp_ord ord = mp_cmp(&A, &B);
	if (ord == MP_GT) {
	    result = 1; /* M*10^q > midpoint -> round up */
	} else if (ord == MP_LT) {
	    result = 0; /* M*10^q < midpoint -> round down */
	} else {
	    /* Exactly halfway: round-to-even.  Round up iff the
	     * candidate mantissa is odd (so mantissa+1 is even). */
	    result = (mantissa & 1) ? 1 : 0;
	}
    }

    mp_clear_multi(&A, &B, &scale, NULL);
    th8BigintTeardown();
    return result;
}
#endif /* TH8_ENABLE_BIGINT */


/*
 *----------------------------------------------------------------------
 *
 * th8ScaleByPow10 --
 *
 *	Compute mantissa * 10^decExp as a correctly-rounded IEEE
 *	754 double using the Eisel-Lemire fast path with a libtommath
 *	exact-comparison fallback for halfway cases.
 *
 * Why / How:
 *	Used by Th8_ToDouble to convert a parsed (mantissa, decimal
 *	exponent) pair to a double.  The historical chained `val *=
 *	1e22` loop drifted by ~13 ULP at the IEEE 754 extremes; the
 *	previous step-of-8 + Mul64Top path narrowed it to ~2 ULP.
 *	Eisel-Lemire with the step-of-1 128-bit cached table delivers
 *	the correctly-rounded result in a single multiplication for
 *	~99.5% of inputs.
 *
 *	For the residual halfway cases (round_bit=1, sticky=0) where
 *	the 0.5 ULP cached-pow10 error could push either way, we
 *	fall back to libtommath integer arithmetic for an exact
 *	comparison against the midpoint when TH8_ENABLE_BIGINT is
 *	defined.  Without bignum support, we apply round-to-even
 *	based on the candidate's LSB -- close enough for typical
 *	use (off by at most 1 ULP on those rare ambiguous inputs).
 *
 *----------------------------------------------------------------------
 */

static double
th8ScaleByPow10(
    Th8_Interp *interp, /* Interpreter (may be NULL); gates the
				 * bigint exact-comparison fallback. */
    th8_uint64_t mantissa, /* Unsigned integer mantissa. */
    int decExp) /* Decimal exponent. */
{
    th8_uint64_t W;
    th8_uint64_t cHi, cLo;
    th8_uint64_t a_hi, a_lo, b_hi, b_lo;
    th8_uint64_t mid, top, low, sticky;
    th8_uint64_t M, roundBit;
    union {
	th8_uint64_t u;
	double d;
    } cvt;
    int lz, idx, cE, biased_exp, upperbit;
    th8_uint64_t M_orig;
    int q_orig;
    int round_up;

    if (mantissa == 0) return 0.0;
    if (decExp < TH8_POW10_Q_MIN) return 0.0;
    if (decExp > TH8_POW10_Q_MAX) {
	cvt.u = 0x7FF0000000000000ULL;
	return cvt.d;
    }

    M_orig = mantissa;
    q_orig = decExp;

    /*
     * Normalize mantissa to 64 bits (top bit set).  Track the
     * shift so we can recover the binary exponent later.
     */

    W = mantissa;
    lz = 0;
    while (!(W & (1ULL << 63))) {
	W <<= 1;
	lz++;
    }

    idx = decExp - TH8_POW10_Q_MIN;
    cHi = th8Pow10Lemire[idx].hi;
    cLo = th8Pow10Lemire[idx].lo;
    cE = th8Pow10Lemire[idx].e;

    /*
     * Full 128-bit products: W*cHi and W*cLo.  Their sum at the
     * appropriate alignment gives the top 128 bits of the 192-bit
     * value W * ((cHi << 64) | cLo), with a residual 64-bit
     * sticky-bit word for rounding.
     */

    th8Mul64x64(W, cHi, &a_hi, &a_lo);
    th8Mul64x64(W, cLo, &b_hi, &b_lo);
    mid = a_lo + b_hi;
    top = a_hi + (mid < a_lo ? 1 : 0); /* propagate carry */
    low = b_lo;

    /*
     * The 128-bit value (top, mid) has its top bit at position
     * 127 (upperbit=1) or 126 (upperbit=0).  Shift left by 1 in
     * the latter case so the leading bit is always at 127.
     */

    upperbit = (int)((top >> 63) & 1);
    if (!upperbit) {
	top = (top << 1) | (mid >> 63);
	mid = (mid << 1) | (low >> 63);
	low <<= 1;
    }

    /*
     * Binary exponent of the result:
     *   value = (top:mid:low_192bit) * 2^(cE - lz)
     *   normalized to top bit at 127: scaled by 2^(64 - upperbit)
     * So unbiased exponent of result's leading "1." position is:
     *   cE - lz + (190 + upperbit) = 1213 + upperbit + cE - lz - 1023
     * biased = unbiased + 1023 = 1213 + upperbit + cE - lz
     */

    biased_exp = 1213 + upperbit + cE - lz;
    if (biased_exp >= 0x7FF) {
	cvt.u = 0x7FF0000000000000ULL;
	return cvt.d;
    }

    /*
     * Extract 53-bit mantissa, round bit, and sticky bits.
     * For subnormals (biased_exp <= 0), shift right by extra
     * (1 - biased_exp) bits and reset biased_exp to 0.
     */

    if (biased_exp > 0) {
	M = top >> 11;
	roundBit = (top >> 10) & 1;
	sticky = (top & 0x3FFULL) | mid | low;
    } else {
	int shift = 1 - biased_exp;
	int totalShift;

	if (shift > 52) return 0.0;
	totalShift = 11 + shift;
	if (totalShift >= 64) return 0.0;
	M = top >> totalShift;
	roundBit = (top >> (totalShift - 1)) & 1;
	sticky = (top & (((th8_uint64_t)1 << (totalShift - 1)) - 1)) | mid |
	         low;
	biased_exp = 0;
    }

    /*
     * Halfway ambiguity: roundBit=1, sticky=0.  The cached
     * pow10's 0.5 ULP error means the true value could be just
     * above or just below the midpoint.  Resolve via bignum exact
     * arithmetic ONLY when bigint is enabled at runtime for this
     * interpreter (bigint is opt-in, including for this double-
     * rounding refinement); otherwise apply round-to-nearest-even
     * on the candidate's LSB (off by at most 1 ULP on these rare
     * ambiguous inputs).
     */

    if (roundBit && sticky == 0) {
#if defined(TH8_ENABLE_BIGINT)
	if (interp && Th8_IsBigintEnabled(interp)) {
	    round_up =
	        th8BignumDecideRound(interp, M_orig, q_orig, M, biased_exp);
	} else {
	    round_up = (M & 1) ? 1 : 0;
	}
#else
	/* Best effort: round-to-even */
	round_up = (M & 1) ? 1 : 0;
#endif
    } else {
	/* Unambiguous: standard round-to-nearest-even */
	round_up = (roundBit && (sticky || (M & 1))) ? 1 : 0;
    }

    if (round_up) {
	M++;
	/*
	 * Carry transitions:
	 *   normal:    M reaches 2^53 -> bump biased_exp, M becomes 2^52
	 *   subnormal: M reaches 2^52 -> promote to smallest normal
	 *              (biased_exp = 1, M = 0)
	 */
	if (biased_exp > 0 && M == (1ULL << 53)) {
	    M = 1ULL << 52;
	    biased_exp++;
	    if (biased_exp >= 0x7FF) {
		cvt.u = 0x7FF0000000000000ULL;
		return cvt.d;
	    }
	} else if (biased_exp == 0 && M == (1ULL << 52)) {
	    biased_exp = 1;
	    M = 0;
	}
    }

    if (biased_exp > 0) {
	cvt.u = ((th8_uint64_t)biased_exp << 52) | (M & 0xFFFFFFFFFFFFFULL);
    } else {
	cvt.u = M & 0xFFFFFFFFFFFFFULL;
    }
    return cvt.d;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_SetResultDouble --
 *
 *	Set the interpreter result to a double.  Uses a simple
 *	decimal format without C runtime dependency.
 *
 * Why / How:
 *	Implements %g-style formatting without libc.  Handles NaN,
 *	Inf, negative zero, and reads ::tcl_precision to control
 *	significant digits.  When precision is 0 (default), finds
 *	the shortest digit count that round-trips to the original
 *	IEEE 754 value.  The result always contains '.' or 'e' so
 *	it is distinguishable from an integer.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Previous result is freed.
 *
 *----------------------------------------------------------------------
 */

int
Th8_SetResultDouble(
    Th8_Interp *interp, /* Interpreter. */
    double rVal) /* Double value. */
{
    /*
     * Format a double using %g-style output that respects
     * ::tcl_precision.  The default precision (when unset or
     * 0) is 17 significant digits -- enough for exact IEEE 754
     * round-trip.  Non-zero values (1..17) specify the number
     * of significant digits explicitly.
     *
     * The result always contains a '.' or 'e' so that it is
     * distinguishable from an integer.
     */

    char zBuf[80];
    char *z = zBuf;
    int neg = 0;
    int nSig; /* Significant digits to emit. */
    int expn = 0; /* Base-10 exponent. */
    int useExp; /* True to use scientific notation. */
    int i;

    if (!interp) return TH8_ERROR;

    /*
     * Read ::tcl_precision.  Default to 0 which means "use
     * the shortest representation that uniquely identifies
     * the double" -- 17 significant digits (IEEE 754
     * round-trip).  Non-zero values (1..17) specify the
     * number of significant digits explicitly.
     */

    nSig = 0; /* 0 = shortest round-trip (default). */
#if defined(TH8_ENABLE_VARIABLES)
    if (Th8_GetVar(interp, "::tcl_precision", TH8_NOLEN) == TH8_OK) {
	int v;
	size_t nP;
	const char *zP = Th8_GetResult(interp, &nP);

	if (Th8_ToInt(interp, zP, nP, &v) == TH8_OK && v >= 0 && v <= 17) {
	    nSig = v;
	}
    }
#endif

    /*
     * Handle sign and zero.  The signbit() check catches
     * negative zero, which compares equal to positive zero
     * but must be formatted as "-0.0" to preserve its
     * IEEE 754 sign through string round-trip.
     */

    /*
     * Check the sign bit via union to avoid strict-aliasing
     * violation.  This catches negative zero, which compares
     * equal to positive zero in IEEE 754 but must be formatted
     * as "-0.0" to preserve its sign through string round-trip.
     */

    {
	union {
	    double d;
	    th8_uint64_t u;
	} signCheck;

	signCheck.d = rVal;
	if (signCheck.u >> 63) {
	    neg = 1;
	    rVal = -rVal;
	}
    }

    /*
     * Handle NaN and Infinity (quiet NaN, per IEEE 754).
     * Tcl represents these as "NaN" and "Inf"/"-Inf".
     */

    if (rVal != rVal) {
	/* NaN: x != x is true only for NaN. */
	Th8_SetResultStatic(interp, "NaN", 3);
	return TH8_OK;
    }
    if (rVal > 0.0 && rVal + rVal == rVal && ALWAYS(rVal > 1.0)) {
	/* Positive infinity: adding to itself doesn't change it. */
	if (neg) {
	    Th8_SetResultStatic(interp, "-Inf", 4);
	    return TH8_OK;
	}
	Th8_SetResultStatic(interp, "Inf", 3);
	return TH8_OK;
    }

    if (rVal == 0.0) {
	if (neg) *z++ = '-';
	*z++ = '0';
	*z++ = '.';
	*z++ = '0';
	*z = 0;
	return Th8_SetResult(interp, zBuf, (size_t)(z - zBuf));
    }

    /*
     * Extract 17 significant digits and the base-10 exponent
     * via Grisu (DiyFp arithmetic against a cached power of
     * ten).  The historical approach -- repeated `mant /=
     * 1e16` followed by a `m = (m - d) * 10.0` accumulator
     * loop -- drifted by several ULP near DBL_MAX and
     * DBL_MIN_NORMAL, causing the 17 emitted digits to
     * disagree with the true double and forcing the
     * round-trip search below into the fallback path with the
     * wrong digits.  Grisu gives us 17 digits accurate to
     * within ~1 ULP at the last position, which is enough for
     * the round-trip search to find the shortest correct
     * representation.
     */

    {
	char zDig[20];
	int nDig;
	int carry;

	th8ExtractDigits17(rVal, zDig, &expn);

	/*
	 * When nSig == 0 (default), find the shortest digit
	 * count that round-trips to the original double.
	 * This produces "3.14" instead of "3.1400000000000001"
	 * for values that are exactly representable at fewer
	 * digits, while still distinguishing nextafter(1.0,2.0)
	 * from 1.0.
	 *
	 * Algorithm: for each candidate length k (1..17),
	 * round the 17-digit buffer to k digits and check
	 * if reconstructing the double from the rounded k
	 * digits recovers the original rVal.
	 */

	if (nSig == 0) {
	    double origVal = neg ? -rVal : rVal;

	    nDig = 1;

	    /*
	     * Negative rVal was negated at the top; rVal is
	     * now positive.  origVal is the signed original.
	     */

	    (void)origVal; /* suppress unused if loop below
			     * terminates early */
	    for (nDig = 1; nDig <= 17; nDig++) {
		/*
		 * Round zDig to nDig digits (working on a
		 * copy to avoid corrupting the full buffer).
		 */

		char zTry[20];
		int c;
		int tExpn = expn;

		for (i = 0; i < nDig; i++)
		    zTry[i] = zDig[i];
		c = (nDig < 17 && zDig[nDig] >= '5') ? 1 : 0;
		for (i = nDig - 1; c && i >= 0; i--) {
		    zTry[i] += (char)c;
		    if (zTry[i] > '9') {
			zTry[i] = '0';
			c = 1;
		    } else {
			c = 0;
		    }
		}
		if (c) {
		    for (i = nDig - 1; i > 0; i--) {
			zTry[i] = zTry[i - 1];
		    }
		    zTry[0] = '1';
		    tExpn++;
		}

		/*
		 * Reconstruct the double from the rounded digits
		 * and exponent.  Build the candidate string
		 * "[+-]?<zTry digits> 'e' <tExpn>" and parse it
		 * with Th8_ToDouble -- this is the SAME parser
		 * that any user script that reads back the
		 * emitted string would use, so a "round-trip
		 * match" here is the user-observable definition
		 * of round-tripping.
		 *
		 * The historical reconstruction used a local
		 * "digit * place; place /= 10.0" accumulator that
		 * was not correctly-rounded, which caused
		 * `Th8_SetResultDouble` to report shorter
		 * round-trip lengths than the user-observable
		 * Th8_ToDouble could actually reproduce -- e.g.
		 * sqrt(2) was emitted as "1.414213562373095" but
		 * that 15-digit string parses to a different
		 * double than sqrt(2).  Routing through
		 * Th8_ToDouble removes that mismatch.
		 */

		{
		    char zCheck[40];
		    int nCheck = 0;
		    double rebuilt;
		    int j;

		    for (j = 0; j < nDig; j++) {
			zCheck[nCheck++] = zTry[j];
		    }
		    zCheck[nCheck++] = 'e';
		    {
			int eVal = tExpn - (nDig - 1);
			char zExpBuf[12];
			int nExpBuf = 0;

			if (eVal < 0) {
			    zCheck[nCheck++] = '-';
			    eVal = -eVal;
			}
			if (eVal == 0) {
			    zExpBuf[nExpBuf++] = '0';
			} else {
			    while (eVal > 0) {
				zExpBuf[nExpBuf++] = (char)('0' +
				                            (eVal % 10));
				eVal /= 10;
			    }
			}
			while (nExpBuf > 0) {
			    zCheck[nCheck++] = zExpBuf[--nExpBuf];
			}
		    }
		    /* Parse the reconstructed string the same way
		     * a user script would.  NULL interp avoids
		     * disturbing the interpreter result. */
		    if (Th8_ToDouble(0, zCheck, (size_t)nCheck, &rebuilt) ==
		            TH8_OK &&
		        rebuilt == rVal) {
			for (i = 0; i < nDig; i++) {
			    zDig[i] = zTry[i];
			}
			expn = tExpn;
			goto found_precision;
		    }
		}
	    }
	    /* Fallback: use all 17 digits. */
	    nDig = 17;
found_precision:;
	} else {
	    nDig = nSig;
	    if (nDig > 17) nDig = 17;

	    /* Round at nDig. */
	    carry = (nDig < 17 && zDig[nDig] >= '5') ? 1 : 0;
	    for (i = nDig - 1; carry && i >= 0; i--) {
		zDig[i] += (char)carry;
		if (zDig[i] > '9') {
		    zDig[i] = '0';
		    carry = 1;
		} else {
		    carry = 0;
		}
	    }
	    if (carry) {
		for (i = nDig - 1; i > 0; i--) {
		    zDig[i] = zDig[i - 1];
		}
		zDig[0] = '1';
		expn++;
	    }
	}

	/*
	 * Decide format: use scientific notation if exponent
	 * is < -4 or very large.  When nSig == 0 (shortest
	 * round-trip), nDig may be very small (e.g., 1 for
	 * 10.0), so we use 17 as the upper threshold to
	 * avoid premature scientific notation for values
	 * like 10.0, 100.0, etc.
	 */

	useExp = (expn < -4 || expn >= (nSig == 0 ? 17 : nDig));

	/*
	 * Strip trailing zeros from significant digits.
	 */

	while (nDig > 1 && zDig[nDig - 1] == '0') {
	    nDig--;
	}

	/*
	 * Emit the formatted result.
	 */

	if (neg) *z++ = '-';

	if (useExp) {
	    /*
	     * Scientific: D.DDDe+NN
	     */

	    *z++ = zDig[0];
	    if (nDig > 1) {
		*z++ = '.';
		for (i = 1; i < nDig; i++) {
		    *z++ = zDig[i];
		}
	    }
	    *z++ = 'e';
	    if (expn >= 0) {
		*z++ = '+';
	    } else {
		*z++ = '-';
		expn = -expn;
	    }
	    if (expn >= 100) {
		*z++ = (char)('0' + expn / 100);
		*z++ = (char)('0' + (expn / 10) % 10);
		*z++ = (char)('0' + expn % 10);
	    } else {
		*z++ = (char)('0' + expn / 10);
		*z++ = (char)('0' + expn % 10);
	    }
	} else {
	    /*
	     * Fixed: DDDD.DDD
	     *
	     * expn is the exponent of the first digit.
	     * expn=0 means D.DDD, expn=2 means DDD.D, etc.
	     */

	    int dotPos = expn + 1; /* digits before '.' */

	    if (dotPos <= 0) {
		/* 0.00...0DDD */
		*z++ = '0';
		*z++ = '.';
		for (i = 0; i < -dotPos; i++) {
		    *z++ = '0';
		}
		for (i = 0; i < nDig; i++) {
		    *z++ = zDig[i];
		}
	    } else if (dotPos >= nDig) {
		/* DDD000.0 */
		for (i = 0; i < nDig; i++) {
		    *z++ = zDig[i];
		}
		for (i = nDig; i < dotPos; i++) {
		    *z++ = '0';
		}
		*z++ = '.';
		*z++ = '0';
	    } else {
		/* DD.DDD */
		for (i = 0; i < dotPos; i++) {
		    *z++ = zDig[i];
		}
		*z++ = '.';
		for (i = dotPos; i < nDig; i++) {
		    *z++ = zDig[i];
		}
	    }
	}
    }
    *z = 0;

    return Th8_SetResult(interp, zBuf, (size_t)(z - zBuf));
}


/*
 *----------------------------------------------------------------------
 *
 * Introspection helpers --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8AppendHashKeys --
 *
 *	Hash iteration callback: append each entry's key to a list.
 *
 * Why / How:
 *	Used with Th8_HashIterate to collect all keys from a hash
 *	table into a Tcl list.  The pCtx is a three-element void*
 *	array (interp, pzList, pnList) packed by the caller.
 *	Each key is appended with proper Tcl list quoting.
 *
 * Results:
 *	Always returns TH8_OK (continue iterating).
 *
 * Side effects:
 *	Appends the key to the list buffer via Th8_ListAppend.
 *
 *----------------------------------------------------------------------
 */

int
th8AppendHashKeys(
    Th8_HashEntry *pEntry, /* Hash entry. */
    void *pCtx) /* Th8_InterpAndList struct. */
{
    /*
     * pCtx points to a { Th8_Interp*, char**, size_t* } triple
     * packed on the stack by the caller.
     */

    void **ap = (void **)pCtx;
    Th8_Interp *interp = (Th8_Interp *)ap[0];
    char **pzList = (char **)ap[1];
    size_t *pnList = (size_t *)ap[2];

    Th8_ListAppend(interp, pzList, pnList, pEntry->zKey, pEntry->nKey);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendCommands --
 *
 *	Append all command names to a list.  Iterates the current
 *	namespace first, then the global namespace (if different).
 *
 * Why / How:
 *	Uses Th8_HashIterate with th8AppendHashKeys to walk the
 *	current namespace's command hash, then the global namespace
 *	(if different), appending every key.  This implements
 *	[info commands] with no pattern filter.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendCommands(
    Th8_Interp *interp, /* Interpreter. */
    char **pz, /* IN/OUT: list buffer. */
    size_t *pn) /* IN/OUT: list length. */
{
    void *aCtx[3];

    if (!interp) return TH8_ERROR;
    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    Th8_HashIterate(
        interp, interp->pCurrentNs->paCmd, th8AppendHashKeys, (void *)aCtx);
    if (interp->pCurrentNs != interp->pGlobalNs) {
	Th8_HashIterate(
	    interp, interp->pGlobalNs->paCmd, th8AppendHashKeys,
	    (void *)aCtx);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendCommandsMatching --
 *
 *	Append command names to a list, filtered by xProc.
 *	Only commands whose dispatch function matches xMatch1
 *	or xMatch2 are included.  Pass NULL for xMatch2 to
 *	match a single dispatch function.
 *
 * Why / How:
 *	Iterates command hashes using th8AppendMatchingKeys, which
 *	compares each command's xProc against the two target function
 *	pointers.  Function-pointer comparison uses a union to
 *	safely cast between void* and Th8_CommandProc.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8AppendMatchingKeys --
 *
 *	Hash iteration callback: append a command's key to a list
 *	if its dispatch function matches one of two target procs.
 *
 * Why / How:
 *	Extracts the Th8_Command from the hash entry and compares
 *	its xProc against xMatch1 and xMatch2 (passed via the
 *	void* context array using a union for safe function-pointer
 *	casting).  Only matching entries are appended to the list.
 *
 * Results:
 *	Always returns TH8_OK (continue iterating).
 *
 * Side effects:
 *	May append to the list buffer via Th8_ListAppend.
 *
 *----------------------------------------------------------------------
 */

static int
th8AppendMatchingKeys(Th8_HashEntry *pEntry, void *pContext)
{
    void **aCtx = (void **)pContext;
    Th8_Interp *interp = (Th8_Interp *)aCtx[0];
    char **pz = (char **)aCtx[1];
    size_t *pn = (size_t *)aCtx[2];
    union {
	void *p;
	Th8_CommandProc f;
    } u1, u2;
    Th8_CommandProc xMatch1, xMatch2;
    Th8_Command *pCmd = (Th8_Command *)pEntry->pData;

    u1.p = aCtx[3];
    u2.p = aCtx[4];
    xMatch1 = u1.f;
    xMatch2 = u2.f;

    if (pCmd &&
        (pCmd->xProc == xMatch1 || (xMatch2 && pCmd->xProc == xMatch2))) {
	Th8_ListAppend(interp, pz, pn, pEntry->zKey, pEntry->nKey);
    }
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_ListAppendCommandsMatching --
 *
 *	Append command names whose dispatch function matches xMatch1
 *	or xMatch2 to a list buffer.
 *
 * Why / How:
 *	Packs the interp, list pointers, and two function pointers
 *	into a five-element void* context array, then iterates the
 *	current and global namespace command hashes using
 *	th8AppendMatchingKeys as the per-entry callback.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	List buffer is modified.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ListAppendCommandsMatching(
    Th8_Interp *interp, /* Interpreter. */
    char **pz, /* IN/OUT: list buffer. */
    size_t *pn, /* IN/OUT: list length. */
    Th8_CommandProc xMatch1, /* First dispatch to match. */
    Th8_CommandProc xMatch2) /* Second dispatch (or NULL). */
{
    void *aCtx[5];
    union {
	void *p;
	Th8_CommandProc f;
    } u1, u2;

    if (!interp) return TH8_ERROR;
    aCtx[0] = (void *)interp;
    aCtx[1] = (void *)pz;
    aCtx[2] = (void *)pn;
    u1.f = xMatch1;
    u2.f = xMatch2;
    aCtx[3] = u1.p;
    aCtx[4] = u2.p;

    /*
     * Search the current namespace first, then (if different)
     * the global namespace.
     */

    Th8_HashIterate(
        interp, interp->pCurrentNs->paCmd, th8AppendMatchingKeys,
        (void *)aCtx);
    if (interp->pCurrentNs != interp->pGlobalNs) {
	Th8_HashIterate(
	    interp, interp->pGlobalNs->paCmd, th8AppendMatchingKeys,
	    (void *)aCtx);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * UTF-8 additional helpers --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_Utf8Advance --
 *
 *	Advance a pointer by nChar code points.
 *
 * Why / How:
 *	Decodes one UTF-8 character at a time via Th8_Utf8Decode,
 *	advancing the byte pointer by the decoded byte count, until
 *	nChar code points have been consumed or the byte limit is
 *	reached.  Returns a pointer past the last decoded character.
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_Utf8Advance(
    const char *z, /* UTF-8 string. */
    size_t n, /* Byte length. */
    int nChar) /* Code points to advance. */
{
    size_t i = 0;
    int c = 0;

    while (i < n && c < nChar) {
	int nByte;

	Th8_Utf8Decode(&z[i], n - i, &nByte);
	i += (size_t)nByte;
	c++;
    }
    return &z[i];
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Utf8Index --
 *
 *	Return a pointer to the iChar'th code point.
 *
 * Why / How:
 *	Delegates to Th8_Utf8Advance to skip iChar code points,
 *	then checks that the resulting pointer is within bounds.
 *	Returns NULL for negative indices or out-of-range offsets.
 *
 *----------------------------------------------------------------------
 */

const char *
Th8_Utf8Index(
    const char *z, /* UTF-8 string. */
    size_t n, /* Byte length. */
    int iChar) /* 0-based character index. */
{
    const char *p;

    if (iChar < 0) return 0;
    p = Th8_Utf8Advance(z, n, iChar);
    if ((size_t)(p - z) > n) return 0;
    return p;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Utf8Validate --
 *
 *	Check that a string is well-formed UTF-8.
 *
 * Why / How:
 *	Walks the byte string calling Th8_Utf8Decode at each
 *	position.  Rejects decode failures (negative code point),
 *	surrogate code points (U+D800..U+DFFF), and values above
 *	U+10FFFF.  On failure, *piOffset receives the byte offset
 *	of the first invalid byte.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Utf8Validate(
    const char *z, /* String to check. */
    size_t n, /* Byte length. */
    int *piOffset) /* OUT: offset of first bad byte. */
{
    size_t i = 0;

    while (i < n) {
	int nByte;
	int cp;

	cp = Th8_Utf8Decode(&z[i], n - i, &nByte);
	if (cp < 0) {
	    if (piOffset) *piOffset = (int)i;
	    return TH8_ERROR;
	}
	/* Reject surrogates and overlong encodings */
	if (cp >= 0xD800 && cp <= 0xDFFF) {
	    if (piOffset) *piOffset = (int)i;
	    return TH8_ERROR;
	}
	if (cp > 0x10FFFF) {
	    if (piOffset) *piOffset = (int)i;
	    return TH8_ERROR;
	}
	i += (size_t)nByte;
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Strdup --
 *
 *	Duplicate a string.
 *
 * Why / How:
 *	Allocates n+1 bytes via Th8_AttemptMalloc, copies n bytes
 *	via Th8_Memcpy, and NUL-terminates.  Handles TH8_NOLEN by
 *	measuring the string first.  Returns NULL on allocation
 *	failure.
 *
 *----------------------------------------------------------------------
 */

char *
Th8_Strdup(
    Th8_Interp *interp, /* Interpreter for memory. */
    const char *z, /* String to duplicate. */
    size_t n) /* Length (TH8_NOLEN = NUL-term). */
{
    char *zNew;

    if (n == TH8_NOLEN) {
	n = Th8_Strlen(interp, z);
    }
    zNew = (char *)TH8_ALLOC_STR(interp, n);
    if (zNew) {
	Th8_Memcpy(interp, zNew, z, n);
	zNew[n] = 0;
    }
    return zNew;
}


/*
 *----------------------------------------------------------------------
 *
 * Interpreter lifecycle --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8SourcePkgIndex --
 *
 *	Source a single pkgIndex.th8 file.  Before sourcing, the
 *	variable $dir is set to the normalized directory containing
 *	the index file.
 *
 * Why / How:
 *	Builds the path "$dir/pkgIndex.th8", attempts to read it
 *	via Th8_GetData (platform filesystem), and if found, sets
 *	::dir to the containing directory and evaluates the file
 *	contents.  Missing files are silently skipped.  This mirrors
 *	Tcl's pkgIndex.tcl sourcing convention.
 *
 *----------------------------------------------------------------------
 */

static void
th8SourcePkgIndex(
    Th8_Interp *interp, /* Interpreter. */
    const char *zDir, /* Directory path. */
    size_t nDir) /* Length of zDir. */
{
    char *zIdx = 0;
    size_t nIdx = 0;

    /*
     * Build "$dir/pkgIndex.th8".
     */

    TH8_STR_APPEND(interp, &zIdx, &nIdx, zDir, nDir);
    TH8_STR_APPEND(interp, &zIdx, &nIdx, "/pkgIndex.th8", 13);

    /* Try to read the index file; silently skip if not found.
     *
     * Bug 34: pkgIndex sourcing must isolate ::dir and
     * ::th8_security from the surrounding (post-init) global
     * scope.  Without isolation, the last-sourced pkgIndex's
     * directory and its signature-verification details
     * persist as visible globals after Th8_AutoPathSearch
     * returns -- e.g. `parray th8_security` at the interactive
     * shell prompt shows the LAST pkgIndex.th8's algorithmName /
     * dataName / publicKeyToken, and `set dir` succeeds with
     * its containing directory.  Th8_GetData itself triggers
     * signature verification which populates ::th8_security,
     * so the save must bracket BOTH the GetData call and the
     * Eval (mirroring Th8_EvalFile's contract).
     */
    {
	char *zData = 0;
	size_t nData = 0;
#if defined(TH8_ENABLE_VARIABLES)
	void *pSecSaved = NULL;
	char *zSavedDir = NULL;
	size_t nSavedDir = 0;
	int bHadDir = 0;

	/* Save outer scope before any verification work. */
	Th8_SaveSystemVar(interp, "::th8_security", TH8_NOLEN, &pSecSaved);
	if (Th8_GetVar(interp, "::dir", TH8_NOLEN) == TH8_OK) {
	    const char *z;
	    size_t n;

	    z = Th8_GetResult(interp, &n);
	    if (z) {
		zSavedDir = (char *)TH8_ALLOC_STR(interp, n);
		if (zSavedDir) {
		    Th8_Memcpy(interp, zSavedDir, z, n);
		    zSavedDir[n] = '\0';
		    nSavedDir = n;
		    bHadDir = 1;
		}
	    }
	}
#endif

	if (Th8_GetData(
	        interp, zIdx, nIdx, &zData, &nData, TH8_TRANSLATE_EOL) ==
	    TH8_OK) {
#if defined(TH8_ENABLE_VARIABLES)
	    Th8_SetVar(interp, "::dir", TH8_NOLEN, zDir, nDir);
#endif
	    Th8_Eval(interp, 0, zData, nData, NULL, 0);
	    Th8_Free(interp, zData);
	}

#if defined(TH8_ENABLE_VARIABLES)
	/* Restore outer scope.  Order: ::dir first (scalar),
	 * then ::th8_security (array).  If ::dir didn't exist
	 * outside, [unset] it to leave the global scope clean. */
	if (bHadDir) {
	    Th8_SetVar(interp, "::dir", TH8_NOLEN, zSavedDir, nSavedDir);
	} else {
	    Th8_UnsetVar(interp, "::dir", TH8_NOLEN);
	}
	if (zSavedDir) Th8_Free(interp, zSavedDir);
	Th8_RestoreSystemVar(interp, "::th8_security", TH8_NOLEN, pSecSaved);
#endif
    }
    Th8_Free(interp, zIdx);
    return;

oom:
    /* A TH8_STR_APPEND growth failed while building the index path;
     * "out of memory" already set.  Nothing has been sourced yet, so
     * just release the partial path and return (void). */
    Th8_Free(interp, zIdx);
}


/*
 *----------------------------------------------------------------------
 *
 * th8InitGlobals --
 *
 *	Initialize the standard global variables.
 *
 * Why / How:
 *	Populates the Tcl-compatible global variable set that
 *	scripts expect.  Platform-specific values (user, host)
 *	are obtained via platform callbacks; compile-time options
 *	and source version info are assembled into lists.  Called
 *	during Th8_CreateInterp and Th8_RestoreInterp.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets ::tcl_platform(engine), ::tcl_platform(platform),
 *	::argv, ::auto_path, ::errorCode, ::errorInfo,
 *	::tcl_precision.
 *
 *----------------------------------------------------------------------
 */

#if defined(TH8_ENABLE_VARIABLES)
static void
th8InitGlobals(Th8_Interp *interp) /* Interpreter. */
{
    Th8_SetVar(interp, "::tcl_version", TH8_NOLEN, "8.6", TH8_NOLEN);
    Th8_SetVar(interp, "::tcl_patchLevel", TH8_NOLEN, "8.6.19", TH8_NOLEN);
#  if defined(TH8_DEBUG)
    Th8_SetVar(interp, "::tcl_platform(debug)", TH8_NOLEN, "1", TH8_NOLEN);
#  endif
    Th8_SetVar(interp, "::tcl_platform(engine)", TH8_NOLEN, "TH8", TH8_NOLEN);
    Th8_SetVar(
        interp, "::tcl_platform(patchLevel)", TH8_NOLEN, TH8_PATCH_LEVEL,
        TH8_NOLEN);
#  if defined(_WIN32) || defined(WIN32)
    Th8_SetVar(
        interp, "::tcl_platform(platform)", TH8_NOLEN, "windows", TH8_NOLEN);
#  else
    Th8_SetVar(
        interp, "::tcl_platform(platform)", TH8_NOLEN, "unix", TH8_NOLEN);
#  endif

    {
	/*
	 * The "source" array element value is a three-element list:
	 *    <commit_id> <commit_dateTime> <commit_tags>
	 */

	char *zSrc = 0;
	size_t nSrc = 0;

	Th8_ListAppend(interp, &zSrc, &nSrc, TH8_SOURCE_ID, TH8_NOLEN);
	Th8_ListAppend(interp, &zSrc, &nSrc, TH8_SOURCE_TIMESTAMP, TH8_NOLEN);
	Th8_ListAppend(interp, &zSrc, &nSrc, TH8_SOURCE_TAGS, TH8_NOLEN);
	Th8_SetVar(interp, "::tcl_platform(source)", TH8_NOLEN, zSrc, nSrc);
	Th8_Free(interp, zSrc);
    }

    /*
     * ::tcl_platform(compileOptions) -- Tcl list of active
     * compile-time options (TH8_ prefix stripped).
     */

    {
	const char **azOpts = Th8_GetCompileOptions();
	char *zOpts = 0;
	size_t nOpts = 0;
	int k;

	for (k = 0; azOpts[k]; k++) {
	    Th8_ListAppend(interp, &zOpts, &nOpts, azOpts[k], TH8_NOLEN);
	}
	Th8_SetVar(
	    interp, "::tcl_platform(compileOptions)", TH8_NOLEN,
	    zOpts ? zOpts : "", zOpts ? nOpts : 0);
	Th8_Free(interp, zOpts);
    }

    /*
     * ::tcl_platform(user) and ::tcl_platform(host) via
     * platform callbacks.  No direct OS calls here.
     */

    {
	const Th8_Platform *pPlat = Th8_GetPlatform(interp);
	char zBuf[256];

	if (pPlat->xGetUserName &&
	    pPlat->xGetUserName(interp, pPlat->pCtx, zBuf, sizeof(zBuf)) ==
	        TH8_OK) {
	    Th8_SetVar(
	        interp, "::tcl_platform(user)", TH8_NOLEN, zBuf, TH8_NOLEN);
	} else {
	    Th8_SetVar(
	        interp, "::tcl_platform(user)", TH8_NOLEN, "", TH8_NOLEN);
	}

	if (pPlat->xGetHostName &&
	    pPlat->xGetHostName(interp, pPlat->pCtx, zBuf, sizeof(zBuf)) ==
	        TH8_OK) {
	    Th8_SetVar(
	        interp, "::tcl_platform(host)", TH8_NOLEN, zBuf, TH8_NOLEN);
	} else {
	    Th8_SetVar(
	        interp, "::tcl_platform(host)", TH8_NOLEN, "", TH8_NOLEN);
	}
    }

    Th8_SetVar(interp, "::argv", TH8_NOLEN, "", TH8_NOLEN);
    Th8_SetVar(
        interp, "::auto_path", TH8_NOLEN,
        "lib/th8 lib/sqlite3 lib/testlib lib/Standard1.0", TH8_NOLEN);
    Th8_SetVar(interp, "::errorCode", TH8_NOLEN, "NONE", TH8_NOLEN);
    Th8_SetVar(interp, "::errorInfo", TH8_NOLEN, "", TH8_NOLEN);
    Th8_SetVar(interp, "::tcl_precision", TH8_NOLEN, "0", TH8_NOLEN);
    /* NOTE: 0 = "shortest representation that round-trips exactly".
     * Th8_SetResultDouble implements the round-trip check. */

    /* ::th8_timeout (2026-06-08, Bug 47 fix): default per-network-
     * operation timeout in MILLISECONDS.  Consulted by libcurl
     * (CURLOPT_TIMEOUT_MS + CURLOPT_CONNECTTIMEOUT_MS) and the
     * DNSSEC resolver where applicable.  Scripts can change the
     * value (eg. `set ::th8_timeout 5000` for a 5-second cap);
     * the library reads it on every operation that honours it.
     *
     * Default is 30 000 ms (30 seconds) to match the pre-2026-06-
     * 08 behaviour and keep `clock https` / real HTTPS time-
     * server fetches working from a default-configured interp.
     * Tests that intentionally drive unreachable endpoints (e.g.
     * the DNS-mock sweep in plat_wrappers) should locally lower
     * the value to a fraction of a second before issuing the
     * call, and restore the previous value afterwards. */
    Th8_SetVar(interp, "::th8_timeout", TH8_NOLEN, "30000", 5);
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * Th8_AutoPathSearch --
 *
 *	Search ::auto_path for pkgIndex.th8 files and source them
 *	to populate the package hash.  Must be called after
 *	language commands are registered (Th8_RegisterLanguage)
 *	because the index files may use [file join], [list], etc.
 *
 *	Embedders should call this once after interpreter creation
 *	and command registration are complete.
 *
 * Why / How:
 *	Reads ::auto_path (or uses the caller-supplied value),
 *	splits it as a Tcl list, and calls th8SourcePkgIndex for
 *	each directory.  The pkgIndex.th8 files register packages
 *	via [package ifneeded], populating the package registry.
 *
 *----------------------------------------------------------------------
 */

int
Th8_AutoPathSearch(
    Th8_Interp *interp, /* Interpreter. */
    char *zAuto, /* The auto-path value to split and check. */
    size_t nAuto) /* Size of the auto-path value. */
{
    char **azList = 0;
    size_t *anList = 0;
    int nList = 0;
    int i;
    int rc = TH8_OK;
    char *zLocalAuto = zAuto;
    size_t nLocalAuto = nAuto;

    if (!interp) return TH8_ERROR;
    if (zLocalAuto == 0) {
#if defined(TH8_ENABLE_VARIABLES)
	if (Th8_GetVar(interp, "::auto_path", TH8_NOLEN) != TH8_OK) {
	    return TH8_ERROR;
	}
	zLocalAuto = Th8_TakeResult(interp, &nLocalAuto);
	/* Bug 26: Th8_TakeResult is documented to return non-NULL
	 * after a successful Th8_GetVar, but a race or OOM in the
	 * take path could in theory yield NULL.  Use plain `if`
	 * so the guard survives TH8_OMIT. */
	if (!zLocalAuto || nLocalAuto == 0) {
	    Th8_Free(interp, zLocalAuto);
	    return TH8_OK;
	}
#else
	Th8_SetResultStatic(
	    interp, "variable resolution not available", TH8_NOLEN);
	return TH8_ERROR;
#endif
    }
    if (Th8_SplitList(
            interp, zLocalAuto, nLocalAuto, &azList, &anList, &nList,
            TH8_LIST_NONE) != TH8_OK) {
	Th8_Free(interp, zAuto);
	return TH8_ERROR;
    }
    if (zLocalAuto != zAuto) {
	Th8_Free(interp, zLocalAuto);
    }
    for (i = 0; i < nList; i++) {
	th8SourcePkgIndex(interp, azList[i], anList[i]);
    }

    /*
     * Th8_SplitList uses a single allocation for pointers,
     * lengths, and string data.  Only free the pointer array;
     * anList points into the same block.
     */

    Th8_Free(interp, (void *)azList);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Complete --
 *
 *	Check whether a script is syntactically complete, i.e.,
 *	all braces, brackets, and double-quotes are balanced.
 *	Used by interactive shells to decide whether to prompt
 *	for continuation input.
 *
 *	This is a standalone function that does not require an
 *	interpreter.
 *
 * Why / How:
 *	Scans the input character by character, tracking nesting
 *	depth for braces and brackets and an in-quote flag for
 *	double-quote strings.  Backslash-escaped characters are
 *	skipped.  Returns 1 only when all three counters are zero
 *	at the end of the input.
 *
 * Results:
 *	1 if the script appears complete, 0 if more input is
 *	needed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Complete(
    const char *zScript, /* Script text. */
    size_t nScript) /* Length (TH8_NOLEN=NUL). */
{
    size_t i;
    int nBrace = 0; /* {} nesting depth */
    int nBracket = 0; /* [] nesting depth */
    int inQuote = 0; /* 1 if inside "" */

    if (nScript == TH8_NOLEN) {
	for (nScript = 0; zScript[nScript]; nScript++) {
	    /* strlen */
	}
    }

    for (i = 0; i < nScript; i++) {
	char c = zScript[i];

	if (c == '\\' && i + 1 < nScript) {
	    i++; /* skip escaped character */
	    continue;
	}

	if (inQuote) {
	    if (c == '"') {
		inQuote = 0;
	    }
	    continue;
	}

	switch (c) {
	case '{':
	    nBrace++;
	    break;
	case '}':
	    if (nBrace > 0) nBrace--;
	    break;
	case '[':
	    nBracket++;
	    break;
	case ']':
	    if (nBracket > 0) nBracket--;
	    break;
	case '"':
	    inQuote = 1;
	    break;
	}
    }

    return (nBrace == 0 && nBracket == 0 && !inQuote);
}


/*
 *----------------------------------------------------------------------
 *
 * th8ParseCommand --
 *
 *	Parse the first command in zScript into a Th8_Parse struct.
 *	This performs ONLY lexical parsing -- no substitution or
 *	evaluation occurs.  Each word is represented as a Th8_Value
 *	with type TH8_TOKEN_WORD (composite) or TH8_TOKEN_SIMPLE_WORD
 *	(literal).  Children describe the word's internal structure
 *	(TEXT, BS, COMMAND, VARIABLE tokens).
 *
 *	pParse->zAfter points past the parsed command for
 *	incremental parsing of multi-command scripts.
 *
 * Why / How:
 *	Skips leading whitespace and comments, then iterates over
 *	words using th8NextWord (scan-only mode, no substitution).
 *	Each word is classified as simple (brace-delimited or no
 *	special chars) or composite.  The word array grows
 *	dynamically.  Used by the LSP integration and syntax
 *	checking -- separate from the evaluator's word-splitting
 *	path which performs substitution.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on syntax error.
 *
 * Side effects:
 *	Allocates pParse->aWord.  Caller must call th8FreeParse().
 *
 *----------------------------------------------------------------------
 */

int
th8ParseCommand(
    Th8_Interp *interp, /* Interpreter (for error messages). */
    const char *zScript, /* Script text. */
    size_t nScript, /* Byte length (TH8_NOLEN = NUL). */
    int nLine, /* 1-based starting line number. */
    Th8_Parse *pParse) /* OUT: filled on success. */
{
    const char *z;
    size_t n;
    size_t nSpace;
    size_t nWord;
    int rc = TH8_OK;
    int nAlloc = 0;
    int nCount = 0;
    Th8_Value *aWord = 0;
    const char *zStart;

    if (!pParse) return TH8_ERROR;
    if (nScript == TH8_NOLEN) {
	size_t k = 0;

	while (zScript[k])
	    k++;
	nScript = k;
    }

    pParse->zCommand = 0;
    pParse->nCommand = 0;
    pParse->nWord = 0;
    pParse->aWord = 0;
    pParse->nLine = nLine;
    pParse->zAfter = zScript;
    pParse->nComment = 0;
    pParse->zComment = 0;

    z = zScript;
    n = nScript;

    /*
     * Skip leading whitespace and newlines.
     */

    while (n > 0 &&
           (th8IsSpace(*z) || *z == '\n' || *z == '\r' || *z == ';')) {
	if (*z == '\n') nLine++;
	z++;
	n--;
    }
    if (n == 0) {
	pParse->zAfter = z;
	return TH8_OK;
    }

    /*
     * Skip comment (# at command position).
     */

    if (*z == '#') {
	const char *zComStart = z;

	while (n > 0 && *z != '\n') {
	    if (*z == '\\' && n > 1) {
		z++;
		n--;
	    }
	    z++;
	    n--;
	}
	pParse->zComment = zComStart;
	pParse->nComment = (size_t)(z - zComStart);
	/* Loop invariant: the comment-scan loop above exits
	 * either when n drops to 0 or when *z becomes '\n', so
	 * `*z == '\n'` is ALWAYS true here whenever n > 0. */
	if (n > 0 && ALWAYS(*z == '\n')) {
	    z++;
	    n--;
	    nLine++;
	}
	pParse->zAfter = z;
	return TH8_OK;
    }

    zStart = z;
    pParse->zCommand = z;
    pParse->nLine = nLine;

    /*
     * Scan words until end of command (newline, semicolon, or
     * end of input).
     */

    while (n > 0 && *z != ';' && *z != '\n' && *z != '\r') {
	th8NextSpace(interp, z, n, &nSpace);
	z += nSpace;
	n -= nSpace;
	if (n == 0 || *z == ';' || *z == '\n' || *z == '\r') {
	    break;
	}

	rc = th8NextWord(interp, z, n, &nWord, 1);
	if (rc != TH8_OK) goto parse_error;
	if (nWord == 0) break;

	/*
	 * Grow the word array.
	 */

	if (nCount >= nAlloc) {
	    int nNew = nAlloc ? nAlloc * 2 : 8;
	    size_t nReallocBytes;
	    Th8_Value *aNew;

	    if (TH8_SAFE_MUL_SIZE(
	            (size_t)nNew, sizeof(Th8_Value), &nReallocBytes)) {
		rc = TH8_ERROR;
		goto parse_error;
	    }
	    aNew = (Th8_Value *)
	        TH8_ATTEMPT_REALLOC(interp, aWord, nReallocBytes);
	    if (!aNew) {
		rc = TH8_ERROR;
		goto parse_error;
	    }
	    aWord = aNew;
	    nAlloc = nNew;
	}

	/*
	 * Classify the word.
	 */

	{
	    Th8_Value *pW = &aWord[nCount];
	    int isSimple;

	    Th8_Memset(interp, pW, 0, sizeof(Th8_Value));

	    pW->zData = z;
	    pW->nData = nWord;
	    pW->u.token.nLine = nLine;

	    /*
	     * A word is "simple" if it's brace-delimited (no
	     * substitution) or contains no $, [, or \ chars.
	     */

	    isSimple = 1;
	    if (z[0] != '{') {
		size_t j;

		for (j = 0; j < nWord; j++) {
		    if (z[j] == '$' || z[j] == '[' || z[j] == '\\') {
			isSimple = 0;
			break;
		    }
		}
	    }
	    pW->eType = isSimple ? TH8_TOKEN_SIMPLE_WORD : TH8_TOKEN_WORD;
	}

	nCount++;
	z += nWord;
	n -= nWord;
    }

    /*
     * Skip the command terminator.
     */

    if (n > 0 && (*z == ';' || *z == '\n' || *z == '\r')) {
	if (*z == '\n') nLine++;
	z++;
	n--;
    }

    pParse->nCommand = (size_t)(z - zStart);
    pParse->nWord = nCount;
    pParse->aWord = aWord;
    pParse->zAfter = z;
    return TH8_OK;

parse_error:
    if (aWord) Th8_Free(interp, aWord);
    pParse->zAfter = z;
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8FreeParse --
 *
 *	Free the internal token arrays in *pParse that were allocated
 *	by th8ParseCommand or th8ParseExpr.  Does not free the
 *	Th8_Parse struct itself.
 *
 * Why / How:
 *	Iterates over pParse->aWord, freeing any child token arrays
 *	within each word, then frees the word array itself.  The
 *	Th8_Parse struct is typically stack-allocated by the caller,
 *	so only the dynamically-allocated internals are freed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees pParse->aWord and any child arrays within.
 *
 *----------------------------------------------------------------------
 */

void
th8FreeParse(
    Th8_Interp *interp, /* Interpreter (for Th8_Free). */
    Th8_Parse *pParse) /* Parse struct to clean up. */
{
    if (!pParse) return;

    if (pParse->aWord) {
	int i;

	/*
	 * Free any child arrays within individual words.
	 */

	for (i = 0; i < pParse->nWord; i++) {
	    if (pParse->aWord[i].u.token.aChild) {
		Th8_Free(interp, pParse->aWord[i].u.token.aChild);
	    }
	}
	Th8_Free(interp, pParse->aWord);
	pParse->aWord = 0;
    }
    pParse->nWord = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_ByteToUtf16Col --
 *
 *	Convert a byte offset within a UTF-8 line to a UTF-16 column
 *	index.  This is needed for Language Server Protocol (LSP)
 *	integration, where column positions are measured in UTF-16
 *	code units.
 *
 *	Uses the ConvertUTF_v2 library (Unicode Consortium reference
 *	implementation, maintained by Joe Mistachkin) for correct
 *	handling of all UTF-8 sequences including surrogates and
 *	supplementary plane characters.
 *
 * Why / How:
 *	Walks the UTF-8 source one character at a time, converting
 *	each to UTF-16 via ConvertUTF8toUTF16 into a two-element
 *	stack buffer.  BMP characters produce 1 UTF-16 code unit;
 *	supplementary plane characters produce a surrogate pair (2).
 *	The column count accumulates the UTF-16 units produced.
 *	Malformed bytes are treated as 1 UTF-16 unit each.
 *
 * Results:
 *	The 0-based UTF-16 column index corresponding to byte offset
 *	nByte.  Returns nByte if nByte is out of range or conversion
 *	fails.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_ByteToUtf16Col(
    const char *zLine, /* UTF-8 line text. */
    size_t nLine, /* Byte length of line. */
    int nByte) /* Byte offset to convert. */
{
    const UTF8 *pSrc;
    const UTF8 *pSrcEnd;
    UTF16 utf16Buf[2]; /* Room for one code point (max surrogate pair). */
    UTF16 *pDst;
    int col = 0;

    if (nByte < 0) return 0;
    if ((size_t)nByte > nLine) return nByte;
    /* Bug 45 (UBSan): C standard treats `NULL + 0` as
     * undefined behavior even though every real
     * implementation produces NULL.  Th8_ByteToUtf16Col is
     * called with (NULL, 0, 0) from null_guard /
     * plat_wrappers; short-circuit before the pointer math
     * so UBSan stays clean. */
    if (!zLine || nByte == 0) return 0;

    pSrc = (const UTF8 *)zLine;
    pSrcEnd = pSrc + (size_t)nByte;

    /*
     * Walk the UTF-8 source one character at a time.
     * For each character, convert to UTF-16 into a small
     * buffer and count the number of UTF-16 code units
     * produced (1 for BMP, 2 for supplementary plane).
     */

    while (pSrc < pSrcEnd) {
	const UTF8 *pCharStart = pSrc;
	ConversionResult result;

	pDst = utf16Buf;
	result = ConvertUTF8toUTF16(
	    &pSrc, pSrcEnd, &pDst, &utf16Buf[2], strictConversion);

	if (result != conversionOK) {
	    /*
	     * Malformed UTF-8: treat the byte as a single
	     * character (1 UTF-16 unit) and advance past it.
	     */

	    if (pSrc == pCharStart) {
		pSrc++; /* Avoid infinite loop. */
	    }
	    col += 1;
	} else {
	    /*
	     * Count the UTF-16 code units produced.
	     */

	    col += (int)(pDst - utf16Buf);
	}
    }
    return col;
}


/* Helper macro used by Th8_MergePlatform: copy `pSrc->field`
 * into `pDst->field` only when the destination slot is NULL.
 * Defined directly above the function so the per-function
 * header below sits immediately before the function body. */
#define MERGE_SLOT(field)                                                    \
    do {                                                                     \
	if (!pDst->field) pDst->field = pSrc->field;                         \
    } while (0)

/*
 *----------------------------------------------------------------------
 *
 * Th8_MergePlatform --
 *
 *	Merge `*pSrc` into `*pDst` slot by slot: for every
 *	callback field, if the corresponding slot in `pDst` is
 *	NULL, copy the value from `pSrc`.  Non-NULL slots in
 *	`pDst` are preserved (the destination wins).  The
 *	`pCtx` pointer is NOT merged (it belongs to the owner of
 *	`pDst`).
 *
 *	This is the embedder-side composition primitive that
 *	lets a host build up a `Th8_Platform` from multiple
 *	modules without forcing them to know about each other:
 *
 *	    Th8_Platform plat = *Th8_GetPosixPlatform();
 *	    Th8_MergePlatform(&plat, Th8_GetLibcPlatform());
 *
 *	The merge specifically excludes `xInitialize` and
 *	`xFinalize` -- those are per-platform-instance lifecycle
 *	callbacks that must not be overwritten by composition.
 *
 * Parameters:
 *	pDst -- target platform; modified in place.  Caller owns
 *	        the storage.  Must be non-NULL and previously
 *	        initialised (at least `nVersion` set).
 *	pSrc -- source platform; read-only.  Must be non-NULL and
 *	        have the same `nVersion` as `pDst`.
 *
 * Returns:
 *	`TH8_OK` on a successful merge.
 *	`TH8_ERROR` if `pDst->nVersion != pSrc->nVersion`
 *	(struct-layout mismatch; merge would corrupt offsets).
 *
 * Side effects:
 *	NULL slots in `*pDst` are filled from `*pSrc`.
 *	`xInitialize`, `xFinalize`, and `pCtx` are NOT touched.
 *
 *----------------------------------------------------------------------
 */
int
Th8_MergePlatform(
    Th8_Platform *pDst, /* Target platform (modified). */
    const Th8_Platform *pSrc) /* Source platform (read-only). */
{
    /*
     * Version check: both platforms must have the same
     * nVersion to ensure struct layout compatibility.
     * If the versions differ, do not merge - the field
     * offsets may not match.
     */

    if (pDst->nVersion != pSrc->nVersion) {
	return TH8_ERROR;
    }

    /*
     * xInitialize and xFinalize are intentionally NOT merged.
     * They are per-platform-instance lifecycle callbacks and
     * must not be overwritten by a merge operation.
     */

    /* Lifecycle (xPreDeleteInterp/xDeleteInterp -- init/final excluded above) */
    MERGE_SLOT(xPreDeleteInterp);
    MERGE_SLOT(xDeleteInterp);

    /* Memory allocation */
    MERGE_SLOT(xMalloc);
    MERGE_SLOT(xRealloc);
    MERGE_SLOT(xFree);
    MERGE_SLOT(xMemorySize);
    MERGE_SLOT(xNeedMemory);

    /* Memory operations */
    MERGE_SLOT(xMemcpy);
    MERGE_SLOT(xMemmove);
    MERGE_SLOT(xMemset);
    MERGE_SLOT(xMemcmp);

    /* String / utility */
    MERGE_SLOT(xStrlen);
    MERGE_SLOT(xStrcmp);
    MERGE_SLOT(xStrchr);
    MERGE_SLOT(xAtoi);
    MERGE_SLOT(xQsort);
    MERGE_SLOT(xVsnprintf);

    /* Mutex / synchronization */
    MERGE_SLOT(xMutexInit);
    MERGE_SLOT(xMutexFinal);
    MERGE_SLOT(xMutexEnter);
    MERGE_SLOT(xMutexLeave);
    MERGE_SLOT(xIntCmpXchg);
    MERGE_SLOT(xMemBarrier);

    /* Manual-reset event handle (per-interp event queue) */
    MERGE_SLOT(xEventCreate);
    MERGE_SLOT(xEventDestroy);
    MERGE_SLOT(xEventSet);
    MERGE_SLOT(xEventReset);
    MERGE_SLOT(xEventWait);

    /* Standard I/O */
    MERGE_SLOT(xInput);
    MERGE_SLOT(xOutput);
    MERGE_SLOT(xOutputError);

    /* Channel accessors */
    MERGE_SLOT(xGetInput);
    MERGE_SLOT(xSetInput);
    MERGE_SLOT(xGetOutput);
    MERGE_SLOT(xSetOutput);
    MERGE_SLOT(xGetErrorOutput);
    MERGE_SLOT(xSetErrorOutput);
    MERGE_SLOT(xChannelControl);

    /* Temporary data */
    MERGE_SLOT(xGetTemporaryData);
    MERGE_SLOT(xDeleteTemporaryData);
    MERGE_SLOT(xSetTemporaryData);
    MERGE_SLOT(xCloseTemporaryData);

    /* Filesystem / paths */
    MERGE_SLOT(xNormalizePath);
    MERGE_SLOT(xGetCwd);
    MERGE_SLOT(xSetCwd);
    MERGE_SLOT(xGetExePath);
    MERGE_SLOT(xGetRealPath);
    MERGE_SLOT(xGetRootPath);
    MERGE_SLOT(xSameFile);

    /* Shared objects / plugins */
    MERGE_SLOT(xGetData);
    MERGE_SLOT(xDataExists);
    MERGE_SLOT(xLoad);
    MERGE_SLOT(xUnload);

    /* Time / sleep */
    MERGE_SLOT(xTimeMs);
    MERGE_SLOT(xTimeUs);
    MERGE_SLOT(xSleep);

    /* Environment queries */
    MERGE_SLOT(xGetPid);
    MERGE_SLOT(xGetUserName);
    MERGE_SLOT(xGetHostName);
    MERGE_SLOT(xGetEnv);
    MERGE_SLOT(xKeyValue);
    MERGE_SLOT(xGetStackBounds);
    MERGE_SLOT(xGetParentPid);
    MERGE_SLOT(xGetThreadId);
    MERGE_SLOT(xGetLastError);
    MERGE_SLOT(xSetLastError);

    /* Diagnostics / misc */
    MERGE_SLOT(xEmitTrace);
    MERGE_SLOT(xPanic);
    MERGE_SLOT(xMathFunc);
    MERGE_SLOT(xRandomBytes);

    /* DNS (DNSSEC-validating resolver, libunbound on POSIX) */
    MERGE_SLOT(xDnsResolve);
    MERGE_SLOT(xDnsResolveFree);

    /* Diagnostics */
    MERGE_SLOT(xStackBackTrace);

    /* 64-bit atomics */
    MERGE_SLOT(xIntCmpXchg64);
    return TH8_OK;
}

#undef MERGE_SLOT


/*
 *----------------------------------------------------------------------
 *
 * Th8_ClonePlatform --
 *
 *	Allocate a new mutable copy of a const platform table.
 *	Uses calloc (not Th8_Malloc, since no interpreter exists
 *	yet when platforms are being assembled).
 *
 * Why / How:
 *	Allocates via the source platform's own xMalloc callback
 *	(with a NULL interp, since no interpreter exists yet), then
 *	struct-copies all fields.  The clone can be freely modified
 *	without affecting the original.  Used by
 *	Th8_MergePlatformInterp to create per-interpreter overrides.
 *
 *----------------------------------------------------------------------
 */

Th8_Platform *
Th8_ClonePlatform(const Th8_Platform *pSrc)
{
    Th8_Platform *pNew;

    if (!pSrc || !pSrc->xMalloc) return NULL;
    pNew = (Th8_Platform *)
               pSrc->xMalloc(NULL, pSrc->pCtx, sizeof(Th8_Platform));
    if (pNew) {
	*pNew = *pSrc;
    }
    return pNew;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FreePlatform --
 *
 *	Free a platform table allocated by Th8_ClonePlatform.
 *
 * Why / How:
 *	Frees the platform struct via its own xFree callback with a
 *	NULL interp (since the platform may outlive any interpreter).
 *	Safe to call with NULL.  Must only be used on cloned
 *	platforms, not on the original static platform.
 *
 *----------------------------------------------------------------------
 */

void
Th8_FreePlatform(Th8_Platform *pPlatform)
{
    if (pPlatform && pPlatform->xFree) {
	pPlatform->xFree(NULL, pPlatform->pCtx, pPlatform);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_MergePlatformInterp --
 *
 *	Merge the callbacks from pSrc into the interpreter's current
 *	platform.  Clones the current platform, merges pSrc into it,
 *	and replaces the interpreter's platform pointer.  If a
 *	previous clone exists (from an earlier call), it is freed.
 *
 * Why / How:
 *	Clones the interpreter's current platform via
 *	Th8_ClonePlatform, merges pSrc into the clone via
 *	Th8_MergePlatform, then swaps the interpreter's platform
 *	pointer.  The bPlatformCloned flag tracks ownership so
 *	Th8_DeleteInterp knows to free the clone.
 *
 *----------------------------------------------------------------------
 */

int
Th8_MergePlatformInterp(
    Th8_Interp *interp, /* Interpreter to modify. */
    const Th8_Platform *pSrc) /* Source platform to merge from. */
{
    Th8_Platform *pClone;

    /* Split per Finding 005. */
    if (!interp) return TH8_ERROR;
    if (!pSrc) return TH8_ERROR;

    pClone = Th8_ClonePlatform(interp->pPlatform);
    if (!pClone) return TH8_ERROR;

    if (Th8_MergePlatform(pClone, pSrc) != TH8_OK) {
	Th8_FreePlatform(pClone);
	return TH8_ERROR;
    }

    /*
     * Free the previous clone if we own it.
     */

    if (interp->bPlatformCloned) {
	Th8_FreePlatform(interp->pPlatform);
    }

    interp->pPlatform = pClone;
    interp->bPlatformCloned = 1;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_CreateInterp --
 *
 *	Create a new TH8 interpreter.
 *
 *	INITIALIZATION ORDER:
 *
 *	  1. Single allocation: Th8_Interp + Th8_Frame (global frame)
 *	     are allocated in one xMalloc call and zero-filled.  This
 *	     guarantees the global frame is never separately freed.
 *	  2. Platform pointer is stored (not owned).
 *	  3. The global frame (at &interp[1]) is pushed as the
 *	     initial call frame.
 *	  4. The global namespace "::" is created with its three
 *	     hashes (paCmd, paVar, paChild).  pCurrentNs is set to
 *	     point at it.
 *	  5. The package registry hash is created.
 *	  6. Standard global variables are set (th8InitGlobals):
 *	     ::tcl_platform(engine), ::argv, ::errorInfo, etc.
 *	  7. Stack checking is initialized if the platform provides
 *	     xGetStackBounds.  Growth direction is detected.
 *
 * Why / How:
 *	The interpreter and its global frame are allocated as a
 *	single block to ensure the global frame is never separately
 *	freed.  Initialization proceeds in a strict order: platform
 *	wiring, frame push, namespace creation, package registry,
 *	global variables, stack guard, cache, and security.  Each
 *	step depends on the prior ones being complete.
 *
 *----------------------------------------------------------------------
 */

/*
 * Global library state.  th8Initialized, th8GlobalMutex, and
 * th8GlobalMutexReady are bootstrap state: they are set by
 * Th8_Initialize and cleared by Th8_Finalize, both of which
 * MUST be called from a single thread.  After initialization,
 * th8GlobalPlatform is read-only and th8GlobalMutex is accessed
 * only through th8GlobalMutexEnter/Leave (in th8_plat.c).
 */

static int th8Initialized = 0;
Th8_Mutex th8GlobalMutex;
int th8GlobalMutexReady = 0;
Th8_Platform th8GlobalPlatform;

/*
 *----------------------------------------------------------------------
 *
 * Th8_Initialize --
 *
 *	Initialize the TH8 library-wide global state.  Must be called
 *	exactly once from a single thread before any Th8_CreateInterp
 *	call.  Subsequent calls return TH8_ERROR.
 *
 * Why / How:
 *	Copies the platform function table into the global
 *	th8GlobalPlatform, calls xInitialize if provided, initializes
 *	the global mutex, and sets the current working directory to
 *	the base path (".").  An atomic CAS on th8Initialized prevents
 *	double-initialization.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if already initialized or if
 *	xInitialize or xSetCwd fails.
 *
 * Side effects:
 *	th8GlobalPlatform is populated; th8GlobalMutex is initialized;
 *	xSetCwd may change the process working directory;
 *	th8Initialized is set to 1.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Initialize(Th8_Platform *pPlatform) /* Platform (for mutex callbacks). */
{
    if (Th8_IntCmpXchg(NULL, &th8Initialized, 0, 0)) {
	TH8_TRACE_ERR(NULL, "already initialized");
	return TH8_ERROR; /* Already initialized. */
    }
    /*
     * Register the calling (main) thread with TH8's allocator BEFORE
     * the platform's xInitialize runs.  On mimalloc builds this
     * creates the main thread's dedicated heap and primes mimalloc's
     * per-thread state, so any allocations during xInitialize land
     * on the correct heap.  Worker threads call Th8_ThreadInit
     * themselves at thread entry.                                  */
    Th8_ThreadInit();
    if (pPlatform) {
	if (pPlatform->xInitialize) {
	    if (pPlatform->xInitialize(NULL, pPlatform->pCtx) != TH8_OK) {
		TH8_TRACE_ERR(NULL, "xInitialize callback failed");
		return TH8_ERROR;
	    }
	}
	th8GlobalPlatform = *pPlatform;
	if (Th8_IntCmpXchg(NULL, &th8GlobalMutexReady, 1, 0) == 0 &&
	    pPlatform->xMutexInit) {
	    pPlatform->xMutexInit(NULL, pPlatform->pCtx, &th8GlobalMutex);
	    th8MemBarrier(NULL);
	}

	/*
	 * Set (or reset) the current working directory to the
	 * base directory.  This establishes "." as the base path
	 * for all subsequent file system operations.  If xSetCwd
	 * is not available, this step is silently skipped.  If
	 * xSetCwd is available but fails, initialization fails.
	 */

	if (pPlatform->xSetCwd) {
	    if (pPlatform->xSetCwd(NULL, pPlatform->pCtx, ".", 1) != TH8_OK) {
		TH8_TRACE_ERR(NULL, "xSetCwd callback failed");
		return TH8_ERROR;
	    }
	}
    }
    Th8_IntCmpXchg(NULL, &th8Initialized, 1, 0);
    return TH8_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Th8_Finalize --
 *
 *	Tear down the TH8 library-wide global state.  Must be called
 *	exactly once from a single thread after all interpreters have
 *	been deleted.  Subsequent calls return TH8_ERROR.
 *
 * Why / How:
 *	Single-threaded by contract (same as Initialize).  The global
 *	mutex is destroyed, xFinalize is called if the platform
 *	provided one, and th8Initialized is cleared via CAS.  The
 *	global platform is zeroed to prevent stale use.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the library was not
 *	initialized.
 *
 * Side effects:
 *	th8GlobalMutex is finalized; th8GlobalPlatform is zeroed;
 *	th8Initialized is set to 0.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Finalize(Th8_Platform
                 *pPlatform) /* Platform (reserved for future use). */
{
    (void)pPlatform;
    if (!Th8_IntCmpXchg(NULL, &th8Initialized, 0, 0)) {
	return TH8_ERROR; /* Not initialized. */
    }

    /*
     * Finalize is single-threaded by contract (same as Initialize).
     * The global-platform reads below are NOT mutex-protected because
     * we are about to destroy the mutex itself.  The CAS on
     * th8GlobalMutexReady ensures no concurrent mutex operations.
     */

    if (Th8_IntCmpXchg(NULL, &th8GlobalMutexReady, 0, 1) == 1 &&
        th8GlobalPlatform.xMutexFinal) {
	th8GlobalPlatform
	    .xMutexFinal(NULL, th8GlobalPlatform.pCtx, &th8GlobalMutex);
    }
    if (th8GlobalPlatform.xFinalize) {
	th8GlobalPlatform.xFinalize(NULL, th8GlobalPlatform.pCtx);
    }
    /*
     * Tear down the calling (main) thread's per-thread allocator
     * state.  Symmetric with the Th8_ThreadInit call at the top
     * of Th8_Initialize.                                        */
    Th8_ThreadDone();
    Th8_IntCmpXchg(NULL, &th8Initialized, 0, 1);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8GlobalMutexEnter / th8GlobalMutexLeave --
 *
 *	Enter and leave the process-global mutex.  No-ops if
 *	no mutex callbacks were provided (single-threaded mode).
 *	These are called by platform code (e.g., th8_posix.c) to
 *	protect global state like the loaded-library handle list.
 *
 *----------------------------------------------------------------------
 */


/*
 *----------------------------------------------------------------------
 *
 * Th8_CreateInterp --
 *
 *	Create a new TH8 interpreter.
 *
 *	The caller must provide a Th8_Platform* that remains valid
 *	for the interpreter's entire lifetime.  TH8 does not copy
 *	the platform struct.
 *
 * Why / How:
 *	See the detailed initialization order in the earlier comment
 *	block for Th8_CreateInterp.  This is the public entry point
 *	that performs all seven steps: allocation, platform wiring,
 *	frame push, namespace/package hash creation, globals, stack
 *	guard, and cache/security initialization.
 *
 * Results:
 *	Pointer to the new interpreter, or NULL on failure.
 *
 * Side effects:
 *	Allocates the interpreter, global frame, global namespace,
 *	and package registry.  Initializes standard global variables.
 *
 *----------------------------------------------------------------------
 */

Th8_Interp *
Th8_CreateInterp(Th8_Platform
                     *pPlatform) /* Platform abstraction (not owned). */
{
    size_t nByte;
    Th8_Interp *p;
    Th8_Frame *pGlobalFrame;

    /*
     * STEP 1: Allocate interpreter + global frame as a single block.
     * The global frame is NOT heap-allocated separately; it lives
     * in the bytes immediately following the Th8_Interp struct.
     * This means Th8_DeleteInterp must NOT call Th8_Free on it.
     */

    nByte = sizeof(Th8_Interp) + sizeof(Th8_Frame);
    p = (Th8_Interp *)pPlatform->xMalloc(NULL, pPlatform->pCtx, nByte);
    if (!p) {
	return 0;
    }
    if (pPlatform->xMemset) {
	pPlatform->xMemset(NULL, pPlatform->pCtx, p, 0, nByte);
    }
    p->nVersion = 1; /* Pre-RTM: single ABI version. */
    {
#ifdef TH8_DECLS_H
	extern const Th8StubsTable th8StubsTableData;
#else
	extern const void *th8StubsTableData;
#endif

	p->pStubs = &th8StubsTableData;
    }
    p->pPlatform = pPlatform;
    th8SeedHash(pPlatform);

    /*
     * STEP 2: The global frame lives right after the interpreter
     * struct.  Push it as the bottom of the call stack.
     */

    pGlobalFrame = (Th8_Frame *)&p[1];
    th8PushFrame(p, pGlobalFrame);

    /*
     * STEP 3: Create the global namespace "::".
     * All three hashes are created eagerly (even if initially
     * empty) so that command/variable/child lookups never need
     * NULL checks.
     */

    p->pGlobalNs = (Th8_Namespace *)TH8_ALLOC(p, sizeof(Th8_Namespace));
    if (!p->pGlobalNs) {
	Th8_Free(p, p);
	return 0;
    }
    p->pGlobalNs->zName = (char *)TH8_ALLOC(p, 3);
    if (!p->pGlobalNs->zName) {
	Th8_Free(p, p->pGlobalNs);
	Th8_Free(p, p);
	return 0;
    }
    Th8_Memcpy(p, p->pGlobalNs->zName, "::", 3);
    p->pGlobalNs->nName = 2;
    p->pGlobalNs->pParent = 0;
    p->pGlobalNs->paCmd = Th8_HashNew(p);
#if defined(TH8_ENABLE_VARIABLES)
    p->pGlobalNs->paVar = Th8_HashNew(p);
#endif
    p->pGlobalNs->paChild = Th8_HashNew(p);
    p->pCurrentNs = p->pGlobalNs;
    pGlobalFrame->pNs = p->pGlobalNs;

    /*
     * STEP 4: Package registry.
     */

    p->paPackage = Th8_HashNew(p);

    /*
     * STEP 5: Standard global variables.
     */

#if defined(TH8_ENABLE_VARIABLES)
    th8InitGlobals(p);
#endif

    /*
     * STEP 6: Native stack checking.
     *
     * Query the platform for the thread's stack base and size.
     * If available, detect the stack growth direction by comparing
     * the address of a local variable to the reported base.
     * This enables th8CheckStack to prevent C stack overflow
     * during deeply recursive scripts.
     */

    p->nResultLimit = 0; /* 0 = use TH8_MX_STRLEN */
    p->nStackGuard = 65536; /* 64KB safety margin */
    p->bStackCheckEnabled = 0;
    p->bOverflowCheck = 1; /* Secure default: overflow = error. */

    /*
     * STEP 6.5: Event queue.  The queue itself is per-pState
     * (see Th8_CreateAsyncState); the interp only needs to
     * carry the registry list head.  Initialize it to empty.
     */

    p->pAsyncStateHead = 0;

#if defined(TH8_ENABLE_LOAD)
    /*
     * Generate the load token from secure random bytes.
     * Ensure it's never zero (zero means disabled).
     */

    p->nLoadToken = 0;
    p->nLoadOk = 0;
    if (pPlatform->xRandomBytes) {
	int retries = 0;
	unsigned char buf[8];

retry:

	if (++retries > 100) {
	    TH8_TRACE_ERR(p, "could not generate first load token");
	    pPlatform->xPanic(
	        NULL, NULL, "could not generate first load token", 35);
	}

	if (TH8_OK == pPlatform->xRandomBytes(NULL, NULL, buf, 8)) {
	    size_t j;
	    th8_int64_t tok = 0;

	    for (j = 0; j < 8; j++) {
		tok |= ((th8_uint64_t)buf[j]) << (j * 8);
	    }
	    if (tok == 0 || tok == ~0 || tok == 1)
		goto retry; /* Never zero. */
	    p->nLoadToken = tok;
	}
    }
#endif /* TH8_ENABLE_LOAD */
    if (pPlatform->xGetStackBounds) {
	void *pBase = 0;
	size_t nSize = 0;

	if (TH8_OK ==
	    pPlatform
	        ->xGetStackBounds(NULL, pPlatform->pCtx, &pBase, &nSize)) {
	    volatile char localProbe;

	    p->pStackBase = pBase;
	    p->nStackSize = nSize;
	    p->bStackCheckEnabled = 1;

	    /*
	     * Detect growth direction: if our local is below
	     * the reported base, the stack grows downward.
	     */

	    if ((char *)pBase > &localProbe) {
		p->bStackGrowsDown = 1;
	    } else {
		p->bStackGrowsDown = 0;
	    }
	}
    }

    /*
     * STEP 6b: Capture the owning-thread id.  The creating thread owns
     * this interpreter for its entire lifetime; every subsequent API
     * call (except the documented thread-safe exceptions) MUST be made
     * on this thread.  Publish it atomically via the 64-bit interlocked
     * CAS so foreign threads that legally read it (Th8_GetInterpThreadId,
     * TH8_ASSERT_OWNER) always observe a consistent value.  threadId
     * stays 0 on hosts whose platform provides no xGetThreadId, which
     * disables affinity checking (single-threaded assumption).
     */

    Th8_Int64CmpXchg(p, &p->threadId, Th8_GetThreadId(p), 0);

    /*
     * STEP 7: Internal-representation cache.
     */

    th8CacheInit(p);

#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
    /* Non-fatal: interpreter works without secure variables. */
    (void)th8SecureInit(p);
#endif

    /* Plugin system: no plugins registered, token counter starts at 1. */
    p->pPlugins = 0;
    p->nNextCmdToken = 1;
    p->paCmdToken = 0; /* Created lazily on first CreateCommand. */

    return p;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_GetInterpThreadId --
 *
 *	Return the id of the thread that owns the interpreter (the
 *	thread that called Th8_CreateInterp).  Read atomically via the
 *	64-bit interlocked compare-exchange.
 *
 * Why / How:
 *	The owning-thread id enforces the single-threaded-per-
 *	interpreter affinity contract.  A foreign thread MAY call this
 *	safely (it is one of the documented thread-safe exceptions) to
 *	discover the owner and compare it against Th8_GetThreadId (its
 *	own thread).  The read uses Th8_Int64CmpXchg(...,0,0), a non-
 *	mutating compare-with-0 that returns the current value.
 *
 * Results:
 *	The owning-thread id, or 0 if it was never captured (the
 *	platform provides no xGetThreadId).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

th8_uint64_t
Th8_GetInterpThreadId(Th8_Interp *interp) /* Interpreter. */
{
    if (!interp) return 0;
    return Th8_Int64CmpXchg(interp, &interp->threadId, 0, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * th8CheckThreadOwner --
 *
 *	Return non-zero if the calling thread is permitted to operate
 *	on the interpreter under the single-threaded-per-interpreter
 *	affinity contract.  Used only by the TH8_ASSERT_OWNER debug
 *	assertion.
 *
 * Why / How:
 *	Compares the owning-thread id (captured in Th8_CreateInterp)
 *	against the caller's current thread id, both read atomically.
 *	Returns 1 (permitted) when either id is 0 -- i.e. the platform
 *	cannot report thread ids -- so affinity is simply not enforced
 *	on such hosts rather than falsely tripping.  A NULL interp is
 *	also treated as permitted (callers handle NULL elsewhere).
 *
 * Results:
 *	1 if the caller owns interp or affinity cannot be enforced;
 *	0 if the caller is provably on a foreign thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
th8CheckThreadOwner(Th8_Interp *interp) /* Interpreter. */
{
    th8_uint64_t owner;
    th8_uint64_t self;

    if (!interp) return 1;
    owner = Th8_GetInterpThreadId(interp);
    if (owner == 0) return 1; /* Affinity not trackable on this host. */
    self = Th8_GetThreadId(interp);
    if (self == 0) return 1; /* Current thread id unavailable. */
    return owner == self;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DeleteInterp --
 *
 *	Destroy an interpreter and free all associated resources.
 *
 *	CLEANUP ORDER (mirrors creation in reverse):
 *
 *	  1. Pop the global frame -- frees all frame-level variables
 *	     (but does NOT free the frame struct itself, since it is
 *	     part of the single-allocation block).
 *	  2. Free the interpreter result and cancel message.
 *	  3. Free the package unknown handler string.
 *	  4. Free the global namespace (th8FreeNamespace recursively
 *	     frees all child namespaces, all commands with their
 *	     xDel destructors, and all namespace variables).
 *	  5. Free the package registry hash.
 *	  6. Drain any remaining NRE callbacks (without invoking
 *	     them -- the interpreter is shutting down).
 *	  7. Free the interpreter struct itself (which includes the
 *	     global frame) via the platform's xFree.
 *
 *	After Th8_DeleteInterp returns, the interpreter pointer is
 *	invalid and must not be used.
 *
 * Why / How:
 *	Cleanup proceeds in reverse creation order to ensure that
 *	each resource is freed only after all its dependents.  The
 *	xFree pointer and context are captured before cleanup begins
 *	because the platform table may be freed during STEP 8.
 *	The allocation limit is disabled so that internal cleanup
 *	allocations do not trigger xPanic.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All memory is freed.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8FreeCmdEntry --
 *
 *	Hash iteration callback: free a command entry, invoking its
 *	delete callback if present.
 *
 * Why / How:
 *	Used by th8FreeNamespace during interpreter deletion.
 *	For each command hash entry, removes the command token
 *	from the secondary index, invokes xDel (allowing the
 *	command to release its private context), frees the
 *	qualified name string, and frees the Th8_Command struct.
 *
 * Results:
 *	Always returns TH8_OK (continue iterating).
 *
 * Side effects:
 *	The command's xDel callback is invoked.  The Th8_Command
 *	struct is freed.
 *
 *----------------------------------------------------------------------
 */

static int
th8FreeCmdEntry(
    Th8_HashEntry *pEntry, /* Hash entry to process. */
    void *pCtx) /* Interpreter (as void*). */
{
    Th8_Interp *interp = (Th8_Interp *)pCtx;

    if (pEntry->pData) {
	Th8_Command *pCmd = (Th8_Command *)pEntry->pData;

	th8RemoveCmdTokenEntry(interp, pCmd);
	if (pCmd->xDel) {
	    pCmd->xDel(interp, pCmd->pContext);
	}
	Th8_Free(interp, pCmd->zQualName);
	Th8_Free(interp, pCmd);
    }
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_RestoreInterp --
 *
 *	Re-create built-in state that was destroyed by namespace
 *	deletion or other destructive operations.
 *
 * Why / How:
 *	Selectively re-initializes interpreter subsystems based on
 *	the flags bitmask.  TH8_RESTORE_COMMANDS re-registers all
 *	built-in commands via Th8_RegisterLanguage (which also
 *	registers static plugins).  TH8_RESTORE_VARIABLES re-sets
 *	the standard global variables via th8InitGlobals.
 *
 * Results:
 *	TH8_OK.
 *
 * Side effects:
 *	Re-registers commands and/or variables per the flags.
 *
 *----------------------------------------------------------------------
 */

int
Th8_RestoreInterp(
    Th8_Interp *interp, /* Interpreter. */
    int flags) /* TH8_RESTORE_* bitmask. */
{
    if (!interp) return TH8_ERROR;
    if (flags & TH8_RESTORE_COMMANDS) {
	Th8_RegisterLanguage(interp);
	/* Static plugins (lists, regexp, etc.) are registered
	 * by th8RegisterStaticPlugins inside RegisterLanguage. */
    }
#if defined(TH8_ENABLE_VARIABLES)
    if (flags & TH8_RESTORE_VARIABLES) {
	th8InitGlobals(interp);
    }
#endif
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DeleteInterp --
 *
 *	Destroy an interpreter and free all associated resources.
 *	Cleanup proceeds in reverse order of creation: frame
 *	variables, result, namespaces, packages, NRE callbacks,
 *	and finally the interpreter struct itself.
 *
 * Why / How:
 *	Saves xFree and pCtx before cleanup begins (they point to
 *	read-only code/static data that survive FreePlatform).
 *	Then proceeds through 8 steps in reverse creation order:
 *	pop frame, free result/cancel, free namespaces (with xDel
 *	callbacks), free packages, drain NRE callbacks, free
 *	caches/channels/plugins, notify platform, free the cloned
 *	platform (if any), and finally free the interp block.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All memory is freed.  The interpreter pointer is invalid
 *	after this call.
 *
 *----------------------------------------------------------------------
 */

void
Th8_DeleteInterp(Th8_Interp *interp) /* Interpreter to destroy. */
{
    void (*xFreeInterp)(Th8_Interp *, void *, void *);
    void *pFreeInterpCtx;

    if (!interp) return;

    TH8_ASSERT_OWNER(interp);

    /*
     * Save the free callback and context at the top of cleanup.
     * These function pointers live in read-only code memory and
     * the context points to static platform data, so both survive
     * the entire cleanup sequence including Th8_FreePlatform.
     */

    xFreeInterp = interp->pPlatform->xFree;
    pFreeInterpCtx = interp->pPlatform->pCtx;

    /*
     * Disable the allocation limit during cleanup so that
     * internal temporary allocations (hash iteration, string
     * building) don't trigger xPanic.
     */

    interp->nAllocLimit = 0;

    /*
     * STEP 0: Notify the platform that the interpreter is about
     * to be deleted, while it is still fully functional.  This
     * is the place where loaded libraries' _Unload entry points
     * fire -- they may call back into the interp via Th8_Eval,
     * Th8_DeleteMathFunc, namespace deletion, etc.  The matching
     * platform-handle release (dlclose / FreeLibrary) is deferred
     * to STEP 7 so that any library xDel callbacks invoked during
     * namespace cleanup can still run the library's text segment.
     */

    th8NotifyPreDeleteInterp(interp, NULL);

    /*
     * STEP 0a: Mark every registered Th8_AsyncState as
     * "deleted".  Cross-thread Th8_QueueEvent callers will
     * observe nDeleted != 0 via xIntCmpXchg and bail out
     * cleanly with TH8_ERROR.  We also clear each pInterp so
     * any worker that already passed the nDeleted check but
     * hasn't deref'd pInterp gets NULL (and bails on the
     * second check inside Th8_QueueEvent).  pStates are NOT
     * freed here -- the embedder owns them and must call
     * Th8_FinalizeAsyncState on each one eventually.  See
     * the plan for the full thread-safety contract.
     */

    {
	Th8_AsyncStateNode *pNode = interp->pAsyncStateHead;
	Th8_Platform *pPlat = interp->pPlatform;
	while (pNode) {
	    Th8_AsyncStateNode *pNext = pNode->pNext;
	    /*
	     * Atomically check if Th8_FinalizeAsyncState ran
	     * before us.  If so, pState is already freed; do
	     * NOT touch it.  Otherwise pState is alive: mark
	     * it as deleted and clear its back-pointer so a
	     * later Finalize call sees pNode==NULL and skips
	     * the flag-set step (we're about to free pNode).
	     */
	    int isFinalized = 0;
	    /* For a pNode to exist on this list, an async state
	     * must have been registered via th8EventQueueAvailable,
	     * which validates xIntCmpXchg via TH8_CHECK_EVENT_CALLBACKS
	     * before creating the pState/pNode pair.  The platform is
	     * installed once and never replaced, so pPlat->xIntCmpXchg
	     * is ALWAYS non-NULL while iterating pAsyncStateHead. */
	    if (ALWAYS(pPlat && pPlat->xIntCmpXchg)) {
		isFinalized = pPlat->xIntCmpXchg(
		    interp, pPlat->pCtx, &pNode->nFinalized, 0, 0);
	    } else {
		isFinalized = pNode->nFinalized;
	    }
	    if (!isFinalized) {
		Th8_AsyncState *pAS = pNode->pState;
		if (ALWAYS(pPlat && pPlat->xIntCmpXchg)) {
		    pPlat->xIntCmpXchg(
		        interp, pPlat->pCtx, &pAS->nDeleted, 1, 0);
		} else {
		    pAS->nDeleted = 1;
		}
		pAS->pInterp = NULL;
		pAS->pNode = NULL;
	    }
	    /* Free the node regardless (interp owns it).
	     * pState struct is NOT freed here -- embedder owns
	     * its lifetime via Th8_FinalizeAsyncState.         */
	    Th8_Free(interp, pNode);
	    pNode = pNext;
	}
	interp->pAsyncStateHead = NULL;
    }
    th8MemBarrier(interp);

    /*
     * STEP 0b: (intentionally minimal under the per-pState
     * design.)
     *
     * Each Th8_AsyncState owns its own event queue, signal
     * handle, and queue mutex.  Those resources live until
     * the embedder calls Th8_FinalizeAsyncState on the
     * matching pState; STEP 0a above marked every pState as
     * deleted (nDeleted=1, pInterp=NULL) so any in-flight
     * Th8_QueueEvent will bail and any not-yet-drained
     * callbacks will be dropped silently when the embedder
     * eventually finalizes.  No per-interp queue cleanup is
     * needed here.
     */

    /*
     * STEP 1: Pop the global frame.
     * This frees all frame-level variables (via th8FreeVarEntry)
     * and the frame's variable hash.  The frame struct itself is
     * NOT freed here because it is part of the single allocation
     * block (it lives at &interp[1]).
     */

    th8PopFrame(interp);

    /*
     * STEP 1b: Drain any deferred command/namespace deletions
     * that were queued during eval but never drained (e.g. if
     * the interpreter is deleted without returning to the
     * trampoline).
     */

    th8DrainPendingDeletes(interp);

    /*
     * STEP 2: Free the interpreter result string and the
     * cancel message (if any).  Also free the package unknown
     * handler string.
     */

    Th8_SetResult(interp, 0, 0);
    if (interp->zSavedCancelMsg) {
	Th8_Free(interp, interp->zSavedCancelMsg);
	interp->zSavedCancelMsg = 0;
    }
    if (interp->bCancelMsgOwned) {
	Th8_Free(interp, interp->zCancelMsg);
    }
    interp->zCancelMsg = 0;
    interp->bCancelMsgOwned = 0;
    Th8_Free(interp, interp->zPkgUnknown);

    /*
     * STEP 3: Free global namespace.
     * th8FreeNamespace recursively frees all child namespaces,
     * invokes xDel on every command, and frees all namespace
     * variables and hashes.
     */

    th8FreeNamespace(interp, interp->pGlobalNs);
    interp->pGlobalNs = 0;
    interp->pCurrentNs = 0;

    /*
     * STEP 4: Free the package registry hash.
     * Each entry's pData points to a Th8_PkgInfo (allocated in
     * th8_lang.c) containing a version string and an ifneeded
     * sub-hash.  th8CleanupPackages iterates and frees all of
     * these before Th8_HashDelete releases the entries.
     */

#if defined(TH8_PLUGIN_EXTENSIBILITY)
    th8CleanupPackages(interp);
#endif
    Th8_HashDelete(interp, interp->paPackage);

    /*
     * Clean up math function registry.
     */

    if (interp->paMathFunc) {
	Th8_HashIterate(
	    interp, interp->paMathFunc, th8FreeDataEntry, (void *)interp);
	Th8_HashDelete(interp, interp->paMathFunc);
    }

    /*
     * Clean up array search registry ([array startsearch]).
     * Each entry's pData is a th8ArraySearch* owned by the
     * variables plugin; free via th8ArraySearchFreeEntry which
     * releases the entry's storage.  Gated on
     * TH8_PLUGIN_VARIABLES because the helper lives in
     * src/plugins/th8_variables.c -- without the plugin
     * compiled in, paArraySearch is never populated
     * (th8GetArraySearchHash is only reachable from the
     * variables plugin) and the symbol is undefined at link
     * time.  Bug 35: build-matrix coupling fix.
     */

#if defined(TH8_PLUGIN_VARIABLES)
    if (interp->paArraySearch) {
	Th8_HashIterate(
	    interp, interp->paArraySearch, th8ArraySearchFreeEntry,
	    (void *)interp);
	Th8_HashDelete(interp, interp->paArraySearch);
    }
#endif

    /*
     * STEP 5: Drain any remaining NRE callbacks WITHOUT invoking
     * them.  During normal shutdown there should be none, but if
     * the interpreter is being destroyed after an error or
     * cancellation, some may remain.
     */

    while (interp->pCallbacks) {
	Th8_Callback *pCb = interp->pCallbacks;

	interp->pCallbacks = pCb->pNext;
	Th8_Free(interp, pCb);
    }

    /*
     * STEP 6: System variable hash and cache.
     */

#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
    th8SecureFinish(interp);
#endif

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /*
     * Free the per-interp protected backing region used by
     * Th8_SetResultSensitive.  Must run AFTER any final result
     * teardown has cleared zResult (the result-management code
     * above secure-zeroes the data area in place when the result
     * is sensitive; here we free the region itself).
     */

    if (interp->pProtectedResult) {
	Th8_ProtectedRegion *pPR = (Th8_ProtectedRegion *)
	                               interp->pProtectedResult;
	th8ProtectedFree(interp, pPR);
	Th8_Free(interp, pPR);
	interp->pProtectedResult = NULL;
    }
#endif

    if (interp->paSystemVar) {
	Th8_HashDelete(interp, interp->paSystemVar);
    }
    th8CacheFinish(interp);

    /*
     * STEP 6c: Close all temporary file channels and delete
     * the underlying files via xDeleteTemporaryData.
     */

    th8ChannelCleanup(interp);

    /*
     * Free the finally-result buffer.
     */

    Th8_Free(interp, interp->zFinallyResult);
    interp->zFinallyResult = 0;

#if defined(TH8_ENABLE_LOAD)
    /*
     * STEP 6b: Free the loaded-library name list.
     */

    {
	extern void th8FreeLoadedLibs(Th8_Interp *);
	th8FreeLoadedLibs(interp);
    }
#endif

    /*
     * STEP 6c: Free the plugin registry.
     * Commands were already freed by namespace cleanup (STEP 3);
     * this only frees the plugin metadata.
     */

    th8PluginCleanup(interp);

    /*
     * STEP 6d: Free the command token secondary index.
     * The Th8_Command* pointers it held were already freed
     * during namespace cleanup (STEP 3).  Just delete the
     * hash structure itself.
     */

    if (interp->paCmdToken) {
	Th8_HashDelete(interp, interp->paCmdToken);
	interp->paCmdToken = 0;
    }

    /*
     * STEP 6e: Free the per-callback context hash.
     * The pData pointers belong to extensions and are NOT freed
     * here (ownership is with the extension's _Unload).
     */

    if (interp->paCallbackCtx) {
	Th8_HashDelete(interp, interp->paCallbackCtx);
	interp->paCallbackCtx = 0;
    }

    if (interp->paBreakpoints) {
	Th8_HashDelete(interp, interp->paBreakpoints);
	interp->paBreakpoints = 0;
    }

    /*
     * STEP 7: Notify the platform that this interpreter is
     * being deleted.  The platform can release per-interpreter
     * resources (e.g., dlclose loaded libraries).  The context
     * parameter here is reserved for future use and must be
     * zero.
     */

    th8NotifyDeleteInterp(interp, NULL);

    /*
     * STEP 8: Free the cloned platform table (if any), then the
     * interpreter struct itself.  The platform is freed LAST
     * because all prior cleanup steps may need platform callbacks
     * (xFree, xMemcpy, mutex operations, etc.).
     *
     * The xFreeInterp function pointer and pFreeInterpCtx context
     * were captured at the top of this function, before any
     * cleanup ran.  They point to read-only code and static data
     * respectively, so they survive Th8_FreePlatform.
     */

    if (interp->bPlatformCloned) {
	Th8_FreePlatform(interp->pPlatform);
	interp->pPlatform = NULL;
    }

    xFreeInterp(NULL, pFreeInterpCtx, interp);
}


/*
 * th8CheckCancel --
 *
 *	Check the interpreter's cancellation flag.
 */

static int
th8CheckCancel(Th8_Interp *interp)
{
    return Th8_IntCmpXchg(interp, &interp->bCanceled, 0, 0);
}


/*
 * th8ClearCancel --
 *
 *	Reset the interpreter's cancellation state: clear the
 *	bCanceled flag, release any owned cancel message, and
 *	reset cancelFlags.  Called by [catch] when intercepting
 *	a non-unwind cancellation, and by the outermost Th8_Eval
 *	cleanup.
 */

static void
th8ClearCancel(Th8_Interp *interp)
{
    /*
     * Atomically test-and-clear: if bCanceled is 1, set it
     * to 0 and clean up.  If already 0, do nothing.
     */

    if (Th8_IntCmpXchg(interp, &interp->bCanceled, 0, 1)) {
	interp->cancelFlags = 0;
	if (interp->bCancelMsgOwned) {
	    Th8_Free(interp, interp->zCancelMsg);
	}
	interp->zCancelMsg = 0;
	interp->nCancelMsg = 0;
	interp->bCancelMsgOwned = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Eval --
 *
 *	Evaluate a TH8 script in the specified frame.
 *
 *	FRAME RESOLUTION (iFrame parameter):
 *
 *	  iFrame == 0 : Evaluate in the current frame (most common).
 *
 *	  iFrame < 0  : Relative to current frame.  -1 means the
 *	                caller's frame, -2 means the caller's caller,
 *	                etc.  Used by [uplevel].
 *
 *	  iFrame > 0  : Absolute frame number.  Converted internally
 *	                to a negative offset: the total frame count is
 *	                computed, then iFrame is mapped to
 *	                -(totalFrames - iFrame).  Frame 0 (absolute)
 *	                would be the global frame, but since iFrame>0
 *	                is required here, the global frame is not
 *	                directly reachable this way.
 *
 *	When iFrame != 0, the current frame is saved in
 *	pDownlevelFrame (for [downlevel] support), pFrame is switched
 *	to the target, and both are restored after th8EvalLocal
 *	returns.
 *
 *	At the outermost Th8_Eval return (nEvalDepth==0), the
 *	cancellation state is automatically cleared so the interpreter
 *	can be reused without explicitly calling Th8_ResetCancel.
 *
 * Why / How:
 *	This is the blocking (recursive) entry point.  It delegates
 *	to th8EvalCommon, which pushes NRE callbacks via th8EvalLocal
 *	and then drains them via th8EvalTrampoline.  The frame
 *	resolution logic supports [uplevel] (negative iFrame),
 *	[namespace eval] (absolute iFrame), and normal eval (zero).
 *
 * Results:
 *	A TH8 return code.
 *
 * Side effects:
 *	Commands are executed.  The interpreter result is set.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8EvalCommon --
 *
 *	Common implementation for Th8_Eval and Th8_EvalTrusted.
 *	The flags argument is passed through to th8EvalLocal and
 *	ultimately to the policy callback.
 *
 * Why / How:
 *	Resolves the target frame (if iFrame != 0), calls
 *	th8EvalLocal to push NRE callbacks, then drains them via
 *	th8RunCallbacks.  Handles suspend/yield by detaching the
 *	remaining callback chain.  Restores the frame pointer
 *	after eval, clears cancellation at the outermost level
 *	(nSavedDepth==0), and converts TH8_RETURN to TH8_OK at
 *	the top level.
 *
 *----------------------------------------------------------------------
 */

static int
th8EvalCommon(
    Th8_Interp *interp, /* Interpreter. */
    int iFrame, /* Frame identifier (0=current). */
    const char *zProg, /* Script to evaluate. */
    size_t nProg, /* Script length (TH8_NOLEN=NUL). */
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName, /* Origin name length. */
    int flags) /* Eval flags (TH8_EVAL_TRUSTED etc). */
{
    int rc = TH8_OK;
    Th8_Frame *pSavedFrame = interp->pFrame;
    int nSavedDepth = interp->nEvalDepth;
    size_t nInput;

    TH8_ASSERT_OWNER(interp);

    /*
     * Resolve TH8_NOLEN, but otherwise carry the taint bit through to
     * th8EvalLocal so its security gate (TH8_TAINTED) fires on a
     * tainted script.  "Trusted" evaluation concerns the signature
     * policy, NOT taint -- a tainted script must never execute.
     * th8EvalLocal masks nProgram to the raw length internally for its
     * own arithmetic (nInput is only ever passed straight through).
     */

    if (nProg == TH8_NOLEN) {
	nInput = Th8_Strlen(interp, zProg);
    } else {
	nInput = TH8_LEN(nProg) | (nProg & TH8_TAG_BITS);
    }

    /*
     * Resolve the target frame if iFrame != 0.
     */

    if (iFrame != 0) {
	Th8_Frame *pTarget = interp->pFrame;
	int i;

	if (iFrame > 0) {
	    /* Count total frames, convert to negative offset */
	    int nFrames = 0;
	    Th8_Frame *p;

	    for (p = interp->pFrame; p; p = p->pCaller) {
		nFrames++;
	    }
	    iFrame = -nFrames + iFrame;
	    pTarget = interp->pFrame;
	}
	for (i = 0; pTarget && i < (-iFrame); i++) {
	    pTarget = pTarget->pCaller;
	}
	if (!pTarget) {
	    Th8_SetResult(interp, "no such frame", TH8_NOLEN);
	    return TH8_ERROR;
	}
	interp->pDownlevelFrame = pSavedFrame;
	interp->pFrame = pTarget;
    }

    /*
     * th8EvalLocal pushes NRE callbacks and returns TH8_OK
     * immediately.  Save the callback chain bottom marker so
     * we can drain exactly those callbacks via the trampoline
     * before proceeding with frame restoration and cancel
     * cleanup.
     */

    {
	Th8_Callback *pBottom = interp->pCallbacks;

	rc = th8EvalLocal(interp, zProg, nInput, zName, nName, flags);
	if (interp->pCallbacks != pBottom) {
	    rc = th8RunCallbacks(interp, pBottom, rc);
	}

	if ((rc == TH8_SUSPEND || rc == TH8_YIELD) &&
	    interp->pCallbacks != pBottom) {
	    /*
	     * Suspend or yield: detach the remaining callbacks
	     * from the chain and save them on the interpreter.
	     * This prevents them from leaking into the outer
	     * eval's trampoline.  Th8_Thaw (for suspend) or
	     * the coroutine resume (for yield) re-attaches and
	     * drains them.
	     *
	     * The `pCallbacks != pBottom` test is a REAL runtime
	     * check, NOT an invariant -- do not wrap it in ALWAYS
	     * (Bug 73).  th8EvalLocal can return TH8_SUSPEND from its
	     * entry readiness check (PHASE 3) BEFORE pushing any NRE
	     * callbacks -- e.g. freeze-on-break, or any bSuspended
	     * pending when a nested eval begins.  In that case nothing
	     * was pushed above pBottom, so pCallbacks == pBottom and
	     * there is nothing to detach: skip the block, leaving
	     * pSuspendedCallbacks untouched (Th8_Thaw then correctly
	     * finds nothing to resume, since no command ran).  Wrapping
	     * this in ALWAYS made it a constant-true in the omit build,
	     * so the block ran with an empty segment, the walk below ran
	     * off the end of the chain, and the (equally mis-wrapped)
	     * ALWAYS(pTail->pNext) failed to stop the NULL deref.
	     *
	     * We must NULL-terminate the detached chain by
	     * finding the callback just before pBottom and
	     * setting its pNext to NULL.  Here pCallbacks != pBottom
	     * is established, and by construction the pushed segment is
	     * a prefix that terminates at pBottom, so pBottom is always
	     * reachable before NULL -- ALWAYS(pTail->pNext) below is a
	     * genuine invariant given that guarantee.
	     */

	    {
		Th8_Callback *pTail = interp->pCallbacks;

		while (ALWAYS(pTail->pNext) && pTail->pNext != pBottom) {
		    pTail = pTail->pNext;
		}
		pTail->pNext = 0; /* NULL-terminate */
	    }
	    interp->pSuspendedCallbacks = interp->pCallbacks;
	    interp->pCallbacks = pBottom;
	    interp->pSavedFrame = pSavedFrame;
	}
    }

    if (rc != TH8_SUSPEND && rc != TH8_YIELD) {
	if (iFrame != 0) {
	    interp->pDownlevelFrame = 0;
	}
	interp->pFrame = pSavedFrame;
    }

    /*
     * Cancel-unwind cleanup.
     *
     * When TH8_CANCEL_UNWIND fires, the trampoline discards all
     * NRE callbacks (including th8EvalCleanup) without running
     * them.  This leaves nEvalDepth elevated and EvalState leaked.
     * Restore nEvalDepth to the value it had on entry so that the
     * outermost-level check works correctly.
     *
     * If this is the outermost Th8_Eval call (nSavedDepth==0) and
     * cancellation is still active, clear it so the interpreter
     * can accept new scripts (e.g. in an interactive REPL).
     */

    th8MemBarrier(interp);
    if (th8CheckCancel(interp)) {
	interp->nEvalDepth = nSavedDepth;
	if (nSavedDepth == 0) {
	    th8ClearCancel(interp);
	}
    }

    /*
     * At the outermost level (nSavedDepth==0), convert TH8_RETURN
     * to TH8_OK.  A [return] that propagates all the way to the
     * top-level eval has nowhere further to go; the interpreter
     * result already holds the returned value.
     */

    if (rc == TH8_RETURN && nSavedDepth == 0) {
	rc = TH8_OK;
    }

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Eval --
 *
 *	Public entry point for script evaluation.  Evaluates the
 *	script zProg in the specified call frame.  This is the
 *	primary API for embedding hosts to run TH8 scripts.
 *
 * Why / How:
 *	Delegates to th8EvalCommon with no special flags.  Frame
 *	switching, NRE trampoline draining, cancel/suspend handling,
 *	and depth limiting are all handled by th8EvalCommon.
 *
 * Results:
 *	A TH8 return code (TH8_OK, TH8_ERROR, TH8_RETURN,
 *	TH8_BREAK, TH8_CONTINUE, TH8_SUSPEND, or TH8_YIELD).
 *
 * Side effects:
 *	Commands in the script are executed; the interpreter result
 *	is set to the result of the last command.
 *
 *----------------------------------------------------------------------
 */

int
Th8_Eval(
    Th8_Interp *interp, /* Interpreter. */
    int iFrame, /* Frame identifier (0=current). */
    const char *zProg, /* Script to evaluate. */
    size_t nProg, /* Script length (TH8_NOLEN=NUL). */
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName) /* Origin name length. */
{
    if (!interp) return TH8_ERROR;
    return th8EvalCommon(interp, iFrame, zProg, nProg, zName, nName, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EvalTrusted --
 *
 *	Evaluate a script with an explicit trust assertion from the
 *	embedder.  Identical to Th8_Eval except that the
 *	TH8_EVAL_TRUSTED flag is passed through to the eval
 *	callback via th8EvalLocal.
 *
 * Why / How:
 *	SECURITY: The TH8_EVAL_TRUSTED flag is propagated to the
 *	policy callback (PRE and POST phases).  A policy callback
 *	can use this flag to bypass restrictions (e.g. allowing
 *	[load] or [source] of specific paths) that would be denied
 *	for untrusted scripts.  Only the embedder can call this API,
 *	so the trust assertion cannot be forged from script level.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EvalTrusted(
    Th8_Interp *interp, /* Interpreter. */
    int iFrame, /* Frame identifier (0=current). */
    const char *zProg, /* Script to evaluate. */
    size_t nProg, /* Script length (TH8_NOLEN=NUL). */
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName) /* Origin name length. */
{
    if (!interp) return TH8_ERROR;
    return th8EvalCommon(
        interp, iFrame, zProg, nProg, zName, nName, TH8_EVAL_TRUSTED);
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_EvalDownlevel --
 *
 *	Evaluate a script in the call frame that was active just
 *	prior to the most recent uplevel (frame switch).  Per Eagle
 *	semantics, [downlevel] undoes the scope change of [uplevel],
 *	running the script back in the frame the uplevel departed
 *	from.
 *
 *	It is an error to call this when no uplevel is active.
 *
 * Why / How:
 *	SECURITY: Reads interp->pDownlevelFrame (set by Th8_Eval
 *	when iFrame != 0) to find the frame that the uplevel
 *	departed from.  Temporarily switches pFrame and pCurrentNs
 *	to the target, evaluates the script via th8EvalLocal with
 *	NRE trampoline draining, then restores both.  This ensures
 *	variable resolution and command lookup use the correct
 *	namespace context.
 *
 * Results:
 *	A TH8 return code.
 *
 * Side effects:
 *	Commands are executed in the prior frame.
 *
 *----------------------------------------------------------------------
 */

int
Th8_EvalDownlevel(
    Th8_Interp *interp, /* Interpreter. */
    const char *zProg, /* Script to evaluate. */
    size_t nProg, /* Script length (TH8_NOLEN=NUL). */
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName) /* Origin name length. */
{
    Th8_Frame *pTarget;
    Th8_Frame *pSaved;
    int rc;

    if (!interp) return TH8_ERROR;
    pTarget = interp->pDownlevelFrame;
    if (!pTarget) {
	Th8_SetResult(interp, "downlevel: no uplevel active", TH8_NOLEN);
	return TH8_ERROR;
    }

    pSaved = interp->pFrame;
    interp->pFrame = pTarget;

    /*
     * Restore the target frame's namespace context so that
     * command resolution and [namespace current] reflect
     * the frame we are returning to.
     */

    {
	Th8_Namespace *pSavedNs = interp->pCurrentNs;
	Th8_Callback *pBottom;

	if (pTarget->pNs) {
	    interp->pCurrentNs = pTarget->pNs;
	}

	/*
	 * th8EvalLocal pushes NRE callbacks; drain them via
	 * the bottom-marker pattern before restoring state.
	 */

	pBottom = interp->pCallbacks;
	rc = th8EvalLocal(
	    interp, zProg,
	    (nProg == TH8_NOLEN) ? Th8_Strlen(interp, zProg) : TH8_LEN(nProg),
	    zName, nName, 0);
	if (interp->pCallbacks != pBottom) {
	    rc = th8RunCallbacks(interp, pBottom, rc);
	}
	interp->pCurrentNs = pSavedNs;
    }
    interp->pFrame = pSaved;
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DoesEnvExist --
 *
 *	Return non-zero if the named environment variable exists.
 *
 * Why / How:
 *	Calls Th8_GetEnv (which routes through the platform's
 *	xGetEnv callback) and checks for a non-NULL return.
 *	The returned string is immediately freed since only
 *	existence is being tested, not the value.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DoesEnvExist(
    Th8_Interp *interp, /* Interpreter (may be NULL). */
    const char *zName) /* Variable name. */
{
    char *zValue;

    if (!interp) return TH8_ERROR;
    zValue = Th8_GetEnv(interp, zName);

    if (zValue != NULL) {
	Th8_Free(interp, zValue);
	return 1;
    } else {
	return 0;
    }
}
