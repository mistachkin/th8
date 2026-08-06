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

As of the last generation there are **81 waivers**.  A decision may
contribute more than one waived condition; the gate accounts for conditions,
this register lists decisions.

## Waiver classes

### correlated-conditions -- Correlated conditions (3)

MC/DC requires each condition of a decision to be shown to *independently* change the decision's outcome -- an "independence pair" of test vectors in which only that condition differs. In these decisions the conditions are structurally correlated, so no choice of inputs can form the pair. The clearest case is a sign test on one operand: `iLeft > 0` and `iLeft < 0` are two *separate* conditions to the coverage tool, but they are mutually determined by the single value of `iLeft` -- you cannot make one true and the other true, nor flip one while holding the rest such that the outcome changes. The missing pairs are therefore unformable *by construction*, not merely un-exercised.

- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `interp->bOverflowCheck && iRight != 0 && ((iLeft > 0 && iRight > 0 && iLeft > TH8_INT64_MAX / iRight) || (iLeft < 0 && iRight < 0 && iLeft < TH8_INT64_MAX / iRight) || (iLeft > 0 && iRight < 0 && iRight < TH8_INT64_MIN / iLeft) || (iLeft < 0 && iRight > 0 && iLeft < TH8_INT64_MIN / iRight))`
  - Why it cannot be hit: MULTIPLY overflow: per-arm sign conditions (iLeft>0 vs iLeft<0 etc.) are mutually determined, so C9/C10/C12/C13 independence pairs are unformable by any operand.
- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `interp->bOverflowCheck && base != 0 && ((result > 0 && base > 0 && result > TH8_INT64_MAX / base) || (result < 0 && base < 0 && result < TH8_INT64_MAX / base) || (result > 0 && base < 0 && base < TH8_INT64_MIN / result) || (result < 0 && base > 0 && result < TH8_INT64_MIN / base))`
  - Why it cannot be hit: EXPONENT step-multiply overflow: correlated result/base sign conditions across binary-exponentiation steps; pairs unformable.
- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `exp > 1 && interp->bOverflowCheck && base != 0 && base != 1 && base != -1 && ((base > 0 && base > TH8_INT64_MAX / base) || (base < 0 && base < TH8_INT64_MAX / base))`
  - Why it cannot be hit: EXPONENT base-squaring overflow: correlated base-sign conditions; residual pairs unformable.

### defensive-omit-guard -- Defensive invariant guards (31)

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
- **`Th8_ClonePlatform`** (`src/th8_core.c`)
  - Decision: `!pSrc || !pSrc->xMalloc`
  - Why it cannot be hit: Internal platform-clone NULL guard; pSrc and its xMalloc are always valid at the sole caller; kept plain for TH8_OMIT.
- **`Th8_DeleteCommand`** (`src/th8_core.c`)
  - Decision: `pCmd->zQualName && pCmd->nQualName > 0`
  - Why it cannot be hit: A registered command always has a qualified name; the NULL/zero-length arm is unreachable, kept as a defensive guard.
- **`Th8_DeleteNamespace`** (`src/th8_core.c`)
  - Decision: `pParent && pNs->zName`
  - Why it cannot be hit: A non-root namespace always has a parent and a name; the else arm is a defensive fallback.
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
- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `!pExpr->pRight || !pExpr->pRight->pOp || pExpr->pRight->pOp->eOp != TH8_OP_TERNARY_C`
  - Why it cannot be hit: Parser-invariant ternary guard; plain-if kept so TH8_OMIT degrades to a graceful error not a SEGV; error arm unreachable under correct parser output.
- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `!pExpr->pLeft || !pExpr->pRight`
  - Why it cannot be hit: = parser-invariant guard; both operands bound before eval; plain-if for TH8_OMIT safety.
- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `(zLeft == 0 || TH8_OK == Th8_ToWideInt(0, zLeft, nLeft, &iLeft)) && (zRight == 0 || TH8_OK == Th8_ToWideInt(0, zRight, nRight, &iRight))`
  - Why it cannot be hit: NUMBER-branch NULL guard; zLeft/zRight are non-NULL under correct phase-2 binding; kept plain for TH8_OMIT.
- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `Th8_IsBigintEnabled(interp) && (zLeft == 0 || th8IsBigint(interp, zLeft, nLeft) || TH8_OK == Th8_ToWideInt(0, zLeft, nLeft, &iLeft)) && (zRight == 0 || th8IsBigint(interp, zRight, nRight) || TH8_OK == Th8_ToWideInt(0, zRight, nRight, &iRight))`
  - Why it cannot be hit: Bigint NUMBER-branch NULL guard (zLeft/zRight==0 arms unreachable under correct binding).
