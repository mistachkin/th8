# TH8 MC/DC waiver register

This document explains, per decision, why each condition waived from the TH8
MC/DC (Modified Condition/Decision Coverage) coverage gate cannot be exercised
by a test.  It is the human-readable companion to the machine-checked waiver
file `tools/data/mcdc_waivers.tsv`, and is GENERATED from it by
`tools/mkmcdcwaiverdoc.tcl`.  The RTM gate `make check-mcdc`
(`tools/mcdc_gate.tcl`) enforces that every waiver still corresponds to a
genuinely uncovered decision -- a waiver whose decision becomes covered, or
disappears, fails the gate as **stale** and must be removed -- and
`make check-mcdc-doc` enforces that every waiver is documented here.

## Why a waiver register exists

TH8's RTM 1.0 MC/DC gate targets **95% of the *reachable* denominator**:
`covered / (total - waived) >= 95%`.  A flat 95% is not achievable by writing
more tests, because a residue of conditions is undrivable *by construction* --
structurally-correlated conditions, defense-in-depth guards against states that
cannot occur, Windows-only or network-only paths, external-library error
returns, and so on.  Rather than wrap those away (which, under the omit build,
would compile out real safety handlers) or pretend they are covered, each is
recorded here with a concrete reason it cannot be hit, assigned to one of the
waiver *classes* below.  Everything NOT waived is genuine, drivable coverage
debt and still counts against the 95%.

As of the last generation there are **128 waivers**.  A decision may
contribute more than one waived condition; the gate accounts for conditions,
this register lists decisions.

## Waiver classes

### correlated-conditions -- Correlated conditions (18)

MC/DC requires each condition of a decision to be shown to *independently* change the decision's outcome -- an "independence pair" of test vectors in which only that condition differs. In these decisions the conditions are structurally correlated, so no choice of inputs can form the pair. The clearest case is a sign test on one operand: `iLeft > 0` and `iLeft < 0` are two *separate* conditions to the coverage tool, but they are mutually determined by the single value of `iLeft` -- you cannot make one true and the other true, nor flip one while holding the rest such that the outcome changes. The missing pairs are therefore unformable *by construction*, not merely un-exercised.

- **`th8PolicyParseTimestamp`** (`src/plugins/harpy/th8_policy.c`)
  - Decision: `year < 1970 || year > 9999`
  - Why it cannot be hit: The year field is pre-validated as exactly 4 digits (each z[i] checked '0'..'9' before accumulation), so year is always in [0000,9999]; the year>9999 arm is unreachable, capping the decision at the year<1970 pair (driven by ann_pre_epoch=1969) -- like th8ScanStoreInt's digit-domain guards.
- **`th8PolicyParseTimestamp`** (`src/plugins/harpy/th8_policy.c`)
  - Decision: `hour < 0 || hour > 23`
  - Why it cannot be hit: Hour is pre-validated as 2 digits then accumulated (hour = hour*10 + digit), so hour is always >= 0; the hour<0 arm is unreachable, capping the decision at the hour>23 pair (driven by ann_big_hour).
- **`th8PolicyParseTimestamp`** (`src/plugins/harpy/th8_policy.c`)
  - Decision: `minute < 0 || minute > 59`
  - Why it cannot be hit: Minute is pre-validated as 2 digits then accumulated, so minute is always >= 0; the minute<0 arm is unreachable, capping the decision at the minute>59 pair (driven by ann_big_minute).
- **`th8PolicyParseTimestamp`** (`src/plugins/harpy/th8_policy.c`)
  - Decision: `second < 0 || second > 60`
  - Why it cannot be hit: Second is pre-validated as 2 digits then accumulated, so second is always >= 0; the second<0 arm is unreachable, capping the decision at the second>60 leap-second pair (driven by ann_big_second).
- **`th8PolicyCheckAnnotations`** (`src/plugins/harpy/th8_policy.c`)
  - Decision: `pAnn->bHasNotBefore && pAnn->bHasNotAfter && pAnn->nNotBefore > pAnn->nNotAfter`
  - Why it cannot be hit: This final inverted-range guard is unreachable: the preceding checks (nowSec < nNotBefore -> not-yet-valid, nowSec > nNotAfter -> expired) run first with the same nowSec, and an inverted range [nNotBefore>nNotAfter] makes [nNotBefore,nNotAfter] empty, so one of those guards always fires before this line. It is defensive redundancy; the true-arm independence pair (T,T,T) is unformable.
- **`th8PolicyVerifyData`** (`src/plugins/harpy/th8_policy.c`)
  - Decision: `!th8PolicyIsRelativePath(zName, nName) && !th8PolicyIsHttpUri(interp, zName, nName)`
  - Why it cannot be hit: The (T,F) vector needs !IsRelativePath=T (name starts with '/' or '\\') AND !IsHttpUri=F (name starts with "http(s)://") simultaneously, which is impossible -- a string cannot begin with both '/' and "http". The absolute-path probe drives the (T,T) pair; the C2 independence pair is unformable.
- **`Th8_RsaVerify`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!zModulus || nModulus == 0`
  - Why it cannot be hit: Th8_RsaKeyModulus returns pKey->zModulus and pKey->nModulus as a pair set together by the loader; a NULL key gives (zModulus=NULL, n=0) [drives the !zModulus C1 pair, covered], and a loaded key always has a non-NULL modulus with nonzero length, so the (zModulus!=NULL && nModulus==0) C2 independence pair is unformable.
- **`Th8_RsaExtractHash`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!zModulus || nModulus == 0`
  - Why it cannot be hit: Same key-modulus invariant as Th8_RsaVerify: Th8_RsaKeyModulus yields the modulus pointer and length together, so a non-NULL modulus always has nonzero length; the (zModulus!=NULL && nModulus==0) C2 pair is unformable while the !zModulus C1 pair is covered by a NULL key.
