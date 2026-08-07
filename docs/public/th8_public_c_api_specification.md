# TH8 Public C API Specification

*Version 1.0 -- date 2026-07-03 (currency-reviewed; 243 unique
`R-NNNNN-NNNNN` markers verified clean against
`tools/mkreq.tcl --verify`; cross-checked against `src/th8.h`).*

This document specifies the public C embedding API for the TH8
interpreter.  It is the companion to the *Tcl Language Standard*
(`tcl_language_standard_v1.md`), which defines portable
script-visible behaviors, and to *TH8 Language Extensions*
(`th8_language_extensions.md`), which defines TH8-specific
script-visible additions.  This document defines the C-level
interface for:

- Interpreter creation, configuration, and destruction
- Platform abstraction callbacks
- Script evaluation and result management
- Resource limits and security enforcement
- Cryptographic signing and verification
- Dynamic extension loading
- Testing infrastructure (fault injection, fuzzing)
- Script debugging

## Normative Status

Requirements in this document use R-markers (e.g., R-12345-67890)
that are traceable to the TH8 conformance test suite.  Each
requirement marked with an R-marker has at least one test that
verifies the stated behavior.

---


## Part VI --- Security and Cryptography



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

#### 29.1a  Script Annotations

R-44468-02007
:   Script annotations in the format <<notBefore:YYYY_MM_DDThh_mm_ssZ>> and <<notAfter:YYYY_MM_DDThh_mm_ssZ>> SHALL be validated with strict ISO-8601 date-time rules including leap year bounds. The underscore delimiters are an intentional deviation for Eagle compatibility.
R-62434-18753
:   Script annotations SHALL be extracted from all scripts when the policy callbacks are installed, regardless of whether the signed-only policy is enabled. The parsed values SHALL populate the ::th8_security array.
R-43104-17325
:   When the signed-only policy is enabled, any script with a malformed annotation SHALL be rejected. Scripts whose current time falls outside the notBefore/notAfter range SHALL also be rejected.

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


### 30  Cryptographic API

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


## Part VII --- Platform Embedding API



### 31  Platform Initialization

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

R-34644-16389
:   `Th8_CreateInterp` stores a pointer to the `Th8_Platform` struct, not a copy. The platform struct must remain valid and at a stable address for the entire lifetime of the interpreter.
R-15464-05884
:   Stack-allocated `Th8_Platform` structs are safe only when the interpreter is created and destroyed within the same stack frame. For interpreters that outlive the creating function, the platform must be static, global, or heap-allocated.

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

R-50978-10214
:   `Th8_RandomBytes` SHALL fill a buffer with cryptographically random bytes via the platform's `xRandomBytes` callback, returning `TH8_ERROR` if the callback is NULL.
R-02237-14041
:   `Th8_GetExePath` SHALL return the executable path via `xGetExePath`, returning NULL if unavailable.
R-12070-22255
:   `Th8_GetEvalDepth` SHALL return the current script evaluation nesting depth (0 = no eval active).
R-59078-48052
:   `Th8_SaveSignedOnly` and `Th8_RestoreSignedOnly` SHALL atomically save and restore the signed-only gate state and the preEval and preGetData callbacks using a caller-provided buffer of at least `TH8_SIGNED_SAVE_SIZE` bytes.
R-41127-05315
:   The `xDeleteInterp` callback SHALL be invoked by `Th8_DeleteInterp` immediately before the interpreter struct is freed, allowing the platform to release per-interpreter resources.
R-21973-13714
:   `Th8_Sha512Hex` SHALL compute the SHA-512 hash of the input data and write a 128-character hex string to the output buffer.
R-28457-15581
:   `Th8_RsaExtractHash` SHALL recover the SHA-512 hash embedded in a PKCS#1 v1.5 RSA signature and write a 128-character hex string to the output buffer.
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

The protected-region allocator that backs sensitive-result
storage is implemented as `TH8_INTERNAL` helpers, not as
`TH8_API` symbols.  Its specification has moved to
[`th8_internal_api_specification.md`](th8_internal_api_specification.md)
§I-3.  Embedders interact with the protected region only
through the public `Th8_SetResultSensitive` / `Th8_GetResult`
surface documented in §35.5 ("Sensitive Result Storage")
below.

#### 32.6  Channel Control Operations

R-55340-20582
:   TH8_CHANCTL_OPEN (opcode 6) SHALL open a file at path pBuf (nArg1 bytes long), with nArg2 selecting the mode (0 for read-only, 1 for write-create-truncate), and return the opaque handle in *pnResult.

#### 32.7  Platform Struct Versioning

(This section previously specified `nVersion == 2` for a rationalized
callback ordering.  That intermediate version was never released;
subsequent revisions added `xKeyValue` (version 3, also never
released as a standalone version) and the manual-reset event
callbacks (`xEventCreate` / `xEventDestroy` / `xEventSet` /
`xEventReset` / `xEventWait`).  The current shipping value is
`nVersion == 4`; see §33.9 for the normative statement.  This
section is retained as a placeholder so the section numbering
remains stable across revisions.)

#### 32.8  Internal Platform Wrappers (moved)

The thin libc-equivalent platform-callback wrappers
(`th8Memmove`, `th8Strcmp`, `th8Strchr`, `th8Atoi`, `th8Qsort`,
`th8Snprintf`, `th8Vsnprintf`) are `TH8_INTERNAL` helpers, not
`TH8_API` symbols.  Their specifications have moved to
[`th8_internal_api_specification.md`](th8_internal_api_specification.md)
§I-4.  External embedders should call the underlying platform
callback directly (`interp->pPlatform->x<Name>(...)`).

#### 32.9  POSIX I/O

R-37252-15077
:   The POSIX platform's xInput, xOutput, and xOutputError callbacks SHALL use native POSIX read() and write() system calls, interpreting a non-NULL pChannel as a POSIX file descriptor via TH8_PTR2INT.

#### 32.10  Channel I/O Routing (moved)

The script-visible channel-I/O dispatchers (`th8ChannelWrite`,
`th8ChannelRead`) are `TH8_INTERNAL` helpers, not `TH8_API`
symbols.  Their specifications have moved to
[`th8_internal_api_specification.md`](th8_internal_api_specification.md)
§I-5.

#### 32.11  File Subcommands

R-16870-62748
:   [file extension name] SHALL return the file extension (the last dot and everything after it in the last path component), or the empty string if no extension is present.
R-48203-47216
:   [file nativename name] SHALL convert directory separators to the native form: forward slashes on POSIX, backslashes on Windows.
R-25988-01568
:   [file pathtype name] SHALL return "absolute" for paths beginning with a root separator or drive letter plus separator, "volumerelative" for paths beginning with a drive letter without a separator (Windows only), and "relative" for all other paths.
R-29418-47253
:   [file rootname name] SHALL return the path with the file extension removed (everything before the last dot in the last path component).
R-54388-15254
:   [file rootpath name] SHALL return "." if the path resolves under the base directory, or the empty string if the path resolves outside the base directory, via the platform's xGetRootPath callback.
R-11676-50485
:   [file same name1 name2] SHALL return non-zero only if both names refer to the exact same physical file, as determined by the platform's xSameFile callback (inode/device comparison on POSIX, file identity on Windows).
R-41650-23802
:   [file separator] without arguments SHALL return the native directory separator character. With a name argument, it SHALL return the first directory separator found in the name, or the native separator if none is present.
R-16710-28334
:   [file type name] SHALL return "file" for regular files, "directory" for directories, "file symbolicLink" or "directory symbolicLink" for symbolic links to files or directories respectively, "unsupported" for other file types, and "unknown" when the native API fails or the file does not exist.
R-62378-19050
:   [file channels] SHALL include "stdin" and "stdout" in its output when the platform provides xInput and xOutput callbacks, in addition to any temporary file channels.
R-24372-60291
:   [file under name1 name2] SHALL return non-zero if name1 resides within the directory hierarchy of name2.
R-53536-14671
:   [file validname path ?pathType?] SHALL return non-zero only if the path is syntactically valid for the operating system. This command SHALL be exempt from base-path relative path restrictions. The only supported pathType value is "None" (case-insensitive).

#### 32.12  Hash Command

R-12269-46713
:   [hash normal algorithm string] SHALL compute a cryptographic digest of string using the specified algorithm and return the hexadecimal result. Only "SHA512" (case-insensitive) is supported as an algorithm.

#### 32.13  Info Subcommands

R-02028-09998
:   [info context] SHALL return a stable 128-character hexadecimal string computed by SHA-512 hashing a 32-byte seed composed of the parent process ID (4 bytes), process ID (4 bytes), thread ID (8 bytes), and 16 bytes of cryptographic random entropy. The value SHALL be computed once and cached for the lifetime of the process.
R-08542-47922
:   [info varlinks] SHALL return a list of all variable names in the current call frame that are linked to variables in other frames via upvar, global, or variable.

#### 32.14  Variable Command Fix

R-19756-08248
:   The [variable] command with a fully qualified namespace name (e.g., variable ::foo::x) SHALL create a local link using the tail portion of the name, matching Tcl 8.x behavior.

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

R-21066-15504
:   `Th8_AttemptMalloc` SHALL allocate the requested number of bytes and return a pointer to the allocated memory, or NULL on failure. It SHALL NOT call the xPanic callback on allocation failure. Callers MUST check for NULL.
R-63076-33930
:   `Th8_AttemptRealloc` SHALL resize the given memory block to the requested number of bytes and return a pointer to the resized memory, or NULL on failure. It SHALL NOT call the xPanic callback on allocation failure. Callers MUST check for NULL.
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

R-53779-03588
:   `TH8_KV_SET2` SHALL execute as a single atomic transaction over the underlying KV backend: either every matched key is updated to `zValue` or no key is updated.  A failure of any individual write, exhaustion of `SQLITE_BUSY` retries, or process termination during the operation SHALL NOT leave the KV store in a state where some matched keys hold the new value while others retain their pre-call value.

#### 33.14  UNSET2 Operation

R-28703-03927
:   `TH8_KV_UNSET2` SHALL remove all keys where the key matches the glob pattern `zName` (or all keys if `zName` is NULL) AND the value matches the glob pattern `zValue` (or any value if `zValue` is NULL).  It SHALL return a Tcl dictionary of the deleted key-value pairs.  The implementation SHALL collect matching entries before deletion to avoid invalidating the iteration state.