- **`th8ExprEval`** (`src/th8_expr.c`)
  - Decision: `(zLeft && TH8_OK != Th8_ToDouble(interp, zLeft, nLeft, &fLeft)) || (zRight && TH8_OK != Th8_ToDouble(interp, zRight, nRight, &fRight))`
  - Why it cannot be hit: Double fall-through NULL guard; zLeft/zRight non-NULL under correct binding; plain for TH8_OMIT.
- **`th8ExprEval`** (`src/th8_expr.c`)
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

### reserved-value-reject -- Reserved-value rejection loops (4)

A loop that re-draws a cryptographic-strength random token until it is not a reserved sentinel (`0`, `~0`, or `1`). The sentinel arms fire only when a full 64-bit CSPRNG draw lands on exactly one of those three values -- astronomically improbable in a test run -- and the sentinels gate no externally observable behavior. (They could in principle be forced via the `forceRandomBytes` test hook; they are waived rather than driven because the resulting test would exercise a contrived, behaviorally-inert path.)

- **`Th8_EnableSecurePersist`** (`src/plugins/crypto/th8_secure.c`)
  - Decision: `tok == 0 || tok == ~(th8_int64_t)0 || tok == 1`
  - Why it cannot be hit: CSPRNG token sentinel-rejection loop (forceRandomBytes-drivable; sentinels gate no observable behavior).
- **`Th8_CreateInterp`** (`src/th8_core.c`)
  - Decision: `tok == 0 || tok == ~0 || tok == 1`
  - Why it cannot be hit: CSPRNG interp-token sentinel-rejection loop; the ==0/==~0/==1 arms fire only for exact 64-bit draws (forceRandomBytes could drive them, but the sentinels gate no observable behavior).
- **`Th8_EnableBigint`** (`src/th8_core.c`)
  - Decision: `tok == 0 || tok == ~(th8_int64_t)0 || tok == 1`
  - Why it cannot be hit: CSPRNG token sentinel-rejection loop; exact-draw arms (forceRandomBytes-drivable).
- **`Th8_EnableSignedOnly`** (`src/th8_core.c`)
  - Decision: `tok == 0 || tok == ~(th8_int64_t)0 || tok == 1`
  - Why it cannot be hit: CSPRNG token sentinel-rejection loop; exact-draw arms (forceRandomBytes-drivable).

### startup-cached-callback -- Startup-cached platform callbacks (8)

A platform callback that runs ONCE during interpreter or process initialization and caches its result -- the user name via `getpwuid`, or the executable/module/base path via `dladdr`. By the time any script, test, or runtime fault could act, the callback has already run and its value is cached at file scope; it is not re-invoked, so a runtime fixture cannot re-drive its alternative arms. (`pw->pw_name` etc. are also never NULL when the lookup succeeds.)

- **`Th8_Finalize`** (`src/th8_core.c`)
  - Decision: `Th8_IntCmpXchg(NULL, &th8GlobalMutexReady, 0, 1) == 1 && th8GlobalPlatform.xMutexFinal`
  - Why it cannot be hit: Process-teardown once-only path via the global-mutex CAS; runs at Th8_Finalize, not re-triggerable mid-test.
- **`Th8_Initialize`** (`src/th8_core.c`)
  - Decision: `Th8_IntCmpXchg(NULL, &th8GlobalMutexReady, 1, 0) == 0 && pPlatform->xMutexInit`
  - Why it cannot be hit: Process-init once-only path via the global-mutex CAS; runs once at first Th8_Initialize.
- **`th8PosixFindStaticAnchorPath`** (`src/th8_posix.c`)
  - Decision: `th8PosixGetModuleAnchorPath(zBuf, nBuf) && th8PosixPathReadable(zBuf)`
  - Why it cannot be hit: Static trust-anchor path discovery via the startup-cached module-anchor resolver.
- **`th8PosixGetBasePath`** (`src/th8_posix.c`)
  - Decision: `dladdr(uAddr.p, &info) && info.dli_fname`
  - Why it cannot be hit: Base-path detection via dladdr runs once at startup and is file-scope cached; a runtime fault cannot re-trigger it.
- **`th8PosixGetBasePath`** (`src/th8_posix.c`)
  - Decision: `slash && slash != resolved`
  - Why it cannot be hit: Base-path realpath parsing; computed once at startup and cached.
- **`th8PosixGetBasePath`** (`src/th8_posix.c`)
  - Decision: `tail && (th8PosixStrcmp(tail + 1, "bin") == 0 || th8PosixStrcmp(tail + 1, "bin-afl") == 0 || th8PosixStrcmp(tail + 1, "bin-static") == 0 || th8PosixStrcmp(tail + 1, "lib") == 0)`
  - Why it cannot be hit: Base-path bin/lib directory classification; startup-cached.