- **`Th8_RsaKeyLoad`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `nData >= 12 && th8ReadLE32(&zData[0]) == DOTNET_SIG_ALG_RSA`
  - Why it cannot be hit: The entry guard `if (!zData || nData < 20 || !ppKey) return TH8_ERROR` dominates this decision, so nData >= 20 is guaranteed here and the C1 `nData >= 12` arm is always true -- its (F,-) independence pair is unformable (a short blob is rejected earlier, at that already-covered guard). The C2 .NET-vs-CAPI format discriminator is covered by both a wrapped and a raw blob.
- **`th8ScanStoreInt`** (`src/plugins/th8_formatting.c`)
  - Decision: `c >= '0' && c <= '9'`
  - Why it cannot be hit: Digits are pre-validated by th8ScanDigitOk for the base, so c < '0' is unreachable in the value-accumulation loop.
- **`th8ScanStoreInt`** (`src/plugins/th8_formatting.c`)
  - Decision: `c >= 'a' && c <= 'f'`
  - Why it cannot be hit: The char is a validated hex digit (0-9a-fA-F); the c > 'f' non-hex gap is unreachable.
- **`scan_command`** (`src/plugins/th8_formatting.c`)
  - Decision: `bConvAttempted && !bEof`
  - Why it cannot be hit: BEof is set only when no conversion was attempted, so bConvAttempted implies !bEof; the both-true pair is unreachable.
- **`th8RemoveSubTokenEntry`** (`src/th8_core.c`)
  - Decision: `pSub->nToken && interp->paSubToken`
  - Why it cannot be hit: A sub-command being deleted always carries a non-zero token AND interp->paSubToken exists (created when the first tokened sub-command was added); only the (T,T) vector is reachable.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `interp->bOverflowCheck && iRight != 0 && ((iLeft > 0 && iRight > 0 && iLeft > TH8_INT64_MAX / iRight) || (iLeft < 0 && iRight < 0 && iLeft < TH8_INT64_MAX / iRight) || (iLeft > 0 && iRight < 0 && iRight < TH8_INT64_MIN / iLeft) || (iLeft < 0 && iRight > 0 && iLeft < TH8_INT64_MIN / iRight))`
  - Why it cannot be hit: MULTIPLY overflow: per-arm sign conditions (iLeft>0 vs iLeft<0 etc.) are mutually determined, so C9/C10/C12/C13 independence pairs are unformable by any operand.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `interp->bOverflowCheck && base != 0 && ((result > 0 && base > 0 && result > TH8_INT64_MAX / base) || (result < 0 && base < 0 && result < TH8_INT64_MAX / base) || (result > 0 && base < 0 && base < TH8_INT64_MIN / result) || (result < 0 && base > 0 && result < TH8_INT64_MIN / base))`
  - Why it cannot be hit: EXPONENT step-multiply overflow: correlated result/base sign conditions across binary-exponentiation steps; pairs unformable.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `exp > 1 && interp->bOverflowCheck && base != 0 && base != 1 && base != -1 && ((base > 0 && base > TH8_INT64_MAX / base) || (base < 0 && base < TH8_INT64_MAX / base))`
  - Why it cannot be hit: EXPONENT base-squaring overflow: correlated base-sign conditions; residual pairs unformable.
- **`th8SubsetFindCommandProc`** (`src/th8_lang.c`)
  - Decision: `th8StaticPlugins[pi].xGetCommands(NULL, &nCmd) != TH8_OK || nCmd <= 0`
  - Why it cannot be hit: Every static plugin GetCommands returns TH8_OK with a non-empty count; the failure/empty arms are unreachable from the built-in plugin set.
- **`Th8_GetSubsetMembers`** (`src/th8_lang.c`)
  - Decision: `pP->xGetCommands(NULL, &nCmd) == TH8_OK && nCmd > 0`
  - Why it cannot be hit: Every static plugin GetCommands returns TH8_OK with nCmd>0; the false arms (query fails, or zero commands) are unreachable from the built-in plugin set.

### defensive-omit-guard -- Defensive invariant guards (46)

These are runtime checks of an *internal invariant* -- for example that the expression parser always hands the evaluator a well-formed tree, or that a live object always carries the sub-field being dereferenced. Under correct operation the invariant always holds, so the check's "invariant violated" arm never runs and its independence pair cannot be formed.

The natural way to tell a coverage tool that an arm is dead is to wrap the condition in a "this never happens" macro, which folds it to a constant and excludes it from the metric. TH8 deliberately does NOT do that here, and the reason is specific to how MC/DC is measured. The MC/DC build is an *omit* build (`TH8_OMIT_AUXILIARY_SAFETY_CHECKS`), in which such a wrapped guard is COMPILED OUT entirely. If the invariant were ever violated -- by a future change, memory corruption, or an untrusted extension feeding in a malformed structure -- a compiled-out guard would be gone, and the code would dereference a NULL or garbage pointer and crash (SIGSEGV) instead of returning a clean error. Keeping the check as a plain `if` means that even in the omit build a violated invariant degrades to a graceful error rather than a crash.

The unavoidable consequence is that the guard's error arm is uncovered: it cannot be reached without actually violating the invariant, which no test can safely do. That coverage gap is the intended price of defense-in-depth, so the guard is waived rather than wrapped.

- **`th8SecureDecrypt`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `!pData->zCipher || pData->nCipher == 0`
  - Why it cannot be hit: A stored secure var always has ciphertext; empty-cipher arm is a defensive guard.
- **`th8SecureGetVar`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `!pEntry || !pEntry->pData`
  - Why it cannot be hit: Secure-var hash entry always carries pData while stored; the NULL arms are defensive guards on an internally-consistent structure.
