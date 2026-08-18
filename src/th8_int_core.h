/*
 * th8_int_core.h --
 *
 *	TH8 internal core header.  Contains the full definitions of
 *	Th8_Interp, Th8_Frame, Th8_Variable, Th8_Command, Th8_Callback,
 *	and other internal structures.
 *
 *	This header is for TH8 core implementation files ONLY:
 *	  th8_core.c, th8_expr.c, th8_ns.c, th8_security.c,
 *	  th8_load.c, th8_resource.c, th8_var.c, th8_cache.c, etc.
 *
 *	Plugin files must NOT include this header.
 *	They use only the public Th8_ API from th8.h.
 *
 *	Extensions and embedders must NOT include this header.
 *	The struct layout is an implementation detail that may
 *	change between releases.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_INT_CORE_H
#define TH8_INT_CORE_H

/*
 * NOTE: This header does not include other project headers.
 * Each .c file must include th8.h and th8_int.h before this.
 */

/*
 *----------------------------------------------------------------------
 *
 * Forward declarations for internal structures.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_Callback Th8_Callback;
typedef struct Th8_Frame Th8_Frame;
typedef struct Th8_Variable Th8_Variable;
typedef struct Th8_Command Th8_Command;
typedef struct Th8_Event Th8_Event;
/* Th8_AsyncState is forward-declared in th8_int.h */
typedef struct Th8_AsyncStateNode Th8_AsyncStateNode;
/* Th8_Namespace is forward-declared in th8_int.h */

/*
 *----------------------------------------------------------------------
 *
 * TH8_EVENT_QUEUE_STATIC_N --
 *
 *	Number of slots in the embedded fixed-size portion of the
 *	per-pState event queue.  Events are appended into this
 *	array first (zero heap activity); only when this fills do
 *	overflow events get heap-allocated as Th8_Event nodes on
 *	a linked list.  Size 8 fits typical event-loop bursts in
 *	the static portion while keeping the AsyncState struct
 *	small.  A circular-buffer scheme avoids memmove on pop.
 *
 *----------------------------------------------------------------------
 */

#define TH8_EVENT_QUEUE_STATIC_N 8

/*
 *----------------------------------------------------------------------
 *
 * Th8_AsyncState --
 *
 *	Opaque handle returned by Th8_CreateAsyncState and passed
 *	to Th8_QueueEvent from any thread.  Lifetime is owned by
 *	the embedder; TH8 never frees it (only sets nDeleted on
 *	interp teardown).
 *
 *	Each AsyncState owns its OWN event queue, signal handle,
 *	and serialization mutex.  Multiple threads queueing
 *	events use distinct AsyncStates, so each producer's
 *	signal/queue path is contention-free with respect to
 *	other producers.  Cross-pState ordering is undefined; a
 *	caller that needs ordering across producers must impose
 *	its own synchronisation.
 *
 *	nDeleted is the FIRST FIELD by design: a worker thread
 *	can read pState->nDeleted via xIntCmpXchg(&nDeleted, 0, 0)
 *	even if it knows nothing else about the struct layout.
 *	Non-zero means "the owning interp has been deleted; bail
 *	out cleanly".
 *
 *----------------------------------------------------------------------
 */

struct Th8_AsyncState {
    volatile int nDeleted; /* MUST be first.  Atomic flag set
				 * by Th8_DeleteInterp on every
				 * pState registered with the interp. */
    Th8_Interp *pInterp; /* Back-pointer; NULL'd on delete. */
    Th8_AsyncStateNode *pNode; /* Registry node owned by the
				 * interp; NULL'd by Th8_DeleteInterp
				 * after it has freed the node, so
				 * Th8_FinalizeAsyncState can detect
				 * a post-delete call and skip the
				 * flag-set step. */
    void *pCtx;   /* Embedder's pCtx (passed to cb). */