R-08097-58386
:   `TH8_KV_UNSET2` SHALL execute as a single atomic transaction over the underlying KV backend: either every matched (key, value) pair is removed and reported in the returned dictionary or no pair is removed.  A failure of any individual delete, exhaustion of `SQLITE_BUSY` retries, or process termination during the operation SHALL NOT leave the KV store in a state where some matched pairs are gone while others remain.


### 34  Per-Callback Platform Context

#### 34.1  Context Override

R-03660-39492
:   `Th8_SetPlatformContext` SHALL associate a per-callback context pointer with a specific platform callback function on a given interpreter.  If `pCtx` is NULL, the override SHALL be removed and the callback SHALL revert to using the platform's default `pCtx`.  The `xCallback` parameter type is `Th8_PlatformFunc` (a generic function pointer typedef defined as `void (*)(void)`); callers SHALL cast their actual platform callback function pointer to `Th8_PlatformFunc` to avoid ISO C function-pointer-to-object-pointer conversion warnings.

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


## Part VIII --- Testing, Debugging, and Recovery



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


### 37a  Thread-Safe Public APIs

The TH8 public API surface is **single-threaded per
interpreter**.  Every `Th8_*` API MUST be invoked on the
interpreter's owning thread -- the thread that created it
with `Th8_CreateInterp` -- with the following documented
exceptions, each callable from any thread:

* `Th8_CancelEval` (covered in §37);
* the event-queue family (`Th8_CreateAsyncState`,
  `Th8_FinalizeAsyncState`, `Th8_QueueEvent`, §37a.1);
* the thread-identity queries `Th8_GetThreadId` and
  `Th8_GetInterpThreadId` (§37a.3);
* the per-thread runtime hooks `Th8_ThreadInit` /
  `Th8_ThreadDone`.

In **debug** builds (`TH8_DEBUG`) the contract is enforced
at the most important owning-thread-only entry points --
the evaluation choke point, interpreter teardown, language
registration, and the variable / result / command surface
-- by the internal `TH8_ASSERT_OWNER` assertion, which
aborts if a foreign thread calls one of them.  In release
and coverage builds the assertion compiles to nothing.  The
owning thread is captured once, atomically, in
`Th8_CreateInterp`; on hosts whose platform provides no
`xGetThreadId` the id is 0 and affinity is not enforced.

#### 37a.1  `Th8_QueueEvent` thread-safety contract

```c
TH8_API void *Th8_CreateAsyncState(Th8_Interp *interp,
    void *pCtx);
TH8_API void  Th8_FinalizeAsyncState(void *pState);
TH8_API int   Th8_QueueEvent(void *pState,
    int (*xCallback)(Th8_Interp *interp, void *pCtx));
```

R-46584-22506
:   `Th8_CreateAsyncState` SHALL be called only on the
interpreter's owning thread.  It returns an opaque pState
handle whose lifetime is owned by the embedder.  The
returned pState SHALL be NULL if any of the required
threading callbacks (`xMutexInit`, `xMutexEnter`,
`xMutexLeave`, `xMutexFinal`, `xEventCreate`, `xEventSet`,
`xEventReset`, `xEventWait`, `xEventDestroy`,
`xIntCmpXchg`) is missing on the interpreter's platform.

R-02185-13991
:   `Th8_QueueEvent` SHALL be callable from any thread.
Implementations MUST guarantee atomicity of the
"enqueue + signal" critical section; in particular, a
queued event MUST be observable to the interpreter's
draining thread before the next event-handle wait can
sleep through the new arrival.

R-49706-62124
:   `Th8_QueueEvent` SHALL atomically observe the
`nDeleted` field at the start of the pState (via
compare-exchange-zero-with-zero on the cached
`xIntCmpXchg`).  If non-zero, the call SHALL return
TH8_ERROR without dereferencing the interpreter pointer.
This is the mechanism that makes the API safe across
interpreter teardown.

R-43533-06664
:   `Th8_QueueEvent` SHALL NOT touch
`interp->pPlatform` directly.  All platform function
pointers it needs (`xMalloc`, `xFree`, `xMutexEnter`,
`xMutexLeave`, `xEventSet`, `xIntCmpXchg`) SHALL be
captured into the pState at `Th8_CreateAsyncState` time
(on the interpreter's owning thread, where the platform
pointer is stable) and read from there thereafter.

R-12270-09732
:   `Th8_DeleteInterp` SHALL atomically increment the
`nDeleted` field of every pState registered with the
interpreter, and clear each pState's interpreter back-
pointer, BEFORE freeing any interpreter state.  The
pState memory itself SHALL NOT be freed by
`Th8_DeleteInterp`; the embedder owns lifetime via
`Th8_FinalizeAsyncState`.

R-56128-57256
:   Embedders MUST quiesce all worker threads (no
in-flight `Th8_QueueEvent` calls) before invoking
`Th8_DeleteInterp`.  The `nDeleted` flag is a fail-safe
backstop for stragglers that complete after teardown
begins, NOT a substitute for thread-lifecycle
management.  An in-flight `Th8_QueueEvent` that has
passed the `nDeleted` check but not yet completed has a
narrow race window with teardown that the embedder is
responsible for closing.

R-27142-65509
:   `Th8_FinalizeAsyncState` SHALL be safe to call on a
pState whose interpreter has already been deleted; the
pState's cached `xFree` releases the storage.  It is NOT
safe to call concurrently with `Th8_QueueEvent` on the
same pState.

#### 37a.2  Script-side surface

The script-level `[update]` and `[vwait]` commands drain
the same per-interpreter queue.  Their semantics are
documented in the Tcl Language Standard, §25.5–§25.6.
Scripts have NO API for *adding* events to the queue —
that is a deliberate security boundary preventing
untrusted script from injecting work into the embedder's
event loop.

#### 37a.3  Thread-identity queries

```c
TH8_API th8_uint64_t Th8_GetThreadId(Th8_Interp *interp);
TH8_API th8_uint64_t Th8_GetInterpThreadId(Th8_Interp *interp);
TH8_API th8_uint64_t Th8_Int64CmpXchg(Th8_Interp *interp,
    volatile th8_uint64_t *pTarget, th8_uint64_t iExchange,
    th8_uint64_t iComparand);
```

R-31635-30129
:   `Th8_GetThreadId` SHALL return the identifier of the
:   CALLING thread, obtained through the platform's
:   `xGetThreadId` callback, or 0 if the platform provides no
:   such callback.  It SHALL be callable from any thread: it
:   reports the caller's own thread and reads only immutable
:   interpreter state, so it is exempt from the single-
:   threaded-per-interpreter affinity contract.

R-31999-07274
:   `Th8_GetInterpThreadId` SHALL return the identifier of the
:   thread that OWNS the interpreter -- the thread that called
:   `Th8_CreateInterp` -- read atomically through the 64-bit
:   interlocked compare-exchange, or 0 if the owning thread was
:   never captured.  It SHALL be callable from any thread and is
:   exempt from the affinity contract, so a foreign thread MAY
:   call it to discover the owner and compare that against its
:   own `Th8_GetThreadId`.

R-47926-46982
:   The owning-thread identifier SHALL be captured exactly once,
:   during `Th8_CreateInterp`, and published atomically via the
:   platform's `xIntCmpXchg64` 64-bit compare-exchange callback
:   (exposed publicly as `Th8_Int64CmpXchg`).  Every read of the
:   identifier -- by `Th8_GetInterpThreadId` and by the debug
:   affinity assertion -- SHALL use the same interlocked
:   compare-with-zero read so that foreign threads always
:   observe a consistent value.

R-10851-48859
:   In debug builds, calling an owning-thread-only API from a
:   thread other than the interpreter's owner SHALL abort via
:   the affinity assertion; in release and coverage builds the
:   assertion SHALL have no effect.  On a host whose platform
:   provides no `xGetThreadId`, the owning-thread identifier is
:   0 and affinity SHALL NOT be enforced.


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


## Part IX --- Pluggable Backends



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


## Part X --- Memory Management



### 42  Overflow-Safe Allocation Macros

The following macros are defined in `th8.h` and wrap the
`Th8_SafeAlloc*` functions with automatic `__FILE__` and `__LINE__`
capture for diagnostic tracing.

#### 42.1  TH8_ALLOC

R-32518-09232
:   `TH8_ALLOC(interp, nByte)` SHALL allocate `nByte` bytes of zero-filled memory using `Th8_SafeAlloc`, returning NULL on failure or overflow.

#### 42.2  TH8_ALLOC_STR

R-35742-60058
:   `TH8_ALLOC_STR(interp, nLen)` SHALL allocate `nLen+1` bytes (overflow-safe) for a NUL-terminated string using `Th8_SafeAllocStr`.

#### 42.3  TH8_ALLOC_MUL / TH8_ALLOC_ADD / TH8_ALLOC_MUL_ADD

R-17429-23305
:   `TH8_ALLOC_MUL(interp, a, b)`, `TH8_ALLOC_ADD(interp, a, b)`, and `TH8_ALLOC_MUL_ADD(interp, a, b, c)` SHALL reject any allocation whose size computation overflows `size_t`, returning NULL instead of wrapping.

#### 42.4  TH8_ALLOC_STR_ADD

R-21088-56431
:   `TH8_ALLOC_STR_ADD(interp, k, n)` SHALL allocate `k + n + 1` bytes using `Th8_SafeAllocStrAdd`, validating both the `(k + n)` and `(+ 1)` operations for `size_t` overflow.  Used for the "prefix + tail + NUL" string-buffer pattern; replaces the unsafe form `TH8_ALLOC_ADD(interp, k, n + 1)` in which the inner `(n + 1)` addition is plain C arithmetic that wraps silently to 0 on overflow.

#### 42.5  TH8_ALLOC_STR_MUL

R-25747-09737
:   `TH8_ALLOC_STR_MUL(interp, n, sz)` SHALL allocate `(n + 1) * sz` bytes using `Th8_SafeAllocStrMul`, validating both the `(n * sz)` product and the `(+ sz)` addition for `size_t` overflow.  Used for the "wide-string buffer with NUL slot" pattern; replaces the unsafe form `TH8_ALLOC_MUL(interp, n + 1, sz)`.

#### 42.6  TH8_ALLOC_MUL_ADD2

R-22764-62182
:   `TH8_ALLOC_MUL_ADD2(interp, a, b, c, d, e)` SHALL allocate `(a*b) + (c*d) + e` bytes using `Th8_SafeAllocMulAdd2`, performing four sequential overflow checks: `(a*b)`, `(c*d)`, the sum of products, and the trailing addition of `e`.  Used for packed-record allocation patterns (e.g., `nE * sizeof(char *) + nE * sizeof(size_t) + nStringTotal`); replaces the unsafe form in which the multi-term sum is pre-computed in plain C `size_t` arithmetic before being passed to `TH8_ALLOC`.