- **`th8SecureLoad`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `pData2 && nUsable2 >= nCanary2`
  - Why it cannot be hit: Post-decrypt canary-region size guard; nUsable2 always covers the canary for a well-formed record.
- **`th8SecureSave`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `!pEntry || !pEntry->pData`
  - Why it cannot be hit: Secure-var hash entry always carries pData while stored; the NULL arms are defensive guards on an internally-consistent structure.
- **`th8SecureSave`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `pData8 && nUsable >= nCanary`
  - Why it cannot be hit: Pre-encrypt canary-region size guard; nUsable always covers the canary for a live record.
- **`th8SecureSetVar`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `!pEntry || !pEntry->pData`
  - Why it cannot be hit: Secure-var hash entry always carries pData while stored; the NULL arms are defensive guards on an internally-consistent structure.
- **`th8SecureVarCleanup`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `!pEntry || !pEntry->pData`
  - Why it cannot be hit: Secure-var hash entry always carries pData while stored; the NULL arms are defensive guards on an internally-consistent structure.
- **`th8SecureVarCleanup`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `pKS && pData->iSlot >= 0 && pData->iSlot < TH8_SECURE_MAX_SLOTS`
  - Why it cannot be hit: Secure-var key-slot index is always assigned in range when set; out-of-range arm is a defensive bound.
- **`th8SecureVarDelete`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `!pEntry || !pEntry->pData`
  - Why it cannot be hit: Secure-var hash entry always carries pData while stored; the NULL arms are defensive guards on an internally-consistent structure.
- **`th8SecureVarDelete`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `pKS && pData->iSlot >= 0 && pData->iSlot < TH8_SECURE_MAX_SLOTS`
  - Why it cannot be hit: Secure-var key-slot index is always assigned in range when set; out-of-range arm is a defensive bound.
- **`Th8_PolicyGetKeyTokens`** (`src/plugins/harpy/th8_policy.c`)
  - Decision: `pEntry->zKey && pEntry->nKey == 16`
  - Why it cannot be hit: A key token is always exactly 16 bytes; the !=16 arm is a defensive length guard.
- **`Th8_RsaKeyLoad`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `pKey->field && pKey->nField > 0`
  - Why it cannot be hit: Parsed RSA key field always non-empty when present; defensive field guard.
- **`Th8_RsaSign`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!zModulus || nModulus == 0 || !zPrivExp || nPrivExp == 0 || !zPrime1 || nPrime1 == 0 || !zPrime2 || nPrime2 == 0`
  - Why it cannot be hit: RSA private-key component NULL guard; all CRT components present for a valid signing key; defensive.
- **`th8RsaSignRawBlock`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!ppSig || !pnSig`
  - Why it cannot be hit: Defensive out-pointer guard in a TH8_INTERNAL test-only helper whose sole caller (the rsa_extract_mismatch probe) always passes &pSig/&nSig; the T arms are not reached by any in-tree caller, so their independence pairs cannot be formed.
