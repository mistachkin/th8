# TH8 Language Extensions

*Version 1.0 -- date 2026-06-22 (currency-reviewed; cross-checked
against `src/th8.h` and the implementation; 259 normative
requirements verify clean against `tools/mkreq.tcl --verify`).*

This document specifies extensions to the Tcl Language Standard v1
that are implemented by the TH8 interpreter.  It is the companion
to `tcl_language_standard_v1.md`, which defines the portable Tcl
core.  Content here falls into two categories:

1. **TH8-specific commands, sub-commands, options, and operators**
   that are not part of canonical Tcl 8.6 and are not defined in the
   Tcl Language Standard.  Examples: `clock https`, `info breakpoints`,
   the `bigint` math feature, the `harpy` script-signing command.

2. **TH8-specific subsystems** that no canonical Tcl implementation
   exposes: the script security policy, cryptographic API, the
   C-level platform embedding API, fuzz/fault/debugging
   infrastructure, and pluggable backends.  These are the contents
   of Parts II through V below.

R-marker IDs in this document are MD5-derived from requirement text
in the same way as the standard.  Tests reference R-marker IDs
directly, so they remain valid regardless of which document a
requirement lives in.

Cross-references to the standard use the form
*Tcl Language Standard §N*; cross-references inside this
document use bare §N.M.

---

## Part I --- TH8-Specific Language Extensions

*This part collects TH8-specific features that extend the
script-visible language surface beyond canonical Tcl 8.6.  It is
populated by extracting the relevant sub-content from the standard's
Parts II and III.  Sections are grouped by feature area.*

### 1  Expression Extensions

The following expression-language features are TH8-specific extensions to the Tcl Language Standard.

#### 9.2.1a  Machine Epsilon

R-45561-34695
:   The epsilon() math function SHALL return the IEEE 754 double-precision machine epsilon (2.220446049250313e-16), the smallest value e such that 1.0 + e is not equal to 1.0.

#### 9.3.1  Arbitrary Precision Integers

R-28097-02932
:   When arbitrary precision integers are enabled for the interpreter, integer overflow in expressions promotes the result to an arbitrary precision integer (bigint) instead of producing an error.
R-03101-05776
:   Bigint values participate in all integer arithmetic, bitwise, comparison, and shift operations.
R-48717-03962
:   The `abs`, `max`, `min`, `int`, `entier`, and `typeof` math functions accept bigint values.
R-12852-08369
:   Bigint support is a per-interpreter security gate controlled by the C embedder via `Th8_EnableBigint`.

#### 9.3.2  Strict Compliance Contract and Opt-In Expression Features

R-11018-20346
:   In its default configuration, an interpreter SHALL accept exactly the operators and operand forms defined by the official Tcl 8.6 `expr(n)` reference; no extension to the expression grammar is reachable without an explicit embedder action.
R-62282-22280
:   The C embedder MAY enable expression-grammar extensions on a per-interpreter basis by passing a bitwise-OR of `TH8_EXPR_*` flags to `Th8_SetExprFeatures`; the previous flag set is returned so callers can save and restore around scoped enables.  The current flag set is read via `Th8_GetExprFeatures`.
R-05343-51878
:   `TH8_EXPR_TOP_COMMA` enables a TOP-LEVEL-ONLY `,` separator: `expr {a, b, c}` evaluates each sub-expression left-to-right and yields the value of the last.  The comma is NOT a binary operator: a `,` inside parentheses, ternary operands, or any other scope without parens remains a syntax error, and function-call argument commas (`pow(x, y)`) are unaffected.
R-17445-47782
:   `TH8_EXPR_VAR_ASSIGN` enables a `:=` variable-assignment operator at the lowest binary precedence (right-associative).  The left operand SHALL be an ordinary `expr(n)` operand whose substituted-string value names the variable; bare-word LHS remains rejected by the bareword-rejection security envelope.  The result of the expression is the assigned value (so `"a" := "b" := 1` sets both `a` and `b` to 1 and yields 1).
R-56775-06795
:   The `Th8_SetExprFeatures` API SHALL silently mask off bits not corresponding to any defined `TH8_EXPR_*` constant in the calling library, so embedder code built against a future TH8 release continues to work correctly when linked with an older library.
R-55928-25418
:   The expression-feature flag set is NOT exposed as a script-level command; only the C embedder may modify it.  Untrusted scripts cannot enable extensions on themselves.

### 2  Procedure and C-API Extensions

The following C-level entry points are TH8-specific.  They are documented here rather than in the standard because they are part of the embedding API.

#### 18.3a  Th8_EvalFile (C API)

**Synopsis (C):** `int Th8_EvalFile(Th8_Interp *interp, const char *zName, size_t nName)`

R-37343-58361
:   The `Th8_EvalFile` function retrieves the file contents via the platform's data-retrieval callback and evaluates them as a script.
R-15788-25921
:   The `Th8_EvalFile` function pushes the file name onto the `[info script]` stack before evaluation and pops it after.
R-26048-05083
:   The `Th8_EvalFile` function returns `TH8_OK` on success or `TH8_ERROR` if the file cannot be retrieved or the script produces an error.
R-58065-24188
:   `Th8_EvalFile` SHALL save the `::th8_security` array before retrieving the file and restore it after the evaluation completes, giving the security state stack-like behavior tied to the source depth.

#### 18.3b  Th8_EvalFileAsData (C API)

**Synopsis (C):** `int Th8_EvalFileAsData(Th8_Interp *interp, const char *zName, size_t nName, const unsigned char **pzData, size_t *pnData, void *pCtx)`

R-29455-61245
:   `Th8_EvalFileAsData` SHALL create a child interpreter that inherits the parent's platform callbacks and signed-only policy, evaluate the named file in the child, and base64-decode the result.
R-51860-19180
:   If the parent interpreter has the signed-only policy enabled, `Th8_EvalFileAsData` SHALL enable it in the child interpreter as well.
R-00108-51576
:   The child interpreter created by `Th8_EvalFileAsData` SHALL always be deleted, whether the evaluation succeeds or fails.
R-18553-41093
:   `Th8_EvalFileAsData` SHALL return the base64-decoded script result via pzData and pnData, with the caller responsible for freeing via `Th8_Free`.

#### 18.3c  Th8_EvalFileAndRsaKeyLoad (C API)

**Synopsis (C):** `int Th8_EvalFileAndRsaKeyLoad(Th8_Interp *interp, const char *zName, size_t nName, void *pCtx, int bPreload)`

R-42421-02532
:   `Th8_EvalFileAndRsaKeyLoad` SHALL combine `Th8_EvalFileAsData` and `Th8_RsaKeyLoad` to load an RSA public key from a signed script file.
R-36002-25646
:   When bPreload is non-zero, `Th8_EvalFileAndRsaKeyLoad` SHALL preload the key into the policy cache via `Th8_PolicyPreloadKey`, requiring pCtx to be a valid policy context.
R-52671-00029
:   If any step of `Th8_EvalFileAndRsaKeyLoad` fails, the function SHALL return `TH8_ERROR` with no key loaded.

#### 18.4a  Authenticode Signature Verification (Win32)

On Win32, the platform's `xLoad` callback verifies the
Authenticode digital signature of every DLL before mapping it
into the process.  This prevents loading of unsigned, tampered,
or untrusted binaries.

R-37051-31498
:   On Win32, the xLoad platform callback SHALL verify the Authenticode digital signature of the DLL file before calling LoadLibraryA, rejecting unsigned or invalidly signed binaries.
R-51287-12935
:   The Authenticode verification SHALL use `WTD_CACHE_ONLY_URL_RETRIEVAL` to permit offline operation without network access for CRL or OCSP fetches.
R-27510-48346
:   The Authenticode verification SHALL use `WTD_REVOKE_WHOLECHAIN` to validate the entire certificate chain, using only locally cached revocation data.
R-06804-06311
:   The Authenticode verification SHALL be skipped when `NDEBUG` is not defined, to permit loading of unsigned development builds.

#### 24.2  Interpreter Suspension