    /*
     * Platform function pointers cached at Th8_CreateAsyncState
     * time (on the interp's thread, when pPlatform is stable).
     * Cross-thread code paths (Th8_QueueEvent, th8SignalEvent
     * et al) operate solely off these cached pointers and
     * NEVER dereference interp->pPlatform -- that would race
     * against Th8_SetPlatform / Th8_MergePlatformInterp.
     *
     * Field names match Th8_Platform's so a single macro
     * (TH8_CHECK_EVENT_CALLBACKS) validates either struct.
     */
    void *(*xMalloc)(Th8_Interp *, void *, size_t);
    void (*xFree)(Th8_Interp *, void *, void *);
    void (*xMutexInit)(Th8_Interp *, void *, Th8_Mutex *);
    void (*xMutexFinal)(Th8_Interp *, void *, Th8_Mutex *);
    void (*xMutexEnter)(Th8_Interp *, void *, Th8_Mutex *);
    void (*xMutexLeave)(Th8_Interp *, void *, Th8_Mutex *);
    void *(*xEventCreate)(Th8_Interp *, void *);
    void (*xEventDestroy)(Th8_Interp *, void *, void *);
    void (*xEventSet)(Th8_Interp *, void *, void *);
    void (*xEventReset)(Th8_Interp *, void *, void *);
    int (*xEventWait)(Th8_Interp *, void *, void *, int);
    int (*xIntCmpXchg)(Th8_Interp *, void *, volatile int *, int, int);
    void *pPlatCtx;

    /*
     * Per-pState event queue.  Layout: a fixed-size circular
     * buffer (the static portion) for the common-case load,
     * plus a singly-linked overflow list for events that
     * arrive while the static portion is full.  Pop pulls
     * from the static portion's head; if the overflow list is
     * non-empty afterward, its head is moved into the freed
     * slot, preserving FIFO order across the static/overflow
     * boundary.  All queue mutations (insert, pop, list
     * splice) are serialized by queueMutex.
     */
    Th8_Mutex queueMutex; /* Serializes queue mutations. */
    int bMutexReady; /* 1 if queueMutex is initialized. */
    void *pEventHandle; /* Per-pState manual-reset event
				 * handle from xEventCreate.  Set on
				 * every enqueue + on cancel; reset
				 * by [vwait] before each xEventWait. */

    int (*aStatic[TH8_EVENT_QUEUE_STATIC_N])(Th8_Interp *, void *);
                                /* Circular buffer of pending callbacks.
				 * The valid entries occupy positions
				 * iHead, (iHead+1)%N, ..., wrapping
				 * around as needed.  Entries outside
				 * [iHead, iHead+nStatic) are stale
				 * and must not be read.            */
    int iHead;  /* Index of oldest queued callback. */
    int nStatic;  /* Count of entries currently in the
				 * static buffer (0..N inclusive). */
    Th8_Event *pOverflowHead; /* Singly-linked overflow list,
				 * oldest first; head is the next
				 * candidate to refill the static
				 * buffer when it has room. */
    Th8_Event *pOverflowTail; /* Tail pointer for O(1) append. */
    int nOverflow; /* Length of the overflow list. */
};

/*
 *----------------------------------------------------------------------
 *
 * Th8_AsyncStateNode --
 *
 *	One entry in the per-interp registry of pStates.  Owned by
 *	the interp; freed by Th8_DeleteInterp.  Holds a borrowed
 *	pointer to the embedder-owned Th8_AsyncState plus an atomic
 *	nFinalized flag set by Th8_FinalizeAsyncState before it
 *	frees the pState.  Th8_DeleteInterp's walk reads nFinalized
 *	atomically: 0 = pState is alive, mark it as deleted; non-zero
 *	= embedder already freed pState, leave it alone.
 *
 *----------------------------------------------------------------------
 */

struct Th8_AsyncStateNode {
    Th8_AsyncState *pState;
    volatile int nFinalized;
    Th8_AsyncStateNode *pNext;
};