- **`th8RsaSignRawBlock`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!zModulus || nModulus == 0 || !zPrivExp || nPrivExp == 0`
  - Why it cannot be hit: Reached only after the preceding Th8_RsaKeyHasPrivate(pKey) check has passed, which guarantees a fully-loaded private key with a non-NULL modulus and private exponent of nonzero length; all four T arms are unreachable given that dominating check, so their pairs are unformable.
- **`th8VerifyAnchorSig`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `!zAnchor || nAnchor == 0 || !zSig || nSig == 0`
  - Why it cannot be hit: Callers pass a read anchor+sig; NULL/empty arms are defensive.
- **`th8VerifyAnchorSig`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `zKey == NULL || nKey == 0`
  - Why it cannot be hit: Embedded key accessors never return NULL for a compiled-in key.
- **`scan_command`** (`src/plugins/th8_formatting.c`)
  - Decision: `iStr < maxEnd && (zStr[iStr] == '-' || zStr[iStr] == '+')`
  - Why it cannot be hit: The EOF check guarantees iStr < nStr and a positive width keeps maxEnd > iStr, so the iStr < maxEnd bound arm is redundant.
- **`Th8_ClonePlatform`** (`src/th8_core.c`)
  - Decision: `!pSrc || !pSrc->xMalloc`
  - Why it cannot be hit: Internal platform-clone NULL guard; pSrc and its xMalloc are always valid at the sole caller; kept plain for TH8_OMIT.
- **`Th8_CreateCommand`** (`src/th8_core.c`)
  - Decision: `pEntry && pEntry->pData`
  - Why it cannot be hit: A command hash entry always carries a live command: deletion removes the entry (op=-1) and rename moves-then-removes it, never leaving a persistent tombstone, so pEntry non-NULL implies pData non-NULL; the pData check is a defensive guard.
- **`Th8_DeleteCommand`** (`src/th8_core.c`)
  - Decision: `pCmd->zQualName && pCmd->nQualName > 0`
  - Why it cannot be hit: A registered command always has a qualified name; the NULL/zero-length arm is unreachable, kept as a defensive guard.
- **`th8UnlinkFreeNamespace`** (`src/th8_core.c`)
  - Decision: `pParent && pNs->zName`
  - Why it cannot be hit: A non-root namespace always has a parent and a name; the else arm is a defensive fallback.
- **`th8EvalLocal`** (`src/th8_core.c`)
  - Decision: `interp->nEvalDepth == 0 && (interp->pPendingCmdHead || interp->pPendingNsHead)`
  - Why it cannot be hit: Opportunistic early drain of the intrusive deferred-delete FIFO on th8EvalLocal's rare error exits (pre-eval policy reject / NRE-callback OOM at the outermost eval); the queue is otherwise drained at the next outermost-eval completion (covered) or at interp teardown, so the undriven drain arm cannot leak or use-after-free.
- **`Th8_FreePlatform`** (`src/th8_core.c`)
  - Decision: `pPlatform && pPlatform->xFree`
  - Why it cannot be hit: Internal free-platform NULL guard; xFree always present at the caller; plain for TH8_OMIT.
- **`Th8_ListAppendExpansions`** (`src/th8_core.c`)
  - Decision: `!pNs || !pNs->paExpansion`
  - Why it cannot be hit: Namespace-expansion NULL guard; paExpansion always allocated for a live namespace.
- **`Th8_SplitList`** (`src/th8_core.c`)
  - Decision: `nE > 0 && azSrc && anSrc`
  - Why it cannot be hit: Internal split output-array NULL guard; azSrc/anSrc always provided by the caller.
- **`th8AppendNsChildKeys`** (`src/th8_core.c`)
  - Decision: `pChild && pChild->zName`
  - Why it cannot be hit: Namespace child always has a name; NULL-name arm is a defensive fallback.
- **`th8ArraySearchIterEntry`** (`src/th8_core.c`)
  - Decision: `!pEntry || !pEntry->pData`
  - Why it cannot be hit: Hash-iter entry always carries pData for a live array-search; defensive guard.
- **`th8OversizeString`** (`src/th8_core.c`)
  - Decision: `p && p->xPanic`
  - Why it cannot be hit: Platform may be NULL here (falls back to the global platform) -- deliberate fallback, wrapping it deletes real behavior.
- **`th8CancelMsgBuild`** (`src/th8_core.c`)
  - Decision: `!interp->pPlatform || !interp->pPlatform->xMalloc`
  - Why it cannot be hit: TH8K-008 cross-thread cancel-message allocator NULL guard; interp->pPlatform and its validated mandatory xMalloc are always present on a live interpreter; kept plain for TH8_OMIT (mirrors Th8_ClonePlatform).
- **`th8CancelMsgFree`** (`src/th8_core.c`)
  - Decision: `pBuf && interp->pPlatform && interp->pPlatform->xFree`
  - Why it cannot be hit: TH8K-008 cross-thread cancel-message free guard; every caller guards pBuf non-NULL and the validated mandatory xFree is always present; kept plain for TH8_OMIT (mirrors Th8_FreePlatform).
- **`th8CommandSubCommands`** (`src/th8_core.c`)
  - Decision: `!interp || !zName`
  - Why it cannot be hit: [info subcommands] always dispatches with a live interpreter and a non-NULL command name; the NULL arms are unreachable from any caller.
- **`th8CommandExists`** (`src/th8_core.c`)
  - Decision: `interp && zName && th8LookupCommand(interp, zName, nName) != NULL`
  - Why it cannot be hit: The subset-apply callers always pass a live interpreter and a non-NULL name; interp/zName are always true, so their independence pairs cannot be formed.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `!pExpr->pRight || !pExpr->pRight->pOp || pExpr->pRight->pOp->eOp != TH8_OP_TERNARY_C`
  - Why it cannot be hit: Parser-invariant ternary guard; plain-if kept so TH8_OMIT degrades to a graceful error not a SEGV; error arm unreachable under correct parser output.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `!pExpr->pLeft || !pExpr->pRight`
  - Why it cannot be hit: = parser-invariant guard; both operands bound before eval; plain-if for TH8_OMIT safety.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `(zLeft == 0 || TH8_OK == Th8_ToWideInt(0, zLeft, nLeft, &iLeft)) && (zRight == 0 || TH8_OK == Th8_ToWideInt(0, zRight, nRight, &iRight))`
  - Why it cannot be hit: NUMBER-branch NULL guard; zLeft/zRight are non-NULL under correct phase-2 binding; kept plain for TH8_OMIT.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `Th8_IsBigintEnabled(interp) && (zLeft == 0 || th8IsBigint(interp, zLeft, nLeft) || TH8_OK == Th8_ToWideInt(0, zLeft, nLeft, &iLeft)) && (zRight == 0 || th8IsBigint(interp, zRight, nRight) || TH8_OK == Th8_ToWideInt(0, zRight, nRight, &iRight))`
  - Why it cannot be hit: Bigint NUMBER-branch NULL guard (zLeft/zRight==0 arms unreachable under correct binding).
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `(zLeft && TH8_OK != Th8_ToDouble(interp, zLeft, nLeft, &fLeft)) || (zRight && TH8_OK != Th8_ToDouble(interp, zRight, nRight, &fRight))`
  - Why it cannot be hit: Double fall-through NULL guard; zLeft/zRight non-NULL under correct binding; plain for TH8_OMIT.
- **`th8ExprEvalNode`** (`src/th8_expr.c`)
  - Decision: `Th8_IsBigintEnabled(interp) && ((zLeft && th8IsBigint(interp, zLeft, nLeft)) || (zRight && th8IsBigint(interp, zRight, nRight)))`
  - Why it cannot be hit: Bigint NUMBER-branch NULL guard (zLeft/zRight==0 arms unreachable under correct binding).
- **`th8PosixFaultErrno`** (`src/th8_posix.c`)
  - Decision: `th8FaultActiveCfg != NULL && th8FaultActiveCfg->nFailPosixErrno > 0`
  - Why it cannot be hit: Redundant cfg!=NULL recheck (th8PosixSyscallTrip already guaranteed non-NULL before this is called); C1=F unreachable.
- **`th8PosixFaultShort`** (`src/th8_posix.c`)
  - Decision: `th8FaultActiveCfg != NULL && th8FaultActiveCfg->nFailPosixErrno < 0`
  - Why it cannot be hit: Redundant cfg!=NULL recheck (trip guarantees non-NULL); C1=F unreachable.
- **`th8PosixPanic`** (`src/th8_posix.c`)
  - Decision: `zMsg && nMsg > 0`
  - Why it cannot be hit: XPanic fatal-abort handler; only runs while aborting the process, so not reachable in a normal test run.
- **`th8PosixReadFile`** (`src/th8_posix.c`)
  - Decision: `!zPath || !zBuf || nBuf == 0`
  - Why it cannot be hit: Internal callers always pass a valid path/buffer; NULL/zero arms are defensive.
- **`th8PosixIsPathUnderBase`** (`src/th8_posix.c`)
  - Decision: `n > 0 && depth < TH8_CWD_SEG_MAX`
  - Why it cannot be hit: Path-segment depth bound; realistic paths never reach TH8_CWD_SEG_MAX segments.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `!interp || !pOps || !zName || !ppResult`
  - Why it cannot be hit: Internal callers pass non-NULL args; the NULL arms are defensive.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `nName == 0 || nName >= sizeof(zHostBuf)`
  - Why it cannot be hit: Callers pass a bounded non-empty name; length arms are defensive.

### overflow-guard -- Overflow / size-ceiling guards (4)

A belt-and-suspenders integer-overflow or size-ceiling check whose failing arm cannot occur given the operand type's range or the value's provenance -- e.g. `n + 1 < n` where `n` is bounded well below `SIZE_MAX`, a path-length ceiling of 100000 bytes, or an RSA-signature-length bound that a valid (modulus-sized) signature never exceeds. The guard is cheap insurance against a future change; its overflow arm is unreachable today.

- **`Th8_RsaSign`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `nSig > nModulus + 64 || nSig > TH8_RSA_MAX_SIG_BYTES`
  - Why it cannot be hit: RSA signature-length ceiling; a valid RSA signature is exactly modulus-sized, so the >modulus+64 arm is unreachable.
- **`Th8_RsaVerify`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `nSig > nModulus + 64 || nSig > TH8_RSA_MAX_SIG_BYTES`
  - Why it cannot be hit: RSA signature-length ceiling; a valid RSA signature is exactly modulus-sized, so the >modulus+64 arm is unreachable.
- **`Th8_SetResultSensitive`** (`src/th8_core.c`)
  - Decision: `n + 1 < n || n + 1 > nUsable`
  - Why it cannot be hit: N+1<n integer-overflow guard; unreachable given n <= 268M.
- **`th8PosixResolveAbsolute`** (`src/th8_posix.c`)
  - Decision: `nCwd > 100000 || nPath > 100000`
  - Why it cannot be hit: Defensive path-length ceiling (>100000 bytes); unreachable in practice, kept as a belt-and-suspenders bound.

### reserved-value-reject -- Reserved-value rejection loops (3)

A loop that re-draws a cryptographic-strength random token until it is not a reserved sentinel (`0`, `~0`, or `1`). The sentinel arms fire only when a full 64-bit CSPRNG draw lands on exactly one of those three values -- astronomically improbable in a test run -- and the sentinels gate no externally observable behavior. (They could in principle be forced via the `forceRandomBytes` test hook; they are waived rather than driven because the resulting test would exercise a contrived, behaviorally-inert path.)

- **`Th8_EnableSecurePersist`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `tok == 0 || tok == ~(th8_int64_t)0 || tok == 1`
  - Why it cannot be hit: CSPRNG token sentinel-rejection loop (forceRandomBytes-drivable; sentinels gate no observable behavior).
- **`Th8_EnableBigint`** (`src/th8_core.c`)
  - Decision: `tok == 0 || tok == ~(th8_int64_t)0 || tok == 1`
  - Why it cannot be hit: CSPRNG token sentinel-rejection loop; exact-draw arms (forceRandomBytes-drivable).
- **`Th8_EnableSignedOnly`** (`src/th8_core.c`)
  - Decision: `tok == 0 || tok == ~(th8_int64_t)0 || tok == 1`
  - Why it cannot be hit: CSPRNG token sentinel-rejection loop; exact-draw arms (forceRandomBytes-drivable).

### startup-cached-callback -- Startup-cached platform callbacks (5)

A platform callback that runs ONCE during interpreter or process initialization and caches its result -- the user name via `getpwuid`, or the executable/module/base path via `dladdr`. By the time any script, test, or runtime fault could act, the callback has already run and its value is cached at file scope; it is not re-invoked, so a runtime fixture cannot re-drive its alternative arms. (`pw->pw_name` etc. are also never NULL when the lookup succeeds.)

- **`Th8_Finalize`** (`src/th8_core.c`)
  - Decision: `Th8_IntCmpXchg(NULL, &th8GlobalMutexReady, 0, 1) == 1 && th8GlobalPlatform.xMutexFinal`
  - Why it cannot be hit: Process-teardown once-only path via the global-mutex CAS; runs at Th8_Finalize, not re-triggerable mid-test.
- **`th8PosixFindStaticAnchorPath`** (`src/th8_posix.c`)
  - Decision: `th8PosixGetModuleAnchorPath(zBuf, nBuf) && th8PosixPathReadable(zBuf)`
  - Why it cannot be hit: Static trust-anchor path discovery via the startup-cached module-anchor resolver.
- **`th8PosixGetBasePath`** (`src/th8_posix.c`)
  - Decision: `dladdr(uAddr.p, &info) && info.dli_fname`
  - Why it cannot be hit: Base-path detection via dladdr runs once at startup and is file-scope cached; a runtime fault cannot re-trigger it.
- **`th8PosixGetModuleAnchorPath`** (`src/th8_posix.c`)
  - Decision: `!dladdr(uAddr.p, &info) || !info.dli_fname`
  - Why it cannot be hit: Module-anchor dladdr resolution; startup-cached, not re-triggerable at runtime.
- **`th8PosixGetUserName`** (`src/th8_posix.c`)
  - Decision: `pw && pw->pw_name`
  - Why it cannot be hit: Getpwuid runs once at interp init to set tcl_platform(user); no runtime re-trigger, and pw_name is never NULL when pw is non-NULL.

### environmental -- Environmental (host/filesystem) limits (19)

Requires a host or filesystem condition that cannot be synthesized in the sandbox: two paths under a single-filesystem sandbox base that differ in `st_dev` (a cross-device comparison -- impossible when the base is one filesystem), a symlink chain 256 levels deep, or a file larger than 256 MB. These need environmental fixtures, not scripts.

- **`Th8_RsaSign`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `OSSL_CALL( TH8_OSSL_OP_S_DSFINAL1, EVP_DigestSignFinal(mdctx, NULL, &nSig)) != 1 || nSig == 0`
  - Why it cannot be hit: EVP_DigestSignFinal size-probe error guard; it fails only on an OpenSSL-internal signing/provider error, not injectable via TH8's allocator fault harness, and a validated RSA key never yields nSig==0.
- **`th8RsaSignRawBlock`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `EVP_PKEY_sign(sctx, NULL, &nSig, zBlock, nBlock) != 1 || nSig == 0`
  - Why it cannot be hit: EVP_PKEY_sign size-probe error guard; it fails only on an OpenSSL-internal signing error, not injectable via TH8's harness, and a validated RSA key never yields nSig==0 (same class as the Th8_RsaSign DigestSignFinal size-probe guard).
- **`th8VerifyAnchorSig`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `Th8_HarpySigLoad(interp, zSig, nSig, &pSig, &nSigBytes, &zId) != TH8_OK || pSig == NULL || nSigBytes == 0`
  - Why it cannot be hit: Needs a malformed .b64sig anchor override on disk.
- **`th8VerifyAnchorSig`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `ki < 2 && rc != TH8_OK`
  - Why it cannot be hit: Key-iteration fallback needs a signed anchor under an alternate embedded key.
- **`th8PosixGetData`** (`src/th8_posix.c`)
  - Decision: `nSize < 0 || nSize > 0x0fffffff`
  - Why it cannot be hit: File-size guard; nSize<0 impossible for a regular file and >256MB needs a 256MB+ fixture (or an fstat-size fault mode not yet built).
- **`th8PosixSameFile`** (`src/th8_posix.c`)
  - Decision: `st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino`
  - Why it cannot be hit: Cross-device (st_dev !=) pair needs two paths under the sandbox base on DIFFERENT filesystems; impossible with a single-filesystem base.
- **`th8PosixDeriveBaseFromModule`** (`src/th8_posix.c`)
  - Decision: `!slash || slash == resolved`
  - Why it cannot be hit: Module path shape (slash-less/root-level) not reproducible from the test binary layout.
- **`th8PosixDeriveBaseFromModule`** (`src/th8_posix.c`)
  - Decision: `tail && (th8PosixStrcmp(tail + 1, "bin") == 0 || th8PosixStrcmp(tail + 1, "bin-afl") == 0 || th8PosixStrcmp(tail + 1, "bin-static") == 0 || th8PosixStrcmp(tail + 1, "lib") == 0)`
  - Why it cannot be hit: Matches only for a bin/bin-afl/bin-static/lib executable dir; the test binary dir is fixed.
- **`Th8_SetBasePath`** (`src/th8_posix.c`)
  - Decision: `nPath == 1 && zPath[0] == '.'`
  - Why it cannot be hit: Base-path is set once at init to "."; the non-"." arm needs an alternate embedding.
- **`th8PosixStrcmp`** (`src/th8_posix.c`)
  - Decision: `*a && *a == *b`
  - Why it cannot be hit: Reached only from th8PosixDeriveBaseFromModule comparing the module dir tail to bin/lib; exercised by the base-path derivation, not scriptable.
- **`th8PosixFindHandle`** (`src/th8_posix.c`)
  - Decision: `p->nName == nName && 0 == Th8_Memcmp(interp, p->zName, zName, nName)`
  - Why it cannot be hit: [load] handle-table name match requires a real loadable shared library fixture.
- **`th8PosixRemoveHandle`** (`src/th8_posix.c`)
  - Decision: `p->nName == nName && 0 == Th8_Memcmp(interp, p->zName, zName, nName)`
  - Why it cannot be hit: [unload] handle removal requires a real loaded shared library to remove.
- **`th8PosixUnload`** (`src/th8_posix.c`)
  - Decision: `zProc && nProc > 0`
  - Why it cannot be hit: [unload] cleanup-proc arm requires a loaded library exposing an unload entry point.
- **`th8PosixIsUnderBase`** (`src/th8_posix.c`)
  - Decision: `nAbs == nBase && memcmp(zAbs, zBase, nBase) == 0`
  - Why it cannot be hit: The exact-equal-to-base arm depends on the deployed base path, not reproducible from a script.
- **`th8UnboundVerifyTh8Anchor`** (`src/th8_unbound.c`)
  - Decision: `!pOps->xReadFile(zPath, anchorBuf, sizeof(anchorBuf), &nAnchor) || nAnchor == 0`
  - Why it cannot be hit: Anchor read outcome depends on a deployed bundled root.key.
- **`th8UnboundVerifyTh8Anchor`** (`src/th8_unbound.c`)
  - Decision: `pOps->xReadFile(zSigPath, sigBuf, sizeof(sigBuf), &nSig) && nSig > 0`
  - Why it cannot be hit: Companion .b64sig read depends on a deployed sidecar.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `bHaveSrc && pOps->xGetModuleAnchorPath`
  - Why it cannot be hit: Module-adjacent anchor selection needs a bundled root.key next to the library.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `pOps->xGetModuleAnchorPath(zMod, sizeof(zMod)) && Th8_Strlen(interp, zMod) == nSrc && Th8_Memcmp(interp, zSrc, zMod, nSrc) == 0 && !th8UnboundVerifyTh8Anchor(pOps, interp, zSrc)`
  - Why it cannot be hit: Module-anchor identity+verify chain needs a deployed signed bundle.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `!bHaveTa && bHaveSrc`
  - Why it cannot be hit: Trust-anchor source selection depends on deployed anchors.

### win32-only -- Windows-only code paths (3)

A code path selected only on Windows. The whole block is gated -- directly or transitively -- on `TH8_IS_SEP('\\')` (backslash treated as a path separator), which is false on the POSIX host where MC/DC is measured, so the path is never entered.

- **`file_validname_command`** (`src/plugins/th8_filesystems.c`)
  - Decision: `c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*'`
  - Why it cannot be hit: Win32-reserved-filename-char rejection; the whole block is gated on TH8_IS_SEP('\\') (backslash-as-separator), false on the POSIX MC/DC host so unreachable.