**Synopsis (C):** `int Th8_Freeze(Th8_Interp *interp)` | `int Th8_Thaw(Th8_Interp *interp)`

R-02127-03083
:   The `Th8_Freeze` function suspends the interpreter at the next command boundary by setting an internal flag, returning `TH8_OK`.
R-13096-49145
:   A frozen interpreter returns `TH8_SUSPEND` from `Th8_Ready`, causing the current evaluation to unwind without discarding the NRE callback chain.
R-18963-52029
:   The `[catch]` command does not intercept `TH8_SUSPEND`; the suspension propagates through all catch frames.
R-21420-54044
:   The `Th8_Thaw` function clears the suspension flag so that `Th8_Ready` returns `TH8_OK` again; the caller may then re-enter the NRE trampoline via `Th8_EvalTrampoline` to resume preserved callbacks.
R-15699-25001
:   The NRE callback chain, frame stack, variables, and interpreter result are all preserved across a freeze/thaw cycle.

### 3  Introspection Extensions

The following `info` sub-commands are TH8-specific introspection extensions; they are not part of canonical Tcl 8.6.

#### 20.0j  info breakpoints

**Synopsis:** `info breakpoints`

R-16236-39798
:   The `info breakpoints` command SHALL return a flat list of breakpoint descriptions.  Each breakpoint contributes three consecutive elements: the integer breakpoint ID, the script name, and the line number.  If no breakpoints are set, an empty list is returned.

#### 20.0k  info expansions

**Synopsis:** `info expansions` ?*pattern*?

R-01803-53009
:   The `info expansions` command SHALL return a list of expansion operator tag names registered in the current namespace, optionally filtered by a glob pattern.

#### 20.0l  Custom Argument-Expansion Operators

The Tcl Language Standard §5.7 specifies argument expansion via the
`{*}` prefix.  TH8 generalises that mechanism: any word beginning
with `{` *tagname* `}` (where *tagname* is a registered expansion
operator name) invokes the named operator on the rest of the word
to produce the argument list.  The `*` operator is built-in and
behaves as the standard argument-expansion mechanism specifies; all
other tag names are user-registered.

R-50966-37458
:    A word whose first character is `{` and whose first balanced
:    brace group contains a non-`*` identifier (the *tag*) followed
:    by `}` SHALL be treated as a tagged expansion: the rest of
:    the word SHALL be passed as input to the expansion operator
:    registered for that tag, and the operator's return value
:    SHALL be parsed as a list whose elements become arguments to
:    the enclosing command in place of the tagged-expansion word.

R-53729-50924
:    A tagged-expansion word whose tag is not registered as an
:    expansion operator SHALL produce a parse-time script error
:    with a message of the form `unknown expansion operator
:    "TAG"`; the message SHALL include the offending tag name.

### 4  Time and System Extensions

The following time-and-system commands are TH8-specific.

#### 25.5  clock ntp

**Synopsis:** `clock ntp` ?`-server` *host*? ?`-timeout` *ms*? ?`-attempts` *n*?

The `clock ntp` subcommand queries NTP v4 servers for authenticated
wall-clock time.  Multiple `-server` options may be specified for
multi-server consensus.  Because NTP runs over UDP, each server is
queried with a small number of retry attempts (`-attempts`, default 3;
1 disables retries) so that a single lost packet does not fail the
query.

R-14640-47759
:   The `clock ntp` command SHALL query one or more NTP v4 servers and return the consensus wall-clock time as Unix epoch seconds.
R-32287-57587
:   When multiple NTP servers are queried, the `clock ntp` command SHALL reject responses that disagree by more than the configured threshold.
R-54400-11734
:   The `clock ntp` command SHALL detect if the local clock has moved backward since the last successful NTP query and return an error.
R-57714-59415
:   The default NTP server SHALL be `urn.to`.
R-39982-49558
:   The `clock ntp` subcommand SHALL retry a transient per-server failure -- a send error, a receive timeout, or a short response, all expected because NTP runs over UDP -- up to a bounded number of attempts before treating that server as unresponsive.  A response that arrives but fails validation SHALL NOT be retried.  The attempt count SHALL default to a fixed value and be configurable via the `-attempts` option, where 1 disables retries.

#### 25.6  clock https

**Synopsis:** `clock https` ?*url*?

The `clock https` subcommand fetches authenticated time from an
HTTPS endpoint.  The response is verified with a random nonce
(anti-replay) and an RSA-SHA512 signature (anti-forgery) using
the embedded `keyTime` public key.

R-08803-10244
:   The `clock https` command SHALL fetch authenticated time from an HTTPS endpoint and verify the RSA-SHA512 signature against the embedded keyTime public key.
R-33190-18538
:   The `clock https` command SHALL generate a random nonce for each request and verify that the response echoes the same nonce.
R-07082-35746
:   The `clock https` command SHALL reject timestamps outside the plausible range of 2020 to 2100.

#### 25.7  flags

**Synopsis:** `flags have` ?*options*? *flagString* *haveFlags*
**Synopsis:** `flags change` ?*options*? *flagString* *changeSpec*
**Synopsis:** `flags show` ?*options*? *flagString*

The `flags` command manipulates Harpy-compatible attribute flag
strings.  Simple flag strings are sequences of alphanumeric
characters.  Complex flag strings use brace-delimited groups
with hex keys: `{HEXKEY:flags}`.

R-47520-46390
:   The `flags` command SHALL parse simple flag strings as sequences of alphanumeric characters, and complex flag strings as brace-delimited hex-key:flags groups.
R-17475-11102
:   The `flags have` subcommand SHALL return 1 if the requested flags are present in the flag string for the specified key, 0 otherwise.
R-19403-45740
:   The `flags change` subcommand SHALL apply +add, -remove, and =set operators to the flag string and return the modified result.
R-28435-22737
:   The `flags show` subcommand parses the flags string and returns a dictionary mapping each key ID to its sorted flags, with the global key (0) listed first.
R-41140-31025
:   The wildcard characters `*`, `#`, `!`, `$`, and `@` SHALL expand to all alphanumeric, all digits, all letters, uppercase letters, and lowercase letters respectively.

---

## Part II --- Security and Cryptography


### 29  Script Security Policy

#### 29.1  The ::th8_security Array

R-32092-29314
:   The `::th8_security` array SHALL contain seven elements: `algorithmName`, `dataName`, `flags`, `notAfter`, `notBefore`, `policy`, and `publicKeyToken`, all initialized to "none".
R-56520-28721
:   The `dataName` element of `::th8_security` SHALL contain the origin name of the script whose signature was most recently verified in the current evaluation context.
R-04287-59267
:   The `::th8_security` array SHALL be declared as a system variable (read-only from scripts).
R-50169-65270
:   Scripts SHALL NOT be able to modify `::th8_security` via `set`, `unset`, `append`, `incr`, `lappend`, or `array set`.
R-39227-63501
:   `Th8_ResetSecurityArray` SHALL set all seven elements of the `::th8_security` array to `"none"`.

#### 29.2  Signed-Only Mode

R-19249-39118
:   When signed-only mode is enabled, every script evaluation with a non-NULL origin SHALL have its data verified via a preGetData callback before evaluation proceeds.
R-01415-20789
:   When signed-only mode is enabled, scripts without an origin name (NULL) at eval depth 1 SHALL be rejected with an error.
R-23239-63645
:   Internal sub-evaluations (eval depth > 1) SHALL be permitted when the outermost script has been successfully verified, as indicated by a random-token verification flag.

#### 29.3  Signature Verification

R-26716-52256
:   The preGetData callback SHALL receive the raw file data before EOL translation and SHALL verify the RSA signature against these raw bytes.
R-21948-04583
:   Signature verification SHALL use PKCS#1 v1.5 padding with SHA-512.
R-15123-42251
:   The public key for verification SHALL be looked up in a hash-table cache keyed by the 16-character hex public key token.
R-09186-19763
:   A script whose content has been modified after signing (tampered) SHALL be rejected by the preGetData callback with a verification failure.
R-56307-62946
:   When RSA verification fails, the preGetData callback SHALL emit the computed data hash and the extracted signature hash via `Th8_EmitTrace` for diagnostic purposes.