/*
 *----------------------------------------------------------------------
 *
 * Th8_Event --
 *
 *	One node on the OVERFLOW portion of a per-pState event
 *	queue.  Static-portion entries do not use this struct --
 *	they live directly in the AsyncState's aStatic[] array.
 *	A Th8_Event is heap-allocated only when the static
 *	portion is full at enqueue time; it is freed when its
 *	callback runs and slots open up.
 *
 *	The owning pState is implicit (the queue is per-pState),
 *	so this node only needs the callback pointer and the
 *	link.  pCtx for the callback is fetched from
 *	pState->pCtx at drain time.
 *
 *----------------------------------------------------------------------
 */

struct Th8_Event {
    int (*xCallback)(Th8_Interp *interp, void *pCtx);
    Th8_Event *pNext;
};

/*
 *----------------------------------------------------------------------
 *
 * Th8_Callback --
 *
 *	NRE (Non-Recursive Evaluation) callback node.  These form a
 *	LIFO chain (stack) rooted at Th8_Interp.pCallbacks.
 *
 *----------------------------------------------------------------------
 */

struct Th8_Callback {
    Th8_CallbackProc xProc; /* Continuation function to invoke. */
    void *pData[4];  /* Four opaque client-data slots. */
    Th8_Callback *pNext; /* Next callback in the LIFO chain. */
};

/*
 *----------------------------------------------------------------------
 *
 * Deferred deletion --
 *
 *	When a command or namespace is deleted while script is
 *	evaluating (nEvalDepth > 0), the hash removal happens
 *	immediately (so the name can't be resolved again) but the xDel
 *	callback and memory free are deferred until the eval stack fully
 *	unwinds.  This prevents use-after-free when a command deletes
 *	itself during dispatch (e.g. coroutine auto-delete) or when a
 *	namespace is deleted from inside one of its own commands.
 *
 *	The queue is INTRUSIVE (TH8K-007): the pending object is already
 *	unlinked from every hash/index when it is queued, so it is
 *	threaded onto a per-interpreter FIFO through its own pPendingNext
 *	field (Th8_Command / Th8_Namespace).  Queuing therefore allocates
 *	nothing and cannot fail -- eliminating the former "risk a
 *	use-after-free rather than leak" OOM fallback.  Commands and
 *	namespaces use separate lists; on drain, commands are freed
 *	before namespaces so a command destructor may still reference a
 *	namespace that is also pending.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_Frame --
 *
 *	Interpreter call frame.  Each procedure call or namespace eval
 *	pushes a new frame.
 *
 *----------------------------------------------------------------------
 */

struct Th8_Frame {
#if defined(TH8_ENABLE_VARIABLES)
    Th8_Hash *paVar;  /* Variable hash for this scope. */
#endif
    Th8_Frame *pCaller;  /* Calling (enclosing) frame. */
    Th8_Namespace *pNs;  /* Namespace context for this frame. */
    int argc;   /* Argument count of invoking command. */
    const char **argv;  /* Borrowed argument string pointers. */
    size_t *argl;  /* Borrowed argument length array. */
};

/*
 *----------------------------------------------------------------------
 *
 * Th8_Variable --
 *
 *	Variable storage.  Reference-counted for upvar/global links.
 *
 *----------------------------------------------------------------------
 */

struct Th8_Variable {
    int nRef;   /* Reference count. */
    int bBorrowed;  /* zData is borrowed (cache-owned), don't free. */
    size_t nData;  /* Byte length of scalar value. */
    size_t nAlloc;  /* Allocated capacity when bBorrowed (else 0). */
    char *zData;  /* Scalar value, or NULL if array. */
    Th8_Hash *pHash;  /* Array element hash, or NULL. */
    int nEpoch;   /* Mutation counter; bumped on element add/remove. */
    int nGeneration;  /* Unique counter assigned when pHash is
				 * (re)allocated; lets array-search SIDs detect
				 * unset+recreate of the same name even when the
				 * allocator reuses the freed pHash address. */
    int nWait;   /* "vwait" signal counter; bumped on every
				 * create / change / unset of the variable. */
};