- **`file_validname_command`** (`src/plugins/th8_filesystems.c`)
  - Decision: `i < n && !th8IsPathSep(z[i])`
  - Why it cannot be hit: Path-component scan inside the TH8_IS_SEP('\\')-gated Win32 reserved-device-name check; POSIX-unreachable.
- **`file_validname_command`** (`src/plugins/th8_filesystems.c`)
  - Decision: `i < n && th8IsPathSep(z[i])`
  - Why it cannot be hit: Path-separator skip inside the TH8_IS_SEP('\\')-gated Win32 validname block; POSIX-unreachable.

### network -- Network-gated conditions (21)

The condition sits behind a live network operation -- DNS resolution (`getaddrinfo`, libunbound), an NTP query, or an HTTPS fetch -- or is only reached after such an operation returns data to parse or verify. Exercising it needs a real network or a mock that has not been built; the MC/DC host runs offline.

- **`th8HttpsTimeQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `Th8_SplitList( interp, zData, nData, &azOuter, &anOuter, &nOuter, TH8_LIST_NONE) != TH8_OK || nOuter < 2`
  - Why it cannot be hit: Parse of the HTTPS-time server response; needs a live HTTPS fetch.
- **`th8HttpsTimeQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `!zNonce || !zTs`
  - Why it cannot be hit: HTTPS-time response field check; live HTTPS fetch.