### 42a  Safe Allocation Anti-Patterns

The macros in section 42 perform overflow-safe arithmetic on their
arguments, but this guarantee is voided if the caller pre-computes
any sub-expression in plain C before passing the result to the macro.
This section catalogs the recurring anti-patterns and prescribes the
canonical safe form for each.

#### 42a.1  Pre-NUL string allocation (`X + 1` inside TH8_ALLOC)

```
Anti-pattern:  TH8_ALLOC(interp, n + 1)
Why unsafe:    If n == SIZE_MAX, n + 1 wraps to 0 before the macro
               sees it.  The safe-alloc receives 0 and either rejects
               it or allocates a zero-byte block; either way the
               caller proceeds against a buffer too small for the
               intended payload.
Correct form:  TH8_ALLOC_STR(interp, n)
               -- the +1 is performed inside Th8_SafeAllocStr with
                  an explicit overflow check.
```

R-13115-20764
:   `TH8_ALLOC_STR(interp, n)` SHALL be used in preference to `TH8_ALLOC(interp, n + 1)` wherever the `+ 1` represents a single trailing NUL terminator.

#### 42a.2  Combined prefix + tail + NUL (`K, T + 1` inside TH8_ALLOC_ADD)

```
Anti-pattern:  TH8_ALLOC_ADD(interp, k, t + 1)
Why unsafe:    The inner (t + 1) is plain C addition; it wraps before
               the outer overflow check ever sees the operand.
Correct form:  TH8_ALLOC_STR_ADD(interp, k, t)
               -- both additions ((k + t) and (+ 1)) are checked
                  inside the safe layer.
```

R-46774-46631
:   `TH8_ALLOC_STR_ADD(interp, k, n)` SHALL be used for the "prefix + tail + NUL" allocation pattern, replacing `TH8_ALLOC_ADD(interp, k, n + 1)`.

#### 42a.3  Element-count + 1 product (`(N+1) * T` inside TH8_ALLOC_MUL)

```
Anti-pattern:  TH8_ALLOC_MUL(interp, n + 1, sizeof(T))
Why unsafe:    If n == SIZE_MAX, n + 1 wraps to 0 and the
               multiplication silently succeeds with
               0 * sizeof(T) == 0.
Correct form:  TH8_ALLOC_STR_MUL(interp, n, sizeof(T))
               -- both the product and the trailing per-element
                  NUL slot are overflow-checked.
```

R-03951-16007
:   Allocations of the form `(n+1) * sizeof(T)` SHALL use `TH8_ALLOC_STR_MUL(interp, n, sizeof(T))`, not `TH8_ALLOC_MUL(interp, n+1, sizeof(T))`.

#### 42a.4  Pre-computed packed-record size (`A*X + B*Y + C` outside the macro)

```
Anti-pattern:  nA = nE * sizeof(char *)
                  + nE * sizeof(size_t) + nStr;
               TH8_ALLOC(interp, nA);
Why unsafe:    Three unchecked size_t arithmetic operations
               (two multiplications and two additions) run before
               the safe-alloc sees a single integer.  Any one of
               them can wrap; subsequent steps then propagate the
               wrong total.
Correct form:  TH8_ALLOC_MUL_ADD2(interp, nE, sizeof(char *),
                                         nE, sizeof(size_t), nStr)
               -- each of the four internal steps (two products,
                  two sums) is overflow-checked independently.
```

R-18934-08742
:   `TH8_ALLOC_MUL_ADD2(interp, a, b, c, d, e)` SHALL be used for any `(a*b) + (c*d) + e` allocation pattern, in preference to pre-computing the size in plain C arithmetic.

#### 42a.5  General principle

R-38392-47888
:   No allocation request size SHALL be computed using plain C `size_t` arithmetic on caller-supplied or attacker-influenced values.  Every contributing arithmetic step SHALL be performed by an overflow-checked primitive (`TH8_ALLOC_*` macro, `Th8_SafeAlloc*` function, or `Th8_SafeMul`/`Th8_SafeAdd`).


### 42b  Public Safe-Math Helpers

For size calculations that do not directly result in an allocation
(e.g., offsets, indices, capacity doublings, or composite sizes that
must be passed to a single later allocation), TH8 exposes the
underlying overflow-checked primitives as public functions.

#### 42b.1  Th8_SafeMul

R-26323-64339
:   `Th8_SafeMul(interp, a, b, &out)` SHALL store `a * b` in `*out` and return `TH8_OK` on success, or return `TH8_ERROR` (leaving `*out` unchanged) if the multiplication would overflow `size_t`.  No allocation is performed.  `interp` MAY be NULL.  If `&out` is NULL, the function SHALL return `TH8_ERROR` without performing any computation.

#### 42b.2  Th8_SafeAdd

R-40857-48614
:   `Th8_SafeAdd(interp, a, b, &out)` SHALL store `a + b` in `*out` and return `TH8_OK` on success, or return `TH8_ERROR` (leaving `*out` unchanged) if the addition would overflow `size_t`.  Other semantics identical to `Th8_SafeMul`.

#### 42b.3  Th8_SafeAllocStrAdd

R-51492-41171
:   `Th8_SafeAllocStrAdd(interp, k, n, file, line)` SHALL allocate `k + n + 1` bytes via `Th8_SafeAlloc`, returning NULL if either `(k + n)` or `((k + n) + 1)` would overflow `size_t` or if the allocation itself fails.  This is the function backing the `TH8_ALLOC_STR_ADD` macro.

#### 42b.4  Th8_SafeAllocStrMul

R-41471-25493
:   `Th8_SafeAllocStrMul(interp, n, sz, file, line)` SHALL allocate `(n + 1) * sz` bytes via `Th8_SafeAlloc`, returning NULL if either `(n * sz)` or `((n * sz) + sz)` would overflow `size_t` or if the allocation itself fails.  This is the function backing the `TH8_ALLOC_STR_MUL` macro.

#### 42b.5  Th8_SafeAllocMulAdd2

R-55878-51290
:   `Th8_SafeAllocMulAdd2(interp, a, b, c, d, e, file, line)` SHALL allocate `(a*b) + (c*d) + e` bytes via `Th8_SafeAlloc`, performing four sequential overflow checks: `(a*b)`, `(c*d)`, the sum of the two products, and the addition of `e` to that sum.  Returns NULL on overflow at any step or on allocation failure.  This is the function backing the `TH8_ALLOC_MUL_ADD2` macro.


### 43  Deferred Deletion (Pending-Delete Queue)

When script code is executing (`nEvalDepth > 0`), deleting a command
or namespace immediately could free memory still referenced by the
call stack.  The pending-delete queue defers the destructive portion
of the deletion until the eval stack fully unwinds.

#### 43.1  Command Deletion During Eval

R-21377-38127
:   When `Th8_RenameCommand` is called with an empty new name while `nEvalDepth > 0`, the command SHALL be removed from the namespace hash table immediately (preventing further invocation) and the `xDel` callback and memory free SHALL be deferred until `nEvalDepth` returns to 0.

#### 43.2  Namespace Deletion During Eval

R-11767-56046
:   When `Th8_DeleteNamespace` is called while `nEvalDepth > 0`, the namespace SHALL be detached from the parent namespace's child hash immediately and the recursive free (`th8FreeNamespace`) SHALL be deferred until `nEvalDepth` returns to 0.

#### 43.3  Queue Drain