/*
 *----------------------------------------------------------------------
 *
 * Th8_Command --
 *
 *	Command registration entry.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Th8_SubCmd --
 *
 *	One sub-command of an ensemble command.  Stored as the pData of a
 *	Th8_HashEntry in the parent Th8_Command's paSubCommands hash, keyed
 *	by the sub-command name.  A sub-command is a mini-command: it has its
 *	own handler, context, destructor, and token, so it can be unregistered
 *	by token (Th8_DeleteSubCommand) exactly like a top-level command --
 *	interp->paSubToken maps the token back to this record for O(1) delete.
 *
 *----------------------------------------------------------------------
 */

typedef struct Th8_SubCmd {
    Th8_CommandProc xProc; /* Sub-command implementation function. */
    void *pContext;  /* Opaque context passed to xProc. */
    void (
        *xDel)(Th8_Interp *, void *); /* Destructor for pContext, or NULL. */
    th8_uint64_t nToken; /* Unique token (for Th8_DeleteSubCommand). */
    char *zName; /* Owned copy of the sub-command name (for ordered
			 * listing / introspection, like Th8_Command.zQualName). */
    size_t nName; /* Byte length of zName. */
    struct Th8_Command *pParent; /* Owning command, so a token lookup can
                                  * remove this sub-command from the right
                                  * paSubCommands hash. */
} Th8_SubCmd;

struct Th8_Command {
    Th8_CommandProc xProc; /* Command implementation function. */
    void *pContext; /* Opaque context passed to xProc. */
    void (*xDel)(Th8_Interp *, void *);
    /* Destructor for pContext, or NULL. */
    void *(*xCopy)(Th8_Interp *, void *);
    /* Deep-copy for namespace import. */
    Th8_Namespace *pDefNs; /* Defining namespace. */
    th8_uint64_t nToken; /* Unique command token. */
    char *zQualName; /* Fully qualified name. */
    size_t nQualName; /* Byte length of zQualName. */
    Th8_Command *pPendingNext; /* Intrusive deferred-delete FIFO link
				 * (TH8K-007); NULL unless queued. */
    Th8_Hash *paSubCommands; /* Sub-command overlay (name -> Th8_SubCmd), or
				 * NULL if none.  When set, a matching sub-command
				 * wins; an unmatched invocation falls back to
				 * xProc (if any), else the ensemble error.  A
				 * pure ensemble has xProc == NULL. */
};

/*
 *----------------------------------------------------------------------
 *
 * Th8_Interp --
 *
 *	Full interpreter state.  This is the central data structure
 *	of TH8.  All public API functions take a Th8_Interp* as
 *	their first argument.
 *
 *----------------------------------------------------------------------
 */

#define TH8_MAX_SOURCE_DEPTH (256)

struct Th8_Interp {
    /*
     * Version + stubs.
     */

    th8_int64_t nVersion;
    const void *pStubs;

    /*
     * Platform abstraction.
     */

    Th8_Platform *pPlatform;
    int bPlatformCloned; /* Non-zero if pPlatform was cloned
				   by Th8_MergePlatformInterp and must
				   be freed on interpreter delete. */
    Th8_Hash *paCallbackCtx; /* Per-callback context overrides.
				   Keyed on callback function address;
				   pData is the custom context pointer.
				   Created lazily by SetPlatformContext. */
    th8_uint64_t threadId; /* Id of the owning (creating) thread.
				   Captured in Th8_CreateInterp via
				   xGetThreadId; read/written ONLY through
				   the 64-bit interlocked CAS
				   (Th8_Int64CmpXchg).  Enforces the single-
				   threaded-per-interpreter affinity contract
				   (see TH8_ASSERT_OWNER,
				   Th8_GetInterpThreadId).  0 if the platform
				   reports no thread id (affinity unchecked). */

    /*
     * Script debugging.
     */

    Th8_DebugProc xDebug; /* Debug callback (NULL = no debugging). */
    void *pDebugCtx; /* Debug callback client data. */
    int nStepMode; /* TH8_STEP_* mode. */
    int nStepDepth; /* Frame depth when stepping began. */
    Th8_Hash *paBreakpoints; /* Breakpoint table (lazy). */
    int nNextBreakpointId; /* Auto-incrementing breakpoint ID. */