#### 29.4  Policy Management Functions

R-19703-54915
:   `Th8_EnableSignedPolicy` SHALL install the signed-only policy, preload all embedded keys, and enable the gate in a single call. When called with bEnable=0, it SHALL remove the policy and disable the gate.
R-63100-39804
:   `Th8_EnableSignedOnly` SHALL enable or disable the signed-only script evaluation gate using a random-token mechanism.
R-10988-57474
:   `Th8_IsSignedOnlyEnabled` SHALL return non-zero when the signed-only gate is active.
R-54030-01886
:   `Th8_PolicyPreloadKey` SHALL insert an RSA key into the signed-only policy cache, taking ownership of the key.

#### 29.5  Policy Callback and Trusted Evaluation

R-22846-55069
:   The Th8_PolicyProc callback type SHALL receive a phase bitmask combining exactly one of TH8_PHASE_PRE or TH8_PHASE_POST with exactly one of TH8_PHASE_READ or TH8_PHASE_EVAL, allowing a single function to handle both data verification and eval authorization.
R-29651-54370
:   Before checking notBefore/notAfter script annotations, the policy SHALL attempt NTP time synchronization. When the TH8_FORCE_HTTPS_TIME flag is set, HTTPS-based time retrieval SHALL be used instead of NTP.
R-23237-25352
:   In the POST|EVAL phase, when the evaluation depth is less than or equal to 1, the policy SHALL clear the verification flag that was set by signature verification during the PRE|READ phase.
R-57476-09524
:   When the TH8_EVAL_TRUSTED flag is set on an evaluation call, the policy SHALL allow scripts with NULL origin names (interactive or embedder-vouched input) to proceed without requiring a signature.
R-46373-63539
:   `Th8_EvalTrusted` SHALL evaluate a script with the TH8_EVAL_TRUSTED flag set, causing the policy callback to permit execution of scripts with no origin name.

#### 29.6  Sandbox Security

R-19191-01287
:   A sandbox child interpreter SHALL have xPanic set to NULL so that resource limit exhaustion returns errors instead of aborting the host process.
R-09721-12704
:   A sandbox child interpreter SHALL have binary loading disabled (Th8_EnableLoad not called).
R-50184-02460
:   A sandbox child interpreter SHALL have bigint disabled (Th8_EnableBigint not called).
R-16375-65393
:   A sandbox child interpreter SHALL have integer overflow checking enabled (bOverflowCheck=1, the secure default).
R-12425-26970
:   A sandbox child interpreter SHALL enforce a memory allocation limit via Th8_SetAllocLimit.
R-42761-35236
:   A sandbox child interpreter SHALL enforce a step execution limit via Th8_SetStepLimit.
R-51616-58234
:   A sandbox child interpreter SHALL enforce a result size limit via Th8_SetResultLimit.
R-20999-34016
:   A sandbox child interpreter SHALL NOT inherit commands, variables, namespaces, or procedures from the parent interpreter.
R-12924-62009
:   A sandbox child interpreter SHALL NOT have access to the parent interpreter's policy callbacks.
R-56918-62587
:   File system operations in a sandbox child interpreter SHALL be restricted to paths under the base directory.
R-02231-09675
:   Absolute paths SHALL be rejected by the sandbox child interpreter's xGetData and xDataExists callbacks.
R-63048-10768
:   Path traversal attempts using ".." SHALL be rejected by the sandbox child interpreter.
R-34935-39021
:   The [info nameofexecutable] command in a sandbox SHALL return a base-relative path, not an absolute path.
R-55895-04504
:   The [pwd] command in a sandbox SHALL return "." (the base directory).
R-47394-37015
:   Resource exhaustion attacks (regexp bombs, string amplification, list bombs, format abuse) SHALL be bounded by the sandbox step limit and memory allocation limit.
R-27789-36268
:   Integer overflow in sandbox [expr] operations SHALL produce an error when overflow checking is enabled.
R-26649-55462
:   Error state (errorInfo, errorCode) in a sandbox child interpreter SHALL NOT contain information from the parent interpreter.

#### 29.7  Script Annotations

R-16487-40383
:   Script annotations SHALL be delimited by `<<` and `>>` and may appear anywhere in the script text.
R-12455-60956
:   The `<<notBefore:TIMESTAMP>>` annotation SHALL specify the earliest time at which the script is valid, in YYYY_MM_DDThh_mm_ssZ format.
R-55366-36861
:   The `<<notAfter:TIMESTAMP>>` annotation SHALL specify the latest time at which the script is valid, in YYYY_MM_DDThh_mm_ssZ format.
R-15750-16500
:   Annotation timestamps SHALL be validated for correct format, valid month (1-12), valid day (1-maxDay including leap year rules), valid hour (0-23), minute (0-59), second (0-59), and year >= 1970.
R-20621-61185
:   When both notBefore and notAfter are present, notBefore SHALL be less than or equal to notAfter; otherwise the annotation is rejected as having a bad time range.
R-63019-38593
:   When signed-only mode is enabled, expired scripts (current time > notAfter) SHALL be rejected with an error.
R-59386-08297
:   When signed-only mode is enabled, not-yet-valid scripts (current time < notBefore) SHALL be rejected with an error.
R-48430-59337
:   When signed-only mode is enabled, malformed annotations SHALL cause the script to be rejected.
R-38064-59928
:   Annotation values SHALL be stored in the ::th8_security array elements notBefore, notAfter, and flags.
R-32639-01913
:   Scripts without annotations SHALL execute normally; the absence of annotations is not an error.
R-56140-29717
:   The <<flags:VALUE>> annotation SHALL be validated via Th8_AttrFlagsParse; spaces are not allowed in the value.
R-44468-02007
:   Script annotations in the format <<notBefore:YYYY_MM_DDThh_mm_ssZ>> and <<notAfter:YYYY_MM_DDThh_mm_ssZ>> SHALL be validated with strict ISO-8601 date-time rules including leap year bounds. The underscore delimiters are an intentional deviation for Eagle compatibility.
R-62434-18753
:   Script annotations SHALL be extracted from all scripts when the policy callbacks are installed, regardless of whether the signed-only policy is enabled. The parsed values SHALL populate the ::th8_security array.
R-43104-17325
:   When the signed-only policy is enabled, any script with a malformed annotation SHALL be rejected. Scripts whose current time falls outside the notBefore/notAfter range SHALL also be rejected.

#### 29.7a  harpy

**Synopsis:** `harpy sign` *script* *publicKeyToken* | `harpy verify` *script* *signature* *publicKeyToken*

The `harpy` command provides the script-level interface to TH8's
Harpy `.b64sig` script-signing format.  It is the script-visible
counterpart to the C-level signing infrastructure (cf. *TH8 Public
C API Specification* §32.3).

The `sign` sub-command produces a Harpy `.b64sig` containing a
public-key-token comment header and a base64-encoded RSA-SHA512
signature body.  The `verify` sub-command checks that an existing
signature is valid for a given script and key.

The full normative requirements for `harpy sign` and `harpy verify`
are stated in the TH8 Public C API Specification §32.3
(`R-31508-51826`, `R-01323-22268`, `R-49596-49581`,
`R-40734-02380`, `R-40078-47877`, `R-46108-37349`,
`R-45789-59282`).  Those R-markers govern the script-level command
behavior and apply equally whether the command is invoked from a
script or from C via the embedding API.

The command requires `TH8_ENABLE_CRYPTOGRAPHY` at build time and
the signed-only policy to be installed at runtime.

#### 29.8  The `secure` Command

**Synopsis:**
`secure create` *varName* ?*value*?
`secure exists` *varName*
`secure delete` *varName*
`secure save` *varName*
`secure load` *varName*