R-18195-10100
:   The pending-delete queue SHALL be drained (FIFO order) whenever `nEvalDepth` drops to 0 in `th8EvalStateCleanup`, and during `Th8_DeleteInterp` teardown.
R-06325-59148
:   New entries added to the queue during draining (e.g., a command's `xDel` callback deleting another command) SHALL be processed before draining completes.


### 44  List Splitting

#### 44.1  Th8_SplitList Flags

R-26791-59865
:   `Th8_SplitList` SHALL accept an `int flags` parameter as its last argument, where `TH8_LIST_NONE` (0) enables the IR cache and `TH8_LIST_NO_CACHE` (1) bypasses the cache entirely.

#### 44.2  Single-Allocation Return Block

R-63239-00519
:   `Th8_SplitList` SHALL return `*pazElem` and `*panElem` as pointers into a single allocation block.  Only `*pazElem` (the block start) is a valid target for `Th8_Free`; `*panElem` is an interior pointer and MUST NOT be freed separately.


### 45  Secure Variable Persistence

#### 45.1  Th8_SecureSetMasterKey

R-10586-41297
:   `Th8_SecureSetMasterKey(interp, pKey, nKey)` SHALL copy `nKey` bytes from `pKey` into the locked key page (slot 1) as the master encryption key for secure variable persistence.  `nKey` MUST be exactly 32 (AES-256).

#### 45.2  Th8_SecureClearMasterKey

R-06579-42988
:   `Th8_SecureClearMasterKey(interp)` SHALL securely zero the master key in slot 1 of the locked key page.  Subsequent `[secure save]` and `[secure load]` operations SHALL fail until a new master key is set.

#### 45.3  Th8_EnableSecurePersist

R-54649-42492
:   `Th8_EnableSecurePersist(interp, bEnable)` SHALL enable (bEnable=1) or disable (bEnable=0) the secure persistence security gate using the dual-field random-token pattern.

#### 45.4  Th8_IsSecurePersistEnabled

R-60041-55699
:   `Th8_IsSecurePersistEnabled(interp)` SHALL return non-zero if the secure persistence gate is enabled, zero otherwise.


### 46  Introspection Extensions

#### 46.1  Th8_ListAppendExpansions

R-27910-15138
:   `Th8_ListAppendExpansions(interp, pzList, pnList, zPat, nPat)` SHALL append the tag names of all expansion operators registered in the current namespace to the list, optionally filtered by a glob pattern.

#### 46.2  Th8_ListAppendBreakpoints

R-11879-05897
:   `Th8_ListAppendBreakpoints(interp, pzList, pnList)` SHALL append a flat list of breakpoint descriptions (ID, scriptName, lineNumber triples) from the interpreter's breakpoint table.


### 47  Memory Recovery Platform

#### 47.1  Th8_GetMemPlatform

R-41372-31985
:   `Th8_GetMemPlatform()` SHALL return a static platform struct whose only non-NULL callback is `xNeedMemory`.  When invoked, the callback SHALL clear the interpreter's IR cache, reject requests exceeding `TH8_MX_ALLOC` bytes, and retry allocation via the interpreter's current `xMalloc`.


### 48  Sensitive Interpreter Result

The interpreter result can be marked sensitive when it contains plaintext derived from secret material (e.g. a decrypted `[secure]` variable, an unwrapped session token).  Sensitive results receive two complementary protections:

1.  **Secure-zero on overwrite.**  The result buffer is zeroed via `Th8_SecureZero` before being freed or replaced, regardless of whether the buffer lives in regular heap or in a protected backing region.
2.  **In-place enforcement.**  The result MUST be consumed in place via `Th8_GetResult`; ownership-transferring APIs (`Th8_TakeResult`) refuse for sensitive results to prevent the plaintext from escaping the interpreter into caller-managed memory.

When TH8 is built with `TH8_ENABLE_CRYPTOGRAPHY`, sensitive results may additionally be backed by a per-interpreter mlock'd, guard-paged region (a `Th8_ProtectedRegion`) so the plaintext is never written to swap, never appears in core dumps, and is flanked by `PROT_NONE` guard pages that catch buffer overruns into adjacent memory.

#### 48.1  Th8_IsResultSensitive

R-39669-03059
:   `Th8_IsResultSensitive(interp)` SHALL return non-zero if and only if the current interpreter result is marked sensitive.  This function SHALL be available in all builds; on builds without `TH8_ENABLE_CRYPTOGRAPHY` it SHALL always return 0 because no API is available to set the flag.

#### 48.2  Th8_TakeResult refusal for sensitive results

R-32296-63095
:   `Th8_TakeResult` SHALL refuse to detach a sensitive interpreter result, returning NULL and replacing the interpreter result with a non-sensitive error message describing the refusal.  Callers that need a sensitive value MUST consume it in place via `Th8_GetResult`.

#### 48.3  Th8_MarkResultSensitive

R-55462-50989
:   `Th8_MarkResultSensitive(interp)` SHALL set the sensitive flag on the current interpreter result without moving the result buffer.  When the result is later overwritten or cleared, the buffer SHALL be securely zeroed via `Th8_SecureZero` before being freed.  Available only when TH8 is built with `TH8_ENABLE_CRYPTOGRAPHY`.

#### 48.4  Th8_SetResultSensitive

R-47618-29168
:   `Th8_SetResultSensitive(interp, z, n)` SHALL set the interpreter result to a copy of `z` (`n` bytes) stored in a per-interpreter mlock'd, guard-paged backing region (a `Th8_ProtectedRegion`).  The result SHALL be automatically marked sensitive.  The protected region SHALL be allocated lazily on first use and reused for the lifetime of the interpreter; subsequent overwrites or clears SHALL securely zero the region's data area in place rather than freeing it.  If `n` exceeds the protected region's usable capacity (one OS page minus the canary), the function SHALL return `TH8_ERROR` with a descriptive error result.  Available only when TH8 is built with `TH8_ENABLE_CRYPTOGRAPHY`.

#### 48.5  Lifetime of the protected backing region

R-19055-10405
:   The per-interpreter `Th8_ProtectedRegion` backing sensitive results SHALL be freed only when the interpreter is destroyed via `Th8_DeleteInterp`; it SHALL NOT be freed on every result transition, since the cost of `mlock`/`mprotect` is amortized across the interpreter's lifetime.

#### 48.6  Secure variable decrypt SHALL write directly into protected memory

R-09780-55484
:   When `[secure]` decrypts a variable's plaintext for delivery to the interpreter result, the decryption output SHALL be written directly into the per-interpreter `Th8_ProtectedRegion` and SHALL be made the live sensitive result without an intermediate copy through pageable memory.  The plaintext SHALL NOT transit a regular-heap staging buffer between decryption and result finalization.

#### 48.7  Secure persistence save SHALL use the same protected region as scratch

R-41752-58484
:   When `[secure save]` re-encrypts a variable's plaintext under the master key for persistence, the decrypted plaintext SHALL be staged in the per-interpreter `Th8_ProtectedRegion` (the same region used to back sensitive results) rather than in pageable heap.  Implementations SHALL `Th8_ClearResult` any active result before reusing the region as scratch and SHALL securely zero the region's data area when the re-encryption completes.

#### 48.8  Secure persistence load SHALL decrypt directly into the protected region

R-34573-24063
:   When `[secure load]` decrypts a master-key-encrypted blob fetched from the key-value backend, the AES-256-GCM decryption output SHALL be written directly into the per-interpreter `Th8_ProtectedRegion`.  The plaintext SHALL be passed by pointer (into the region's data area) to `th8SecureVarCreate` for re-encryption under a fresh per-variable key; it SHALL NOT be staged in a regular-heap buffer.  Implementations SHALL `Th8_ClearResult` any active result before overwriting the region and SHALL securely zero the region's data area before returning.

#### 48.9  Sensitivity propagates through value-preserving operations

R-54106-02907
:   The sensitive classification of a value SHALL be a value-level tag that
:   travels with the value, and SHALL propagate through value-preserving
:   operations -- command substitution, variable substitution, list
:   construction, string concatenation, and result or variable storage -- in
:   the same conservative manner as taint, so that any value derived from
:   sensitive plaintext remains marked sensitive.  A sensitive value
:   substituted into another command's word, or stored in a variable and later
:   read, SHALL still report as sensitive via `Th8_IsResultSensitive` at the
:   boundary, so that rendering, export, and `Th8_TakeResult` detachment
:   boundaries continue to suppress it.  Sensitivity and taint are independent:
:   a value may be sensitive, tainted, both, or neither.

#### 48.10  Sensitive values SHALL NOT egress as plaintext

R-26595-38386
:   A sensitive value SHALL NOT be emitted from the interpreter through any
:   egress boundary -- an output channel, the host output callback, an error
:   diagnostic, the interactive result echo, or a network request -- as
:   plaintext.  Any such attempt SHALL be rejected or redacted before any byte
:   of the value leaves the process, with a fixed diagnostic that discloses no
:   portion of the value.


### 48a  Secure Variable Data-Flow Reference

This section is informative.  It traces the bytes of a secure variable's plaintext from the database (key-value backend) through the cryptographic pipeline into the interpreter result, showing precisely which transformations occur in pageable memory and which occur in the per-interpreter protected region.  The architectural invariant is that **decrypted plaintext never resides in pageable memory** — at every point in the pipeline the plaintext is either inside a `Th8_ProtectedRegion` (mlock'd, guard-paged, marked `MADV_DONTDUMP`/`MADV_WIPEONFORK` on Linux), or it is encrypted.

#### 48a.1  Persistent storage to live result

The complete flow from a master-key-encrypted blob in the KV backend (`[secure load]`, then `$var` read) is shown in the diagram below.  Solid arrows are byte transfers; **bold** boxes are protected memory; plain boxes are pageable heap; the dashed boundary marks the pageable/protected divide.

```
   PAGEABLE HEAP (regular allocation)
   ............................................................
   .                                                          .
   .   ┌─────────────────────┐                                .
   .   │ KV backend          │                                .
   .   │ (SQLite / env / KV) │                                .
   .   │                     │                                .
   .   │   master-encrypted  │                                .
   .   │   blob              │                                .
   .   └──────────┬──────────┘                                .
   .              │                                           .
   .              │  Th8_KeyValue(GET, "th8:secure:X")        .
   .              ▼                                           .
   .   ┌─────────────────────┐                                .
   .   │ heap blob (master-  │                                .
   .   │ encrypted ciphertxt)│                                .
   .   └──────────┬──────────┘                                .
   .              │                                           .
.... CROSSING BOUNDARY (load): ciphertext in, plaintext goes  ....
   :              │            directly into protected memory :
   :              │                                           :
   :              │ AES-256-GCM decrypt under                 :
   :              │  master key (slot 1 of mlock'd            :
   :              │  key page).  Output destination           :
   :              │  is pProtectedResult's data area;         :
   :              │  plaintext NEVER lands in heap.           :
   :              ▼                                           :
   :   ┌───────────────────────────────────────────────┐      :
   :   │ pProtectedResult (used as scratch by load)    │      :
   :   │  ┌──────────┬────────────────────────────┐    │      :
   :   │  │ canary   │ PLAINTEXT (master-decrypt) │    │      :
   :   │  └──────────┴────────┬───────────────────┘    │      :
   :   └──────────────────────┼────────────────────────┘      :
   :                          │ th8SecureVarCreate            :
   :                          │  reads plaintext, encrypts    :
   :                          │  under fresh per-variable     :
   :                          │  key into Th8_SecureVarData   :
   :                          │  in heap (encrypted, so OK).  :
   :                          │ Region is then secure-zeroed  :
   :                          │  before this function returns :
   :..........................┼................................
   .                          │                               .
   .                          ▼                               .
   .     ┌─────────────────────┐                              .
   .     │ Th8_SecureVarData   │                              .
   .     │  zCipher: per-var   │ per-var-encrypted            .
   .     │   encrypted blob    │ ciphertext lives here        .
   .     │  iSlot: key index   │ for the variable's           .
   .     └──────────┬──────────┘ in-memory lifetime           .
   .                │                                         .
   .                │ Th8_GetVar of secure var                .
   .                │   ⇒ th8SecureGetVar                     .
   .                │                                         .
   .                │ Th8_ClearResult(interp)                 .
   .                │ th8SecureDecrypt(... pProtected         .
   .                │     Result ...) writes plaintext        .
   .                │ DIRECTLY into the region:               .
   .                │                                         .
.... CROSSING BOUNDARY (read): ciphertext in, plaintext       ....
   :                │          becomes the live sensitive     :
   :                │          result                         :
   :                ▼                                         :
   :   ┌───────────────────────────────────────────────┐      :
   :   │ pProtectedResult (Th8_ProtectedRegion)        │      :
   :   │  ┌──────────┬────────────────────────────┐    │      :
   :   │  │ canary   │ PLAINTEXT BYTES            │    │      :
   :   │  │ 8 bytes  │ (EVP_DecryptUpdate output) │    │      :
   :   │  └──────────┴────────┬───────────────────┘    │      :
   :   │                      │                        │      :
   :   │  th8FinalizeSensitiveResult assigns:          │      :
   :   │     interp->zResult ─┘                        │      :
   :   │     interp->nResult = nLen                    │      :
   :   │     interp->bResultBorrowed   = 1             │      :
   :   │     interp->bResultSensitive  = 1             │      :
   :   │                                               │      :
   :   │  Flanked by PROT_NONE / PAGE_NOACCESS guard   │      :
   :   │  pages on both sides; mlock'd against swap;   │      :
   :   │  MADV_DONTDUMP excludes from core dumps;      │      :
   :   │  MADV_WIPEONFORK clears on fork().            │      :
   :   └───────────────────────────────────────────────┘      :
   :                          │                               :
   :                          │ Th8_GetResult(interp, &n)     :
   :                          │   returns pointer INTO        :
   :                          │   the region (in-place use).  :
   :                          │                               :
   :                          │ Th8_TakeResult REFUSES        :
   :                          │   (returns NULL and replaces  :
   :                          │    result with non-sensitive  :
   :                          │    error).  Plaintext cannot  :
   :                          │    be detached.               :
   :                                                          :
   :   PROTECTED MEMORY (mlock'd, guard pages, no swap,       :
   :                    no core dump, wiped on fork)          :
   :..........................................................:
```

Both pageable-to-protected boundary crossings (the master-key decrypt during `[secure load]` and the per-variable decrypt during `$var` read) write their plaintext output **directly** into `pProtectedResult`.  At no point does decrypted plaintext live in pageable heap.

#### 48a.2  Result lifetime and zero-on-overwrite

The plaintext remains in the region until one of the following occurs, after which the region's data area is securely zeroed in place (the region itself is retained for reuse):

-   `Th8_SetResult` / `Th8_SetResultStatic` / `Th8_ClearResult` / `th8SetResultBorrowed` is called (any result mutation).
-   The interpreter is destroyed via `Th8_DeleteInterp` (the region is freed entirely).

The data area is never freed and reallocated on a per-result basis; the same mlock'd page is used for the lifetime of the interpreter.  This amortizes the page-aligned allocation cost and the mlock/mprotect syscalls across all sensitive result transitions.

#### 48a.3  Save flow (variable to backend)

The reverse direction (`[secure save] var`) reuses the same protected region as transient scratch:

1.  `Th8_ClearResult(interp)` — secure-zero any active sensitive result.
2.  `th8SecureDecrypt` writes the plaintext into the protected region.
3.  PKCS#7 pad bytes from the region into a heap-allocated blob buffer.
4.  AES-256-GCM in-place encrypt under the master key (heap blob now contains ciphertext, plaintext window in heap is the few CPU cycles between memcpy and `EVP_EncryptUpdate` finishing).
5.  `Th8_KeyValue(SET, ...)` persists the encrypted blob to the KV backend.
6.  `Th8_SecureZero` the region's data area; `Th8_Free` the heap blob.

The plaintext lives in the protected region throughout, never reaching pageable heap as plaintext.

---

---

## Appendix A -- Public C API Reference Catalog

The preceding sections specify the load-bearing entry points of
the TH8 public C API with full normative requirements.  This
appendix is a **complete reference catalog** of the remaining
`TH8_API` symbols exported from `src/th8.h`: 176 additional
functions whose contracts are documented inline in the header
file but do not yet carry a section-length specification in this
document.

Each entry gives the function name, a one-line summary extracted
from the header's declaration-adjacent comment, and its C
prototype.  Full argument-level contracts, error conditions, and
lifetime guarantees are in the header comment; this catalog is a
navigation aid so no public API surface is entirely undocumented.

Future revisions of this specification will promote catalog
entries into full sections as their requirements are marked and
tested; for now, embedders MAY treat the header comment as the
authoritative contract for a catalog entry.

### Alphabetic listing

* **`Th8_AttrFlagsChange`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API int Th8_AttrFlagsChange( Th8_Interp *interp, Th8_AfMap *pMap, const char *zChange, size_t nChange, th8_int64_...;`

* **`Th8_AttrFlagsFormat`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API int Th8_AttrFlagsFormat( Th8_Interp *interp, const Th8_AfMap *pMap, int bLegacy, int bCompact, int bSpace, in...;`

* **`Th8_AttrFlagsHave`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API int Th8_AttrFlagsHave( const Th8_AfMap *pMap, th8_int64_t key, const char *zHave, size_t nHave, int bAll, int...;`

* **`Th8_AutoPathSearch`** -- Search ::auto_path for pkgIndex.th8 files and source them
  Prototype: `TH8_API int Th8_AutoPathSearch(Th8_Interp *interp, char *zAuto, size_t nAuto);`

* **`Th8_ByteToUtf16Col`** -- Convert a byte offset nByte within the line zLine (nLine bytes)
  Prototype: `TH8_API int Th8_ByteToUtf16Col(const char *zLine, size_t nLine, int nByte);`

* **`Th8_CallSubCommand`** -- Dispatch to a sub-command.  argv[1] is matched against the zName
  Prototype: `TH8_API int Th8_CallSubCommand( Th8_Interp *interp, void *ctx, int argc, const char **argv, size_t *argl, const Th8_S...;`

* **`Th8_ClonePlatform`** -- Allocate a new mutable copy of a const platform table.
  Prototype: `TH8_API Th8_Platform *Th8_ClonePlatform(const Th8_Platform *pSrc);`

* **`Th8_Complete`** -- Test whether zScript (nScript bytes) is a "complete" Tcl script,
  Prototype: `TH8_API int Th8_Complete(const char *zScript, size_t nScript);`

* **`Th8_CoroCreate`** -- Create a coroutine named zName.  Evaluates zBody; if the body
  Prototype: `TH8_API int Th8_CoroCreate( Th8_Interp *interp, const char *zName, size_t nName, const char *zBody, size_t nBody);`

* **`Th8_CoroYield`** -- Suspend the current coroutine and return zValue to the
  Prototype: `TH8_API int Th8_CoroYield(Th8_Interp *interp, const char *zValue, size_t nValue);`

* **`Th8_CreateCommand`** -- Register a new command named zName (NUL-terminated) in the
  Prototype: `TH8_API int Th8_CreateCommand( Th8_Interp *interp, const char *zName, Th8_CommandProc xProc, void *pContext, void (*x...;`

* **`Th8_CreateMathFunc`** -- Register a math function for use in [expr].  The function is
  Prototype: `TH8_API int Th8_CreateMathFunc( Th8_Interp *interp, const char *zName, size_t nName, int nArg, Th8_MathFuncProc xProc...;`

* **`Th8_DataExists`** -- Test whether named data exists via the platform's xDataExists
  Prototype: `TH8_API int Th8_DataExists( Th8_Interp *interp, const char *zName, size_t nName, int *pAttrs);`

* **`Th8_DeclareSystemVar`** -- Mark a variable name as a system variable.  For arrays,
  Prototype: `TH8_API int Th8_DeclareSystemVar(Th8_Interp *interp, const char *zName, size_t nName);`

* **`Th8_DeleteCommand`** -- Delete a command by its unique token.  The command is found
  Prototype: `TH8_API int Th8_DeleteCommand(Th8_Interp *interp, th8_uint64_t token);`

* **`Th8_DeleteMathFunc`** -- Remove a math function from the interpreter.
  Prototype: `TH8_API int Th8_DeleteMathFunc(Th8_Interp *interp, const char *zName, size_t nName);`

* **`Th8_DnsResolve`** -- DNSSEC-validating DNS lookup.  Wraps the platform's
  Prototype: `TH8_API int Th8_DnsResolve( Th8_Interp *interp, const char *zName, size_t nName, int eType, Th8_DnsResult **ppResult);`

* **`Th8_DnsResolveFree`** -- Release a Th8_DnsResult returned by Th8_DnsResolve.
  Prototype: `TH8_API void Th8_DnsResolveFree(Th8_Interp *interp, Th8_DnsResult *pResult);`

* **`Th8_DoesEnvExist`** -- Return non-zero if the named environment variable exists.
  Prototype: `TH8_API int Th8_DoesEnvExist(Th8_Interp *interp, const char *zName);`

* **`Th8_DrainQueueEvents`** -- Drain and invoke all pending events across every pState
  Prototype: `TH8_API int Th8_DrainQueueEvents(Th8_Interp *interp);`

* **`Th8_EnableUnload`** -- Enable or disable the [unload] command.  The flags
  Prototype: `TH8_API int Th8_EnableUnload(Th8_Interp *interp, int flags);`

* **`Th8_ErrorMessage`** -- Set the interpreter result to an error message composed of
  Prototype: `TH8_API int Th8_ErrorMessage( Th8_Interp *interp, const char *zPre, const char *z, size_t n);`

* **`Th8_EvalDownlevel`** -- Evaluate a script in the call frame that was active just
  Prototype: `TH8_API int Th8_EvalDownlevel( Th8_Interp *interp, const char *zProg, size_t nProg, const char *zName, size_t nName);`

* **`Th8_ExistsArrayVar`** -- Test whether zVar names an array variable (as opposed to a scalar).
  Prototype: `TH8_API int Th8_ExistsArrayVar(Th8_Interp *interp, const char *zVar, size_t nVar);`

* **`Th8_Exit`** -- Set the interpreter's exit flag.  Th8_Ready will return
  Prototype: `TH8_API void Th8_Exit(Th8_Interp *interp);`

* **`Th8_FaultConfigInit`** -- Initialize a fault config to safe defaults (no faults).
  Prototype: `TH8_API void Th8_FaultConfigInit(Th8_FaultConfig *pCfg);`

* **`Th8_FaultCtxSize`** -- Return sizeof(Th8_FaultCtx) so callers can stack-allocate
  Prototype: `TH8_API size_t Th8_FaultCtxSize(void);`

* **`Th8_FindExpansion`** -- Look up an expansion operator by tag.  Searches the current
  Prototype: `TH8_API int Th8_FindExpansion( Th8_Interp *interp, const char *zTag, size_t nTag, Th8_ExpansionProc *pxProc, void **p...;`

* **`Th8_FindInCache`** -- Look up or create a cache entry for the string z (n bytes,
  Prototype: `TH8_API Th8_Value * Th8_FindInCache(Th8_Interp *interp, int cacheType, const char *z, size_t n);`

* **`Th8_FindMathFunc`** -- Look up a math function by name.
  Prototype: `TH8_API int Th8_FindMathFunc( Th8_Interp *interp, const char *zName, size_t nName, int *pnArg, Th8_MathFuncProc *pxPr...;`

* **`Th8_FindNamespace`** -- Look up the namespace zName (nName bytes, or TH8_NOLEN).  If
  Prototype: `TH8_API int Th8_FindNamespace( Th8_Interp *interp, const char *zName, size_t nName, int bCreate);`

* **`Th8_GetAllocBytes`** -- Returns the current total bytes allocated by this interp.
  Prototype: `TH8_API size_t Th8_GetAllocBytes(Th8_Interp *interp);`

* **`Th8_GetAllocLimit`** -- Returns the current allocation limit (0 = unlimited).
  Prototype: `TH8_API size_t Th8_GetAllocLimit(Th8_Interp *interp);`

* **`Th8_GetAndroidPlatform`** -- Return a Th8_Platform with Android-specific overrides.  Android
  Prototype: `TH8_API const Th8_Platform *Th8_GetAndroidPlatform(void);`

* **`Th8_GetBasePath`** -- Return the current base path as a NUL-terminated string,
  Prototype: `TH8_API const char *Th8_GetBasePath(void);`

* **`Th8_GetCacheStats`** -- Retrieve IR cache performance counters.  Any output pointer
  Prototype: `TH8_API void Th8_GetCacheStats( Th8_Interp *interp, th8_uint64_t *pnHit, th8_uint64_t *pnMiss, th8_uint64_t *pnEvict);`

* **`Th8_GetCommandInfo`** -- Look up the command named zName (nName bytes, or TH8_NOLEN)
  Prototype: `TH8_API int Th8_GetCommandInfo( Th8_Interp *interp, const char *zName, size_t nName, Th8_CommandProc *pxProc, void **...;`

* **`Th8_GetCosmopolitanPlatform`** -- Return a Th8_Platform for Cosmopolitan Libc (Actually Portable
  Prototype: `TH8_API const Th8_Platform *Th8_GetCosmopolitanPlatform(void);`

* **`Th8_GetCurlPlatform`** -- Return a Th8_Platform with xGetData backed by libcurl.
  Prototype: `TH8_API const Th8_Platform *Th8_GetCurlPlatform(void);`

* **`Th8_GetCurrentNamespace`** -- Return the name of the current namespace (NUL-terminated).
  Prototype: `TH8_API const char *Th8_GetCurrentNamespace(Th8_Interp *interp);`

* **`Th8_GetCwd`** -- Return the current working directory via the platform's
  Prototype: `TH8_API char *Th8_GetCwd(Th8_Interp *interp);`

* **`Th8_GetData`** -- Retrieve named data via the platform's xGetData callback.
  Prototype: `TH8_API int Th8_GetData( Th8_Interp *interp, const char *zName, size_t nName, char **pzOut, size_t *pnOut, int flags);`

* **`Th8_GetEmbeddedKeyTest`** -- Returns a pointer to the embedded 2048-bit test signing key
  Prototype: `TH8_API const unsigned char *Th8_GetEmbeddedKeyTest(size_t *pnData);`

* **`Th8_GetEmbeddedKeyTime`** -- Returns a pointer to the embedded keyTime data (the time
  Prototype: `TH8_API const unsigned char *Th8_GetEmbeddedKeyTime(size_t *pnData);`

* **`Th8_GetEmbeddedKeyring`** -- Return the array of trusted-root keys embedded in this build.
  Prototype: `TH8_API const Th8_KeyringEntry *Th8_GetEmbeddedKeyring(size_t *pnEntries);`

* **`Th8_GetErrorLine`** -- Return the 1-based source line number where the most recent
  Prototype: `TH8_API int Th8_GetErrorLine(Th8_Interp *interp);`

* **`Th8_GetFrameObjv`** -- Retrieve the argument vector for frame level iLevel.  On success,
  Prototype: `TH8_API int Th8_GetFrameObjv( Th8_Interp *interp, int iLevel, int *pArgc, const char ***pArgv, size_t **pArgl);`

* **`Th8_GetInterpThreadId`** -- Return the id of the thread that owns the interpreter (its creating thread); thread-safe (§37a.3).
  Prototype: `TH8_API th8_uint64_t Th8_GetInterpThreadId(Th8_Interp *interp);`

* **`Th8_GetIosPlatform`** -- Return a Th8_Platform with iOS-specific overrides.  iOS shares
  Prototype: `TH8_API const Th8_Platform *Th8_GetIosPlatform(void);`

* **`Th8_GetLibcPlatform`** -- Return a Th8_Platform using only standard C library functions
  Prototype: `TH8_API const Th8_Platform *Th8_GetLibcPlatform(void);`

* **`Th8_GetLine`** -- Return the 1-based source line number currently being evaluated.
  Prototype: `TH8_API int Th8_GetLine(Th8_Interp *interp);`

* **`Th8_GetMacOSPlatform`** -- Return a Th8_Platform with macOS-specific memory management
  Prototype: `TH8_API const Th8_Platform *Th8_GetMacOSPlatform(void);`

* **`Th8_GetMathFuncHash`** -- Return the interpreter's math function registry hash.
  Prototype: `TH8_API Th8_Hash *Th8_GetMathFuncHash(Th8_Interp *interp);`

* **`Th8_GetMimallocPlatform`** -- Return a Th8_Platform backed by Microsoft's mimalloc allocator.
  Prototype: `TH8_API const Th8_Platform *Th8_GetMimallocPlatform(void);`

* **`Th8_GetNullIoPlatform`** -- Return a Th8_Platform with null I/O: all I/O callbacks are
  Prototype: `TH8_API const Th8_Platform *Th8_GetNullIoPlatform(void);`

* **`Th8_GetOverflowCheck`** -- Returns 1 if overflow checking is enabled, 0 if disabled.
  Prototype: `TH8_API int Th8_GetOverflowCheck(Th8_Interp *interp);`

* **`Th8_GetPackageHash`** -- Return the interpreter's package registry hash table.  Used by
  Prototype: `TH8_API struct Th8_Hash *Th8_GetPackageHash(Th8_Interp *interp);`

* **`Th8_GetPackageUnknown`** -- Return the name of the "package unknown" handler command, or
  Prototype: `TH8_API const char *Th8_GetPackageUnknown(Th8_Interp *interp);`

* **`Th8_GetParentPid`** -- Return the parent process ID via the platform's xGetParentPid
  Prototype: `TH8_API int Th8_GetParentPid(Th8_Interp *interp);`

* **`Th8_GetPid`** -- Return the process ID via the platform's xGetPid callback.
  Prototype: `TH8_API int Th8_GetPid(Th8_Interp *interp);`

* **`Th8_GetPolicyCallback`** -- Retrieve the current policy callback and context.
  Prototype: `TH8_API void Th8_GetPolicyCallback( Th8_Interp *interp, Th8_PolicyProc *pxProc, void **ppCtx);`

* **`Th8_GetPosixPlatform`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API const Th8_Platform *Th8_GetPosixPlatform(void);`

* **`Th8_GetPublicKeyTest`** -- Load the embedded test key as a parsed Th8_RsaKey.  The
  Prototype: `TH8_API Th8_RsaKey *Th8_GetPublicKeyTest(Th8_Interp *interp);`

* **`Th8_GetPublicKeyTestToken`** -- Returns the 16-character hex public key token for the
  Prototype: `TH8_API int Th8_GetPublicKeyTestToken(Th8_Interp *interp, char zOut[17]);`

* **`Th8_GetResultLimit`** -- Return the current result size limit.  Returns 0 if disabled.
  Prototype: `TH8_API size_t Th8_GetResultLimit(Th8_Interp *interp);`

* **`Th8_GetRootPath`** -- Return the filesystem root (mount point) for a given path via
  Prototype: `TH8_API int Th8_GetRootPath( Th8_Interp *interp, const char *zPath, size_t nPath, char *zBuf, size_t nBuf);`

* **`Th8_GetSourceName`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API const char *Th8_GetSourceName(Th8_Interp *interp, size_t *pnName);`

* **`Th8_GetStepCount`** -- Return the number of steps executed since the last reset.
  Prototype: `TH8_API th8_int64_t Th8_GetStepCount(Th8_Interp *interp);`

* **`Th8_GetStepLimit`** -- Return the current step limit.  Returns 0 if step counting
  Prototype: `TH8_API th8_int64_t Th8_GetStepLimit(Th8_Interp *interp);`

* **`Th8_GetStepMode`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API int Th8_GetStepMode(Th8_Interp *interp);`

* **`Th8_GetStubs`** -- Return a pointer to the interpreter's stubs table (opaque).
  Prototype: `TH8_API const void *Th8_GetStubs(Th8_Interp *interp);`

* **`Th8_GetThreadId`** -- Return the CALLING thread's ID via the platform's xGetThreadId; thread-safe (§37a.3).
  Prototype: `TH8_API th8_uint64_t Th8_GetThreadId(Th8_Interp *interp);`

* **`Th8_GetTimeMs`** -- Get the current time in milliseconds via the platform's xTimeMs
  Prototype: `TH8_API int Th8_GetTimeMs(Th8_Interp *interp, th8_int64_t *pMs);`

* **`Th8_GetTimeUs`** -- Get the current monotonic time in microseconds via the
  Prototype: `TH8_API int Th8_GetTimeUs(Th8_Interp *interp, th8_int64_t *pUs);`

* **`Th8_GetWin32Platform`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API const Th8_Platform *Th8_GetWin32Platform(void);`

* **`Th8_HashDelete`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void Th8_HashDelete(Th8_Interp *interp, Th8_Hash *pHash);`

* **`Th8_HashFind`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API Th8_HashEntry *Th8_HashFind( Th8_Interp *interp, Th8_Hash *pHash, const char *zKey, size_t nKey, int op);`

* **`Th8_HashIterate`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void Th8_HashIterate( Th8_Interp *interp, Th8_Hash *pHash, int (*xCallback)(Th8_HashEntry *, void *), void *p...;`

* **`Th8_HashIterateOrdered`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void Th8_HashIterateOrdered( Th8_Interp *interp, Th8_Hash *pHash, int (*xCallback)(Th8_HashEntry *, void *), ...;`

* **`Th8_HashNew`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API Th8_Hash *Th8_HashNew(Th8_Interp *interp);`

* **`Th8_HashRemove`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void Th8_HashRemove( Th8_Interp *interp, Th8_Hash *pHash, const char *zKey, size_t nKey);`

* **`Th8_InstallSignedPolicy`** -- Install the signed-only policy callback.  When the
  Prototype: `TH8_API int Th8_InstallSignedPolicy(Th8_Interp *interp, void **ppCtx);`

* **`Th8_IntCmpXchg`** -- Atomic integer compare-and-exchange via the global platform's
  Prototype: `TH8_API int Th8_IntCmpXchg( Th8_Interp *interp, volatile int *pTarget, int iExchange, int iComparand);`

* **`Th8_Int64CmpXchg`** -- 64-bit atomic compare-and-exchange via the platform's xIntCmpXchg64; thread-safe.
  Prototype: `TH8_API th8_uint64_t Th8_Int64CmpXchg( Th8_Interp *interp, volatile th8_uint64_t *pTarget, th8_uint64_t iExchange, th8_uint64_t iComparand);`

* **`Th8_IsBeingUnwound`** -- Check whether an unwind-style cancellation is in progress.
  Prototype: `TH8_API int Th8_IsBeingUnwound(Th8_Interp *interp);`

* **`Th8_IsBigintEnabled`** -- Returns 1 if bigint is enabled for this interpreter, 0 if
  Prototype: `TH8_API int Th8_IsBigintEnabled(Th8_Interp *interp);`

* **`Th8_IsCanceled`** -- Check whether the interpreter has a pending cancellation.
  Prototype: `TH8_API int Th8_IsCanceled(Th8_Interp *interp, int flags);`

* **`Th8_IsExited`** -- Check whether the interpreter's exit flag is set.  Returns
  Prototype: `TH8_API int Th8_IsExited(Th8_Interp *interp);`

* **`Th8_IsLoadEnabled`** -- Returns non-zero if binary loading is currently enabled
  Prototype: `TH8_API int Th8_IsLoadEnabled(Th8_Interp *interp);`

* **`Th8_IsSuspended`** -- Return non-zero if the interpreter has a pending suspension
  Prototype: `TH8_API int Th8_IsSuspended(Th8_Interp *interp);`

* **`Th8_IsSystemVar`** -- Returns non-zero if the variable name (or its array base
  Prototype: `TH8_API int Th8_IsSystemVar(Th8_Interp *interp, const char *zName, size_t nName);`

* **`Th8_IterateArraySearches`** -- Diagnostic enumeration of pending [array startsearch] state
  Prototype: `TH8_API int Th8_IterateArraySearches( Th8_Interp *interp, int (*xCallback)( const char *zArray, size_t nArray, const ...;`

* **`Th8_LinkVar`** -- Create a variable link: the local variable zLocal (nLocal bytes)
  Prototype: `TH8_API int Th8_LinkVar( Th8_Interp *interp, const char *zLocal, size_t nLocal, int iFrame, const char *zRemote, size...;`

* **`Th8_ListAppend`** -- Append a single element zElem (nElem bytes, or TH8_NOLEN) to the
  Prototype: `TH8_API int Th8_ListAppend( Th8_Interp *interp, char **pzList, size_t *pnList, const char *zElem, size_t nElem);`

* **`Th8_ListAppendArray`** -- Append all element names of the array variable zArr (nArr bytes)
  Prototype: `TH8_API int Th8_ListAppendArray( Th8_Interp *interp, const char *zArr, size_t nArr, char **pz, size_t *pn);`

* **`Th8_ListAppendCommands`** -- Append all command names in the interpreter to the list at
  Prototype: `TH8_API int Th8_ListAppendCommands(Th8_Interp *interp, char **pz, size_t *pn);`

* **`Th8_ListAppendCommandsMatching`** -- Like Th8_ListAppendCommands, but only appends commands whose
  Prototype: `TH8_API int Th8_ListAppendCommandsMatching( Th8_Interp *interp, char **pz, size_t *pn, Th8_CommandProc xMatch1, Th8_C...;`

* **`Th8_ListAppendGlobalVariables`** -- Like Th8_ListAppendVariables but iterates the global frame.
  Prototype: `TH8_API int Th8_ListAppendGlobalVariables(Th8_Interp *interp, char **pz, size_t *pn);`

* **`Th8_ListAppendLoaded`** -- Append the names of all loaded libraries to the list.
  Prototype: `TH8_API int Th8_ListAppendLoaded(Th8_Interp *interp, char **pz, size_t *pn);`

* **`Th8_ListAppendMathFunctions`** -- Append registered math function names to a list string,
  Prototype: `TH8_API void Th8_ListAppendMathFunctions( Th8_Interp *interp, char **pzList, size_t *pnList, const char *zPat, size_t...;`

* **`Th8_ListAppendNsChildren`** -- Append the names of all child namespaces of zNs (nNs bytes) to
  Prototype: `TH8_API int Th8_ListAppendNsChildren( Th8_Interp *interp, const char *zNs, size_t nNs, char **pz, size_t *pn);`

* **`Th8_ListAppendNsVariables`** -- Append all variable names in the named namespace to the list.
  Prototype: `TH8_API int Th8_ListAppendNsVariables( Th8_Interp *interp, const char *zNs, size_t nNs, char **pz, size_t *pn);`

* **`Th8_ListAppendVarLinks`** -- Append the names of all linked variables (upvar/global)
  Prototype: `TH8_API int Th8_ListAppendVarLinks(Th8_Interp *interp, char **pz, size_t *pn);`

* **`Th8_ListAppendVariables`** -- Append all variable names in the current frame to the list at
  Prototype: `TH8_API int Th8_ListAppendVariables(Th8_Interp *interp, char **pz, size_t *pn);`

* **`Th8_Load`** -- Load a binary into the interpreter.  Checks the load gate,
  Prototype: `TH8_API int Th8_Load( Th8_Interp *interp, const char *zName, size_t nName, const char *zProc, size_t nProc);`

* **`Th8_Memcmp`** -- Compare n bytes of a and b using the platform's xMemcmp.
  Prototype: `TH8_API int Th8_Memcmp(Th8_Interp *interp, const void *a, const void *b, size_t n);`

* **`Th8_Memcpy`** -- Copy n bytes from src to dst using the platform's xMemcpy.
  Prototype: `TH8_API void * Th8_Memcpy(Th8_Interp *interp, void *dst, const void *src, size_t n);`

* **`Th8_Memset`** -- Fill n bytes of dst with the byte value c using the
  Prototype: `TH8_API void *Th8_Memset(Th8_Interp *interp, void *dst, int c, size_t n);`

* **`Th8_NRAddCallback`** -- Push a continuation onto the NRE callback stack.  xProc will
  Prototype: `TH8_API int Th8_NRAddCallback( Th8_Interp *interp, Th8_CallbackProc xProc, void *p0, void *p1, void *p2, void *p3);`

* **`Th8_NREval`** -- Schedule a script for evaluation via the NRE trampoline.  This
  Prototype: `TH8_API int Th8_NREval( Th8_Interp *interp, const char *zProg, size_t nProg, const char *zName, size_t nName);`

* **`Th8_NREvalInFrame`** -- Schedule a script for evaluation in the specified frame using
  Prototype: `TH8_API int Th8_NREvalInFrame( Th8_Interp *interp, int iFrame, const char *zProg, size_t nProg, const char *zName, si...;`

* **`Th8_NormalizePath`** -- Normalize a file path via the platform's xNormalizePath
  Prototype: `TH8_API char * Th8_NormalizePath(Th8_Interp *interp, const char *zPath, size_t nPath);`

* **`Th8_NsEval`** -- Evaluate zScript (nScript bytes) in the context of namespace zNs
  Prototype: `TH8_API int Th8_NsEval( Th8_Interp *interp, const char *zNs, size_t nNs, const char *zScript, size_t nScript);`

* **`Th8_NsExport`** -- Add export glob patterns to the named namespace.  If the
  Prototype: `TH8_API int Th8_NsExport( Th8_Interp *interp, const char *zNs, size_t nNs, const char *zPattern, size_t nPattern);`

* **`Th8_NsImport`** -- Import commands matching zPattern from the namespace named
  Prototype: `TH8_API int Th8_NsImport( Th8_Interp *interp, const char *zPattern, size_t nPattern, int bForce);`

* **`Th8_Pledge`** -- Restrict the process to the given set of pledge promises.
  Prototype: `TH8_API int Th8_Pledge( Th8_Interp *interp, const char *zPromises, const char *zExecPromises);`

* **`Th8_PolicyFindKey`** -- Look up an RSA key by its 16-character hex public key token
  Prototype: `TH8_API const Th8_RsaKey *Th8_PolicyFindKey( Th8_Interp *interp, void *pCtx, const char *zToken, size_t nToken);`

* **`Th8_PolicyGetKeyTokens`** -- Return a Tcl list of all public key tokens currently loaded
  Prototype: `TH8_API int Th8_PolicyGetKeyTokens(Th8_Interp *interp, void *pCtx);`

* **`Th8_PopSourceName`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void Th8_PopSourceName(Th8_Interp *interp);`

* **`Th8_PushSourceName`** -- Th8_PushSourceName / Th8_PopSourceName / Th8_GetSourceName --
  Prototype: `TH8_API void Th8_PushSourceName(Th8_Interp *interp, const char *zName, size_t nName);`

* **`Th8_RegisterExpansion`** -- Register an expansion operator for the given tag in the
  Prototype: `TH8_API int Th8_RegisterExpansion( Th8_Interp *interp, const char *zTag, size_t nTag, Th8_ExpansionProc xProc, void *...;`

* **`Th8_RegisterLanguage`** -- Register the built-in TH8 language commands ([if], [while],
  Prototype: `TH8_API int Th8_RegisterLanguage(Th8_Interp *interp);`

* **`Th8_RemoveSignedPolicy`** -- Remove the signed-only policy callback and free the
  Prototype: `TH8_API void Th8_RemoveSignedPolicy(Th8_Interp *interp, void *pCtx);`

* **`Th8_ReportTaint`** -- If zStr (nStr bytes) is tainted (TH8_TAINT_BIT set in nStr),
  Prototype: `TH8_API int Th8_ReportTaint( Th8_Interp *interp, const char *zTitle, const char *zStr, size_t nStr);`

* **`Th8_ResetCacheStats`** -- Reset all IR cache performance counters to zero.
  Prototype: `TH8_API void Th8_ResetCacheStats(Th8_Interp *interp);`

* **`Th8_ResetCancel`** -- Clear the interpreter's cancellation state (bCanceled flag,
  Prototype: `TH8_API void Th8_ResetCancel(Th8_Interp *interp);`

* **`Th8_ResetExit`** -- Clear the exit flag so the interpreter can resume execution.
  Prototype: `TH8_API void Th8_ResetExit(Th8_Interp *interp);`

* **`Th8_ResetStepCount`** -- Reset the step counter to zero without changing the limit.
  Prototype: `TH8_API void Th8_ResetStepCount(Th8_Interp *interp);`

* **`Th8_RestoreInterp`** -- Re-create built-in state that was destroyed by namespace
  Prototype: `TH8_API int Th8_RestoreInterp(Th8_Interp *interp, int flags);`

* **`Th8_RsaKeyBitLen`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API int Th8_RsaKeyBitLen(const Th8_RsaKey *pKey);`

* **`Th8_RsaKeyFree`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void Th8_RsaKeyFree(Th8_Interp *interp, Th8_RsaKey *pKey);`

* **`Th8_RsaKeyHasPrivate`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API int Th8_RsaKeyHasPrivate(const Th8_RsaKey *pKey);`

* **`Th8_RsaKeyModulus`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API const unsigned char * Th8_RsaKeyModulus(const Th8_RsaKey *pKey, size_t *pn);`

* **`Th8_RsaKeyPrime1`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API const unsigned char * Th8_RsaKeyPrime1(const Th8_RsaKey *pKey, size_t *pn);`

* **`Th8_RsaKeyPrime2`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API const unsigned char * Th8_RsaKeyPrime2(const Th8_RsaKey *pKey, size_t *pn);`

* **`Th8_RsaKeyPrivExp`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API const unsigned char * Th8_RsaKeyPrivExp(const Th8_RsaKey *pKey, size_t *pn);`

* **`Th8_RsaKeyPubExp`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API unsigned int Th8_RsaKeyPubExp(const Th8_RsaKey *pKey);`

* **`Th8_RsaKeyTokenHex`** -- Like Th8_RsaKeyToken but returns the token as a 16-character
  Prototype: `TH8_API int Th8_RsaKeyTokenHex(Th8_Interp *interp, const Th8_RsaKey *pKey, char zOut[17]);`

* **`Th8_SafeAllocAdd`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void *Th8_SafeAllocAdd( Th8_Interp *interp, size_t a, size_t b, const char *zFile, int nLine);`

* **`Th8_SafeAllocMul`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void *Th8_SafeAllocMul( Th8_Interp *interp, size_t a, size_t b, const char *zFile, int nLine);`

* **`Th8_SafeAllocMulAdd`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void *Th8_SafeAllocMulAdd( Th8_Interp *interp, size_t a, size_t b, size_t c, const char *zFile, int nLine);`

* **`Th8_SafeAttemptRealloc`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void *Th8_SafeAttemptRealloc( Th8_Interp *interp, void *p, size_t nByte, const char *zFile, int nLine);`

* **`Th8_SafeRealloc`** -- Th8_SafeRealloc / Th8_SafeAttemptRealloc --
  Prototype: `TH8_API void *Th8_SafeRealloc( Th8_Interp *interp, void *p, size_t nByte, const char *zFile, int nLine);`

* **`Th8_SameFile`** -- Test whether two paths refer to the same physical file via
  Prototype: `TH8_API int Th8_SameFile( Th8_Interp *interp, const char *zName1, size_t nName1, const char *zName2, size_t nName2);`

* **`Th8_SetBasePath`** -- Explicitly set the base path for the TH8 platform layer.
  Prototype: `TH8_API int Th8_SetBasePath(const char *zPath, size_t nPath);`

* **`Th8_SetCommandCopy`** -- Set a deep-copy callback on a command.  Used by proc/nproc
  Prototype: `TH8_API void Th8_SetCommandCopy( Th8_Interp *interp, const char *zName, void *(*xCopy)(Th8_Interp *, void *));`

* **`Th8_SetCwd`** -- Change the current working directory via the platform's
  Prototype: `TH8_API int Th8_SetCwd(Th8_Interp *interp, const char *zPath, size_t nPath);`

* **`Th8_SetErrorLine`** -- Set the error line number.  Normally called by the evaluator
  Prototype: `TH8_API void Th8_SetErrorLine(Th8_Interp *interp, int nLine);`

* **`Th8_SetOverflowCheck`** -- Enable or disable integer overflow checking in [expr].
  Prototype: `TH8_API void Th8_SetOverflowCheck(Th8_Interp *interp, int bEnable);`

* **`Th8_SetPackageUnknown`** -- Set the "package unknown" handler command to zCmd (nCmd bytes,
  Prototype: `TH8_API void Th8_SetPackageUnknown(Th8_Interp *interp, const char *zCmd, size_t nCmd);`

* **`Th8_SetPolicyCallback`** -- Register or remove the unified policy callback that is
  Prototype: `TH8_API void Th8_SetPolicyCallback(Th8_Interp *interp, Th8_PolicyProc xProc, void *pCtx);`

* **`Th8_SetPreLoadCallback`** -- Register a callback that is invoked before each [load]
  Prototype: `TH8_API void Th8_SetPreLoadCallback(Th8_Interp *interp, Th8_PreLoadProc xProc, void *pCtx);`

* **`Th8_SetResultInt`** -- Set the interpreter result to the decimal string representation
  Prototype: `TH8_API int Th8_SetResultInt(Th8_Interp *interp, int iVal);`

* **`Th8_SetResultWideInt`** -- Set the interpreter result to the decimal string representation
  Prototype: `TH8_API int Th8_SetResultWideInt(Th8_Interp *interp, th8_int64_t wVal);`

* **`Th8_SetStepCount`** -- Set the step counter to an explicit value, without changing
  Prototype: `TH8_API void Th8_SetStepCount(Th8_Interp *interp, th8_int64_t nCount);`

* **`Th8_SetVar`** -- Set the variable zVar (nVar bytes) to the value zVal (nVal bytes)
  Prototype: `TH8_API int Th8_SetVar( Th8_Interp *interp, const char *zVar, size_t nVar, const char *zVal, size_t nVal);`

* **`Th8_Sleep`** -- Sleep for nMs milliseconds via the platform's xSleep callback.
  Prototype: `TH8_API void Th8_Sleep(Th8_Interp *interp, int nMs);`

* **`Th8_Strdup`** -- Allocate a copy of z (n bytes) using the interpreter's xMalloc.
  Prototype: `TH8_API char *Th8_Strdup(Th8_Interp *interp, const char *z, size_t n);`

* **`Th8_StringAppend`** -- Append zApp (nApp bytes, or TH8_NOLEN) to the string at
  Prototype: `TH8_API int Th8_StringAppend( Th8_Interp *interp, char **pzStr, size_t *pnStr, const char *zApp, size_t nApp);`

* **`Th8_Strlen`** -- Return the byte length of NUL-terminated string z.  Uses the
  Prototype: `TH8_API size_t Th8_Strlen(Th8_Interp *interp, const char *z);`

* **`Th8_Subst`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API int Th8_Subst(Th8_Interp *interp, const char *z, size_t n, int flags);`

* **`Th8_ThreadDone`** -- (see `src/th8.h` for the header comment)
  Prototype: `TH8_API void Th8_ThreadDone(void);`

* **`Th8_ThreadInit`** -- Th8_ThreadInit / Th8_ThreadDone --
  Prototype: `TH8_API void Th8_ThreadInit(void);`

* **`Th8_ToBoolean`** -- Convert a string to a boolean (0 or 1).  Accepts integers
  Prototype: `TH8_API int Th8_ToBoolean(Th8_Interp *interp, const char *z, size_t n, int *pbVal);`

* **`Th8_ToDouble`** -- Parse z (n bytes, or TH8_NOLEN) as a floating-point number and
  Prototype: `TH8_API int Th8_ToDouble(Th8_Interp *interp, const char *z, size_t n, double *prVal);`

* **`Th8_ToInt`** -- Parse z (n bytes, or TH8_NOLEN) as a decimal, hexadecimal (0x),
  Prototype: `TH8_API int Th8_ToInt(Th8_Interp *interp, const char *z, size_t n, int *piVal);`

* **`Th8_ToWideInt`** -- Like Th8_ToInt but stores the result in a 64-bit integer *pwVal.
  Prototype: `TH8_API int Th8_ToWideInt( Th8_Interp *interp, const char *z, size_t n, th8_int64_t *pwVal);`

* **`Th8_Unload`** -- Unload a previously loaded binary.  Checks the unload gate,
  Prototype: `TH8_API int Th8_Unload( Th8_Interp *interp, const char *zName, size_t nName, const char *zProc, size_t nProc, int bCl...;`

* **`Th8_UnregisterExpansion`** -- Remove the expansion operator for the given tag from the
  Prototype: `TH8_API int Th8_UnregisterExpansion(Th8_Interp *interp, const char *zTag, size_t nTag);`

* **`Th8_UnsetVar`** -- Remove the variable zVar (nVar bytes, or TH8_NOLEN) from the
  Prototype: `TH8_API int Th8_UnsetVar(Th8_Interp *interp, const char *zVar, size_t nVar);`

* **`Th8_Unveil`** -- Reveal a filesystem path with the given permissions.
  Prototype: `TH8_API int Th8_Unveil(Th8_Interp *interp, const char *zPath, const char *zPermissions);`

* **`Th8_Utf8Advance`** -- Advance nChar codepoints from z (within n bytes).  Returns a
  Prototype: `TH8_API const char *Th8_Utf8Advance(const char *z, size_t n, int nChar);`

* **`Th8_Utf8Decode`** -- Decode one UTF-8 codepoint from z (at most n bytes).  Returns
  Prototype: `TH8_API int Th8_Utf8Decode(const char *z, size_t n, int *pnByte);`

* **`Th8_Utf8Encode`** -- Encode codepoint as UTF-8 into the buffer z (which must have
  Prototype: `TH8_API int Th8_Utf8Encode(int codepoint, char *z);`

* **`Th8_Utf8Index`** -- Return a pointer to the first byte of the iChar-th codepoint
  Prototype: `TH8_API const char *Th8_Utf8Index(const char *z, size_t n, int iChar);`

* **`Th8_Utf8Len`** -- Return the number of Unicode codepoints in z (n bytes).
  Prototype: `TH8_API int Th8_Utf8Len(const char *z, size_t n);`

* **`Th8_Utf8Validate`** -- Validate that z (n bytes) is well-formed UTF-8.  Returns
  Prototype: `TH8_API int Th8_Utf8Validate(const char *z, size_t n, int *piOffset);`

* **`Th8_WrongNumArgs`** -- Set the interpreter result to "wrong # args: should be \"zMsg\""
  Prototype: `TH8_API int Th8_WrongNumArgs(Th8_Interp *interp, const char *zMsg);`