    /*
     * Binary loading gate.
     */

    th8_int64_t nLoadToken;
    th8_int64_t nUnloadToken;

    /*
     * Interpreter result.
     */

    char *zResult;
    size_t nResult; /* raw byte length | tag bits (taint 0x10000000,
                     * sensitive 0x20000000); sensitivity is derived from
                     * TH8_SENSITIVE(nResult), not a separate flag. */
    int bResultBorrowed; /* zResult is borrowed (cache-owned). */
    int bResultBuildFailed; /* a result-building allocation (Th8_SetResult /
                             * Th8_ListAppend / Th8_StringAppend) failed since
                             * the current command was invoked; th8InvokeCommand
                             * clears this before each command and, if it is set
                             * when the command returns TH8_OK, promotes the
                             * result to an out-of-memory error so a
                             * truncated/empty result is never reported as
                             * success (TH8K-030). */

    /*
     * Finally block state.  Updated by the [try] command after
     * the finally script completes.  Available to C callers via
     * th8GetFinallyResult / th8GetFinallyRc.
     */

    char *zFinallyResult;
    size_t nFinallyResult;
    int nFinallyRc; /* TH8_OK if finally succeeded. */

    /*
     * Namespace tree.
     */

    Th8_Namespace *pGlobalNs;
    Th8_Namespace *pCurrentNs;

    /*
     * Call stack and NRE trampoline.
     */

    Th8_Frame *pFrame;
    Th8_Callback *pCallbacks;
    Th8_Callback *pSuspendedCallbacks;
    Th8_Frame *pSavedFrame;

    /*
     * Coroutine state.
     */

    struct Th8_CoroState *pYieldingCoro;

    /*
     * Pending-delete queue (FIFO).  Commands and namespaces
     * deleted during eval are queued here; the queue is drained
     * when nEvalDepth drops to 0 (th8DrainPendingDeletes).
     */

    Th8_Command *pPendingCmdHead; /* Intrusive deferred-delete FIFOs */
    Th8_Command *pPendingCmdTail; /* (TH8K-007): objects are threaded */
    Th8_Namespace *pPendingNsHead; /* through their own pPendingNext, so */
    Th8_Namespace *pPendingNsTail; /* queuing never allocates. */

    /*
     * Parser state.
     */

    int isListMode;
    int nEvalDepth;
    int nExprDepth; /* Expression-tree recursion depth (TH8K-019). */
    int nLine;
    int nErrorLine;

    /*
     * Cancellation.
     */

    /*
     * TH8K-008 lock-free cross-thread cancellation.
     *
     * nCancelReq is the SINGLE atomic word that carries a cancellation request
     * coherently across threads: TH8_CR_CANCELED plus the request's flag bits
     * (TH8_CANCEL_UNWIND, TH8_CANCEL_SIGNAL).  ANY thread -- the owner, a
     * foreign worker, or a signal handler -- requests cancellation by
     * atomically OR-ing (TH8_CR_CANCELED | flags) into it.  Because the canceled
     * bit and the flags live in ONE word, a cancel and its flags can never be
     * torn or partially lost (the payload-loss race the audit flagged).  The
     * evaluator polls it (th8CheckCancel); [catch]/unwind read the flag bits;
     * the owner clears it (atomic store 0) on reset.  There is NO spinlock.
     */
    volatile int nCancelReq;
    volatile int bSuspended;
    /*
     * Owner-only cancel MESSAGE state (only the owning thread touches these).
     * cancelFlags mirrors nCancelReq's flag bits; the owner refreshes it from
     * nCancelReq at each poll so the owner-only [catch]/save/restore readers
     * need no synchronization.
     */
    volatile int cancelFlags;
    volatile int bCancelMsgOwned;
    char *volatile zCancelMsg;
    volatile size_t nCancelMsg;
    /*
     * TH8K-008 cross-thread cancel MESSAGE buffer.  A foreign NON-signal
     * canceller (which MAY allocate -- it must be Th8_ThreadInit'd, like
     * Th8_QueueEvent) copies the caller's message into a self-describing buffer
     * laid out as [size_t length][bytes...][NUL] and atomic-EXCHANGEs the
     * pointer in here.  Whoever swaps a non-NULL pointer OUT (a later publisher
     * overwriting, the owner adopting, or a reset) owns it exclusively and frees
     * it, so every buffer is freed exactly once -- no leak, double-free, or
     * use-after-free, and no lock.  The owner reads the EXACT length from the
     * prefix, copies it into its owned message, and frees the buffer.  A signal
     * handler never touches this (it cannot allocate/free); a signal cancel
     * carries no message and the owner reports a fixed static text.
     *
     * Stored as a pointer-holding 64-bit integer (portably 32/64-bit) so the
     * exchange is a single Th8_Int64CmpXchg with no aliasing pun; 0 == empty.
     */
    volatile th8_uint64_t nCancelReqMsg;
    volatile int bExit;