- **`th8PosixGetModuleAnchorPath`** (`src/th8_posix.c`)
  - Decision: `!dladdr(uAddr.p, &info) || !info.dli_fname`
  - Why it cannot be hit: Module-anchor dladdr resolution; startup-cached, not re-triggerable at runtime.
- **`th8PosixGetUserName`** (`src/th8_posix.c`)
  - Decision: `pw && pw->pw_name`
  - Why it cannot be hit: Getpwuid runs once at interp init to set tcl_platform(user); no runtime re-trigger, and pw_name is never NULL when pw is non-NULL.

### environmental -- Environmental (host/filesystem) limits (3)

Requires a host or filesystem condition that cannot be synthesized in the sandbox: two paths under a single-filesystem sandbox base that differ in `st_dev` (a cross-device comparison -- impossible when the base is one filesystem), a symlink chain 256 levels deep, or a file larger than 256 MB. These need environmental fixtures, not scripts.

- **`th8PosixGetData`** (`src/th8_posix.c`)
  - Decision: `nSize < 0 || nSize > 0x0fffffff`
  - Why it cannot be hit: File-size guard; nSize<0 impossible for a regular file and >256MB needs a 256MB+ fixture (or an fstat-size fault mode not yet built).
- **`th8PosixIsPathUnderBase`** (`src/th8_posix.c`)
  - Decision: `n > 0 && depth < 256`
  - Why it cannot be hit: Symlink-depth guard; the depth<256 arm needs a 256-deep symlink chain, not feasible as a test fixture.
- **`th8PosixSameFile`** (`src/th8_posix.c`)
  - Decision: `st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino`
  - Why it cannot be hit: Cross-device (st_dev !=) pair needs two paths under the sandbox base on DIFFERENT filesystems; impossible with a single-filesystem base.

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

### network -- Network-gated conditions (20)

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
  - Decision: `nGood < (nServers + 1) / 2 || nGood == 0`
  - Why it cannot be hit: NTP quorum check over server responses; needs live NTP responses.
- **`th8NtpQuery`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `lastNtp > 0 && lastLocal > 0`
  - Why it cannot be hit: Cached NTP time state; populated only by a prior live NTP query.
- **`th8NtpQueryOne`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `ub_ctx_add_ta_file(ubctx, "/etc/unbound/root.key") != 0 && ub_ctx_add_ta_file(ubctx, "/usr/share/dns/root.key") != 0 && ub_ctx_add_ta_file(ubctx, "/var/lib/unbound/root.key") != 0 && ub_ctx_add_ta_file( ubctx, "/opt/homebrew/etc/unbound/root.key") != 0`
  - Why it cannot be hit: DNSSEC trust-anchor setup on the NTP resolve path; live network / libunbound.
- **`th8NtpQueryOne`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `ubrc == 0 && ubresult`
  - Why it cannot be hit: Libunbound resolve result on the NTP path; live network.
- **`th8NtpQueryOne`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `getaddrinfo(zServer, "123", &hints, &res) != 0 || !res`
  - Why it cannot be hit: NTP server DNS resolution; needs live network or a getaddrinfo mock.
- **`th8NtpSortTimes`** (`src/plugins/harpy/th8_time.c`)
  - Decision: `j >= 0 && a[j] > key`
  - Why it cannot be hit: Insertion sort over collected NTP offsets; only reached after live NTP responses.
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
  - Decision: `!th8UnboundSetupManagedAnchor( ubctx, pOps, bHaveSrc ? zSrc : NULL) && bHaveSrc`
  - Why it cannot be hit: DNSSEC managed trust-anchor setup; libunbound + live DNS.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `ub_resolve(ubctx, zHostBuf, eType, 1 /* IN */, &ubr) != 0 || !ubr`
  - Why it cannot be hit: Libunbound DNS resolve; live DNS.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `ubr->data && ubr->data[n]`
  - Why it cannot be hit: Libunbound result-record walk; live DNS response.
- **`th8UnboundResolve`** (`src/th8_unbound.c`)
  - Decision: `pImpl->pub.nRecord > 0 && ALWAYS`
  - Why it cannot be hit: Libunbound record-count check (ALWAYS-wrapped tail); live DNS response.

### external-oom-unreachable -- External-library OOM guards (3)

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

### synchronization-invasive -- Synchronization-invasive arms (2)

Driving the arm requires faking a `pthread_cond_wait` / `pthread_cond_timedwait` return code, or forcing a precise signal-arrives-exactly-at-timeout race, in the event-wait path. Either would perturb real synchronization; the arm is not safely or deterministically reachable from a test.

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