- **`th8HttpsTimeQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `nNonceR != TH8_TIME_NONCE_BYTES * 2 || Th8_Memcmp(interp, zNonce, zNonceHex, nNonceR) != 0`
  - Why it cannot be hit: HTTPS-time nonce echo check; live HTTPS fetch.
- **`th8HttpsTimeQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `epochSec < 1577836800LL || epochSec > 4102444800LL`
  - Why it cannot be hit: HTTPS-time sanity-range check on the returned epoch; live HTTPS fetch.
- **`th8HttpsTimeQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `lastNtp > 0 && lastLocal > 0`
  - Why it cannot be hit: HTTPS-time cached-state check; live fetch.
- **`th8HttpsTimeVerifySignature`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `rc != TH8_OK || !pSig || nSig == 0`
  - Why it cannot be hit: HTTPS-time response signature verify; reached only with a live HTTPS time response.
- **`th8NtpQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `lastNtp > 0 && lastLocal > 0`
  - Why it cannot be hit: Cached NTP time state; populated only by a prior live NTP query.
- **`th8NtpSortTimes`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `j >= 0 && a[j] > key`
  - Why it cannot be hit: Insertion sort over collected NTP offsets; only reached after live NTP responses.
- **`th8NtpResolveInsecure`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `getaddrinfo(zServer, "123", &hints, &res) != 0 || !res`
  - Why it cannot be hit: Getaddrinfo outcome needs a live-but-failing DNS lookup.