The `secure` command manages encrypted-at-rest variables.  Each
secure variable has a fresh AES-256-GCM key allocated from a
locked (mlock'd / VirtualLock'd) key page.  Plaintext values are
re-encrypted on every write and decrypted only into a per-
interpreter protected (mlock'd, guard-paged) result region;
plaintext never resides in pageable heap memory.  See *TH8 Public
C API Specification* §44 (Secure Variables) for the data-flow
contract; this section specifies the script-level command surface.

The save / load sub-commands persist a secure variable's
ciphertext to the platform's key-value backend under master-key
re-encryption.  See `R-15123-42251` and the §29.4 policy gate
requirements for the verification contract.

##### 29.8.1  `secure create`

R-47030-53896
:    The `secure create` *varName* ?*value*? sub-command SHALL
:    create a new encrypted-at-rest variable named *varName* with
:    the given *value* (or the empty string if *value* is omitted),
:    allocating a fresh AES-256-GCM key from the locked key page
:    and registering the variable in the secure-variable hash.

R-35397-13579
:    The `secure create` sub-command SHALL raise a script error
:    if a variable named *varName* already exists, regardless of
:    whether the existing variable is plain or already secure.
:    The error message SHALL be of the form `cannot modify
:    existing variable "VARNAME"`.

R-35314-37131
:    The `secure create` sub-command SHALL raise a script error
:    if *varName* contains array-element syntax (a `(` character).
:    The error message SHALL be of the form
:    `secure: array elements not supported`.

##### 29.8.2  `secure exists`

R-14798-31664
:    The `secure exists` *varName* sub-command SHALL return 1 if
:    *varName* names a variable previously created by `secure
:    create` and not yet deleted, and 0 in every other case
:    (the variable does not exist, or the variable exists but
:    was created by ordinary commands such as `set`).

##### 29.8.3  Read and Update

R-18042-11365
:    Reading a secure variable through `$varName`, `set varName`,
:    or any other script-level read SHALL return the decrypted
:    plaintext.  Successive reads of the same secure variable
:    without an intervening write SHALL return identical results.

R-41789-50474
:    Writing to a secure variable through `set`, `append`, or
:    `incr` SHALL re-encrypt the new value with a freshly-derived
:    nonce under the variable's per-variable key, replacing the
:    previously-stored ciphertext.

R-30156-17445
:    A secure variable SHALL be usable in expression contexts
:    (`expr {$secureVar * 2}`), command substitution
:    (`[string length $secureVar]`), and any other context that
:    normally accepts a variable read; the variable's plaintext
:    value SHALL be the substitution result in each case.

##### 29.8.4  `secure delete`

R-51801-42694
:    The `secure delete` *varName* sub-command SHALL securely
:    zero the variable's key slot and ciphertext, free the
:    per-variable metadata, remove the variable from the secure-
:    variable hash, and unset the underlying script variable.
:    After this sub-command returns, `info exists varName` SHALL
:    report 0.

R-39649-05760
:    The `secure delete` sub-command SHALL raise a script error
:    if *varName* does not name a secure variable (whether the
:    variable does not exist at all or exists as a plain
:    variable).  The error message SHALL begin with
:    `variable is not secure`.

R-54390-04467
:    Deleting a secure variable SHALL release its key slot for
:    subsequent allocation by a later `secure create`; the
:    maximum simultaneously-live secure variable count
:    (TH8_SECURE_MAX_SLOTS - 2 reserved slots) SHALL NOT be
:    reduced by previous create-and-delete cycles.


### 30  Cryptographic API

> **Note:** Sections 31--41 specify C embedding APIs.  See the
> companion *TH8 Public C API Specification* for the authoritative
> embedder reference.

#### 30.1  RSA Key Loading

R-36411-65332
:   `Th8_RsaKeyLoad` SHALL parse CAPI PUBLICKEYBLOB, PRIVATEKEYBLOB, and .NET Strong Name wrapped public key formats.
R-46342-57447
:   `Th8_RsaKeyToken` SHALL compute the public key token as the last 8 bytes of the SHA-1 hash of the full public key blob (including .NET wrapper if present), byte-reversed.

#### 30.2  RSA Verification

R-62055-50597
:   `Th8_RsaVerify` SHALL verify RSA signatures using OpenSSL EVP API with SHA-512 and PKCS#1 v1.5 padding.

#### 30.3  Signature File Parsing

R-31833-59574
:   `Th8_HarpySigLoad` SHALL parse Harpy `.b64sig` raw signature files, extracting the base64-encoded signature and the public key token from the header.

#### 30.4  Embedded Keys

R-48952-62365
:   `Th8_GetEmbeddedKey0` and `Th8_GetEmbeddedKeyRoot` SHALL return pointers to compiled-in public key data.
R-50724-41788
:   `Th8_GetPublicKeyZero` SHALL return a cached parsed `Th8_RsaKey` for the embedded key0.
R-30888-26660
:   `Th8_GetPublicKeyZeroToken` SHALL return the 16-character hex public key token for key0.
R-52162-65406
:   `Th8_GetPublicKeyRoot` SHALL return a cached parsed `Th8_RsaKey` for the embedded root key.
R-21583-54643
:   `Th8_GetPublicKeyRootToken` SHALL return the 16-character hex public key token for the root key.


---

## Part III --- Platform Embedding API


### 31  Platform Initialization

> **Note:** Sections 29 and 31--41 specify the C embedding API and are
> also contained in the companion *TH8 Public C API Specification*
> (`th8_public_c_api_specification.md`).  The C API spec is the
> authoritative reference for embedder-facing details; this document
> retains the requirements for test traceability.

#### 31.1  Th8_Initialize

R-09811-18829
:   The `Th8_Initialize` function calls the platform's `xInitialize` callback, if provided, before any other platform callbacks are used.
R-36666-45636
:   After platform initialization, `Th8_Initialize` sets the current working directory to the base directory by calling `xSetCwd(".", 1, ...)`, establishing "." as the canonical base path for all subsequent file system operations.
R-35192-33230
:   If the platform does not provide an `xSetCwd` callback, the current-directory reset is silently skipped.
R-63795-60847
:   If the platform provides an `xSetCwd` callback and that callback fails (returns non-zero), `Th8_Initialize` returns `TH8_ERROR` and the library is not considered initialized.

#### 31.1a  Th8_CreateInterp

R-17795-52555
:   `Th8_CreateInterp` stores a pointer to the `Th8_Platform` struct, not a copy. The platform struct SHALL remain valid and at a stable address for the entire lifetime of the interpreter.
R-41905-53717
:   Stack-allocated `Th8_Platform` structs are safe only when the interpreter is created and destroyed within the same stack frame. For interpreters that outlive the creating function, the platform SHALL be static, global, or heap-allocated.

#### 31.2  Th8_Finalize

R-38069-19424
:   The `Th8_Finalize` function calls the platform's `xFinalize` callback, if provided, after all other cleanup is complete, allowing the platform to release internal resources such as mutexes and heaps.

#### 31.3  Base Path

The *base path* is the directory from which TH8 operates.  It is defined
as the directory immediately containing the TH8 shared library, or its
parent directory if the immediate directory is named "bin" or "lib".

R-02359-36162
:   The base path is determined automatically from the location of the TH8 shared library at platform initialization time.
R-49340-37967
:   If the shared library directory is named "bin" or "lib", the parent directory is used as the base path.
R-61298-50331
:   The string "." in all file system operations resolves to the base path.

#### 31.4  File System Security Model

R-13194-38737
:   The `xGetCwd` platform callback returns "." if the actual current directory is the base directory, or a relative path of the form "./subdir" if it is underneath the base directory.
R-64118-16729
:   The `xGetCwd` callback returns NULL (signaling an error) if the actual current directory is outside the base directory, producing the message "permission denied: cannot access foreign directory".
R-00624-49023
:   The `xSetCwd` platform callback accepts "." (the base directory) or any path that resolves to a location at or underneath the base directory, and rejects all other paths.
R-51815-04030
:   The `xNormalizePath` platform callback returns paths under the base directory in the form "./relative/path" and returns NULL for paths that resolve outside the base directory.
R-19594-01472
:   If a platform callback (`xGetCwd`, `xSetCwd`, `xNormalizePath`) is NULL, the corresponding script command returns a "permission denied: access to file system unavailable" error.
R-57925-48172
:   The `xGetData` and `xDataExists` platform callbacks SHALL reject paths that are absolute or that resolve outside the base directory. `xGetData` returns `TH8_ERROR`; `xDataExists` returns 0 (not found).
R-54908-52666
:   The `xSameFile` platform callback SHALL reject paths that are absolute or that resolve outside the base directory, returning 0 (not same).
R-43602-25309
:   The `xLoad` platform callback SHALL reject library paths that are absolute or that resolve outside the base directory, returning `TH8_ERROR`.
R-52412-33477
:   The `xGetExePath` platform callback SHALL return the executable path in base-relative form (e.g., "./bin/th8sh"). If the executable is outside the base directory, it SHALL return NULL.
R-30707-46329
:   The `xGetRootPath` platform callback SHALL return "." if the given path resolves under the base directory, or the empty string if the path resolves outside the base directory.

---


### 32  Platform Wrapper Functions

#### 32.1  I/O Channel Redirection

R-50803-29436
:   `Th8_RedirectInput` SHALL set the input channel for the interpreter via the platform's `xSetInput` callback, returning `TH8_ERROR` if the callback is NULL.
R-07718-17484
:   `Th8_GetInput` SHALL retrieve the current input channel via the platform's `xGetInput` callback, storing NULL (default channel) if the callback is not available.
R-41598-13571
:   `Th8_RedirectOutput` SHALL set the output channel for the interpreter via the platform's `xSetOutput` callback, returning `TH8_ERROR` if the callback is NULL.
R-60126-19580
:   `Th8_GetOutput` SHALL retrieve the current output channel via the platform's `xGetOutput` callback, storing NULL (default channel) if the callback is not available.
R-49310-16859
:   `Th8_RedirectErrorOutput` SHALL set the error output channel for the interpreter via the platform's `xSetErrorOutput` callback, returning `TH8_ERROR` if the callback is NULL.
R-55129-27434
:   `Th8_GetErrorOutput` SHALL retrieve the current error output channel via the platform's `xGetErrorOutput` callback, storing NULL (default channel) if the callback is not available.
R-35230-48647
:   `Th8_Input` SHALL query the current input channel via `xGetInput` before invoking the `xInput` callback, passing the channel pointer to `xInput`.
R-44914-34174
:   `Th8_Output` SHALL query the current output channel via `xGetOutput` before invoking the `xOutput` callback, passing the channel pointer to `xOutput`.
R-64687-40860
:   `Th8_OutputError` SHALL query the current error output channel via `xGetErrorOutput` before invoking the `xOutputError` callback, passing the channel pointer to `xOutputError`.
R-32259-63920
:   The `puts` command SHALL reject any channel argument other than `stdout` with a "channel not available" error.
R-60525-53038
:   `Th8_RedirectInput` with a NULL channel argument SHALL reset the input to the default channel.
R-54749-53301
:   `Th8_RedirectOutput` with a NULL channel argument SHALL reset the output to the default channel.
R-54889-38456
:   `Th8_GetInput` SHALL return `TH8_OK` and set the output pointer to NULL when no channel redirection is active.
R-53110-05508
:   `Th8_GetOutput` SHALL return `TH8_OK` and set the output pointer to NULL when no channel redirection is active.
R-06770-22179
:   `Th8_GetErrorOutput` SHALL return `TH8_OK` and set the output pointer to NULL when no channel redirection is active.

#### 32.2  Path Resolution and Error Reporting

R-03277-47826
:   `Th8_GetRealPath` SHALL resolve a file path to its canonical absolute form via the platform's `xGetRealPath` callback, writing the result into the caller-provided buffer.
R-45696-38694
:   `Th8_GetLastError` SHALL return the most recent OS error code via the platform's `xGetLastError` callback, returning -1 if the interpreter is NULL or the callback is unavailable.

#### 32.3  Script Signing and Verification (`harpy` command)

R-31508-51826
:   `harpy sign` SHALL produce a Harpy `.b64sig` format signature containing a comment header with the public key token and a base64-encoded RSA-SHA512 signature body.
R-01323-22268
:   `harpy sign` SHALL require the key identified by the public key token to contain a private key, returning an error if only a public key is available.
R-49596-49581
:   `harpy sign` SHALL verify the produced signature against the public key before returning it (Bellcore self-verification).
R-40734-02380
:   `harpy verify` SHALL return the string `"ok"` if the RSA-SHA512 signature over the script text is valid for the key identified by the public key token.
R-40078-47877
:   `harpy verify` SHALL raise an error if the signature does not match the script text (tamper detection).
R-46108-37349
:   `harpy verify` SHALL raise an error if the token embedded in the signature does not match the token argument.
R-45789-59282
:   The `harpy` command SHALL raise an error if the signed-only policy is not installed.

#### 32.4  RSA Signing Infrastructure

R-64430-14707
:   `Th8_RsaSign` SHALL reject RSA keys shorter than 2048 bits.

#### 32.5  Protected Memory Regions

R-23343-53449
:   `Th8_ProtectedAlloc` SHALL allocate a data page flanked by guard pages and locked into physical memory via `mlock` or `VirtualLock`.
R-25984-53927
:   `Th8_ProtectedFree` SHALL securely zero the data page before unlocking and releasing the memory.

#### 32.6  Channel Control Operations

R-55340-20582
:   TH8_CHANCTL_OPEN (opcode 6) SHALL open a file at path pBuf (nArg1 bytes long), with nArg2 selecting the mode (0 for read-only, 1 for write-create-truncate), and return the opaque handle in *pnResult.

#### 32.7  Platform Struct Versioning

R-57494-14580
:   The Th8_Platform struct nVersion field SHALL be 2, reflecting the rationalized callback ordering into 16 logical groups: lifecycle, memory, byte operations, string/utility, threading, I/O core, I/O redirection, channel/temporary I/O, filesystem, data/loading, time, process/host, error/diagnostics, math/entropy, and host context.

#### 32.8  Public API Wrappers

R-14425-19594
:   Th8_Memmove SHALL move n bytes from src to dst (overlapping regions permitted) via the platform's xMemmove callback.
R-42312-03916
:   Th8_Strcmp SHALL compare two NUL-terminated strings via the platform's xStrcmp callback and return a value less than, equal to, or greater than zero.
R-05138-38482
:   Th8_Strchr SHALL locate the first occurrence of byte c in string s via the platform's xStrchr callback.
R-51313-35869
:   Th8_Atoi SHALL convert a NUL-terminated decimal string to an integer via the platform's xAtoi callback.
R-39933-26635
:   Th8_Qsort SHALL sort an array in place via the platform's xQsort callback.
R-36320-28522
:   Th8_Vsnprintf SHALL format output into a buffer via the platform's xVsnprintf callback.
R-20586-05505
:   Th8_Snprintf SHALL be a variadic convenience wrapper that builds a va_list and delegates to Th8_Vsnprintf.

#### 32.9  POSIX I/O

R-37252-15077
:   The POSIX platform's xInput, xOutput, and xOutputError callbacks SHALL use native POSIX read() and write() system calls, interpreting a non-NULL pChannel as a POSIX file descriptor via TH8_PTR2INT.

#### 32.10  Channel I/O Routing

R-53947-42705
:   Th8_ChannelWrite SHALL prefer the platform's xOutput callback for channel writes, falling back to xChannelControl with TH8_CHANCTL_WRITE only when xOutput is not available.
R-10069-04039
:   Th8_ChannelRead SHALL prefer the platform's xInput callback for channel reads, falling back to xChannelControl with TH8_CHANCTL_READ only when xInput is not available.

#### 32.11  File Subcommands (TH8-specific)

The Tcl-canonical `file` sub-commands (`extension`, `nativename`,
`pathtype`, `rootname`, `separator`, `type`) are documented in
the *Tcl Language Standard* §20.  The following are TH8-specific
additions.

R-54388-15254
:   [file rootpath name] SHALL return "." if the path resolves under the base directory, or the empty string if the path resolves outside the base directory, via the platform's xGetRootPath callback.
R-11676-50485
:   [file same name1 name2] SHALL return non-zero only if both names refer to the exact same physical file, as determined by the platform's xSameFile callback (inode/device comparison on POSIX, file identity on Windows).
R-62378-19050
:   [file channels] SHALL include "stdin" and "stdout" in its output when the platform provides xInput and xOutput callbacks, in addition to any temporary file channels.
R-24372-60291
:   [file under name1 name2] SHALL return non-zero if name1 resides within the directory hierarchy of name2.
R-53536-14671
:   [file validname path ?pathType?] SHALL return non-zero only if the path is syntactically valid for the operating system. This command SHALL be exempt from base-path relative path restrictions. The only supported pathType value is "None" (case-insensitive).

#### 32.12  Hash Command

R-12269-46713
:   [hash normal algorithm string] SHALL compute a cryptographic digest of string using the specified algorithm and return the hexadecimal result. Only "SHA512" (case-insensitive) is supported as an algorithm.

#### 32.15  Platform Callback Extensions

R-48709-07272
:   The xDataExists platform callback SHALL accept an optional int *pAttrs output parameter. When non-NULL, the callback SHALL store file type attributes using the TH8_FILE_ATTR_FILE, TH8_FILE_ATTR_DIRECTORY, TH8_FILE_ATTR_UNSUPPORTED, and TH8_FILE_ATTR_SYMLINK constants.
R-32063-53221
:   The xGetParentPid platform callback SHALL return the parent process ID, or 0 if unavailable.
R-31253-15832
:   The xGetThreadId platform callback SHALL return the current thread ID as a 64-bit unsigned integer.
R-58471-10958
:   The xGetRootPath platform callback SHALL resolve the filesystem root or mount point for a given path.
R-47728-37792
:   The xSameFile platform callback SHALL compare two paths and return non-zero only if they refer to the same physical file.

#### 32.16  Temporary Channel Close

R-16003-05438
:   The xCloseTemporaryData platform callback SHALL be called before explicitly closing a temporary channel. If it returns non-TH8_OK, the close is vetoed and the channel remains open.
R-40336-27663
:   The [close] command without arguments SHALL close all temporary file channels, skipping standard channels.
R-40340-22859
:   The [close] command with a standard channel name (stdin, stdout, stderr) SHALL detach that channel, causing subsequent I/O to return EOF or error.

#### 32.17  Attempt Allocation Functions

R-18197-02806
:   `Th8_AttemptMalloc` SHALL allocate the requested number of bytes and return a pointer to the allocated memory, or NULL on failure. It SHALL NOT call the xPanic callback on allocation failure. Callers SHALL check for NULL.
R-17760-02773
:   `Th8_AttemptRealloc` SHALL resize the given memory block to the requested number of bytes and return a pointer to the resized memory, or NULL on failure. It SHALL NOT call the xPanic callback on allocation failure. Callers SHALL check for NULL.
R-46067-27528
:   Script-reachable allocation paths with unbounded sizes SHALL use `Th8_AttemptMalloc` or `Th8_AttemptRealloc` instead of `Th8_Malloc` or `Th8_Realloc`.

#### 32.18  Static Result Setting

R-50612-20316
:   `Th8_SetResultStatic` SHALL set the interpreter result to a static (non-allocated) string. The string SHALL NOT be copied or freed. The function SHALL never allocate memory. It SHALL be used in out-of-memory error paths where allocation is not possible.

#### 32.19  Default Platform Configuration

R-32491-46527
:   `Th8_UseDefaultPlatform` SHALL populate a caller-provided Th8_Platform struct with the default OS-appropriate platform configuration, using the same layering as th8sh.

#### 32.20  Environment Variable Access

R-47213-47439
:   `Th8_GetEnv` SHALL return the value of the named environment variable as an allocated UTF-8 string. The caller SHALL free the returned string with `Th8_Free`. If the variable is not set, NULL SHALL be returned.
R-59784-50985
:   The `xGetEnv` platform callback SHALL retrieve the value of an environment variable from the operating system, returning it as an allocated UTF-8 string.

#### 32.21  xPanic NULL Behavior

R-26147-24608
:   When xPanic is NULL (as in sandbox child interpreters), allocation failures in `Th8_Malloc` and `Th8_Realloc` SHALL return NULL instead of aborting the process. All script-reachable allocation paths SHALL use `Th8_AttemptMalloc` or `Th8_AttemptRealloc`, which never call xPanic.

#### 32.22  Memory Recovery Platform

R-08479-28857
:   The memory platform layer (`Th8_GetMemPlatform`) SHALL provide an `xNeedMemory` callback that clears the interpreter's IR cache, validates the requested size against `TH8_MX_ALLOC`, and retries allocation via the interpreter's `xMalloc` callback.

R-50978-10214
:   `Th8_RandomBytes` SHALL fill a buffer with cryptographically random bytes via the platform's `xRandomBytes` callback, returning `TH8_ERROR` if the callback is NULL.
R-02237-14041
:   `Th8_GetExePath` SHALL return the executable path via `xGetExePath`, returning NULL if unavailable.
R-12070-22255
:   `Th8_GetEvalDepth` SHALL return the current script evaluation nesting depth (0 = no eval active).
R-59078-48052
:   `Th8_SaveSignedOnly` and `Th8_RestoreSignedOnly` SHALL atomically save and restore the signed-only gate state and the preEval and preGetData callbacks using a caller-provided buffer of at least `TH8_SIGNED_SAVE_SIZE` bytes.
R-32694-45710
:   `Th8_NotifyDeleteInterp` SHALL invoke the platform's `xDeleteInterp` callback, passing the interpreter and the caller-provided context pointer.
R-41127-05315
:   The `xDeleteInterp` callback SHALL be invoked by `Th8_DeleteInterp` immediately before the interpreter struct is freed, allowing the platform to release per-interpreter resources.
R-21973-13714
:   `Th8_Sha512Hex` SHALL compute the SHA-512 hash of the input data and write a 128-character hex string to the output buffer.
R-28457-15581
:   `Th8_RsaExtractHash` SHALL recover the SHA-512 hash embedded in a PKCS#1 v1.5 RSA signature and write a 128-character hex string to the output buffer.
R-19594-12902
:   `Th8_TranslateLineEndings` SHALL verify that all newlines in the buffer are preceded by carriage return, then translate `\r\n` to `\n` in-place, returning `TH8_ERROR` if any bare `\n` is found.
R-42644-59892
:   `Th8_SaveSystemVar` SHALL snapshot all elements of the named array variable into an opaque handle without modifying the interpreter result.
R-43654-03456
:   `Th8_RestoreSystemVar` SHALL restore the saved array elements from the opaque handle and free the handle, without modifying the interpreter result.
R-23684-09371
:   `Th8_ExistsVar` SHALL return non-zero if the variable has an assigned value or is an array, zero otherwise.
R-16657-20632
:   The `xSleep` platform callback SHALL sleep for the specified number of milliseconds, yielding the CPU to the operating system.
R-17209-27346
:   The `xTimeUs` platform callback SHALL return the current monotonic time in microseconds, using a clock source immune to wall-clock adjustments.
R-49201-61647
:   `Th8_SetResultDouble` SHALL use the shortest decimal representation that round-trips exactly to the original IEEE 754 double value.


### 33  Key-Value Platform Abstraction

The `xKeyValue` platform callback provides a general-purpose
key-value store abstraction.  The first implementation
(`th8_env.c`) backs the abstraction with host process environment
variables, but embedders may provide alternative backends
(databases, registries, in-memory stores).

#### 33.1  Callback Contract

R-28687-20710
:   The `xKeyValue` platform callback SHALL support five operations identified by `TH8_KV_EXISTS`, `TH8_KV_LIST`, `TH8_KV_GET`, `TH8_KV_SET`, and `TH8_KV_UNSET`.  It SHALL return `TH8_OK` on success and `TH8_ERROR` on failure for all operations.

#### 33.2  EXISTS Operation

R-37121-11261
:   `TH8_KV_EXISTS` SHALL check whether the key identified by `zName` exists.  It SHALL return `TH8_OK` if the key exists and `TH8_ERROR` if it does not.  The `zValue` and `nValue` parameters are unused.

#### 33.3  LIST Operation

R-11116-56026
:   `TH8_KV_LIST` SHALL return a Tcl list of key names matching the glob pattern in `zName` via `Th8_SetResult`.  The glob matching SHALL use the same algorithm as the `string match` command (`Th8_GlobMatch`).  If `zName` is NULL or `nName` is zero, all keys SHALL be listed.

#### 33.4  GET Operation

R-03099-10973
:   `TH8_KV_GET` SHALL retrieve the value associated with the key `zName` and set it as the interpreter result via `Th8_SetResult`.  If the key does not exist, it SHALL return `TH8_ERROR`.

#### 33.5  SET Operation

R-58341-24198
:   `TH8_KV_SET` SHALL associate the key `zName` with the value `zValue`.  The value `zValue` SHALL be a valid UTF-8 string.  On Win32, the implementation SHALL use `ConvertUTF_v2` with strict validation for all UTF-8 to UTF-16 conversions.

#### 33.6  UNSET Operation

R-27832-52749
:   `TH8_KV_UNSET` SHALL remove the key identified by `zName`.  If the key does not exist, the operation SHALL succeed silently.

#### 33.7  Environment Variable Backend

R-45356-29159
:   The `Th8_GetEnvPlatform` function SHALL return a platform layer with `xKeyValue` backed by the host process environment variables.  On POSIX it SHALL use `getenv`, `setenv`, `unsetenv`, and `environ` iteration.  On Win32 it SHALL use `GetEnvironmentVariableW`, `SetEnvironmentVariableW`, and `GetEnvironmentStringsW`.

#### 33.8  Thread Safety

R-00313-45995
:   The environment variable backend SHALL serialize all operations with a file-scope mutex to prevent data races on concurrent access to the process environment.

#### 33.9  Platform Version

R-16823-25281
:   The `Th8_Platform` struct `nVersion` field SHALL be 4 to reflect the addition of the `xKeyValue` callback field (version 3 delta) and the manual-reset event callbacks `xEventCreate`, `xEventDestroy`, `xEventSet`, `xEventReset`, `xEventWait` (version 4 delta).  All platform static initializers under `Th8_GetPosixPlatform`, `Th8_GetWin32Platform`, `Th8_GetMacOSPlatform`, `Th8_GetIosPlatform`, `Th8_GetAndroidPlatform`, `Th8_GetCosmopolitanPlatform`, `Th8_GetLibcPlatform`, `Th8_GetNullIoPlatform`, `Th8_GetCurlPlatform`, `Th8_GetEnvPlatform`, `Th8_GetMemPlatform`, and `Th8_GetMimallocPlatform` SHALL use `nVersion = 4`.  `Th8_MergePlatform` SHALL reject version mismatches between source and destination platforms.

#### 33.10  EXISTS2 Operation

R-51464-55505
:   `TH8_KV_EXISTS2` SHALL check whether any key matches the glob pattern `zName` (or all keys if `zName` is NULL) AND the value matches the glob pattern `zValue` (or any value if `zValue` is NULL).  It SHALL return `TH8_OK` if any match exists and `TH8_ERROR` if none do.  Glob matching SHALL use `Th8_GlobMatch`.

#### 33.11  LIST2 Operation

R-47381-42074
:   `TH8_KV_LIST2` SHALL return a Tcl list of key names where the key matches the glob pattern `zName` (or all keys if `zName` is NULL) AND the value matches the glob pattern `zValue` (or any value if `zValue` is NULL).

#### 33.12  GET2 Operation

R-12345-42997
:   `TH8_KV_GET2` SHALL return a Tcl dictionary of key-value pairs where the key matches the glob pattern `zName` (or all keys if `zName` is NULL) AND the value matches the glob pattern `zValue` (or any value if `zValue` is NULL).

#### 33.13  SET2 Operation

R-41253-10109
:   `TH8_KV_SET2` SHALL set all keys matching the glob pattern `zName` (or all keys if `zName` is NULL) to the literal value `zValue`.  The implementation SHALL collect matching keys before mutation to avoid invalidating the iteration state.

#### 33.14  UNSET2 Operation

R-28703-03927
:   `TH8_KV_UNSET2` SHALL remove all keys where the key matches the glob pattern `zName` (or all keys if `zName` is NULL) AND the value matches the glob pattern `zValue` (or any value if `zValue` is NULL).  It SHALL return a Tcl dictionary of the deleted key-value pairs.  The implementation SHALL collect matching entries before deletion to avoid invalidating the iteration state.


### 34  Per-Callback Platform Context

#### 34.1  Context Override

R-20079-49924
:   `Th8_SetPlatformContext` SHALL associate a per-callback context pointer with a specific platform callback function on a given interpreter.  If `pCtx` is NULL, the override SHALL be removed and the callback SHALL revert to using the platform's default `pCtx`.

#### 34.2  Context Resolution

R-17852-33984
:   When dispatching a platform callback, the interpreter SHALL first check the per-callback context hash for an override.  If no override is found, the platform's default `pCtx` SHALL be used.  This resolution SHALL be performed for every dispatch site in the platform wrapper layer.

#### 34.3  Context Retrieval

R-56188-62606
:   `Th8_GetPlatformContext` SHALL return the context associated with a callback function.  If a per-callback override exists, it SHALL be returned; otherwise the platform's default `pCtx` SHALL be returned.

#### 34.4  Context Hash Lifecycle

R-36935-26839
:   The per-callback context hash table SHALL be created lazily on the first call to `Th8_SetPlatformContext`.  It SHALL be freed when the interpreter is deleted.  The hash entries' data pointers SHALL NOT be freed by the interpreter (ownership belongs to the extension that set them).


### 35  Platform Merge for Extensions

#### 35.1  Interpreter Platform Merge

R-41362-08739
:   `Th8_MergePlatformInterp` SHALL clone the interpreter's current platform, merge the source platform's non-NULL callbacks into the clone, replace the interpreter's platform pointer with the clone, and free any previous clone.  It SHALL return `TH8_OK` on success and `TH8_ERROR` on failure.

#### 35.2  Clone Tracking

R-33219-49367
:   The interpreter SHALL track whether its platform was cloned by `Th8_MergePlatformInterp`.  On interpreter deletion, a cloned platform SHALL be freed via `Th8_FreePlatform` before the interpreter struct is freed.


---

## Part IV --- Testing, Debugging, and Recovery


### 36  Fuzz Testing Infrastructure

TH8 includes a fuzz testing infrastructure with six harnesses
targeting distinct attack surfaces of the interpreter.  The
harnesses support three execution modes: standalone (stdin-based),
libFuzzer, and AFL++.

#### 36.1  Fuzz Targets

| Target | Source | Attack surface |
|--------|--------|----------------|
| `fuzz_eval` | `fuzz/fuzz_eval.c` | Script evaluation (`Th8_Eval`) |
| `fuzz_expr` | `fuzz/fuzz_expr.c` | Expression parser (`Th8_Expr`) |
| `fuzz_list` | `fuzz/fuzz_list.c` | List parser (`Th8_SplitList`) |
| `fuzz_format` | `fuzz/fuzz_format.c` | Format command (`[format]`) |
| `fuzz_harpy` | `fuzz/fuzz_harpy.c` | Harpy `.b64sig` parser (`Th8_HarpySigLoad`) |
| `fuzz_snk` | `fuzz/fuzz_snk.c` | SNK/CAPI key parser (`Th8_RsaKeyLoad`) |

All harnesses share common initialization via `fuzz/fuzz_common.h`.
The `harpy` and `snk` targets are gated on `TH8_ENABLE_CRYPTOGRAPHY`.

#### 36.2  Execution Modes

Three modes are selected via `FUZZ_MODE`:

-   **standalone** (default) --- Plain executables that read one
    input from stdin.  Used for manual testing and integration
    with external fuzzers via pipe.

-   **libfuzzer** --- Compiled with `-fsanitize=fuzzer` for LLVM's
    built-in coverage-guided fuzzer.  Each target runs as a
    self-contained fuzzer process.

-   **afl** --- Compiled with `afl-clang-fast` for AFL++ coverage
    instrumentation.  A recursive make rebuilds the entire static
    library into `bin-afl/` so that every object carries AFL
    feedback.

All modes enable AddressSanitizer and UndefinedBehaviorSanitizer.

#### 36.3  Deterministic Execution

When `TH8_FUZZ_STANDALONE` is defined (standalone and AFL modes),
the platform random-bytes callback uses a deterministic PRNG with
a fixed seed instead of `/dev/urandom`.  This ensures identical
hash-table layouts and internal random tokens across runs,
eliminating non-deterministic coverage maps that cause false
instability warnings in coverage-guided fuzzers.

Binary loading (`dlopen`/`dlsym`/`dlclose`) is also excluded under
`TH8_FUZZ_STANDALONE` since the fuzz harnesses never invoke
`[load]` and the `dlopen` symbol would otherwise trigger spurious
warnings from AFL++.

#### 36.4  Seed Corpus Generation

The `tools/mkfuzzcorpus.tcl` script generates seed corpora for
all six targets:

-   **eval** --- Extracts test bodies from the conformance test
    suite plus synthetic edge cases (1600+ seeds).
-   **expr** --- Arithmetic, boolean, function call, and boundary
    expressions.
-   **list** --- Simple and whitespace-heavy list strings.
-   **format** --- Format specifiers including width, precision,
    flags, and malformed patterns.
-   **harpy** --- Real `.b64sig` files from the project plus
    synthetic malformed signatures.
-   **snk** --- Real `.snk` files plus synthetic CAPI blobs and
    random byte sequences.


### 37  Cancel-Unwind Recovery

R-16133-17525
:   When `TH8_CANCEL_UNWIND` fires, the outermost `Th8_Eval` SHALL restore `nEvalDepth` to its entry value and clear the cancel state, allowing the interpreter to accept new evaluations.
R-48881-39702
:   After cancel-unwind recovery, subsequent `Th8_Eval` calls SHALL succeed normally.
R-16309-49714
:   The NRE trampoline SHALL consume non-unwind cancellation flags in a one-shot manner, allowing `catch` to intercept the error.


### 38  Fault Injection Testing

The fault injection platform layer provides systematic testing of
allocation failure paths.  It wraps a real platform with a
fault-injecting layer that selectively returns NULL from allocation
callbacks after a configurable number of successful allocations.

#### 38.1  Compile-Time Gate

R-16678-32222
:   The fault injection platform layer SHALL be gated on `TH8_ENABLE_FAULT_INJECTION`. When the gate is not defined, the fault injection API functions SHALL not be compiled and their stubs table slots SHALL be NULL.

#### 38.2  Fault Layer API

R-00022-45585
:   `Th8_FaultInstall` SHALL install a fault-injecting platform wrapper on a given interpreter, and `Th8_FaultUninstall` SHALL restore the original platform. The fault layer SHALL intercept allocation callbacks and return NULL after a configurable number of successful allocations.

#### 38.3  Test Command

R-62256-21792
:   The `::th8testlib::fault eval` command SHALL create an isolated child interpreter with `xPanic` set to NULL, install the fault injection layer, evaluate the given script, capture the child result and return code, uninstall the fault layer, destroy the child, and return the results to the parent interpreter.

#### 38.4  Buffer Growth Safety

R-44847-59776
:   Internal buffer growth functions (`th8BufWrite`, `th8BufAddChar`) SHALL check for allocation failure and return safely without writing to a NULL buffer pointer.

#### 38.5  Compile-Time Options Completeness

R-41515-54395
:   The compile-time options list returned by `Th8_GetCompileOptions` SHALL include every user-tunable `TH8_` preprocessor gate that is active in the current build.

#### 38.6  Fault Sweep Correctness

R-55401-13047
:   Fault injection testing at every allocation threshold from 1 to the maximum allocation count SHALL produce either a clean error (`TH8_ERROR` with an error message) or a correct result, and SHALL NOT crash, corrupt state, or trigger undefined behavior.


### 39  Script Debugging API

#### 39.1  Debug Callback

R-19600-10064
:   `Th8_SetDebugCallback` SHALL install or remove a debug callback on an interpreter.  The callback SHALL fire at every command boundary when debugging is active (step mode is non-NONE or breakpoints are set).  Passing NULL SHALL remove the callback.

#### 39.2  Zero Overhead

R-60983-44621
:   When no debug callback is installed (`xDebug` is NULL), the debug check in `Th8_Ready` SHALL have zero overhead beyond a single pointer comparison.

#### 39.3  Debug Events

R-17084-33855
:   The debug callback SHALL receive an event type (`TH8_DEBUG_STEP` or `TH8_DEBUG_BREAKPOINT`), the current script name, 1-based line number, and call frame depth.  It SHALL return `TH8_OK` to continue, `TH8_BREAK` to suspend the interpreter, or `TH8_ERROR` to abort.

#### 39.4  Step Modes

R-33869-41827
:   `Th8_SetStepMode` SHALL support four modes: `TH8_STEP_NONE` (free-run), `TH8_STEP_INTO` (break at every command), `TH8_STEP_OVER` (break when frame depth is at or below starting depth), and `TH8_STEP_OUT` (break when frame depth is below starting depth).

#### 39.5  Breakpoints

R-27403-27790
:   `Th8_SetBreakpoint` SHALL register a breakpoint at a given script name and line number, returning a unique positive integer ID.  `Th8_ClearBreakpoint` SHALL remove a breakpoint by ID.  `Th8_ClearAllBreakpoints` SHALL remove all breakpoints.

#### 39.6  Breakpoint Lookup

R-43788-42163
:   Breakpoint lookup SHALL be performed at every command boundary via a hash table keyed on script name and line number, providing O(1) lookup time.

#### 39.7  Freeze on Break

R-54392-24527
:   When the debug callback returns `TH8_BREAK`, the interpreter SHALL be frozen via `Th8_Freeze` and `Th8_Ready` SHALL return `TH8_SUSPEND`.  The NRE callback chain SHALL be preserved on the heap, allowing `Th8_Thaw` followed by `Th8_Eval` to resume from the exact point of suspension.

#### 39.8  Frame Inspection

R-38951-45462
:   `Th8_GetFrameCount` SHALL return the number of call frames on the stack.  `Th8_GetFrameInfo` SHALL return the procedure name, script name, and line number for a given frame index (0 = innermost).

#### 39.9  Eval at Frame

R-48493-50344
:   `Th8_EvalAtFrame` SHALL evaluate a script in the variable context of a specific call frame, allowing watch expressions and interactive debugging during a pause.

---

## Part V --- Pluggable Backends


### 40  SQLite Key-Value Backend

#### 40.1  Extension Loading

R-16174-56903
:   The SQLite key-value extension SHALL be loadable via the `[load]` command.  Its initialization function SHALL call `sqlite3_initialize`, open a database connection, create the key-value table, prepare cached SQL statements, merge the SQLite-backed platform into the interpreter, and register a per-callback context for its `xKeyValue` callback.

#### 40.2  Database Schema

R-03069-48740
:   The SQLite key-value backend SHALL use a table with columns `name TEXT PRIMARY KEY NOT NULL` and `value TEXT NOT NULL`.  The table name SHALL be configurable via the `Th8_SQLiteKvCtx` structure.

#### 40.3  Statement Cleanup

R-13764-37417
:   On extension unload, the SQLite backend SHALL use `sqlite3_next_stmt` to iterate and finalize all prepared statements on each connection before closing it via `sqlite3_close_v2`, and SHALL call `sqlite3_shutdown` after all connections are closed.


### 41  Test Key-Value Command

#### 41.1  Command Syntax

R-31231-17812
:   The `::th8testlib::kv` command SHALL accept the syntax `::th8testlib::kv op ?name? ?value?` where `op` is a case-insensitive operation name corresponding to a `TH8_KV_*` constant with the prefix removed.

#### 41.2  Dispatch

R-58721-31908
:   The `::th8testlib::kv` command SHALL dispatch operations through the `Th8_KeyValue` public API, which routes to the interpreter's platform `xKeyValue` callback.  The command SHALL NOT directly access any specific backend implementation.

---

*End of Document*