    /*
     * Unknown command handler.
     */

    int bInUnknown;

    /*
     * Source script name stack.
     */

    const char *azSourceName[TH8_MAX_SOURCE_DEPTH];
    size_t anSourceName[TH8_MAX_SOURCE_DEPTH];
    int nSourceDepth;

    /*
     * Uplevel / downlevel frame tracking.
     */

    Th8_Frame *pDownlevelFrame;

    /*
     * Phase callbacks (hooks).
     */

    Th8_PolicyProc xPolicyCb;
    void *pPolicyCbCtx;
    Th8_PreLoadProc xPreLoad;
    void *pPreLoadCtx;

    /*
     * Package system.
     */

    Th8_Hash *paPackage;
    Th8_Hash *paMathFunc;
    char *zPkgUnknown;
    size_t nPkgUnknown;

    /*
     * Array search state ([array startsearch] et al).  Internal
     * only; no public accessor.  Lazily allocated on first
     * startsearch.  Keyed by search-id string; values point to
     * th8ArraySearch records owned by th8_variables.c.
     */

    Th8_Hash *paArraySearch;
    int iArraySearchCounter;
    int iArrayHashGeneration; /* Monotonic per-interp counter used to
				 * stamp Th8_Variable.nGeneration on each
				 * element-hash (re)allocation; see
				 * Th8_Variable for the lifecycle rationale. */

    /*
     * Thread-safe event queue (per-pState design).  The queue
     * itself, its mutex, and its signal handle live on each
     * Th8_AsyncState -- see Th8_CreateAsyncState.  The interp
     * keeps only the registry of pStates currently bound to it;
     * Th8_DrainQueueEvents walks this list, draining each
     * pState's queue in turn on the interp's owning thread.
     *
     * The registry is single-threaded: Th8_CreateAsyncState
     * inserts on the interp's thread; Th8_FinalizeAsyncState
     * marks (without removing) on the same thread or
     * post-delete; Th8_DeleteInterp walks once at teardown.
     */
    Th8_AsyncStateNode *pAsyncStateHead; /* Linked list of
					 * registry nodes; each node
					 * is interp-owned and points
					 * to an embedder-owned
					 * pState.  Freed by
					 * Th8_DeleteInterp. */

    /*
     * Resource limits.
     */

    th8_int64_t nStepCount;
    th8_int64_t nStepLimit;
    th8_int64_t nDeadlineUs; /* Absolute monotonic-microsecond wall-clock
			      * deadline (0 = none); checked periodically
			      * in th8Step (TH8K-010). */
    size_t nResultLimit;

    /*
     * Native stack checking.
     */

    void *pStackBase;
    size_t nStackSize;
    size_t nStackGuard;
    int bStackGrowsDown;
    int bStackCheckEnabled;

    /*
     * Integer overflow enforcement.
     */

    int bOverflowCheck;

    /*
     * Bigint gate.
     */

    th8_int64_t nBigintToken;
    th8_int64_t nBigintOk;