- **`th8NtpResolveInsecure`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `rp->ai_addrlen == 0 || (size_t)rp->ai_addrlen > sizeof(addrs[nAddrs].sa)`
  - Why it cannot be hit: Per-address size guard over live getaddrinfo results.
- **`th8NtpQueryOne`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `Th8_DnsResolve( interp, zServer, Th8_Strlen(interp, zServer), aTypes[ti], &pDns) != TH8_OK || pDns == NULL`
  - Why it cannot be hit: DNSSEC resolve outcome needs a live validating resolver.
- **`th8NtpQueryOne`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `pDns->nRecord > 0 && !pDns->secure`
  - Why it cannot be hit: Insecure-answer arm needs a live unsigned zone.
- **`th8NtpQueryOne`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `pDns->pData != NULL && i < pDns->nRecord && nAddrs < NTP_MAX_ADDRS`
  - Why it cannot be hit: Validated-address copy loop needs live DNSSEC records.
- **`th8HttpsTimeQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `!zUrl || nUrl == 0`
  - Why it cannot be hit: HTTPS-time default-URL path exercised only under live clock https.
- **`th8NtpResolveInsecure`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `rp != NULL && nAddrs < nMax`
  - Why it cannot be hit: Getaddrinfo address loop runs only over live-resolved addresses.
- **`th8NtpQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `!azServers || nServers <= 0`
  - Why it cannot be hit: The explicit-server (azServers set) arm is reached only by network-gated clock ntp -server tests.
- **`th8PosixEnsureParentDir`** (`src/th8_posix.c`)
  - Decision: `mkdir(zScratch, 0700) != 0 && errno != EEXIST`
  - Why it cannot be hit: Only caller is th8_unbound.c xEnsureParentDir (DNSSEC trust-anchor write); mkdir-fail arm needs libunbound + the DNS trust-anchor path.
