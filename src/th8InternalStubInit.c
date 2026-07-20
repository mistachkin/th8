/*
 * th8InternalStubInit.c --
 *
 *	TH8 INTERNAL stubs table static initializer.  Hand-
 *	maintained companion to src/th8InternalDecls.h.
 *
 *	This file is compiled into the TH8 core library and
 *	produces:
 *
 *	  - th8InternalStubsTableData: a fully-populated table
 *	    of TH8_INTERNAL function pointers.
 *	  - Th8_GetInternalStubs(): a TH8_API accessor that
 *	    returns a pointer to the table.
 *
 *	Binary plugins (testlib, future diagnostic plugins)
 *	call Th8_GetInternalStubs(), cast the result to
 *	`const Th8InternalStubsTable *`, validate magic/
 *	version, and then invoke internal entries through the
 *	table -- bypassing the hidden-visibility barrier of the
 *	main library.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8.h"
#include "th8_int.h"
#include "th8_vars.h"
#include "th8_plugin.h"
#include "th8_spilornis.h"
#include "th8InternalDecls.h"

/*
 * The actual stubs-table instance.  Static-storage; lifetime
 * = process.  Initialised at C-startup, never modified.
 */

static const Th8InternalStubsTable th8InternalStubsTableData = {
    TH8_INTERNAL_STUBS_MAGIC,
    TH8_INTERNAL_STUBS_VERSION,
    th8ParseCommand,
    th8FreeParse,
#if defined(TH8_ENABLE_EXPRESSIONS)
    th8ParseExpr,
#else
    0,
#endif
#if defined(TH8_ENABLE_VARIABLES)
    th8ParseVarName,
#else
    0,
#endif
    th8Memmove,
    th8Strcmp,
    th8Strchr,
    th8Strrchr,
    th8Atoi,
    th8Qsort,
    th8Snprintf,
    th8MemBarrier,
    th8TranslateLineEndings,
#if defined(TH8_ENABLE_VARIABLES)
    th8GetVarValue,
    th8SetVarLength,
#else
    0,
    0,
#endif
#if defined(TH8_ENABLE_LOAD)
    th8XorInterpLoadToken,
#else
    0,
#endif
    th8XorInterpBigintToken,
    th8XorInterpSignedToken,
    th8XorInterpSecurePersistToken,
    th8XorInterpSecurePersistOk,
#if defined(TH8_ENABLE_LOAD)
    th8XorInterpUnloadToken,
    th8ClearInterpUnloadFlags,
    th8IsUnloadEnabled,
    th8IsUnloadDangerous,
    th8LoadNameMatch,
#else
    0,
    0,
    0,
    0,
    0,
#endif
    th8ChannelCreate,
    th8ChannelFind,
    th8ChannelWrite,
    th8ChannelRead,
    th8ChannelSeek,
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    th8ProtectedCheckCanary,
    th8ProtectedData,
#else
    0,
    0,
#endif
#if defined(TH8_ENABLE_VARIABLES)
    th8SetVarValue,
    th8GetArrayEpoch,
    th8GetArrayGeneration,
#else
    0,
    0,
    0,
#endif
    th8SetCmdToken,
    th8GetCmdToken,
    th8SignalEvent,
    th8ResetEvent,
    th8WaitEvent,
    th8PStateQueueLen,
    th8SetLine,
    th8SetFrameNsPtr,
    th8GetFinallyRc,
    th8GetFinallyResult,
    th8EvalTrampoline,
    th8SetResultBorrowed,
    th8InFrame,
    th8_spilornis_memcmp,
    th8_spilornis_strlen,
    th8_spilornis_strncmp,
    th8_spilornis_strncpy,
    th8CopyValue,
    th8FreeValue,
    th8RemoveFromCache,
    th8FindListInCache,
    th8SetCacheString,
    /* Phase 2 (2026-05-29) -- unconditional bulk expansion.  Order
     * MUST match the struct layout in src/th8InternalDecls.h. */
    th8AnyEventQueued,
    th8BufferAlloc,
    th8BufferFree,
    th8ChannelCleanup,
    th8ChannelClose,
    th8ChannelFlush,
    th8ChannelList,
    th8ChannelTell,
    th8CheckStack,
    th8ClearCache,
    th8DrainAll,
    th8DrainOneStateEvent,
    th8EventQueueAvailable,
    th8FreePStateEvents,
    th8GetCurrentNsPtr,
    th8GetFrameLevel,
    th8GetNsParent,
    th8GetPlatformLibs,
    th8GlobalMutexEnter,
    th8GlobalMutexLeave,
    th8IsAlnum,
    th8IsAlpha,
    th8IsBinDig,
    th8IsDigit,
    th8IsHexDig,
    th8IsOctDig,
    th8IsSpace,
    th8IsSpecial,
#if defined(TH8_ENABLE_EXPRESSIONS)
    th8MathOp,
#endif
#if defined(TH8_USE_MIMALLOC)
    th8MiHeapDone,
    th8MiHeapInit,
#endif
    th8NRInFrame,
    th8NotifyDeleteInterp,
    th8NotifyPreDeleteInterp,
    th8NsGetExport,
    th8OversizeString,
    th8PlatformHasEventQueue,
    th8RestoreCancel,
    th8SaveCancel,
    th8SetAllocBytes,
    th8SetCurrentNsPtr,
    th8SetFinallyState,
    th8SetFrameObjv,
    th8SetPlatformLibs,
    th8SignalAllStates,
    th8Step,
    th8BigintCacheStore,
#if defined(TH8_PLUGIN_FILE_SYSTEMS)
    th8IsDeviceName,
#else
    NULL, /* th8IsDeviceName defined only with the file-systems plugin */
#endif
    th8Vsnprintf,
    /* Phase 2 (2026-05-29) gated entries.  Order MUST match the
     * struct layout in th8InternalDecls.h. */
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    th8FinalizeSensitiveResult,
    th8GetLastLocalMs,
    th8GetLastNtpSec,
    th8GetProtectedResultRegion,
    th8HttpsTimeQuery,
    th8NtpQuery,
    th8ProtectedAlloc,
    th8ProtectedCanarySize,
    th8ProtectedFree,
    th8ProtectedPageSize,
    th8SetLastLocalMs,
    th8SetLastNtpSec,
#endif
#if defined(TH8_ENABLE_VARIABLES) && defined(TH8_ENABLE_CRYPTOGRAPHY)
    th8GetSecureKeyStore,
    th8GetSecureVarHash,
    th8IsSecureVar,
    th8SecureFinish,
    th8SecureInit,
    th8SecureLoad,
    th8SecureSave,
    th8SecureVarCreate,
    th8SecureVarDelete,
    th8SetSecureKeyStore,
    th8SetSecureVarHash,
#endif
#if defined(TH8_PLUGIN_VARIABLES)
    th8AppendInPlace,
#endif
#if defined(TH8_ENABLE_FAULT_INJECTION)
    th8FaultStashSite,
#endif
    th8SplitQualName,
    th8FindNamespace,
    th8GetInterpCmdToken,
    th8SetInterpCmdToken,
    th8GetInterpCurrentNs,
    th8GetFramePaVar,
    th8GetInterpPaChannels,
    th8GetArraySearchHash,
    th8GetArrayElementHash,
    th8CheckExpansionPrefix,
    th8AsyncStateXorBMutexReady,
    th8AsyncStateScrubField,
    th8XchgInterpPlatform,
    th8_bigint_calloc,
    th8BigintSetup,
    th8BigintTeardown,
#if defined(TH8_ENABLE_FAULT_INJECTION)
    th8FaultStrEqAscii,
    th8FaultPathMatchesBaseName,
#else
    NULL, /* th8FaultStrEqAscii defined only with fault injection */
    NULL, /* th8FaultPathMatchesBaseName defined only with fault injection */
#endif
#if defined(TH8_PLATFORM_POSIX)
    th8PosixIsUnderBase,
    th8PosixIsPathUnderBase,
    th8PosixCallUnloadProc,
#else
    0,
    0,
    0,
#endif
#if defined(TH8_ENABLE_CRYPTOGRAPHY)
    th8PolicyIsHttpUri,
    th8PolicyDaysInMonth,
    th8SecureCheckCanary,
    th8NtpSortTimes,
    th8AfParseHexKey,
    th8RsaParseCapi,
    th8HttpsTimeVerifySignature,
    th8HttpsTimeFindField,
    th8SecureHasMasterKey,
    th8PolicyResetCachedKeys,
    th8PolicyVerifyData,
    th8AfFlagSetAdd,
    th8AfFlagSetRemove,
    th8AfMapGet,
    th8TestRsaKeyClearPubBlob,
    th8TestRsaKeyRestorePubBlob,
    th8NtpValidateResponse,
#else
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
#endif
};

/*
 *----------------------------------------------------------------------
 *
 * Th8_GetInternalStubs --
 *
 *	Public accessor for the TH8 internal stubs table.  Returns
 *	a pointer to the read-only Th8InternalStubsTable instance.
 *
 *	The caller is expected to cast to
 *	`const Th8InternalStubsTable *` (declared in
 *	th8InternalDecls.h) and verify the magic / version fields
 *	before dereferencing any function pointer.
 *
 *	Returning `const void *` rather than the typed pointer keeps
 *	th8.h's public surface free of th8_int.h and
 *	th8InternalDecls.h dependencies -- callers that need the
 *	internal stubs explicitly include th8InternalDecls.h.
 *
 * Results:
 *	Pointer to the static stubs-table instance.  Never NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const void *
Th8_GetInternalStubs(void)
{
    return (const void *)&th8InternalStubsTableData;
}