    /*
     * Expression-grammar feature flags.
     *
     * Bitmask of TH8_EXPR_* constants enabling opt-in extensions
     * to the strict Tcl 8.6 expr(n) grammar (top-level comma
     * separator, := assignment operator, etc.).  Default 0 means
     * strict compliance -- no extensions are reachable until the
     * embedder calls Th8_SetExprFeatures.  This is a per-interp
     * scalar (not the random-token gate pattern used by load /
     * bigint) because the protected resource here is syntactic
     * compliance, not access to dangerous capabilities; a memory
     * corruption that flips a flag bit at worst makes the parser
     * accept an additional operator the embedder did not opt
     * into, which is not a privilege escalation.
     */

    int nExprFeatures;

    /*
     * Binary loading gate (loaded state).
     */

    th8_int64_t nLoadOk;
    th8_int64_t nUnloadOk;
    int nUnloadFlags;

    struct Th8_LoadedLib *pLoaded;
    void *pPlatformLibs;

    /*
     * Memory accounting.
     */

    size_t nAllocBytes;
    size_t nAllocLimit;
    size_t nAllocPeak; /* High-water mark of nAllocBytes (TH8K-021). */

    /*
     * Signed-only gate.
     */

    th8_int64_t nSignedToken;
    th8_int64_t nSignedOk;

    /*
     * Secure variable persistence gate (dual-field token).
     */

    th8_int64_t nSecurePersistToken;
    th8_int64_t nSecurePersistOk;

    /*
     * System variable names.
     */

    Th8_Hash *paSystemVar;

    /*
     * Internal-representation cache.
     */

    Th8_Hash *paCache;
    Th8_Mutex cacheMutex;
    int bCacheMutexReady;

#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    /*
     * Secure variables.
     */

    void *pSecureKeyStore;
    Th8_Hash *paSecureVar;
    th8_int64_t nLastNtpSec;
    th8_int64_t nLastLocalMs;

    /*
     * Protected (mlock'd, guard-paged) backing region for the
     * interpreter result when it is sensitive (TH8_SENSITIVE(nResult)),
     * AND the
     * decryption destination for [secure] variable operations
     * (th8SecureGetVar uses it for the plaintext result;
     * th8SecureSave uses it as scratch for re-encryption with
     * an explicit Th8_ClearResult and post-use secure-zero).
     * Lazily allocated on first sensitive use and reused for
     * the lifetime of the interpreter.  Type-erased to void*
     * to avoid leaking the Th8_ProtectedRegion type into
     * non-crypto-aware code.
     */

    void *pProtectedResult;
#endif

    /*
     * Plugin system.
     */

    void *pPlugins;
    th8_uint64_t nNextCmdToken;
    Th8_Hash *paCmdToken;
    Th8_Hash *paSubToken; /* token -> Th8_SubCmd* index for O(1)
			   * Th8_DeleteSubCommand; lazily created with the
			   * first sub-command, freed at teardown. */

    /*
     * Channel registry: maps channel names (e.g., "./tmp/foo.tmp")
     * to Th8_Channel structs for temp file I/O.
     */

    Th8_Hash *paChannels;

#if defined(TH8_BENCHMARKING)
    /*
     * Cache performance counters.
     */

    th8_uint64_t nCacheHit; /* Cache lookup hit. */
    th8_uint64_t nCacheMiss; /* Cache lookup miss (new entry). */
    th8_uint64_t nCacheEvict; /* Cache collision eviction. */
#endif
};

/*
 * Active fault-injection config (set by Th8_FaultInstall,
 * cleared by Th8_FaultUninstall).  NULL when no fault is
 * installed.  Read by Th8_FindInCache to consult the
 * per-cacheType lookup-failure mask without going through a
 * platform-callback dispatch.  Defined in th8_fault.c.
 * Forward-declared here so files including th8_int_core.h
 * without also including th8.h still compile.
 */
struct Th8_FaultConfig;
extern struct Th8_FaultConfig *th8FaultActiveCfg;

#endif /* TH8_INT_CORE_H */