- **`th8PosixGetManagedAnchorPath`** (`src/th8_posix.c`)
  - Decision: `zBase && *zBase`
  - Why it cannot be hit: DNSSEC managed trust-anchor path discovery; reachable only on the libunbound managed-key path.
- **`th8PosixGetManagedAnchorPath`** (`src/th8_posix.c`)
  - Decision: `!zBase || !*zBase`
  - Why it cannot be hit: DNSSEC managed trust-anchor path discovery (inverse guard); libunbound managed-key path only.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `zEnv && *zEnv`
  - Why it cannot be hit: Trust-anchor-source env override on the libunbound path; DNS-only.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `ub_resolve(ubctx, zHostBuf, eType, 1 /* IN */, &ubr) != 0 || !ubr`
  - Why it cannot be hit: Libunbound DNS resolve; live DNS.

### external-oom-unreachable -- External-library OOM guards (6)

An error-return guard on an EXTERNAL library's API -- OpenSSL big-number allocation (`BN_new`/`BN_CTX_new`/`BN_dup`), parameter-builder (`OSSL_PARAM_BLD_push_BN`), and key-import (`EVP_PKEY_fromdata`) calls -- whose failing arm fires only under that library's INTERNAL out-of-memory or internal error. TH8's fault-injection harness hooks TH8's own allocator, not OpenSSL's, so it cannot induce an OpenSSL OOM. This is distinct from TH8's *own* allocation-failure paths, which ARE reachable via the alloc-fault harness and remain tracked debt, not waivers.

- **`Th8_RsaExtractHash`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) || !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e)`
  - Why it cannot be hit: OSSL_PARAM_BLD_push_BN error-return guard; the push fails only on OpenSSL-internal OOM, not injectable via TH8's allocator fault harness.
- **`Th8_RsaExtractHash`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `EVP_PKEY_fromdata_init(kctx) != 1 || EVP_PKEY_fromdata(kctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) != 1`
  - Why it cannot be hit: EVP_PKEY_fromdata_init/fromdata error-return guard; in the extract path the RSA params come from the already-validated modulus, so these fail only on OpenSSL-internal OOM/error, not injectable via TH8's harness.
- **`Th8_RsaSign`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!bnctx || !pm1 || !qm1 || !bn_dmp1 || !bn_dmq1 || !bn_iqmp`
  - Why it cannot be hit: OpenSSL BN allocation (BN_CTX_new/BN_new/BN_dup) NULL guards; fail only under OpenSSL-internal OOM, which TH8's fault harness (scoped to TH8's own allocator) cannot inject.
- **`th8RsaSignRawBlock`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `!OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn_n) || !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, bn_e) || !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_D, bn_d)`
  - Why it cannot be hit: OSSL_PARAM_BLD_push_BN error-return guards; the pushes fail only on OpenSSL-internal OOM, not injectable via TH8's allocator fault harness (same class as the identical Th8_RsaExtractHash push guard).
- **`th8RsaSignRawBlock`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `EVP_PKEY_fromdata_init(kctx) != 1 || EVP_PKEY_fromdata(kctx, &pkey, EVP_PKEY_KEYPAIR, params) != 1`
  - Why it cannot be hit: EVP_PKEY_fromdata_init/fromdata error-return guard; with a valid n/e/d parameter set these fail only on OpenSSL-internal OOM/error, not injectable via TH8's harness (same class as the identical Th8_RsaExtractHash fromdata guard).
- **`th8RsaSignRawBlock`** (`src/plugins/harpy/th8_snk.c`)
  - Decision: `EVP_PKEY_sign_init(sctx) != 1 || EVP_PKEY_CTX_set_rsa_padding(sctx, RSA_PKCS1_PADDING) != 1`
  - Why it cannot be hit: EVP_PKEY_sign_init / set-padding error-return guard; fails only on an OpenSSL-internal error, not injectable via TH8's allocator fault harness.

### synchronization-invasive -- Synchronization-invasive arms (3)

Driving the arm requires faking a `pthread_cond_wait` / `pthread_cond_timedwait` return code, or forcing a precise signal-arrives-exactly-at-timeout race, in the event-wait path. Either would perturb real synchronization; the arm is not safely or deterministically reachable from a test.

- **`Th8_Initialize`** (`src/th8_core.c`)
  - Decision: `bXInit && pPlatform->xFinalize`
  - Why it cannot be hit: TH8K-004 init rollback path; reachable only from an uninitialized global, which cannot be reproduced mid-suite because Th8_Finalize calls mi_thread_done() and tears down the main-thread mimalloc heap that every live interpreter object depends on; verified by inspection (mirrors Th8_Finalize).
- **`th8PosixEventWait`** (`src/th8_posix.c`)
  - Decision: `r != 0 && r != EINTR`
  - Why it cannot be hit: Pthread_cond_wait/timedwait return arm; driving needs a faked pthread return code that perturbs real synchronization.
- **`th8PosixEventWait`** (`src/th8_posix.c`)
  - Decision: `e->signaled && rc == 1`
  - Why it cannot be hit: Timed event-wait signaled-at-timeout race; not deterministically drivable without perturbing synchronization.

## Maintenance

This file is GENERATED -- do not edit it by hand.  The source of truth is
`tools/data/mcdc_waivers.tsv` (one tab-separated row per waiver: file,
function, decision source-text, class, rationale).  Regenerate with
`tclsh tools/mkmcdcwaiverdoc.tcl`.  `make check-mcdc-doc` (part of the audit
gate) fails if this file is out of date, so every waiver declared in the TSV
must appear here to be valid.  `make check-mcdc` separately rejects a stale
waiver whose decision is now covered or gone.
